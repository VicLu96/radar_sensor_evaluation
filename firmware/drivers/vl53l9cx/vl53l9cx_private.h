/*
 * Driver-private state, shared between the platform layer and the Zephyr
 * device driver.
 *
 * ST's driver reaches us through an opaque `void *const p_dev`, and it never
 * dereferences it — it only hands it back to our platform functions. So we
 * pass the Zephyr `const struct device *` straight through and cast it back
 * here. That is the whole trick, and it is why this port needs no shadow
 * device struct of its own.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VL53L9CX_PRIVATE_H_
#define VL53L9CX_PRIVATE_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include <vl53l9cx/vl53l9cx.h>

/* Worst case raw frame: binning 2 (54x42).
 * 2268 zones * 3 planes * 2 bytes + 1134 DSS + 100 status. Source: RAW_BUFFER_SIZE
 * in st/vl53l9.c:84, verified 2026-09-01. Do NOT recompute this from zone count
 * alone — that is how the earlier 13,608-byte figure went wrong.
 */
#define VL53L9CX_RAW_BUF_MAX 14842U

/* The status line trailing every frame is ST's vl53l9_meta_t. We read the two
 * fields at its head explicitly rather than casting the bitfield struct over
 * wire data: the tail of that struct is compiler-layout dependent, and a
 * silently wrong bitfield here would corrupt the very telemetry we are adding
 * in order to trust the bring-up.
 */
#define VL53L9CX_META_FRAME_COUNTER 0U /* uint32, LE, offset into status line */
#define VL53L9CX_META_TEMPERATURE   4U /* uint16, LE */

struct vl53l9cx_config {
	struct i2c_dt_spec i2c;

	/* All three optional — a board may tie XSHUT high, poll instead of
	 * using the interrupt, or have no switchable sensor domain.
	 */
	struct gpio_dt_spec xshut;
	struct gpio_dt_spec intr;
	struct gpio_dt_spec power;

	/* AP_CLK. Either the board has its own oscillator (clock_from_pwm
	 * false, and we only need to know the frequency), or the MCU generates
	 * it (clock_from_pwm true, and we must start it before first contact).
	 * The sensor does not acknowledge its I2C address without this clock.
	 */
	struct pwm_dt_spec clk;
	bool clock_from_pwm;
	uint32_t ext_clock_hz;

	uint16_t blob_chunk_size;

	uint8_t vdda;  /* vl53l9_vdda_t  — VDDA_2V8 / VDDA_3V3   */
	uint8_t vddio; /* vl53l9_vddio_t — VDDIO_1V2 / VDDIO_1V8 */
};

struct vl53l9cx_data {
	const struct device *dev;

	struct gpio_callback int_cb;
	struct k_sem frame_ready;
	struct k_mutex lock;

	/* Duration of the last firmware blob upload. A measurement the power
	 * model needs, not a debug line — see vl53l9cx_last_boot_ms().
	 */
	uint32_t boot_ms;

	uint8_t binning;  /* ST's binning value, 2/4/6/8/12/24 */
	uint8_t cols;     /* logical, after any on-device crop */
	uint8_t rows;
	uint8_t tx_rows;  /* transmitted rows — square formats send more */
	uint8_t y_offset; /* crop offset for the square formats */

	bool streaming;
	bool use_interrupt;

	uint32_t seq;

	uint8_t raw[VL53L9CX_RAW_BUF_MAX];
};

#endif /* VL53L9CX_PRIVATE_H_ */
