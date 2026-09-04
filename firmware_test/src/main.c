/*
 * water_sense_board bring-up test.
 *
 * Two stages, in order, because each one only means something if the previous
 * one passed:
 *
 *   1. The MCU is alive and can talk to a host  — banner + heartbeat over RTT.
 *   2. The I2C bus works and the IMU is real    — WHO_AM_I, then accel XYZ.
 *
 * Nothing here touches the VL53L9CX. If the IMU talks and the ToF sensor does
 * not, the bus is proven and the fault is on the ToF side — which is worth a
 * great deal on a board where nothing else is known good.
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

LOG_MODULE_REGISTER(board_test, LOG_LEVEL_INF);

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
 * The addresses below are common to both variants we might have. The ONE that
 * differs is the accelerometer output block, and it differs silently — see
 * pick_variant().
 */
#define REG_WHO_AM_I   0x0F
#define REG_CTRL1      0x10 /* [3:0] ODR_XL, [6:4] OP_MODE_XL */
#define REG_CTRL8      0x17 /* [1:0] FS_XL                    */
#define REG_STATUS     0x1E /* bit 0 = XLDA, accel data ready */

#define OUTX_L_A_16X   0x28 /* LSM6DSV16X / LSM6DSV */
#define OUTX_L_A_16BX  0x2C /* LSM6DSV16BX          */

#define ID_16X         0x70
#define ID_16BX        0x71

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

/* Output register base, chosen from WHO_AM_I. Zero until identified. */
static uint8_t out_base;

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
 * This is not ceremony. The accelerometer output block sits at 0x28 on the
 * LSM6DSV16X and 0x2C on the LSM6DSV16BX, so reading the wrong one returns
 * four bytes of neighbouring registers and two bytes of real data — numbers
 * that look like data and are not. Establishing which part is present is the
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
		out_base = OUTX_L_A_16BX;
		LOG_INF("  -> LSM6DSV16BX. Accel output block at 0x%02x.", out_base);
		LOG_INF("  -> No in-tree Zephyr driver for this variant; see "
			"docs/plan/imu-lsm6dsv-bx.md for what a real driver takes.");
		return 0;

	case ID_16X:
		out_base = OUTX_L_A_16X;
		LOG_INF("  -> LSM6DSV16X / LSM6DSV. Accel output block at 0x%02x.",
			out_base);
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

	ret = i2c_burst_read(imu_bus, IMU_ADDR, out_base, raw, sizeof(raw));
	if (ret < 0) {
		LOG_ERR("accel burst read failed (%d)", ret);
		return;
	}

	/* Little-endian int16 per axis. */
	x = (int16_t)((raw[1] << 8) | raw[0]);
	y = (int16_t)((raw[3] << 8) | raw[2]);
	z = (int16_t)((raw[5] << 8) | raw[4]);

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
			"full-scale setting or the output register base before "
			"suspecting the sensor.", mag_mg);
	}
}

int main(void)
{
	uint32_t beat = 0;
	bool imu_ok;

	LOG_INF("========================================");
	LOG_INF(" water_sense_board is alive");
	LOG_INF(" board  : %s", CONFIG_BOARD_TARGET);
	LOG_INF(" zephyr : %s", KERNEL_VERSION_STRING);
	LOG_INF(" built  : " __DATE__ " " __TIME__);
	LOG_INF("========================================");

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
		LOG_WRN("IMU not usable — continuing with heartbeat only.");
	}

	while (true) {
		LOG_INF("heartbeat %u  (uptime %lld ms)", beat++, k_uptime_get());

		if (imu_ok) {
			imu_read_and_log();
		}

		k_sleep(K_SECONDS(1));
	}

	return 0;
}
