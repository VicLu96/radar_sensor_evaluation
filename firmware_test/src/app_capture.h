/*
 * The one frame buffer, and the one place that fills it.
 *
 * Why this file exists: with BLE added there are now two things that want to
 * range — the RTT bring-up path in main() and the BLE streaming thread — and a
 * struct vl53l9cx_frame is 18 KB. Giving each its own buffer would cost 36 KB
 * of the 188 KB on this part for no reason, and letting both drive the sensor
 * concurrently would interleave STANDBY transitions, which is exactly the class
 * of race that cost 2026-09-10.
 *
 * So: one buffer, one mutex, one entry point. The two callers are mutually
 * exclusive by policy as well (main() skips its capture while streaming is on),
 * but the mutex is what makes that a guarantee rather than an assumption.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APP_CAPTURE_H_
#define APP_CAPTURE_H_

#include <zephyr/kernel.h>
#include <vl53l9cx/vl53l9cx.h>

/** Running totals, for the heartbeat line and the BLE telemetry service. */
struct app_capture_stats {
	uint32_t attempts;
	uint32_t ok;
	uint32_t failed;
	uint16_t last_capture_ms;  /* wall time of the last successful capture */
	uint16_t last_error_code;  /* ERROR_CODE from the last frame's status line */
	uint8_t  last_error_status;/* ERROR_STATUS, the eight health bits */
	int8_t   last_errno;       /* 0, or the errno of the last failure */
};

/**
 * Capture one frame into the shared buffer.
 *
 * Takes the capture lock for the duration. On success the frame is readable
 * between app_capture_lock() and app_capture_unlock().
 *
 * @return 0, or a negative errno from the driver.
 */
int app_capture_once(enum vl53l9cx_res res, k_timeout_t timeout);

/** The shared frame. Only valid while the capture lock is held. */
struct vl53l9cx_frame *app_capture_frame(void);

void app_capture_lock(void);
void app_capture_unlock(void);

/** A snapshot of the counters. Safe to call at any time. */
void app_capture_get_stats(struct app_capture_stats *out);

/** Set by main() once the sensor has booted; cleared when it stops answering. */
void app_capture_set_ready(bool ready);
bool app_capture_ready(void);

#endif /* APP_CAPTURE_H_ */
