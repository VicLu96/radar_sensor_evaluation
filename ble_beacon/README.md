# ble_beacon

Zephyr's stock `beacon` sample — `src/main.c` copied verbatim — as a first-class
application in this repo, so it builds and flashes from the **nRF Connect
sidebar** like any other.

## In VS Code

1. Open the nRF Connect side bar. **`ble_beacon`** now appears under
   Applications alongside `firmware_test`.
2. Add a build configuration: board **`water_sense_board/nrf54l15/cpuapp`**,
   everything else default.
3. **Build**, then **Flash**.
4. Scan with nRF Connect on a phone for **"Test beacon"**.
5. When you are done, switch back to `firmware_test` and flash that.

Board discovery works because `CMakeLists.txt` appends `../firmware_test` to
`BOARD_ROOT`, the same mechanism `firmware_test` uses on itself.

## The question it answers

On 2026-09-11 the real firmware reported itself advertising on every heartbeat
— `bt_le_adv_start` returning `-EALREADY`, the controller confirming **+8 dBm**,
every HCI command `status 0x00` — while no scanner could see it. Four
advertising configurations were tried (legacy and extended, connectable and
non-connectable) and none reached the air. The same radio *received* 61–89
advertisements per run at −40 dBm.

This is about forty lines: advertise non-connectably, do nothing else. It shares
**none** of the real firmware's ~3,000 — no GATT services, no streaming thread,
no sensor driver, no staged bring-up.

| Result | Meaning |
|---|---|
| **"Test beacon" visible** | The fault is in **our application**, and that is a tractable search through code we control. |
| **"Test beacon" invisible** | Our code is exonerated. Nothing we write will fix this, and the question is the controller, the board, or the module. |

91,680 B against `firmware_test`'s 190,928.

## What is deliberately NOT changed

Everything except what the board forces. Console and log go over RTT because
this board has no UART pins; memory protection is off to match `firmware_test`
(Victor, 2026-09-04); and transmit power is +8 dBm to match. Nothing else — the
value of this build is in being as close to the stock sample as the hardware
allows.
