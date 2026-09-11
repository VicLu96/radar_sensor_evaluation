# Plan: raising the I²C bus to 1 MHz

**The bus is the frame-rate limit, and nothing else is close.** At 400 kHz a full
54×42 frame takes ~334 ms to read, which caps the node at ~2.5 fps. BLE has
roughly three times that headroom; the sensor's own ranging is a few
milliseconds. Everything downstream of "how fast can we range" is decided here.

Written 2026-09-11.

## What it buys

| | 400 kHz | 1 MHz | |
|---|---|---|---|
| Full frame, 14,842 B | ~334 ms | **~134 ms** | 2.5× |
| Frame-rate ceiling | ~2.5 fps | **~6.2 fps** | |
| Firmware blob on cold start, 9,865 B | **313 ms measured** | ~125 ms | sets the duty-cycle floor |
| Distance-only 54×42 on the wire | unchanged | unchanged | BLE is not the limit |

The frame-rate number is the one that matters for the product. The corner-mount
design needs a **track tier at 1.5–2.5 fps** to associate a walking person
between frames (`ble-streaming-and-web-ui.md` §6.3). At 400 kHz that tier has
**no margin at all** — it sits exactly at the ceiling. At 1 MHz it has 2.5×.

The blob figure matters separately: it is paid on every cold start, so it sets
the idle period beyond which powering the sensor down beats keeping it in
standby. Cutting it from 313 ms to ~125 ms moves that crossover.

## The three parts, and what each needs

### 1. The IMU — **CLOSED 2026-09-11. It is not the obstacle.**

**The fitted part is the LSM6DSV16BX — confirmed by Victor, 2026-09-11.** Its
I²C interface supports fast mode *and* **fast mode plus at 1 MHz**, so the
slowest-device-wins problem does not arise.

This was worth asking because the answer flips on the part number: an
**LSM6DSO** is **400 kHz only**, with higher rates available solely over I3C,
and it would have closed the 1 MHz question on its own.

The part has been **silent at 0x6B since 2026-09-10**, so it cannot currently be
identified by WHO_AM_I — this rests on Victor and the schematic, which is
sufficient. Whether it answers again is a separate problem and does not gate
the bus speed.

### 2. Pull-ups — **VICTOR: FIT 1 kΩ. This is now the only blocker.**

`t_r = 0.8473 · R · C`, and the rise-time limit is **300 ns at 400 kHz** and
**120 ns at Fast-mode Plus**.

| bus C | max R @ 400 kHz | max R @ 1 MHz | what 4.7 kΩ gives |
|---|---|---|---|
| 80 pF | 4.4 kΩ | 1.8 kΩ | 319 ns |
| 100 pF | 3.5 kΩ | **1.4 kΩ** | 398 ns |
| 150 pF | 2.4 kΩ | 0.94 kΩ | 597 ns |

**The current 4.7 kΩ is already out of spec at 400 kHz.** It works because I²C is
static and the controller samples late — not because the timing is legal. That
is worth knowing on its own: the bus is being run outside spec today.

**1 kΩ** meets Fm+ to ~140 pF and fixes 400 kHz as a side effect.

| R | t_r @ 100 pF | Fm+ (120 ns)? | sink @ 1.8 V |
|---|---|---|---|
| 4.7 kΩ *(fitted today)* | 398 ns | **no — 3.3× over** | 0.4 mA |
| 1.5 kΩ | 127 ns | marginal; only below ~90 pF | 1.2 mA |
| **1 kΩ — recommended** | **85 ns** | **yes, to ~140 pF** | 1.8 mA |
| 820 Ω | 69 ns | yes, to ~170 pF | 2.2 mA |

Take 820 Ω if the traces are long or the bus carries more than two devices.
Sink current is comfortably inside what both parts handle either way.

**This is the last thing standing between the node and 6.2 fps**, and it fixes
an out-of-spec 400 kHz bus whether or not Fast-mode Plus is adopted — so it is
worth doing on its own merits.

*Cost:* ~1.8 mA per line while low. Negligible at 0.1 Hz; ~0.6 mA average at
2 fps. One more reason the track tier should run only during activity.

### 3. Controller and sensor — **both already cleared**

- **nRF54L15**: `TWIM_FREQUENCY_FREQUENCY_K1000` is defined, and Zephyr's
  `I2C_SPEED_FAST_PLUS` maps through to it.
- **VL53L9CX**: ST's own reference port sets 1 MHz explicitly.

Neither is the obstacle. Only the pull-ups and the IMU identity are.

## Status: being tested at 4.7 kΩ, 2026-09-11

Victor has 4.7 kΩ fitted and asked to try 1 MHz now rather than wait for new
resistors. `clock-frequency = <I2C_BITRATE_FAST_PLUS>` is in the application
overlay and built.

**The rise time is ~398 ns against a 120 ns limit — more than three times over.**
It is worth trying anyway because the failure modes are cheap and one of them is
loud: the firmware blob is verified against `DEVICE_ID 0x53334C39` and the FSM
after upload, so a corrupted transfer fails at boot rather than silently.

The case to actually watch for is **it appears to work**. A marginal bus corrupts
data rather than failing cleanly, and a 14,842-byte read has a lot of surface.
Halved timings prove the clock changed; only unchanged zone-validity and
amplitude prove the data survived.

If it works at 4.7 kΩ that is a useful data point and not a licence to stop —
the bus would still be outside spec, and out-of-spec buses fail with temperature,
with a different board, and on the day of the demo.

## How to do it

One line, in the **application overlay** rather than the board file:

```dts
&i2c21 {
	clock-frequency = <I2C_BITRATE_FAST_PLUS>;   /* 1000000 */
};
```

Then **measure, do not assume**:

1. **Logic analyser on SCL and SDA.** Check the actual clock is 1 MHz and the
   rise time is under 120 ns. This is the measurement the pull-up arithmetic is
   predicting, and `CLAUDE.md` already names the logic analyser as the reference
   instrument for this bus.
2. **Watch the blob upload time** in the boot log — it should fall from ~313 ms
   toward ~125 ms. That is a free end-to-end check that the bus really changed
   speed, with no extra instrumentation.
3. **Watch `capture_ms`** in the web interface: ~334 ms should become ~134 ms.
4. **Then check the frame is still correct**, not merely faster. A marginal bus
   corrupts data rather than failing cleanly, and a 14,842-byte read has a lot of
   surface. The zone-validity percentage and the amplitude split are the fastest
   way to see it; a garbled frame shows up as noise in the heatmap immediately.

## Order of work

1. ~~Confirm the fitted IMU part number.~~ **DONE 2026-09-11: LSM6DSV16BX,
   1 MHz capable. Not the obstacle.**
2. **VICTOR: fit 1 kΩ pull-ups.** The only remaining blocker, and it fixes an
   out-of-spec 400 kHz bus whether or not Fast-mode Plus is adopted.
3. Change `clock-frequency`, measure the four things above.
4. Re-measure the energy per frame. Halving the bus time halves the CPU-awake,
   sensor-active window, which lands directly in the number the paper reports.

## Why this is worth doing before the detection work

The two-tier duty cycle in `ble-streaming-and-web-ui.md` §6.3 is not an
optimisation — it is structural, because association needs frames fast enough to
follow a walking person. **At 400 kHz the track tier runs at the ceiling with no
headroom, so any added per-frame cost breaks it.** Doing this first means the
detection algorithm is developed against a bus that is not the binding
constraint, rather than being tuned around one and re-tuned later.
