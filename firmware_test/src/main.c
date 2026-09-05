/*
 * water_sense_board bring-up test.
 *
 * Two stages, in order, because each one only means something if the previous
 * one passed:
 *
 *   1. The MCU is alive and can talk to a host  — banner + heartbeat over RTT.
 *   2. The I2C bus works and the IMU is real    — WHO_AM_I, then accel XYZ.
 *   3. The VL53L9CX ranges                      — a frame, printed as a grid.
 *
 * The order matters. The IMU is the simpler device on the same bus, so if it
 * answers and the ToF sensor does not, the bus itself is proven and the fault
 * is on the ToF side — which is worth a great deal on a board where nothing
 * else is known good.
 *
 * Output goes over SEGGER RTT (no UART pins on this board). Open it with
 * JLinkRTTViewer on channel 0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <stdio.h>

/*
 * Catch a stale build directory before the compiler produces a misleading
 * error.
 *
 * If the VL53L9CX node is absent, CONFIG_VL53L9CX is not set, so the driver
 * module's CMakeLists skips its zephyr_include_directories() — and the next
 * line fails with "vl53l9cx/vl53l9cx.h: No such file or directory", which
 * points at the include path and not at the actual cause.
 *
 * The usual actual cause: overlay discovery happens once, at configure time,
 * and is cached in DTC_OVERLAY_FILE. A build directory created before
 * boards/water_sense_board_nrf54l15_cpuapp.overlay existed will never pick it
 * up, however many times it is rebuilt.
 */
#if !DT_HAS_COMPAT_STATUS_OKAY(st_vl53l9cx)
#error "No enabled st,vl53l9cx node in the devicetree. Almost certainly a STALE BUILD DIRECTORY: overlay discovery is cached at configure time, so a build dir created before the overlay existed never sees it. Rebuild pristine: west build -b water_sense_board/nrf54l15/cpuapp firmware_test -p always . If that does not fix it, check that firmware_test/boards/water_sense_board_nrf54l15_cpuapp.overlay exists and that its filename matches the board target exactly."
#endif

#include <vl53l9cx/vl53l9cx.h>

/* For the AP_CLK check below: read back what the GRTC peripheral thinks it is
 * doing, rather than trusting devicetree to have been applied.
 */
#include <haly/nrfy_grtc.h>
#include <hal/nrf_gpio.h>

LOG_MODULE_REGISTER(board_test, LOG_LEVEL_INF);

#if defined(CONFIG_APP_ENABLE_IMU)
/* The I2C controller the board file enables. Follows the board, so if the
 * instance is ever renumbered this is the one line that moves.
 */
#define IMU_BUS_NODE DT_NODELABEL(i2c21)

/* 7-bit. SA0 strapped high (Victor, 2026-09-04). No collision with the
 * VL53L9CX at 0x29.
 */
#define IMU_ADDR 0x6B

/*
 * Register map, taken from ST's own headers in the SDK rather than from
 * memory: modules/hal/st/sensor/stmemsc/lsm6dsv16bx_STdC/driver/.
 *
 * The control and status addresses below are identical on both variants we
 * might have. The accelerometer output block differs, and it differs silently —
 * see the note above OUT_A_BASE.
 */
#define REG_WHO_AM_I   0x0F
#define REG_CTRL1      0x10 /* [3:0] ODR_XL, [6:4] OP_MODE_XL */
#define REG_CTRL8      0x17 /* [1:0] FS_XL                    */
#define REG_STATUS     0x1E /* bit 0 = XLDA, accel data ready */

/*
 * The accelerometer output block starts at 0x28 on BOTH parts. What differs is
 * the AXIS ORDER inside it, and that is the whole trap:
 *
 *          0x28   0x2A   0x2C
 *   16X     X      Y      Z
 *   16BX    Z      Y      X     <- reversed
 *
 * So LSM6DSV16BX_OUTX_L_A is 0x2C not because the block moved, but because X is
 * last. Reading six bytes from 0x2C on a 16BX therefore returns the real X in
 * the first word and then 0x2E-0x31, which are undefined and read back as zero.
 * That is exactly what the first version of this code did, and the log said so
 * plainly: X drifting around zero, Y and Z exactly 0.000 every sample.
 */
#define OUT_A_BASE     0x28 /* both variants */

#define ID_16X         0x70
#define ID_16BX        0x71

/* Byte offsets of each axis within the six-byte block at OUT_A_BASE. */
struct axis_layout {
	uint8_t x;
	uint8_t y;
	uint8_t z;
};

static const struct axis_layout layout_16x  = { .x = 0, .y = 2, .z = 4 };
static const struct axis_layout layout_16bx = { .x = 4, .y = 2, .z = 0 };

/* CTRL1: high-performance mode (op_mode_xl = 0), 60 Hz (odr_xl = 0x5).
 * 60 Hz is deliberately slow: fast enough that a 1 Hz read loop never sees
 * stale data, slow enough to be obviously wrong if the ODR write did not land.
 */
#define CTRL1_XL_60HZ_HP  0x05

/* CTRL8: FS_XL = 0 -> +/-2 g, which is 0.061 mg/LSB
 * (lsm6dsv16bx_from_fs2_to_mg() in ST's driver).
 */
#define CTRL8_FS_2G       0x00
#define MG_PER_LSB_NUM    61
#define MG_PER_LSB_DEN    1000

static const struct device *const imu_bus = DEVICE_DT_GET(IMU_BUS_NODE);

/* Axis order within the output block, chosen from WHO_AM_I. */
static const struct axis_layout *layout;

#endif /* CONFIG_APP_ENABLE_IMU */

/* ------------------------------------------------------------------ ToF -- */

#define TOF_NODE DT_ALIAS(tof0)

/* The resolution this test ranges at. 12x10 is 880 bytes on the wire — about
 * 24 ms at 400 kHz — and it is in the WIDE family, so it shares the full
 * 54x42 field of view rather than a cropped one. That makes it a like-for-like
 * preview of what full resolution sees, at a twentieth of the bus time, and it
 * prints as a grid a human can actually read.
 */
#define TOF_RES  VL53L9CX_RES_12X10

static const struct device *const tof = DEVICE_DT_GET(TOF_NODE);

/* ~18 KB. Static: one of these is more than the whole main stack. */
static struct vl53l9cx_frame frame;

#if defined(CONFIG_APP_ENABLE_IMU)

/* Integer square root, so the magnitude check below needs no float printf
 * support. Newton's method; the inputs here are around 1e6 so it converges in
 * a handful of iterations.
 */
static uint32_t isqrt(uint64_t n)
{
	uint64_t x = n;
	uint64_t y = (x + 1) / 2;

	if (n == 0) {
		return 0;
	}
	while (y < x) {
		x = y;
		y = (x + n / x) / 2;
	}
	return (uint32_t)x;
}

/*
 * Identify the part before reading anything from it.
 *
 * This is not ceremony. The two parts store their accelerometer axes in
 * opposite order within the same six-byte block, so getting it wrong swaps X
 * and Z silently — a board lying flat would report gravity on the wrong axis
 * and nothing would look broken. Establishing which part is present is the
 * difference between a reading and a plausible-looking lie.
 */
static int pick_variant(void)
{
	uint8_t id = 0;
	int ret;

	ret = i2c_reg_read_byte(imu_bus, IMU_ADDR, REG_WHO_AM_I, &id);
	if (ret < 0) {
		LOG_ERR("WHO_AM_I read failed (%d) — nothing answered at 0x%02x",
			ret, IMU_ADDR);
		LOG_ERR("  Check, in order: SA0 strap (0x6B vs 0x6A), SDA/SCL "
			"pull-ups (the board pinctrl sets none), and that the "
			"part is powered.");
		return ret;
	}

	LOG_INF("WHO_AM_I = 0x%02x", id);

	switch (id) {
	case ID_16BX:
		layout = &layout_16bx;
		LOG_INF("  -> LSM6DSV16BX. Accel block at 0x%02x, axes REVERSED "
			"(Z,Y,X).", OUT_A_BASE);
		LOG_INF("  -> No in-tree Zephyr driver for this variant; see "
			"docs/plan/imu-lsm6dsv-bx.md for what a real driver takes.");
		return 0;

	case ID_16X:
		layout = &layout_16x;
		LOG_INF("  -> LSM6DSV16X / LSM6DSV. Accel block at 0x%02x, axes "
			"in order (X,Y,Z).", OUT_A_BASE);
		LOG_INF("  -> GOOD NEWS: Zephyr ships an in-tree driver for this "
			"one. A proper integration is a devicetree node, not a "
			"new driver.");
		return 0;

	default:
		LOG_ERR("  -> UNRECOGNISED. Expected 0x%02x (16BX) or 0x%02x "
			"(16X).", ID_16BX, ID_16X);
		LOG_ERR("  Something answered, so the bus works — but this is "
			"not the part we planned for. Identify it before "
			"trusting any register offset.");
		return -ENODEV;
	}
}

static int imu_configure(void)
{
	int ret;

	ret = i2c_reg_write_byte(imu_bus, IMU_ADDR, REG_CTRL8, CTRL8_FS_2G);
	if (ret < 0) {
		LOG_ERR("CTRL8 write failed (%d)", ret);
		return ret;
	}

	ret = i2c_reg_write_byte(imu_bus, IMU_ADDR, REG_CTRL1, CTRL1_XL_60HZ_HP);
	if (ret < 0) {
		LOG_ERR("CTRL1 write failed (%d)", ret);
		return ret;
	}

	/* Read them back. A write that is silently dropped and a write that
	 * lands look identical from here otherwise, and on an unproven bus
	 * that distinction is the whole point of this exercise.
	 */
	uint8_t ctrl1 = 0, ctrl8 = 0;

	(void)i2c_reg_read_byte(imu_bus, IMU_ADDR, REG_CTRL1, &ctrl1);
	(void)i2c_reg_read_byte(imu_bus, IMU_ADDR, REG_CTRL8, &ctrl8);

	LOG_INF("configured: CTRL1=0x%02x (wrote 0x%02x), CTRL8=0x%02x (wrote 0x%02x)",
		ctrl1, CTRL1_XL_60HZ_HP, ctrl8, CTRL8_FS_2G);

	if (ctrl1 != CTRL1_XL_60HZ_HP || ctrl8 != CTRL8_FS_2G) {
		LOG_WRN("  readback does not match — writes are not sticking. "
			"Readings below are not trustworthy.");
	}

	return 0;
}

static void imu_read_and_log(void)
{
	uint8_t raw[6];
	uint8_t status = 0;
	int16_t x, y, z;
	int32_t x_mg, y_mg, z_mg;
	uint32_t mag_mg;
	int ret;

	ret = i2c_reg_read_byte(imu_bus, IMU_ADDR, REG_STATUS, &status);
	if (ret < 0) {
		LOG_ERR("STATUS read failed (%d)", ret);
		return;
	}

	if ((status & 0x01) == 0) {
		/* XLDA clear. At 60 Hz against a 1 Hz loop this should never
		 * happen, so it means the ODR write did not take.
		 */
		LOG_WRN("no new accel sample (STATUS=0x%02x) — is the ODR set?",
			status);
		return;
	}

	ret = i2c_burst_read(imu_bus, IMU_ADDR, OUT_A_BASE, raw, sizeof(raw));
	if (ret < 0) {
		LOG_ERR("accel burst read failed (%d)", ret);
		return;
	}

	/* Little-endian int16 per axis, at the offsets this variant uses. */
	x = (int16_t)((raw[layout->x + 1] << 8) | raw[layout->x]);
	y = (int16_t)((raw[layout->y + 1] << 8) | raw[layout->y]);
	z = (int16_t)((raw[layout->z + 1] << 8) | raw[layout->z]);

	x_mg = ((int32_t)x * MG_PER_LSB_NUM) / MG_PER_LSB_DEN;
	y_mg = ((int32_t)y * MG_PER_LSB_NUM) / MG_PER_LSB_DEN;
	z_mg = ((int32_t)z * MG_PER_LSB_NUM) / MG_PER_LSB_DEN;

	mag_mg = isqrt((uint64_t)(x_mg * x_mg) + (uint64_t)(y_mg * y_mg) +
		       (uint64_t)(z_mg * z_mg));

	LOG_INF("accel  X %6d mg   Y %6d mg   Z %6d mg   |a| %5u mg   "
		"(raw %6d %6d %6d)",
		x_mg, y_mg, z_mg, mag_mg, x, y, z);

	/* The one check worth doing automatically. Whatever the orientation,
	 * a board at rest measures one gravity. If the magnitude is wrong the
	 * scaling or the register base is wrong; if only the axes look odd,
	 * that is orientation and can wait.
	 */
	if (mag_mg < 800 || mag_mg > 1200) {
		LOG_WRN("  |a| is %u mg, expected ~1000 at rest. Suspect the "
			"full-scale setting or the axis layout before suspecting "
			"the sensor.", mag_mg);
		LOG_WRN("  Two axes reading exactly 0 means the block base is "
			"wrong; all three plausible but small means the scale is.");
	}
}

#endif /* CONFIG_APP_ENABLE_IMU */

/*
 * Say what the devicetree claims before touching the sensor.
 *
 * All of it is confirmed against the schematic now, so this is no longer a
 * warning — it is a record. A wrong rail value would not fail loudly, it would
 * misconfigure the analogue front end and return plausible rubbish, so having
 * the values in the log means any future reading can be checked against the
 * configuration that produced it.
 */
static void tof_report_config(void)
{
	LOG_INF("---- VL53L9CX configuration (from devicetree) ----");
	LOG_INF("  address   0x%02x (7-bit)", DT_REG_ADDR(TOF_NODE));
	LOG_INF("  VDDA      %d uV", DT_PROP(TOF_NODE, vdda_microvolt));
	LOG_INF("  VDDIO     %d uV", DT_PROP(TOF_NODE, vddio_microvolt));
	LOG_INF("  AP_CLK    %d Hz on P0.00 (GRTC clkout-fast, always on)",
		DT_PROP(TOF_NODE, ext_clock_frequency));
	LOG_INF("  XSHUT     %s",
		DT_NODE_HAS_PROP(TOF_NODE, xshut_gpios) ? "P1.07" : "NOT WIRED — no reset control");
	LOG_INF("  INT       %s",
		DT_NODE_HAS_PROP(TOF_NODE, int_gpios) ? "P0.01, active low" : "NOT WIRED — polling");
	LOG_INF("  PWR_EN    %s",
		DT_NODE_HAS_PROP(TOF_NODE, power_gpios) ? "P0.02" : "NOT WIRED — cannot power down");

	/*
	 * Ask the GRTC peripheral whether its fast clock output is actually
	 * enabled.
	 *
	 * This is the one piece of the AP_CLK chain testable in software.
	 * Devicetree saying 8 MHz proves only that a property was written; this
	 * proves the timer driver acted on it and set the enable bit. It does
	 * NOT prove the pin is toggling — pinctrl could have routed it
	 * elsewhere, or the pad could be loaded — so a scope is still the last
	 * word. But if this reads DISABLED, the scope will show nothing and
	 * there is no point looking.
	 */
	LOG_INF("  AP_CLK enable bit in the GRTC peripheral: %s",
		nrfy_grtc_clkout_enable_check(NRF_GRTC, NRF_GRTC_CLKOUT_FAST)
			? "ENABLED" : "DISABLED  <-- the clock is definitely not running");

	/*
	 * And separately: is P0.00 actually handed to the GRTC?
	 *
	 * The enable bit above does NOT prove this. In nrf_grtc_timer.c the
	 * output is enabled at line 616 and pinctrl is applied at line 623, so
	 * a pinctrl failure leaves the bit set and the pin unrouted — the clock
	 * generating happily into nothing.
	 *
	 * CTRLSEL is the register field that says which peripheral owns a pin.
	 * GPIO means pinctrl never took it; GRTC means the routing is real and
	 * anything still wrong is on the board rather than in the firmware.
	 */
	{
		uint32_t cnf = NRF_P0->PIN_CNF[0];
		uint32_t sel = (cnf & GPIO_PIN_CNF_CTRLSEL_Msk)
			       >> GPIO_PIN_CNF_CTRLSEL_Pos;

		LOG_INF("  P0.00 CTRLSEL = %u (%s)", sel,
			sel == GPIO_PIN_CNF_CTRLSEL_GRTC
				? "GRTC — pin routing is real, so if there is no "
				  "8 MHz on the pad the fault is on the board"
				: "NOT GRTC — pinctrl did not take the pin, so "
				  "nothing is driving it. This is a firmware "
				  "fault, not a wiring one");
	}
	LOG_INF("--------------------------------------------------");
}

/*
 * One frame, printed two ways: a one-line summary for scanning past, and a
 * grid for actually seeing whether the sensor is looking at the room.
 *
 * The grid is the point. Summary statistics can look healthy while the frame
 * is garbage — a plausible mean over nonsense zones — whereas a grid of
 * distances either has the shape of the scene in it or it does not, and a
 * human spots that instantly.
 */
static void tof_capture_and_log(void)
{
	uint32_t valid = 0, sum = 0, min = UINT32_MAX, max = 0;
	int64_t t0 = k_uptime_get();
	int64_t took;
	int ret;

	ret = vl53l9cx_capture(tof, TOF_RES, &frame, K_SECONDS(2));
	took = k_uptime_get() - t0;

	if (ret < 0) {
		LOG_ERR("ToF capture failed (%d)", ret);
		if (ret == -EAGAIN) {
			LOG_ERR("  timed out waiting for frame-ready. With no "
				"int-gpios this is polled, so it means the sensor "
				"never finished a measurement.");
		}
		return;
	}

	for (uint16_t i = 0; i < (uint16_t)frame.cols * frame.rows; i++) {
		if (!frame.zone[i].valid) {
			continue;
		}
		valid++;
		sum += frame.zone[i].distance_mm;
		min = MIN(min, frame.zone[i].distance_mm);
		max = MAX(max, frame.zone[i].distance_mm);
	}

	LOG_INF("ToF %ux%u in %lld ms — %u/%u zones valid",
		frame.cols, frame.rows, took, valid,
		(unsigned)frame.cols * frame.rows);

	if (valid == 0) {
		LOG_WRN("  no valid zones at all. Either nothing is within "
			"range, or the depth word is being read wrong — check "
			"byte order before blaming the scene.");
		return;
	}

	LOG_INF("  distance  min %u mm   mean %u mm   max %u mm",
		min, sum / valid, max);

	/* The device's own frame counter, from the status line. If this stops
	 * advancing while our sequence does, we are re-reading a stale buffer.
	 * Nothing else makes that visible.
	 */
	LOG_INF("  device frame %u (seq %u), temperature raw %u",
		frame.frame_counter, frame.seq, frame.temperature);

	/* The grid: centimetres per zone, '.' where there is no target.
	 * Centimetres rather than millimetres purely so the columns line up in
	 * three characters at everything up to 9.99 m.
	 */
	LOG_INF("  distances in cm ('   .' = no target):");

	for (uint8_t r = 0; r < frame.rows; r++) {
		char line[VL53L9CX_COLS_FULL * 4 + 1];
		int n = 0;

		for (uint8_t c = 0; c < frame.cols; c++) {
			const struct vl53l9cx_zone *z =
				&frame.zone[vl53l9cx_idx(&frame, c, r)];

			if (z->valid) {
				n += snprintf(&line[n], sizeof(line) - n, "%4u",
					      z->distance_mm / 10U);
			} else {
				n += snprintf(&line[n], sizeof(line) - n, "   .");
			}
		}
		LOG_INF("   %s", line);
	}
}

int main(void)
{
	uint32_t beat = 0;
	bool tof_ok;
#if defined(CONFIG_APP_ENABLE_IMU)
	bool imu_ok;
#endif

	LOG_INF("========================================");
	LOG_INF(" water_sense_board is alive");
	LOG_INF(" board  : %s", CONFIG_BOARD_TARGET);
	LOG_INF(" zephyr : %s", KERNEL_VERSION_STRING);
	LOG_INF(" built  : " __DATE__ " " __TIME__);
	LOG_INF("========================================");

#if defined(CONFIG_APP_ENABLE_IMU)
	/* Stage 2 — the IMU. Failures here are reported and then ignored: the
	 * heartbeat carries on either way, because "the MCU runs but the IMU
	 * does not" is a useful state to be able to observe rather than a
	 * reason to stop.
	 */
	imu_ok = false;

	if (!device_is_ready(imu_bus)) {
		LOG_ERR("I2C bus %s not ready", imu_bus->name);
	} else {
		LOG_INF("I2C bus %s ready, probing IMU at 0x%02x",
			imu_bus->name, IMU_ADDR);

		if (pick_variant() == 0 && imu_configure() == 0) {
			imu_ok = true;
		}
	}

	if (!imu_ok) {
		LOG_WRN("IMU not usable — continuing without it.");
	}
#else
	LOG_INF("IMU stage disabled (CONFIG_APP_ENABLE_IMU=n) — part not fitted.");
	LOG_WRN("So nothing else on this I2C bus is answering, and the bus is "
		"therefore UNPROVEN. A silent VL53L9CX below could be a bus "
		"fault as easily as a sensor fault.");
#endif

	/* Stage 3 — the VL53L9CX. Same policy: report and carry on. */
	tof_report_config();

	tof_ok = device_is_ready(tof);
	if (!tof_ok) {
		LOG_ERR("VL53L9CX not ready — its init failed, which means the "
			"firmware blob upload did not complete.");
		LOG_ERR("  In order: (1) AP_CLK actually present on P0.00 — no "
			"clock, no ACK, and it looks exactly like a dead sensor; "
			"(2) the sensor rail coming up on P0.02; (3) XSHUT "
			"rising on P1.07; (4) the address.");
#if defined(CONFIG_APP_ENABLE_IMU)
		LOG_ERR("  Note the IMU result above: if that worked, the I2C "
			"bus is proven and the fault is on the ToF side.");
#else
		LOG_ERR("  With the IMU unfitted there is no second device to "
			"exonerate the bus, so SDA/SCL and the pull-ups are "
			"suspects here too — not just the sensor.");
#endif
	} else {
		LOG_INF("VL53L9CX ready. Firmware blob upload took %u ms.",
			vl53l9cx_last_boot_ms(tof));
	}

	while (true) {
		LOG_INF("heartbeat %u  (uptime %lld ms)", beat, k_uptime_get());

#if defined(CONFIG_APP_ENABLE_IMU)
		if (imu_ok) {
			imu_read_and_log();
		}
#endif

		/* Ranging every fifth beat. A 12x10 capture is cheap, but a
		 * ten-row grid once a second buries everything else.
		 */
		if (tof_ok && (beat % 5 == 0)) {
			tof_capture_and_log();
		}

		beat++;
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
