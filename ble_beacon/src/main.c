/*
 * The simplest BLE advertiser that can exist, restarted in a loop.
 *
 * Enable the stack. Advertise a flags byte and a name. Tear it down and start
 * it again every few seconds, forever, printing what the host says each time.
 * That is the whole program.
 *
 * WHY IT IS THIS BARE. The real firmware reports itself advertising on every
 * heartbeat - bt_le_adv_start returning -EALREADY, the controller confirming
 * +8 dBm, every HCI command status 0x00 - while no scanner can see it. Four
 * advertising configurations were tried (legacy and extended, connectable and
 * non-connectable) and none reached the air, while the same radio RECEIVED
 * 61-89 advertisements per run at -40 dBm.
 *
 * Zephyr's stock beacon sample was the first thing tried here, and it is not
 * actually simple: Eddystone service data, a 16-bit service UUID list, the name
 * pushed into a scan response, and a non-connectable identity-address
 * advertiser. Every one of those is a thing that could be wrong, which is the
 * opposite of what this build is for.
 *
 * So: two AD elements, connectable, default parameters, no scan response, no
 * service data. If THIS is invisible then nothing about the advertising payload
 * or its configuration is to blame, because there is almost nothing here to
 * blame.
 *
 *   VISIBLE   -> the fault is in the real firmware's ~3,000 lines, and that is
 *                a tractable search through code we control.
 *   INVISIBLE -> our code is exonerated. Nothing we write will fix it, and the
 *                question is the controller, the board, or the module.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/printk.h>

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/*
 * Seconds per stop/start cycle. Long enough for a phone scanner to complete a
 * sweep and show the device, short enough that a rare success gets many
 * chances inside a minute.
 */
#define CYCLE_S 5U

/*
 * Two elements: 3 bytes of flags plus 2 + DEVICE_NAME_LEN for the name,
 * against a 31-byte budget. "Test beacon" is 11 characters, so 16 used and 15
 * spare. Nothing else goes in here on purpose.
 */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

int main(void)
{
	char addr_s[BT_ADDR_LE_STR_LEN];
	bt_addr_le_t addr = { 0 };
	size_t count = 1;
	int err;

	printk("\n=== simple advertiser, looping ===\n");

	err = bt_enable(NULL);
	if (err) {
		printk("bt_enable failed (%d) - nothing else can work\n", err);
		return 0;
	}
	printk("bluetooth up\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
			      NULL, 0);
	if (err) {
		printk("bt_le_adv_start FAILED (%d)\n", err);
		return 0;
	}

	bt_id_get(&addr, &count);
	bt_addr_le_to_str(&addr, addr_s, sizeof(addr_s));
	printk("advertising as \"%s\", address %s\n", DEVICE_NAME, addr_s);
	printk("scan for either of those. restarting every %u s.\n", CYCLE_S);

	/*
	 * STOP AND START IT AGAIN, FOREVER.
	 *
	 * Not merely a keep-alive check: the advertiser is genuinely torn down
	 * and rebuilt every cycle, so the start path is exercised over and over
	 * rather than once at boot.
	 *
	 * That matters because of what has already happened here. The node was
	 * seen ONCE, on 2026-09-11, and never again - not from the same commit
	 * rebuilt, not from that commit rebuilt a second way, not across several
	 * resets. A single boot-time start gives a rare success exactly one
	 * chance; this gives it one every few seconds for as long as the board
	 * is powered. If advertising works one time in fifty, this finds it and
	 * the log says which cycle it was.
	 *
	 * Every return code is printed, because 0, -EALREADY and an errno mean
	 * three different things and only the log can tell them apart.
	 */
	for (uint32_t cycle = 1;; cycle++) {
		k_sleep(K_SECONDS(CYCLE_S));

		err = bt_le_adv_stop();
		if (err && err != -EALREADY) {
			printk("cycle %u: bt_le_adv_stop -> %d\n", cycle, err);
		}

		err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad,
				      ARRAY_SIZE(ad), NULL, 0);

		if (err == 0) {
			printk("cycle %u (%u s): restarted OK - scan NOW\n",
			       cycle, cycle * CYCLE_S);
		} else if (err == -EALREADY) {
			printk("cycle %u (%u s): already advertising\n",
			       cycle, cycle * CYCLE_S);
		} else {
			printk("cycle %u (%u s): START FAILED (%d)\n",
			       cycle, cycle * CYCLE_S, err);
		}
	}

	return 0;
}
