# water_sense_board bring-up test

Proves the chain: built for the right SoC, flashed to the right offset, booted,
and talking to a host. It touches no peripheral on purpose — if this does not
print, the fault is the toolchain, the partition offset or the debugger, never
the sensor or the SD card.

## Build and flash

```bash
west build -b water_sense_board/nrf54l15/cpuapp firmware_nrf_board_testing
```

```bash
west flash
```

Output is over **SEGGER RTT**, not a UART — no UART pins are assigned on this
board. Open `JLinkRTTViewer` (or `JLinkRTTLogger`) on channel 0 after flashing.

Expected:

```
[00:00:00.000,000] <inf> board_test: ========================================
[00:00:00.000,000] <inf> board_test:  water_sense_board is alive
[00:00:00.000,000] <inf> board_test:  model  : water_tracker
[00:00:00.000,000] <inf> board_test:  board  : water_sense_board/nrf54l15/cpuapp
...
[00:00:01.000,000] <inf> board_test: heartbeat 0  (uptime 1000 ms)
[00:00:02.000,000] <inf> board_test: heartbeat 1  (uptime 2000 ms)
```

The counter matters more than the text. If it stops, something reset the chip or
wedged the log backend; a count that resumes from zero says "reset" rather than
"hang".

## If it says nothing

In this order, because each one rules out everything below it:

1. **Did it build for the right target?** `water_sense_board/nrf54l15/cpuapp`,
   not `water_sense_board`. The bare name is the old hardware-model-v1 form and
   no longer exists.
2. **Is the image at offset 0?** This board deliberately has no
   `zephyr,code-partition`, so a plain build links at 0 and runs. If MCUboot is
   later added without the matching partition entry, the chip will flash happily
   and never jump to the image — which looks identical to a dead board.
3. **Is RTT actually connected?** RTT needs the J-Link attached and the target
   powered. `JLinkExe` connecting at all proves the debug path independently of
   the firmware.
4. **Only then** suspect the firmware.

## What this does not test

Nothing on the board. No I2C, no SPI, no ADC, no AP_CLK. Those come next, one at
a time, in `firmware/app` — see `firmware/app/README.md` for that staging.
