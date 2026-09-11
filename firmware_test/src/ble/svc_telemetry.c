/*
 * Telemetry service 53f92001: the bring-up diagnostics, made remote.
 *
 * Everything here was already being printed to RTT. The point of putting it on
 * the link is that RTT needs a J-Link cable and a viewer, and the moment this
 * node is on a ceiling bracket in a corner at 2.5 m, it has neither. A health
 * panel in the browser is the only way the device's own verdict on itself
 * survives deployment.
 *
 * Pushed on every command, every capture failure, and every tenth success -
 * not on a timer. A health characteristic that notifies at a fixed rate costs
 * radio time to say nothing; one that notifies on change says something every
 * time it arrives.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_ble.h"
#include "ble_uuid.h"
#include "../app_capture.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_ble, LOG_LEVEL_INF);

#define TOF_NODE DT_ALIAS(tof0)
static const struct device *const tof = DEVICE_DT_GET(TOF_NODE);

static bool health_subscribed;
static bool energy_subscribed;

static void build_health(struct app_health *h)
{
	struct app_capture_stats st;

	app_capture_get_stats(&st);

	h->flags = (app_capture_ready() ? BIT(0) : 0U) |
		   (app_ble_streaming() ? BIT(1) : 0U);
	h->error_status = st.last_error_status;
	h->error_code = st.last_error_code;
	h->last_errno = st.last_errno;
	h->reserved[0] = 0;
	h->reserved[1] = 0;
	h->reserved[2] = 0;
	h->capture_ok = st.ok;
	h->capture_failed = st.failed;
}

static void build_energy(struct app_energy *e)
{
	struct app_capture_stats st;
	const struct app_config *cfg = app_ble_config();

	app_capture_get_stats(&st);

	e->last_capture_ms = st.last_capture_ms;
	e->blob_upload_ms = (uint16_t)MIN(vl53l9cx_last_boot_ms(tof), UINT16_MAX);
	/*
	 * The LIVE exposure, not the configured one. The laser-fault backoff
	 * halves it, so these two can differ - and a frame whose exposure is
	 * unknown is not a usable energy measurement.
	 */
	e->exposure_ms = vl53l9cx_exposure_ms(tof);
	e->resolution = cfg->resolution;
	e->reserved = 0;
	e->frames_sent = app_stream_frames_sent();
	e->uptime_s = (uint32_t)(k_uptime_get() / 1000);
}

static ssize_t read_health(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset)
{
	struct app_health h;

	build_health(&h);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &h, sizeof(h));
}

static ssize_t read_energy(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset)
{
	struct app_energy e;

	build_energy(&e);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &e, sizeof(e));
}

static void health_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	health_subscribed = (value == BT_GATT_CCC_NOTIFY);
}

static void energy_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	energy_subscribed = (value == BT_GATT_CCC_NOTIFY);
}

BT_GATT_SERVICE_DEFINE(telemetry_svc,
	BT_GATT_PRIMARY_SERVICE(UUID_SVC_TELEMETRY),

	BT_GATT_CHARACTERISTIC(UUID_CHR_HEALTH,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_health, NULL, NULL),
	BT_GATT_CCC(health_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	BT_GATT_CHARACTERISTIC(UUID_CHR_ENERGY,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_energy, NULL, NULL),
	BT_GATT_CCC(energy_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/*
 * attrs[]: 0 service, 1 health decl, 2 health value, 3 health CCC,
 *          4 energy decl, 5 energy value, 6 energy CCC.
 */
#define ATTR_HEALTH_VALUE 2
#define ATTR_ENERGY_VALUE 5

void app_svc_telemetry_push(void)
{
	if (health_subscribed) {
		struct app_health h;

		build_health(&h);
		(void)bt_gatt_notify(NULL,
				     &telemetry_svc.attrs[ATTR_HEALTH_VALUE],
				     &h, sizeof(h));
	}

	if (energy_subscribed) {
		struct app_energy e;

		build_energy(&e);
		(void)bt_gatt_notify(NULL,
				     &telemetry_svc.attrs[ATTR_ENERGY_VALUE],
				     &e, sizeof(e));
	}
}
