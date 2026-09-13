# Implementation plan

**Rewritten 2026-09-13.** The single, ordered plan from today's state to the paper's
measurements. The 2026-08-31 version (ISP2454-LL, several boards, a running-median
background, line-crossing counting) is in git history and is superseded.

**This file decides the order and the specification. The other plan files hold the
reasoning.** Where a build order or a code sketch elsewhere disagrees with this file,
this file wins — see §9 for the list of what it supersedes.

How to pick up work: take the **lowest-numbered work package whose dependencies are
met**. Bench items (`B`) are Victor's; everything else is software. Several software
packages need no bench result at all and can start today (marked *can start now* in §6).

---

## 1. Where the project is — 2026-09-13

| Stage | State | Evidence |
|---|---|---|
| **1. Driver** | **done** — ranges at 54×42, 2268 zones | tag `working-54x42-2026-09-11` |
| **2. Telemetry** | **done** — BLE staged flow (advertise → connect → boot sensor → stream), live heatmap, config round-trip | tag `demo-2026-09-11` |
| 2+. Demo features | **done, untested in detail** — five measurement modes, SHORT/LONG ranging context, editable settings, recording to CSV and `.wstof` v1 | commits after the demo tag |
| **3. Detection** | **planned, not started** — this file | tag `planning-milestone-2026-09-11` and the 2026-09-12/13 plan commits |
| **4. Power / paper** | **not started** | — |

**Measured:** full frame 14,842 B, ~334 ms at 400 kHz, ~2.5 fps; firmware blob
313 ms; amplitude 126 / ambient 1 at 24×20 (all Victor's bench, 2026-09-11).
**Not measured:** full-resolution SNR, the range presets, every energy number.

**Hardware facts an implementer trips on** (details in `DECISIONS.md`):
- The HFXO/LFXO **internal load capacitors in the application overlay are what make
  the radio transmit.** Never remove them. (2026-09-11)
- AP_CLK is a 12 MHz external crystal; the GRTC clock output is deleted.
- I²C is 400 kHz on 4.7 kΩ — out of spec even at 400 kHz. 1 MHz waits on 1 kΩ (B5).
- IMU LSM6DSV16BX at 0x6B has been silent since 2026-09-10.
- **Check the version string on the board before testing anything.** An evening was
  lost on 2026-09-11 testing the wrong commit.

---

## 2. The shape: two firmware images, in sequence

Decided 2026-09-13 (Victor). Full reasoning in
[detection-evaluation.md](detection-evaluation.md) §5.

```
   IMAGE 1 — EVALUATION  (prj.conf)          IMAGE 2 — DEPLOYED  (prj_deployed.conf)
   ────────────────────────────────          ───────────────────────────────────────
   web interface connected                   no connection during operation
   D0 raw · D1 calibrate · D2 detect stream  D3: count in the advertisement only
   A1, A2, A3, A4, mass — all, every frame   the ONE chosen algorithm
   label plane, detection record, counts     frame service NOT in the binary
   true count, recordings, live tunables     duty-cycled, parameters frozen
   nothing measured here is an energy number every energy and paper number
          │                                          ▲
          └──── evaluation gate (WP15) ──── freeze ──┘
```

| image | mode | runs | sends |
|---|---|---|---|
| 1 | **D0 Raw** | nothing | distance frames — **exists** |
| 1 | **D1 Calibrate** | 64–128-frame background | progress + quality report (+ floor fit once A4 exists) |
| 1 | **D2 Detect stream** | A1, A2, A3, A4, mass | distance + label plane + detection record; floor map on request |
| 2 | **D3 Count** | chosen algorithm, bit-identical | 8-byte count advertisement |

**Two rules that make the separation valid:**

1. **The chosen algorithm is bit-identical in both images.** One `lib/detect`, one
   frozen parameter file with a hash, one hash test run on the PC, image 1 and image 2.
   Only what surrounds the algorithm may differ.
2. **There is no runtime switch between the images.** The privacy claim ("frames never
   leave") is a property of the image-2 binary, checked by `.config` grep and `nm` on the
   ELF — never `strings | grep <uuid>`, which cannot match `BT_UUID_128_ENCODE` output.

---

## 3. Decisions already settled — do not reopen while implementing

| decision | source |
|---|---|
| A1/A2/A3 are an **ablation**, not independent controls; the **mass regression** is the independent control | detection-evaluation §2.4 |
| A4 is a second **representation** (plan view), forming a 2×2 with A1/A3; A4a is A4 on A1's mask | detection-evaluation §2.6 |
| Zone grid is **equal-angle**, 1° per zone, pitch **17.45 mrad**, FoV 54°×42° (ST) — the 16.6 mrad figure is withdrawn; the floor fit still tests both models once | detection-evaluation §2.6, 2026-09-13 |
| Background is **frozen**; re-baseline only on confident emptiness; no leaky integrator in v1 | people-counting §7.3 |
| Despeckle is **asymmetric** (remove ≤1, fill ≥7 of 8), never a majority filter | people-counting §5b |
| Connected components are **run-based, two-scan, 4-connectivity** (8 runtime-switchable) | people-counting §7.3 |
| The 1-D split runs along the **plan-view principal axis**, 50 mm bins, d²-weighted, **Kittler–Illingworth**, not Otsu along slant range | counting-algorithms, Review |
| Plan-view modes by **distance transform + h-maxima**, no 5×5 blur | counting-algorithms, Review |
| Label plane is **`uint16` per zone**: low byte blob id, high byte why-flags | detection-evaluation §3.1 |
| Detection settings live on **Detection Config `53f93003`**, not the 16-byte config (one reserved byte left) | detection-evaluation §3.5 |
| The **observer's true count is recorded frame-aligned** from the first detection recording | detection-evaluation §4.1 |
| Two images selected by **`FILE_SUFFIX`**, not snippets (snippets failed silently in the extension on 2026-09-10) | detection-evaluation §5.6 |
| Removing the unused algorithms from image 2 is **not** an energy optimisation and is not described as one | detection-evaluation §5.2 |

---

## 4. Specification — the corrected detection pipeline

This is the consolidated, corrected version. **Do not implement the code sketches in
`people-counting.md` §4 or `counting-algorithms.md`** — they carry the bugs the reviews
found. The rationale for every line below is in those files' review sections.

### 4.1 Contract of `lib/detect`

- C99, **integers only**, includes only `<stdint.h>`, `<stddef.h>`, `<string.h>`.
  Enforced by one grep in CI.
- **Time is a parameter**: every entry point takes `now_ms`. No clock calls inside.
- **All state in one caller-owned struct**; no globals, no statics with state.
- **One config struct end to end**: the Detection Config payload *is* the struct the
  core takes, and the recording header stores it verbatim.
- **Determinism**: association ties break by lowest track id; track ids come from a
  counter in the state struct; never scan order, addresses or timestamps.
- Input is the **raw planes**, not `struct vl53l9cx_frame`: depth `uint16` per zone
  (bit 15 = valid, bits 14..0 = mm), amplitude `uint16` per zone, width, height.

```c
void detect_init(struct det_state *s, const struct det_config *c);
void detect_calibrate_add(struct det_state *s, const uint16_t *depth,
                          const uint16_t *amp, uint32_t now_ms);
int  detect_calibrate_finish(struct det_state *s, struct det_cal_report *r);
void detect_step(struct det_state *s, const uint16_t *depth, const uint16_t *amp,
                 uint32_t now_ms, struct det_output *out);   /* all algorithms */
uint32_t detect_state_hash(const struct det_state *s);        /* bit-exact test */
```

### 4.2 Calibration (D1)

- **64–128 frames** of an empty room, started by a command (only a human knows the
  room is empty).
- Per zone keep `sum`, `count`, `min`, `max` of valid distances.
  `bg_mm = (sum − min − max) / (count − 2)` — trimmed mean, exact, four lines.
  `spread = max − min`.
- **Two distinct sentinels** (review defect 1 — never overload `bg_mm == 0`):

  | zone state | meaning | foreground rule |
  |---|---|---|
  | `BG_OK` | stable background | NEAR / SHADOW tests |
  | `BG_NONE` | too few valid returns at calibration | a valid return in range is APPEARED |
  | `BG_EXCLUDED` | installer mask, or spread above limit | **never foreground** |

- Quality report, all three numbers: *"2,268 zones: N reliable, M excluded for
  validity, K excluded for spread."*
- **Installer exclusion mask**: 2268 bits = 284 B, painted on the heatmap.
- **Persistence (v1):** RAM only — recalibrate after a power cycle. Storing it in RRAM
  with temperature and a timestamp is the preferred later step (people-counting §2);
  it is not on the critical path.

### 4.3 Front end, shared by every algorithm

**F1 — foreground, per zone.** All subtractions in `int32_t` with explicit casts on
both operands.

```
thr[i] = max(T_min, k · s[i])          s[i] from the calibration spread
NEAR     : BG_OK,  valid, amplitude ≥ A_min(d),  (int32)bg − (int32)d > thr[i]
APPEARED : BG_NONE, valid, d_min < d < d_max, amplitude ≥ A_min(d)
SHADOW   : BG_OK,  invalid now, zone was reliably valid at calibration
A_min(d) : a function of range (amplitude falls as 1/r²·cos θ), not a constant
```

**`depth_used[]`** (review defect 2): every stage after F1 reads this plane, never
`depth[]`. SHADOW zones take `bg_mm[i]`; FILLED zones take the median of their valid
foreground neighbours. Without it shadow zones carry range 0 into the metric, the
histograms and the deprojection.

**F2 — despeckle:** remove a foreground zone with ≤1 of 8 foreground neighbours; fill a
background zone with ≥7 of 8 (flag it `FILLED`).

**F3 — connected components:** run-based two-scan with union-find, 4-connectivity by
default, `MAX_RUNS 1134` (worst case: alternating zones on every row of 54).

**Metric size gate:** per blob `metric = Σ (p · d_i)²` over its zones, `p = 17.45 mrad`,
using `depth_used`. **Compute `mm_per_zone = p·d` first, then square, in `uint64_t`**
(review defect 3: `54 × 17450 × 9600 = 9.05e9` overflows `uint32_t` and silently turns a
full-field fault into an empty room). Gate with a **lower and an upper** bound — the
upper bound is what rejects curtains.

### 4.4 A1 — background subtraction

`count_a1` = blobs passing the metric gate. The baseline every other algorithm must beat.

### 4.5 A2 — motion

```
for each zone i, BOTH frames valid:
    delta = (int32_t)d_prev[i] − (int32_t)d_now[i]      /* signed: arrivals only */
    if (delta > T_motion[i])  mhi[i] = MHI_MAX
    else if (mhi[i] > 0)      mhi[i]--
motion_mask = mhi > MHI_MIN   → F2 → F3 → metric gate → count_a2
T_motion[i] : running per-zone frame-to-frame noise, estimated online on still frames
never compute delta across an invalid → valid transition
```

No calibration needed. Keeping only decreases removes the double image; the MHI fills
the hollow blob. **Frame-rate dependent:** `MHI_MAX` is stored in frames, so it must be
retuned when the bus goes to 1 MHz.

### 4.6 A3 — fusion and the track lifecycle

```
blobs = A1 blobs; motion evidence = overlap with A2's motion_mask
association: gate, then nearest; merge/split aware; ties by lowest track id
TENTATIVE  new blob not in the background
CONFIRMED  showed motion                           ── rule M (motion only)
           OR persisted at plausible size ≥ N s    ── rule M+S (runtime switch)
DORMANT    confirmed, stopped moving, still foreground
LOST       gone from foreground beyond lost_timeout
count_a3 = CONFIRMED + DORMANT
```

**The promotion conflict is left open on purpose:** rule M rejects a moved chair but
never counts someone seated before power-up; rule M+S counts both, chair included.
Implement both behind the switch and measure them on those two scenarios.

### 4.7 Mass-regression control

```
mass      = Σ over foreground zones of (p · d_i)²       (same metric as the gate)
perimeter = number of 4-neighbour foreground/background edges
count_mass = round(a · mass + b · perimeter + c)
```

No segmentation. `a, b, c` come from a closed-form least-squares fit on recordings with
true counts, on the PC; the device only evaluates the formula.

### 4.8 A4 — plan view (only after the WP12 go/no-go)

- **Deproject** each foreground zone with `depth_used` under the equal-angle model: zone
  centre angles at 1° steps about the optical axis. The exact parametrisation
  (azimuth/elevation versus polar) is settled in WP12 with the equal-tangent model as
  the comparison.
- **Extrinsics from calibration:** RANSAC plane fit to the empty-room points → mount
  height and two tilt angles, plus the residual. These are shown in the calibration
  panel.
- **Height band** over the floor (head/torso band; a second, lower class for seated
  people) → occupancy grid, ~60×60 cells, cell size tunable.
- **Modes:** distance transform + h-maxima (one physical parameter `h`).
- **Split test:** 1-D histogram of the blob's plan-view mass along its **principal axis**
  (2×2 scatter matrix, closed-form eigenvector, `isqrt`), 50 mm bins relative to the
  blob's own minimum, d²-weighted, Kittler–Illingworth threshold. `total` is counted
  during the fill; bin index clamped; histogram `static`.
- `count_a4` = modes passing mass/height tests. **A4a** is the same module fed A1's mask.
- A zone's label is the plan-view mode it contributed to; the reason a zone was rejected
  (height band, volume of interest, mass) is a **gate-reason code in the detection
  record**, because the flag byte is full.

### 4.9 Duty cycling — image 2 only, but written in `lib/detect`

So that it can be replayed against image-1 recordings.

- **Watch tier:** 18×14, depth-only, 0.05–0.2 Hz, **its own calibration** (sensor binning
  averages SPAD returns, not distances). Must stay in the wide family — 24×20 is cropped.
- **Wake = three tests that all agree:** largest component after despeckle above the
  same metric threshold; `valid_count` not more than X% below calibration (else flag
  `BLINDED` — this is what rejects sunlight); persistence 2-of-3 from long idle, 1-of-1
  if occupied within 60 s.
- **Track tier** at full rate while something moves; **exit after 5–10 s without motion**,
  not without tracks — a room of still, seated people runs entirely in the watch tier.
- **A false wake costs one frame:** take one track-resolution frame and return if no
  person-sized blob.
- **Adaptive watch back-off** 10 → 20 → 40 → 120 s on continued emptiness.
- **Re-baseline on confident emptiness:** N consecutive watch frames with no raw
  foreground and no tracks → snapshot the background. Guard on raw foreground, not on
  tracks alone.
- Target ~1 false wake per hour, not zero.

---

## 5. Interfaces

All UUIDs are already reserved in `firmware_test/src/ble/ble_uuid.h`
(base `53f9XXXX-1e2d-11ef-9262-0242ac120002`). Layouts below are **proposed** — finalise
them in the work package, then freeze them in `ble_uuid.h` / `protocol.ts` together.

### 5.1 Counting service `53f93001`

| characteristic | direction | content |
|---|---|---|
| **Count `3002`** | notify | the **detection record**, one per frame, sent before that frame's fragments |
| **Detection Config `3003`** | read / write | `det_config`, versioned, validated whole then applied (like the config) |
| **Calibration `3004`** | write + notify | commands: start(N frames), abort, mask chunk (offset + bytes); notify: progress `n/N`, then the quality report (+ floor fit h, tilt, roll, residual) |
| **Background model `3005`** | read, fragmented | `bg_mm` and spread planes for the web calibration view — **lowest priority** |

**Detection record — 10 B header + 14 B per blob, ≤16 blobs = 234 B, fits one 244 B
notification:**

```
header   u8 version  u16 seq  u8 algorithm_shown
         u8 count_a1  u8 count_a2  u8 count_a3  u8 count_a4  u8 count_mass
         u8 n_blobs
per blob u8 id  u8 state  u8 x0 y0 x1 y1  u16 area_zones  u16 metric_dm2
         u16 mean_mm  u8 motion_q8  u8 split_or_gate_reason
```

**Detection Config, leading bytes:**

```
u8 version  u8 detection_mode (0 raw, 1 calibrate, 2 detect stream)
u8 algorithm_shown (0 A1, 1 A2, 2 A3, 3 A4, 4 A4a, 5 mass)
u8 flags    bit0 floor map requested   bit1 A3 rule M+S   bit2 8-connectivity
...         tunables = struct det_config verbatim, then u32 parameter hash
```

`algorithm_shown` changes **only** which labels the plane carries. Every algorithm keeps
running and every count keeps arriving.

### 5.2 Frame service additions

- **Label plane**: a new `PLANE` bit, `uint16` per zone, through the existing plane and
  fragmentation machinery. Distance + label = 9,072 B, 38 fragments at 54×42.

  ```
  low byte   blob id (0 = none)
  high byte  bit0 NEAR  bit1 APPEARED  bit2 SHADOW  bit3 FILLED
             bit4 MOTION  bit5 GATED_OUT  bit6 SPLIT_B  bit7 EXCLUDED
  ```

- **Floor map** (A4): 60×60 `uint16`, 7.2 KB, sent only while requested.

### 5.3 Recording format `.wstof` v2

Extends v1 ([recording-format.md](recording-format.md)); v1 files stay readable.
Header adds the `det_config` and its hash. Each frame optionally adds the label plane,
the detection record, and **`u8 true_count`** from the observer's −/+ control.

### 5.4 Image-2 advertisement

The 8-byte manufacturer payload in
[ble-streaming-and-web-ui.md](ble-streaming-and-web-ui.md) §4.1 (company `0xFFFF`:
version, instance, count, confidence, flags, `u16 report_seq`, battery). The 128-bit UUID
goes in the scan response. The **parameter hash** is reported in the health
characteristic, not the advertisement.

---

## 6. Work packages, in order

Each: **goal · files · depends on · done when.**

### Phase 0 — bench gates (Victor)

| # | what | gates | done when |
|---|---|---|---|
| **B1** | **Full-resolution SNR** at 54×42: amplitude (vs 126 at 24×20, ~32 predicted), valid-zone count, frame time, saturation | on-device tuning of everything; the paper's resolution axis | numbers in `notes/` with date. **If unusable, develop detection at 24×20** and say so in `DECISIONS.md` |
| **B2** | **Range presets**: white card at three distances per mode | the demo, the first range data | presets in `protocol.ts` carry measured, dated exposures |
| **B3** | **FoV + projection markers** at 54×42 / 18×14 / 12×10; zone orientation | A4 if WP12 fails; the watch tier; the orientation `VERIFY` | marker positions recorded, orientation settled |
| **B4** | **Overnight empty-room recording** at the real mount, Room mode (needs WP1) | calibration limits, false-wake threshold, the A4 go/no-go | ≥8 h recording saved with its manifest entry |
| **B5** | **1 kΩ pull-ups** (820 Ω if traces are long), then 1 MHz | track-tier margin; frame rate | WP-I2C passes |
| **B6** | Test site: ceiling height, room size, mount position and tilt (≥ ~41° for the 9.6 m gate) | scenario library, A4 | written in `notes/` |
| **B7** | **Separate sensor and MCU rails** (board revision) | phase 4 only | a per-rail measurement is possible |

`EXPOSURE_BACKOFF` is still on in image 1. It prints every step: **if it fires during B1
or B4, turn it off in `prj.conf` too**, or those recordings mix exposures.

### Phase 1 — image 1 foundations

**WP1 — Long recordings (web).** *Can start now.*
Today `MAX_FRAMES = 4000` in `webinterface/lib/recorder.ts` — **~27 minutes at 2.5 fps**,
shorter than the overnight recording and the 30-minute sit-down scenario, and everything
is held in browser memory. Stream to disk (File System Access API, Chrome), keep the
v1 layout, add an optional decimation (keep 1 frame in N).
Files: `lib/recorder.ts`, `components/RecordPanel.tsx`.
Done when: an 8 h session records with flat memory use and decodes with the documented
decoder.

**WP-I2C — 1 MHz.** After B5: enable `I2C_BITRATE_FAST_PLUS` in the application overlay
(the block is already there, commented), check SCL rise time on the logic analyser.
Done when: the 54×42 frame time is measured and recorded with date (~134 ms predicted,
i2c-fast-mode-plus.md).

**WP2 — `lib/detect` harness.** *Can start now.*
Files: `firmware_test/lib/detect/` (core, no Zephyr), `tools/detect_host/` (PC build:
`.wstof` → `detect_step` → CSV; synthetic frame generator; unit tests), CI grep for
forbidden includes. Choose the host compiler once (any C99 compiler; results must be
identical because the core is integer-only).
Done when: a synthetic blob on a known path gives exact expected CCL and gate outputs in
unit tests; an existing v1 recording replays; `detect_state_hash` is defined and stable.

**WP3 — Calibration D1 in the core** (§4.2). Depends on WP2.
Done when: unit tests cover both sentinels, the trimmed mean against a walk-through
frame, and the three-number report; B4 (once it exists) replays to a report.

**WP4 — Front end + A1 in the core** (§4.3–4.4). Depends on WP3.
Done when: unit tests for the signed-subtraction trap, `depth_used` imputation, both
despeckle rules, the `MAX_RUNS` worst case, and the metric overflow case; offline A1
count on B4 is 0 and on a one-person walk-through is 1 for the frames the person is in
view.

**WP5 — Firmware glue.** Depends on WP4.
Files: `firmware_test/src/app_detect.c` (thread consuming `app_capture` frames, mode
handling), `src/ble/svc_count.c` (§5.1), Kconfig `APP_DETECT`. Detection runs on the
same frame the stream sends.
Done when: calibration and detect-stream commands work from nRF Connect; the device's
`detect_state_hash` after the synthetic sequence equals the PC's.

**WP6 — D2 detect stream in the web interface** (detection-evaluation §3.3, §3.5).
Depends on WP5.
Files: `lib/protocol.ts` (label plane, detection record, detection config, calibration),
`lib/ble.ts`, new `components/DetectionPanel.tsx`, `CalibrationPanel.tsx`,
`FrameCanvas.tsx` overlay.
Contents: detection-mode selector (Raw · Calibrate · Detect stream), selecting a
detection mode proposes the Room measurement mode and warns on override; calibration
panel (empty-room confirmation, progress, report, mask painting); evaluation panel
(algorithm shown, view distance/foreground/flags, flag filter, all counts + 60 s strip
chart, **true count −/+**, advanced tunables with the parameter hash).
Done when: a live walk-through shows the blob outlined, `GATED_OUT` visible, and the
true count changes with the buttons.

**WP7 — `.wstof` v2** (§5.3). Depends on WP6. **Ship together with WP6** — the true
count must be in the very first detection recording.
Files: `lib/recorder.ts`, `docs/plan/recording-format.md`, `tools/detect_host/` reader.
Done when: a v2 recording replays on the PC and reproduces the device's counts on every
frame.

### Phase 2 — image 1 algorithms and evaluation

**WP8 — A2** (§4.5). Depends on WP4 (core) and WP6 (to see it).
Done when: unit test — one walker gives one blob, not two; a person who stops decays
out of `count_a2` as predicted.

**WP9 — A3 + tracker** (§4.6), both promotion rules. Depends on WP8.
Done when: the "chair moved and left" and "seated before power-up" recordings behave as
§4.6 predicts under each rule.

**WP10 — Mass-regression control** (§4.7) + least-squares fit in `tools/detect_host/`.
Depends on WP4 and a few true-count recordings.

**WP11 — Scenario library** (Victor records, with WP1/WP7). Depends on B6.
The first ten: empty overnight · one walks through · one sits 30 min · two walk in and
sit · two abreast · two cross · one leaves while one stays · chair moved and left ·
seated before power-up · dark clothing at the far edge. Scripted cue track where
possible.
Recordings are too large for git: keep them outside the repo and commit a manifest
(`docs/data/scenarios.md`: file name, SHA-256, date, mount, mode, scenario, duration).

**WP12 — A4 go/no-go.** *An afternoon on the PC, no firmware.* Depends on B4.
Deproject the empty room, RANSAC floor fit, under equal-angle and equal-tangent.
**Go** if residual < ~50 mm RMS with no growth toward the edges, and a standing person
reads within ±15 cm of true height at 3 m and 6 m. **Otherwise** fix the projection
model with B3's markers first. A1–A3 never wait on this.

**WP13 — A4 + A4a + floor map** (§4.8). Depends on WP12 = go, WP9.
Adds the floor fit to the calibration report and a top-down floor view to the web
interface.

**WP14 — Software binning ladder** in `tools/detect_host/`: 2×2 / 3×3 / … of the 54×42
recordings (3×3 = the hardware 18×14 grid), every algorithm at every level. The paper's
accuracy-versus-zone-count curve comes from this. Depends on WP11. B3 decides whether
software binning is a fair stand-in for sensor binning, and in which direction the error
runs.

**WP15 — Evaluation → the gate.** Depends on WP9–WP14.
1. **Write the selection criterion before looking at results** — default: lowest
   time-weighted count MAE across the library, no scenario worse than A1.
2. Replay every scenario through every algorithm (session-level cluster bootstrap for
   intervals).
3. Choose one algorithm; fit and **freeze its parameters** in one versioned file with a
   hash.
4. Write down its weak scenarios — they become the paper's limitations.

Done when: criterion, result table, chosen algorithm and parameter hash are in
`DECISIONS.md`. **Tag `eval-<date>`.**

### Phase 3 — image 2

**WP16 — `prj_deployed.conf`.** Depends on WP15.
`APP_BLE_FRAME_SERVICE=n`; only the chosen algorithm's entry point linked; RTT and
logging off; `APP_BLE_FIRST`, `VL53L9CX_DEFER_BOOT`, `APP_BLE_AUTOSTREAM`,
`VL53L9CX_EXPOSURE_BACKOFF`, `APP_LOG_FULL_GRID` off; IMU off unless used; boot banner
names the image and prints the parameter hash.
Build: `west build -b water_sense_board/nrf54l15/cpuapp firmware_test -- -DFILE_SUFFIX=deployed`.
Done when: `.config` grep and `nm` show no frame service; the web interface shows its
"no frame service" banner.

**WP17 — Duty cycling** (§4.9). Depends on WP16, B4, WP11.
First resolve the `VERIFY`: **can the frame read stop after the depth plane?**
(4,636 B ≈ 104 ms instead of 334 ms.) Tune the wake tests by replaying B4 and the
library through the core at watch rate.
Done when: B4 replayed gives ≤ ~1 false wake per hour; the library replayed with duty
cycling loses no more accuracy than the stated limit.

**WP18 — Count advertisement** (§5.4). Depends on WP16.
Non-connectable, 1–10 s interval, `report_seq`, plus a bounded connectable window for
calibration and configuration. TX power chosen by measurement, not the +8 dBm set for
bring-up.

**WP19 — Advertisement scanner bridge.** *Required, not optional* — energy runs must not
hold a connection, and Chrome cannot scan advertisements without a flag.
Files: `tools/adv_bridge/` (Node + `noble`, WebSocket), a count view in the web
interface. nRF Connect on a phone for spot checks.

**WP20 — Image-2 verification.** Depends on WP17–WP19.
The hash test agrees on the PC, image 1 and image 2; a short session confirms the
**advertised** count against ground truth through the bridge.

**WP21 — Tag `deployed-<date>`** from the same commit as `eval-<date>`, parameter hash
in both tag messages. Only then do energy measurements start.

### Phase 4 — the paper's measurements

On image 2 only, rails separated (B7), PPK2 with GPIO phase markers. Details in
[paper.md](paper.md) and [people-counting.md](people-counting.md) §7.2.

- **Energy per frame, per phase:** integration, I²C transfer, processing, radio — at
  every resolution and rate. Expected headline: at 400 kHz ~95% of sensor-active time is
  the transfer, not the measurement; at 1 MHz with depth-only 18×14 exposure becomes
  dominant. Capture the inversion.
- **Accuracy versus angular pitch** from WP14: non-inferiority (TOST) against a margin
  declared before collection, parameters tuned per resolution on training folds only,
  confidence intervals by session-level cluster bootstrap — never n = frames.
- **The standby/power-down crossover**, below.
- **A measured multi-day battery run** with a discharge curve.

#### The crossover that decides the architecture

Powering the sensor down removes idle current but costs a firmware-blob reload on wake:

```
full power-down wins when   T_idle  >  E_reload / P_standby
```

The blob upload is **313 ms, measured 2026-09-11** (the 250 ms here before was an
estimate). Its power is **unsourced** — the VCSEL is not firing, so almost certainly well
below the 450–800 mW active figure — and **VL53L9CX standby current is unknown**. Both are
`VERIFY`. With a depth-only 18×14 watch tier the upload becomes ~82% of a watch cycle,
which moves the crossover toward keeping standby — so measure both strategies rather
than defaulting to either.

---

## 7. Open decisions — Victor's, with the default if nobody decides

| question | default |
|---|---|
| Host compiler for `tools/detect_host/` | whatever is already installed; MinGW-w64 gcc if nothing is |
| Where recordings live | outside git, manifest with SHA-256 in `docs/data/scenarios.md` |
| Calibration persistence | RAM only in image 1; RRAM + temperature + timestamp in image 2 |
| Selection criterion (WP15) | time-weighted MAE, no scenario worse than A1 — fixed before results |
| Test site geometry (B6) | none — needed before WP11 |
| A3 promotion rule | not a decision: measured in WP9 |
| Paper venue / deadline | changes sequencing of phase 4 only |

---

## 8. Risks and `VERIFY` items, carried

| risk | where it bites | early warning |
|---|---|---|
| Full-resolution SNR too low | everything at 54×42 | B1 |
| Merged people (abreast, same range) | every count | the two-abreast scenario; A4's split |
| Background at grazing incidence | A1, A3 | the calibration spread count |
| Projection model wrong | A4 only | WP12 residual |
| Seated people ~40% of standing silhouette | gates, A4 height band | "one sits 30 min" |
| Frame-rate dependence (MHI, timeouts) | after 1 MHz | retune at WP-I2C |
| 9.6 m hard gate | coverage, mount tilt ≥ ~41° | stated as a limitation |

`VERIFY`, not designed against silently: early stop after the depth plane · blob-upload
power · standby current · zone orientation · software vs sensor binning · whether 12×10
at binning 8 keeps the wide FoV (108×84 / 8 is non-integer) · the load-capacitor values
(Nordic DK values, empirically required, not confirmed for the module).

---

## 9. What this file supersedes

| document | section | status |
|---|---|---|
| [roadmap.md](roadmap.md) | §5 build order, §7 next three things | replaced by §6 here |
| [people-counting.md](people-counting.md) | §4 implementation sketch | **buggy — do not implement**; §4.1–4.9 here |
| [people-counting.md](people-counting.md) | §5, §5b build order | replaced by §6 here |
| [counting-algorithms.md](counting-algorithms.md) | all code; "Algorithm A/B" framing | replaced by §4 here (A → A4, B dropped) |
| [detection-evaluation.md](detection-evaluation.md) | §7 build order; §3.5 "count-only preview" | replaced by §6 here; preview dropped per §5 there |
| [ble-streaming-and-web-ui.md](ble-streaming-and-web-ui.md) | §6 tier-4 algorithm, §10 build order | replaced by §4 and §6 here |

Those files remain the record of **why**. This one is the record of **what, and in which
order**.

---

## 10. Repository layout — planned additions

```
firmware_test/
  prj.conf                 image 1 — evaluation
  prj_deployed.conf        image 2 — deployed / measurement          (WP16)
  lib/detect/              detection core, no Zephyr                  (WP2–WP13)
  src/app_detect.c         Zephyr glue: thread, modes                 (WP5)
  src/ble/svc_count.c      counting service 53f93001                  (WP5)
tools/
  detect_host/             PC replay, synthetic frames, tests, fits   (WP2, WP10, WP14)
  adv_bridge/              advertisement → WebSocket                  (WP19)
webinterface/
  components/DetectionPanel.tsx, CalibrationPanel.tsx                 (WP6)
docs/data/scenarios.md     recording manifest                         (WP11)
```
