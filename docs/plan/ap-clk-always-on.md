# AP_CLK runs continuously — the decision, and what it obliges us to measure

**Decided 2026-09-04 (Victor): accept an always-on 8 MHz AP_CLK and measure what it
actually costs, rather than contort the design around a number nobody has.**

That is the right default. The alternative was to design around a guess, and this repo's
rule is that estimates size decisions and never enter the paper. This page exists so the
measurement actually happens, and so nobody later reads a stage-4 energy figure without
knowing there is a constant term underneath it.

---

## Why it is always on

AP_CLK comes from the GRTC fast clock output on P0.00. GRTC's clock output is configured
once at boot from devicetree and Zephyr exposes no runtime gate, so the driver's
`TURN_OFF` path — which drops XSHUT and the sensor rail — leaves this running.

It is not a choice of mechanism that could be swapped: **PWM cannot reach port 0**, and
the AP_CLK trace goes to P0.00 on the board. So on this hardware, GRTC is the only way to
put 8 MHz on that pin.

## Why "it may be noise" deserves an actual measurement

The intuition is reasonable: the sensor draws ~150 mW active, and at 0.1 Hz dwell with
~650 ms of activity per cycle that is roughly 6.5% duty, so on the order of **10 mW
average from the sensor**. A clock output sounds small beside that.

Two things make it worth checking rather than assuming.

**1. The clock term does not scale with duty cycle.** Everything else in this project
gets cheaper as the sensor is gated harder — that is the entire thrust of stage 4. The
AP_CLK term does not move. So it grows as a *fraction* of the total precisely as the
work succeeds, and at the low-duty limit it sets a floor that caps the battery claim no
matter how good the sensor gating gets. A term that is 3% of the budget today and 30% of
it after stage 4 is not noise; it is the result.

**2. The pin is probably not the expensive part.** Two components:

- **Switching the pin.** `P = C·V²·f`. At an assumed 10 pF of trace and pad capacitance,
  1.8 V IOVDD and 8 MHz, that is about **260 µW**. Genuinely small. *(Estimate, for
  sizing only — the capacitance is assumed, not measured.)*
- **Keeping the clock domain alive.** `clkout-fast` divides `pclk`, the 16 MHz
  high-frequency clock. Holding that domain up continuously is what stops the SoC
  reaching its deepest idle states, and on every Nordic part that has been the term that
  dominates this kind of trade. **This is the number that decides the question, and it
  is not one we can derive — it has to come off the Power Profiler.**

## The measurement, and it is cheap

A clean A/B, on the MCU rail, with the sensor held off so it contributes nothing:

1. Build as-is and record idle current.
2. Delete `clkout-fast-frequency-hz` from the board's `&grtc` node, rebuild, record idle
   current again.

The difference is the whole cost of the decision, pin and clock domain together. It takes
one rebuild and no extra instrumentation, and it should happen **early** — before the
stage-4 sweep, not after — because if the term is large it changes what that sweep means.

Record the result here with its date, per the repo's figures rule.

| | Idle current | Date | Notes |
|---|---|---|---|
| AP_CLK enabled | *not yet measured* | | |
| AP_CLK removed | *not yet measured* | | |
| **Difference** | | | |

## If it turns out to matter — the escape hatch exists

This decision is reversible **in firmware, without a board respin**, which is the reason
accepting it now costs nothing.

`nrfy_grtc_clkout_set()` is a static inline in the nrfx HAL that is already linked into
this build:

```c
/* modules/hal/nordic/nrfx/haly/nrfy_grtc.h */
nrfy_grtc_clkout_set(NRF_GRTC, NRF_GRTC_CLKOUT_FAST, false);
```

So the driver's `clock_stop()` / `clock_start()` — today no-ops on this board because the
clock is board-supplied — can gate the GRTC output directly, in a few lines, in exactly
the place the design already has for it. Zephyr not exposing an API is an inconvenience,
not a constraint.

Two caveats to check if we go that way, neither of them blocking:

- **The sensor needs the clock before it will answer at all.** Gating has to be
  sequenced with `TURN_ON` the same way the rail and XSHUT already are — the driver's
  existing ordering handles this, since `clock_start()` runs before any I²C contact.
- **GRTC is also the system timer.** Disabling the *clock output* is not the same as
  disabling GRTC, and `nrf_grtc_clkout_set` touches only the output. Worth confirming on
  the bench that the kernel clock is undisturbed, because a subtly broken timebase would
  poison every timing number the paper reports.

## What this changes in the repo today

- The driver's `TURN_OFF` no longer claims to gate AP_CLK. Corrected in
  `firmware/drivers/vl53l9cx/README.md` and in the PM comment in `vl53l9cx.c`.
- The driver logs, at init, that AP_CLK is board-supplied and **not** gated with the
  sensor domain — so it is visible in the first lines of console output rather than
  buried here.
- Nothing in the board file changes. Always-on is what it already does.
