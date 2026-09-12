# Plan: measurement range, and showing distance on a demo

Written 2026-09-12, after Victor asked why nothing measures close to the sensor
when the part is specified from 5 cm.

## Why close range did not work

**The driver used the LONG ranging context unconditionally.** Five hardcoded
`VL53L9_CONTEXT_LONG` in `apply_resolution()`, since the port was written.

The context is not a filter on the output. `vl53l9_set_context()`
(`st/vl53l9.c:420-447`) changes the analogue front end:

| | `CAL_PROG_OFFSET_1TO5` | `_6` | `CAB` short scale |
|---|---|---|---|
| **SHORT** (near) | 1 | 5 | 256 |
| **LONG** (far) | −8 | −1 | 683 |

ST's own four profiles split on exactly this
(`st-reference/vl53l9/vl53l9_utils.c:29-64`):

| use case | context | binning | exposure |
|---|---|---|---|
| `AR_RANGE` | LONG | 2 | 8 ms |
| `AR_PRECISION` | **SHORT** | 2 | 10 ms |
| `AF_RANGE` | LONG | 4 | 4 ms |
| `AF` | **SHORT** | 4 | 5 ms |

So the part is capable of close range; this firmware was never asking for it.

## Range is not one setting. It is three.

This is the part worth internalising before the demo, because getting it wrong
looks identical to a broken sensor.

**1. Context — SHORT or LONG.** Decides whether close targets resolve at all.

**2. Exposure — and it works the OPPOSITE way from intuition at close range.** A
near target returns a great deal of light. Too much exposure saturates the
histogram, the peak cannot be located, and the zone is **rejected** — reported
as *no target*, not as *near*. This was already observed at ~1 m on 2026-09-11:
the invalid zones came back **brighter** than the valid ones, 259 against 126,
with only 86 of 480 zones usable. **A hand held 10 cm from the sensor at 16 ms
will read as empty space.**

**3. Switchover distance** — `STREAM_SWITCHOVER_DIST`, where the selected
context hands between its short and long paths. Reset 500; UM3683 §2.5.5.1 uses
650; the driver wrote 650 unconditionally.

**And one hard limit that no setting touches:** UM3683 §2.6.1 fixes the ranging
period at **64 ns = 9.6 m**. The device cannot see beyond it. The "8.8 m" in the
marketing is inside this; there is no configuration that extends it.

## What was implemented, 2026-09-12

**Firmware.** `vl53l9cx_set_range_mode(dev, mode, switchover_mm)`; the context
and switchover became live variables instead of hardcoded constants; and
`CONFIG_VL53L9CX_SWITCHOVER_MM` sets the boot default.

**BLE.** `range_mode` (u8) and `switchover_mm` (u16) carved out of what was a
reserved `uint32` in `struct app_config`. The struct is still 16 bytes and
`PROTOCOL_VERSION` is unchanged **on purpose**: an older client sends zeros,
zero means FAR and "leave the switchover alone", and that is exactly what every
build before this did. Nothing changes behaviour silently.

**Web interface.** Four one-click presets, plus the context, switchover and
exposure as independent fields underneath. **The heatmap's colour scale now
follows the selected range** — it was fixed at 0–4 m, which makes a near-range
demo look almost flat, with a hand at 20 cm and a desk at 60 cm landing in the
same 10% of the ramp.

### The four modes

A **mode is a whole working point**, not a range setting: context, exposure,
switchover, resolution and frame period together. They are bundled because they
are not independent — a near target needs the SHORT context AND a low exposure
AND enough frame rate to follow a moving hand, and setting one without the
others gives a worse result than leaving the default alone.

| mode | band it is good at | context | exposure | resolution |
|---|---|---|---|---|
| **Close object** | **5 cm – 50 cm** | SHORT | 1 ms | 24×20 |
| **Desk / gesture** | **10 cm – 1.5 m** | SHORT | 2 ms | 24×20 |
| **Room detection** | **0.5 m – 4 m** | LONG | 4 ms | 54×42 |
| **Long range** | **2 m – 9.6 m** | LONG | 16 ms | 54×42 |

Switchover is 200 / 400 / 650 / 1500 mm respectively.

The near modes drop to **24×20** deliberately: ~90 ms per frame against ~334 ms,
so roughly 4× the frame rate. A hand moving at normal speed is unwatchable at
2.5 fps. The crop costs field of view, which matters for the paper's
energy-versus-zones curve and not at all for a hand 10 cm away.

The band is what each mode is **good at**, not what the sensor can do. The part
is specified 5 cm to 8.8 m and hard-limited to 9.6 m; no mode extends that, they
trade where inside it the measurement is accurate.

Each mode carries a **watch-out** shown in the UI. The one that matters most is
on Close object: *if a very close object reads as EMPTY rather than near,
exposure is still too high — it is saturating and the zone gets rejected.*

**The exposure figures are starting points, not measurements.** Nothing in this
repo has yet measured the valid-zone count against distance for either context.
Treat the presets as a place to start and adjust from what the amplitude split
says.

## Showing different distances on the demo — what to adjust

### Before the demo: measure the presets once

Half an hour, and it converts four guesses into four settings that work.

For each preset, put a flat target (a sheet of white card) at three distances
inside its band and record **valid-zone percentage** and the **amplitude split**.
What you are looking for:

- **invalid zones brighter than valid** → saturation. Lower exposure.
- **invalid zones near zero amplitude** → too little signal. Raise exposure, or
  the target is genuinely out of range.
- **valid count collapses as the target comes closer** → the classic close-range
  failure, and it is exposure rather than context.

Write the numbers into this file. They are also the first real data for the
paper's range characterisation, so the work is not only for the demo.

### During the demo: the sequence that shows the most

1. **Room preset, hand sweeping at 0.5–2 m.** The default, and the shape people
   recognise: a hand moving across the field of view.
2. **Switch to Very near, hold a hand 10 cm out.** This is the one that lands —
   it is visibly a different instrument, and it is the capability that did not
   exist yesterday.
3. **Switch back to Room without moving the hand.** The near target degrades or
   disappears. That makes the point that range is a *setting*, not a property of
   the sensor, better than any explanation.
4. **Far preset, aim down a corridor.** Shows the 9.6 m ceiling honestly. Say it
   is a hard limit; it is more interesting than pretending otherwise.

### What still needs adjusting for that to go well

- **Apply latency.** Each change stops ranging, waits for STANDBY, writes and
  restarts. Measure it; if it is more than about a second the demo needs a
  "switching…" state rather than a frozen heatmap.
- **The colour legend** already follows the range, but the numbers under the bar
  should be checked at each preset — a legend reading 0–160 cm while the label
  says "5 cm to 50 cm" is the kind of detail an audience notices.
- **Frame rate at 54×42 is ~2.5 fps**, and a hand moving at normal speed will
  look stuttery. For a near-range demo consider **24×20**, which is ~90 ms per
  frame and roughly 4× faster — the field of view is cropped, which matters for
  the paper and not for a hand 10 cm away.
- **Orientation is still an unverified `VERIFY`.** The community driver flips
  180° and this driver deliberately does not rotate. Point at something
  asymmetric and check before demonstrating that a hand on the left appears on
  the left.

## Open, and worth doing next

- **Power mode.** `vl53l9_set_power_mode()` offers REGULAR / LOW / ULTRA_LOW,
  and ST use ULTRA_LOW for both SHORT-context profiles. Not exposed yet. It is
  likely to matter for close range *and* it is directly an energy knob, so it
  belongs in the same sweep.
- **Per-context switchover.** The register is per context, so NEAR and FAR could
  each keep their own value rather than sharing one field.
- **Measured range bands**, replacing the estimates in the preset table above.
