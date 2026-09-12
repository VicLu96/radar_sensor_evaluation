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

**STATUS: reviews in progress. This section is the pickup point.**
