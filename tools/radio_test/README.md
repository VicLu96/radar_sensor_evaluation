# radio_test on water_sense_board

Nordic's own radio test, built for this board with the shell on RTT (no UART pins).
**It uses none of our firmware** — not the Bluetooth host, not the SoftDevice
Controller's advertising path, not the VL53L9CX driver. It drives the RADIO
peripheral directly.

## Why it exists

On 2026-09-11, four advertising configurations — legacy connectable, legacy
non-connectable, extended connectable, extended non-connectable — all reported
success and **none reached the air**, while the same radio *received* 61–89
advertisements per run at −40 to −42 dBm. `bt_le_adv_start()` returned 0 and then
`-EALREADY`; every HCI init command returned `status 0x00`.

At that point no amount of reading our code can answer the question, because the
gap is between *"the controller says it is advertising"* and *"packets exist"*,
and nothing in Zephyr can see into it.

## Build and flash

```bash
west build --build-dir tools/radio_test/build \
  $NCS/nrf/samples/peripheral/radio_test \
  --board water_sense_board/nrf54l15/cpuapp --pristine -- \
  -DBOARD_ROOT=$PWD/firmware_test \
  -DEXTRA_CONF_FILE=$PWD/tools/radio_test/water_sense_board.conf \
  -DEXTRA_DTC_OVERLAY_FILE=$PWD/tools/radio_test/water_sense_board.overlay
west flash --build-dir tools/radio_test/build
```

Then open RTT. You get a shell prompt.

## The measurement

```
start_channel 40        # 2440 MHz — mid-band, clear of Wi-Fi 1/6/11
parameters_print
start_tx_carrier        # continuous unmodulated carrier
```

Put a spectrum analyser near the board and look for a peak at **2440 MHz**.
`cancel` stops it.

| Result | Meaning |
|---|---|
| **Carrier present** | The SoC and the antenna transmit. The fault is in the BLE stack or its configuration, and it is a software problem after all — a narrow one, because four advertising paths have already been eliminated. |
| **No carrier** | The transmit path is broken below all software. Hardware: the RF strap (pin 20 to 22), the matching network, or the module. Receiving would still work, because reception needs far less of the front end than transmission. |

A spectrum analyser is the right instrument and ETH will have one. Failing that,
an SDR (RTL-SDR reaches 2.4 GHz with an upconverter; a HackRF or similar does it
directly) shows the same peak.

## What this does NOT do

It does not transmit anything a phone or a browser can decode — a continuous
carrier is not a BLE packet. Do not expect it to show up in nRF Connect. The
whole point is to ask whether RF energy leaves the board at all, independently
of whether anything can parse it.
