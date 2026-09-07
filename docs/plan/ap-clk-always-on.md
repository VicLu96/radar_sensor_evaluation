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

## 2026-09-06 — "always on" was not actually always on

> **Why this was never cosmetic — UM3683 §2.5.1, read 2026-09-06.** An active external
> clock is one of three necessary conditions for the device to leave `POWER_OFF`, and ST
> states that if any condition *"becomes invalid, the sensor returns to the off state"*. So
> an intermittent AP_CLK does not produce degraded ranging or a corrupt frame. It resets
> the part, continuously. See `docs/research/um3683-power-on-and-boot.md`.


Victor measured the 8 MHz on P0.00 and found it **intermittent**: on and off, not absent.
Two causes were found in the tree, and only one of them is the real one.

**The cause: the SYSCOUNTER sleeps with the CPU.** The fast clock output is a divider
hanging off the GRTC SYSCOUNTER, so it produces edges only while the SYSCOUNTER runs.
This build has `CONFIG_NRF_GRTC_TIMER_AUTO_KEEP_ALIVE=y`, whose own Kconfig help says it
keeps the SYSCOUNTER awake *"when any core is in active state"* — which is to say it lets
the SYSCOUNTER sleep as soon as the CPU does. The application heartbeats and then sleeps,
so the clock ran in bursts that tracked CPU activity. Nothing in software looked wrong.

Zephyr has the right symbol for this, `CONFIG_NRF_GRTC_ALWAYS_ON`, which makes the timer
driver call `nrfx_grtc_active_request_set(true)`. It is **promptless**, nothing in this
tree selects it, and it therefore cannot be set from `prj.conf`. So `clock_start()` issues
the request itself, once, and never releases it.

**The second suspect, kept but disabled.** The first hypothesis was the high-frequency
clock domain stopping under the divider. That is now behind
`CONFIG_VL53L9CX_HOLD_HFCLK`, **default n**, with `&clock` enabled in the application
overlay so it is one line away if needed. It is off by default because it is expected to
be redundant, and a redundant clock request inflates idle current — which is precisely the
number the A/B below has to measure honestly.

**Drive strength, separately.** The waveform was also described as not a clean square.
Independent of the on/off question, the pin was on `NRF_DRIVE_S0S1` (~0.5 mA), and slewing
1.8 V across ~15 pF in a twelfth of the 125 ns period needs `I = C·dV/dt ≈ 2.7 mA`. The
application overlay now overrides the GRTC pinctrl to `NRF_DRIVE_H0H1`, with a **sleep
state that also drives** — the board's `grtc_sleep` carries `low-power-enable`, which
disconnects the pin, and that is a way to stop a clock without anything in software
appearing to do so. *(Arithmetic for sizing, not a measurement.)*

**What to check on the bench, in order:**

1. Is the 8 MHz now continuous? The driver logs `AP_CLK: GRTC SYSCOUNTER held ACTIVE`.
2. Is the edge clean? If not, the trace and the probe are the next suspects, not the SoC.
3. Only then retest I²C — a clock that stops is enough on its own to explain the silence.

If 1 still fails, set `CONFIG_VL53L9CX_HOLD_HFCLK=y` and repeat.

**This changes what the A/B below measures.** The SYSCOUNTER now runs through idle, so the
"AP_CLK enabled" row includes that cost. That is the honest number — it is what the design
actually pays — but it means the measurement must be taken with this firmware, not with
anything built before today.

## 2026-09-07 — the `apclk-always-on` snippet, for one decisive bench test

**Tick `apclk-always-on` in the nRF Connect extension's Optional snippets, without
`x-nucleo`.** That is the whole setup. It builds the custom `water_sense_board` firmware
with every clock-holding mechanism engaged at once.

### Why a second mechanism exists at all

The 2026-09-06 fix requests **GRTC SYSCOUNTER ACTIVE**, and the reasoning behind it was
stated here with more confidence than it deserved. An expert review of the driver pushed
back, and the objection holds up:

- `CLKOUT_FAST` divides the GRTC's **hfclock** — devicetree binds that to `pclk`, a 16 MHz
  `fixed-clock`. `SYSCOUNTER.CLKCFG.CLKSEL` selects among **low-frequency** sources
  (LFXO, SystemLFCLK, LFLPRC). Different registers, different clock trees.
- Upstream Zephyr enables `CLKOUT_FAST` with no ACTIVE request at all. If the two were
  coupled that would be an upstream bug.
- The GRTC has its own `STATUS.CLKOUT.READY` handshake, which reads like CLKOUT makes an
  independent clock request.

What **is** confirmed is the premise about idle: `CONFIG_NRF_GRTC_TIMER_AUTO_KEEP_ALIVE=y`
sets `MODE.AUTOEN = CpuActive`, documented as *"any local CPU that is not sleeping keeps
the SYSCOUNTER active"* — so the SYSCOUNTER does drop out at WFI. Whether `CLKOUT_FAST`
follows it down is the unproven link.

The honest response is to stop arguing and hold both.

### What the snippet holds

| | Mechanism | In the default build? |
|---|---|---|
| 1 | GRTC **SYSCOUNTER ACTIVE**, requested once, never released | yes |
| 2 | **HFXO / high-frequency clock domain**, `clock_control_on()`, never released | **only with this snippet** |

### Reading the result

Scope P0.00 across a long idle — several seconds with nothing but heartbeats.

| Observation | Conclusion |
|---|---|
| Continuous **with** the snippet, intermittent without | Mechanism **2** is the real one. The default build is wrong and must adopt it. |
| Continuous **both** ways | Mechanism 1 was sufficient. Leave the default alone; this snippet is then only a diagnostic. |
| **Still intermittent** with the snippet | It is not clock gating at all. Move to the pin and the board: drive strength, the trace, the probe ground. |

The firmware states which mechanisms are engaged, in the first lines of the log:

```
AP_CLK: GRTC SYSCOUNTER ACTIVE clear -> set — without this the clock output stops ...
AP_CLK: high-frequency clock ALSO held on (0) — HFXO requested and never released
```

Without the snippet the second line reads
`AP_CLK: HFCLK not separately held (CONFIG_VL53L9CX_HOLD_HFCLK=n)`, so a capture can never
be ambiguous about which firmware produced it.

`main.c` separately reports the GRTC clkout enable bit and the P0.00 `CTRLSEL` field, which
together confirm the peripheral is enabled and actually owns the pin. If those two say
"enabled" and "GRTC" and the scope still shows nothing, the fault is past the SoC.

### Do not measure energy with this build

HFXO runs continuously. That is the point for this test and disqualifying for any other:
the idle current under this snippet is not a number that belongs in the table above.

## What this changes in the repo today

- The driver's `TURN_OFF` no longer claims to gate AP_CLK. Corrected in
  `firmware_test/drivers/vl53l9cx/README.md` and in the PM comment in `vl53l9cx.c`.
- The driver logs, at init, that AP_CLK is board-supplied and **not** gated with the
  sensor domain — so it is visible in the first lines of console output rather than
  buried here.
- Nothing in the board file changes. Always-on is what it already does.
