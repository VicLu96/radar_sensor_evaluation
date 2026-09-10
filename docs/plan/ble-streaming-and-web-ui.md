# Plan: BLE frame streaming and the Next.js configuration UI

Written 2026-09-10. **Nothing here is implemented.** This is the design to build against.

---

## 0. The rule this crosses, and how to keep it intact

`CLAUDE.md`: *"Counts leave the device, frames never do. The privacy claim is architectural
and free — do not add a raw-frame transmit path."*

This plan streams raw frames. That is a real conflict and it needs a deliberate answer, not
a footnote.

**Tier 4 below resolves this rather than excusing it.** The deployed application is
people counting running ON the MCU, emitting a single integer. That IS the counts-only
architecture the rule describes. Frame streaming is the instrument used to build and score
that algorithm — a bench tool, not the product.

**So: two firmware profiles, and the privacy claim attaches to one of them.**

| | `dev-stream` | `deployed` |
|---|---|---|
| Frames over BLE | yes | **never** |
| Counts over BLE | yes | yes |
| Counting algorithm | runs, and is scored against the frames | runs, and is all there is |
| Built by | `-S dev-stream` snippet | plain build |
| Purpose | tuning, calibration, the paper's data | the product |

The tuning work *requires* seeing frames — you cannot choose a resolution, exposure or
threshold from a count. What the privacy claim actually says is that the **deployed** node
has no frame transmit path compiled into it, and that is checkable: the GATT frame service
must not exist in the default build.

**Make it verifiable, not just stated.** A CI or pre-release check that greps the deployed
`.elf` for the frame-service UUID and fails if present is worth more than any amount of
documentation. `strings zephyr.elf | grep 53l90002` must return nothing.

Update `CLAUDE.md` to say this explicitly before writing the code, so the rule and the
build agree.

---

## 1. What we are building

```
  VL53L9CX ──I²C 400 kHz──▶ nRF54L15 ──BLE 2M PHY──▶ Chrome ──▶ Next.js UI
   14,842 B/frame           fragment +               Web           canvas heatmap
   ~404 ms read             GATT notify              Bluetooth     + config panel
                                  ▲                                      │
                                  └───────── config writes ◀─────────────┘
```

**Established numbers this design has to live inside** (from
[ble-frame-rate.md](ble-frame-rate.md) and [frame-rate-budget.md](frame-rate-budget.md)):

- Full frame 54×42 = **14,842 bytes** (3 planes × 2268 × 2, + 1134 DSS, + 100 status)
- I²C read at 400 kHz ≈ **404 ms** → **~2.5 fps ceiling**, and the bus is the bottleneck
- BLE practical **105–140 kB/s** → a full frame in 106–141 ms, so BLE has ~3× headroom
- ATT payload with MTU 247 = **244 bytes** per notification

At 2.5 fps with all three planes that is ~37 kB/s — comfortable. **Distance-only is 4,536
bytes**, and should be the default; the other planes are opt-in.

---

## 2. Realtime configuration — what is worth exposing

Chosen from what actually changed behaviour during bring-up, not from what the register map
allows.

### Tier 1 — the tuning loop, needed constantly

| Parameter | Range | Why | Constraint |
|---|---|---|---|
| **Resolution** | 4×4 … 54×42 | The paper's main axis: energy vs zone count | STANDBY |
| **Exposure** | 1–30 ms | Decides valid-zone count. `set_exposure` limit is ST's | STANDBY |
| **Planes streamed** | distance / +amplitude / +ambient | Bandwidth vs diagnosis. Amplitude is how we tell "no signal" from "rejected signal" | host-side |
| **Frame period** | 10 ms – 1 s | Autonomous rate. ST rejects outside this | STANDBY |
| **Sync mode** | manual / autonomous | Single-shot for stepping, autonomous for streaming | STANDBY |

### Tier 2 — the energy experiment

| Parameter | Range | Why |
|---|---|---|
| **Power mode** | regular / low / ultra-low | ST's three modes, never yet compared |
| **Duty cycle** | on-time / off-time ms | The stage-4 experiment: `TURN_OFF` between frames vs standby |
| **Sensor power** | on / off | Manual rail control, for measuring the crossover directly |
| **AP_CLK hold** | on / off | The A/B that has been outstanding since 2026-09-04 |

### Tier 3 — profile tuning, rarely touched

`DISTANCE_SWITCHOVER`, `DISTANCE_RTN_SHORT_OFFSET`, `DSS_DEFAULT_INIT_LUT`, context
short/long. Expose as raw register writes behind an "advanced" panel, because getting these
wrong is how you produce plausible rubbish.

### Tier 4 — people counting on the MCU. **This is the product.**

Everything above is instrumentation. This is what the paper is about, and it is the only
mode a deployed node runs.

**Why the algorithm belongs on the MCU and not in the browser.** `CLAUDE.md` already says
it: sensor energy dominates and MCU cycles are nearly free. Two passes over 2268 zones on a
128 MHz Cortex-M33 is well under a millisecond, against a **404 ms** I²C read. The
computation is free; the transmission is not. Counting on-device turns 14,842 bytes per
frame into **8 bytes per report**, and at 0.1 Hz that is the difference between a
maintained streaming connection and a radio that is off almost always.

#### The scene, and what it implies

From [room-occupancy.md](room-occupancy.md): ceiling-mounted, 0.05–0.2 Hz, and the
difficulty is **segmentation, not timing**. The field of view is 1.02h × 0.77h, so at a
2.7 m ceiling the footprint is ~2.8 × 2.1 m and each 54×42 zone covers roughly **5 × 5 cm**.

**Confirmed by Victor 2026-09-10: ~2.5–3 m standard ceiling, and 3–5 people must be counted
simultaneously.** Both numbers below follow from that and are no longer assumptions.

A seated or standing person's shoulders span ~45 cm ≈ **9 zones across**, and a whole person
is a blob of roughly **50–150 zones**.

#### Why 3–5 people changes the algorithm, not just the constants

The footprint is **5.88 m²**. Five people is **1.18 m² each** — about 1.1 m of average
spacing. Areal coverage is only ~11%, so they are not packed, and blobs will often be
separate.

But *often* is the problem. People in conversation stand 0.5–1 m apart, and at 0.5 m
separation two 45 cm shoulder spans leave a **5 cm gap — exactly one zone**. One noisy zone
and the two blobs merge. So with 3–5 people, **merging is not a corner case; it is a
routine event**, and connected-component counting alone will systematically under-count
precisely when the room is busiest — which is the worst possible error profile for an
occupancy sensor.

**Therefore the primary detector is heads, not blobs.** From a ceiling sensor a head is the
closest point on a person: floor background ~2.7 m, a standing head ~1.0 m nearer, shoulders
~0.25 m further than the head. A head is ~20 cm ≈ **4 zones** across and shows as a distinct
local minimum in distance. Two people whose shoulder blobs merge still present **two
separate minima**, which is the whole point.

This is also the established approach for overhead ToF counting, and it is a better fit
here than blob counting was.

#### Calibration: the empty-room background

Triggered by command, because only a human knows the room is empty.

1. Capture `N` frames (default **16**) at the configured resolution.
2. Per zone, accumulate the mean of *valid* distances and count how many frames were valid.
3. A zone with validity below `min_valid_pct` (default 75%) is marked **unreliable** and
   excluded from detection for good — glass, a dark absorbing surface, or a grazing angle
   will never give a usable background, and pretending otherwise manufactures false blobs.
4. Record the **temperature** from the status line alongside it.
5. Persist to NVS so it survives reboot and power-cycling.

**Storage**: `u16 bg_mm` per zone plus a validity bit = 2268 × 2 + 284 ≈ **4.8 KB**.

**Report the quality back.** "1,932 of 2,268 zones have a usable background (85%)" tells
you immediately whether the mount is any good. A calibration that only fixes 40% of the
frame is a mounting problem, and it should say so rather than silently producing bad counts.

#### Detection, per frame

| Step | What | Parameters |
|---|---|---|
| 1 | **Foreground**: `bg_mm[z] − dist_mm[z] > fg_threshold_mm`, zone valid, zone reliable, `amplitude > min_amplitude` | `fg_threshold_mm` (300), `min_amplitude` |
| 2 | **Despeckle**: 3×3 majority filter | — |
| 3 | **Head candidates**: local minima of distance within a `head_window` box, at least `head_prominence_mm` nearer than the window edge | `head_window_zones` (5 ≈ 25 cm), `head_prominence_mm` (150) |
| 4 | **Non-maximum suppression**: candidates closer together than `min_head_sep_zones` collapse to the nearest one | `min_head_sep_zones` (8 ≈ 40 cm) |
| 5 | **Connected components** for *support*, not for counting: a candidate with no plausible body around it is noise, and blob size feeds confidence | `min_blob_zones` (30), `max_blob_zones` (400) |
| 6 | **Temporal debounce**: present in `n` of last `m` frames | `temporal_n` (2), `temporal_m` (3) |
| 7 | **Count** = surviving heads; **confidence** from stability and the reliable-zone fraction | — |

Steps 3–4 are what make 3–5 people workable. Blob analysis is demoted to a sanity check: a
box on a chair produces a blob but no head-shaped minimum, and a merged two-person blob
produces two minima.

`fg_threshold_mm` at 300 says a head is at least 30 cm below the background. Step 6 matters
more than it looks at 0.1 Hz: with frames 10 s apart, "present in 2 of the last 3" costs up
to **30 s of latency** — right for dwell, wrong for anything transient. **Make it
configurable and show the implied latency in the UI**, so nobody sets it without seeing the
cost.

**The failure mode to watch** is two heads at the same height 40 cm apart, which NMS will
merge. `min_head_sep_zones` trades that against splitting one person's head-and-shoulder
into two. That trade cannot be settled on paper — it needs the observer log.

**Working memory**: a `u16` label per zone = 4.5 KB, plus the background model. About 10 KB
total, against 77 KB used of 188 KB.

#### What gets sent

```
u32 timestamp_s
u8  count
u8  confidence      0..100
u8  flags           bit0 calibrated, bit1 background stale, bit2 degraded FoV
u8  instance_id     which node
```

**8 bytes.** Sent on a configurable period, or immediately on change, or both.

`instance_id` costs nothing now and is a protocol break later, so it goes in from the
start — Victor confirmed on 2026-09-10 that several nodes are planned. **One consequence
worth flagging early**: a single sensor's 2.8 × 2.1 m footprint does not cover a room, so
multiple nodes will need coverage stitching and de-duplication of people seen by two
sensors. That is a design question of its own and is not solved by an id field.

#### How the paper gets its accuracy axis

**In `dev-stream`, the frame and the count are sent together for the same capture.** That is
the whole point of building the streaming path: it lets the count be scored against the
picture that produced it, offline, at every resolution and exposure. Without that pairing
there is no accuracy axis and no paper — so the Frame Info header and the Count payload
must carry the **same `seq`**.

#### Configurable, and that is deliberate

Every parameter above is exposed over BLE, because the paper's contribution is the
trade-off curve. Resolution and exposure move energy; `fg_threshold_mm`,
`min_blob_zones` and the temporal window move accuracy; and the interesting result is where
they cross. A build-time constant would make that sweep a firmware rebuild per point.

#### Honest open questions

- **Two people touching** merge into one blob. Size gating catches some of it
  (`max_blob_zones`), watershed splitting would catch more, and neither is free. Decide
  after seeing real data.
- **Furniture moved** invalidates the background silently. The `background stale` flag can
  be driven by a slow drift estimate, but the real answer is recalibration, which is why it
  is a command.
- **Temperature drift** shifts distances. Calibration records the temperature; whether that
  needs compensating is a measurement, not a guess.
- **A person under the sensor at calibration time** is baked into the background as floor,
  and that zone then never detects. Report the reliable-zone count and the mean background
  distance so an obviously wrong calibration is visible.

### Commands, not settings

`START`, `STOP`, `SINGLE_SHOT`, `REBOOT`, **`CALIBRATE`**, **`CLEAR_CALIBRATION`**.

> **Every Tier-1 change needs the device in STANDBY.** The firmware must stop streaming,
> apply, and restart — and report which happened. That sequencing is exactly what broke on
> 2026-09-10, so the config handler must reuse `wait_for_standby()` rather than assume.

---

## 3. GATT design

One base UUID, three services. **These are arbitrary but must be frozen before both sides
are written.**

Base: `53l9XXXX-1e2d-11ef-9262-0242ac120002`

### 3.1 Frame service — `53l90001-…`  *(dev-stream builds only)*

| Char | UUID | Props | Payload |
|---|---|---|---|
| Frame Data | `53l90002` | Notify | fragment, ≤244 B |
| Frame Info | `53l90003` | Read, Notify | 16-byte frame header, sent once per frame **before** its fragments |

**Frame Info (16 B, little-endian)** — the reassembler needs this before the data:

```
u8  instance_id      which node this came from (Victor, 2026-09-10: plan for several)
u8  reserved0
u16 seq              driver frame counter
u16 device_frame     the DEVICE's counter (gaps = we dropped one, not the sensor)
u8  cols, rows
u8  planes           bitmask: 1 distance, 2 amplitude, 4 ambient
u8  flags            bit0 square-format
u16 total_fragments
u16 payload_bytes
u16 temperature_raw
u16 capture_ms       how long the capture actually took — an energy datum
```

### 3.2 Fragmentation

Each Frame Data notification:

```
u16 seq              matches Frame Info
u16 frag_index       0 .. total-1
--- 240 bytes payload ---
```

4-byte header, **240 B payload** → 14,842 B = **62 fragments**; distance-only = 19.

Reassembly rule on the host: a frame is complete when all indices for `seq` have arrived.
Any fragment with a new `seq` **discards the incomplete previous frame** and counts a drop.
No retransmission — at 2.5 fps a lost frame is cheaper than a stall.

**Report the drop rate in the UI.** It is the honest measure of whether BLE keeps up, and
the paper will want it.

### 3.3 Config service — `53l91001-…`

| Char | UUID | Props | Payload |
|---|---|---|---|
| Config | `53l91002` | Read, Write | 16-byte packed struct below |
| Command | `53l91003` | Write | `u8 opcode, u8 arg[3]` |
| Config Result | `53l91004` | Notify | `u8 opcode, i8 status, u8 detail[2]` |

**Config struct (16 B)**

```
u8  resolution       0..5 enum, matches VL53L9CX_RES_*
u8  exposure_ms      1..30
u32 frame_period_us  10000..1000000
u8  sync_mode        0 slave, 1 manual, 2 autonomous
u8  power_mode       0 regular, 1 low, 2 ultra-low
u8  planes           bitmask
u8  duty_on_frames   0 = continuous
u16 duty_off_ms
u32 reserved
```

**Write is transactional**: validate everything, then apply, then notify Config Result with
0 or a negative errno and which field was rejected. Never partially apply — half a profile
is how you get plausible rubbish.

### 3.4 People counting service — `53l93001-…`  *(both builds — this is the product)*

| Char | UUID | Props | Payload |
|---|---|---|---|
| Count | `53l93002` | Read, Notify | the 8 bytes above |
| Detection Config | `53l93003` | Read, Write | thresholds, blob gates, temporal window, report period |
| Calibration Control | `53l93004` | Write, Notify | `u8 opcode, u8 n_frames`; notifies progress then a quality summary |
| Background Model | `53l93005` | Read | *dev-stream only.* Lets the UI draw the background and show why a zone never fires |

**Detection Config (12 B)**

```
u16 fg_threshold_mm      default 300
u16 min_amplitude
u16 min_blob_zones       default 30
u16 max_blob_zones       default 400
u16 head_prominence_mm   default 150
u8  head_window_zones    default 5
u8  min_head_sep_zones   default 8
u8  temporal_n           default 2
u8  temporal_m           default 3
u16 report_period_s      0 = on change only
```

**Calibration Status notification**

```
u8  state           0 idle, 1 running, 2 done, 3 failed
u8  frames_done
u16 zones_reliable
u16 zones_total
u16 mean_bg_mm
u16 temperature_raw
```

`zones_reliable / zones_total` is the number that says whether the mount is usable. Show it
as a percentage and colour it.

### 3.5 Telemetry service — `53l92001-…`

| Char | UUID | Props | Payload |
|---|---|---|---|
| Health | `53l92002` | Notify | FSM, ERROR_CODE, ERROR_STATUS, 5×LDD_STATUS, capture ok/fail counts |
| Energy | `53l92003` | Notify | last capture ms, boot ms, frames since boot, reboot count |

Health is the bring-up work made remote — the same bytes the driver already reads on a
laser fault. Push it on every failure and every 10th success.

---

## 4. Firmware work

**4.1 Enable BLE.** `CONFIG_BT`, peripheral, `BT_CTLR_PHY_2M`, `BT_USER_PHY_UPDATE`,
`BT_L2CAP_TX_MTU=247`, `BT_BUF_ACL_TX_SIZE=251`, DLE. Board already declares
`HAS_BT_CTLR`. Expect RAM +30–40 KB; we are at 77 KB of 188 KB, so there is room.

**4.2 A streaming thread**, separate from `main()`. Captures, fragments, notifies. Must
handle backpressure: `bt_gatt_notify` returns `-ENOMEM` when buffers are full — wait on a
callback, never spin.

**4.3 Config handlers** that stop → `wait_for_standby()` → apply → restart, reporting each
step.

**4.4 A `dev-stream` snippet** that enables the frame service. Default build: no frame
service, no frame code linked.

**4.5 Keep RTT.** It is the only channel that works when BLE is the thing being debugged.

---

## 5. The Next.js application

```
web/
  package.json          next, react, typescript
  app/page.tsx          single page, no routing needed
  components/
    ConnectButton.tsx   navigator.bluetooth.requestDevice
    FrameCanvas.tsx     <canvas> heatmap, requestAnimationFrame
    ConfigPanel.tsx     Tier 1 + 2 controls
    HealthPanel.tsx     FSM, error bits, LDD — red when non-zero
    StatsBar.tsx        fps, drop rate, capture ms, zones valid
    CountPanel.tsx      the people count, big; confidence, flags, history plot
    CalibratePanel.tsx  trigger, progress, reliable-zone percentage
    DetectionOverlay    blob outlines drawn over the heatmap, so a wrong count
                        is visibly wrong rather than just wrong
  lib/
    ble.ts              connect, subscribe, write
    protocol.ts         decode Frame Info, reassemble, decode config
    palette.ts          distance → colour
  hooks/
    useTofDevice.ts     one hook owning connection + frame state
```

**Run with `npm run dev`, open `http://localhost:3000` in Chrome.**

### Things that will bite, worth knowing before writing code

- **Chrome only.** Web Bluetooth is not in Firefox or Safari. Say so in the UI rather than
  failing mysteriously.
- **Secure context required.** `localhost` counts, so `npm run dev` is fine. Deploying to a
  plain-HTTP host is not.
- **A user gesture is mandatory** for `requestDevice()`. It must be behind a real button.
- **Declare every service in `optionalServices`** or `getPrimaryService` throws even after
  a successful connect. This catches everyone once.
- **Notification throughput in Web Bluetooth is the real risk.** Chrome on Windows has
  historically delivered notifications far below the link's capability. **Prototype this
  first** — a firmware build that notifies a counter as fast as it can, and a page that
  measures the rate. If it cannot sustain ~40 kB/s, the design changes: distance-only,
  lower resolution, or on-demand single frames instead of streaming.
- **Render off the React render path.** Draw to canvas with `ImageData` and
  `requestAnimationFrame`; do not put 2268 zones into component state. Keep the latest
  frame in a ref, render on a timer.
- **Next.js SSR has no `navigator`.** All BLE code behind `'use client'` and a `useEffect`.

---

## 6. Build order

Each phase ends with something demonstrable, and the risky measurement comes first.

| # | Phase | Done when |
|---|---|---|
| **0** | **Throughput spike.** Firmware notifies a counter flat out; a minimal page measures kB/s. **Two days, and it decides the rest.** | A number for sustainable Web Bluetooth throughput |
| 1 | GATT skeleton: all three services, config read/write, telemetry. No frames. | `nRF Connect` app can read and write config; the sensor reconfigures |
| 2 | Next.js shell: connect, config panel, health panel. Still no frames. | Configuration round-trips from the browser |
| 3 | Frame streaming: Frame Info + fragmentation, host reassembly, drop counting. | Frames arrive with a measured drop rate |
| 4 | Canvas heatmap + stats. | The picture, live |
| 5 | Duty-cycle controls and the energy panel. | The stage-4 experiment is drivable from the browser |
| **6** | **Calibration**: capture, background model, NVS persistence, quality report. | An empty room produces a background and a reliable-zone percentage |
| **7** | **Detection on the MCU**: foreground, despeckle, connected components, gating, debounce. Count sent alongside the frame with a matching `seq`. | The UI shows a count AND the blobs it came from |
| **8** | **Deployed profile**: counting only, frame service not compiled in, verified by the UUID grep. | `strings zephyr.elf \| grep 53l90002` returns nothing |

Phases 6–8 are the paper. Phases 0–5 are the instrument that makes them measurable.

**Phase 0 is not optional.** Every later phase assumes a throughput number nobody has yet,
and if Web Bluetooth cannot sustain it the whole streaming design changes shape. Two days
spent there is cheap against rewriting phases 3–5.

---

## 7. What could invalidate this plan

1. **Web Bluetooth throughput** below ~40 kB/s. Mitigation: distance-only (4,536 B), lower
   resolution, or request-a-frame instead of streaming.
2. **The sensor still does not range reliably.** Everything here assumes frames exist.
   Streaming a fault is not progress — finish the laser/supply question first.
3. **Segmentation may not survive real rooms.** With 3–5 people in 5.88 m², head detection
   replaces blob counting for exactly this reason — but it has its own failure: two heads at
   similar height within `min_head_sep_zones` merge, and one person's head-and-shoulder can
   split into two. Both are data questions and neither can be settled on paper. **This is
   the risk most likely to decide whether the paper has a result.**
3. **BLE and the 400 kHz I²C read competing for CPU.** The read blocks ~404 ms; BLE
   connection events must still be serviced. Likely fine (the TWIM is DMA-driven) but
   unverified.
4. **RAM.** BLE wants 30–40 KB and we are at 77 KB of 188 KB with a 32 KB RTT buffer that
   can shrink once BLE carries the diagnostics.

---

## 8. Open decisions for Victor

- **Pairing and bonding?** Plan assumes none — dev tool on a bench. A deployed node
  reporting occupancy probably wants bonding.
- **Multiple sensors later?** If so, put an instance id in Frame Info now rather than
  reworking the protocol.
- ~~Log frames to disk?~~ **Decided 2026-09-10: no. Ground truth is a live observer log.**

  The UI therefore needs an **observer panel**: a control to record "the true count is now
  N" with a timestamp, written alongside the received counts, and exportable as CSV. That
  is small and belongs in phase 7.

  **The cost, stated once.** An observer log cannot be re-scored. Every change to
  `fg_threshold_mm`, `min_head_sep_zones` or the temporal window means repeating the
  experiment with people in the room, rather than re-running the algorithm over saved
  data. With head detection now carrying the count, and `min_head_sep_zones` explicitly
  needing empirical tuning, that is a real cost in bodies and hours.

  A cheap hedge, if it ever bites: frames are already arriving in `dev-stream`, so a
  "record" button dumping them to disk is perhaps a day of work and preserves the option.
  Worth reconsidering only if the parameter sweep starts eating sessions.
