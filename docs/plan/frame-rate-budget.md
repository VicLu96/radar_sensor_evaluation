# Frame-rate budget at 54x42

> **SUPERSEDED IN PART, 2026-08-31.** This page assumes doorway counting of walking
> people, which set the tight frame-rate requirement below. Victor has since scoped the
> goal as **room occupancy where people stay for a while** — see
> [room-occupancy.md](room-occupancy.md). Under that framing the required rate drops to
> 0.05-0.2 Hz, 400 kHz is comfortable, and the 1 MHz question is a tuning detail rather
> than go/no-go. The bandwidth arithmetic below remains correct and is kept because the
> doorway case stays a secondary demo.

Written 2026-08-31, after the decision to run full resolution and trade frame rate for
I²C bandwidth.

**The short version:** the decision is right, but it puts the achievable frame rate and
the frame rate people-tracking *needs* uncomfortably close together. This page works out
where they sit, because discovering it during experiments would be expensive.

---

## What the bus allows

> **Numbers corrected 2026-09-01** against ST's source. A 54×42 frame is **14,842
> bytes**, not 13,608: the estimate below counted the three 16-bit zone planes and
> missed the 1,134-byte DSS array and the fixed 100-byte status line
> (`vl53l9.c:65-84`). Everything in this section is ~9% optimistic; corrected figures
> are in the right-hand columns.

A 54×42 frame is 2268 zones × 6 bytes = 13,608 bytes of zone data, **plus 1,134 bytes
of DSS and a 100-byte status line = 14,842 bytes on the wire**.

| I²C clock | Effective | Transfer (est.) | **Transfer (actual)** | Max fps (est.) | **Max fps (actual)** |
|---|---|---|---|---|---|
| 400 kHz | ~40 KB/s | ~370 ms | **~404 ms** | ~2.7 | **~2.5** |
| **1 MHz** | ~100 KB/s | ~148 ms | **~162 ms** | ~6.7 | **~6.2** |

The full six-mode table, and why the fixed status line flattens the low end of the
energy-accuracy curve, is in [st-package-audit.md](st-package-audit.md) §3.

Integration time adds to this and is **VERIFY** — but the bus is clearly the dominant
term at full resolution.

## What tracking needs

A person walking at ~1.4 m/s. The question is how many frames land on them while they
are in view, and the answer depends on mounting height more than anything else.

The sensor sees 54° along its long axis. What matters is not the footprint on the
**floor** but the footprint at **head height**, because that is what is being tracked:

| Mount height | Sensor→head (1.7 m person) | Head-height footprint | Time in view @1.4 m/s |
|---|---|---|---|
| 2.5 m | 0.8 m | ~0.8 m | **~0.6 s** |
| 2.8 m | 1.1 m | ~1.1 m | **~0.8 s** |
| 3.5 m | 1.8 m | ~1.8 m | ~1.3 s |
| 4.0 m | 2.3 m | ~2.3 m | ~1.7 s |

Frames landing on a walking person:

| | @2.7 fps (400 kHz) | @6.7 fps (1 MHz) |
|---|---|---|
| 2.8 m mount | **~2 frames** | ~5 frames |
| 3.5 m mount | ~3.5 frames | ~9 frames |

**Two frames is not enough to track.** It is barely enough to detect that something
passed, and nowhere near enough to associate identity across frames, establish
direction reliably, or separate two people walking abreast — which is the entire reason
for choosing this sensor.

## So three things follow

### 1. The 1 MHz question is now critical, not nice-to-have

At 400 kHz and a normal ceiling, full resolution does not deliver enough frames per
crossing to track. At 1 MHz it comfortably does. **This has been promoted from a
tuning detail to a go/no-go item** — verify that both the VL53L9CX and the nRF54L15
TWIM sustain 1 MHz, early.

If 1 MHz is unavailable, the options are: mount higher, accept line-crossing detection
without tracking, or use the hybrid below.

### 2. Mount height is a design parameter, not a convenience

Higher mounting buys time-in-view linearly. 3.5 m instead of 2.8 m turns 2 frames into
3.5 at the same bus speed. If the test site allows it, mount high — and record the
height with every dataset, because it changes the achievable accuracy and therefore
every number in the paper.

### 3. The hybrid is worth designing in from the start

The elegant resolution, and a genuine contribution rather than a workaround:

```
idle:       4x4 at low rate      96 B/frame,  ~2.4 ms  - "is anything there?"
triggered:  54x42 burst          13.6 KB,     ~370 ms  - "how many, and which way?"
```

Run a coarse mode continuously at very low energy; when a zone changes, burst into full
resolution for the duration of the crossing. Average energy follows the *occupancy rate*
of the doorway rather than the clock — and a doorway is empty most of the day.

This preserves the resolution claim exactly where it matters (separating people) while
removing the cost where it does not (an empty corridor at 3 a.m.). It also makes the
paper's energy result far more interesting than a fixed-rate sweep: **energy per correct
count as a function of traffic**, not of configuration.

## What this means for the plan

- **Stage 1**: verify the maximum I²C clock. It is now a go/no-go, not a tuning knob.
- **Stage 3**: design the counter for a *low and variable* frame count per crossing.
  A tracker assuming 10 fps will not survive 2.7.
- **Stage 4**: the sweep gains an axis — fixed-rate versus event-triggered hybrid.
- **Paper**: the frame-rate/resolution tension at full resolution over I²C is itself a
  finding. It generalises to any high-zone-count ToF integration on a constrained bus,
  which is most of them.

## Revised power picture at full resolution

The earlier conclusion — "full power-down beats standby" — needs qualifying now that
the frame is 370 ms rather than a notional short read.

Per cycle at 54×42, 400 kHz, with the sensor fully powered down between frames:

```
blob reload   ~250 ms
integration   VERIFY (~10-30 ms?)
transfer      ~370 ms
--------------------------
active        ~650 ms
```

At **1 Hz that is 65% duty** — there is almost no idle left to save, so powering down
buys little and costs a reload. At **0.2 Hz (one frame per 5 s)** active drops to 13%
and power-down clearly wins.

But 0.2 Hz cannot track a walking person at all. **So at full resolution and a fixed
rate, the duty cycle and the application requirement pull in opposite directions** —
which is precisely the argument for the event-triggered hybrid, where the coarse mode
is cheap enough to run continuously and the expensive mode runs only when someone is
actually there.

---

## What actually limits 1 MHz — checked 2026-09-10

The 1 MHz question keeps returning (it decides whether the corner-mount track tier has
headroom), so here is what stands in the way, ranked, from this board rather than in general.

### 1. There are no external pull-ups. This is the blocker.

Extracted from `radar_shield.kicad_pcb`: the only parts touching `/SDA` and `/SCL` are **J6
(the connector) and U1 (the sensor)**. No resistors. So the bus is running on the
**nRF54L15's internal pull-ups**, which is exactly what `bias-pull-up` in the board pinctrl
turned on — and what changed the bus from `-ETIMEDOUT` to `-EIO` on 2026-09-06.

Nordic's internal pull-up is roughly **13 kΩ**. Rise time is `t_r = 0.8473 · R · C`:

| Bus C | Internal 13 kΩ | 4.7 kΩ | 2.2 kΩ | 1.0 kΩ |
|---|---|---|---|---|
| 50 pF | 551 ns | 199 ns | **93 ns** | **42 ns** |
| 100 pF | 1101 ns | 398 ns | 186 ns | **85 ns** |
| 150 pF | 1652 ns | 597 ns | 280 ns | 127 ns |

I²C allows **300 ns** at Fast-mode (400 kHz) and **120 ns** at Fast-mode Plus (1 MHz).

Two things follow:

- **1 MHz is impossible as the board stands.** At 13 kΩ and 100 pF the rise takes 1.1 µs,
  and a 1 MHz bit period is 1 µs — the line would never get high.
- **400 kHz is already out of spec.** 551–1652 ns against a 300 ns limit. It works because
  I²C is static and the controller samples late, not because the timing is legal. Worth
  knowing: some of the marginal behaviour chased during bring-up may have been this.

**Fix: fit ~1 kΩ external pull-ups to +1V8 on SDA and SCL.** Sink current is then 1.8 mA,
and Fm+ devices must sink 20 mA, so that side is trivial. **Board change — Victor's.**

### 2. Bus capacitance, which this build makes worse

The path is MCU → J2/J6 → flying leads → shield → sensor. Connectors and unshielded leads
add capacitance that a monolithic board would not have, which is why the 100–150 pF columns
above are the realistic ones rather than 50 pF. Shorter leads are worth as much as smaller
resistors here.

### 3. The IMU may cap the bus — `VERIFY`

The LSM6DSV16BX shares this bus. **Several ST IMUs in that family are 400 kHz maximum over
I²C**, with the higher rates only on MIPI I3C. If that holds here, the slowest device sets
the bus speed and 1 MHz is off the table while the IMU is fitted. **Check the datasheet
before buying pull-ups** — this could settle the question on its own.

### 4. Pin drive mode

Nordic recommend high drive on the low side for Fm+. The I²C pins currently take pinctrl's
default `S0D1` (standard low, disconnect high). `H0D1` would be the Fm+ setting — a board
pinctrl change, and only relevant once 1 and 3 are resolved.

### 5. The SoC and the sensor both support it

Cleared, so they are not the obstacle. `TWIM_FREQUENCY_FREQUENCY_K1000` is defined for the
nRF54L15, so `NRF_TWIM_HAS_1000_KHZ_FREQ` is 1 and Zephyr's `I2C_SPEED_FAST_PLUS` maps
straight to it. On the sensor side ST's own reference sets 1 MHz explicitly, and UM3683
Table 1's read times imply it.

### What it buys

A 54×42 frame goes from **~404 ms to ~162 ms**, and the ceiling from ~2.5 fps to ~6.2 fps.
For the corner-mount design that is the difference between a track tier with no margin and
one with room to spare — see the TODO in
[ble-streaming-and-web-ui.md](ble-streaming-and-web-ui.md).
