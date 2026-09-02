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
 * Resolution modes. Source-verified 2026-09-01 against vl53l9_set_binning()
 * in ST's driver (X-CUBE-53L9A1 v1.0.0).
 *
 * These six points are the energy-accuracy curve, but they are NOT one curve.
 * ST splits them into two families:
 *
 *   WIDE   — 54x42, 18x14, 12x10. Zones merged, no crop, SAME field of view.
 *   SQUARE — 24x20, 8x6, 4x4. A square array is transmitted and cropped
 *            on-device with a y-offset, so the field of view is DIFFERENT.
 *
 * A like-for-like energy-versus-zones comparison must stay inside the wide
 * family: 2268 -> 252 -> 120 zones, an 18.9x span at constant coverage.
 *
 * Bus time spans 72.8x across all six (14,842 down to 204 bytes), not the
 * ~150x an earlier estimate suggested — every frame carries a fixed 100-byte
 * status line, which dominates the smallest mode.
 *
 * Ordered coarse-to-fine so a sweep can walk the enum.
 */
enum vl53l9cx_res {
	VL53L9CX_RES_4X4 = 0,  /* 16 zones,   binning 24, 204 B    */
	VL53L9CX_RES_8X6,      /* 48 zones,   binning 12, 516 B, transmits 8x8   */
	VL53L9CX_RES_12X10,    /* 120 zones,  binning 8,  880 B    */
	VL53L9CX_RES_18X14,    /* 252 zones,  binning 6,  1,738 B  */
	VL53L9CX_RES_24X20,    /* 480 zones,  binning 4,  3,844 B, transmits 24x24 */
	VL53L9CX_RES_54X42,    /* 2268 zones, binning 2,  14,842 B — full */
	VL53L9CX_RES_COUNT,
};

#define VL53L9CX_COLS_FULL  54
#define VL53L9CX_ROWS_FULL  42
#define VL53L9CX_ZONES_FULL (VL53L9CX_COLS_FULL * VL53L9CX_ROWS_FULL)

/* Three uint16 per zone: depth, amplitude, ambient — but see the note on
 * struct vl53l9cx_zone: on the wire they are three separate PLANES, not three
 * words per zone.
 */
#define VL53L9CX_BYTES_PER_ZONE 6

/* Fixed metadata trailer on every frame, any resolution. ST's vl53l9_meta_t. */
#define VL53L9CX_STATUS_LINE_BYTES 100U

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

/* Per-zone target status. ST reports validity as a single flag in the depth
 * word; the richer codes below are ours, and only VALID and NO_TARGET are
 * populated today. Anything not VALID must not be treated as a distance —
 * dark hair and clothing at 940 nm produce exactly these, and silently
 * trusting them is how a person acquires a hole in the middle.
 */
enum vl53l9cx_zone_status {
	VL53L9CX_ZONE_VALID = 0,
	VL53L9CX_ZONE_NO_TARGET,
	VL53L9CX_ZONE_LOW_CONFIDENCE,
	VL53L9CX_ZONE_SATURATED,   /* ambient IR, e.g. direct sunlight */
	VL53L9CX_ZONE_INVALID,
};

/* One zone, unpacked.
 *
 * NOTE ON THE WIRE FORMAT. The device does not send three words per zone; it
 * sends three PLANES — all depths, then all amplitudes, then all ambients,
 * then a DSS array and the status line. The driver de-planarises here so no
 * consumer has to.
 *
 * Depth arrives as 15 bits of millimetres with a validity flag in bit 15,
 * little-endian. The driver splits them so no consumer has to remember the
 * mask — forgetting it yields distances around 32 m.
 */
struct vl53l9cx_zone {
	uint16_t distance_mm; /* bits 14:0 of the depth word */
	uint16_t amplitude;   /* return signal strength */
	uint16_t ambient;     /* background IR — rises in sunlight */
	uint8_t  valid;       /* bit 15 of the depth word: 1 = real measurement */
	uint8_t  status;      /* enum vl53l9cx_zone_status */
};

struct vl53l9cx_frame {
	uint32_t seq;             /* driver-side count; gaps mean we dropped one */
	uint32_t frame_counter;   /* the DEVICE's own counter, from the status
				   * line. Compare against seq to tell "the
				   * driver missed a frame" from "the sensor
				   * never produced one" — a distinction that
				   * is otherwise invisible.
				   */
	uint16_t temperature;     /* raw, from the status line. Scaling VERIFY. */
	int64_t  timestamp_ms;    /* k_uptime_get() at read */
	uint8_t  cols;
	uint8_t  rows;
	struct vl53l9cx_zone zone[VL53L9CX_ZONES_FULL];

	/* The rest of ST's metadata, unparsed. Its tail is bitfields whose
	 * layout is compiler-dependent, so the driver takes only the two plain
	 * fields above and hands the line over intact. It carries the eight
	 * health bits — VHV over/under-voltage, SPAD supply overload, HV boost
	 * limit, PLL lock, reference array, internal firmware — which on a
	 * board with no known-good reference are the only second opinion
	 * available. Log them from the first frame.
	 */
	uint8_t status_line[VL53L9CX_STATUS_LINE_BYTES];
};

/* Zone (col, row) -> index. Row-major, origin top-left as seen by the sensor.
 *
 * The driver does NOT rotate or flip. Orientation relative to the mounted
 * board is a board-file question. VERIFY at bring-up: the hardware-validated
 * community Python driver applies a 180-degree flip by default, which suggests
 * the sensor's natural raster order may not match the intuitive one. Point it
 * at an asymmetric scene and look before trusting any spatial logic.
 */
static inline uint16_t vl53l9cx_idx(const struct vl53l9cx_frame *f,
				    uint8_t col, uint8_t row)
{
	return (uint16_t)row * f->cols + col;
}

/**
 * Start continuous (autonomous) ranging.
 *
 * @param res        resolution mode
 * @param period_ms  frame period in milliseconds.
 *
 * A period rather than a rate in Hz: room dwell wants one frame every 5-20 s
 * (0.05-0.2 Hz), which an integer Hz cannot express at all.
 *
 * NOTE: the I2C bus, not the sensor, is usually the limit at high rates — a
 * full 54x42 frame is 14,842 bytes, roughly 404 ms at 400 kHz, so the ceiling
 * is about 2.5 fps.
 */
int vl53l9cx_start(const struct device *dev, enum vl53l9cx_res res,
		   uint32_t period_ms);

/** Stop ranging. The sensor stays powered and keeps its firmware. */
int vl53l9cx_stop(const struct device *dev);

/**
 * Take exactly one frame: configure, trigger, wait, read, stop.
 *
 * This is the room-dwell path. At a few percent duty cycle the sensor should
 * be off almost all the time, and a single-shot capture wrapped in a
 * PM_DEVICE_ACTION_TURN_OFF either side is what makes that possible — running
 * autonomous mode and discarding frames would spend the energy anyway.
 */
int vl53l9cx_capture(const struct device *dev, enum vl53l9cx_res res,
		     struct vl53l9cx_frame *out, k_timeout_t timeout);

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
 * powering the sensor down beats keeping it in standby. The blob is 9,865
 * bytes — about 250 ms at 400 kHz — so the crossover is expected to be short,
 * but this function is what turns that expectation into a number. Returns 0 if
 * the blob has not been uploaded this boot.
 */
uint32_t vl53l9cx_last_boot_ms(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* VL53L9CX_H_ */
