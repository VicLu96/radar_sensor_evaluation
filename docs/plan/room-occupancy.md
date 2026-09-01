# Reframing: room occupancy, not doorway counting

Written 2026-08-31, after Victor set the goal as **monitoring a room where people stay
for a while**, not a highly dynamic environment.

This is a better fit for the hardware than doorway counting was, and it removes the
constraint that was about to force a design compromise. But it does not make the
problem easier — it moves the difficulty from *timing* to *segmentation*, and that
needs saying plainly.

---

## What this fixes

**The frame-rate crunch disappears.** Doorway counting needed enough frames on a person
crossing in 0.8 s. People who stay put need nothing of the kind.

| | Doorway counting | Room dwell |
|---|---|---|
| Required rate | 3–5 fps | **0.05–0.2 Hz** (every 5–20 s) |
| 54×42 @ 400 kHz feasible? | Marginal — ~2 frames per crossing | **Comfortably** |
| 1 MHz I²C | **Go/no-go** | Nice to have |
| Duty cycle at 54×42 | 65% at 1 Hz — nothing to save | **~2% at 0.1 Hz** |

**The 1 MHz question is demoted** from go/no-go back to a tuning detail. 400 kHz is
fine.

**Full power-down becomes clearly correct.** One cycle is ~650 ms of activity (250 ms
blob reload + integration + 370 ms transfer). At 0.1 Hz that is **~6.5% duty**, so over
93% of the time the sensor domain can simply be off. This is where the multi-month
battery claim comes from, and it is now a comfortable margin rather than a stretch.

**The low-power framing gets much stronger.** A battery room sensor that lasts a year is
a real product; a battery doorway counter that lasts a year while missing half the
traffic is not.

---

## What this breaks, and it is not small

### The field of view does not cover a room

54° × 42° at height `h` gives a floor footprint of roughly **1.02h × 0.77h**:

| Mount height | Floor footprint |
|---|---|
| 2.5 m | 2.5 × 1.9 m |
| 3.0 m | 3.1 × 2.3 m |
| 4.0 m | 4.1 × 3.1 m |

**A ceiling-mounted sensor at normal height covers about 6 m² — a desk cluster or a
small meeting table, not a room.** This was true for the doorway framing too, but it did
not matter there: a doorway is narrow by definition. For "monitor a room" it matters a
great deal, and it must be settled before any test site is chosen.

Three options, and the choice shapes everything downstream:

| Option | Coverage | Cost |
|---|---|---|
| **Ceiling, accept the patch** | ~6 m² | Honest scope: "desk-cluster occupancy". Clean background plane, minimal occlusion. **Simplest and most defensible** |
| **Ceiling, mount very high** | 4 m → ~13 m² | Needs the height; range is not the limit (8.8 m) but angular resolution per person drops |
| **Corner / wall, looking across** | Much larger — the 8.8 m range finally gets used | **Occlusion**: people hide behind people and furniture. No flat background plane. Substantially harder segmentation |

**Recommendation: ceiling-mounted, scope stated honestly.** Claim what one unit actually
covers and say multi-unit coverage is future work. A paper that overclaims its coverage
gets caught; one that states a 6 m² footprint and characterises it well does not.

### The hard problem is now static-person segmentation

This is the real change. A doorway counter can lean on motion — people move through, and
motion is easy to see. **A person sitting still for forty minutes is, to a depth sensor,
furniture.**

Specifically:

- **Background models absorb stationary people.** A running median over frames will,
  given enough time, learn a seated person as background and stop reporting them. At
  0.1 Hz you have hours of frames — absorption is guaranteed unless it is designed
  against.
- **There is no motion cue to fall back on**, unlike doorway counting.
- **The failure is silent**: occupancy quietly drops to zero while the room is still
  full, which is the worst possible failure mode for an HVAC or safety application.

This is the central technical problem of the project now, and it deserves to be treated
as such rather than as a footnote in the algorithm section.

---

## Approaches to static-person segmentation

Ordered by how much they are worth trying first.

**1. Bootstrap the background from a known-empty room, then freeze it.**
Simplest and most robust. Calibrate once at install, adapt only very slowly (hours) and
only in zones with no current detection. Weakness: furniture moves, and the model drifts.

**2. Shape and height priors.** A person is a coherent blob of a certain size at a
certain height above the floor. A chair is smaller and lower; a desk is flat and static.
With 2268 zones there is enough spatial detail to distinguish these — **this is where
the resolution earns its cost in this framing**, replacing the two-abreast argument that
belonged to the doorway version.

**3. Depth micro-variation — the interesting one.**
Depth is reported as **15-bit millimetres**. A seated person is never perfectly still:
posture shifts, breathing, small movements. Furniture is. Comparing frames minutes apart
and looking for zones with millimetre-scale variance may separate living occupants from
objects **without any motion tracking at all**, and it works at arbitrarily low frame
rates.

Whether the sensor's noise floor at 0.1 Hz permits this is **unknown and needs
measuring early** — a static-scene noise characterisation is cheap and answers it. If it
works, it is a genuinely novel contribution and not something the doorway literature
covers.

**4. Amplitude and ambient channels.** The frame carries amplitude and ambient IR per
zone alongside depth. Skin and clothing return differently from a desk surface. Free
data already being read — worth examining before adding anything clever.

---

## Consequences for the plan

- **Application** — lead is now **room/desk-cluster occupancy and dwell time**, not
  line-crossing counting. Doorway counting becomes a secondary demo of the same pipeline.
- **Stage 3** — the algorithm changes shape: background bootstrapping and freezing,
  shape priors, and a static-occupancy detector. Tracking becomes secondary.
- **Scenario list** — needs rewriting around dwell. The critical new scenarios:
  **a person sitting still for 30+ minutes** (does the count survive?), **a person
  leaving while another stays**, **furniture moved mid-session**, and **an empty room
  for hours** (false-positive drift).
- **Stage 4** — the target duty cycle drops by an order of magnitude, which makes the
  battery claim much stronger and full power-down clearly correct.
- **Paper** — the energy-accuracy framing survives intact and improves: very low duty
  cycle is exactly what "leveraging low-power capabilities" should mean. The accuracy
  axis becomes *static occupancy correctness over time*, which is harder and less
  reported than transit counting.
- **Privacy** — strengthened. Rooms where people dwell (offices, meeting rooms, care
  settings) are precisely where cameras are unacceptable.

## The question that now needs answering first

**What room, and how big?** Coverage is the binding constraint, and everything —
mounting height, whether one unit suffices, what the paper can claim — follows from it.
A 3 × 2 m patch is a genuine answer if the target is a desk cluster or a small meeting
table; it is not an answer for a 40 m² office.
