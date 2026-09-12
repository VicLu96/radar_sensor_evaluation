/*
 * BLE for the water_sense_board node: the wire format, and the small API the
 * rest of the firmware uses.
 *
 * EVERY STRUCT HERE IS A WIRE FORMAT. Its mirror is
 * webinterface/lib/protocol.ts, decoded with DataView and explicit
 * little-endian. Change one and you must change the other; nothing checks.
 *
 * Little-endian throughout, which is free on both ends (Cortex-M33 and every
 * machine a browser runs on), and __packed everywhere so a compiler cannot
 * insert padding that the TypeScript side does not know about.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APP_BLE_H_
#define APP_BLE_H_

#include <zephyr/kernel.h>
#include <vl53l9cx/vl53l9cx.h>

struct bt_conn;

/* Bumped when a struct below changes shape. The web interface refuses to
 * decode a version it does not know, which is better than rendering garbage.
 */
#define APP_PROTOCOL_VERSION 1

/* --- Config characteristic 53f91002, 16 bytes ---------------------------- */

enum app_plane_bits {
	APP_PLANE_DISTANCE  = BIT(0),
	APP_PLANE_AMPLITUDE = BIT(1),
	APP_PLANE_AMBIENT   = BIT(2),
};

enum app_mode {
	APP_MODE_IDLE = 0,
	APP_MODE_STREAMING = 1,
};

struct __packed app_config {
	uint8_t  protocol_version;
	uint8_t  resolution;      /* enum vl53l9cx_res, 0..5 */
	uint8_t  planes;          /* enum app_plane_bits */
	uint8_t  instance_id;
	uint16_t exposure_ms;
	uint16_t frame_period_ms; /* 0 = as fast as the bus allows */
	uint16_t adv_interval_ms;
	uint8_t  mode;            /* enum app_mode */
	uint8_t  flags;
	/*
	 * MEASUREMENT RANGE, carved out of what was a reserved uint32 on
	 * 2026-09-12. The struct is still 16 bytes and the protocol version is
	 * unchanged, deliberately: an older client sends zeros here, and zero
	 * means FAR plus "leave the switchover alone", which is exactly what
	 * every build before this did. Nothing silently changes behaviour.
	 */
	uint8_t  range_mode;      /* enum vl53l9cx_range_mode: 0 far, 1 near */
	uint8_t  reserved0;
	uint16_t switchover_mm;   /* 0 = leave the driver's value alone */
};
BUILD_ASSERT(sizeof(struct app_config) == 16, "config is a 16-byte wire format");

/* --- Command characteristic 53f91003, 4 bytes ---------------------------- */

enum app_opcode {
	APP_CMD_NONE = 0,
	APP_CMD_START = 1,
	APP_CMD_STOP = 2,
	APP_CMD_SINGLE_SHOT = 3,
	APP_CMD_REBOOT_SENSOR = 4,
	APP_CMD_CALIBRATE = 5,          /* phase 6 — returns -ENOTSUP today */
	APP_CMD_CLEAR_CALIBRATION = 6,  /* phase 6 — returns -ENOTSUP today */
};

struct __packed app_command {
	uint8_t opcode;
	uint8_t arg[3];
};

/* --- Config Result characteristic 53f91004, 4 bytes ---------------------- */

/*
 * status is int16_t, not int8_t. -ENOTSUP is 134 on this toolchain, and an
 * int8_t silently turns -134 into +122 - an error that reads as success. The
 * compiler caught it; the wire format is sized for the whole errno range.
 */
struct __packed app_cfg_result {
	uint8_t opcode;   /* which command or write this answers */
	uint8_t detail;   /* which field was rejected, when status says so */
	int16_t status;   /* 0 = applied, otherwise a negative errno */
};
BUILD_ASSERT(sizeof(struct app_cfg_result) == 4, "result is a 4-byte wire format");

/* --- Frame Info characteristic 53f90003, 18 bytes ------------------------ */

struct __packed app_frame_info {
	uint8_t  instance_id;
	uint8_t  protocol_version;
	uint16_t seq;             /* the driver's count — gaps mean WE dropped one */
	uint16_t device_frame;    /* the DEVICE's own counter, from the status line */
	uint8_t  cols;
	uint8_t  rows;
	uint8_t  planes;
	uint8_t  flags;           /* bit0 square format (transmits more rows) */
	uint16_t total_fragments;
	uint16_t payload_bytes;
	uint16_t temperature_raw;
	uint16_t capture_ms;      /* an energy datum in itself, not a debug field */
};
BUILD_ASSERT(sizeof(struct app_frame_info) == 18, "frame info is an 18-byte wire format");

/* --- Frame Data characteristic 53f90002 ----------------------------------
 *
 * Each notification is a 4-byte header then payload. A fragment carrying a seq
 * the host has not seen discards any incomplete frame and counts a drop: there
 * is no retransmission, because at ~2.5 fps a lost frame is cheaper than a
 * stall, and the drop RATE is the honest measure of whether BLE keeps up.
 */
struct __packed app_frag_header {
	uint16_t seq;
	uint16_t frag_index;
};

/* 244 = the ATT payload at MTU 247. Minus our 4-byte header. */
#define APP_FRAG_PAYLOAD_MAX 240

/* --- Telemetry 53f92002 / 53f92003, 16 bytes each ------------------------ */

struct __packed app_health {
	uint8_t  flags;          /* bit0 sensor ready, bit1 streaming */
	uint8_t  error_status;   /* the device's eight health bits, UM3683 Table 16 */
	uint16_t error_code;     /* UM3683 Table 17 */
	int8_t   last_errno;
	uint8_t  reserved[3];
	uint32_t capture_ok;
	uint32_t capture_failed;
};
BUILD_ASSERT(sizeof(struct app_health) == 16, "health is a 16-byte wire format");

struct __packed app_energy {
	uint16_t last_capture_ms;
	uint16_t blob_upload_ms;  /* the cost of a cold start — sets the duty-cycle floor */
	uint16_t exposure_ms;     /* LIVE, not configured: the backoff can lower it */
	uint8_t  resolution;
	uint8_t  reserved;
	uint32_t frames_sent;
	uint32_t uptime_s;
};
BUILD_ASSERT(sizeof(struct app_energy) == 16, "energy is a 16-byte wire format");

/* --- API ----------------------------------------------------------------- */

/** Bring up the controller, register services, start advertising. */
int app_ble_init(void);

/** True while a central is connected. */
bool app_ble_connected(void);

/** True while the node is discoverable. Mutually exclusive with connected. */
bool app_ble_advertising(void);

/** The live connection, or NULL. Needed wherever the pointer itself is. */
struct bt_conn *app_ble_conn(void);

/** True while the streaming thread should be capturing. */
bool app_ble_streaming(void);

/**
 * Start or stop streaming from the application, without a GATT write.
 *
 * Frames still only go out once the client has SUBSCRIBED to the frame data
 * characteristic, so this arms the capture loop rather than forcing traffic at
 * a central that has not asked for it.
 */
void app_ble_set_streaming(bool on);

/** The live configuration. Read-only for callers outside the config service. */
const struct app_config *app_ble_config(void);

/* Implemented by the individual service files. */
int  app_svc_config_init(void);
void app_svc_config_notify_result(uint8_t opcode, int16_t status,
				  uint8_t detail);
void app_svc_telemetry_push(void);
void app_stream_kick(void);
uint32_t app_stream_frames_sent(void);

#endif /* APP_BLE_H_ */
