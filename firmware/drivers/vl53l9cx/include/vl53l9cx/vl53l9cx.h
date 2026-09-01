/*
 * Public API for the VL53L9CX Zephyr driver.
 *
 * Deliberately NOT Zephyr's sensor API. That API is built around scalar
 * channels fetched one at a time; this device produces a 2268-zone frame.
 * Forcing it through sensor_channel_get would mean either 2268 calls or a
 * channel that lies about what it returns. A small custom API is honest and
 * costs nothing — a thin sensor-API shim returning the centre zone can be
 * added later if something generic needs one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VL53L9CX_H_
#define VL53L9CX_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolution modes.
 *
 * VERIFY against X-CUBE-53L9A1: only the full 54x42 grid is confirmed from the
 * datasheet. The reduced modes below are the expected shape by analogy with the
 * VL53L5CX (4x4 / 8x8), and the whole energy-accuracy curve in the paper
 * depends on which actually exist. Treat anything other than FULL as
 * provisional until the ST headers are read.
 */
enum vl53l9cx_res {
	VL53L9CX_RES_FULL = 0,   /* 54 x 42 = 2268 zones - confirmed */
	VL53L9CX_RES_HALF,       /* VERIFY - expected ~27 x 21 */
	VL53L9CX_RES_QUARTER,    /* VERIFY */
};

#define VL53L9CX_COLS_FULL  54
#define VL53L9CX_ROWS_FULL  42
#define VL53L9CX_ZONES_FULL (VL53L9CX_COLS_FULL * VL53L9CX_ROWS_FULL)

/* Per-zone target status. ST uses a wider set of codes; these are the ones a
 * consumer must branch on. Anything not VALID must not be treated as a
 * distance — dark hair and clothing at 940 nm produce exactly these, and
 * silently trusting them is how a person acquires a hole in the middle.
 */
enum vl53l9cx_zone_status {
	VL53L9CX_ZONE_VALID = 0,
	VL53L9CX_ZONE_NO_TARGET,
	VL53L9CX_ZONE_LOW_CONFIDENCE,
	VL53L9CX_ZONE_SATURATED,   /* ambient IR, e.g. direct sunlight */
	VL53L9CX_ZONE_INVALID,
};

struct vl53l9cx_zone {
	uint16_t distance_mm;
	uint8_t  status;      /* enum vl53l9cx_zone_status */
	uint8_t  reflectance; /* 0 when not requested */
};

struct vl53l9cx_frame {
	uint32_t seq;             /* increments per frame; gaps mean drops */
	int64_t  timestamp_ms;    /* k_uptime_get() at data-ready */
	uint8_t  cols;
	uint8_t  rows;
	struct vl53l9cx_zone zone[VL53L9CX_ZONES_FULL];
};

/* Zone (col, row) -> index. Row-major, origin top-left as seen by the sensor.
 * Orientation relative to the mounted board is a board-file question and is
 * deliberately not baked in here.
 */
static inline uint16_t vl53l9cx_idx(const struct vl53l9cx_frame *f,
				    uint8_t col, uint8_t row)
{
	return (uint16_t)row * f->cols + col;
}

/**
 * Start continuous ranging.
 *
 * @param res  resolution mode
 * @param hz   target frame rate. NOTE: the I2C bus, not the sensor, is usually
 *             the limit — a full frame is ~9 KB, roughly 225 ms at 400 kHz. A
 *             requested rate the bus cannot sustain is clamped, and the driver
 *             logs a warning rather than silently dropping frames.
 */
int vl53l9cx_start(const struct device *dev, enum vl53l9cx_res res, uint8_t hz);

/** Stop ranging. The sensor stays powered and keeps its firmware. */
int vl53l9cx_stop(const struct device *dev);

/**
 * Wait for and copy the next frame.
 *
 * Blocks until data-ready or timeout. Returns -EAGAIN on timeout, which is
 * information rather than an error: at low duty cycles a timeout usually means
 * the caller's period is shorter than the sensor's integration time.
 */
int vl53l9cx_get_frame(const struct device *dev, struct vl53l9cx_frame *out,
		       k_timeout_t timeout);

/**
 * Duration of the last firmware blob upload, in milliseconds.
 *
 * Exposed because it is a measurement, not a debug detail. It sets the cost of
 * PM_DEVICE_ACTION_TURN_OFF and therefore the idle period beyond which fully
 * powering the sensor down beats keeping it in standby. Returns 0 if the blob
 * has not been uploaded this boot.
 */
uint32_t vl53l9cx_last_boot_ms(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* VL53L9CX_H_ */
