# Plan: BLE, on-device people counting, and the web interface

Consolidated 2026-09-10 from four rounds of design discussion. **Nothing here is
implemented.** This is the document to build against.

---

## Status

### Decided

| | Decision | When |
|---|---|---|
| Mount | Angled, in a **ceiling corner** — not overhead | 2026-09-10 |
| Ceiling height | 2.5–3 m | 2026-09-10 |
| People to count | **3–5** simultaneously | 2026-09-10 |
| Detection | **Both** static (calibrated background) **and** motion, fused | 2026-09-10 |
| Algorithm location | **On the MCU.** Only the count leaves | 2026-09-10 |
| Deployed reporting | **Connectionless** — count in the advertisement | 2026-09-10 |
| Device clock | **None.** The web interface timestamps on receipt | 2026-09-10 |
| Config persistence | **No NVS.** Defaults on boot | 2026-09-10 |
| Multi-node | Plan for several now — `instance_id` from the start | 2026-09-10 |
| Ground truth | **Live observer log**, not frame logging | 2026-09-10 |

> **⚠ REVIEWED 2026-09-11 — read [expert-review-2026-09-11.md](expert-review-2026-09-11.md)
> before building anything from this document.** Three specialist reviews found three
> findings that change the project rather than the plan, two of them verified against
> UM3683: the sensor **cannot range beyond 9.6 m** (a 64 ns gate, which bounds tilt and
> therefore coverage), our profile draws **450–800 mW and not 150 mW**, and full-resolution
> SNR may not exist at all. Sections below are **not yet updated** for those.

### Built 2026-09-11 — phases 1-5

Scope confirmed with Victor: the streaming instrument now, detection (phases 6-9)
after. Chrome. Mount ~2.5 m at 41-50 deg tilt, which is the shallow end of what the
9.6 m range gate allows.

| Phase | State |
|---|---|
| 1 GATT skeleton: config + telemetry | **done** — `firmware_test/src/ble/` |
| 2 Advertising | **done** for connections. Count-in-advert is phase 7 |
| 3 Next.js shell: connect, config, health | **done** — `webinterface/` |
| 4 Frame streaming, fragmentation, drop counting | **done** |
| 5 Canvas heatmap + stats | **done** |
| 0 Throughput spike | **folded into phase 5** rather than run separately: the stats bar measures kB/s, fps and drop rate against real frames, so the number arrives from the instrument itself |

Cost: FLASH 77,484 -> 188,496 B (12.9% of 1428 KB), RAM 77,376 -> 111,328 B (57.8%
of 188 KB). The plan predicted +30-40 KB RAM and it is +34 KB.

**UUIDs are now FROZEN** in `firmware_test/src/ble/ble_uuid.h` and mirrored in
`webinterface/lib/protocol.ts`. Nothing checks that the two agree; `PROTOCOL_VERSION`
is the only guard, and both sides refuse to work across a mismatch.

### Blocking

1. **The sensor does not yet range reliably.** It boots, uploads its blob and enters
   streaming, then leaves streaming without producing a frame — UM3683 §2.4 says a laser
   safety fault does exactly that. Until this is closed, everything below is theory.
   **Streaming a fault is not progress.**
2. **Pull-ups need changing** if the track tier is to have headroom — see §3.

### The two things I could not answer and you should

- **Does calibration persist?** Config does not, by decision. But a calibration needs an
  **empty room** and 16 frames, and losing it on every power cycle is a different cost from
  re-sending a threshold. Not the same question.
- **Does the count include people already seated when the node powers up?** They have never
  moved in view, so motion cannot confirm them. See §6.6 item 4.

---

## 1. Architecture

```
  VL53L9CX ──I²C──▶ nRF54L15 ──────────────────────────────▶ host
   14,842 B         · capture                BLE
   per frame        · calibrate              ├─ STREAMING mode: connection,
                    · detect + count         │    frames + config + telemetry
                    · advertise              └─ COUNTING mode: advertisement only,
                                                  count + activity, nobody connects
```

**Two firmware profiles**, and the privacy claim attaches to one:

| | `dev-stream` | `deployed` |
|---|---|---|
| Frames over BLE | yes | **never — not compiled in** |
| Counting algorithm | runs, scored against the frames | runs, and is all there is |
| Built by | `-S dev-stream` | plain build |

`CLAUDE.md` says *"counts leave the device, frames never do"*. Tier 4 **is** that
architecture; streaming is the instrument used to build and score it. Make it checkable, not
merely stated. **But not with `strings`** — corrected 2026-09-11: `BT_UUID_128_ENCODE` emits
a 16-byte binary initialiser, not ASCII, so a `strings | grep` gate can never fail and would
have been cited as architectural proof in a paper. Gate on the generated `.config` (the frame
service's Kconfig symbol unset) **and** `nm zephyr.elf` showing the service symbol absent. Update `CLAUDE.md` to say this before writing code, so the rule and the build agree.

---

## 2. The numbers this has to live inside

Measured or derived, not assumed. Sources: [ble-frame-rate.md](ble-frame-rate.md),
[frame-rate-budget.md](frame-rate-budget.md).

| | |
|---|---|
| Full frame 54×42 | **14,842 B** (3 planes × 2268 × 2, + 1134 DSS, + 100 status) |
| I²C read @ 400 kHz | **~404 ms** → **~2.5 fps ceiling** |
| I²C read @ 1 MHz | ~162 ms → ~6.2 fps |
| Blob upload on boot | **306 ms**, measured |
| BLE practical | 105–140 kB/s → a full frame in 106–141 ms |
| ATT payload @ MTU 247 | 244 B → **62 fragments** per full frame, 19 distance-only |
| Distance-only | **4,536 B** — the default; other planes opt-in |

**The bus is the bottleneck, not BLE.** BLE has ~3× headroom at 400 kHz.

---

## 3. Hardware prerequisites

### 3.1 Pull-ups — fit 1 kΩ

Currently **4.7 kΩ** (Victor, adjustable). `t_r = 0.8473·R·C`; I²C allows 300 ns at 400 kHz
and 120 ns at Fast-mode Plus.

| Bus C | max R @ 400 kHz | max R @ 1 MHz | 4.7 kΩ gives |
|---|---|---|---|
| 80 pF | 4.4 kΩ | 1.8 kΩ | 319 ns |
| 100 pF | 3.5 kΩ | **1.4 kΩ** | 398 ns |
| 150 pF | 2.4 kΩ | 0.94 kΩ | 597 ns |

**4.7 kΩ is already out of spec at 400 kHz.** It works because I²C is static and the
controller samples late — not because the timing is legal. **1 kΩ** meets Fm+ to ~140 pF and
fixes 400 kHz as a side effect. Sink current 1.8 mA against a 20 mA requirement. 820 Ω if
the leads are long; 1.5 kΩ is the most that is still safe for 1 MHz.

*Cost:* 1.8 mA per line while held low — ~0.3 µA average at 0.1 Hz, but ~0.6 mA at 2 fps.
One more reason the track tier runs only during activity.

### 3.2 ~~`VERIFY` — the IMU may cap the bus at 400 kHz~~ **ANSWERED 2026-09-11**

**The LSM6DSV16BX supports fast mode AND fast mode plus (1 MHz), so it is not the
obstacle.** An **LSM6DSO** would have been — that part is 400 kHz only, with higher rates
available solely over I3C — which is why the question was worth asking and why the
schematic should confirm which is actually fitted. `docs/hardware/mcu-isp2454ll.md`
records an LSM6DSV..BX.

Note the part has been **silent at 0x6B since 2026-09-10**, so this identification rests
on the schematic rather than on a WHO_AM_I read. Worth settling both at once.

Full working: **[i2c-fast-mode-plus.md](i2c-fast-mode-plus.md)**.

### 3.3 Cleared

The nRF54L15 supports 1 MHz (`TWIM_FREQUENCY_FREQUENCY_K1000` defined → Zephyr's
`I2C_SPEED_FAST_PLUS` maps through). The sensor supports it (ST's reference sets it
explicitly). Neither is the obstacle.

---

## 4. Operating modes

| Mode | Radio | Connection | Host behaviour |
|---|---|---|---|
| **Streaming** (dev) | connectable adv, ~100 ms | **yes** | connects; frames + config + telemetry |
| **Counting** (deployed) | connectable adv, 1–10 s, **count in the advert** | **no** | listens to adverts; connects only to reconfigure |

The node stays **connectable in both modes**, so it can always be reached. Only the
advertising interval changes: slower is cheaper but slower to reach. 1 s default,
configurable.

### 4.1 The advertisement payload

Manufacturer-specific data (AD type `0xFF`), company ID **`0xFFFF`** — the development
range, the honest choice for a research node.

```
u8  protocol_version
u8  instance_id
u8  count
u8  confidence     0..100
u8  flags          bit0 calibrated
                   bit1 ACTIVITY NOW
                   bit2 background stale
                   bit3 degraded field of view
                   bit4 sensor fault
u16 report_seq
u8  battery_pct
```

**8 bytes** (1+1+1+1+1+2+1 — corrected 2026-09-11; this said 10). The 31-byte budget is
tighter than it looks: Flags (3) + manufacturer element (4 + 8) = 15, plus a short name ~9
= 24. A 128-bit service UUID is an 18-byte element and **does not fit** — put it in the
**scan response**, or filter on `manufacturerData` instead.

`report_seq` is not decoration: advertising is stateless and repeats the same payload until
it changes, so without it a listener cannot tell *"still 3 people"* from *"the node has
stopped updating"*.

**No timestamp** — the receiver stamps on arrival. That removes the RTC, the time-sync
command and drift, and is more honest than a device clock set once and drifting since.

### 4.2 Why connectionless

> **Corrected 2026-09-11. The energy argument I gave here was wrong.** Per `CLAUDE.md`'s own
> rule the sensor dominates: the watch tier is milliwatts, advertising is tens of microwatts.
> **BLE is under 1% of the budget either way**, and per event a connection is *cheaper* than
> connectable advertising (one RX window versus three TX plus three RX). What makes streaming
> expensive is that 2.5 fps keeps the **sensor** active essentially continuously — a sensor
> duty-cycle cost, not a radio cost.

The real reasons connectionless is right, and they are sufficient: **N nodes to one listener**
with no connection limit, no central required in range, no supervision-timeout blackouts, no
reconnection state machine, and no per-node pairing.

One consequence to design around: **with a single advertising set, Zephyr stops advertising
while a device is connected** — so counts stop reaching the bridge, and with no pairing anyone
can connect and hold the link. Advertise the count **non-connectable** and make connectability
a bounded window.

---

## 5. Configuration surface

### Tier 1 — the tuning loop

| Parameter | Range | Constraint |
|---|---|---|
| Resolution | 4×4 … 54×42 | STANDBY |
| Exposure | 1–30 ms (ST's limit) | STANDBY |
| Planes streamed | distance / +amplitude / +ambient | host-side |
| Frame period | 10 ms – 1 s (ST's limit) | STANDBY |
| Sync mode | manual / autonomous | STANDBY |

### Tier 2 — the energy experiment

Power mode (regular / low / ultra-low), duty-cycle on/off times, manual rail control,
AP_CLK hold.

### Tier 3 — profile tuning, behind an "advanced" panel

`DISTANCE_SWITCHOVER`, `DISTANCE_RTN_SHORT_OFFSET`, `DSS_DEFAULT_INIT_LUT`, context
short/long. Getting these wrong produces plausible rubbish, so they are deliberately awkward
to reach.

> **Every Tier-1 change needs STANDBY.** The handler must stop → `wait_for_standby()` →
> apply → restart, reporting each step. That exact sequencing is what broke on 2026-09-10.

### Commands

`START`, `STOP`, `SINGLE_SHOT`, `REBOOT`, `CALIBRATE`, `CLEAR_CALIBRATION`, `SET_MODE`.

---

## 6. Tier 4 — the counting algorithm. This is the product.

### 6.1 Geometry: the corner mount changes everything

**Angled from a ceiling corner**, not overhead. Two consequences.

**Head detection is impossible.** From overhead a head is the closest point on a person —
that was the whole basis for counting heads rather than blobs. From a corner the nearest
point is whatever body part faces the sensor, and it changes as someone turns.

**Blob-size gates must scale with range**, because the oblique view makes zone footprint a
function of distance:

| Range | Zone footprint | A 45 cm person spans |
|---|---|---|
| 2 m | ~3 cm | **~13 zones** |
| 4 m | ~7 cm | ~6 zones |
| 8 m | ~14 cm | **~3 zones** |

A **4× swing linearly, ~16× in area, inside one frame.** At 45° tilt from 2.7 m the slant
range spans ~3.0–6.6 m; at 30° the far edge passes 15 m, where a person is 2–3 zones and
probably below the noise floor. **Tilt angle is a design input, not a mounting detail.**

### 6.2 Fusing static and motion

Neither signal works alone:

| | Catches | Misses |
|---|---|---|
| Background subtraction | anyone present, moving or not | a coat on a chair, a moved bin |
| Motion differencing | anyone walking; confirms a blob is alive | anyone who sits down |

**Motion promotes a blob to "person"; background subtraction keeps it counted once it
stops.** The mechanism is a track lifecycle:

| State | Enters when | Counted |
|---|---|---|
| `TENTATIVE` | foreground blob appears that was not in the background | no |
| `CONFIRMED` | it shows motion, or persists with plausible size/distance | **yes** |
| `DORMANT` | a confirmed track stops moving, blob still in foreground | **yes** ← sat down |
| `LOST` | blob out of foreground beyond `lost_timeout` | no |

A coat enters `TENTATIVE`, never moves, and is either left uncounted or absorbed — a policy
decision rather than an accident.

> **The trap: never adapt the background under a live track.** Background models normally
> adapt to absorb moved furniture. Do that naively and **a person sitting still is absorbed
> and vanishes from the count** — the exact failure this design exists to prevent. Update
> only zones not covered by a `CONFIRMED` or `DORMANT` track.

### 6.3 Two-tier duty cycling — structurally required, not an optimisation

The two signals need different rates, and that is what saves the battery claim:

| Tier | Rate | Job |
|---|---|---|
| **Watch** | 0.05–0.2 Hz | Are `DORMANT` blobs still there? Any new foreground? Background subtraction only — no association, so no rate requirement |
| **Track** | 1.5–2.5 fps | Association and motion, run only while something moves |

Why the track tier needs that rate — a walking person covers 1.4 m/s:

| Rate | Movement between frames | Usable? |
|---|---|---|
| 0.1 Hz | **14 m** | No — no correspondence possible |
| 0.5 Hz | 2.8 m | No — further than the spacing between people |
| 1.5 Hz | 0.9 m | Marginal |
| 2.5 Hz | 0.6 m | Workable |

At 400 kHz the ceiling is ~2.5 fps, so the track tier has **no margin**. This is what makes
§3.1 worth doing: at 1 MHz it becomes 6.2 fps.

**Duty scales with activity, not time** — and *that* is the paper's result. The energy cost
of occupancy sensing becomes a function of how much the occupants move, which is a stronger
claim than a fixed duty cycle.

### 6.4 Calibration

Triggered by command, because only a human knows the room is empty.

1. Capture **16** frames.
2. Per zone: mean of *valid* distances, and the count of valid frames.
3. Validity below `min_valid_pct` (75%) → **permanently unreliable**, excluded. Glass, dark
   surfaces and grazing angles never give a usable background, and pretending otherwise
   manufactures false blobs. **Expect far more of these at oblique incidence than overhead.**
4. Record the temperature.

**Storage** ~4.8 KB (`u16` per zone + a validity bit).

**Report quality back**: *"1,932 of 2,268 zones usable (85%)"* tells you the mount is wrong
before the counts do.

### 6.5 Detection per frame

| Step | What |
|---|---|
| 1 | Foreground: `bg − dist > fg_threshold_mm`, valid, reliable, `amplitude > min_amplitude` |
| 2 | Despeckle (3×3 majority) |
| 3 | Connected components, 8-connectivity |
| 4 | **Range-normalised** size gating — the gate is a function of the blob's mean distance |
| 5 | Motion: difference against the previous frame within each blob |
| 6 | Association to existing tracks (nearest neighbour on centroid + size + mean distance) |
| 7 | Lifecycle update; count = `CONFIRMED` + `DORMANT` |

**Working memory** ~10 KB (a `u16` label per zone plus the background model), against 77 KB
used of 188 KB.

**Why on the MCU**: two passes over 2268 zones on a 128 MHz M33 is well under a millisecond
against a **404 ms** I²C read. The computation is free; the transmission is not. Counting
on-device turns 14,842 bytes per frame into **8 bytes per advertisement**.

### 6.6 What the plan still has to answer

1. **Tilt angle and height** — every range figure depends on them.
2. **The range-normalised size model**, and what it does when one person straddles a steep
   range gradient.
3. **What "an activity" is** — a track persisting N frames? Moving more than X metres?
4. **Promoting a track without motion**: someone already seated at power-up has never moved
   in view. Size and distance plausibility must carry it, or the system waits for a fidget.
5. **`lost_timeout` / `dormant_timeout`** — too short drops a still person, too long counts a
   departed one for minutes.
6. **What wakes the track tier** without firing on sunlight, a curtain, or noise.
7. **Does background subtraction survive oblique incidence at all?** A wall at a grazing
   angle returns very little.

---

## 7. GATT design

Base `53f9XXXX-1e2d-11ef-9262-0242ac120002`. **Freeze these before both sides are written.**

### 7.1 Frame service `53f90001` *(dev-stream only)*

| Char | UUID | Props |
|---|---|---|
| Frame Data | `53f90002` | Notify — fragment, ≤244 B |
| Frame Info | `53f90003` | Read, Notify — 18-byte header, sent **before** its fragments |

**Frame Info (18 B, LE)**

```
u8  instance_id
u8  reserved0
u16 seq              driver frame counter
u16 device_frame     the DEVICE's counter — gaps mean we dropped one, not the sensor
u8  cols, rows
u8  planes           bit0 distance, bit1 amplitude, bit2 ambient
u8  flags            bit0 square format
u16 total_fragments
u16 payload_bytes
u16 temperature_raw
u16 capture_ms       an energy datum in itself
```

**Fragment**: `u16 seq, u16 frag_index` + **240 B**. A fragment with a new `seq` discards any
incomplete previous frame and counts a drop. No retransmission — at 2.5 fps a lost frame is
cheaper than a stall. **Report the drop rate in the UI**; it is the honest measure of whether
BLE keeps up.

### 7.2 Config service `53f91001`

| Char | UUID | Props |
|---|---|---|
| Config | `53f91002` | Read, Write — 16-byte packed struct |
| Command | `53f91003` | Write — `u8 opcode, u8 arg[3]` |
| Config Result | `53f91004` | Notify — `u8 opcode, i8 status, u8 detail[2]` |

**Writes are transactional**: validate everything, then apply, then notify. Never partially
apply — half a profile is how you get plausible rubbish.

### 7.3 Counting service `53f93001` *(both builds)*

| Char | UUID | Props |
|---|---|---|
| Count | `53f93002` | Read, Notify — the advertisement payload |
| Detection Config | `53f93003` | Read, Write |
| Calibration Control | `53f93004` | Write, Notify — progress then a quality summary |
| Background Model | `53f93005` | **Notify, paged** — *dev-stream only*. Cannot be a `Read`: GATT attributes cap at **512 bytes** and the model is 4.8 KB (corrected 2026-09-11). Reuse the fragmentation machinery. |

**Detection Config**

```
u16 fg_threshold_mm       default 300
u16 min_amplitude
u16 blob_zones_at_2m_min  range-normalised, not absolute
u16 blob_zones_at_2m_max
u8  temporal_n            default 2
u8  temporal_m            default 3
u16 lost_timeout_s
u16 dormant_timeout_s
u16 motion_threshold_mm
u16 report_period_s       0 = on change only
```

**Calibration Status**

```
u8  state          0 idle, 1 running, 2 done, 3 failed
u8  frames_done
u16 zones_reliable
u16 zones_total
u16 mean_bg_mm
u16 temperature_raw
```

### 7.4 Telemetry service `53f92001`

Health (FSM, `ERROR_CODE`, `ERROR_STATUS`, 5× `LDD_STATUS`, capture ok/fail) and Energy
(last capture ms, boot ms, frames, reboots). The bring-up diagnostics made remote. Push on
every failure and every 10th success.

---

## 8. Host software

### 8.1 The problem nobody has hit yet: Chrome cannot read advertisements

**This sits directly between two decisions and needs solving before phase 2.**

Counting mode broadcasts the count in an advertisement, and the UI is a Chrome page. But
**Web Bluetooth cannot scan for advertisements by default**:
`navigator.bluetooth.requestLEScan()` and `BluetoothDevice.watchAdvertisements()` sit behind
`chrome://flags/#enable-experimental-web-platform-features`. `VERIFY` against current
Chrome — but plan for it being true, because it has been for years.

So the browser handles **streaming** mode fine (that is a connection) and cannot handle
**counting** mode at all without a flag.

| Option | Cost | Verdict |
|---|---|---|
| Require the Chrome flag | zero code; fragile setup, breaks on any machine that has not set it | fine for your own bench, not for a demo |
| Connect in order to read counts | defeats the entire point of connectionless mode | no |
| **Node.js scanner bridging to the page over WebSocket** | ~half a day with `noble`; runs beside `npm run dev` | **recommended** |

The bridge is the honest answer: it becomes the gateway a deployed system needs anyway, it
can log counts for the paper without involving the browser, and it removes Chrome version
roulette from the experiment. **Design the page to take counts from a WebSocket regardless**,
so the source can be the bridge or a direct connection without the UI caring.

### 8.2 Next.js structure

```
web/
  app/page.tsx           single page
  components/
    ConnectButton        requestDevice — streaming mode
    FrameCanvas          <canvas> heatmap, ImageData + rAF
    DetectionOverlay     blob/track outlines over the heatmap
    CountPanel           the count, large; confidence, activity flag, history
    CalibratePanel       trigger, progress, reliable-zone percentage
    ConfigPanel          Tier 1 + 2
    HealthPanel          FSM, error bits, LDD — red when non-zero
    ObserverPanel        record "true count is now N", export CSV
    StatsBar             fps, drop rate, capture ms, zones valid
  lib/
    ble.ts               Web Bluetooth: connect, subscribe, write
    counts.ts            WebSocket client for the bridge
    protocol.ts          Frame Info decode, reassembly, config codec
  bridge/
    scan.js              Node + noble → WebSocket
```

Run: `npm run dev`, open `http://localhost:3000` in Chrome; `node bridge/scan.js` alongside.

### 8.3 Things that will bite

- **Chrome only.** Web Bluetooth is not in Firefox or Safari. Say so in the UI.
- **Secure context required** — `localhost` counts, so `npm run dev` is fine.
- **A user gesture is mandatory** for `requestDevice()`. Real button, not `useEffect`.
- **Declare every service in `optionalServices`** or `getPrimaryService` throws after a
  successful connect. Everyone hits this once.
- **Notification throughput is the real risk** — see phase 0.
- **Render off the React path.** Latest frame in a ref, draw with `ImageData` and `rAF`.
  Never put 2268 zones in component state.
- **No `navigator` during SSR.** All BLE behind `'use client'` + `useEffect`.

### 8.4 Ground truth

**Observer log, decided.** `ObserverPanel` records "true count is now N" against the
receiver's clock, exported as CSV.

**The cost, stated once:** an observer log cannot be re-scored. Every change to
`fg_threshold_mm`, the size model or the temporal window means repeating the experiment with
people in the room, and detection now has ~10 tunables needing empirical fitting. If that
starts eating sessions, a "record" button dumping frames is about a day, since frames
already arrive in `dev-stream`.

---

## 9. Firmware work

1. **Enable BLE**: `CONFIG_BT`, peripheral, `BT_CTLR_PHY_2M`, `BT_L2CAP_TX_MTU=247`,
   `BT_BUF_ACL_TX_SIZE=251`, DLE. The board already declares `HAS_BT_CTLR`. Expect
   RAM +30–40 KB against 77 KB used of 188 KB — and the 32 KB RTT buffer can shrink once BLE
   carries the diagnostics.
2. **Advertising**: manufacturer data, updated when the count changes; interval configurable.
3. **Streaming thread**, separate from `main()`. `bt_gatt_notify` returns `-ENOMEM` when
   buffers are full — wait on the callback, never spin.
4. **Config handlers**: stop → `wait_for_standby()` → apply → restart, reporting each step.
5. **Calibration**: capture, accumulate, quality report.
6. **Detection**: foreground, despeckle, components, range-normalised gating, motion,
   association, lifecycle.
7. **Two-tier scheduler**: watch tier, wake condition, track tier, fall back.
8. **`dev-stream` snippet**; the default build links no frame service.
9. **Keep RTT.** It is the only channel that works when BLE is what is being debugged.

---

## 10. Build order

| # | Phase | Done when |
|---|---|---|
| **0** | **Throughput spike.** Firmware notifies a counter flat out; a page measures kB/s. **Two days, and it decides the rest.** | A real number for Web Bluetooth throughput |
| 1 | GATT skeleton: config + telemetry, no frames | `nRF Connect` can read/write config; the sensor reconfigures |
| 2 | Advertising + the Node bridge | Counts arrive in a terminal with nothing connected |
| 3 | Next.js shell: connect, config, health | Configuration round-trips from the browser |
| 4 | Frame streaming: Frame Info, fragmentation, reassembly, drop counting | Frames arrive with a measured drop rate |
| 5 | Canvas heatmap + stats | The picture, live |
| 6 | Calibration: capture, model, quality report | An empty room produces a background and a percentage |
| 7 | Detection on the MCU; count sent with a matching `seq` | The UI shows a count **and** the blobs it came from |
| 8 | Two-tier duty cycling + energy panel | Duty scales with activity; the crossover is measurable |
| 9 | Deployed profile: counting only, frame service absent | `nm zephyr.elf` shows no frame-service symbol and its Kconfig is unset |

Phases 6–9 are the paper. 0–5 are the instrument that makes them measurable.

**Phase 0 is not optional.** Every later phase assumes a throughput number nobody has, and
if Web Bluetooth cannot sustain it the streaming design changes shape.

---

## 11. What could invalidate this plan

1. **The sensor never ranges reliably.** Currently blocking. Laser-fault hypothesis under
   test.
2. **Web Bluetooth throughput** below ~40 kB/s → distance-only, lower resolution, or
   request-a-frame instead of streaming.
3. **Segmentation does not survive real rooms.** With 3–5 people at oblique incidence and no
   head detection to fall back on, blob separation is doing all the work. **This is the risk
   most likely to decide whether the paper has a result.**
4. **Oblique background subtraction is too noisy** — a wall at a grazing angle returns
   little, and the reliable-zone percentage may be too low to work with.
5. **BLE and the 404 ms I²C read competing for CPU.** Probably fine — TWIM is DMA-driven —
   but unverified.
6. **The IMU capping the bus at 400 kHz**, leaving the track tier without margin.

---

## 12. Open questions

- **Calibration persistence** — see Status.
- **Confirming a track for someone already seated at power-up** — §6.6 item 4.
- **Coverage stitching.** `instance_id` identifies a node; it does not de-duplicate a person
  seen by two sensors, and one node's footprint does not cover a room. A design question of
  its own once multi-node is real.
- **Bonding**: assumed none. A deployed node broadcasting occupancy in the clear is a
  decision rather than an oversight — worth stating explicitly in the paper.
