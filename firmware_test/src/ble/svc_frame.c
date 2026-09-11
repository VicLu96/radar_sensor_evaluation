/*
 * Frame service 53f90001, and the thread that feeds it.
 *
 * This is the instrument, not the product. CLAUDE.md says "counts leave the
 * device, frames never do" - this whole file is what gets compiled OUT of the
 * deployed build, and CONFIG_APP_BLE_FRAME_SERVICE is the switch. The
 * architectural claim is then checkable rather than merely stated: the
 * generated .config has the symbol unset, and `nm zephyr.elf` finds no
 * frame_svc symbol.
 *
 * (NOT checkable with `strings | grep <uuid>`, which an earlier draft of the
 * plan proposed. BT_UUID_128_ENCODE emits a 16-byte binary initialiser, not
 * ASCII, so that grep can never match and would have "passed" in a paper.)
 *
 * FRAGMENTATION. A frame is up to 14,842 bytes and an ATT notification carries
 * at most MTU-3. So each frame is one Frame Info notification followed by N
 * fragments, each a 4-byte {seq, index} header plus payload. There is no
 * retransmission and no acknowledgement: at ~2.5 fps a lost frame is cheaper
 * than a stall, and the DROP RATE is the honest measure of whether the link
 * keeps up. The host counts drops by watching for gaps in frag_index and for a
 * seq that changes before the previous frame completed.
 *
 * FLOW CONTROL. bt_gatt_notify() returns -ENOMEM when the stack's buffers are
 * full, and the wrong answer is to spin on it: that burns the CPU the radio
 * needs to drain them. Instead a semaphore counts in-flight notifications and
 * the completion callback returns each slot. The thread blocks when it runs
 * out, which is exactly the backpressure wanted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_ble.h"
#include "ble_uuid.h"
#include "../app_capture.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(app_ble, LOG_LEVEL_INF);

static K_SEM_DEFINE(kick, 0, 1);
static uint32_t frames_sent;

void app_stream_kick(void)
{
	k_sem_give(&kick);
}

uint32_t app_stream_frames_sent(void)
{
	return frames_sent;
}

#if defined(CONFIG_APP_BLE_FRAME_SERVICE)

/* --- the service ---------------------------------------------------------- */

static bool data_subscribed;
static bool info_subscribed;
static struct app_frame_info last_info;

static void data_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	data_subscribed = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("frame data notifications %s",
		data_subscribed ? "ENABLED" : "disabled");
	app_stream_kick();
}

static void info_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	info_subscribed = (value == BT_GATT_CCC_NOTIFY);
}

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &last_info,
				 sizeof(last_info));
}

BT_GATT_SERVICE_DEFINE(frame_svc,
	BT_GATT_PRIMARY_SERVICE(UUID_SVC_FRAME),

	BT_GATT_CHARACTERISTIC(UUID_CHR_FRAME_DATA,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(data_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	BT_GATT_CHARACTERISTIC(UUID_CHR_FRAME_INFO,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_info, NULL, NULL),
	BT_GATT_CCC(info_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/*
 * attrs[]: 0 service, 1 data decl, 2 data value, 3 data CCC,
 *          4 info decl, 5 info value, 6 info CCC.
 */
#define ATTR_FRAME_DATA_VALUE 2
#define ATTR_FRAME_INFO_VALUE 5

/* --- notification slots --------------------------------------------------- */

#define NOTIFY_SLOTS CONFIG_APP_BLE_NOTIFY_SLOTS

struct frag_slot {
	struct bt_gatt_notify_params params;
	uint8_t buf[sizeof(struct app_frag_header) + APP_FRAG_PAYLOAD_MAX];
};

static struct frag_slot slots[NOTIFY_SLOTS];
static K_SEM_DEFINE(slot_sem, NOTIFY_SLOTS, NOTIFY_SLOTS);
static uint8_t slot_next;

static void frag_sent(struct bt_conn *conn, void *user_data)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(user_data);
	k_sem_give(&slot_sem);
}

/* --- payload serialisation ------------------------------------------------ *
 *
 * The wire layout is PLANE-major, matching what the sensor itself sends and
 * what the web interface expects:
 *
 *   [ distance u16 x zones ][ amplitude u16 x zones ][ ambient u16 x zones ]
 *
 * with only the enabled planes present, in ascending bit order. Distance keeps
 * the device's own encoding - millimetres in bits 14:0, validity in bit 15 -
 * because throwing the flag away would force the host to guess, and 32-metre
 * readings are what guessing looks like.
 *
 * Bytes are produced on demand rather than into a staging buffer: a full frame
 * is 14,842 bytes and there is no reason to hold a second copy of it.
 */
static uint16_t plane_word(const struct vl53l9cx_frame *f, uint8_t plane,
			   uint16_t idx)
{
	const struct vl53l9cx_zone *z = &f->zone[idx];

	switch (plane) {
	case 0: return (uint16_t)(z->distance_mm & 0x7FFFU) |
		       (z->valid ? 0x8000U : 0U);
	case 1: return z->amplitude;
	default: return z->ambient;
	}
}

static void fill_payload(const struct vl53l9cx_frame *f, uint8_t planes,
			 uint32_t offset, uint16_t len, uint8_t *dst)
{
	const uint32_t zones = (uint32_t)f->cols * f->rows;
	const uint32_t plane_bytes = zones * 2U;

	for (uint16_t i = 0; i < len; i++) {
		uint32_t o = offset + i;
		uint32_t nth = o / plane_bytes;   /* which ENABLED plane */
		uint32_t rem = o % plane_bytes;
		uint8_t plane = 0;
		uint32_t seen = 0;

		/* Map the nth enabled plane to its actual index. */
		for (plane = 0; plane < 3U; plane++) {
			if (planes & BIT(plane)) {
				if (seen == nth) {
					break;
				}
				seen++;
			}
		}

		uint16_t w = plane_word(f, plane, (uint16_t)(rem >> 1));

		dst[i] = (rem & 1U) ? (uint8_t)(w >> 8) : (uint8_t)(w & 0xFFU);
	}
}

/* --- one frame on the wire ------------------------------------------------ */

static int send_frame(struct bt_conn *conn, const struct vl53l9cx_frame *f,
		      uint8_t planes, uint16_t capture_ms)
{
	const uint32_t zones = (uint32_t)f->cols * f->rows;
	const uint8_t nplanes = POPCOUNT(planes & 0x07U);
	const uint32_t payload_bytes = zones * 2U * nplanes;
	uint16_t chunk;
	uint16_t total;
	uint32_t off = 0;
	uint16_t index = 0;
	int ret;

	/*
	 * The fragment size follows the NEGOTIATED MTU, not a constant. Chrome
	 * on desktop gives 247, so 240 bytes of payload; a central that never
	 * exchanges MTU leaves 23, so 16 bytes - a 15x difference in fragment
	 * count that would otherwise be invisible until the frame rate looked
	 * wrong.
	 */
	chunk = bt_gatt_get_mtu(conn);
	chunk = (chunk > (3U + sizeof(struct app_frag_header)))
			? chunk - 3U - sizeof(struct app_frag_header)
			: 16U;
	chunk = MIN(chunk, APP_FRAG_PAYLOAD_MAX);

	total = (uint16_t)DIV_ROUND_UP(payload_bytes, chunk);

	last_info = (struct app_frame_info){
		.instance_id = app_ble_config()->instance_id,
		.protocol_version = APP_PROTOCOL_VERSION,
		.seq = (uint16_t)f->seq,
		.device_frame = (uint16_t)f->frame_counter,
		.cols = f->cols,
		.rows = f->rows,
		.planes = planes,
		.flags = 0,
		.total_fragments = total,
		.payload_bytes = (uint16_t)payload_bytes,
		.temperature_raw = f->temperature,
		.capture_ms = capture_ms,
	};

	/*
	 * Info BEFORE the fragments it describes. The host allocates its
	 * reassembly buffer from this, so a fragment arriving first has nowhere
	 * to go and is counted as a drop.
	 */
	if (info_subscribed) {
		ret = bt_gatt_notify(conn, &frame_svc.attrs[ATTR_FRAME_INFO_VALUE],
				     &last_info, sizeof(last_info));
		if (ret) {
			return ret;
		}
	}

	while (off < payload_bytes) {
		uint16_t n = (uint16_t)MIN((uint32_t)chunk, payload_bytes - off);
		struct frag_slot *s;
		struct app_frag_header hdr = {
			.seq = (uint16_t)f->seq,
			.frag_index = index,
		};

		/* Backpressure. Blocks rather than spinning; see the header. */
		if (k_sem_take(&slot_sem, K_MSEC(2000)) != 0) {
			LOG_WRN("notification slots stalled for 2 s - "
				"abandoning frame %u at fragment %u/%u",
				(unsigned int)f->seq, index, total);
			return -ETIMEDOUT;
		}

		if (!data_subscribed || !app_ble_streaming()) {
			k_sem_give(&slot_sem);
			return -ECANCELED;
		}

		s = &slots[slot_next];
		slot_next = (slot_next + 1U) % NOTIFY_SLOTS;

		memcpy(s->buf, &hdr, sizeof(hdr));
		fill_payload(f, planes, off, n, s->buf + sizeof(hdr));

		s->params = (struct bt_gatt_notify_params){
			.attr = &frame_svc.attrs[ATTR_FRAME_DATA_VALUE],
			.data = s->buf,
			.len = (uint16_t)(sizeof(hdr) + n),
			.func = frag_sent,
		};

		ret = bt_gatt_notify_cb(conn, &s->params);
		if (ret) {
			/* The callback will not run, so return the slot here. */
			k_sem_give(&slot_sem);
			if (ret == -ENOMEM) {
				/* Let the radio drain and retry this fragment. */
				k_sleep(K_MSEC(2));
				continue;
			}
			LOG_WRN("notify failed (%d) at fragment %u/%u", ret,
				index, total);
			return ret;
		}

		off += n;
		index++;
	}

	frames_sent++;
	return 0;
}

/* --- the streaming thread -------------------------------------------------- */

static void stream_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		const struct app_config *cfg = app_ble_config();
		struct bt_conn *conn;
		int64_t t0;
		int ret;
		uint16_t capture_ms;

		if (!app_ble_streaming() || !data_subscribed) {
			/* Idle: the sensor is off and nothing is being paid
			 * for. Wake on a kick from a command, a config write,
			 * a subscription, or a connection change.
			 */
			k_sem_take(&kick, K_FOREVER);
			continue;
		}

		conn = app_ble_conn();
		if (conn == NULL) {
			/* app_ble_streaming() was true a moment ago; the link
			 * went away in between. Nothing to send it to.
			 */
			continue;
		}

		t0 = k_uptime_get();
		ret = app_capture_once((enum vl53l9cx_res)cfg->resolution,
				       K_SECONDS(5));
		capture_ms = (uint16_t)MIN(k_uptime_get() - t0, UINT16_MAX);

		if (ret < 0) {
			/*
			 * The driver has already diagnosed this against the
			 * device's own registers. Do not guess underneath it -
			 * on 2026-09-10 the guess was wrong twice.
			 */
			app_svc_telemetry_push();
			k_sleep(K_MSEC(200));
			continue;
		}

		app_capture_lock();
		ret = send_frame(conn, app_capture_frame(), cfg->planes,
				 capture_ms);
		app_capture_unlock();

		if (ret == -ECANCELED) {
			continue;
		}

		/* Telemetry on every tenth frame, and on every failure. */
		if (ret != 0 || (frames_sent % 10U) == 0U) {
			app_svc_telemetry_push();
		}

		if (cfg->frame_period_ms > 0U) {
			int64_t elapsed = k_uptime_get() - t0;
			int64_t left = (int64_t)cfg->frame_period_ms - elapsed;

			if (left > 0) {
				k_sleep(K_MSEC(left));
			}
		} else {
			/* Free-running. Yield so the heartbeat and the BT RX
			 * thread are not starved by a tight capture loop.
			 */
			k_sleep(K_MSEC(1));
		}
	}
}

K_THREAD_DEFINE(stream_tid, CONFIG_APP_BLE_STREAM_STACK_SIZE, stream_thread,
		NULL, NULL, NULL, K_PRIO_PREEMPT(7), 0, 0);

#else /* !CONFIG_APP_BLE_FRAME_SERVICE - the deployed profile */

/*
 * Deliberately empty. No frame service, no streaming thread, no serialisation
 * code: a frame physically cannot leave this build. That is the privacy claim,
 * and it is now a property of the binary rather than of a promise.
 */

#endif /* CONFIG_APP_BLE_FRAME_SERVICE */
