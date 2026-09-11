/* SPDX-License-Identifier: Apache-2.0 */

#include "app_capture.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_capture, LOG_LEVEL_INF);

#define TOF_NODE DT_ALIAS(tof0)
static const struct device *const tof = DEVICE_DT_GET(TOF_NODE);

static struct vl53l9cx_frame frame;
static K_MUTEX_DEFINE(lock);
static struct app_capture_stats stats;
static bool ready;

struct vl53l9cx_frame *app_capture_frame(void)
{
	return &frame;
}

void app_capture_lock(void)
{
	k_mutex_lock(&lock, K_FOREVER);
}

void app_capture_unlock(void)
{
	k_mutex_unlock(&lock);
}

void app_capture_set_ready(bool r)
{
	ready = r;
}

bool app_capture_ready(void)
{
	return ready;
}

void app_capture_get_stats(struct app_capture_stats *out)
{
	k_mutex_lock(&lock, K_FOREVER);
	*out = stats;
	k_mutex_unlock(&lock);
}

int app_capture_once(enum vl53l9cx_res res, k_timeout_t timeout)
{
	int64_t t0;
	int ret;

	k_mutex_lock(&lock, K_FOREVER);

	t0 = k_uptime_get();
	stats.attempts++;
	ret = vl53l9cx_capture(tof, res, &frame, timeout);

	if (ret < 0) {
		stats.failed++;
		stats.last_errno = (int8_t)ret;
		k_mutex_unlock(&lock);
		return ret;
	}

	stats.ok++;
	stats.last_errno = 0;
	stats.last_capture_ms = (uint16_t)MIN(k_uptime_get() - t0, UINT16_MAX);

	/*
	 * The device's own verdict on itself, pulled out of the status line so
	 * the telemetry service does not have to re-derive it.
	 *
	 * Offsets are UM3683 Table 7, verified field by field on 2026-09-06:
	 * ERROR_CODE 0x0064 -> byte 60, ERROR_STATUS 0x0066 -> byte 62.
	 */
	stats.last_error_code = (uint16_t)frame.status_line[60] |
				((uint16_t)frame.status_line[61] << 8);
	stats.last_error_status = frame.status_line[62];

	k_mutex_unlock(&lock);
	return 0;
}
