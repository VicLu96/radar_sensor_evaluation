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
 * Resolution modes, from the device's binning register. Source-verified
 * 2026-08-31 against a hardware-validated driver (vendor/vl53l9cx-python).
 *
 * These six points ARE the energy-accuracy curve: 142x in zone count and
 * roughly 150x in I2C transfer time between the extremes. Keep the enum
 * ordered coarse-to-fine so a sweep can walk it.
 *
 * Note the square formats: 24x20 and 8x6 transmit a SQUARE array (24x24, 8x8)
 * and the device crops rows on-device with a y-offset. The transmitted size,
 * not the logical size, is what costs bus time — see VL53L9CX_TX_ZONES.
 */
enum vl53l9cx_res {
	VL53L9CX_RES_4X4 = 0,  /* 16 zones,   binning 24 */
	VL53L9CX_RES_8X6,      /* 48 zones,   binning 12, transmits 8x8  */
	VL53L9CX_RES_12X10,    /* 120 zones,  binning 8  */
	VL53L9CX_RES_18X14,    /* 252 zones,  binning 6  */
	VL53L9CX_RES_24X20,    /* 480 zones,  binning 4, transmits 24x24 */
	VL53L9CX_RES_54X42,    /* 2268 zones, binning 2  - full */
	VL53L9CX_RES_COUNT,
};

#define VL53L9CX_COLS_FULL  54
#define VL53L9CX_ROWS_FULL  42
#define VL53L9CX_ZONES_FULL (VL53L9CX_COLS_FULL * VL53L9CX_ROWS_FULL)

/* Three uint16 per zone: depth, amplitude, ambient. */
#define VL53L9CX_BYTES_PER_ZONE 6

/* Zones actually transmitted, including the square-format padding. This is
 * what determines bus time and therefore energy, so it is the number the
 * power model wants — not the logical zone count.
 */
#define VL53L9CX_TX_ZONES(res)                       \
	((res) == VL53L9CX_RES_4X4    ? 16   :       \
	 (res) == VL53L9CX_RES_8X6    ? 64   :       \
	 (res) == VL53L9CX_RES_12X10  ? 120  :       \
	 (res) == VL53L9CX_RES_18X14  ? 252  :       \
	 (res) == VL53L9CX_RES_24X20  ? 576  :       \
	 VL53L9CX_ZONES_FULL)

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

/* Mirrors the device's on-wire layout: three uint16 per zone.
 *
 * Depth arrives as 15 bits of millimetres with a VALID flag in bit 15. The
 * driver splits them here so no consumer has to remember the mask — forgetting
 * it yields distances around 32 m and a person with a hole in the middle.
 */
struct vl53l9cx_zone {
	uint16_t distance_mm; /* bits 14:0 of the depth word */
	uint16_t amplitude;   /* return signal strength */
	uint16_t ambient;     /* background IR - rises in sunlight */
	uint8_t  valid;       /* bit 15 of the depth word: 1 = real measurement */
	uint8_t  status;      /* enum vl53l9cx_zone_status */
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
