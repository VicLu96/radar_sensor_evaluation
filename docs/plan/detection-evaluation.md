# Plan: three counting algorithms and a detection stream to evaluate them

Written 2026-09-13, from Victor's proposal. **Planning only — nothing implemented.**
Builds on [people-counting.md](people-counting.md) and
[counting-algorithms.md](counting-algorithms.md); references in
[../research/literature.md](../research/literature.md).

---

## 1. The proposal, and what it gets right

Victor's approach, restated:

1. **Background subtraction.** Calibrate the empty room, take the difference,
   find blobs, count blobs.
2. **Motion.** Detect what changes between frames, group it into blobs, count.
3. A third to compare against — see §2.4.
4. **A detection-stream mode** that sends the 54×42 image with the zones belonging
   to detected objects marked, for evaluating and tuning the algorithms against
   what the sensor actually saw.
5. **Then switch to count-only advertisements**, and take the paper's
   measurements there.

**The structure is right, and step 4 is the most valuable part of it.** Every
failure so far on this project was found by *looking at what the device actually
did* — the version string, the RTT log, the amplitude split. An algorithm whose
output is a single integer gives you nothing to look at when that integer is
wrong. Marking the zones that produced it does.

**And step 5 is right for a reason worth stating:** streaming dominates the energy
of a detection frame, so energy measured in the stream mode says nothing about
the deployed node. Measure on the count-only build — but see §5.2 for the rule
that makes that legitimate.

---

## 2. The algorithms

### 2.1 A1 — background subtraction, count blobs

```
calibrate:   64–128 empty frames → per-zone trimmed mean + spread   (see §5b of
                                                                     people-counting.md)
per frame:   foreground = (bg − d) > max(T_min, k·spread)
             despeckle (remove ≤1 neighbour, fill ≥7 of 8)
             connected components, 4-connectivity
             gate each blob on metric size  (Σ (p·d_i)² per zone)
             count = number of blobs that pass
```

**What it measures:** how many *things are present* that were not in the empty
room.

| strength | weakness |
|---|---|
| **sees people sitting still** — the dwell case this project exists for | **a blob is not a person**: two people touching → one blob (undercount); one person split by a dark jacket → two (overcount) |
| simplest possible baseline | **moved furniture is a permanent phantom** until recalibration |
| deterministic, cheap, easy to debug | depends entirely on a calibration that goes stale |

This is the right **baseline**. Every other algorithm has to beat it.

### 2.2 A2 — frame differencing, count moving blobs

**The problem to design around first: frame differencing cannot see a person who
is still.** It detects *change*. A person who sits down produces motion for one or
two frames and then disappears from the count entirely. For a room-occupancy
application — people who *stay* — that is not a weakness to tune, it is a
different measurement: **A2 counts moving people, not present people.**

That does not make it useless. It makes it the right answer to a different
question, and an essential component (§2.3). Two further classical problems, both
fixable, and one fix is specific to depth data:

**The double image.** At 2.5 fps a walker moves ~0.5 m — four zones at 8 m —
between frames. A plain `|d_t − d_{t−1}|` lights up **both** where they were and
where they are now, and counts one person as two. The camera-era fix is three-frame
differencing. **Depth gives a cleaner one: the sign of the difference.**

```
at a zone the person just ARRIVED at:   previous = background (far)
                                         now      = person     (near)   → d DECREASED
at a zone the person just LEFT:          previous = person     (near)
                                         now      = background (far)    → d INCREASED
```

This holds whatever direction they walk, because it compares each zone with what
*that zone* held before. **Keep decreases, discard increases, and the ghost
vanishes** — no third frame needed.

**The hollow blob.** A person who moves half a body-width overlaps their own
previous position, so their interior zones barely change and only the leading edge
lights up — a crescent that fragments into several blobs. Fix it by **accumulating
over a short window** rather than using a single difference: a Motion History
Image (Bobick & Davis 2001), one byte per zone, decaying each frame.

```
per frame:
    for each zone i, both frames valid:
        delta = (int32_t)d_prev[i] − (int32_t)d_now[i]         /* signed, explicit */
        if (delta >  T_motion[i])  mhi[i] = MHI_MAX            /* arrived: stamp   */
        else if (mhi[i] > 0)       mhi[i]--                    /* decay            */
    motion_mask = mhi > MHI_MIN
    despeckle, connected components, metric size gate
    count = blobs that pass

T_motion[i]:  from a running estimate of each zone's frame-to-frame noise, measured
              online on frames with no motion — so A2 still needs NO calibration.
invalid zones: never compute a delta where either frame was invalid; an
              invalid→valid transition is a 9,000 mm "motion" spike otherwise.
```

| strength | weakness |
|---|---|
| **needs no calibration** — works the moment it powers up | **people who stop moving vanish from the count** |
| **immune to moved furniture and stale backgrounds** | frame-rate dependent: tune it at 2.5 fps and it changes at 6 fps |
| the natural **wake condition** for the duty-cycle track tier | fast movers still fragment if the MHI window is too short |

### 2.3 A3 — fusion: motion confirms, background sustains

**This should be the third algorithm, rather than a new unrelated one.** It is the
design from the original plan, and it exists precisely because A1 and A2 fail in
opposite directions:

| | a person walks in | they sit still for 30 min | furniture is moved |
|---|---|---|---|
| **A1** background | ✓ | ✓ | ✗ phantom forever |
| **A2** motion | ✓ | ✗ vanishes | ✓ |
| **A3** fused | ✓ | ✓ | ✓ — never confirmed, because it never moved |

```
blobs    = A1's foreground blobs                        (what is present)
for each blob:
    motion evidence = overlap with A2's motion mask     (is it alive)
track lifecycle:
    TENTATIVE  → a blob appears that was not in the background
    CONFIRMED  ← it showed motion  (or persisted with a plausible person size)
    DORMANT    ← a confirmed track stopped moving, still in the foreground
    LOST       ← gone from the foreground beyond a timeout
count = CONFIRMED + DORMANT
```

**The moved chair never moves after it arrives, so it never leaves TENTATIVE and
is never counted.** A person who sits down was confirmed while walking in, and
DORMANT keeps them. That is the whole argument for fusion, and it can be shown in
one recording.

Its known failure: **someone already seated when the node powers up** has never
moved in view. They need promotion on size and persistence alone — which is
exactly where the size gate has to be trustworthy.

### 2.4 Why these three are a legitimate comparison

The expert review objected to comparing algorithms that share a front end:
*"a control that shares its failure modes with the thing it controls is not a
control."* That objection is correct — **and it does not apply here**, because
A1/A2/A3 are not being offered as independent controls. **They are an ablation**:
each component alone, then combined. An ablation is *supposed* to share everything
except the part being tested, and reviewers expect exactly that table.

Keep two more alongside it:

- **The mass-regression control** — `n = round(a·mass + b·perimeter + c)`, no
  segmentation at all. This *is* the independent control, and it is cheap. It
  answers *"does the count saturate even for an estimator that never segments?"*
- **The plan-view method (Harville) is the upgrade, not a fourth parallel
  algorithm.** Apply it to whichever of A1–A3 wins, once the raster versions are
  debuggable. Starting in the raster is deliberate: **the detection stream is a
  raster image, so raster algorithms are the ones you can see failing.**

### 2.5 Predictions, written down so they can be wrong

| | expected to fail on |
|---|---|
| A1 | merged pairs (undercount), moved furniture (permanent overcount) |
| A2 | anyone still for more than a few seconds (count decays toward zero) |
| A3 | a person seated at power-up; DORMANT timeout set too short or too long |
| mass regression | occlusion — 3–5 people in ~10 m² from a corner is the *common* case |

---

## 3. The detection stream (mode D2)

### 3.1 What it sends

The 54×42 distance image, as today, **plus a label plane** marking which zones
belong to which detection.

**Label plane: `uint16` per zone, reusing the existing plane machinery.**

```
low byte   blob id       0 = not foreground, 1..255 = which blob
high byte  flags:
  bit 0    FG_NEAR       closer than the background             (A1 channel 1)
  bit 1    FG_APPEARED   return where no stable background      (channel 2)
  bit 2    FG_SHADOW     lost a return that was reliably there  (channel 3)
  bit 3    FG_FILLED     added by the despeckle fill
  bit 4    MOTION        in A2's motion mask this frame
  bit 5    GATED_OUT     was foreground, rejected by the size gate
  bit 6    SPLIT_B       second half of a blob the split test divided
  bit 7    EXCLUDED      in the installer mask or unreliable at calibration
```

**Why per-zone flags and not just a mask.** A bit mask (284 B) says *where* the
detector fired. The flags say *why* — and when the count is wrong, why is the only
thing that helps. A zone lit as `FG_SHADOW | GATED_OUT` tells you the shadow channel
fired and the size gate threw it away, which is a completely different bug from a
zone that never fired at all. `GATED_OUT` in particular makes **rejected** detections
visible, and rejected detections are where most tuning mistakes hide.

**Cost:** distance + label = 9,072 B per 54×42 frame, **38 fragments at 240 B**.
The bus still caps it at ~2.5 fps and BLE has roughly 3× headroom, so this fits.
(A `uint8` plane would halve it but lose the flags, and the flags are the point.)

### 3.2 Alongside each frame

A **detection record** — one notification per frame, sent before the fragments:

```
u16 seq                matches the frame
u8  algorithm_shown    which algorithm the label plane shows: A1 / A2 / A3
u8  count_a1           ┐
u8  count_a2           │  ALL THREE COUNTS, EVERY FRAME, from the same frame
u8  count_a3           │
u8  count_mass         ┘
u8  n_blobs
per blob (≤16):  id, bbox, area, metric, mean_mm, motion_q8, state, split_decision
```

**Run all of them on every frame, and send every count.** CPU is not the
constraint — detection is ~150 µs against a 334 ms I²C read. Running them in
parallel means **the comparison is on the identical frame**, with no timing mismatch
and no need to repeat the scene once per algorithm. The label plane can only show
one at a time, so `algorithm_shown` is switchable live from the web interface.

### 3.3 In the web interface

- **Overlay on the heatmap:** each blob outlined and tinted by id; a toggle between
  raw distance, foreground only, and flags.
- **Flag filter:** show only `GATED_OUT`, only `SHADOW`, only `MOTION` — each
  answers a different tuning question.
- **The four counts side by side**, and a small strip chart of them over the last
  minute, so disagreement between algorithms is visible as it happens.
- **A true-count control: `−` / `+` buttons for the observer.** This matters more
  than anything else on the page — see §4.1.
- Tunables behind an "advanced" panel, applied live.

### 3.4 In recordings

Extend `.wstof` to **format version 2**: the label plane as an optional plane, the
per-frame detection record, and the observer's true count. All three algorithms'
counts on every frame means a session can be **re-scored offline against all of them
at once** — and re-run through the PC harness with different tunables, which is how
the ~10 parameters actually get fitted.

---

## 4. The evaluation loop

### 4.1 Seeing a failure is not the same as scoring one

The stream shows you **when** the algorithm fails. It does not tell you **that** it
failed unless the correct answer is in the same recording. Watching a heatmap and
thinking "that looks wrong" is not a measurement.

So **the observer's true count must be recorded frame-aligned**, from the start — the
`−`/`+` control above, written into the recording. Later sessions should move to the
scripted cue track and a door break-beam, as the methodology review recommends
(`people-counting.md` §7.2), but a manual count in the recording is enough to begin
tuning.

### 4.2 The loop

```
1. record a scenario in D2           — raw + labels + all counts + true count
2. find the frames where counts ≠ truth
3. look at those frames' labels and flags   — which channel, which gate, which split
4. change a tunable in the PC harness, replay the recording
5. repeat until it stops improving on that scenario
6. check it has not got WORSE on the others          ← the step that gets skipped
7. write the fitted parameters to the device
```

**Step 6 is why recordings matter more than live tuning.** Fixing one scenario by
eye while breaking three others is the normal outcome of tuning against a live
stream; replaying a library of recordings after every change is what prevents it.

### 4.3 The scenario library, first ten

Empty room overnight · one person walks through · one person sits for 30 min · two
walk in and sit · two walk abreast · two cross · one leaves while one stays · chair
moved and left · someone seated before power-up · dark clothing at the far edge.

Each has a different algorithm expected to fail on it (§2.5) — which is what makes
the library diagnostic rather than just large.

---

## 5. Mode D3 — count only, and measurement

### 5.1 What it is

The count in the BLE advertisement, as already planned (8 bytes: version,
instance, count, confidence, flags, report sequence, battery). No frame service in
the binary. **This is the build every energy and battery number is taken on.**

### 5.2 The rule that makes D2 evaluation valid for D3 measurements

> **The detector must be bit-identical between D2 and D3. Only the output path may
> differ.**

If D3 runs different code — a different threshold, a skipped stage, a "cheaper"
variant for power — then what was evaluated in D2 is not what was measured in D3,
and the accuracy and energy numbers in the paper describe two different systems.

How to guarantee it:

- **One `lib/detect`, compiled into both builds.** D3 simply does not call the
  label-plane serialiser.
- **The bit-exact hash test** from the harness plan, run on a D3 build: same
  synthetic frames in, same track state hash out, as D2 and the PC.
- **Tunables written into both** from the same fitted parameter file, with its hash
  in the build banner.

### 5.3 One consequence to accept

**D3 may still run all four algorithms**, and it probably should during the paper's
data collection: the advertisement carries one count, but the RTT log and a
recording host can capture all four. That keeps the accuracy comparison available
on the measurement build itself. CPU cost remains negligible; only the count in the
advertisement is chosen.

---

## 6. The mode ladder, updated

| mode | runs | sends | for |
|---|---|---|---|
| **D0 Raw** | nothing | distance frames | range tuning, demos — **exists** |
| **D1 Calibrate** | 64–128 frame background | progress + quality report | install |
| **D2 Detect stream** | A1, A2, A3, mass — all | distance + **label plane** + detection record + all counts | **evaluation and tuning** |
| **D3 Count** | the same detector, bit-identical | **count in the advertisement** | **the product, and every paper measurement** |

D0–D2 require `CONFIG_APP_BLE_FRAME_SERVICE=y`. D3 is the build with it off.

---

## 7. Build order

1. **Bench gates** — full-resolution SNR, FoV and projection check, overnight
   empty room. Unchanged from the roadmap; nothing below is safe without them.
2. **Harness** — `lib/detect` with no Zephyr, PC replay, synthetic frames,
   bit-exact hash.
3. **A1** on the corrected front end. Offline first, against the overnight
   recording and one walk-through.
4. **D2 label plane + web overlay + true-count control.** Built right after A1 so
   that everything from here on is debugged by looking at it.
5. **A2** with signed differencing and the motion history image.
6. **A3** fusion and the lifecycle.
7. **Mass-regression control.**
8. **`.wstof` v2** and the scenario library.
9. **D3** — advertisement, frame service compiled out, bit-exact hash confirmed.
10. **Plan-view upgrade** applied to the winner.

**Why D2 comes fourth, not last:** built last, it only shows the finished
algorithm; built right after A1, it debugs A2 and A3 as they are written.
