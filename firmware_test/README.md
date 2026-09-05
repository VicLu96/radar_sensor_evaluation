# water_sense_board bring-up test

Walks the board up one layer at a time, so that a failure names its own cause. The first
stage touches no peripheral at all: if the banner does not print, the fault is the
toolchain, the partition offset or the debugger, and never a sensor.

## What it does

Three stages, in order, because each only means something if the previous one passed:

1. **The MCU is alive and can talk to a host** — banner plus a one-per-second heartbeat.
2. **The I²C bus works and the IMU is real** — WHO_AM_I at 0x6B, then accelerometer XYZ.
3. **The VL53L9CX ranges** — a 12×10 frame every five seconds, printed as a grid.

The order matters: the IMU is the simpler device on the same bus, so if it answers and
the ToF sensor does not, **the bus itself is proven** and the fault is on the ToF side.

IMU failures are reported and then ignored: the heartbeat carries on regardless, because
"the MCU runs but the IMU does not" is a state worth being able to observe rather than a
reason to stop.

Nothing here touches the VL53L9CX. If the IMU answers and the ToF sensor does not, the
bus is proven and the fault is on the ToF side — worth a great deal on a board where
nothing else is known good.

### Reading the IMU output

```
<inf> board_test: WHO_AM_I = 0x71
<inf> board_test:   -> LSM6DSV16BX. Accel output block at 0x2c.
<inf> board_test: configured: CTRL1=0x05 (wrote 0x05), CTRL8=0x00 (wrote 0x00)
<inf> board_test: accel  X    -12 mg   Y      6 mg   Z    998 mg   |a|   998 mg   (raw ...)
```

**`|a|` is the number to look at.** Whatever the orientation, a board at rest measures one
gravity. ~1000 mg means the full scale, the register base and the byte order are all
right. A wrong magnitude means the scaling or the output register base is wrong — not the
sensor. Odd-looking axes with a correct magnitude are just orientation, and can wait.

`WHO_AM_I` also settles which part is fitted, and the log says what each answer implies:
`0x71` is the LSM6DSV16BX (no in-tree Zephyr driver), `0x70` is the LSM6DSV16X (Zephyr
ships one, so a real integration becomes a devicetree node rather than a new driver).

### Reading the ToF output

```
<inf> board_test: ToF 12x10 in 41 ms - 118/120 zones valid
<inf> board_test:   distance  min 412 mm   mean 1832 mm   max 3210 mm
<inf> board_test:   device frame 37 (seq 7), temperature raw 8412
<inf> board_test:   distances in cm ('   .' = no target):
<inf> board_test:      183 181 180 179 178 178 179 180 181 183 185 188
...
```

**The grid is the point, not the statistics.** A mean and a min/max can look perfectly
healthy over a frame of nonsense. A grid of distances either has the shape of the scene
in it — a wall at constant distance, a hand closer in the middle, a doorway as a column
of larger numbers — or it does not, and that is obvious at a glance.

12×10 is used rather than full 54×42 for two reasons: it is 880 bytes on the wire
against 14,842, and it is in the **wide** family, so it shares the full field of view
rather than a cropped one. A like-for-like preview at a twentieth of the bus time.

`device frame` versus `seq` is the drop detector: if the device's own counter stops
advancing while ours climbs, the driver is re-reading a stale buffer.

## Status: builds clean

Verified 2026-09-04 against NCS v3.3.0 at `C:/ncs/v3.3.0`:

```
FLASH:  56876 B    1428 KB   3.89%
RAM:    41008 B     188 KB  21.30%
```

RAM is mostly two buffers: the driver's 14,842-byte worst-case raw frame and the
application's ~18 KB unpacked `struct vl53l9cx_frame`. Both static.

`zephyr.elf` links and `merged.hex` is generated. What that proves is the toolchain, the
board definition, the devicetree and the memory map. What it does not prove is anything
about the hardware — the pins and rails are still only as right as the schematic.

## Layout

Self-contained: one directory to build, flash, and hand to someone else.

```
CMakeLists.txt              adds the driver below as an out-of-tree module
prj.conf
src/main.c                  the three staged tests
boards/ethzurich/...        water_sense_board definition (Victor's)
boards/water_sense_board_nrf54l15_cpuapp.overlay
                            VL53L9CX node — application hardware, not board hardware
drivers/vl53l9cx/           the ToF driver, ST's sources under st/
```

## Build and flash

```bash
west build -b water_sense_board/nrf54l15/cpuapp firmware_test -p always
```

**`-p always` is not optional here.** Zephyr discovers `boards/*.overlay` once, at
configure time, and caches the result in `DTC_OVERLAY_FILE`. A build directory created
before the overlay existed will never pick it up, no matter how many times it is rebuilt
— and the symptom is not "missing overlay", it is
`vl53l9cx/vl53l9cx.h: No such file or directory`, because without the sensor node
`CONFIG_VL53L9CX` is unset and the driver module skips its include directory.

`src/main.c` carries a `#error` that catches exactly this and says so, so the misleading
message now arrives second, behind an explanation.

### The other Windows path trap, now fixed at source

A second 260-character failure used to hit the driver module itself. `zephyr_library()`
derives its library name from the module's path relative to the build, and this module
sits outside the application tree — so the name came out as a 113-character
`..__..__..__Users__luder__...__vl53l9cx`, used verbatim as an object directory. That put
the dependency-file path at 262 characters: over by two.

The driver's `CMakeLists.txt` now calls `zephyr_library_named(vl53l9cx)`, which takes the
same path to 161. Nothing to do on your side, but worth knowing the shape of it: the
error said `opening dependency file ...: No such file or directory` and mentioned neither
paths nor lengths.

**Windows path length matters here.** The nRF Connect SDK crypto sources sit very deep,
and the compiler fails with `opening dependency file ... No such file or directory` —
which names a missing file rather than the real cause — once a path exceeds 260
characters. The folder was renamed from `firmware_nrf_board_testing` to `firmware_test`
for exactly this reason: it removes 13 characters from the build path and another 13
from the sysbuild subdirectory, which is enough on this machine. If it still bites,
build somewhere short:

```bash
west build -b water_sense_board/nrf54l15/cpuapp -d c:/b firmware_test -p always
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
