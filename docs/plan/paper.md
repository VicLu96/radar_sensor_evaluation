# Paper plan

Written 2026-08-31.

## The honest problem statement

**People counting is solved.** Overhead ToF counters are published, commercial and
accurate. A paper that counts people with a new sensor and reports 95% accuracy is not
a contribution — it is a product datasheet with citations.

So the contribution cannot be *that* it counts. It has to be something nobody can
currently look up.

## What is genuinely unknown

The VL53L9CX shipped in 2026. It is the first dToF part at this resolution — **2268
zones, 35× a VL53L8CX** — and its power figure (150 mW typical) is high enough that
battery operation is not obviously viable at all.

Nobody has published:

1. **What a 2.3K-zone dToF frame actually costs in energy** on a real embedded host,
   broken down by integration, bus transfer and processing.
2. **How much of that resolution people counting actually needs** — the accuracy-versus-
   zones curve. High resolution is *assumed* better; the amount better, and the price,
   is unmeasured.
3. **Whether the bus, not the sensor, is the binding constraint** at high zone counts.
   With I²C and ~9 KB frames this is a real possibility, and it is a result that
   generalises to every high-resolution ToF integration.

That third one is the interesting one. It reframes the design problem from "how fast is
the sensor" to "how much data can you afford to move", which is not where the field's
attention currently is.

## Proposed contribution

> **An energy-accuracy characterisation of high-resolution dToF sensing for
> battery-powered occupancy analytics**, using the VL53L9CX on an nRF54L15 BLE node.

Three claims, each measured:

1. **An energy breakdown per frame** — integration, I²C transfer, processing, radio —
   at every available resolution and frame rate. *Nobody has this for this class of
   sensor.*
2. **An accuracy-versus-resolution curve** for people counting, identifying the point
   of diminishing returns. The useful, slightly contrarian finding would be that
   counting saturates well below 2268 zones — which tells system designers when the
   expensive part is *not* worth it.
3. **A demonstrated multi-month battery operating point**, with a measured discharge
   curve, not a spreadsheet projection.

Plus one experiment that justifies the sensor: **two people walking abreast**, resolved
at high zone counts and merged at low ones. That is the concrete case where resolution
buys accuracy, and it should be shown, not asserted.

## Why this survives review

- **Reproducible:** hardware is commercially available, firmware open-sourced, scenario
  list published.
- **Negative results included.** If counting saturates at 8×8, say so — it is more
  useful to the field than a claim that more zones are better, and it is the kind of
  finding reviewers trust.
- **The privacy property is architectural**, not asserted: the device has no code path
  that transmits a frame.
- The energy measurement is **instrumented and phase-attributed**, not derived from
  datasheet arithmetic.

## Venue candidates

- **Sensors** (MDPI) — fast, fits sensor characterisation, open access
- **IEEE Sensors Journal** — stronger, slower
- **IEEE Internet of Things Journal** — if the system/BLE framing leads
- **EWSN / IPSN** — if the energy-accuracy trade-off leads; better fit for the
  contrarian resolution finding, harder to get in
- **IEEE SENSORS conference** — good first outing, deadlines usually spring

Recommendation: aim the full version at *Sensors* or *IEEE Sensors Journal*, with the
conference as a fallback for an early subset.

## Structure

1. **Introduction** — occupancy sensing, privacy, why battery operation is the unlock
2. **Related work** — ToF counting, low-resolution IR array learning approaches,
   energy-aware sensing
3. **Sensor characterisation** — VL53L9CX modes, energy per frame, bus analysis.
   *The section nobody else can write yet*
4. **System** — nRF54L15 node, duty-cycle architecture, on-device pipeline, BLE
5. **Method** — detection and counting; test setup, scenarios, ground truth
6. **Results** — energy breakdown; accuracy vs resolution; abreast experiment; measured
   battery life
7. **Discussion** — when the high-resolution part is worth it and when it is not; the
   I²C-versus-I3C limitation stated plainly
8. **Conclusion**

## Threats to the contribution — watch these

| Threat | Response |
|---|---|
| **ST or a competitor publishes a characterisation first** | The part is months old; move. This is a real deadline |
| **Only one resolution mode exists** (Phase 0.2) | The curve collapses to a point. Fall back to frame-rate and duty-cycle as the swept axis, and lead on the energy breakdown |
| Accuracy saturates so low the sensor looks pointless | **That is a finding, and a good one.** Report it. Do not bury it |
| Single-unit FoV limits the claim to doorways | State the scope honestly in the title and abstract |
| Reviewers ask for a learned baseline | Phase 3 of the implementation plan; keep the classical baseline as the comparison |

## What to do first, for the paper specifically

The measurement rig (implementation Phase 2) is the long pole, not the algorithm. An
energy claim that cannot be attributed to a phase is not publishable, and that
capability depends on a board whose sensor and MCU rails can be measured separately —
**which is a hardware question to settle now**, while the board can still change.
