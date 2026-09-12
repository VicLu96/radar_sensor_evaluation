# Plan: low-power people counting

Stage 3. The mode that makes this a product rather than an instrument.

Opened 2026-09-12. **This is a planning document — nothing here is implemented.**
Expert review findings are appended in §7 as they arrive; pick them up from there.

---

## 1. What the mode does

A **People counting** mode, selectable from the web interface alongside the
measurement modes. Selecting it:

- sets the sensor to **maximum resolution, 54×42** (2268 zones), far context,
  room exposure;
- switches the firmware from streaming frames to running detection on-device;
- reports **only the number of individual people detected** — a count, not a
  scene;
- offers **Calibrate**, which stores an empty-room background.

The count is the only thing that leaves the device in the deployed build. Frames
never do, and that is a property of the binary rather than a policy — see
`roadmap.md` §2.2.

### Why maximum resolution

Because the paper's headline claim is the accuracy-versus-zone-count curve, and
it needs a top of the curve. 54×42 is where "does resolution buy anything" gets
answered. **It is also the expensive end** — ~334 ms of I²C per frame and
450–800 mW while active — so this mode is the one the duty cycling has to rescue.

> **A caveat that could reshape this whole mode.** Full-resolution SNR has never
> been measured. Binning goes 4→2 between 24×20 and 54×42, so each zone is built
> from a quarter as many SPADs and per-zone amplitude should fall from the
> measured 126 to roughly 32. **If that is too low to threshold reliably, the
> counting mode should run at 24×20 and the paper's resolution axis is shorter
> than hoped.** Measure it before building on it — it is one bench session.

---

## 2. The user flow

```
  web interface                         firmware
  ─────────────                         ────────
  select "People counting"   ───────▶   config write: 54×42, far, 4 ms
                                        detection mode D1 armed
        │
        ├─ "Calibrate" ──────────────▶  EMPTY THE ROOM FIRST
        │                               16 frames, per-zone background
        │                               per-zone reliability
        │  ◀────────── progress ─────   "3/16 … 16/16"
        │  ◀────────── quality ──────   "1,932 of 2,268 zones usable (85%)"
        │
        ├─ "Start counting" ─────────▶  detection mode D2/D3
        │                               watch tier 0.05–0.2 Hz
        │                               track tier 1.5–2.5 fps on activity
        │
        │  ◀────────── count ────────   count, confidence, activity flag
        │              (notify, and in the advertisement)
```

**Calibration is a command, not a timer**, because only a human knows the room is
empty. The quality number matters as much as the count: *"1,932 of 2,268 zones
usable (85%)"* tells you the mount is wrong before the counts do, and at oblique
incidence far more zones are expected to be permanently unreliable than an
overhead mount would produce.

### Does calibration survive a power cycle?

**Open question, and it is not the same question as config persistence.** Config
deliberately has no NVS — the node comes up in a known state. But a calibration
costs an empty room and 16 frames, and losing that on every power cycle is a
different kind of cost. The background model is ~4.8 KB.

Options: keep it in RAM only (simple, lost on reset); store in RRAM (survives,
but stale after furniture moves); store with a timestamp and temperature and warn
when it is old. **Leaning to the third**, since the header already records
temperature and a stale background is the failure mode that matters.

---

## 3. What is published, and what it tells us

A scan of the literature, 2026-09-12. **None of it is at 2268 zones from a corner
mount, which is why the project exists** — but the failure modes transfer.

### The adjacent art

- **ST's own 8×8 parts (VL53L5CX / VL53L8CX)** are marketed for exactly this:
  *"detect and track multiple targets within the FoV, with a 64-zone depth
  measurement"*, *"detects human presence quickly and at low-power"*. **64 zones.**
  The VL53L9CX gives 2268 — 35× more — and the honest question the paper asks is
  whether counting needs any of that. ST shipping presence detection on 64 zones
  is itself evidence for the "saturates low" hypothesis.
- **Overhead depth people counting** (Kinect era and ToF cameras) reports
  **90–98%** accuracy. Real-time on modest hardware: one method runs in **9 ms per
  frame** and reports ~95%. These numbers are the bar, and they are **not** what
  this paper is competing on — they are overhead, high resolution, mains powered.
- **Blob analysis on connected depth regions with height characteristics** is the
  standard classical pipeline, which is what the design here already follows.
- **Mounting matters in the literature too**: work is split between ceiling-mount
  over a narrow passage and wall-mount with depth+intensity. A ceiling *corner* at
  41–50° is neither, and the oblique geometry is the part with least prior art.

### The merged-blob problem, which is the real risk

The literature is explicit that this is the hard part, and equally explicit that
**depth is what solves it**: two people connected in an image *"can be separated
by using the depth data"* — targets separated in physical space but merged in the
projection. **Watershed** on the depth surface is the classical tool for splitting
touching objects, treating the image as a topography and flooding it.

That is encouraging for us: we have nothing *but* depth. But it is untested at
oblique incidence with ~3–13 zones per person, and §11 of the BLE plan already
names this as **the single risk most likely to decide whether the paper has a
result.**

### What we should NOT copy

Nearly all of the above assumes an overhead view where a head is the closest
point. **From a corner that is false** — the nearest point is whatever body part
faces the sensor and it changes as someone turns. Any method resting on
"find local depth minima = heads" is inapplicable here, and that rules out a
large fraction of the published pipelines.

Sources: [VL53L5CX](https://www.st.com/en/imaging-and-photonics-solutions/vl53l5cx.html),
[VL53L8CX](https://www.st.com/en/imaging-and-photonics-solutions/vl53l8cx.html),
[real-time counting in cluttered scenes](https://arxiv.org/pdf/1804.04339),
[splitting merged objects in detected blobs](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10229503),
[video content analysis using depth sensing](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/9805266).

---

## 4. Implementation sketch

Dummy code, to argue about rather than to compile. Types and sizes are real;
the algorithms are placeholders.

### 4.1 Memory

```c
/* 54x42 = 2268 zones. ~114 KB of 188 KB is already used, so this has to
 * live in roughly 60 KB. Every array here is sized at compile time —
 * nothing in the detection path allocates. */

#define ZONES  VL53L9CX_ZONES_FULL      /* 2268 */
#define MAX_BLOBS   32                  /* far more than 3-5 people need */
#define MAX_TRACKS  16

struct background {
    uint16_t mean_mm[ZONES];      /* 4536 B  per-zone empty-room distance   */
    uint8_t  reliability[ZONES];  /* 2268 B  percent of frames zone was valid */
    uint16_t temperature_raw;     /*         drift reference                */
    uint32_t captured_at_s;       /*         uptime; staleness warning      */
    bool     valid;
};                                 /* ~6.8 KB */

struct detect_state {
    struct background bg;
    uint16_t prev_mm[ZONES];      /* 4536 B  motion reference               */
    uint8_t  label[ZONES];        /* 2268 B  connected components, 0 = none */
    uint8_t  fg[ZONES / 8 + 1];   /*  284 B  foreground bitmap              */
    struct blob  blobs[MAX_BLOBS];
    struct track tracks[MAX_TRACKS];
    uint8_t  n_blobs, n_tracks;
};                                 /* ~14 KB — comfortable */
```

### 4.2 The per-frame pipeline

```c
/* Runs on the capture thread, after vl53l9cx_capture() returns.
 *
 * Cost is not the issue: two passes over 2268 zones on a 128 MHz M33 is well
 * under a millisecond, against a 334 ms I2C read. SENSOR ACTIVE TIME is the
 * only thing worth optimising — see CLAUDE.md.
 */
static uint8_t detect_frame(struct detect_state *s,
                            const struct vl53l9cx_frame *f)
{
    foreground_mask(s, f);      /* bg - d > thr, valid, reliable, amplitude  */
    despeckle_3x3(s);           /* majority filter; kills single-zone noise  */
    label_components(s);        /* 8-connectivity -> s->blobs               */
    gate_by_size(s);            /* RANGE-NORMALISED: the gate is a function  */
                                /* of each blob's own mean distance         */
    split_merged(s);            /* THE HARD PART — see section 3            */
    measure_motion(s, f);       /* per-zone diff vs prev_mm, inside blobs   */
    associate(s);               /* nearest neighbour: centroid, size, depth  */
    update_lifecycle(s);        /* TENTATIVE -> CONFIRMED -> DORMANT -> LOST */

    memcpy(s->prev_mm, /* this frame's distances */, sizeof(s->prev_mm));
    return count_confirmed_and_dormant(s);
}
```

### 4.3 The trap, written into the code

```c
/*
 * NEVER ADAPT THE BACKGROUND UNDER A LIVE TRACK.
 *
 * Background models normally adapt, to absorb moved furniture. Do that
 * naively here and A PERSON SITTING STILL IS ABSORBED AND VANISHES FROM THE
 * COUNT — the exact failure this whole design exists to prevent.
 *
 * So adaptation is allowed only for zones covered by NO confirmed or dormant
 * track, and even then slowly.
 */
static void adapt_background(struct detect_state *s)
{
    for (uint16_t i = 0; i < ZONES; i++) {
        if (zone_is_under_a_live_track(s, i)) {
            continue;                     /* <-- the whole point */
        }
        /* slow IIR toward the observed distance */
    }
}
```

### 4.4 Range-normalised size gating

```c
/*
 * A person's apparent size in ZONES depends on range, and the corner mount
 * makes that swing hard inside a single frame:
 *
 *     2 m  -> zone ~3 cm  -> a 45 cm person spans ~13 zones
 *     4 m  -> zone ~7 cm  ->                      ~6 zones
 *     8 m  -> zone ~14 cm ->                      ~3 zones
 *
 * 4x linearly, ~16x in area. A constant blob-size gate is therefore wrong
 * everywhere except at one distance. The gate must be evaluated per blob
 * against that blob's own mean distance.
 *
 * PARAMETERISATION IS UNRESOLVED — expected area falls roughly as 1/d^2, but
 * the constant depends on tilt and mounting height and has to be measured.
 */
static bool size_plausible(const struct blob *b)
{
    uint32_t expect = expected_zones_at(b->mean_mm);   /* ~ k / d^2 */
    return b->n_zones > expect / 3 && b->n_zones < expect * 3;
}
```

### 4.5 Two-tier duty cycling

```c
/*
 * STRUCTURAL, not an optimisation. A person walks 1.4 m/s:
 *
 *     0.1 Hz  -> 14 m between frames   no correspondence is possible
 *     1.5 Hz  -> 0.9 m                 marginal
 *     2.5 Hz  -> 0.6 m                 workable
 *
 * At 400 kHz the bus ceiling IS ~2.5 fps, so the track tier has no margin.
 * That is why the 1 kOhm pull-ups and the 1 MHz bus are on this critical path.
 *
 * And this is the paper's most interesting result: duty cycle scales with
 * ACTIVITY, not with time.
 */
enum tier { TIER_WATCH, TIER_TRACK };

static enum tier next_tier(const struct detect_state *s, enum tier now)
{
    if (now == TIER_WATCH && activity_detected(s))     return TIER_TRACK;
    if (now == TIER_TRACK && quiet_for(s, QUIET_S))    return TIER_WATCH;
    return now;
}
```

`activity_detected()` must not fire on sunlight, a curtain or noise. **How to
compute it cheaply and robustly is an open question** — it is the thing that
decides the battery life.

### 4.6 The same code offline

```c
/*
 * The detection code must run BOTH on-device and against a recorded file on a
 * PC. There are ~10 tunables to fit, and fitting them by repeating experiments
 * with people in a room does not scale.
 *
 * So detect_frame() takes a plain struct and touches no hardware, no Zephyr
 * primitive and no global. A small host harness replays a .wstof recording
 * through the identical translation unit.
 *
 * This is not a nicety. It is what makes the tunables fittable at all, and it
 * is why the recording format exists.
 */
```

---

## 5. Firmware and web interface work

**Firmware**

1. `detect.c` / `detect.h` — pure, no Zephyr, no globals. Buildable for the host.
2. Calibration: capture 16 frames, accumulate, quality report.
3. The counting GATT service `53f93001` — UUIDs already frozen in `ble_uuid.h`:
   Count `53f93002`, Detection Config `53f93003`, Calibration Control `53f93004`,
   Background Model `53f93005` (**notify and paged — a GATT attribute caps at 512
   bytes and the model is 4.8 KB**).
4. Count in the BLE **advertisement**, 8 bytes: version, instance, count,
   confidence, flags, `report_seq`, battery.
5. Two-tier scheduler.
6. A host harness replaying `.wstof` through the same `detect.c`.

**Web interface**

1. A **People counting** measurement mode that applies the settings.
2. A **Calibrate** panel: trigger, progress, reliable-zone percentage.
3. A **Count** panel: the number, large; confidence, activity flag, history.
4. A **detection overlay** on the heatmap — blob and track outlines, dev builds
   only. This is what makes the algorithm debuggable at all.
5. Detection tunables behind an "advanced" panel.

**Note the asymmetry:** items 3 and 4 on the web side only work in the
dev-stream build, because the deployed build has no frames to draw. That is
correct and intended.

---

## 5b. What the three reviews change — the corrected build order

Written after §7. **Read this before writing code; §4's sketch has known bugs.**

**Three bench sessions gate everything, and none needs people:**

1. **Full-resolution SNR** at binning 2. Decides whether 54×42 is a real point at
   all. If amplitude is unusable the watch tier gets *better* (binning 6 has ~9×
   the SPADs), so this changes the design either way.
2. **Verify the wide-family FoV** at 54×42 / 18×14 / 12×10 with retroreflective
   markers. 12×10 at binning 8 from a 108×84 array is 13.5×10.5 — non-integer, so
   the premise may have a hole.
3. **An overnight empty-room recording.** Sets the false-wake threshold directly,
   and it is *"the single highest-value first dataset item"*.

**Then, before any detection code:** the `lib/detect` split with no Zephyr
(CI-enforced by grep), time as a parameter, all state in one opaque struct, and a
synthetic frame generator. That harness is what makes ten tunables fittable.

**Corrections to §4 that must land in v1:**

| §4 says | change to | why |
|---|---|---|
| 3×3 majority despeckle | asymmetric open/close, remove ≤1, fill ≥7 | majority **erases far people**; ≥7 never bridges an abreast gap |
| 8-connectivity | **4-connectivity**, runtime-switchable | 8-conn **merges diagonally-touching people** |
| `expected = k / d²` | multiply area by d², **per zone** | integer division collapses the gate; per-zone solves straddling |
| global foreground threshold | per-zone `max(T_min, k·sigma)` | sigma varies 10× across a corner FoV |
| constant amplitude minimum | function of range | amplitude falls as 1/r²·cos θ |
| mean of 16 calibration frames | trimmed mean of 64–128, plus a **spread** map | one walk-through pulls the mean 190 mm |
| binary 75% reliability | validity **and** spread, both reported | a stable 12/16 zone beats a jittery 16/16 one |
| `bg[i] - dist[i]` | explicit `(int32_t)` casts | unsigned underflow makes **every excluded zone foreground** |
| greedy nearest neighbour | gate-then-nearest, merge/split aware | NN cannot represent "one measurement, two tracks" |
| `struct vl53l9cx_frame` input | raw depth/amplitude planes | saves 18.3 KB and makes replay trivial |
| background adapts | **frozen**, re-baseline on confident emptiness | a leaky integrator reintroduces the failure this exists to prevent |

**And two structural changes worth adopting now:**

- **The watch tier runs at 18×14, depth-only** — up to 24.6× less transfer, which
  is 95% of sensor-active time. Must stay in the *wide* family and needs its own
  calibration.
- **A 284-byte installer-painted exclusion mask** in the web UI. Kills curtains
  and windows more reliably than any algorithm.

---

## 6. Open questions before any code

1. **Full-resolution SNR.** Blocks everything. One bench session.
2. **`activity_detected()`** — the wake condition. Decides the battery claim.
3. **Splitting merged people.** Nothing in the current design does it.
4. **Range-normalised gate parameters** — need measuring at the real tilt.
5. **Calibration persistence** — §2.
6. **Someone already seated at power-up.** They have never moved in view, so
   motion cannot confirm them. Size and distance plausibility must carry it, or
   the system waits for a fidget.
7. **`lost_timeout` / `dormant_timeout`** — too short drops a still person, too
   long counts a departed one for minutes.
8. **Does background subtraction survive oblique incidence at all?** A wall at a
   grazing angle returns very little. The D1 reliable-zone percentage is the
   early warning, and it is cheap to get.

---

## 7. Expert review, 2026-09-12

*Three specialists were asked to review this plan: people detection on
low-resolution depth data, embedded implementation on Cortex-M, and evaluation
methodology for the paper. Findings are recorded below as they arrive.*

**STATUS: all three returned. This section is the pickup point.**

### 7.1 People detection on low-resolution depth — returned 2026-09-12

> Verdict: *"the right family, run in the wrong coordinate system with the wrong
> association layer."*

#### The geometry table, which the reviewer calls the project's central result

Derived from our own figures: zone pitch **~16.6 mrad, ~0.95°/zone**, so ~51°×40°
FoV. *VERIFY against ST's FoV spec — this was back-derived from our numbers.*

| slant range | zone | person width | person area | gap between two abreast, 15 cm clear |
|---|---|---|---|---|
| 3 m | 5 cm | 9 zones | ~290 | **3.0 zones** |
| 5 m | 8 cm | 5.4 | ~105 | **1.8 zones** |
| 8 m | 13 cm | 3.4 | ~41 | **1.1 zones** |
| 9.6 m | 16 cm | 2.8 | ~28 | **0.9 zones** |

**Lateral splitting of an abreast pair dies at about 5 m and is below the sampling
limit past ~7 m.** No watershed, no morphology, no CNN recovers a sub-zone gap.
*"Any claim otherwise is wrong."*

**Publish this as a measured curve — split success against (lateral separation,
range) — rather than trying to beat it.** A stated operating envelope is a better
contribution than a claim the problem is solved.

#### The reframe: split in RANGE, not in the raster

> *"Range is your high-resolution axis and the image plane is your
> low-resolution axis."*

Zone pitch at 8 m is 130 mm; range sigma is plausibly 10–40 mm. **That is a 3–10×
resolution advantage in depth.** Every splitting mechanism should look in range or
in a ground-plane projection, never in the zone raster.

And the corner mount *helps*: the worst case is two people on the same iso-range
arc, and a corner mount makes that arc cut diagonally across the room, so most
natural formations put 10–40 cm of range between two "side by side" people.

#### Deproject to a plan-view map — the biggest structural change

**Harville**, *Stereo person tracking with adaptive plan-view statistical
templates* (IVC 2004) is named the single most important citation: explicitly an
**oblique single depth view**, 3D volume-of-interest, projected to plan-view
occupancy + height maps. *"Your problem is Harville's problem with 2268 points
instead of 300k."*

Cost is negligible — the zone grid is a regular angular grid, so the per-zone unit
vector factorises into per-row and per-column terms: **54 + 42 sin/cos pairs ≈
400 bytes**, ~10 multiply-adds per zone. (Naively storing 2268 float3 vectors
would be 27 KB — do not.)

What it buys: **height above floor** (the most discriminative cue available
without head detection), floor rejection, range-independent gates, a fixed-scale
split test, and calibration that is *(mount height, two tilt angles)* rather than
per-installation magic numbers.

**Get the extrinsics free: RANSAC a plane to the deprojected empty-room
calibration frames — the dominant plane is the floor.** Nothing on an M33, makes
install a zero-effort step, and is a small paper contribution in its own right.

Keep the raster for connected components, deproject only the labelled foreground.

#### Splitting mechanisms, ranked

1. **Range-axis bimodality in the blob.** Histogram the blob's foreground ranges,
   test with Otsu / Kittler–Illingworth, or Hartigan's dip test for unimodality.
   Split only if mode separation > k·sigma(r), both children pass the size gate,
   and the valley is deep enough. **Primary mechanism.**
2. **Plan-view mode finding**, weighting each point by r² so near and far people
   contribute equally. One person is a compact ~0.3×0.5 m blob *at every range*;
   two abreast are two modes 45–60 cm apart — a fixed-scale problem.
3. **Metric width versus expected.** One person 0.35–0.7 m; two abreast
   0.9–1.6 m. **Separates most abreast pairs without segmenting anything**, and
   tells the tracker *how many* people even when it cannot say *where* — which is
   all we need, since only the count leaves. *"Cheapest, most robust, build it
   first."*
4. **Velocity-field splitting.** At 2.5 fps a 1.2 m/s walker moves 4 zone widths
   at 8 m — closer to teleport than flow. **This is a concrete argument for the
   1 MHz bus**: at 6 fps inter-frame motion is 0.2 m and the mechanism becomes
   viable. *The merge problem, not the tracking rate, is what buys the bus
   upgrade.*
5. **Amplitude bimodality.** Clothing reflectance at 940 nm spans ~5% to ~60%, so
   two people in different clothes can be bimodal in amplitude even when depth is
   not. Free — we already read amplitude. Marked a guess; secondary cue only.
6. **Watershed — rated BELOW all of the above.** Over-segments catastrophically at
   3–13 zone widths. If used: h-minima transform first, and in plan-view space so
   h has fixed physical meaning.
7. **Track-cardinality memory** — keep two labels through a merge and re-confirm
   on split. Converts "segment a 41-zone blob" into "maintain two hypotheses for
   4 seconds". Does **not** cover two people who enter already abreast; that case
   is genuinely unrecoverable past ~5 m.

#### Latent bugs found in the section 4 sketch

These are the findings that change code rather than plans.

- **The fixed 3×3 despeckle will ERASE far people.** At 8 m a person is 3.4 zones
  wide; a 3×3 open removes them entirely. *"The most likely silent failure in the
  current design."* Make it range-adaptive, or despeckle in plan-view space.
- **The constant amplitude minimum gates out the far half of the room.** Amplitude
  falls as 1/r² and as cos of incidence angle. Make it a function of r, or gate on
  per-zone sigma.
- **The single global `(background − distance) > threshold`.** Replace with
  **per-zone `max(T_min, k·sigma_zone)`** learned at calibration. At grazing
  incidence sigma varies by an order of magnitude across the FoV, so one global
  threshold false-alarms in one region while under-detecting in another. *"The
  most important single change for a corner mount."*
- **The binary 75% reliability rule could silently delete a third of the FoV.** On
  a corner mount many zones sit at 40–70% validity and are informative. Make it a
  continuous per-zone prior.
- **`Count = CONFIRMED + DORMANT` accumulates phantoms forever.** DORMANT needs
  decay tied to exit reasoning.
- **16 calibration frames is not enough** once a per-zone sigma is needed. Use
  64–128. ~50 s once at install.
- **Greedy nearest-neighbour cannot represent "one measurement, two tracks"** —
  precisely the hard case. Replace with global assignment (Hungarian on an 8×8
  cost matrix is nothing here) plus explicit merge/split hypotheses. The
  principled version is JPDA with a merged-measurement model, or GM-PHD / labelled
  multi-Bernoulli — and there is an existing **8×8 IR occupancy LMB paper** to
  cite (Remote Sensing 13(16):3127, 2021).

#### Two ideas worth more than the rest

**A resolution cascade, which ties the algorithm to the energy contribution.**
*"You do not need 54×42 to count; you need it to split."* Run the watch and track
tiers at 4× binning (18×14 — fewer bytes, shorter sensor-active time, and **16×
the SPADs per zone**, so far better far-field SNR), and escalate to 54×42 only for
blobs that fail the metric-width gate. The reviewer calls this the strongest
structural idea in the review, and it connects the resolution axis, the energy
axis and the merge problem in one architecture.

**A three-state background — two extra detection channels for free.** The current
foreground test only fires on range *shortening*. It should fire on three events:

1. range shortening beyond k·sigma in a reliably-valid zone;
2. **invalid → valid** in a reliably-invalid zone — a person entering a zone that
   had no background return. At 9 m against a dark wall this may be the *only*
   cue;
3. **valid → invalid** in a reliably-valid zone — the person's **ToF shadow**. A
   dark jacket at 9 m may return nothing itself while still suppressing the
   background return behind it.

Marked a guess for (3), and *"if it works on your data it is a genuinely novel,
cheap, paper-worthy element."* **This turns the corner mount's biggest liability —
masses of unreliable zones — into signal.** Worth an early bench experiment: dark
jacket, 8–9 m, against a grazing wall.

#### Also flagged

- **Spatially-varying track birth.** People appear only at the FoV boundary, a
  door, or an occlusion boundary. Penalise births elsewhere — kills most phantom
  counts for free.
- **Conditional background update with a candidate model** (MOG2/PBAS style),
  committed only after N frames with no track covering the zone. Fixes the
  staleness that "never adapt under a live track" causes.
- **An area-regression fallback count** — plan-view occupancy mass / per-person
  mass. Degrades gracefully and **catches the merged-pair undercount even when
  segmentation fails**.
- **FoV clipping inside ~3 m**: the ~40° vertical FoV covers only 1.4–2 m of a
  person at 2.5 m mount height, so near blobs are truncated. Relax the upper size
  gate for any blob touching a frame boundary.
- **Sitting people are ~40% of the standing silhouette** — gate on width and
  height-above-floor, not area.
- **Corner-reflector multipath.** Two walls at ~90° next to the sensor is
  retroreflector geometry; expect phantom returns at roughly twice the true range
  along the wall junction. Handle with the 3D volume-of-interest, not threshold
  tuning.
- **Grazing incidence causes range WALK, not just noise.** One zone covers a long
  strip of wall, the pulse broadens across histogram bins, and the peak-finder
  reports a biased distance with inflated sigma. Grazing zones are both noisy
  *and* biased.
- **ViBe is out of budget** — 20 samples × 2268 zones × 2 B = 91 KB — and buys
  nothing indoors at 940 nm. Use a **single running Gaussian with a per-zone MAD
  sigma: int16 mean + int8 sigma + uint8 validity = 4 B/zone = 9 KB.**
- Total RAM for the reviewer's version: **~25 KB** of our ~60 KB.

#### A question for ST that changes the whole energy design

> **Is 450–800 mW drawn during *ranging only*, or for the whole ranging-mode
> window including the 334 ms I²C readout?** If the VCSEL can be off during
> readout, per-frame energy drops by most of an order of magnitude and the
> duty-cycle design changes completely.

#### What we cannot use

**Nearly the whole overhead depth literature** — Water Filling, field seeding,
local-depth-minima head detection — assumes a head is the closest point. At 41–50°
that is false. *State this explicitly in the paper; it is a clean justification
for a new method.*

#### Key sources

[Harville CVPR 2004](https://scispace.com/pdf/fast-integrated-person-tracking-and-activity-recognition-a5z62mqct5.pdf) ·
[Multi-Bernoulli occupancy tracking on 8x8 IR](https://doi.org/10.3390/rs13163127) ·
[Shetty et al., Grid-EYE detection and tracking](https://www.researchgate.net/publication/324725660_Detection_and_tracking_of_a_human_using_the_infrared_thermopile_array_sensor_-_Grid-EYE) ·
[ST UM2600, counting with VL53L1X](https://www.st.com/resource/en/user_manual/um2600-counting-people-with-the-vl53l1x-longdistance-ranging-timeofflight-sensor-stmicroelectronics.pdf) ·
[HW-SW optimisation for low-res IR people counting](https://arxiv.org/pdf/2402.01226) ·
[mmWave sparse point-cloud tracking](https://arxiv.org/pdf/2105.11368) ·
[RGB-D multiple human tracking survey](https://arxiv.org/pdf/1606.04450) ·
[Depth-aware ViBe for invalid pixels](https://arxiv.org/pdf/1609.09240) ·
[Incidence-angle effects on lidar signal](https://pmc.ncbi.nlm.nih.gov/articles/PMC5829180/)

### 7.2 Evaluation methodology for the paper — returned 2026-09-12

#### The finding that changes the headline experiment

**The accuracy-versus-zones curve as planned has three points, two of them almost
certainly at floor performance — and the saturation point lives in the gap
between them.**

The wide family is binning 2 / 6 / 8 → 2268 / 252 / 120 zones. Our own estimate is
**3–13 zones per person at 54×42**. So at **18×14 a person is 0.3–1.4 zones**, and
at 12×10, 0.2–0.8. **A person is sub-zone at both lower hardware points.** We would
measure a cliff between 2268 and 252 with nothing in between.

> *"This is the single biggest methodological risk in the project and it is not on
> the TODO list."*

**Therefore software binning is not a convenience — it is the only way to get data
where the answer is.**

#### The bin ladder, from one 54×42 recording

54 = 2·3³ and 42 = 2·3·7, so square bins dividing both are 1, 2, 3, 6 →
**2268, 567, 252, 63**. Rectangular bins fill the gaps → **1134, 756, 378, 189,
126**. That is **nine points across the decade where the answer lives**, from a
single recording.

Report square bins as the primary curve; rectangular ones distort blob aspect
ratio and any shape gate has to be re-derived per aspect — *a real confound, not a
formality.*

**The validation anchor is free and exact: 3×3 binning of 54×42 IS precisely
hardware 18×14** (binning 2 → binning 6).

#### Software binning is a conservative lower bound — and that is the defence

Three effects, all making software binning *pessimistic*:

1. **Sub-threshold dropout is unrecoverable.** A zone failing validity at binning 2
   contributes nothing to a software average; hardware would have summed those
   photons *before* the detection threshold. Dominant effect, cannot be simulated.
2. **Outliers.** Multipath corrupts a mean; hardware histogram merging behaves more
   like a dominant-peak pick. **Use median or signal-weighted median over valid
   zones, never the mean** — and run all three as an ablation.
3. **Exposure budget** — hold exposure or frame period constant across hardware
   points, and say which. Currently unanswered, and it is a confound on this curve.

> *"So the defensible claim is: software binning is a conservative lower bound on
> what hardware binning would achieve."* If the software curve saturates low,
> hardware saturates at or below that point. One sentence converts the weakness
> into a defence.

#### Interleave resolutions within a single recording

Blocks of 30–60 s, counterbalanced, first N frames after each switch discarded
(measure and report N). **The recording format's mid-recording config change is
practically built for this.** Separate sessions per resolution confounds resolution
with session — people behave differently. *"The highest-leverage design decision
available to you."*

#### A hole in the wide-family premise

`sensor-vl53l9cx.md` records "FoV preserved within the wide family" as
source-verified. But **12×10 at binning 8 from a 108×84 array is 13.5×10.5 —
non-integer.** Either the array is not 108×84, or 12×10 silently discards edge
rows, **in which case it is not the same field of view and the premise has a
hole.** Half an hour with retroreflective markers at the field corners settles it.
Do this before the family split is load-bearing.

#### Change the x-axis: angular pitch, not zone count

Zone count is not the physically meaningful variable — **angular pitch is**, and it
equals the bin factor (~1°/zone at binning 2). Plot against **zone footprint at the
nominal target plane in cm**, zone count secondary. Transferable to other sensors
and mount heights, makes the geometric prediction falsifiable (a 45 cm torso goes
sub-zone between 2° and 3° pitch — **pre-register that**), and partially rescues
the square family.

Then a second derived axis: **effective zone count** — zones whose footprint lands
inside the usable floor polygon. Reporting accuracy against effective rather than
nominal zones is honest and a small original methodological point.

#### Do not eyeball the knee

**Non-inferiority testing with a pre-declared margin (TOST).** Declare before
collection: *"resolution R is non-inferior if the upper bound of the 95% CI on
ΔMAE is below 0.25 persons."* Justify the margin from the downstream decision — an
error that does not change an HVAC setpoint is not an error that matters.

> **"Failing to find a difference is not equivalence."** A wide CI means
> underpowered, not saturated. *The most common fatal error in saturation claims.*

**Frames are heavily autocorrelated. Reporting n = frames is pseudo-replication and
a standard reason for rejection.** The unit of resampling is the **session**;
cluster bootstrap every confidence interval.

Parameter tuning: tune **per resolution on training folds only** — the question is
what each resolution *can* achieve. Publish the fitted parameters per resolution.

#### Metrics, in three layers, all time-weighted

Time-weighted on a common 1 Hz grid, not frame-weighted — otherwise different frame
rates across resolutions bias the comparison mechanically.

- **Instantaneous:** MAE *and* RMSE; exact-match and ±1 accuracy; the full 0–5
  confusion matrix. And **signed bias reported separately** — *"arguably more
  informative than MAE here: merging predicts negative bias, so bias-versus-zones
  directly tests the merged-blob hypothesis."*
- **Temporal — under-reported, and where this paper can differentiate.** Latency to
  correct count (median and p90). **Error persistence**: the duration distribution
  of contiguous wrong-count episodes, and the fraction of wrong-time in episodes
  over 60 s. *"A 200 ms glitch and a 20-minute stuck count have identical MAE and
  completely different consequences."* Given `room-occupancy.md`'s "the failure is
  silent — occupancy quietly drops to zero while the room is still full", **this
  metric is the project's central risk, quantified.** Plus count churn, which is
  also an energy driver since the count publishes on change.
- **Baselines:** modal count, a PIR-equivalent binary from total return energy, and
  a **63-zone equivalent**. *"If 2268 zones beat a 63-zone baseline by little, that
  is the paper"* — and it answers the reviewer's first question in advance.
- **Ceiling:** report inter-annotator MAE as the ground-truth noise floor.

#### Ground truth: script it

**A scripted cue track as primary.** Drive sessions from a timeline played to
participants and log it. Ground truth then holds *by construction* and is fully
re-scorable, because the script is a file. **This is the change that fixes the
re-scoring problem.** Keep a minority of unscripted sessions for external validity.

**Quantify observer latency** (0.3–1.5 s, non-constant variance) rather than
ignoring it: calibrate against a known cue, then apply a **guard band** excluding
±T around every labelled transition from instantaneous metrics.

**Dual-annotate 20% blind**, report agreement. Pre-declare the disagreement rule:
*the recording is authoritative for timing, the observer for identity and intent*
— whether someone sat or left is often unresolvable from a depth map at grazing
incidence. Adjudication must be **blind to algorithm output**, and say so.

**Instrument the door.** A break-beam or pressure mat gives exact, privacy-clean
entry/exit events; with a known initial count that is a near-exact occupancy
timeline with no human and no camera. *"Probably the strongest reference available
to you, and it is cheap."*

#### Scenarios — rewrite around dwell

`room-occupancy.md` already reframed this to dwell and said the scenario list
"needs rewriting"; the walking/abreast/crossing list is **the transit list**. The
accuracy axis for a dwell application is **static occupancy correctness over
time**.

Core set: empty room ≥10 h cumulative including disturbances (door, blinds, a
sunlight patch tracking across the floor, a chair moved and left); **a person
seated still for 30+ minutes at 1, 2 and 3 people** — the make-or-break scenario
for background absorption; one person leaving while another stays; **sessions at
3, 4 and 5 people** (the brief tops out at 2 — a gap); **occlusion along the line
of sight, which at a corner mount with 3–5 people in 10 m² is the common case, not
an edge case**; far-edge standing; furniture moved mid-session.

**Participants: 8–12 distinct, spanning 1.55–1.95 m, and at least two sessions
with dark matte clothing and dark hair.** *"If the participant pool is all lab
members in light shirts, a reviewer will say so."*

**Make the two-abreast experiment a curve, not a binary.** Lateral separation
0.3 / 0.5 / 0.7 / 1.0 m, ≥15 traversals each, reporting **minimum resolvable
separation versus angular pitch** against the geometric prediction. *"That tension
— counting saturates, resolving does not — is the most interesting thing in the
paper."*

Volume: ~40–60 sessions, ≥8 h occupied and ≥10 h empty. At least two rooms; if
only one, treat **tilt (41° vs 50°) as a factor** and state that cross-room
generalisation is untested.

#### Energy: the single most important practical recommendation

**Use the PPK2's digital inputs as phase markers.** Toggle a spare GPIO at each
phase boundary — integration start, INT assert, I²C start/end, processing end, TX
start/end — captured synchronously with current at 100 kSa/s. **Do not attempt to
align phases using host BLE timestamps.**

**Rail separation is a board-revision question, not a measurement question.** A
0 Ω jumper or cuttable trace per rail is all it takes. Without it, claim 1 weakens
to a phase breakdown on the system rail.

Pitfalls named: phases are **not disjoint** (use marginal attribution over an
all-idle baseline, and **report a residual** — *"an honestly reported unattributed
residual is far stronger than a breakdown that suspiciously sums to 100%"*); bulk
capacitance smears phase boundaries; series resistance in ampere-meter mode changes
VCSEL drive; report integrated charge, not peak current; DC-DC efficiency is
load-dependent; **ambient light changes sensor energy — measure dark and bright,
because if frame energy is ambient-dependent that is a figure nobody else has**;
sweep supply at fresh and end-of-life voltage.

**The bus-speed control is the cleanest figure for claim 1**: 100 / 400 / 1000 kHz
with the sensor configuration unchanged. Plus regress energy per frame against
bytes transferred across the six modes, and report measured versus theoretical bus
time with the gap accounted for.

**Battery: never quote a hero number.** Report lifetime as a curve against assumed
occupancy hours per day, then quote one point off it. Report cell chemistry and
**pulse sag** — coin cells and Li-SOCl₂ lose usable capacity under BLE TX pulses,
and that is the number-one attack on battery claims. ≥30 days measured, with the
extrapolated segment visually separated.

#### Threats a reviewer will raise that were not on our list

- **"Your curve saturates because your ALGORITHM saturates, not because the
  information does."** The obvious response to claim 2, and it needs a control:
  **run a second, structurally different detector** and show the saturation point
  is similar. *"The difference between a decent paper and a good one."*
- **Occlusion sets an accuracy ceiling independent of zone count**, and may be what
  the curve is actually measuring. Quantify it geometrically and report it as a
  ceiling.
- **Host timestamps cannot support frame-rate or latency claims** — BLE
  connection-interval quantisation. Those must come from the GPIO/PPK2 trace or an
  on-MCU timer. Publish the host-timestamp jitter distribution.

#### Framing, and the idea most likely to lift the paper

**Make the Pareto plot Figure 1** — accuracy against energy per frame or battery
months, one point per configuration, knee highlighted. *"A saturation point is only
interesting because the energy cost of the unused zones is quantified."*

**Pre-register the margin and decision rule** — a dated commit in the public repo
is enough. With it, a low saturation point is a test that returned an answer.

**And the resolution axis is also a PRIVACY axis.** At 63 zones the data is
genuinely non-identifying; at 2268 — a coarse silhouette, standing height, body
width, gait timing — it is much less clearly so. Run a **re-identification
experiment: can a classifier distinguish participants from binned frames, at each
zone count?** If re-id collapses at or near the zone count where counting
saturates, **the recommended operating point is simultaneously energy-optimal and
privacy-optimal** — a third dimension on the Pareto figure, and *"of everything in
this document, the idea most likely to lift the paper above a characterisation
study."*

**On releasing recordings:** do not claim depth maps are inherently anonymous.
*"These are low-resolution depth maps; they are not images, and they are not
anonymous either."* Release tiering: full resolution under a DUA with a
no-re-identification clause, ≤252 zones fully open, **empty-room recordings fully
open** — they carry no personal data and are exactly what someone needs to
reproduce the background-noise characterisation. Archive with a Zenodo DOI, and
include a Datasheet for Datasets.

#### The order of operations this review recommends

1. **Measure full-resolution SNR** — decides whether 54×42 is a real point at all.
2. **Bench-verify the wide-family FoV** at 54×42 / 18×14 / 12×10.
3. **Interleaved 54×42 ↔ 18×14 capture**; validate software 3×3 binning against
   hardware.
4. **Only then** pre-register the margin and collect the dataset.

*"Steps 1–3 are three bench sessions and they determine whether the headline claim
is measurable. Everything else is downstream of them."*

### 7.3 Embedded implementation — returned 2026-09-12

This reviewer read the actual linker map rather than estimating.

#### The memory premise was wrong, in our favour

From `build_1/firmware_test/zephyr/zephyr_final.map`:

| symbol | size | note |
|---|---|---|
| `.rtt_buff_data` | **32,952 B** | **dev-only.** Zero in a measurement or D3 build |
| `.bss.frame` (`app_capture.c`) | **18,272 B** | the unpacked frame — **avoidable** |
| driver raw buffer | 14,920 B | must stay |

**The 60–70 KB ceiling is a dev-build figure. The deployed build has ~95–100 KB
free.** So: *"when a choice is between smaller and clearer/more robust, take
clearer every time."*

**Detection RAM: 38.5 KB** — 18.1 KB persistent plus a 20.4 KB arena, with the
scratch and calibration buffers in **one union** (they never run concurrently).
Net system effect is **+20 KB**, because deleting the unpacked frame gives back
18.3 KB.

**Delete `struct vl53l9cx_frame` from the detector path.** Depth and amplitude are
already contiguous planes inside the driver's `raw[]`. Make the detector's input
`(const uint16_t *depth_plane, const uint16_t *amp_plane)` — zero copy, 18.3 KB
saved, and it makes the PC replay harness trivial.

**Every buffer `static`, never on the stack.** `z_main_stack` is 4,096 B; one
`uint16_t local[2268]` is 4,536 B and blows it instantly, with a hard fault whose
backtrace points somewhere unrelated. *Same class as the `MAIN_STACK_SIZE` 1024
overflow in the bring-up notes.*

#### Connected components: run-based, and 4-connectivity

**Run-based two-scan with union-find over runs.** Not because of RAM but because
per-blob accumulators come out in closed form per run — and `(c0+c1)*L` is always
even, so the centroid division is exact with no rounding.

**The union-find bound is provable, not a guess:** two run-starts in a row must be
≥2 apart, so ≤⌈54/2⌉ = 27 runs/row × 42 rows = **1,134 runs**, hard. Size for that
and the structure *cannot* overflow — 9.1 KB to delete an entire failure class.

> If you cap lower, you **must** define the overflow path: mark the frame
> `DEGRADED`, do not update tracks, do not update the background. *"A silently
> truncated label set merges unrelated blobs and the count goes wrong quietly —
> the worst failure mode this project has."*

**Change to the plan: 4-connectivity, not 8.** §4.2 currently specifies 8.
**8-connectivity merges diagonally-touching people** — at 4 m two abreast are ~6
zones each and a single corner contact merges them, dropping the count by one.
That is precisely the experiment that justifies the sensor. The usual argument for
8-conn (dropouts inside a person from dark hair) is the *despeckle's* job. Keep it
runtime-switchable — the overlap test differs by one character — and sweep both.

#### Fixed-point traps, including two that would have shipped

**The signed-subtraction trap — the most likely bug in the pipeline.**
`bg[i] - dist[i]` on two `uint16_t` works *by accident* (both promote to `int`).
The moment either becomes `uint32_t`, `0 - 3000` is 4,294,964,296, passes any
threshold, and **every excluded zone becomes foreground** — a full-frame blob that
looks exactly like a sensor fault. Write `(int32_t)bg - (int32_t)dist` explicitly.
The `bg == 0` sentinel for "excluded" then works for free, with no reliability
bitmap at runtime.

**The size gate is the wrong way round in §4.4.** `expected = k / (d*d)` collapses
under integer division — at 6 m it evaluates to 1 and the gate has evaporated.
**Multiply the measured area by d² and compare against a constant**, no division at
all.

And better: **weight per zone, not per blob** — accumulate `dd*dd` per zone during
the run scan, where `dd` is that zone's own distance in decimetres. **This solves
open question §6.6(2)** — a person straddling a steep range gradient is handled by
construction, and the blob's mean distance disappears from the gate entirely.

**The despeckle as specified destroys the far field** (agreeing with §7.1). A "3×3
majority" is ≥5 of 9 and erodes ~1 zone all round; a 3-zone-wide person at 8 m
vanishes. Use an **asymmetric open/close**: remove a foreground zone with ≤1
foreground neighbour, add one with **≥7 of 8**.

> The ≥7 threshold is deliberate: a true interior hole has 8 foreground neighbours
> and gets filled; **a 1-zone gap between two people abreast has at most 6 and is
> never bridged.** Two interpretable tunables, and it protects the abreast case
> rather than fighting it.

Also: `sum_d²` overflows u32 (2.09 × 10¹¹) — use `max − min` instead; round
centroids rather than truncating, or every centroid carries a systematic −0.5 zone
bias; never compute motion where either frame's zone was invalid; express motion as
a **fraction**, or a near blob always looks more alive than a far one. **No `float`
anywhere** — one stray float pulls in soft-float and makes host/target
bit-exactness an open question. Compile with `-Wconversion`.

**Association: gate, then nearest — do not build a weighted-sum cost.** Zones,
millimetres and areas have wildly different magnitudes, and the weights would be
three more uninterpretable tunables on top of ten. Every gate should be a physical
quantity with a unit that can be set by looking at a plot.

#### Background: trimmed mean, spread, and frozen

**Not the mean.** One frame with someone at 2 m against a 5 m background pulls it
190 mm — most of the margin against a 300 mm threshold. True median needs 72.6 KB
and does not fit. Keep `sum`, `count`, `min`, `max` per zone and use
**`(sum − min − max) / (count − 2)`**. Four lines, exact, removes the
walked-through-frame case.

**`max − min` is a strictly better exclusion criterion than validity fraction, and
it answers open question §6.6(8).** A zone valid 16/16 whose distance swings
800 mm (a grazing wall, a glass edge) is **worse** than one valid 12/16 at a stable
distance — and the 75% rule keeps the bad one and discards the good one. Report
both in the calibration summary:

> 2,268 zones: 1,932 reliable, 214 excluded for validity, 122 excluded for spread.

Those two numbers say **whether** the mount is wrong *and why* — validity failures
mean no return at all, spread failures mean grazing incidence.

**Ship v1 with the background frozen.** *"Adding a leaky integrator is adding the
exact failure mode this project exists to prevent, in exchange for a
convenience."* Instead, **re-baseline on confident emptiness**: after N consecutive
watch frames with zero foreground and no live tracks, snapshot wholesale. Handles
moved furniture in one step, can never absorb a person, and gives one observable
telemetry event.

Two traps if adaptation is ever built:

- **`bg += (d − bg) >> 8` with `bg` in u16 millimetres never converges** — any
  residual under 256 mm shifts to zero and the update is a silent no-op. *"You
  conclude adaptation works because nothing drifts."* Keep the background in **Q8**.
- **A frame-driven EWMA has a wall-clock constant that depends on the tier** —
  `shift=8` is 43 minutes at 0.1 Hz but **128 seconds at 2 fps**, so it adapts 20×
  faster precisely when someone is in the room. **Adapt only on watch-tier frames.**
- And guard on `fg_raw`, not just tracks: **a person can be foreground without
  being a track** (TENTATIVE, or too small, or the frame before a track exists), and
  gating only on tracks leaks every arrival into the background.

#### The wake condition: three tests that must all agree

1. **Spatial coherence, not zone count.** Sunlight scatters; a person is
   contiguous. Threshold on the **largest component after despeckle**, using the
   *same* range-normalised metric as the track tier — one tunable, not two.
2. **The `valid_count` test, which is what actually kills sunlight.** Direct
   sunlight raises ambient and **loses** zones; a person does not. If `valid_count`
   falls more than X% below calibration, do not wake — flag `BLINDED`. **This is
   testable offline today against any existing recording**, since `valid_count` is
   already in the `.wstof` frame header.
3. **Persistence with hysteresis.** 2-of-3 at 0.1 Hz is 20–30 s of latency —
   acceptable for dwell, sluggish for a demo. Use 2-of-3 out of long idle, **1-of-1
   if the room was occupied within 60 s**.

**On curtains:** coherent, plausible distance, good amplitude — tests 1 and 2 do
not reject them. What does: the **upper** size bound, and a **static exclusion mask
the installer paints in the web UI — 2268 bits = 284 bytes.** *"A 284-byte feature
that eliminates a whole class of false positives, far more reliably than any
algorithm."*

**Tune for ~1 false wake per hour, not zero.** One costs ~7 s of sensor time ≈
4–6 J; a handful a day is nothing against a multi-month budget. **This makes an
overnight empty-room recording the single highest-value first dataset item — and
it needs no people.**

#### The reframe for the paper: the bus is the sensor's cost, not the laser

> At 400 kHz, transfer is 334 ms against 4–16 ms of integration. **~95% of
> sensor-active time is handing over the answer, not measuring it.**

*"For high-resolution dToF as a class, the I²C interface, not the optical front
end, sets the energy floor."* A more general claim than "duty cycling saves
power", and it is already implied by numbers we have measured.

Three levers, in order:

1. **Depth-only reads.** 4,636 B ≈ 104 ms instead of 14,842 B ≈ 334 ms —
   **3.2× on the dominant cost**, no resolution change. **VERIFY:** depends on
   whether the frame read can stop early after the depth plane. *"Probably the
   highest-value single check on this list."*
2. **Watch tier at 18×14** — 39 ms vs 334 ms, **8.6×**. Combined with (1):
   **24.6×**. Two constraints: the watch tier must stay in the **wide** family
   (24×20 is square/cropped and would invalidate the background), and **calibrate
   at 18×14 separately** — sensor binning averages SPAD returns, not distances.
3. **1 MHz** — already on the critical path, 2.5× on both tiers.

**These move the standby/power-down crossover, so re-derive it afterwards.** With
depth-only 18×14 the 313 ms blob upload becomes **82%** of a watch cycle, and the
crossover shifts decisively toward keeping standby.

**And the ranking inverts once the bus is fixed:** integration is ~4% of
sensor-active time today, so tuning exposure is energetically pointless — but at
1 MHz with depth-only 18×14, transfer falls to ~5 ms and **exposure becomes
dominant.** *"Design the measurement to capture that inversion; it is a better
result than either endpoint."*

#### Testability — set this up before the first line of detection code

```
firmware_test/lib/detect/     <- NO Zephyr. C99. Integers only.
firmware_test/src/app_detect.c <- Zephyr glue: thread, k_poll, PM, BLE
host/replay.c                  <- .wstof -> detect_step() -> CSV
```

The rules that get violated:

- **`lib/detect` includes only `<stdint.h>`, `<stddef.h>`, `<string.h>`. Enforce
  with one grep in CI.** That is the entire contract.
- **Time is a parameter, never a call.** Every entry point takes `now_ms`. One
  `k_uptime_get()` inside the core and replaying last week's recording produces
  different track states than the device did.
- **All state in one caller-owned opaque struct.** *"Ten tunables means thousands
  of runs; reloading a 20 MB recording each time turns a 3-minute sweep into an
  hour."* It also allows running two configs over the same frames in one process
  and diffing track streams **frame by frame** — so you learn *which frame* a
  parameter flipped.
- **Test bit-exactness, don't assume it.** A device command that hashes the track
  state after ~8 synthetic frames, compared against the host. *"Catching this once
  costs an afternoon; discovering it after fitting ten parameters costs a week."*
- **Build a synthetic frame generator before any real recording exists** — a
  configurable blob on a known path. Makes CCL, gating, association and lifecycle
  testable as unit tests with exact expected answers.
- **Determinism:** break association ties by lowest track id, never scan order;
  track IDs from a counter in the state struct, never an address or timestamp.
- **One config struct end to end** — the GATT payload *is* the struct the core
  takes. Then a fitted parameter set writes to the device verbatim, and the fitted
  config goes into the recording header.

#### Further energy levers

- **Cap a false wake at one frame, not twenty** — take one track-resolution frame
  immediately and return to watch if it holds no person-sized blob. ~20 lines, and
  it lets the watch threshold be set far more sensitively.
- **Adaptive watch period** — back off 10 → 20 → 40 → 120 s on continued
  emptiness. Overnight in an office that is ~12× across two-thirds of the day.
- **Asymmetric tier hysteresis**: exit the track tier after 5–10 s of no *motion*,
  not "no tracks" — so **a room of seated, still people runs entirely in the watch
  tier.** That is the design's best case and it should be reachable.
- **Later: escalate to 54×42 only when two tracks come within N zones**, cutting
  the track tier ~8× while keeping resolution exactly where it earns its keep.
  *"Resolution becomes activity-dependent in the same way the rate is."*
- **What NOT to do:** optimise the detector's arithmetic. Two passes over 2268
  zones is 50–200 µs. *"The entire detection algorithm could be 100× slower than
  necessary and not register in the energy budget."*

#### New VERIFY items

- **Can the frame read stop after the depth plane?** Highest-value unknown here.
- **Power during the 313 ms blob upload** — the VCSEL is not firing, so almost
  certainly well below 450–800 mW, but unsourced. Sets the standby crossover.
- **VL53L9CX standby current** — same decision.
- 18×14 person sizes are scaled from our own table, not measured.
- Whether software 3×3 binning approximates sensor binning 6, and in which
  direction the error runs.
