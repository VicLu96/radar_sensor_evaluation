/*
 * BLE bring-up: controller, advertising, connection lifecycle.
 *
 * Deliberately boring. Everything interesting is in the service files; this
 * one exists so that when BLE does not work, there is a single place that says
 * how far it got.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_ble.h"
#include "ble_uuid.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_ble, LOG_LEVEL_INF);

static struct bt_conn *current_conn;

bool app_ble_connected(void)
{
	return current_conn != NULL;
}

/*
 * The live connection, for callers that need the connection ITSELF rather than
 * just its existence.
 *
 * bt_gatt_notify() accepts NULL and means "every subscriber", which is why the
 * rest of this firmware passes NULL. bt_gatt_get_mtu() does NOT: it
 * dereferences its argument, so the streaming path needs the real pointer or it
 * faults on the first frame.
 */
struct bt_conn *app_ble_conn(void)
{
	return current_conn;
}

/*
 * Advertising data.
 *
 * The name goes in the advertisement and the 128-bit service UUID in the SCAN
 * RESPONSE, because a 128-bit UUID is an 18-byte AD element and the budget is
 * 31: flags (3) + an 18-byte UUID + a name of any useful length does not fit.
 * This is the single most common reason a device advertises and then cannot be
 * filtered for.
 *
 * The web interface filters on the name prefix and lists every service in
 * optionalServices, so it does not depend on the UUID being advertised at all —
 * but a scanner like nRF Connect shows it, which is worth the scan response.
 */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/*
 * Scan response carries the 128-bit config service UUID, as planned
 * (docs/plan/ble-streaming-and-web-ui.md section 7).
 *
 * It goes here rather than in the advertisement because a 128-bit UUID is an
 * 18-byte AD element and the budget is 31: flags (3) + 18 + a name of any
 * useful length does not fit. That is the single most common reason a device
 * advertises and then cannot be filtered for.
 *
 * It was removed for one build on 2026-09-11 while chasing an invisible
 * advertiser, on the theory that legacy advertising with scan response data
 * leaves the PDU type inferred rather than stated. Removing it changed nothing
 * — the subsequent four-way sweep showed legacy AND extended, connectable AND
 * non-connectable all equally invisible — so the theory was wrong and the
 * planned design is restored.
 */
static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, UUID_SVC_CONFIG_VAL),
};

/*
 * Advertising restart, on a work item rather than inline.
 *
 * THIS IS A REAL BUG THAT WAS HERE. With a single legacy advertising set,
 * Zephyr STOPS advertising the moment a connection is established and does not
 * resume on disconnect. Nothing restarted it, so the node was discoverable
 * exactly once per boot: connect, disconnect, and it vanishes until a reset.
 *
 * On a work item because the disconnected callback runs in the host's own
 * context, and bt_le_adv_start() from there is asking the stack to reconfigure
 * itself from inside its own teardown.
 */
static void adv_start(struct k_work *work);
static K_WORK_DEFINE(adv_work, adv_start);

static int advertising_start(void)
{
	int ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
				  sd, ARRAY_SIZE(sd));

	if (ret == -EALREADY) {
		return 0;
	}
	if (ret) {
		LOG_ERR("advertising failed to start (%d)", ret);
		return ret;
	}

	LOG_INF("advertising as \"%s\" — connectable", CONFIG_BT_DEVICE_NAME);
	return 0;
}

static void adv_start(struct k_work *work)
{
	ARG_UNUSED(work);
	(void)advertising_start();
}

/*
 * Is this node ACTUALLY advertising?
 *
 * The previous version of this function returned !app_ble_connected() and
 * claimed the two were mutually exclusive by construction. That was an
 * assumption dressed as a measurement: it reported "discoverable now" on every
 * heartbeat of a node that no scanner could see, which is worse than silence
 * because it retires the question.
 *
 * This asks the host instead. bt_le_adv_start() returns -EALREADY when an
 * advertising set is already running, so the return code IS the state:
 *
 *   -EALREADY  the host is advertising. Believe it.
 *   0          it had STOPPED and we have just restarted it — a finding in
 *              itself, and worth shouting about.
 *   other      it cannot advertise, and now we have the errno.
 *
 * Self-healing as a side effect, which is a bonus rather than the point.
 */
bool app_ble_advertising(void)
{
	int ret;

	if (app_ble_connected()) {
		return false;
	}

	ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (ret == -EALREADY) {
		return true;
	}
	if (ret == 0) {
		LOG_WRN("advertising had STOPPED and has just been restarted — "
			"something is ending it, which is worth knowing");
		return true;
	}

	LOG_ERR("NOT advertising: bt_le_adv_start returned %d", ret);
	return false;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info;

	if (err) {
		LOG_ERR("connection failed (0x%02x)", err);
		return;
	}

	current_conn = bt_conn_ref(conn);
	LOG_INF("=== BLE CONNECTED ===");

	if (bt_conn_get_info(conn, &info) == 0) {
		LOG_INF("  interval %u units (%u.%02u ms), latency %u, "
			"timeout %u ms",
			info.le.interval,
			(info.le.interval * 125U) / 100U,
			((info.le.interval * 125U) % 100U),
			info.le.latency, info.le.timeout * 10U);
	}

	/*
	 * Ask for 2M PHY. Doubles the on-air rate, and the frame service is the
	 * only thing on this node that can saturate a link. If the central
	 * refuses, the link stays on 1M and everything still works — slower,
	 * which the UI will show as a lower kB/s rather than as a failure.
	 */
	{
		const struct bt_conn_le_phy_param phy = {
			.options = BT_CONN_LE_PHY_OPT_NONE,
			.pref_tx_phy = BT_GAP_LE_PHY_2M,
			.pref_rx_phy = BT_GAP_LE_PHY_2M,
		};
		int ret = bt_conn_le_phy_update(conn, &phy);

		if (ret) {
			LOG_WRN("  2M PHY request failed (%d) — staying on 1M", ret);
		}
	}

	app_stream_kick();
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("=== BLE DISCONNECTED (reason 0x%02x) ===", reason);

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	/*
	 * Streaming stops with the link. Not a safety measure — a stated
	 * policy: the sensor is the expensive component on this board, and
	 * leaving it ranging at 2.5 fps into a link nobody is listening to is
	 * how a bench session quietly burns 450-800 mW.
	 */
	app_stream_kick();

	/* And become findable again. See advertising_start(). */
	k_work_submit(&adv_work);
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			     uint16_t latency, uint16_t timeout)
{
	LOG_INF("BLE params updated: interval %u.%02u ms, latency %u, "
		"timeout %u ms",
		(interval * 125U) / 100U, (interval * 125U) % 100U,
		latency, timeout * 10U);
}

static void le_phy_updated(struct bt_conn *conn,
			   struct bt_conn_le_phy_info *param)
{
	LOG_INF("BLE PHY now tx %s rx %s",
		param->tx_phy == BT_GAP_LE_PHY_2M ? "2M" :
		param->tx_phy == BT_GAP_LE_PHY_CODED ? "coded" : "1M",
		param->rx_phy == BT_GAP_LE_PHY_2M ? "2M" :
		param->rx_phy == BT_GAP_LE_PHY_CODED ? "coded" : "1M");
}

static void le_data_len_updated(struct bt_conn *conn,
				struct bt_conn_le_data_len_info *info)
{
	LOG_INF("BLE data length now tx %u B / %u us, rx %u B",
		info->tx_max_len, info->tx_max_time, info->rx_max_len);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_updated = le_param_updated,
	.le_phy_updated = le_phy_updated,
	.le_data_len_updated = le_data_len_updated,
};

static void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	/*
	 * This number decides the fragment count for every frame, so it is
	 * logged rather than assumed. Chrome on desktop negotiates 247, which
	 * gives a 244-byte ATT payload and 240 bytes of frame data per
	 * notification. A central that stays at the 23-byte default turns a
	 * 4,536-byte frame into 227 notifications instead of 19.
	 */
	LOG_INF("BLE MTU updated: tx %u, rx %u  -> %u bytes of frame data per "
		"notification", tx, rx, MIN(tx, rx) - 3U - 4U);
}

static struct bt_gatt_cb gatt_callbacks = { .att_mtu_updated = mtu_updated };

/*
 * Does the 32 MHz crystal start at all?
 *
 * THE CLASS OF FAULT THAT EXPLAINS "advertising started, nothing on air". BLE
 * needs HFXO; nothing else on this board does, because the CPU runs from
 * internal oscillators. So the radio can be completely silent while the
 * firmware runs perfectly, bt_enable() succeeds and bt_le_adv_start() returns
 * 0 - which is precisely the state observed on 2026-09-11.
 *
 * WHAT THIS CHECK CAN AND CANNOT SAY. It proves the crystal STARTS. It does not
 * prove it is on FREQUENCY, and BLE needs +/-50 ppm. The nRF54L15 has no
 * CLOCKQUALITY indicator (NRF_OSCILLATORS_HAS_CLOCK_QUALITY_IND is 0 on this
 * part - checked, it does not compile), so the frequency half of the question
 * needs a scope or a spectrum analyser and cannot be answered in firmware.
 *
 * Relevant either way: water_sense_board declares no load-capacitors property
 * on &hfxo or &lfxo, while Nordic's own nRF54L15 DK sets
 * load-capacitors = "internal" with 15000 fF (HFXO) and 17000 fF (LFXO) -
 * zephyr/boards/nordic/nrf54l15dk/nrf54l_05_10_15_cpuapp_common.dtsi:34-42.
 * Wrong load capacitance pulls a crystal off frequency; absent capacitance can
 * stop it starting. The correct figure for the ISP2454-LX is Insight SiP's to
 * state - the DK's is NOT it, and must not be copied as if it were.
 */
__maybe_unused static void check_hfxo(void)
{
	const struct device *hf = DEVICE_DT_GET(DT_NODELABEL(clock));
	enum clock_control_status st;
	int ret;

	if (!device_is_ready(hf)) {
		LOG_WRN("clock-control device not ready — cannot check HFXO");
		return;
	}

	ret = clock_control_on(hf, CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (ret != 0 && ret != -EALREADY) {
		LOG_ERR("*** HFXO REQUEST FAILED (%d). The radio cannot "
			"transmit without it.", ret);
		return;
	}

	/* Datasheet startup is ~1.65 ms (hfxo startup-time-us = 1650 in the SoC
	 * devicetree). Allow an order of magnitude before concluding anything.
	 */
	for (int i = 0; i < 20; i++) {
		st = clock_control_get_status(hf, CLOCK_CONTROL_NRF_SUBSYS_HF);
		if (st == CLOCK_CONTROL_STATUS_ON) {
			break;
		}
		k_sleep(K_MSEC(1));
	}

	if (st == CLOCK_CONTROL_STATUS_ON) {
		LOG_INF("HFXO: running. The crystal starts, so the radio has a "
			"clock — note this does NOT prove it is on FREQUENCY, "
			"which is the other half of the question.");
	} else {
		LOG_ERR("*** HFXO DID NOT START within 20 ms (status %d). The "
			"radio cannot transmit, which is exactly the observed "
			"'advertising started, nothing on air'.", (int)st);
		LOG_ERR("    Check that the 32 MHz crystal is fitted, and note "
			"that &hfxo on this board declares no load-capacitors "
			"property while Nordic's own nRF54L15 DK sets "
			"load-capacitors = \"internal\".");
	}

	(void)clock_control_off(hf, CLOCK_CONTROL_NRF_SUBSYS_HF);
}

#if defined(CONFIG_APP_BLE_RX_SELFTEST)
/*
 * Can this board HEAR other BLE devices?
 *
 * THE TEST THAT SPLITS THE REMAINING POSSIBILITIES, and it needs no scope and
 * no second board. On 2026-09-11 the node reported "advertising ...
 * discoverable now" every heartbeat while no scanner could see it, which leaves
 * three candidates: the crystal is wrong, the RF path is broken, or the
 * transmit configuration is.
 *
 * Receiving exercises the SAME crystal and the SAME antenna as transmitting.
 * BLE needs the carrier within about +/-50 ppm, so a receiver simply cannot
 * demodulate anything if HFXO is off frequency, and cannot hear anything if the
 * antenna is not connected. Therefore:
 *
 *   PACKETS HEARD  -> crystal on frequency, RF path intact, radio works. The
 *                     fault is on the transmit side or in the advertising
 *                     configuration, which is a much smaller search.
 *   NOTHING HEARD  -> the crystal or the antenna. Combined with the HFXO check
 *                     above, that separates them: HFXO running plus nothing
 *                     heard points at the antenna and RF matching.
 *
 * An office has BLE traffic in it constantly — phones, laptops, headphones,
 * beacons. Three seconds of passive scanning finding ZERO advertisers is itself
 * a strong result, not an inconclusive one. If in doubt, put a phone beside the
 * board with Bluetooth on.
 */
static uint32_t rx_seen;
static int8_t rx_best_rssi = -128;

static void rx_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
		  struct net_buf_simple *ad)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(type);
	ARG_UNUSED(ad);

	rx_seen++;
	if (rssi > rx_best_rssi) {
		rx_best_rssi = rssi;
	}
}

static void radio_rx_selftest(void)
{
	struct bt_le_scan_param param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};
	int ret;

	LOG_INF("radio RX self-test: listening for %u ms ...",
		CONFIG_APP_BLE_RX_SELFTEST_MS);

	ret = bt_le_scan_start(&param, rx_cb);
	if (ret) {
		LOG_ERR("scan failed to start (%d) — cannot run the RX test", ret);
		return;
	}

	k_sleep(K_MSEC(CONFIG_APP_BLE_RX_SELFTEST_MS));
	(void)bt_le_scan_stop();

	if (rx_seen > 0U) {
		LOG_INF("*** RADIO RX OK: heard %u advertisements, strongest "
			"%d dBm.", rx_seen, rx_best_rssi);
		LOG_INF("    The crystal is on frequency and the antenna works "
			"— you cannot demodulate BLE otherwise. So if nothing "
			"can see this node, the fault is on the TRANSMIT side "
			"or in the advertising configuration, not the RF "
			"front end.");
	} else {
		LOG_ERR("*** RADIO HEARD NOTHING in %u ms. An office has BLE "
			"traffic in it constantly, so zero is a result rather "
			"than a non-result.",
			CONFIG_APP_BLE_RX_SELFTEST_MS);
		LOG_ERR("    Combined with the HFXO line above: crystal running "
			"and nothing heard points at the ANTENNA and RF "
			"matching. Crystal not running points at the crystal "
			"and its load capacitors.");
		LOG_ERR("    If unsure there was anything to hear, put a phone "
			"with Bluetooth on beside the board and reset.");
	}
}
#endif /* CONFIG_APP_BLE_RX_SELFTEST */

#if defined(CONFIG_APP_BLE_ADV_SWEEP)
/*
 * FOUR WAYS TO ADVERTISE, TEN SECONDS EACH.
 *
 * Where this comes from: the host reports the node advertising
 * (bt_le_adv_start -> -EALREADY), the controller accepted every command with
 * status 0x00, the radio receives at -42 dBm, and nothing on air sees it. Three
 * explanations have been offered and all three were wrong, each built on a
 * single observation. This stops explaining and starts enumerating.
 *
 * The four configurations exercise genuinely different paths through the
 * controller:
 *
 *   1 LEGACY CONNECTABLE      ADV_IND on channels 37/38/39. What we use now,
 *                             and what is failing.
 *   2 LEGACY NON-CONNECTABLE  ADV_NONCONN_IND. Same channels, different PDU
 *                             type, and no connectable state machine behind it.
 *   3 EXTENDED CONNECTABLE    ADV_EXT_IND with an AUX pointer to a secondary
 *                             channel. A different advertiser in the
 *                             controller entirely.
 *   4 EXTENDED NON-CONNECTABLE
 *
 * If ANY of them shows up on a scanner, the radio and the antenna are fine and
 * the fault is specific to the path that does not - which is a real, narrow,
 * reportable result. If NONE of them does, the fault is below all four, and
 * that is a Nordic DevZone question with an unusually clean description.
 *
 * Either way it is one flash and forty seconds of watching a phone, instead of
 * another hypothesis.
 */
static void adv_sweep(void)
{
	static const struct bt_le_adv_param ext_conn_param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_CONN,
				     BT_GAP_ADV_FAST_INT_MIN_2,
				     BT_GAP_ADV_FAST_INT_MAX_2, NULL);
	const uint32_t secs = CONFIG_APP_BLE_ADV_SWEEP_SECONDS;
	struct bt_le_ext_adv *ext = NULL;
	int ret;

	LOG_WRN("=================================================");
	LOG_WRN(" ADVERTISING SWEEP: four configurations, %u s each.", secs);
	LOG_WRN(" WATCH A SCANNER AND NOTE WHICH ONES APPEAR.");
	LOG_WRN(" Name \"%s\", address is the identity printed below.",
		CONFIG_BT_DEVICE_NAME);
	LOG_WRN("=================================================");

	/* --- 1: legacy connectable, the failing case --------------------- */
	LOG_WRN(">>> 1/4 LEGACY CONNECTABLE (ADV_IND) — look now");
	ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
			      NULL, 0);
	LOG_WRN("    bt_le_adv_start -> %d", ret);
	k_sleep(K_SECONDS(secs));
	LOG_WRN("    stop -> %d", bt_le_adv_stop());

	/* --- 2: legacy non-connectable ----------------------------------- */
	LOG_WRN(">>> 2/4 LEGACY NON-CONNECTABLE (ADV_NONCONN_IND) — look now");
	ret = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad),
			      NULL, 0);
	LOG_WRN("    bt_le_adv_start -> %d", ret);
	k_sleep(K_SECONDS(secs));
	LOG_WRN("    stop -> %d", bt_le_adv_stop());

	/* --- 3: extended connectable ------------------------------------- */
	LOG_WRN(">>> 3/4 EXTENDED CONNECTABLE (ADV_EXT_IND) — look now");
	ret = bt_le_ext_adv_create(&ext_conn_param, NULL, &ext);
	if (ret) {
		LOG_ERR("    ext_adv_create -> %d (skipping)", ret);
	} else {
		ret = bt_le_ext_adv_set_data(ext, ad, ARRAY_SIZE(ad), NULL, 0);
		LOG_WRN("    ext_adv_set_data -> %d", ret);
		ret = bt_le_ext_adv_start(ext, BT_LE_EXT_ADV_START_DEFAULT);
		LOG_WRN("    ext_adv_start -> %d", ret);
		k_sleep(K_SECONDS(secs));
		LOG_WRN("    ext_adv_stop -> %d", bt_le_ext_adv_stop(ext));
		(void)bt_le_ext_adv_delete(ext);
		ext = NULL;
	}

	/* --- 4: extended non-connectable --------------------------------- */
	LOG_WRN(">>> 4/4 EXTENDED NON-CONNECTABLE — look now");
	ret = bt_le_ext_adv_create(BT_LE_EXT_ADV_NCONN_IDENTITY, NULL, &ext);
	if (ret) {
		LOG_ERR("    ext_adv_create -> %d (skipping)", ret);
	} else {
		ret = bt_le_ext_adv_set_data(ext, ad, ARRAY_SIZE(ad), NULL, 0);
		LOG_WRN("    ext_adv_set_data -> %d", ret);
		ret = bt_le_ext_adv_start(ext, BT_LE_EXT_ADV_START_DEFAULT);
		LOG_WRN("    ext_adv_start -> %d", ret);
		k_sleep(K_SECONDS(secs));
		LOG_WRN("    ext_adv_stop -> %d", bt_le_ext_adv_stop(ext));
		(void)bt_le_ext_adv_delete(ext);
	}

	LOG_WRN("=== SWEEP DONE. Which numbers appeared? ===");
	LOG_WRN("    any of them  -> the radio is fine and the fault is "
		"specific to the paths that did not appear.");
	LOG_WRN("    none of them -> the fault is below all four, and this is "
		"now a very cleanly described Nordic DevZone question.");
}
#endif /* CONFIG_APP_BLE_ADV_SWEEP */

int app_ble_init(void)
{
	int ret;

#if defined(CONFIG_APP_BLE_CHECK_HFXO)
	check_hfxo();
#endif

	ret = bt_enable(NULL);
	if (ret) {
		LOG_ERR("bt_enable failed (%d) — no radio, no web interface", ret);
		return ret;
	}
	LOG_INF("BLE controller up");

	bt_gatt_cb_register(&gatt_callbacks);

#if defined(CONFIG_APP_BLE_RX_SELFTEST)
	radio_rx_selftest();
#endif

#if defined(CONFIG_APP_BLE_ADV_SWEEP)
	adv_sweep();
#endif

	ret = app_svc_config_init();
	if (ret) {
		LOG_ERR("config service init failed (%d)", ret);
		return ret;
	}

	ret = advertising_start();
	if (ret) {
		return ret;
	}

	/*
	 * The identity address, printed because it is what a scanner shows when
	 * the name does not come through. On Windows in particular a cached
	 * pairing can surface a device under an old name, and then the only way
	 * to recognise it is the address.
	 */
	{
		bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
		size_t count = ARRAY_SIZE(addrs);
		char str[BT_ADDR_LE_STR_LEN];

		bt_id_get(addrs, &count);
		if (count > 0) {
			bt_addr_le_to_str(&addrs[0], str, sizeof(str));
			LOG_INF("  identity %s — this is what a scanner shows "
				"if the name does not come through", str);
		}
	}

	return 0;
}
