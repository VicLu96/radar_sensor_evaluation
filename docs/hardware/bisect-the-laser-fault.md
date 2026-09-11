# Bisecting the laser fault — a bench procedure

Written 2026-09-11. **Five flashes, in order. Stop at the first that faults.**

## Why this rather than more theorising

The sensor produced 12×10 frames on 2026-09-10 and has produced none since. Diffing
`working-2026-09-10` against `HEAD` shows **five** changes to how the device is configured,
not the one I first claimed:

| | Working | Now |
|---|---|---|
| `set_exposure()` called at all | **no** — `NB_SHOT_STEP_n` at reset **0 shots** | 16 ms |
| `DSS_DEFAULT_INIT_LUT` | reset 0 | 3 |
| `DISTANCE_SWITCHOVER` | reset 500 | 650 |
| `DISTANCE_RTN_SHORT_OFFSET` | reset 0 | 2 |
| Resolution | **12×10** (binning 8) | 24×20 / 54×42 |

Each is now a tick-box, so one variable moves per flash.

## The procedure

Tick **one** snippet in the nRF Connect build configuration, flash, and watch for about
40 seconds — long enough for three capture attempts at 5-second intervals.

| # | Snippet | What it tests | If it FAULTS | If it WORKS |
|---|---|---|---|---|
| **0** | `legacy-config` | **The control.** All five reverted. | **Stop.** The firmware is exonerated — the hardware changed since 2026-09-10 and no register bisect will find it. Look at the board. | The hardware is fine and the regression is in these five writes. Continue. |
| **1** | `bisect-1-exposure` | Exposure alone | **Most likely outcome.** The VCSEL supply cannot deliver shot current. The backoff walks down to the threshold, and that number is the result. | Exposure is exonerated; continue. |
| **2** | `bisect-2-dss` | `DSS_DEFAULT_INIT_LUT` alone | DSS changes which SPADs are enabled, and *SPAD supply overload* is one of the device's error bits. | Continue. |
| **3** | `bisect-3-profile` | switchover + offset alone | Surprising — these are distance-computation parameters, not drive parameters. | Continue. |
| **4** | `bisect-4-resolution` | 24×20 instead of 12×10 | Binning 4 instead of 8: fewer SPADs summed per zone, more zones, more DSS work per frame. | All five are individually innocent, so it is an interaction — and that is a different and more interesting problem. |

**Read the version line** at the top of every log to confirm what is actually running:

```
VERSION: planning-milestone-2026-09-11-...
config : 12x20, exposure 16 ms, I2C 400 kHz, power held
```

## What each outcome means

**Step 0 faults.** This is the important branch and it is not the expected one. It would
mean the board stopped being able to do what it did on 2026-09-10 — and the IMU rework
happened in between. Check the VCSEL supply path and the load switch before touching
firmware again.

**Step 1 faults.** The expected outcome, and it is a supply result rather than a firmware
one. `VBAT_LDD` is the laser driver's own rail, straight off the load switch, which is
exactly why the sensor's `AVDD`/`DVDD`/`IOVDD` monitors all read clean while CABDT reports
`0x0F00`. UM3683 Table 23 puts the 16 ms ambient profile at **450–800 mW** — 136–242 mA
average at 3.3 V, with pulse current 20–60× higher during emission given the 1.6–4.4% VCSEL
duty in Table 21 — against a board that folded back at **100 mA**.

The fix is then the rail, not a register. But the **threshold exposure** is worth recording
either way: it is the first real measurement of what this board can actually feed the VCSEL,
and it belongs in the paper.

**Scope `+VBat_switched` during a capture while you run step 1.** That is the rail the whole
chain points at, and the droop will be visible on the first step of the frame, where the
shot count is highest.
