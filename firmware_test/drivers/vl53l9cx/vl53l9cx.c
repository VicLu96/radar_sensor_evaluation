/*
 * Zephyr device driver for the ST VL53L9CX, over I2C.
 *
 * Wraps ST's driver (st/vl53l9.c, used unmodified) and presents the small
 * frame-oriented API in include/vl53l9cx/vl53l9cx.h.
 *
 * Written 2026-09-01 against X-CUBE-53L9A1 v1.0.0. Nothing here has touched
 * hardware — see the header of each staged gate below for what has to be
 * confirmed on the bench and in what order.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT st_vl53l9cx

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include "vl53l9cx_private.h"
#include "vl53l9.h"

LOG_MODULE_REGISTER(vl53l9cx, CONFIG_VL53L9CX_LOG_LEVEL);

/* Geometry per resolution. Source: vl53l9_set_binning() in st/vl53l9.c:455-508
 * and vl53l9_utils_get_resolution() in ST's reference utilities.
 *
 * tx_rows is what actually crosses the bus. The square formats (24x20, 8x6,
 * 4x4) transmit a square array and crop on-device with a y-offset, so the
 * transmitted size is larger than the logical size AND their field of view is
 * not the same as the wide formats'. Any energy-versus-zones comparison must
 * stay inside one family — see docs/plan/st-package-audit.md section 4.
 */
struct res_geom {
	uint8_t binning;
	uint8_t cols;
	uint8_t rows;
	uint8_t tx_rows;
	uint8_t y_offset;
	bool wide; /* false = square format, cropped, different FoV */
};

static const struct res_geom geom[VL53L9CX_RES_COUNT] = {
	[VL53L9CX_RES_4X4]   = { 24,  4,  4,  4, 0, true  },
	[VL53L9CX_RES_8X6]   = { 12,  8,  6,  8, 1, false },
	[VL53L9CX_RES_12X10] = {  8, 12, 10, 10, 0, true  },
	[VL53L9CX_RES_18X14] = {  6, 18, 14, 14, 0, true  },
	[VL53L9CX_RES_24X20] = {  4, 24, 20, 24, 2, false },
	[VL53L9CX_RES_54X42] = {  2, 54, 42, 42, 0, true  },
};

/* ST returns small negative integers and nothing maps them to words. A log
 * line naming the failure beats one carrying a number the reader has to go
 * and look up.
 */
static const char *vl53l9_errstr(int err)
{
	switch (err) {
	case VL53L9_ERROR_NONE:              return "none";
	case VL53L9_ERROR_PLATFORM:          return "PLATFORM (our I2C layer said no)";
	case VL53L9_ERROR_INVALID_PARAM:     return "INVALID_PARAM";
	case VL53L9_ERROR_INVALID_STATE:     return "INVALID_STATE (device not in the expected FSM state)";
	case VL53L9_ERROR_INVALID_OPERATION: return "INVALID_OPERATION";
	case VL53L9_ERROR_TIMEOUT:           return "TIMEOUT (device never reached the expected state)";
	case VL53L9_ERROR_INTERNAL:          return "INTERNAL (ST driver, often a patch-version mismatch)";
	default:                             return "unknown";
	}
}

/* ---------------------------------------------------------------------------
 * AP_CLK
 *
 * The sensor does not acknowledge its I2C address until an external clock is
 * running — 6-27 MHz, 12 MHz on every reference design. A missing clock is
 * indistinguishable from a dead sensor or a wiring fault, so this runs first
 * and says so loudly if it cannot.
 * -------------------------------------------------------------------------*/
static int clock_start(const struct device *dev)
{
	const struct vl53l9cx_config *cfg = dev->config;
	uint32_t period_ns;
	int ret;

	if (!cfg->clock_from_pwm) {
		/* Board-supplied clock: an oscillator, or a SoC clock output
		 * configured elsewhere in devicetree (GRTC clkout-fast on
		 * water_sense_board). Nothing for the driver to start — and,
		 * importantly, nothing for it to stop either.
		 */
		LOG_INF("AP_CLK: board-supplied at %u Hz, always on — the sensor "
			"will not answer on I2C without it, and it is NOT gated "
			"with the sensor domain",
			cfg->ext_clock_hz);
		return 0;
	}

	if (!pwm_is_ready_dt(&cfg->clk)) {
		LOG_ERR("AP_CLK PWM not ready");
		return -ENODEV;
	}

	period_ns = (uint32_t)(NSEC_PER_SEC / cfg->ext_clock_hz);

	/* VERIFY ON HARDWARE: 12 MHz is a demanding ask of a general-purpose PWM
	 * peripheral. At a 16 MHz base clock a divider gives 16, 8, 5.33 MHz —
	 * not 12 — and 83 ns of period leaves no room for duty resolution. If
	 * the nRF54L15 has to source AP_CLK, expect to use a clock output or a
	 * TIMER/GPIOTE/PPI path rather than PWM, or fit a board oscillator. The
	 * legal range is 6-27 MHz, so a divider-friendly frequency may be the
	 * cheaper answer. This path is written so the devicetree can express the
	 * intent; whether PWM can honour it is a bench question.
	 */

	/* 50% duty. The part wants a clock, not a pulse train. */
	ret = pwm_set_dt(&cfg->clk, period_ns, period_ns / 2U);
	if (ret < 0) {
		LOG_ERR("AP_CLK PWM start failed (%d)", ret);
		return ret;
	}

	LOG_INF("AP_CLK: driving %u Hz from the MCU", cfg->ext_clock_hz);
	return 0;
}

static void clock_stop(const struct device *dev)
{
	const struct vl53l9cx_config *cfg = dev->config;

	if (!cfg->clock_from_pwm) {
		return;
	}

	/* Gating the clock with the sensor domain is not tidiness — a 12 MHz
	 * output left running through the >90% of the duty cycle when the
	 * sensor is off spends part of the energy budget this project is
	 * about measuring.
	 */
	(void)pwm_set_dt(&cfg->clk, 0, 0);
}

/* ---------------------------------------------------------------------------
 * Power and reset sequencing
 *
 * The whole path from power enable to first I2C transaction, in one place so
 * it can be reasoned about rather than reconstructed from sleeps:
 *
 *   power-gpios high
 *     +500 ms  POWER_SETTLE_MS   rail rise. Far longer than any plausible
 *              settle: during bring-up the point is to remove this from the
 *              list of suspects, not to be efficient. Kconfig-tunable, and paid
 *              on every wake, so it MUST be trimmed with a scope before the
 *              energy work — half a second per wake would dominate the duty
 *              cycle the paper is about.
 *   AP_CLK confirmed running (board-supplied here, so no delay)
 *   XSHUT low
 *     +50 ms   XSHUT_LOW_MS      reset pulse width
 *   XSHUT high
 *     +50 ms   XSHUT_SETTLE_MS   ROM boot before the part will answer
 *   first I2C transaction
 *
 * = 600 ms from power enable to first transaction, at the defaults.
 *
 * The 50 ms figures are ST's reference timing via the hardware-validated
 * community driver. That driver then polls for READY_TO_BOOT for up to 500 ms
 * *tolerating NAKs*, because the part NAKs while its ROM comes up — which is
 * why the probe below retries rather than taking one shot.
 * -------------------------------------------------------------------------*/
#define POWER_SETTLE_MS   CONFIG_VL53L9CX_POWER_SETTLE_MS
#define XSHUT_LOW_MS      CONFIG_VL53L9CX_XSHUT_LOW_MS
#define XSHUT_SETTLE_MS   CONFIG_VL53L9CX_XSHUT_SETTLE_MS

/* How long to keep retrying the first read before declaring the part absent.
 * Deadline rather than attempt count on purpose: a NAKing device fails in
 * microseconds and gets many attempts, while a stuck bus costs a full
 * CONFIG_I2C_NRFX_TRANSFER_TIMEOUT per try and gets one or two. Either way the
 * boot is bounded.
 */
#define PROBE_BUDGET_MS   600
#define PROBE_GAP_MS      10

/* Off-time in the one retry below. Long enough for the rail to actually fall,
 * since a power cycle that does not reach 0 V is just a pause.
 */
#define POWER_CYCLE_MS    100
static int power_up(const struct device *dev)
{
	const struct vl53l9cx_config *cfg = dev->config;
	int ret;

	if (cfg->power.port != NULL) {
		ret = gpio_pin_set_dt(&cfg->power, 1);
		if (ret < 0) {
			LOG_ERR("power-gpios set failed (%d)", ret);
			return ret;
		}
		LOG_DBG("sensor rail enabled");
		k_sleep(K_MSEC(POWER_SETTLE_MS));

		/* Read the pad, not the register we just wrote. A pin driven
		 * high that reads low is being held down — a short, or a rail
		 * the switch cannot pull up. That is a hardware fault visible
		 * from software, and worth catching before anyone reaches for
		 * a meter.
		 */
		{
			int lvl = gpio_pin_get_dt(&cfg->power);

			LOG_INF("power-gpios driven high, pad reads %d%s", lvl,
				lvl == 1 ? "" :
				lvl == 0 ? "  <-- HELD LOW. The pin is not "
					   "reaching the level we are driving: "
					   "a short, or a load the switch cannot"
					   " drive." :
					   "  <-- read failed");
		}
	}

	ret = clock_start(dev);
	if (ret < 0) {
		return ret;
	}

	/*
	 * XSHUT, with ST's reference timing: 50 ms low, 50 ms after release.
	 *
	 * The low period is explicit rather than inherited. The pin is already
	 * configured GPIO_OUTPUT_INACTIVE at init, so it is low by then — but
	 * only for the microseconds between init and here, not for a reset
	 * pulse the part would recognise. Driving it low and holding is the
	 * difference between a reset and a coincidence.
	 *
	 * The 50 ms figures come from the hardware-validated community driver,
	 * which cites them as ST's reference timing. Generous for bring-up;
	 * worth trimming later with a scope, and worth leaving alone until
	 * something is actually measured.
	 */
	if (cfg->xshut.port != NULL) {
		ret = gpio_pin_set_dt(&cfg->xshut, 0);
		if (ret < 0) {
			LOG_ERR("xshut-gpios set failed (%d)", ret);
			return ret;
		}
		k_sleep(K_MSEC(XSHUT_LOW_MS));

		ret = gpio_pin_set_dt(&cfg->xshut, 1);
		if (ret < 0) {
			LOG_ERR("xshut-gpios release failed (%d)", ret);
			return ret;
		}
		k_sleep(K_MSEC(XSHUT_SETTLE_MS));

		{
			int lvl = gpio_pin_get_dt(&cfg->xshut);

			LOG_INF("XSHUT released, pad reads %d%s", lvl,
				lvl == 1 ? "" :
				lvl == 0 ? "  <-- HELD LOW. The part is being "
					   "kept in reset by something other "
					   "than us." :
					   "  <-- read failed");
		}
	}

	return 0;
}

static void power_down(const struct device *dev)
{
	const struct vl53l9cx_config *cfg = dev->config;

	if (cfg->xshut.port != NULL) {
		(void)gpio_pin_set_dt(&cfg->xshut, 0);
	}

	clock_stop(dev);

	if (cfg->power.port != NULL) {
		(void)gpio_pin_set_dt(&cfg->power, 0);
	}
}

/* ---------------------------------------------------------------------------
 * Data-ready interrupt
 * -------------------------------------------------------------------------*/
static void int_handler(const struct device *port, struct gpio_callback *cb,
			uint32_t pins)
{
	struct vl53l9cx_data *data = CONTAINER_OF(cb, struct vl53l9cx_data, int_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	k_sem_give(&data->frame_ready);
}

/* ---------------------------------------------------------------------------
 * Bring the device up: blob upload, version check, defaults.
 *
 * The blob upload is timed because its duration decides whether powering the
 * sensor domain down during idle beats keeping it in standby. That crossover
 * is a stage-4 result, and capturing it costs nothing here.
 * -------------------------------------------------------------------------*/
static uint16_t dev_i2c_addr(const struct device *dev)
{
	const struct vl53l9cx_config *cfg = dev->config;

	return cfg->i2c.addr;
}

/*
 * Does anything acknowledge at this 7-bit address?
 *
 * Writes the two-byte register index and looks only at whether it was ACKed.
 * Deliberately NOT a general bus scan: this part does not tolerate the empty
 * START+STOP transactions a scan uses to probe address ranges, and such a scan
 * can wedge it. Two targeted writes are safe.
 */
static bool addr_acks(const struct device *dev, uint16_t addr)
{
	const struct vl53l9cx_config *cfg = dev->config;
	uint8_t idx[2] = { 0x00, 0x00 };

	return i2c_write(cfg->i2c.bus, idx, sizeof(idx), addr) == 0;
}

/*
 * Try the whole power-up with power-gpios and XSHUT driven at INVERTED raw
 * levels, and probe again.
 *
 * This tests the one remaining cause that is fixable in software rather than
 * on a bench: an active-low enable declared as active-high. A PMOS load switch
 * or a reset line with inverted sense would mean we have been holding the rail
 * OFF, or the part IN reset, throughout — and every symptom would look exactly
 * like this: a healthy bus and a part that never answers at any address.
 *
 * gpio_pin_set_raw() bypasses the devicetree ACTIVE_HIGH/ACTIVE_LOW mapping, so
 * this needs no rebuild and no devicetree change to test.
 *
 * If this works, the fix is one flag in the overlay. If it does not, the
 * polarity is off the list too and what remains is genuinely physical.
 */
static bool try_inverted_polarity(const struct device *dev)
{
	const struct vl53l9cx_config *cfg = dev->config;
	uint32_t id = 0;

	if (cfg->power.port == NULL && cfg->xshut.port == NULL) {
		return false;
	}

	LOG_WRN("trying INVERTED enable polarity, in case an active-low line is "
		"declared active-high");

	if (cfg->power.port != NULL) {
		(void)gpio_pin_set_raw(cfg->power.port, cfg->power.pin, 0);
		k_sleep(K_MSEC(POWER_SETTLE_MS));

		/* Read the pad, not the register we just wrote. A pin driven
		 * high that reads low is being held down — a short, or a rail
		 * the switch cannot pull up. That is a hardware fault visible
		 * from software, and worth catching before anyone reaches for
		 * a meter.
		 */
		{
			int lvl = gpio_pin_get_dt(&cfg->power);

			LOG_INF("power-gpios driven high, pad reads %d%s", lvl,
				lvl == 1 ? "" :
				lvl == 0 ? "  <-- HELD LOW. The pin is not "
					   "reaching the level we are driving: "
					   "a short, or a load the switch cannot"
					   " drive." :
					   "  <-- read failed");
		}
	}

	if (cfg->xshut.port != NULL) {
		(void)gpio_pin_set_raw(cfg->xshut.port, cfg->xshut.pin, 1);
		k_sleep(K_MSEC(XSHUT_LOW_MS));
		(void)gpio_pin_set_raw(cfg->xshut.port, cfg->xshut.pin, 0);
		k_sleep(K_MSEC(XSHUT_SETTLE_MS));
	}

	if (vl53l9_get_device_id((void *)dev, &id) == VL53L9_ERROR_NONE) {
		LOG_ERR("  *** INVERTED POLARITY WORKS *** model id 0x%08x", id);
		LOG_ERR("  One or both of power-gpios / xshut-gpios is ACTIVE_LOW "
			"on this board and declared ACTIVE_HIGH in the overlay. "
			"Fix the flag there — this diagnostic does not persist.");
		return true;
	}

	LOG_ERR("  inverted polarity does not help either. Enable sense is off "
		"the list; what remains is physical: AP_CLK on the pad, the rail "
		"on P0.02, XSHUT on P1.07.");

	/* Put the pins back the way the devicetree says. */
	if (cfg->power.port != NULL) {
		(void)gpio_pin_set_dt(&cfg->power, 1);
	}
	if (cfg->xshut.port != NULL) {
		(void)gpio_pin_set_dt(&cfg->xshut, 1);
	}
	return false;
}

static int device_boot(const struct device *dev)
{
	struct vl53l9cx_data *data = dev->data;
	uint32_t id = 0;
	int64_t t0;
	int ret;

	ret = power_up(dev);
	if (ret < 0) {
		return ret;
	}

	t0 = k_uptime_get();

	/* vl53l9_init() waits for READY_TO_BOOT, writes the three board values,
	 * uploads the 9,865-byte firmware patch, boots, verifies the patch
	 * version and applies ST's default configuration.
	 */
	/*
	 * Probe before booting, so a failure says WHICH failure it is.
	 *
	 * vl53l9_get_device_id() is a bare register read and needs nothing but
	 * a powered, clocked part. It splits the two failure classes that
	 * otherwise look identical from up here:
	 *
	 *   read fails    -> nothing is answering. Power, AP_CLK, XSHUT or
	 *                    address. Note that if the IMU on the same bus
	 *                    works, the bus itself is exonerated.
	 *   read succeeds -> the device is alive and talking, so the fault is
	 *                    in boot: the blob upload, or its timing.
	 */
	{
		uint32_t probe = 0;
		int64_t deadline = k_uptime_get() + PROBE_BUDGET_MS;
		int pret;
		unsigned int tries = 0;

		do {
			tries++;
			pret = vl53l9_get_device_id((void *)dev, &probe);
			if (pret == VL53L9_ERROR_NONE) {
				break;
			}
			k_sleep(K_MSEC(PROBE_GAP_MS));
		} while (k_uptime_get() < deadline);

		if (pret != VL53L9_ERROR_NONE) {
			LOG_ERR("no answer from the sensor after %u attempt(s) "
				"over %d ms (%s).", tries, PROBE_BUDGET_MS,
				vl53l9_errstr(pret));

			/*
			 * Try to unstick the bus, and use the result as a
			 * measurement rather than a fix.
			 *
			 * i2c_recover_bus() clocks SCL until a slave that is
			 * holding SDA low lets go. It separates the only two
			 * causes of a transfer timeout:
			 *
			 *   recovery works, then a read succeeds -> a device was
			 *     holding the bus. Real, and fixable in firmware.
			 *   recovery changes nothing -> the lines cannot reach a
			 *     high level at all, which is missing pull-ups or a
			 *     short. No amount of firmware helps.
			 *
			 * Worth doing once even though it rarely succeeds,
			 * because the failure is as informative as the success.
			 */
			{
				const struct vl53l9cx_config *c = dev->config;
				int rec = i2c_recover_bus(c->i2c.bus);

				if (rec == 0) {
					uint32_t again = 0;

					LOG_WRN("  bus recovery ran; retrying once");
					if (vl53l9_get_device_id((void *)dev, &again)
					    == VL53L9_ERROR_NONE) {
						LOG_WRN("  recovery WORKED — a "
							"device was holding the "
							"bus, not a wiring fault");
						probe = again;
						pret = VL53L9_ERROR_NONE;
						goto probed;
					}
					/* What "recovery changed nothing" means
					 * depends entirely on the errno above,
					 * and saying only one of them is worse
					 * than saying neither.
					 */
					LOG_ERR("  recovery changed nothing. If the "
						"errno above is TIMEOUT that means "
						"the lines cannot reach a high "
						"level — pull-ups or a short. If it "
						"is a plain no-acknowledge, the bus "
						"is healthy and nothing was holding "
						"it: the part simply is not "
						"responding.");
				} else {
					LOG_ERR("  bus recovery unsupported or "
						"failed (%d)", rec);
				}
			}
			LOG_ERR("  Read the errno on the line above this one. "
				"TIMEOUT means the bus is stuck — suspect SDA/SCL "
				"pull-ups. A plain no-acknowledge instead means "
				"the bus is FINE and nothing is at this address.");

			/*
			 * Settle the address question, since it is the one
			 * suspect on the list that software can eliminate.
			 *
			 * 0x29 is the 7-bit address; ST's 0x52 is the 8-bit
			 * form, and their own code shifts it down in two
			 * places. That reasoning is sound, but it is reasoning,
			 * and one register write costs nothing next to a day of
             * looking in the wrong place.
			 */
			{
				const uint16_t configured = dev_i2c_addr(dev);
				const uint16_t other =
					(configured == 0x29) ? 0x52 : 0x29;

				LOG_ERR("  probing both address candidates:");
				LOG_ERR("    0x%02x (configured): %s", configured,
					addr_acks(dev, configured) ? "ACK" : "no ACK");
				LOG_ERR("    0x%02x (the other):  %s", other,
					addr_acks(dev, other) ? "ACK — USE THIS ONE"
							      : "no ACK");
				LOG_ERR("  If neither acknowledges, the address is "
					"not the problem: the part is not "
					"responding at all.");
			}

			if (try_inverted_polarity(dev)) {
				return -EIO; /* report it, do not run on a hack */
			}
			return -EIO;
		}

probed:
		LOG_INF("sensor answered on attempt %u, model id 0x%08x — bus, "
			"power, clock and address are all good", tries, probe);
	}

	ret = vl53l9_init((void *)dev);
	data->boot_ms = (uint32_t)(k_uptime_get() - t0);

	if (IS_ENABLED(CONFIG_VL53L9CX_LOG_BOOT_TIME)) {
		LOG_INF("firmware blob uploaded and booted in %u ms",
			data->boot_ms);
	}

	ret = vl53l9_get_device_id((void *)dev, &id);
	if (ret == VL53L9_ERROR_NONE) {
		LOG_INF("device id 0x%08x", id);
	}

	return 0;
}

/* Frame signalling. There is no in-band interrupt on plain I2C, so the device
 * must be told to use its interrupt pad. ST models both as hw_config fields.
 */
static int configure_signalling(const struct device *dev)
{
	struct vl53l9cx_data *data = dev->data;
	vl53l9_hw_config_t hw;
	int ret;

	ret = vl53l9_get_hw_config((void *)dev, &hw);
	if (ret != VL53L9_ERROR_NONE) {
		return -EIO;
	}

	hw.output_interface = true;  /* I3C-style register output, not CSI-2 */
	hw.signaling_mode = true;    /* interrupt pad, not in-band interrupt */
	hw.interrupt_pad_mode = false; /* CMOS push-pull. VERIFY against the
					* schematic: open-drain if the line is
					* shared or pulled to a different rail.
					*/

	ret = vl53l9_set_hw_config((void *)dev, hw);
	if (ret != VL53L9_ERROR_NONE) {
		LOG_ERR("set_hw_config failed (%d)", ret);
		return -EIO;
	}

	if (!data->use_interrupt) {
		LOG_WRN("no int-gpios: falling back to polling frame-ready. "
			"This keeps the CPU awake across integration, which is "
			"exactly the energy this design exists to avoid.");
	}

	return 0;
}

/* ---------------------------------------------------------------------------
 * Frame unpacking
 *
 * Wire layout, verified against vl53l9_get_frame() (st/vl53l9.c:627) and ST's
 * own vl53l9_utils_parse_frame():
 *
 *   [ depth u16 x tx_zones ][ amplitude u16 x tx_zones ][ ambient u16 x tx_zones ]
 *   [ dss tx_zones/2 ][ status line 100 ]
 *
 * PLANE-major, not interleaved per zone — an earlier comment in the public
 * header claimed the latter, which would have produced a frame that looked
 * almost right. All values little-endian. Depth carries millimetres in bits
 * 14:0 and a validity flag in bit 15; forgetting the mask yields distances
 * around 32 m and a person with a hole in the middle.
 * -------------------------------------------------------------------------*/
static void unpack(const struct device *dev, struct vl53l9cx_frame *out)
{
	struct vl53l9cx_data *data = dev->data;
	const uint8_t *raw = data->raw;
	const uint16_t tx_zones = (uint16_t)data->cols * data->tx_rows;
	const uint32_t plane = (uint32_t)tx_zones * 2U;
	const uint8_t *depth = raw;
	const uint8_t *ampl = raw + plane;
	const uint8_t *ambi = raw + 2U * plane;
	const uint8_t *status = raw + 3U * plane + tx_zones / 2U;

	out->cols = data->cols;
	out->rows = data->rows;
	out->timestamp_ms = k_uptime_get();
	out->seq = ++data->seq;

	/* The status line is ST's vl53l9_meta_t. Its first two fields are plain
	 * integers at a fixed offset; the rest is bitfields whose layout is
	 * compiler-dependent, so we take only these and keep the raw line.
	 */
	out->frame_counter = sys_get_le32(status + VL53L9CX_META_FRAME_COUNTER);
	out->temperature = sys_get_le16(status + VL53L9CX_META_TEMPERATURE);
	memcpy(out->status_line, status, VL53L9CX_STATUS_LINE_BYTES);

	for (uint8_t r = 0; r < data->rows; r++) {
		/* Skip the cropped rows of the square formats. */
		const uint16_t src_row = r + data->y_offset;

		for (uint8_t c = 0; c < data->cols; c++) {
			const uint16_t s = src_row * data->cols + c;
			const uint16_t d = (uint16_t)r * data->cols + c;
			uint16_t depth_word = sys_get_le16(depth + s * 2U);
			struct vl53l9cx_zone *z = &out->zone[d];

			z->distance_mm = depth_word & 0x7FFFU;
			z->valid = (uint8_t)(depth_word >> 15);
			z->amplitude = sys_get_le16(ampl + s * 2U);
			z->ambient = sys_get_le16(ambi + s * 2U);
			z->status = z->valid ? VL53L9CX_ZONE_VALID
					     : VL53L9CX_ZONE_NO_TARGET;
		}
	}
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
static int apply_resolution(const struct device *dev, enum vl53l9cx_res res)
{
	struct vl53l9cx_data *data = dev->data;
	const struct res_geom *g = &geom[res];
	uint16_t need = 0;
	int ret;

	if (res >= VL53L9CX_RES_COUNT) {
		return -EINVAL;
	}

	/* Ask ST rather than trusting our own table. */
	ret = vl53l9_get_raw_buffer_size(g->binning, &need);
	if (ret != VL53L9_ERROR_NONE) {
		return -EINVAL;
	}
	if (need > sizeof(data->raw)) {
		LOG_ERR("raw buffer too small: need %u, have %u",
			need, (unsigned int)sizeof(data->raw));
		return -ENOMEM;
	}

	ret = vl53l9_set_context((void *)dev, VL53L9_CONTEXT_LONG);
	if (ret != VL53L9_ERROR_NONE) {
		return -EIO;
	}

	ret = vl53l9_set_binning((void *)dev, VL53L9_CONTEXT_LONG, g->binning);
	if (ret != VL53L9_ERROR_NONE) {
		LOG_ERR("set_binning(%u) failed (%d) — note it requires the "
			"device to be in STANDBY", g->binning, ret);
		return -EIO;
	}

	data->binning = g->binning;
	data->cols = g->cols;
	data->rows = g->rows;
	data->tx_rows = g->tx_rows;
	data->y_offset = g->y_offset;

	if (!g->wide) {
		LOG_WRN("%ux%u is a SQUARE format: it transmits %ux%u and crops "
			"on-device, so its field of view differs from the wide "
			"modes. Do not mix families in an energy-vs-zones sweep.",
			g->cols, g->rows, g->cols, g->tx_rows);
	}

	return 0;
}

int vl53l9cx_start(const struct device *dev, enum vl53l9cx_res res,
		   uint32_t period_ms)
{
	struct vl53l9cx_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = apply_resolution(dev, res);
	if (ret < 0) {
		goto out;
	}

	ret = vl53l9_set_sync_mode((void *)dev, VL53L9_SYNC_AUTONOMOUS);
	if (ret != VL53L9_ERROR_NONE) {
		ret = -EIO;
		goto out;
	}

	ret = vl53l9_set_frame_period((void *)dev, period_ms * USEC_PER_MSEC);
	if (ret != VL53L9_ERROR_NONE) {
		ret = -EIO;
		goto out;
	}

	k_sem_reset(&data->frame_ready);

	ret = vl53l9_start((void *)dev);
	if (ret != VL53L9_ERROR_NONE) {
		ret = -EIO;
		goto out;
	}

	data->streaming = true;
	ret = 0;
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

int vl53l9cx_stop(const struct device *dev)
{
	struct vl53l9cx_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = vl53l9_stop((void *)dev);
	data->streaming = false;
	k_mutex_unlock(&data->lock);

	return (ret == VL53L9_ERROR_NONE) ? 0 : -EIO;
}

static int wait_frame(const struct device *dev, k_timeout_t timeout)
{
	struct vl53l9cx_data *data = dev->data;

	if (data->use_interrupt) {
		return k_sem_take(&data->frame_ready, timeout);
	}

	/* Polling fallback. Deliberately coarse: this path exists so a board
	 * without the interrupt line still works, not so it works well.
	 */
	const bool forever = K_TIMEOUT_EQ(timeout, K_FOREVER);
	const int64_t deadline = forever ? 0
					 : k_uptime_get() + k_ticks_to_ms_ceil64(timeout.ticks);

	for (;;) {
		uint8_t ready = 0;

		if (vl53l9_poll_frame((void *)dev, &ready) != VL53L9_ERROR_NONE) {
			return -EIO;
		}
		if (ready) {
			return 0;
		}
		if (!forever && k_uptime_get() >= deadline) {
			return -EAGAIN;
		}
		k_sleep(K_MSEC(5));
	}
}

int vl53l9cx_get_frame(const struct device *dev, struct vl53l9cx_frame *out,
		       k_timeout_t timeout)
{
	struct vl53l9cx_data *data = dev->data;
	uint16_t size = 0;
	int ret;

	ret = wait_frame(dev, timeout);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = vl53l9_get_raw_buffer_size(data->binning, &size);
	if (ret != VL53L9_ERROR_NONE) {
		ret = -EIO;
		goto out;
	}

	/* This also acknowledges the frame and releases the interrupt pin. */
	ret = vl53l9_get_frame((void *)dev, data->raw, size);
	if (ret != VL53L9_ERROR_NONE) {
		LOG_ERR("get_frame failed (%d)", ret);
		ret = -EIO;
		goto out;
	}

	unpack(dev, out);
	ret = 0;
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

int vl53l9cx_capture(const struct device *dev, enum vl53l9cx_res res,
		     struct vl53l9cx_frame *out, k_timeout_t timeout)
{
	struct vl53l9cx_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = apply_resolution(dev, res);
	if (ret == 0 &&
	    vl53l9_set_sync_mode((void *)dev, VL53L9_SYNC_MANUAL) != VL53L9_ERROR_NONE) {
		ret = -EIO;
	}
	if (ret == 0 && vl53l9_start((void *)dev) != VL53L9_ERROR_NONE) {
		ret = -EIO;
	}
	if (ret == 0) {
		k_sem_reset(&data->frame_ready);
		if (vl53l9_trigger_frame((void *)dev) != VL53L9_ERROR_NONE) {
			ret = -EIO;
		}
	}

	k_mutex_unlock(&data->lock);

	if (ret < 0) {
		return ret;
	}

	ret = vl53l9cx_get_frame(dev, out, timeout);

	k_mutex_lock(&data->lock, K_FOREVER);
	(void)vl53l9_stop((void *)dev);
	k_mutex_unlock(&data->lock);

	return ret;
}

uint32_t vl53l9cx_last_boot_ms(const struct device *dev)
{
	struct vl53l9cx_data *data = dev->data;

	return data->boot_ms;
}

/* ---------------------------------------------------------------------------
 * Power management
 *
 * SUSPEND and TURN_OFF are deliberately distinct, and keeping them so makes
 * the idle-strategy crossover a runtime choice rather than four firmware
 * builds:
 *
 *   SUSPEND  — stop ranging, sensor standby, rail and clock stay up, firmware
 *              retained. Cheap to resume.
 *   TURN_OFF — drop XSHUT, gate AP_CLK, drop the sensor domain. Zero standby
 *              current, but the next resume pays a full firmware blob reload.
 *
 * The blob is 9,865 bytes, which is ~250 ms at 400 kHz — about one
 * full-resolution frame read. At the few-percent duty cycle that room dwell
 * implies, TURN_OFF should win comfortably. That is a prediction, not a
 * result: vl53l9cx_last_boot_ms() is what turns it into one.
 * -------------------------------------------------------------------------*/
static int pm_action(const struct device *dev, enum pm_device_action action)
{
	struct vl53l9cx_data *data = dev->data;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		if (data->streaming) {
			(void)vl53l9_stop((void *)dev);
			data->streaming = false;
		}
		return (vl53l9_set_power_mode((void *)dev, VL53L9_POWER_ULTRA_LOW)
			== VL53L9_ERROR_NONE) ? 0 : -EIO;

	case PM_DEVICE_ACTION_RESUME:
		return (vl53l9_set_power_mode((void *)dev, VL53L9_POWER_REGULAR)
			== VL53L9_ERROR_NONE) ? 0 : -EIO;

	case PM_DEVICE_ACTION_TURN_OFF:
		data->streaming = false;
		power_down(dev);
		return 0;

	case PM_DEVICE_ACTION_TURN_ON: {
		int ret = device_boot(dev);

		/*
		 * One full retry, and deliberately a POWER CYCLE rather than
		 * another probe.
		 *
		 * The probe inside device_boot() already retries the read, so
		 * repeating it here would add nothing. What this covers is
		 * different: a part that came up in a bad state, or a rail that
		 * had not settled when XSHUT was released. Both are fixed only
		 * by taking the power away and starting the sequence again, and
		 * neither is fixed by asking a second time.
		 *
		 * Exactly one retry. A boot loop that keeps trying hides a
		 * hardware fault behind an occasional success, which on a board
		 * with no known-good reference is worse than failing.
		 */
		if (ret < 0) {
			LOG_WRN("boot failed — power cycling and trying once more");
			power_down(dev);
			k_sleep(K_MSEC(POWER_CYCLE_MS));

			ret = device_boot(dev);
			if (ret < 0) {
				LOG_ERR("boot failed again after a power cycle. "
					"This is not a transient.");
				return ret;
			}
			LOG_WRN("second attempt succeeded — the first failure was "
				"real and worth explaining, not ignoring.");
		}

		return configure_signalling(dev);
	}

	default:
		return -ENOTSUP;
	}
}

/* ---------------------------------------------------------------------------
 * Init
 * -------------------------------------------------------------------------*/
static int vl53l9cx_init(const struct device *dev)
{
	const struct vl53l9cx_config *cfg = dev->config;
	struct vl53l9cx_data *data = dev->data;
	int ret;

	/*
	 * Every step below logs before it can fail.
	 *
	 * The first version returned bare error codes from five of these with
	 * no output at all, so a failure produced a device that was simply
	 * "not ready" and a completely silent driver — which says nothing
	 * about which of five things went wrong. On a board with no known-good
	 * reference that is the most expensive kind of failure there is.
	 */
	LOG_INF("init: starting");

	data->dev = dev;
	k_sem_init(&data->frame_ready, 0, 1);
	k_mutex_init(&data->lock);

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("init: I2C bus %s not ready", cfg->i2c.bus->name);
		return -ENODEV;
	}
	LOG_INF("init: I2C bus %s ready, device at 0x%02x",
		cfg->i2c.bus->name, cfg->i2c.addr);

	if (cfg->power.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->power)) {
			LOG_ERR("init: power-gpios port %s not ready",
				cfg->power.port->name);
			return -ENODEV;
		}
		/* GPIO_INPUT alongside the output connects the input buffer, so
		 * the pad can be read back. Without it, reading an output pin
		 * returns what we wrote rather than what the pin is doing, and
		 * a line held low by a short or an overloaded rail is invisible.
		 */
		ret = gpio_pin_configure_dt(&cfg->power,
					    GPIO_OUTPUT_INACTIVE | GPIO_INPUT);
		if (ret < 0) {
			LOG_ERR("init: power-gpios configure failed (%d)", ret);
			return ret;
		}
		LOG_INF("init: power-gpios ready (%s pin %u)",
			cfg->power.port->name, cfg->power.pin);
	}

	if (cfg->xshut.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->xshut)) {
			LOG_ERR("init: xshut-gpios port %s not ready",
				cfg->xshut.port->name);
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&cfg->xshut,
					    GPIO_OUTPUT_INACTIVE | GPIO_INPUT);
		if (ret < 0) {
			LOG_ERR("init: xshut-gpios configure failed (%d)", ret);
			return ret;
		}
		LOG_INF("init: xshut-gpios ready (%s pin %u)",
			cfg->xshut.port->name, cfg->xshut.pin);
	}

	data->use_interrupt = (cfg->intr.port != NULL);
	if (data->use_interrupt) {
		if (!gpio_is_ready_dt(&cfg->intr)) {
			LOG_ERR("init: int-gpios port %s not ready",
				cfg->intr.port->name);
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&cfg->intr, GPIO_INPUT);
		if (ret < 0) {
			LOG_ERR("init: int-gpios configure failed (%d)", ret);
			return ret;
		}
		ret = gpio_pin_interrupt_configure_dt(&cfg->intr,
						      GPIO_INT_EDGE_TO_ACTIVE);
		if (ret < 0) {
			LOG_ERR("init: int-gpios interrupt configure failed (%d). "
				"On the nRF54L15 each port needs its GPIOTE "
				"instance enabled — P0 uses gpiote30, P1 uses "
				"gpiote20.", ret);
			return ret;
		}
		gpio_init_callback(&data->int_cb, int_handler, BIT(cfg->intr.pin));
		ret = gpio_add_callback(cfg->intr.port, &data->int_cb);
		if (ret < 0) {
			LOG_ERR("init: gpio_add_callback failed (%d)", ret);
			return ret;
		}
		LOG_INF("init: int-gpios ready (%s pin %u, edge to active)",
			cfg->intr.port->name, cfg->intr.pin);
	} else {
		LOG_WRN("init: no int-gpios — frame-ready will be polled");
	}

	LOG_INF("init: bringing the sensor up — power +%d ms, XSHUT low %d ms, "
		"settle %d ms, then first I2C transaction",
		POWER_SETTLE_MS, XSHUT_LOW_MS, XSHUT_SETTLE_MS);

	/* Deliberately NOT booting the device here. pm_device_driver_init()
	 * drives the device to its initial state through pm_action(), whose
	 * TURN_ON case does the blob upload — so booting here as well would
	 * upload it twice and double-count boot_ms, which is a measurement the
	 * power model depends on.
	 */
	ret = pm_device_driver_init(dev, pm_action);
	if (ret < 0) {
		LOG_ERR("init: FAILED (%d)", ret);
		return ret;
	}

	LOG_INF("init: complete");
	return 0;
}

/* The three board values have no defaults on purpose: a wrong VDDA or VDDIO
 * misconfigures the analogue front end rather than failing loudly, so the
 * binding marks them required and a board file that omits one fails to build.
 */
#define VL53L9CX_DEFINE(inst)                                                  \
	static struct vl53l9cx_data vl53l9cx_data_##inst;                      \
                                                                               \
	static const struct vl53l9cx_config vl53l9cx_config_##inst = {         \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                             \
		.xshut = GPIO_DT_SPEC_INST_GET_OR(inst, xshut_gpios, {0}),     \
		.intr = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),        \
		.power = GPIO_DT_SPEC_INST_GET_OR(inst, power_gpios, {0}),     \
		.clk = COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, pwms),    \
				   (PWM_DT_SPEC_INST_GET(inst)), ({0})),       \
		.clock_from_pwm = DT_INST_NODE_HAS_PROP(inst, pwms),     \
		.ext_clock_hz = DT_INST_PROP(inst, ext_clock_frequency),       \
		.blob_chunk_size = DT_INST_PROP(inst, blob_chunk_size),        \
		.vdda = DT_INST_ENUM_IDX(inst, vdda_microvolt),                \
		.vddio = DT_INST_ENUM_IDX(inst, vddio_microvolt),              \
	};                                                                     \
                                                                               \
	PM_DEVICE_DT_INST_DEFINE(inst, pm_action);                             \
                                                                               \
	DEVICE_DT_INST_DEFINE(inst, vl53l9cx_init,                             \
			      PM_DEVICE_DT_INST_GET(inst),                     \
			      &vl53l9cx_data_##inst, &vl53l9cx_config_##inst,  \
			      POST_KERNEL, CONFIG_VL53L9CX_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(VL53L9CX_DEFINE)
