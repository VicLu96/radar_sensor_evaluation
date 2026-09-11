# ble_beacon

**The simplest BLE advertiser that can exist, restarted in a loop.** A
first-class application in this repo, so it builds and flashes from the **nRF
Connect sidebar** like any other.

Enable the stack. Advertise a flags byte and a name. Tear it down and start it
again every 5 s, forever, printing what the host says each time. That is the
whole program.

It started as Zephyr's stock `beacon` sample, which turned out not to be simple
at all: Eddystone service data, a 16-bit service UUID list, the name pushed into
a scan response, and a non-connectable identity-address advertiser. Every one of
those is a thing that could be wrong, which is the opposite of what this build
is for. What is left is two AD elements, connectable, default parameters, no
scan response, no service data.

## In VS Code

1. Open the nRF Connect side bar. **`ble_beacon`** now appears under
   Applications alongside `firmware_test`.
2. Add a build configuration: board **`water_sense_board/nrf54l15/cpuapp`**,
   everything else default.
3. **Build**, then **Flash**.
4. Scan with nRF Connect on a phone for **"Test beacon"**, or for the address
   the log prints at startup.
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

91,996 B against `firmware_test`'s 190,928.

## Why it loops

The node was seen **once**, on 2026-09-11, and never again — not from the same
commit rebuilt, not from that commit rebuilt a second way, not across several
resets. A single boot-time `bt_le_adv_start()` gives a rare success exactly one
chance to happen.

This tears the advertiser down and rebuilds it every 5 s, so the start path is
exercised repeatedly rather than once. If advertising works one time in fifty,
this finds it, and the log says which cycle:

```
cycle 1 (5 s): already advertising
cycle 2 (10 s): already advertising
...
```

`0`, `-EALREADY` and an errno mean three different things, so all three are
printed rather than collapsed into a status flag — a mistake already made once
in the real firmware, where an "advertising" line turned out to mean only
"nobody is connected".

## What is deliberately NOT changed

Everything except what the board forces. Console and log go over RTT because
this board has no UART pins; memory protection is off to match `firmware_test`
(Victor, 2026-09-04); and transmit power is +8 dBm to match. Nothing else — the
value of this build is in being as close to the stock sample as the hardware
allows.
