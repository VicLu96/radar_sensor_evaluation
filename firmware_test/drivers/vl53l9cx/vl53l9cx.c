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
 * -------------------------------------------------------------------------*/
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
		k_sleep(K_MSEC(10)); /* rail settle. VERIFY against the schematic. */
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
		k_sleep(K_MSEC(50));

		ret = gpio_pin_set_dt(&cfg->xshut, 1);
		if (ret < 0) {
			LOG_ERR("xshut-gpios release failed (%d)", ret);
			return ret;
		}
		k_sleep(K_MSEC(50));
		LOG_DBG("XSHUT released");
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
		int pret = vl53l9_get_device_id((void *)dev, &probe);

		if (pret != VL53L9_ERROR_NONE) {
			LOG_ERR("no answer from the sensor (%s). It is not "
				"talking at all.", vl53l9_errstr(pret));
			LOG_ERR("  Check in this order: AP_CLK on P0.00 with a "
				"scope (no clock, no ACK — and it looks exactly "
				"like a dead part), the sensor rail, XSHUT, then "
				"the address. If the IMU on this bus works, the "
				"bus is fine and the fault is one of those four.");
			return -EIO;
		}
		LOG_INF("sensor answered, model id 0x%08x — bus, power, clock "
			"and address are all good", probe);
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

		if (ret < 0) {
			return ret;
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

	data->dev = dev;
	k_sem_init(&data->frame_ready, 0, 1);
	k_mutex_init(&data->lock);

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	if (cfg->power.port != NULL) {
		ret = gpio_pin_configure_dt(&cfg->power, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	if (cfg->xshut.port != NULL) {
		ret = gpio_pin_configure_dt(&cfg->xshut, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	data->use_interrupt = (cfg->intr.port != NULL);
	if (data->use_interrupt) {
		if (!gpio_is_ready_dt(&cfg->intr)) {
			LOG_ERR("interrupt GPIO not ready");
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&cfg->intr, GPIO_INPUT);
		if (ret < 0) {
			return ret;
		}
		ret = gpio_pin_interrupt_configure_dt(&cfg->intr,
						      GPIO_INT_EDGE_TO_ACTIVE);
		if (ret < 0) {
			return ret;
		}
		gpio_init_callback(&data->int_cb, int_handler, BIT(cfg->intr.pin));
		ret = gpio_add_callback(cfg->intr.port, &data->int_cb);
		if (ret < 0) {
			return ret;
		}
	}

	/* Deliberately NOT booting the device here. pm_device_driver_init()
	 * drives the device to its initial state through pm_action(), whose
	 * TURN_ON case does the blob upload — so booting here as well would
	 * upload it twice and double-count boot_ms, which is a measurement the
	 * power model depends on.
	 */
	return pm_device_driver_init(dev, pm_action);
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
