# Two counting algorithms, deterministic, for the MCU

Written 2026-09-12, from the literature scan and expert reviews in
[people-counting.md](people-counting.md) §3 and §7.

> **DO NOT IMPLEMENT THE CODE BELOW AS WRITTEN.** A classical-CV specialist
> reviewed it and found four defects that change behaviour on the first frame,
> eleven further correctness bugs and six structural criticisms. **Read the
> Review section at the end first** — it carries the corrections, and it changes
> which algorithm to ship.

**No learned components.** Every step is closed-form, integer, and inspectable.
That is not asceticism: with no labelled data, no generalisation across mount
geometry, and a paper whose contribution is a *characterisation*, a neural net
would make the result untraceable and the method unciteable. It also has to run
in ~38 KB beside a BLE stack.

Two algorithms, chosen because they **fail differently** — which is what makes
running both worthwhile, and what §7.2 asked for as a control on the saturation
claim (*"your curve saturates because your algorithm saturates"*).

| | **A — Plan-view occupancy** | **B — Raster blobs + range split** |
|---|---|---|
| Works in | metric floor space | the zone raster |
| Needs extrinsics | **yes** (floor plane) | no |
| Scale invariance | by construction | by explicit d² weighting |
| Splits people by | spatial separation on the floor | bimodality along range |
| Cost per frame | ~2268 deprojections + a 60×60 grid | ~2268 compares + run labelling |
| RAM | ~12 KB (grid) | ~9 KB (runs + union-find) |
| Breaks when | the floor plane is wrong | two people are at the same range |
| Lineage | Harville, plan-view templates | Grid-EYE / Shetty, ST UM2600 |

---

## Common front end

Both share everything up to the blob. These are the corrected versions —
see [people-counting.md](people-counting.md) §5b for what changed and why.

### F1. Foreground, per zone, with a per-zone threshold

```c
/* THE SIGNED-SUBTRACTION TRAP. Both operands must be cast explicitly.
 * With unsigned arithmetic 0 - 3000 is 4,294,964,296, which passes any
 * threshold, and every excluded zone becomes foreground: a full-frame blob
 * that looks exactly like a sensor fault.
 *
 * bg_mm[i] == 0 is the "permanently excluded" sentinel and then works for
 * free, because 0 - d is always negative.
 */
static void foreground(struct det *s, const uint16_t *depth)
{
    for (uint16_t i = 0; i < ZONES; i++) {
        uint16_t w = depth[i];
        uint16_t d = w & 0x7FFF;
        bool  valid = (w & 0x8000) != 0;

        s->fg_raw[i] = 0;

        if (!valid) {
            /* CHANNEL 3: the ToF shadow. A dark jacket at 9 m may return
             * nothing itself while still suppressing the background return
             * behind it. Only meaningful where the background WAS reliable. */
            if (s->bg_mm[i] && s->spread_mm[i] < s->cfg.spread_stable)
                s->fg_raw[i] = FG_SHADOW;
            continue;
        }
        if (s->bg_mm[i] == 0) {
            /* CHANNEL 2: a zone with no stable background. A return here at a
             * plausible range is a detection, and at 9 m against a dark wall
             * it may be the only cue available. */
            if (d > s->cfg.min_mm && d < s->cfg.max_mm)
                s->fg_raw[i] = FG_APPEARED;
            continue;
        }

        /* CHANNEL 1: the range shortened. Threshold is PER ZONE, because at
         * grazing incidence sigma varies by an order of magnitude across the
         * field of view. */
        int32_t drop = (int32_t)s->bg_mm[i] - (int32_t)d;
        int32_t thr  = MAX(s->cfg.fg_min_mm,
                           (int32_t)s->spread_mm[i] * s->cfg.k_sigma);
        if (drop > thr)
            s->fg_raw[i] = FG_NEAR;
    }
}
```

### F2. Despeckle — asymmetric, never a majority filter

```c
/* A 3x3 MAJORITY FILTER ERASES FAR PEOPLE. A person is ~3 zones wide at 8 m;
 * requiring 5 of 9 erodes a zone all round and the blob vanishes. It eats
 * exactly the detections that are hardest to get.
 *
 * Instead, two independent thresholds:
 *   remove  a foreground zone with <= 1 foreground neighbour   (speckle)
 *   add     a background zone with >= 7 of 8 foreground        (dropouts)
 *
 * The 7 is load-bearing. An interior hole (dark hair at 940 nm) has 8
 * foreground neighbours and fills. A ONE-ZONE GAP BETWEEN TWO PEOPLE ABREAST
 * has at most 6 -- three each side -- and is NEVER bridged.
 */
static void despeckle(struct det *s)
{
    for (uint16_t i = 0; i < ZONES; i++) {
        uint8_t n = count_fg_neighbours8(s->fg_raw, i);
        s->fg[i] = s->fg_raw[i] ? (n >= s->cfg.keep_min ? s->fg_raw[i] : 0)
                                : (n >= s->cfg.fill_min ? FG_FILLED   : 0);
    }
}
```

### F3. Connected components — run-based, 4-connectivity

```c
/* 4-CONNECTIVITY, NOT 8. Eight-connectivity merges diagonally-touching
 * people: at 4 m two abreast are ~6 zones each, and one corner contact drops
 * the count by one -- destroying the very experiment that justifies a
 * 2268-zone sensor. Runtime-switchable; the overlap test differs by one term.
 *
 * Run-based because a despeckled people-foreground is a handful of compact
 * blobs, and per-blob accumulators come out in closed form per run.
 *
 * The bound is provable: two run-starts in a row must be >= 2 apart, so
 * <= ceil(54/2) = 27 runs/row * 42 rows = 1134. Size for that and the
 * structure CANNOT overflow.
 */
#define MAX_RUNS 1134

static void label_runs(struct det *s)
{
    /* pass 1: extract runs, union with overlapping runs in the row above */
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; ) {
            if (!s->fg[r * COLS + c]) { c++; continue; }
            uint8_t c0 = c;
            while (c < COLS && s->fg[r * COLS + c]) c++;
            uint8_t c1 = c - 1;

            uint16_t lbl = 0;
            for (each run p in row r-1) {
                bool touch = s->cfg.conn8 ? (c0 <= p->c1 + 1 && c1 + 1 >= p->c0)
                                          : (c0 <= p->c1     && c1     >= p->c0);
                if (!touch) continue;
                lbl = lbl ? uf_union(s, lbl, p->label) : p->label;
            }
            if (!lbl) lbl = uf_new(s);        /* provisional label */
            push_run(s, r, c0, c1, lbl);
        }
    }
    /* pass 2: resolve, accumulate per blob. Closed form per run:
     *   area   += L
     *   sum_y  += r * L
     *   sum_x  += (c0 + c1) * L / 2      <- exact: if L is odd then c0,c1
     *                                       share parity, so (c0+c1)*L is even
     *   metric += sum over the run of (d_i/100)^2      <- see M2 below
     */
}
```

### The mathematics shared by both

**M1 — metric width, small-angle.** With zone pitch `p ≈ 16.6 mrad` and mean
range `d̄`:

```
W_phys = w_zones · p · d̄
```

One person 0.25–0.85 m; a merged pair 0.9–1.8 m. **This alone separates most
abreast pairs without segmenting anything** — and since only the count leaves
the device, knowing *how many* without knowing *where* is sufficient.

**M2 — d²-weighted area, accumulated per zone.** The naive form
`expected = k/d²` collapses under integer division (at 6 m it evaluates to 1 and
the gate evaporates). Invert it, and weight **per zone** rather than per blob:

```
A_metric = Σ_i (p · d_i)²          over zones i in the blob
```

Each zone is normalised by *its own* measured range, so a person straddling a
steep range gradient — 3.0 to 6.6 m across one blob at 45° tilt — is handled by
construction. One multiply per zone, and the blob's mean range disappears from
the gate entirely.

In integers, with `dd = d_i / 100` (decimetres, 0–96):

```c
metric += dd * dd;                    /* max 2268 * 96 * 96 = 20,901,888, u32 */
```

Expected per person: `A_person ≈ 0.4–0.9 m²` unclipped, ~0.25 of that when
occluded, ~0.4 when seated.

---

## Algorithm A — plan-view occupancy

**Lineage:** Harville, *Stereo person tracking with adaptive plan-view
statistical templates* (IVC 2004) and CVPR 2004. Chosen because Harville's
problem is explicitly an **oblique single depth view** — our geometry — and
because everything becomes scale-invariant once you are on the floor plane.

### A1. Deproject

```c
/* The zone grid is a REGULAR ANGULAR GRID, so the per-zone unit vector
 * factorises into a per-row and a per-column term:
 *
 *     v(r,c) = ( sin(a_c)·cos(b_r),  sin(b_r),  cos(a_c)·cos(b_r) )
 *     a_c = (c - (COLS-1)/2)·p        b_r = (r - (ROWS-1)/2)·p
 *
 * So 54 + 42 sin/cos pairs -- about 400 bytes in Q15 -- not 2268 vectors
 * (which would be 27 KB).
 */
static void deproject(const struct det *s, uint16_t i, uint16_t d,
                      int16_t *x_cm, int16_t *y_cm, int16_t *h_cm)
{
    uint8_t r = i / COLS, c = i % COLS;

    /* sensor frame, Q15 trig, millimetres */
    int32_t X = ((int32_t)d * s->sin_a[c] >> 15) * s->cos_b[r] >> 15;
    int32_t Y =  (int32_t)d * s->sin_b[r] >> 15;
    int32_t Z = ((int32_t)d * s->cos_a[c] >> 15) * s->cos_b[r] >> 15;

    /* rotate into world by the mount matrix (tilt + yaw), then offset by
     * mount height. M is 9 Q15 constants, fitted once -- see A5. */
    int32_t wx = (M00*X + M01*Y + M02*Z) >> 15;
    int32_t wy = (M10*X + M11*Y + M12*Z) >> 15;
    int32_t wz = (M20*X + M21*Y + M22*Z) >> 15;

    *x_cm = wx / 10;
    *y_cm = wy / 10;
    *h_cm = (s->mount_h_mm - wz) / 10;    /* height ABOVE FLOOR */
}
```

### A2. Accumulate the plan view

```c
/* Two co-registered maps on a 10 cm floor grid, 6 m x 6 m = 60 x 60:
 *   occupancy  O[u][v]  -- how much "stuff" stands over this floor cell
 *   height     H[u][v]  -- the tallest thing over it
 *
 * THE r^2 WEIGHT IS THE WHOLE TRICK. The number of zones subtending a fixed
 * physical area falls as 1/d^2, so a person at 8 m lands ~41 zones while the
 * same person at 3 m lands ~290. Weighting each zone by (d/d_ref)^2 makes
 * occupancy MASS proportional to physical area, independent of range. That is
 * what turns a 16x area swing into a constant.
 *
 * THE HEIGHT GATE IS THE MOST DISCRIMINATIVE CUE WE HAVE without head
 * detection. It also deletes the floor -- which at a 45 degree corner mount
 * fills much of the field of view, and every background error on it would
 * otherwise become a candidate blob.
 */
#define GRID 60           /* 60 x 60 cells of 10 cm = 6 m x 6 m */

static void plan_view(struct det *s, const uint16_t *depth)
{
    memset(s->O, 0, sizeof s->O);
    memset(s->H, 0, sizeof s->H);

    for (uint16_t i = 0; i < ZONES; i++) {
        if (!s->fg[i]) continue;

        int16_t x, y, h;
        uint16_t d = depth[i] & 0x7FFF;
        deproject(s, i, d, &x, &y, &h);

        /* 3D VOLUME OF INTEREST. One rule replaces the unreliable-zone list,
         * the near-wall saturation region and the ceiling returns: anything
         * outside the room box, or below knee height, or above a person, is
         * not a person. */
        if (h < s->cfg.h_min_cm || h > s->cfg.h_max_cm) continue;
        int u = (x - s->cfg.x0_cm) / 10, v = (y - s->cfg.y0_cm) / 10;
        if (u < 0 || u >= GRID || v < 0 || v >= GRID) continue;

        uint32_t dd = d / 100;                 /* decimetres */
        s->O[u][v] += (uint16_t)(dd * dd / 4); /* /4 keeps it in u16 */
        if (h > s->H[u][v]) s->H[u][v] = h;
    }
}
```

### A3. Find modes — fixed physical scale

```c
/* Because the map is metric, the person kernel is a CONSTANT: ~45 cm wide, so
 * a 5x5 window at 10 cm cells. No range dependence anywhere below this line.
 *
 * Deterministic, no mean-shift iteration: box-blur then non-maximum
 * suppression with a minimum separation of one person width.
 */
static uint8_t find_modes(struct det *s)
{
    box_blur_5x5(s->O, s->Ob);                    /* separable, 2 passes */

    uint8_t n = 0;
    for (int u = 2; u < GRID-2; u++)
        for (int v = 2; v < GRID-2; v++) {
            uint16_t o = s->Ob[u][v];
            if (o < s->cfg.mass_min)          continue;   /* too little stuff */
            if (s->H[u][v] < s->cfg.h_head_cm) continue;  /* too short        */
            if (!is_local_max_5x5(s->Ob, u, v)) continue;

            /* Non-maximum suppression at one person width: if a stronger mode
             * is within 45 cm, this is the same person. */
            if (stronger_within(s, u, v, s->cfg.sep_cells)) continue;

            if (n < MAX_BLOBS) s->mode[n++] = (struct mode){ u, v, o, s->H[u][v] };
        }
    return n;
}
```

### A4. Count

```c
/* Two estimates, and they disagree in a useful way.
 *
 *   n_modes  -- how many distinct people were localised
 *   n_mass   -- total occupancy mass / mass-per-person
 *
 * n_mass is the classical area-regression count (Davies 1995). It degrades
 * GRACEFULLY where segmentation fails: two people who cannot be separated
 * still contribute two people worth of mass.
 *
 * So n_mass > n_modes is the SIGNATURE OF AN UNRESOLVED MERGE, and reporting
 * max(n_modes, round(n_mass)) catches the merged-pair undercount that
 * mode-finding alone would miss. The disagreement itself is the confidence.
 */
static uint8_t count_planview(struct det *s, uint8_t n_modes)
{
    uint32_t mass = total_mass_above_height(s);
    uint8_t  n_mass = (mass + s->cfg.mass_person/2) / s->cfg.mass_person;
    return MAX(n_modes, n_mass);
}
```

### A5. Extrinsics for free

```c
/* The floor plane is the DOMINANT PLANE in an empty room, so RANSAC it out of
 * the calibration frames and the install becomes a zero-effort step.
 *
 * Deterministic variant: iterate a FIXED pseudo-random sequence with a fixed
 * seed, so a replay on the PC reproduces the device's answer exactly.
 *
 *   repeat N times:
 *     pick 3 zones, deproject with identity extrinsics
 *     form the plane through them
 *     count inliers within t mm
 *   keep the best, then least-squares refine on its inliers
 *
 * The plane normal gives tilt and roll; the distance from origin gives mount
 * height. Calibration parameters become (height, two angles) instead of a
 * table of per-installation magic thresholds -- which is what makes the
 * method describable in a paper rather than a tuned artefact.
 */
```

### Where A fails

- **Wrong floor plane → everything wrong.** Height gating and the volume of
  interest both hang off it. Mitigate by reporting the fitted tilt/height back
  to the web interface so a bad fit is visible at install.
- **Two people in a line from the sensor** — the far one is occluded, and at a
  corner mount with 3–5 people in ~10 m² that is the *common* case, not an edge
  case. Occupancy mass partly rescues it; a full occlusion does not.
- **Sitting people** are ~40% of the standing silhouette and fail a head-height
  gate. Needs a second height class, or gate on `h_min` only.

---

## Algorithm B — raster blobs with a range split

**Lineage:** the Grid-EYE / low-resolution IR line (Shetty et al. 2017) and
ST's own UM2600, with the merge fix the literature says is the right one —
**split along depth**, because *"targets separated in physical space but merged
in the projection can be separated using the depth data."*

No extrinsics. Everything in the raster plus one 1-D test.

### B1. Gate each blob in metric units

```c
static bool plausible(const struct det *s, const struct blob *b, uint8_t *n_est)
{
    /* M1: metric width */
    uint32_t w_mm = (uint32_t)b->w_zones * s->cfg.pitch_urad * b->mean_mm / 1000000;

    /* M2: d^2-weighted area, already accumulated per zone during run labelling */
    uint32_t a_cm2 = b->metric * s->cfg.pitch_sq_num / s->cfg.pitch_sq_den;

    if (w_mm < s->cfg.w_min_mm) return false;       /* too narrow: noise      */
    if (a_cm2 < s->cfg.a_min_cm2) return false;     /* too small              */

    /* FoV CLIPPING. Inside ~3 m the vertical field covers only 1.4-2 m of a
     * person at 2.5 m mount height, so near blobs are truncated. Relax the
     * upper bound for anything touching a frame edge, or every close person
     * is rejected for being the wrong size. */
    uint32_t a_max = b->touches_edge ? s->cfg.a_max_cm2 * 2 : s->cfg.a_max_cm2;

    *n_est = 1;
    if (w_mm > s->cfg.w_pair_mm || a_cm2 > a_max)
        *n_est = 0;                                  /* candidate merge -> B2 */
    return true;
}
```

### B2. The split test — Otsu along range

```c
/* THE KEY INSIGHT, from the review: range is the HIGH-resolution axis and the
 * raster is the low-resolution one. Zone pitch at 8 m is 130 mm; range sigma
 * is 10-40 mm. That is a 3-10x advantage, so look for the split in DEPTH.
 *
 * And the corner mount helps: the worst case is two people on the same
 * iso-range arc, and a corner mount makes that arc cut diagonally across the
 * room, so most natural "side by side" formations put 10-40 cm of range
 * between the two.
 *
 * Otsu's criterion, exactly: choose the threshold t maximising the
 * BETWEEN-CLASS VARIANCE
 *
 *     sigma_b^2(t) = w0(t) * w1(t) * (mu0(t) - mu1(t))^2
 *
 * computed incrementally in one pass over the histogram. All integer.
 */
static uint8_t split_by_range(struct det *s, const struct blob *b)
{
    uint16_t hist[64] = {0};                   /* 150 mm bins, 0-9.6 m */
    for (each zone i in b) hist[(depth[i] & 0x7FFF) / 150]++;

    uint32_t total = b->area, sum = 0;
    for (int t = 0; t < 64; t++) sum += (uint32_t)t * hist[t];

    uint32_t w0 = 0, s0 = 0, best = 0; int t_best = -1;
    for (int t = 0; t < 63; t++) {
        w0 += hist[t];       if (!w0) continue;
        uint32_t w1 = total - w0; if (!w1) break;
        s0 += (uint32_t)t * hist[t];
        uint32_t m0 = s0 / w0, m1 = (sum - s0) / w1;
        uint32_t between = w0 * w1 * (m1 - m0) * (m1 - m0);
        if (between > best) { best = between; t_best = t; }
    }

    /* Three conditions, all required. Otsu ALWAYS returns a threshold -- it
     * has no notion of "this is unimodal" -- so without these it splits every
     * single person it is given. */
    if (t_best < 0)                                    return 1;
    if (separation_mm(t_best) < s->cfg.split_sep_mm)   return 1;  /* modes too close */
    if (valley_depth(hist, t_best) < s->cfg.valley_q8) return 1;  /* no real valley  */
    if (!both_children_plausible(s, b, t_best))        return 1;  /* halves wrong size */

    return 2;
}
```

### B3. Count

```c
/* Sum the per-blob estimates. Where a blob is a candidate merge that the
 * range test could not split, fall back to the area estimate -- so an
 * unsplittable pair still counts as two rather than one.
 */
static uint8_t count_raster(struct det *s)
{
    uint8_t n = 0;
    for (each blob b) {
        uint8_t est;
        if (!plausible(s, b, &est)) continue;
        if (est == 0) {
            est = split_by_range(s, b);
            if (est == 1)                                   /* split refused */
                est = (b->a_cm2 + s->cfg.a_person/2) / s->cfg.a_person;
        }
        n += est;
    }
    return n;
}
```

### Where B fails

- **Two people at the same range, side by side.** The range histogram is
  unimodal and the raster gap is sub-zone past ~5 m. Falls back to the area
  estimate, which is a count without a position.
- **One person straddling a depth discontinuity** (half in a doorway) reads as
  bimodal and over-splits. The `both_children_plausible` test is what stops it.
- **No height information**, so a tall box and a person are the same thing. A is
  strictly better here.

---

## Tracking, shared

Both algorithms emit a set of detections per frame; the lifecycle is identical.

```c
/* GATE, THEN NEAREST -- not a weighted-sum cost. Zones, millimetres and areas
 * have wildly different magnitudes, so a naive sum of squares is dominated by
 * whichever has the biggest numbers, and the weights become three more
 * uninterpretable tunables on top of ten.
 *
 * Every gate below is a physical quantity with a unit that can be set by
 * looking at a plot.
 */
reject if |dx| > gate_pos or |dy| > gate_pos
reject if |dd| > gate_range
reject if !(a1*2 >= a2 && a2*2 >= a1)          /* area ratio, no division */
among survivors: minimise dx*dx + dy*dy
tie-break on LOWEST TRACK ID                   /* deterministic across compilers */
```

Lifecycle `TENTATIVE → CONFIRMED → DORMANT → LOST`, with:

- **Motion promotes; background subtraction sustains.** A confirmed track that
  stops moving becomes DORMANT and is still counted — that is the person who
  sat down.
- **DORMANT must expire.** `count = CONFIRMED + DORMANT` with no decay
  accumulates phantoms forever.
- **Track-cardinality memory:** two tracks that merge keep both labels through
  the merge, propagated by constant velocity, and re-confirm on split. This
  turns *"segment a 41-zone blob"* into *"maintain two hypotheses for four
  seconds"*, which is tractable. It does **not** help two people who enter
  already abreast.
- **Spatially-varying birth:** people appear at the FoV boundary, a door, or an
  occlusion edge. Penalise births elsewhere and most phantom counts disappear.

---

## Cost, measured against the budget

| | A | B |
|---|---|---|
| foreground + despeckle | 2268 × ~6 ops | same |
| deprojection | 2268 × ~10 ops | — |
| plan-view accumulate | foreground zones only | — |
| blur + NMS | 3600 cells × ~10 | — |
| run labelling | — | ≤1134 runs |
| Otsu | — | 64 bins × blobs |
| **estimate** | **~150 µs** | **~80 µs** |
| **RAM** | **~12 KB** (two 60×60 maps + trig) | **~9 KB** (runs + union-find) |

Against a **334 ms** I²C read and 450–800 mW of sensor. *"The entire detection
algorithm could be 100× slower than necessary and not register in the energy
budget."* Spend the complexity budget on correctness, not on cycles.

---

## Why build both

1. **§7.2's control.** The obvious reviewer objection to the saturation claim is
   *"your curve saturates because your algorithm saturates."* Two structurally
   different detectors showing a similar saturation point turns the claim into a
   statement about the **modality** rather than about our heuristic.
2. **They fail on different inputs.** A fails when the floor fit is wrong or a
   person is occluded; B fails when two people share a range. Running both and
   reporting disagreement is a free confidence signal.
3. **B has no extrinsics**, so it works before the floor fit exists — which
   makes it the right thing to ship first and the right baseline to measure A
   against.

**Build order:** B first (no extrinsics, fewer moving parts), then A, then
compare. Both against recorded frames on the PC before either runs on the MCU.

---

## Review — classical CV specialist, 2026-09-12

> **"The geometry and the fixed-point instincts are good, but the common front
> end contains a bug of exactly the class the document congratulates itself on
> having fixed, and Algorithm A's mode finder blurs by one person-width before
> trying to resolve people one person-width apart."**

4 defects that change behaviour on the first frame, 11 further correctness bugs,
6 structural criticisms. **Nothing below is implemented — this section is the
correction list for when it is.**

### The four that bite immediately

**1. `bg_mm == 0` carries two contradictory meanings, and the wrong one wins.**
It is both *"installer painted this out / permanently excluded"* and *"no stable
background, so any return is news"*. F1 intercepts the zero case **before** the
subtraction, so the second interpretation wins: a curtain at 4 m passes
`min_mm < d < max_mm` and becomes `FG_APPEARED` **on every frame, forever**.
*"Verbatim the failure the comment at the top of F1 exists to prevent, arrived at
by a different route."* Needs two distinct sentinels.

**2. Shadow and filled zones carry a garbage range into every downstream metric.**
`FG_SHADOW` is set where `valid == 0` — those zones have **no distance**. So:
deprojection dumps mass into an arbitrary floor cell; `metric += dd*dd` with
`d ≈ 0` contributes nothing, so **the shadow channel can never produce a detection
on its own** — the dark-jacket-at-9 m case it was invented for; and every shadow
zone lands in **histogram bin 0**, manufacturing a mode at zero range with a
perfect valley, so **Otsu splits on it every time**.
Fix: impute (`d = bg_mm[i]` for shadow, neighbour median for filled) into a
`depth_used[]` plane that every downstream stage reads instead of `depth[]`.

**3. `plausible()` overflows `uint32_t`, and the failure is silent and inverted.**
`54 × 16600 × 9600 = 8,605,440,000` against `UINT32_MAX = 4,294,967,295`. The
wrapped value is 15 mm, which **fails the minimum-width test, so the blob is
discarded — and a whole-field-of-view fault reads as a clean empty room.** Fix by
reordering: compute `mm_per_zone` first, then multiply by width. `a_cm2` has the
same bug one line later.

**4. Algorithm A cannot resolve an abreast pair at any range.** `box_blur_5x5` at
10 cm cells is a **50 cm low-pass**, and two people abreast are **45–60 cm**
apart. Convolving a two-mode signal with a kernel as wide as the mode separation
is the textbook condition for the modes to merge — the Rayleigh criterion.
*"The plan-view projection was supposed to be the thing that FIXED the abreast
case; as specified it is strictly worse than the raster at short range."*

### The Otsu block, which has four separate problems

**The requested overflow check: confirmed.** `w0*w1*(m1-m0)²` reaches
`5,103,959,364` — overflows by 1.19×. Not reachable from a legitimate person
blob, *but specifically reachable from bug 2*: `Δ = 63` needs mass in bin 0 and
bin 63 simultaneously, **and bin 0 is exactly where shadow zones land**.

**A worse bug in the same three lines.** `m0` and `m1` are truncated integer bin
indices, so `m1 − m0 ∈ {1,2,3,4}` for a person blob. Between-class variance takes
a handful of distinct values across all 63 thresholds, **`best` ties constantly,
and `>` keeps the first — Otsu degenerates into "the leftmost threshold in the
first plateau".** Use the exact scaled form with no division inside the loop.

**An out-of-bounds stack write.** `hist[(depth[i] & 0x7FFF) / 150]` with
`uint16_t hist[64]`: stripping the validity bit leaves up to 32767, `/150 = 218`.
The 9.6 m ceiling is a *ranging* limit, not a guarantee about the reported field
for invalid zones. Clamp, and make it static per the project's own rule.

**And `total = b->area` breaks the loop's invariant** the moment any zone is
skipped during the histogram fill — which is exactly what fix 2 requires. Then
`m1 = 0 < m0`, the unsigned subtraction wraps to ~4e9, and a spurious split is
guaranteed. Count `total` during the fill.

### 150 mm bins destroy Algorithm B's own premise

B exists because *"range is the high-resolution axis — zone pitch at 8 m is
130 mm, range sigma is 10–40 mm"*. Then it quantises range at **150 mm, coarser
than the pitch it was meant to beat.** Expected separation between abreast people
is 10–40 cm = **0.7–2.7 bins**; you cannot find a valley between modes 2 bins
apart. Fix: bin relative to the blob's own minimum at **50 mm**, which also
widens `m1 − m0` and fixes the tie-plateau problem above.

### Kittler–Illingworth, but for a different reason than asked

Not variance — **class priors.** Zone count falls as 1/d², so a pair at 3 m and
8 m gives the near person **~7× more zones**. Otsu is biased toward the larger
class and will *"place the threshold inside the near person and split them in
half, rather than between the two people."* KI's `−2[w0 ln w0 + w1 ln w1]` term is
exactly the prior-entropy correction Otsu lacks.

### The question underneath everything

**Is the zone grid equal-angle or equal-tangent?** The deprojection assumes
directions linear in *angle*; a SPAD array behind a rectilinear lens gives
directions linear in *tangent*. Over a ±25.5° half-FoV those differ by 2–3% of the
half-angle at mid-field — **~100–150 mm of lateral error at 9.6 m with a
systematic barrel signature.** Under the wrong model **the floor is not a plane**,
RANSAC fits part of it, and the extrinsics budget is gone before you start.

*"The highest-value thing to resolve before writing A"* — and answerable in the
same bench session already scheduled for the FoV check.

### Ship A, and drop the "two algorithms as a control" framing

> *"A and B are not structurally different detectors — they share F1, F2, F3, M1
> and M2 verbatim, which is where most of the bugs in this review live. B is A
> with the deprojection deleted and the split done along a worse axis. **A control
> that shares its failure modes with the thing it controls is not a control**, and
> a reviewer sharp enough to raise 'your curve saturates because your algorithm
> saturates' is sharp enough to notice the shared front end."*

**The real control is cheaper than building B: a pure mass-regression count with
no segmentation at all.** It shares only the foreground stage. *"The count
saturates even for an estimator that never tries to segment anybody"* is a far
stronger statement than *"two segmenters saturate similarly"*.

And **B's headline failure is the common case, not the edge case.** The claim that
a corner mount puts 10–40 cm of range between abreast people holds for a pair
walking *parallel* to a wall. For a pair walking **toward the sensor corner**
their separation line is tangent to the iso-range arc and they are at **identical
range** — and a corner-mounted sensor covering a door sees that constantly.

B is still worth building **as a stepping stone on schedule grounds** — it gets a
count on the wire before the floor fit exists. Just do not build the comparison
chapter around it.

### Classical methods we are missing

1. **Run the 1-D bimodality test along the plan-view PRINCIPAL AXIS, not along
   slant range.** *"My best single idea for the project."* Range is one arbitrary
   direction that happens to be cheap; the principal axis of the blob's plan-view
   mass is the direction in which a pair is *actually* separated. Same cost — a
   2×2 scatter matrix, a closed-form eigenvector, one isqrt. **It subsumes B's
   range split, is immune to the head-to-foot gradient, works for pairs at the
   same range, and unifies A and B into one splitting module.**
2. **Distance transform + h-maxima** in plan view, replacing blur+NMS. One
   physical parameter. *"Excluding watershed from the raster is right; excluding
   it from plan view is wrong"* — in the raster `h` is millimetres on a 45°
   gradient and means nothing; in metric plan view it means exactly one thing.
3. **Ellipse / second-moment fitting as A's merge trigger** — A currently has no
   merge test at all.
4. **Granulometry / the pattern spectrum** (Matheron, Serra, Maragos 1989). Five
   openings on a 60×60 binary map, all integer. *"The classical answer to
   'count touching objects of known size' that predates everything else here"*,
   and strictly better than `mass/mass_person`.
5. **Projection profiles** — column sums weighted by d², then a valley test. OCR
   touching-character segmentation, one pass. **We currently have NO raster-space
   split at all**, despite the raster being where the gap actually is inside 7 m.
6. **Motion History Image** (Bobick & Davis) in plan view — 1 byte/cell, 3.6 kB.
   Separates two people who merged momentarily but arrived from different
   directions. Cheaper and more robust than track-level cardinality memory.
7. **Amplitude bimodality**, silently dropped from the earlier review. *"The only
   cue that works for two people at the same range and adjacent in the raster."*
   Free, runs through the identical 1-D machinery.

**And a correction to our own citation.** Davies 1995 fitted a linear model *per
scene* using **both** area and edge-pixel count, and reported linearity breaking
down under occlusion — which is the 3–5 people in 10 m² regime we target. Chan &
Vasconcelos' actual point is that a *single* feature saturates. The deterministic
descendant is a **two-feature linear fit** with coefficients from a closed-form
least squares on our own recordings:

```
n = round(a·mass + b·perimeter + c)
```

*"A fitted model, not a learned one — fully traceable, and far more defensible
than dividing by a magic `mass_person` constant. The intercept matters."*

### Not worth it

Chamfer/Gavrila template matching (no reliable contour from sparse samples);
Radon in its literal form (the principal-axis projection is the useful special
case); Hough for person centres (the occupancy map already *is* that vote);
integral images (a 61-entry rolling column sum does the job for 244 B);
codebook/MOG backgrounds (the frozen trimmed-mean + spread is better here).

### The revised build

1. The corrected common front end — two sentinels, `depth_used[]`, the overflow
   reorderings.
2. Plan-view with a head-band occupancy map and **DT + h-maxima** modes.
3. A 1-D split along the **plan-view principal axis**, 50 mm bins, d²-weighted,
   Kittler–Illingworth.
4. **The no-segmentation mass regression as the actual control.**

