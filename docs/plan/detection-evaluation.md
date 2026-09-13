# Plan: three counting algorithms and a detection stream to evaluate them

Written 2026-09-13, from Victor's proposal. **Planning only — nothing implemented.**
**Implement from [implementation.md](implementation.md)**, which consolidates this file
with the corrected front end.
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
| **A3** fused | ✓ | ✓ | ✓ *only if motion is the sole promotion rule* — see below |

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

> **These two claims conflict, and the conflict is real.** *Corrected 2026-09-13.*
> The furniture row above holds only if motion is the **only** way to be
> confirmed. But the seated-at-power-up case needs a second rule — promote a blob
> that persists at a plausible person size — and **a person-sized chair that is
> moved and left satisfies that rule too.** In the raster, a still chair and a
> still person of similar size are indistinguishable by motion and by size.
>
> So A3 must pick one: motion-only promotion (chairs rejected, a person seated at
> power-up is never counted) or size-and-persistence promotion (both counted,
> including the chair). Make it a runtime switch and measure both on the
> "chair moved and left" and "seated before power-up" scenarios.
>
> **This is the strongest argument for A4.** Height above floor separates them: a
> seated adult's head is at roughly 1.2–1.3 m, a chair back at roughly
> 0.8–1.0 m. *Estimates, not measurements* — but if they hold, the plan-view
> height band resolves a conflict that no raster algorithm can.

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
- **The plan-view method (Harville) is a second representation — A4, §2.6.**
  Superseding the earlier note that called it merely an upgrade. Starting in the
  raster is still deliberate: **the detection stream is a raster image, so raster
  algorithms are the ones you can see failing first.**

### 2.5 Predictions, written down so they can be wrong

| | expected to fail on |
|---|---|
| A1 | merged pairs (undercount), moved furniture (permanent overcount) |
| A2 | anyone still for more than a few seconds (count decays toward zero) |
| A3 | a person seated at power-up; DORMANT timeout set too short or too long |
| mass regression | occlusion — 3–5 people in ~10 m² from a corner is the *common* case |
| A4 plan view | a wrong floor fit or projection model (everything degrades together); seated people; far-field sparsity, especially when binned |

---

### 2.6 A4 — plan-view counting (Harville), evaluated 2026-09-13

**Verdict: include it — but as a second *representation*, not a fourth
detector.** That distinction is the whole value of it.

§2.4 said plan view should be "the upgrade applied to the winner". That undersold
it. A1–A3 differ in **which signal** marks a zone as foreground — static, motion,
or fused — and all of them count in the zone raster. A4 differs in **where the
counting happens**: deproject the foreground to 3D and count on the floor plane.
Those are independent axes, so the comparison becomes a 2×2 rather than a list:

| | **raster** (count in zone space) | **plan view** (count on the floor) |
|---|---|---|
| **background only** | A1 | A4a — optional, nearly free |
| **fused background + motion** | A3 | **A4** |

A2 (motion alone) stays raster-only: it is a component and a wake condition, not
a candidate counter for occupancy.

**The 2×2 is nearly free once one plan-view module exists**, because it takes a
foreground mask as input and does not care which algorithm produced it. And it
lets the paper answer something A1–A3 cannot: **does the representation matter,
independent of the detection signal?**

#### What only A4 can do

- **Height above floor.** The most discriminative person cue available without
  head detection, and the only way to tell a person from a tall box, a coat rack,
  or a chair back. Every raster algorithm is blind to this — B's own failure list
  in `counting-algorithms.md` conceded *"a tall box and a person are the same
  thing"*.
- **Floor rejection.** At a 45° corner mount the floor fills much of the field of
  view, and every background error on it is a candidate blob in the raster. A
  height band deletes all of them at once.
- **Range-independent person size.** One person is ~0.3 × 0.5 m on the floor at
  every range. The raster algorithms need per-zone d² weighting to approximate
  this; plan view has it by construction.
- **Splitting pairs at the same range.** The principal-axis split works for two
  people walking straight toward the sensor corner — the case the reviewer showed
  defeats any split along slant range, and the common case at a door.
- **Its calibration is a result.** Mount height and two tilt angles from a RANSAC
  floor fit, instead of per-installation thresholds. Describable in a paper.
- **It produces a floor map natively** — which is also how the coverage polygon,
  the 9.6 m gate and the occlusion ceiling are best presented.

#### What it costs and risks

| risk | severity | why |
|---|---|---|
| **Projection model** — equal-angle vs equal-tangent zone grid | **low — equal-angle, from ST's 1°/zone (closed 2026-09-13)** | the floor-fit test still confirms it for free. If it were wrong: up to ~130 mm lateral error at 9.6 m, and the floor would not fit as a plane |
| **Zone pitch** | **resolved** — **1° = 17.45 mrad**, ST | replaces the back-derived 16.6 mrad used until 2026-09-13; see note below |
| Far-field sparsity | medium | ~28 zones per person at 9.6 m spread over a 60×60 grid; thin, and **may collapse faster than the raster on the software-binning ladder** — genuinely unknown |
| Seated people | medium | ~40% of the standing silhouette; needs a second height class or a lower band |
| More tunables | low | cell size, height band, h for h-maxima, KI bins |
| RAM / CPU | negligible | ~12 KB, well inside the ~95–100 KB a deployed build has free |

**A4 is the only algorithm gated by a bench measurement that has not been made.**
A1–A3 work in the raster and do not care how zones map to space.

#### The projection model — CLOSED 2026-09-13: equal-angle

**Equal-angle, and the source is ST.** `docs/hardware/sensor-vl53l9cx.md:21`
records ST's figures: **54° × 42° field of view at 1° angular resolution** — 54
zones across 54° and 42 across 42°, so **every zone subtends 1°**. That is an
equal-angle statement in itself, and it is what Victor's reading of ST's
angle-based accuracy figures pointed to.

Worth recording why the *datasheet figure* closes it and the *angular accuracy*
alone would not have: nearly every sensor quotes its field of view and accuracy in
degrees, rectilinear cameras included, so "specified in angles" does not
distinguish the models. A **uniform 1° per zone** does. A rectilinear
(equal-tangent) lens would give zones roughly 20% narrower in angle at the edge of
a 54° field.

**Honest residual:** a datasheet angular resolution can be a nominal average
(54° ÷ 54 zones) rather than a per-zone guarantee. That is why the floor-fit test
below still runs both models — it costs nothing extra, and it turns a datasheet
figure into a measurement:

| position across the half field | equal-angle | equal-tangent | lateral difference at 9.6 m |
|---|---|---|---|
| ¼ | 6.75° | 7.26° | 85 mm |
| ½ | 13.50° | 14.29° | **133 mm** |
| ¾ | 20.25° | 20.91° | 111 mm |

*Computed 2026-09-13 for ST's ±27° horizontal half field, both models agreeing at
centre and edge.* The angle tables are generated from a model switch
(`EQUAL_ANGLE` default, `EQUAL_TANGENT`), so the answer changes one line either way.

#### Zone pitch — a correction that came with it

The plans have used **16.6 mrad (~0.95°/zone, ~51° × 40°)**. That number was
**back-derived by a reviewer from our own zone-size estimates**, not taken from ST.
ST's figure is **1° = 17.45 mrad**, ~5% larger, and it replaces it everywhere
from here on.

What it changes: every "zones per person" and "gap in zones" figure in the
reviews shrinks by ~5% — **not enough to move any conclusion**, including the
~5 m limit for splitting an abreast pair. What it does change is one overflow
margin: `54 × 17453 × 9600 = 9,047,635,200`, so the already-reported width
overflow in `counting-algorithms.md` is slightly worse, and its fix (compute
mm-per-zone first) is unchanged.

#### A go/no-go that costs an afternoon and no firmware

Before writing any A4 firmware, **deproject a recorded empty-room frame on the PC**
and fit the floor:

```
run under:  both EQUAL_ANGLE and EQUAL_TANGENT; keep the lower residual

go if:      RANSAC floor residual < ~50 mm RMS across the visible floor
            residual shows NO systematic growth toward the field edges
            a standing person reads within ±15 cm of true height at 3 m and 6 m

fix the projection model first if:
            residual grows toward the edges — the barrel signature of
            equal-angle assumed where the lens is equal-tangent (or vice versa)
```

This uses recordings that already exist and the `.wstof` decoder in
`recording-format.md`. It settles feasibility before a line of firmware, and the
same bench session already scheduled for the FoV check produces the marker data
that pins the model down if it fails.

#### What it adds to the paper

A result the raster algorithms cannot produce: **whether a metric representation
changes the saturation point.** Two plausible and opposite outcomes, both
publishable:

- plan view **raises accuracy at every zone count** — height gating and scale
  invariance buy real robustness; or
- plan view **collapses faster at low zone counts** — the floor map starves of
  points once zones are binned, so the representation that wins at 2268 zones
  loses at 63.

**Marked a hypothesis, not a prediction.** Which one happens is exactly the kind of
thing the software-binning ladder can answer from one set of recordings, by running
A3 and A4 side by side on every binned level.

#### Effect on the detection stream

- **The label plane still works:** each zone records the plan-view mode it
  contributed to as its blob id, so A4's detections show on the 54×42 image like
  the others.
- **The flag byte is full**, so the reason a zone was rejected by A4 — outside the
  height band, outside the volume of interest, too little mass — goes in the
  per-blob detection record as a **gate-reason code**, not in the plane.
- **Add a top-down floor view** in the web interface: the 60×60 occupancy map with
  modes marked, streamed on request only (7.2 KB). It is the only view in which A4's
  mistakes are legible — a raster image cannot show that two points are 45 cm apart
  on the floor.
- **Show the fitted mount height and tilt at calibration**, so a bad floor fit is
  visible at install rather than discovered as bad counts.

#### Where it goes in the build order

After A3, and after the go/no-go above — **never blocking A1–A3 or the detection
stream.** If the floor residual fails, A1–A3 and D2 proceed untouched while the
projection model is fixed.

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
u8  count_mass         │
u8  count_a4           ┘  (plan view, once built)
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

### 3.5 Selecting detection modes and algorithms in the web interface

*Added 2026-09-13.* §3.3 covered what the detection stream *shows*. It did not
cover how you *get into it* — there was no detection-mode selector, no calibration
panel in this plan, and no path for watching the count in D3. This is that part.

#### Two selectors, deliberately separate

The roadmap already distinguishes two axes, and the interface should too:

| selector | chooses | exists |
|---|---|---|
| **Measurement mode** | how the sensor is configured — Close · fast … Room detection, Long range | **yes** |
| **Detection mode** | what the firmware does with each frame — Raw · Calibrate · Detect stream · Count only | planned |

Keeping them separate matters: detection runs on whatever the sensor is
configured to produce, and the paper's resolution sweep changes the first while
holding the second fixed. **Selecting a detection mode other than Raw proposes the
Room detection measurement mode** (54×42, far, room exposure), and warns if the
user overrides it — detection tuned at one resolution is not valid at another.

#### The detection-mode selector

```
Detection  [ Raw ]  [ Calibrate ]  [ Detect stream ]  [ Count only ]
```

- **Raw (D0)** — today's behaviour. Nothing else changes.
- **Calibrate (D1)** opens a panel, not a mode switch:
  - a confirmation — *"The room must be empty. Calibration takes ~50 s."*;
  - progress, `n / 128` frames;
  - the quality report: *"2,268 zones: 1,932 reliable, 214 excluded for validity,
    122 for spread"*;
  - for A4, **the fitted mount height and tilt, and the floor-fit residual** — so a
    bad fit is visible at install rather than discovered as bad counts;
  - the painted exclusion mask: click zones on the heatmap to exclude a window or
    a curtain.
- **Detect stream (D2)** reveals the evaluation panel below.
- **Count only** — see "D3 in the web interface" below; it is not what it looks like.

#### The evaluation panel, visible in Detect stream

```
Algorithm shown   [ A1 ] [ A2 ] [ A3 ] [ A4 ] [ mass ]     ← which one the label plane shows
View              [ distance ] [ foreground ] [ flags ] [ floor map ]
Flag filter       [ ] NEAR  [ ] APPEARED  [ ] SHADOW  [ ] FILLED  [ ] MOTION  [ ] GATED_OUT

Counts            A1  A2  A3  A4  mass  │  TRUE  [ − ]  3  [ + ]
                  ── strip chart, last 60 s, all counts and truth ──

A3 promotion      ( ) motion only    ( ) motion or size + persistence     ← §2.3 conflict
Advanced          tunables, applied live, with the fitted-parameter hash shown
```

**`Algorithm shown` changes only the label plane.** All algorithms keep running
and all counts keep arriving — switching what you look at never changes what is
measured, and a recording made while flicking between them still carries every
count on every frame.

#### Where the settings live on the wire

**The 16-byte config characteristic has one reserved byte left** (byte 13, after
range mode took the rest on 2026-09-12). Detection does not fit there and should not
be squeezed in.

Use the **Detection Config characteristic `53f93003`**, already frozen in
`ble_uuid.h` for exactly this: detection mode, algorithm shown, view, A3 promotion
rule, and the tunables. It gets its own protocol version, it is written
transactionally like the config (validate everything, then apply), and it keeps the
detection surface independent of the measurement surface — the same separation as
the two selectors.

#### Image 2 in the web interface

*Revised 2026-09-13:* the count-only **preview** proposed here is dropped. Victor
decided evaluation and measurement are separate images (§5), so **the web interface
never switches into counting mode** — image 2 is flashed, not selected.

What the interface does when image 2 is on the board:

- **Detects it automatically** from the missing frame service, as today's banner
  already does, and hides the stream, evaluation and recording panels.
- **Keeps setup available:** connection, configuration, health, and the Calibrate
  panel — installation still needs them.
- **Says, prominently, to disconnect before measuring.** A held BLE connection
  replaces advertising events with connection events and changes the radio's
  energy, so a measurement taken while connected is not a measurement of the
  deployed node.
- **Shows the count from the advertisement scanner bridge**
  (`ble-streaming-and-web-ui.md` §8.1) over a WebSocket, not over the connection.
  Chrome cannot scan advertisements without an experimental flag, which is why the
  bridge exists — **for image 2 it is required, not optional.** nRF Connect on a
  phone reads the manufacturer data for spot checks.

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

## 5. Two firmware images, in sequence — decided 2026-09-13

**Victor's decision:** evaluation and measurement are **separate steps with separate
images**. First an *extended* image, connected to the web interface, streaming
blob images and counts until the algorithm is evaluated. Then a *new* image,
generated from the result, energy-optimised, that only advertises the number of
people detected.

This supersedes the "count-only preview" in §3.5: **there is no runtime switch
between the two, and none is needed.** The separation is by build, which is also
the only separation the privacy claim can rest on.

```
   IMAGE 1 — EVALUATION                      IMAGE 2 — DEPLOYED / MEASUREMENT
   ────────────────────                      ─────────────────────────────────
   web interface connected                   no connection during operation
   D0 raw · D1 calibrate · D2 detect stream  count in the advertisement only
   all algorithms, every frame               the ONE chosen algorithm
   label plane, blob images, all counts      no frame service in the binary
   true-count control, recordings            energy-optimised
   tunables changed live                     fitted parameters frozen
          │                                          ▲
          └──── evaluation gate (§5.3) ──── freeze ──┘
```

### 5.1 Image 1 — evaluation

Everything in §3: the three detection modes, every algorithm on every frame, the
label plane, the web evaluation panel, recordings with the true count, and live
tunables.

**Nothing in it is optimised for energy, and nothing measured on it is an energy
number.** It streams 9 KB per frame, holds a BLE connection, logs over RTT and keeps
the sensor ranging freely. Its job is to find out *which* algorithm and *which*
parameters — not what they cost.

### 5.2 Image 2 — deployed and measurement

Built from the evaluation result. What changes, and why each is an energy
decision:

| change | why |
|---|---|
| **frame service compiled out** (`APP_BLE_FRAME_SERVICE=n`) | no frames can leave — the privacy claim, as a property of the binary |
| **only the chosen algorithm linked** | the comparison is already done (see below); keeps the image minimal |
| **count in a non-connectable advertisement**, 1–10 s interval, plus a short connectable window for reconfiguration | connectionless is the design; a held connection changes the radio's energy |
| **duty cycling on**: watch tier (18×14, depth-only) and track tier on activity, adaptive back-off when empty | where the energy actually goes — sensor-active time, ~95% of it I²C transfer |
| **RTT and logging off** | immediate-mode logging costs CPU and blocks; needs a J-Link attached anyway |
| **bring-up switches off**: `APP_BLE_FIRST`, `VL53L9CX_DEFER_BOOT`, `APP_BLE_AUTOSTREAM`, `VL53L9CX_EXPOSURE_BACKOFF`, `APP_LOG_FULL_GRID` | the node must range unattended, and the backoff silently changes the variable being measured |
| **TX power chosen by measurement**, not left at the +8 dBm set for bring-up | +8 dBm roughly triples transmit current against 0 dBm |
| **IMU off** unless the paper uses it | it has been silent since 2026-09-10 and costs current |

**What stays:** the config and telemetry services, and Calibration Control — so a
brief connection can still calibrate and configure the node at install. **Connect
for setup, disconnect before any measurement.**

**Removing the other algorithms is not an energy optimisation**, and should not be
described as one. Detection is ~150 µs against a 334 ms I²C read; all four together
would not register. It is done to keep the measured image minimal and unambiguous.

### 5.3 The evaluation gate — what "evaluated" means

Image 2 is only generated when these hold. Written down now so the gate is a
decision rather than a feeling:

1. **A scenario library exists** (§4.3) and every scenario has been replayed through
   every algorithm on the PC harness.
2. **One algorithm is chosen**, against a criterion stated before looking at the
   results — for example, lowest time-weighted count MAE across the library, with no
   scenario worse than the A1 baseline.
3. **Its parameters are fitted and frozen** in one versioned parameter file, with a
   hash.
4. **The chosen algorithm's weak scenarios are written down**, from §2.5 and from
   what the stream showed. They become the paper's stated limitations rather than
   surprises in review.

### 5.4 The rule that makes image-1 evaluation valid for image-2 measurement

> **The chosen algorithm must be bit-identical between the two images. Only what
> surrounds it may differ.**

If image 2 runs different detection code — a different threshold, a skipped stage,
a "cheaper" variant for power — then what was evaluated is not what was measured,
and the paper's accuracy and energy numbers describe two different systems.

How to guarantee it:

- **One `lib/detect`, compiled into both images**, from the same commit. Image 2
  links only the chosen algorithm's entry point; its code is unchanged.
- **The frozen parameter file compiled into both**, with its hash printed in image
  1's banner and carried in image 2's advertisement flags or its telemetry.
- **The bit-exact hash test** — same synthetic frames in, same track-state hash out —
  run on image 1, image 2 and the PC. All three must agree.
- **Tag both images** from the same commit: `eval-<date>` and `deployed-<date>`,
  with the parameter hash in both tag messages.

### 5.5 Where the algorithm comparison lives now

§5.3 of an earlier draft suggested the measurement build keep running all four
algorithms so the comparison stayed available on it. **Superseded.** The
comparison lives in **image 1's recordings, replayed offline** — the PC harness
reproduces every algorithm's count on every recorded frame, bit-exactly. Image 2
does not need to run them, and a measurement image with RTT logging on to capture
four counts would not be an energy measurement.

**One check does have to happen on image 2 itself:** a short validation session
confirming its **advertised** count matches ground truth, observed through the
advertisement scanner bridge. That confirms the frozen algorithm survived the
rebuild — the hash test says the code is identical; this says the system around it
did not break it.

### 5.6 How the two images are built

**Two `prj` files, not snippets.** Snippets failed to apply silently in the nRF
Connect extension on 2026-09-10 and cost two bench runs. Zephyr's `FILE_SUFFIX`
selects between them, and the extension shows each as its own build configuration:

```
firmware_test/prj.conf            image 1 — evaluation (today's file)
firmware_test/prj_deployed.conf   image 2 — deployed / measurement
```

```bash
west build -b water_sense_board/nrf54l15/cpuapp firmware_test -- -DFILE_SUFFIX=deployed
```

**The boot banner prints which image it is**, and the web interface already detects
a missing frame service. Between the two, **"which image is on the board" can never
be a question** — the lesson of 2026-09-11, when an evening was lost testing the
wrong commit.

---

## 6. The mode ladder, updated

| image | mode | runs | sends | for |
|---|---|---|---|---|
| 1 | **D0 Raw** | nothing | distance frames | range tuning, demos — **exists** |
| 1 | **D1 Calibrate** | 64–128 frame background | progress + quality report | install, evaluation |
| 1 | **D2 Detect stream** | A1, A2, A3, A4, mass — all | distance + **label plane** + detection record + all counts; floor map on request | **evaluation and tuning** |
| 2 | **D3 Count** | the chosen algorithm, bit-identical, duty-cycled | **count in the advertisement** | **the product, and every paper measurement** |

Image 1 is `CONFIG_APP_BLE_FRAME_SERVICE=y`. Image 2 is the build with it off.

---

## 7. Build order

> **SUPERSEDED 2026-09-13 — this order is carried into implementation.md §6 as work packages with dependencies and done-when criteria.** The ordered plan and the corrected
> specification are in [implementation.md](implementation.md). This section is kept as
> the record of the reasoning.

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
9. **A4 go/no-go** — deproject a recorded empty room on the PC, fit the floor,
   check the residual. An afternoon, no firmware.
10. **A4 plan view** (and A4a nearly free), plus the top-down floor view in the web
    interface — only if 9 passes. Never blocks anything above it.

**— evaluation gate, §5.3: algorithm chosen, parameters frozen and hashed —**

12. **`prj_deployed.conf`** — image 2's profile, banner naming the image.
13. **Duty cycling** — watch tier at 18×14 depth-only, track tier on activity,
    three-test wake condition, adaptive back-off. Only in image 2; its tuning needs
    the overnight and scenario recordings from image 1.
14. **Count advertisement** — non-connectable, plus a bounded connectable window
    for calibration and configuration.
15. **Advertisement scanner bridge** — required, not optional, since energy runs
    must not hold a connection.
16. **Image 2 verification** — bit-exact hash on image 1, image 2 and the PC; a short
    session confirming the advertised count against ground truth.
17. **Tag** `eval-<date>` and `deployed-<date>` from the same commit, parameter hash
    in both. Then, and only then, the energy measurements.

Steps 1–11 are **image 1**; steps 12–17 are **image 2**.

**Why D2 comes fourth, not last:** built last, it only shows the finished
algorithm; built right after A1, it debugs A2 and A3 as they are written.
