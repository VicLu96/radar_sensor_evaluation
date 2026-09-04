/*
 * water_sense_board bring-up test.
 *
 * Does nothing but prove the chain works: the image was built for the right
 * SoC, flashed to the right offset, booted, and can talk to a host. Everything
 * else on this board is unproven, so this deliberately touches no peripheral —
 * if this does not print, the problem is the toolchain, the partition offset,
 * or the debugger, and never the sensor or the SD card.
 *
 * Output goes over SEGGER RTT (no UART pins on this board). Open it with
 * JLinkRTTViewer, or `JLinkRTTLogger`, on channel 0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>

LOG_MODULE_REGISTER(board_test, LOG_LEVEL_INF);

int main(void)
{
	uint32_t beat = 0;

	LOG_INF("========================================");
	LOG_INF(" water_sense_board is alive");
	LOG_INF(" board  : %s", CONFIG_BOARD_TARGET);
	LOG_INF(" zephyr : %s", KERNEL_VERSION_STRING);
	LOG_INF(" built  : " __DATE__ " " __TIME__);
	LOG_INF("========================================");

	/* One line a second, forever. The counter matters more than the text:
	 * if it stops, something reset the chip or wedged the log backend, and
	 * a resumed count starting from zero says "reset" rather than "hang".
	 */
	while (true) {
		LOG_INF("heartbeat %u  (uptime %lld ms)", beat++, k_uptime_get());
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
