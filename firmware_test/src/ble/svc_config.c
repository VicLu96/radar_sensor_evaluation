/*
 * Config service 53f91001: the live configuration, the command channel, and
 * the result notification.
 *
 * TWO RULES, both learned the hard way on this project.
 *
 * 1. WRITES ARE TRANSACTIONAL. Validate everything, then apply, then notify.
 *    Never partially apply - half a profile is how you get plausible rubbish,
 *    which is worse than an error because it looks like data.
 *
 * 2. NOTHING SLOW RUNS IN THE WRITE CALLBACK. It executes on the Bluetooth RX
 *    thread. vl53l9cx_retry_boot() takes 600 ms on success and up to 3.5 s on
 *    the failing path; blocking the host stack for that long drops the
 *    connection. Every command is therefore handed to a work queue and
 *    answered by notification when it has actually finished.
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

#if defined(CONFIG_APP_TOF_RES_4X4)
#define DEFAULT_RES VL53L9CX_RES_4X4
#elif defined(CONFIG_APP_TOF_RES_8X6)
#define DEFAULT_RES VL53L9CX_RES_8X6
#elif defined(CONFIG_APP_TOF_RES_12X10)
#define DEFAULT_RES VL53L9CX_RES_12X10
#elif defined(CONFIG_APP_TOF_RES_18X14)
#define DEFAULT_RES VL53L9CX_RES_18X14
#elif defined(CONFIG_APP_TOF_RES_24X20)
#define DEFAULT_RES VL53L9CX_RES_24X20
#else
#define DEFAULT_RES VL53L9CX_RES_54X42
#endif

/*
 * Defaults on boot, and there is no NVS by decision (2026-09-10). A node that
 * comes up in a known state every time is easier to reason about than one
 * carrying a configuration somebody set three sessions ago.
 *
 * mode starts IDLE: the sensor does not range until the web interface asks. At
 * 450-800 mW that is not a detail.
 */
static struct app_config cfg = {
	.protocol_version = APP_PROTOCOL_VERSION,
	.resolution = DEFAULT_RES,
	.planes = APP_PLANE_DISTANCE,
	.instance_id = CONFIG_APP_BLE_INSTANCE_ID,
	.exposure_ms = CONFIG_VL53L9CX_EXPOSURE_MS,
	.frame_period_ms = 0,
	.adv_interval_ms = 1000,
	.mode = APP_MODE_IDLE,
	.flags = 0,
};

const struct app_config *app_ble_config(void)
{
	return &cfg;
}

bool app_ble_streaming(void)
{
	return cfg.mode == APP_MODE_STREAMING && app_ble_connected() &&
	       app_capture_ready();
}

/* --- Config Result notification ------------------------------------------ */

static bool result_subscribed;

static void result_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	result_subscribed = (value == BT_GATT_CCC_NOTIFY);
}

/* --- Command execution, off the Bluetooth RX thread ---------------------- */

static struct k_work cmd_work;
static struct app_command pending;

static void cmd_handler(struct k_work *work)
{
	int16_t status = 0;

	ARG_UNUSED(work);

	switch (pending.opcode) {
	case APP_CMD_START:
		cfg.mode = APP_MODE_STREAMING;
		LOG_INF("command START - streaming at resolution %u",
			cfg.resolution);
		app_stream_kick();
		break;

	case APP_CMD_STOP:
		cfg.mode = APP_MODE_IDLE;
		LOG_INF("command STOP");
		app_stream_kick();
		break;

	case APP_CMD_SINGLE_SHOT: {
		int ret = app_capture_once((enum vl53l9cx_res)cfg.resolution,
					   K_SECONDS(5));
		status = (int16_t)ret;
		LOG_INF("command SINGLE_SHOT -> %d", ret);
		break;
	}

	case APP_CMD_REBOOT_SENSOR: {
		int ret;

		cfg.mode = APP_MODE_IDLE;
		app_stream_kick();
		LOG_INF("command REBOOT_SENSOR - this takes up to 3.5 s");
		ret = vl53l9cx_retry_boot(tof);
		app_capture_set_ready(ret == 0);
		status = (int16_t)ret;
		break;
	}

	case APP_CMD_CALIBRATE:
	case APP_CMD_CLEAR_CALIBRATION:
		/*
		 * Phase 6. The opcodes are frozen now so the web interface can
		 * be written against them, and answering -ENOTSUP is far more
		 * useful than answering nothing at all.
		 */
		status = -ENOTSUP;
		LOG_WRN("command %u is phase 6 and not implemented yet",
			pending.opcode);
		break;

	default:
		status = -EINVAL;
		break;
	}

	app_svc_telemetry_push();
	app_svc_config_notify_result(pending.opcode, status, 0);
}

/* --- Config characteristic ----------------------------------------------- */

static ssize_t read_config(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &cfg,
				 sizeof(cfg));
}

static ssize_t write_config(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset,
			    uint8_t flags)
{
	struct app_config in;
	int ret;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != sizeof(in)) {
		LOG_WRN("config write is %u bytes, expected %u", len,
			(unsigned int)sizeof(in));
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(&in, buf, sizeof(in));

	/* --- validate EVERYTHING before applying ANYTHING --- */
	if (in.protocol_version != APP_PROTOCOL_VERSION) {
		LOG_WRN("config write rejected: it speaks protocol %u, we speak %u",
			in.protocol_version, APP_PROTOCOL_VERSION);
		app_svc_config_notify_result(APP_CMD_NONE, -EPROTO, 0);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	if (in.resolution >= VL53L9CX_RES_COUNT) {
		app_svc_config_notify_result(APP_CMD_NONE, -EINVAL, 1);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	if (in.planes == 0U || in.planes > 7U) {
		app_svc_config_notify_result(APP_CMD_NONE, -EINVAL, 2);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	/* ST's own limit. 1-100 is the Kconfig range; ST's profiles span 4-10. */
	if (in.exposure_ms < 1U || in.exposure_ms > 100U) {
		app_svc_config_notify_result(APP_CMD_NONE, -EINVAL, 3);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	if (in.mode > APP_MODE_STREAMING) {
		app_svc_config_notify_result(APP_CMD_NONE, -EINVAL, 4);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	/*
	 * Exposure goes through the driver, which invalidates its resolution
	 * cache so the STANDBY-only registers are rewritten on the next
	 * capture. Skipping that invalidation is exactly the bug that left
	 * reset values in place after a recovery on 2026-09-10.
	 */
	if (in.exposure_ms != cfg.exposure_ms) {
		ret = vl53l9cx_set_exposure_ms(tof, in.exposure_ms);
		if (ret) {
			LOG_ERR("set exposure %u ms failed (%d)",
				in.exposure_ms, ret);
			app_svc_config_notify_result(APP_CMD_NONE, (int16_t)ret, 3);
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}
	}

	cfg.resolution = in.resolution;
	cfg.planes = in.planes;
	cfg.instance_id = in.instance_id;
	cfg.exposure_ms = in.exposure_ms;
	cfg.frame_period_ms = in.frame_period_ms;
	cfg.adv_interval_ms = in.adv_interval_ms;
	cfg.mode = in.mode;

	LOG_INF("config applied: res %u, planes 0x%02x, exposure %u ms, "
		"period %u ms, mode %s",
		cfg.resolution, cfg.planes, cfg.exposure_ms,
		cfg.frame_period_ms,
		cfg.mode == APP_MODE_STREAMING ? "STREAMING" : "idle");

	app_stream_kick();
	app_svc_config_notify_result(APP_CMD_NONE, 0, 0);
	return len;
}

static ssize_t write_command(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len, uint16_t offset,
			     uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != sizeof(struct app_command)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(&pending, buf, sizeof(pending));

	/* Off this thread. See the file header. */
	k_work_submit(&cmd_work);
	return len;
}

BT_GATT_SERVICE_DEFINE(config_svc,
	BT_GATT_PRIMARY_SERVICE(UUID_SVC_CONFIG),

	BT_GATT_CHARACTERISTIC(UUID_CHR_CONFIG,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_config, write_config, NULL),

	BT_GATT_CHARACTERISTIC(UUID_CHR_COMMAND,
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE,
			       NULL, write_command, NULL),

	BT_GATT_CHARACTERISTIC(UUID_CHR_CFG_RESULT,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(result_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/*
 * attrs[] index 6 is the Config Result VALUE.
 *
 * The table above is: 0 service, 1 config decl, 2 config value, 3 command decl,
 * 4 command value, 5 result decl, 6 result value, 7 result CCC. Getting this
 * index wrong notifies the wrong characteristic and is silent on both ends,
 * which is why it is counted out here rather than left as a literal.
 */
#define ATTR_CFG_RESULT_VALUE 6

void app_svc_config_notify_result(uint8_t opcode, int16_t status,
				  uint8_t detail)
{
	struct app_cfg_result res = {
		.opcode = opcode,
		.detail = detail,
		.status = status,
	};

	if (!result_subscribed) {
		return;
	}
	(void)bt_gatt_notify(NULL, &config_svc.attrs[ATTR_CFG_RESULT_VALUE],
			     &res, sizeof(res));
}

int app_svc_config_init(void)
{
	k_work_init(&cmd_work, cmd_handler);

	LOG_INF("config service ready - res %u, planes 0x%02x, exposure %u ms, "
		"instance %u, mode idle",
		cfg.resolution, cfg.planes, cfg.exposure_ms, cfg.instance_id);
	return 0;
}
