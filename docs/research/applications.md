# Candidate applications and demos

Researched 2026-08-31. Ranked by whether they exploit **what this hardware is
uniquely good at** — 2268 zones at low duty cycle on a battery — rather than by how
impressive they sound.

## The selection criterion

The VL53L9CX is expensive, high-resolution and power-hungry (150 mW active). Any
application a **VL53L8CX at 64 zones** could do equally well is a bad demo for it: it
argues for the cheaper part. A good demo must need either the **angular resolution**
(1°) or the **range** (8.8 m) — and must survive being duty-cycled to ~1 Hz.

That rules out more ideas than it sounds like, and it is the right filter.

## Primary — battery people counter over BLE (Victor's proposal)

**Keep it.** It is the right lead application and the paper's backbone.

- **Geometry:** overhead, doorway or corridor.
- **Why this sensor:** two people walking abreast merge into one blob at 8×8; at 54×42
  they separate. That is a *measurable* claim, and it is exactly the classic failure
  mode of cheap counters. **Design the experiment around it.**
- **Why low power matters:** a mains-free counter can be stuck to a lintel with
  adhesive in a rented building. That is the actual commercial unlock, and it is what
  the energy work buys.
- **Output:** counts and direction over BLE. Never frames.
- **Risk:** it is also the most crowded field. The novelty must come from the energy
  characterisation, not from counting people, which is solved.

## Strong secondary demos

Each of these is cheap once the counter works, and each stresses a different part.

**Queue length and dwell time.** Same pipeline, different aggregation: how many tracks
are stationary in the FoV and for how long. Retail and canteen framing. Costs almost
nothing extra to demo and is visibly useful.

**Fall detection / bed exit — assisted living.** Overhead in a doorway or over a bed;
a track whose depth profile flattens abruptly to floor level. **This is the demo where
privacy is not a nice-to-have but the entire reason the product can exist** — cameras
in bedrooms are unacceptable, depth is not. Strong story, and genuinely uses the
resolution to tell a person-shape from a dropped bag. Higher clinical validation burden,
so demo it, do not claim it.

**Desk / room-level occupancy for HVAC.** The building-energy framing has real
literature behind it. Honest limitation: one unit covers ~3 × 2 m at ceiling height, so
this is desk-cluster occupancy, not whole-room. Say so.

**Smart door / approach detection.** Long range (8.8 m) means a person can be detected
approaching well before arrival — useful for pre-opening a door or waking a
higher-power system. Uses the range spec specifically.

## Ideas considered and rejected

Worth recording so they are not re-proposed later:

- **Gesture recognition** — needs frame rates that defeat the low-power premise.
- **Whole-room occupancy from one unit** — the FoV does not reach. Would need multiple
  units, which changes the product.
- **Anything needing identity or re-identification** — depth deliberately cannot, and
  the privacy claim depends on it staying that way.
- **Generic obstacle detection for robots** — real, but needs high frame rate and
  argues for a cheaper part.

## The unifying demo

One device, one firmware, three BLE-reported modes selected by configuration:

1. **Count** — line-crossing tally with direction
2. **Occupancy** — current tracks present, with dwell
3. **Event** — a flagged anomaly (fall-like, or loitering beyond a threshold)

Same detection front-end, three aggregations. It demonstrates breadth without three
firmwares, and it makes the paper's system section honest: one pipeline, characterised
once.

## What makes this defensible rather than a gadget

The applications above are all *known*. Nobody needs another people counter. What is
not known — because the part is months old — is **what a 2.3K-zone dToF sensor costs in
energy per useful inference, and how far resolution can be cut before counting accuracy
degrades.** Every application here is a vehicle for that measurement. See
[`../plan/paper.md`](../plan/paper.md).
