/*
 * Frozen GATT identifiers for the water_sense_board node.
 *
 * FROZEN 2026-09-11. The plan (docs/plan/ble-streaming-and-web-ui.md section 7)
 * says to freeze these before both sides are written, because a UUID change
 * costs a firmware flash AND a web deploy and the symptom is a silent
 * "service not found" rather than an error that names itself.
 *
 * Base: 53f9XXXX-1e2d-11ef-9262-0242ac120002
 *
 * The base was 53l9XXXX in the first draft — 'l' is not a hex digit, so nothing
 * would have compiled. Kept as a note because the mistake is invisible when you
 * read it as the part number rather than as hex.
 *
 * The mirror of this table is webinterface/lib/protocol.ts. Change one and you
 * must change the other; there is no build step that checks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APP_BLE_UUID_H_
#define APP_BLE_UUID_H_

#include <zephyr/bluetooth/uuid.h>

/* --- Frame service 53f90001 — dev-stream builds only --------------------- */
#define UUID_SVC_FRAME_VAL \
	BT_UUID_128_ENCODE(0x53f90001, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)
#define UUID_CHR_FRAME_DATA_VAL \
	BT_UUID_128_ENCODE(0x53f90002, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)
#define UUID_CHR_FRAME_INFO_VAL \
	BT_UUID_128_ENCODE(0x53f90003, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)

#define UUID_SVC_FRAME      BT_UUID_DECLARE_128(UUID_SVC_FRAME_VAL)
#define UUID_CHR_FRAME_DATA BT_UUID_DECLARE_128(UUID_CHR_FRAME_DATA_VAL)
#define UUID_CHR_FRAME_INFO BT_UUID_DECLARE_128(UUID_CHR_FRAME_INFO_VAL)

/* --- Config service 53f91001 --------------------------------------------- */
#define UUID_SVC_CONFIG_VAL \
	BT_UUID_128_ENCODE(0x53f91001, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)
#define UUID_CHR_CONFIG_VAL \
	BT_UUID_128_ENCODE(0x53f91002, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)
#define UUID_CHR_COMMAND_VAL \
	BT_UUID_128_ENCODE(0x53f91003, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)
#define UUID_CHR_CFG_RESULT_VAL \
	BT_UUID_128_ENCODE(0x53f91004, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)

#define UUID_SVC_CONFIG     BT_UUID_DECLARE_128(UUID_SVC_CONFIG_VAL)
#define UUID_CHR_CONFIG     BT_UUID_DECLARE_128(UUID_CHR_CONFIG_VAL)
#define UUID_CHR_COMMAND    BT_UUID_DECLARE_128(UUID_CHR_COMMAND_VAL)
#define UUID_CHR_CFG_RESULT BT_UUID_DECLARE_128(UUID_CHR_CFG_RESULT_VAL)

/* --- Telemetry service 53f92001 ------------------------------------------ */
#define UUID_SVC_TELEMETRY_VAL \
	BT_UUID_128_ENCODE(0x53f92001, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)
#define UUID_CHR_HEALTH_VAL \
	BT_UUID_128_ENCODE(0x53f92002, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)
#define UUID_CHR_ENERGY_VAL \
	BT_UUID_128_ENCODE(0x53f92003, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)

#define UUID_SVC_TELEMETRY BT_UUID_DECLARE_128(UUID_SVC_TELEMETRY_VAL)
#define UUID_CHR_HEALTH    BT_UUID_DECLARE_128(UUID_CHR_HEALTH_VAL)
#define UUID_CHR_ENERGY    BT_UUID_DECLARE_128(UUID_CHR_ENERGY_VAL)

/* --- Counting service 53f93001 — phase 7, not implemented yet ------------
 * Reserved here so the numbering cannot drift while the algorithm is written.
 */
#define UUID_SVC_COUNT_VAL \
	BT_UUID_128_ENCODE(0x53f93001, 0x1e2d, 0x11ef, 0x9262, 0x0242ac120002)

#endif /* APP_BLE_UUID_H_ */
