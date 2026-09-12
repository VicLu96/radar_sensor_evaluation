# Roadmap: where this is, what the modes are, and what belongs to the paper

Written 2026-09-12. The one document to read before planning work. It supersedes
nothing — it points at the others.

---

## 1. Where this actually is

| Stage | State |
|---|---|
| **1. Driver** | **Done.** Ranges at 54×42, 2268 zones. Tag `working-54x42-2026-09-11`. |
| **2. Telemetry** | **Done.** BLE + web interface, live heatmap, config round-trip, recording. Tag `demo-2026-09-11`. |
| **3. Algorithm** | **Not started.** People detection and counting. This document plans it. |
| **4. Power** | **Not started.** The paper. |

### Measured, and therefore usable

| | |
|---|---|
| Full frame 54×42 | 14,842 B, ~334 ms at 400 kHz → ~2.5 fps |
| Firmware blob on cold start | **313 ms**, measured |
| Amplitude / ambient at 24×20 | 126 / 1, after the 12 MHz clock fix |
| BLE RX at the node | −40 dBm, 61–89 adverts per 3 s |
| Build sizes | FLASH 191 kB, RAM 114 kB of 188 kB |

### Not measured, and quoted nowhere as if it were

- **Full-resolution SNR.** Amplitude at binning 2 against the 126 at 24×20. Binning
  4→2 is a quarter the SPADs per zone, so ~32 is the *prediction*. **This decides
  whether the paper's resolution axis is a real axis.**
- **Anything at 54×42** — no valid-zone count, no frame rate, no capture time.
- **Every energy number.** Nothing has been on a Power Profiler yet.
- **Range bands per mode.** The four presets carry estimated exposures.
- **Zone orientation.** Still `VERIFY`; the community driver flips 180°.

---

## 2. Modes — what exists and what is planned

Two separate axes that are easy to confuse.

### 2.1 Measurement modes — exist today, web interface

How the sensor is configured. A mode sets context, exposure, switchover,
resolution and frame period together, because they are not independent.

| mode | band | context | exposure | resolution | ceiling |
|---|---|---|---|---|---|
| Close · fast | 5–50 cm | SHORT | 1 ms | 24×20 | ~11 fps |
| Close · detail | 5–50 cm | SHORT | 1 ms | 54×42 | ~3 fps |
| Desk / gesture | 10 cm–1.5 m | SHORT | 2 ms | 24×20 | ~11 fps |
| Room detection | 0.5–4 m | LONG | 4 ms | 54×42 | ~3 fps |
| Long range | 2–9.6 m | LONG | 16 ms | 54×42 | ~3 fps |

See [measurement-range.md](measurement-range.md).

### 2.2 Detection modes — PLANNED, stage 3

What the firmware *does* with a frame. This is the axis that does not exist yet.

| # | mode | what runs | what leaves the device | state |
|---|---|---|---|---|
| **D0** | **Raw** | nothing | frames | **done** — this is today |
| **D1** | **Calibrate** | 16 frames → background model, quality report | progress + reliable-zone % | planned |
| **D2** | **Detect** | background subtraction, blobs, tracks, count | frames **and** count + blob overlay | planned |
| **D3** | **Count** | the same detection | **count only, 8 bytes, in an advertisement** | planned |

**D2 and D3 run identical detection code.** The difference is only whether the
frame service is compiled in. That is the privacy claim: not a policy, a build.

```
CONFIG_APP_BLE_FRAME_SERVICE=y   →  D0/D1/D2 available   (dev-stream)
CONFIG_APP_BLE_FRAME_SERVICE=n   →  D3 only              (deployed)
```

Checkable, and the check matters: `grep` the generated `.config` and `nm` the ELF
for the frame-service symbol. **Not** `strings | grep <uuid>` — `BT_UUID_128_ENCODE`
emits binary, so that grep can never match and would "pass" on a build that
streams frames happily.

### 2.3 Duty-cycle tiers — planned, part of D2/D3

Not a user mode; an automatic behaviour underneath detection.

| tier | rate | job |
|---|---|---|
| **Watch** | 0.05–0.2 Hz | Are dormant blobs still there? Anything new? Background subtraction only. |
| **Track** | 1.5–2.5 fps | Association and motion. Runs **only while something moves.** |

**This is structural, not an optimisation.** A person walks 1.4 m/s; at 0.1 Hz
they move 14 m between frames and no correspondence is possible. At 400 kHz the
track tier sits *exactly* at the bus ceiling with no margin — which is why
[i2c-fast-mode-plus.md](i2c-fast-mode-plus.md) and the 1 kΩ pull-ups are on the
critical path for stage 3, not just nice to have.

**And it is the paper's most interesting result**: duty cycle scales with
*activity*, not with time. The energy cost of occupancy sensing becomes a
function of how much the occupants move.

---

## 3. The flow, end to end

```
  power on
     │
     ├─ BLE advertises "water-sense-tof"        ← sensor NOT powered yet
     │
     ├─ a client connects ──────────────────────────────────────────┐
     │                                                              │
     ├─ sensor powers up, boots (313 ms blob upload)                │
     │                                                              │
     ├─ streaming arms; frames flow once the client SUBSCRIBES      │
     │                                                              │
     │   ┌─ measurement mode ──→ context, exposure, resolution      │
     │   ├─ detection mode ────→ D0 raw … D3 count        (planned) │
     │   └─ recording ─────────→ CSV or .wstof, config in header    │
     │                                                              │
     └─ disconnect ──→ streaming stops, sensor stays booted ────────┘
```

Deployed (D3) skips the whole middle: advertise a count every 1–10 s, connect
only to reconfigure, no frame service in the binary.

**Firmware switches that gate the flow today:**
`APP_BLE_FIRST` (wait for a connection before powering the sensor),
`VL53L9CX_DEFER_BOOT`, `APP_BLE_AUTOSTREAM`, `APP_BLE_FRAME_SERVICE`.

---

## 4. Demo versus paper — the separation

Everything runs through one firmware and one web interface. **The separation is
not in the code, it is in what a number is allowed to be used for**, and it has to
be explicit or demo-grade numbers will leak into the paper.

### The rule

> **A figure is a paper figure only if it came off the bench under a recorded
> configuration.** Anything read off the live UI, any preset, any estimate, and
> anything measured with a `TEMPORARY` switch enabled is a **demo figure** and
> must not be cited.

This is `CLAUDE.md`'s "every figure carries its source and date", made operational.

### What is which

| Feature | Demo | Paper | Notes |
|---|---|---|---|
| Live heatmap | ✅ | — | It is the picture, not a measurement |
| Measurement-mode presets | ✅ | ⚠️ | Exposures are **estimates** until measured |
| Mode band labels ("5–50 cm") | ✅ | ❌ | What a mode is *good at*, not a characterisation |
| Frame-rate ceiling in the UI | ✅ | ❌ | Derived from the I²C read; an **upper bound** |
| Stats bar fps / kB/s / drop rate | ✅ | ⚠️ | Real, but transport-dependent — use `capture_ms` |
| `capture_ms` per frame | ✅ | ✅ | The device's own figure. Citable |
| Zone-validity %, amplitude split | ✅ | ✅ | Citable **from a recording**, not from the screen |
| **Recordings (CSV / .wstof)** | ✅ | ✅ | **The bridge.** Carries its own conditions |
| Power Profiler traces | — | ✅ | The only source of energy numbers |
| Observer ground-truth log | — | ✅ | Accuracy scoring |

### Switches that must be OFF for any paper measurement

| switch | why it invalidates a measurement |
|---|---|
| `CONFIG_VL53L9CX_EXPOSURE_BACKOFF` | **Still on.** Halves exposure on a laser fault, so the device silently changes the variable being measured |
| `CONFIG_APP_BLE_FIRST` / `DEFER_BOOT` | Bring-up ergonomics; a node that will not range unattended is not the deployed node |
| `CONFIG_APP_LOG_FULL_GRID` | ~9 kB of RTT per capture, in immediate mode, on the calling thread |
| `CONFIG_APP_BLE_AUTOSTREAM` | Free-running capture is not a controlled duty cycle |
| `CONFIG_BT_CTLR_TX_PWR_PLUS_8` | +8 dBm roughly triples radio TX current against 0 dBm |

**A `measurement` build profile should exist before stage 4** — all of the above
off, frame period explicit, RTT quiet — so that "was the backoff on?" is never a
question asked of a dataset afterwards.

### Where they legitimately share

The demo is the instrument the paper is measured with, and that is fine:

- **The recording format is the bridge.** Every file carries its full
  configuration including mid-recording changes, which is exactly what makes a
  demo session re-usable as data.
- **The detection algorithm is identical in D2 and D3.** Scoring it against
  streamed frames is only valid because the deployed build runs the same code.
- **The measurement modes are the paper's independent variable.** They exist for
  the demo; the sweep uses the same mechanism.

---

## 5. Stage 3 — people detection, ordered

Full design in [ble-streaming-and-web-ui.md](ble-streaming-and-web-ui.md) §6.
Ordered here by what unblocks what.

**Prerequisites, both outstanding:**

1. **Measure full-resolution SNR.** If amplitude at binning 2 is unusable, the
   whole resolution axis changes shape and detection should be developed at
   24×20 instead. **Do this first — it is one bench session and it can invalidate
   a month of work.**
2. **1 kΩ pull-ups → 1 MHz bus.** The track tier has no margin at 400 kHz.

**Then:**

3. **D1 Calibrate.** 16 frames, per-zone mean of valid distances, exclude zones
   below 75% validity, report the reliable-zone percentage. That percentage is
   itself a result: *"1,932 of 2,268 zones usable (85%)"* tells you the mount is
   wrong before the counts do — and oblique incidence is expected to be much
   worse than overhead.
4. **Record a scenario library.** Empty room, one person still, one walking, two
   abreast, two crossing, someone sitting down. **Now possible because recordings
   exist** — and it converts detection tuning from "repeat the experiment with
   people in the room" into fitting against a fixed dataset.
5. **D2 Detect**, offline first against recordings, then on-device: foreground →
   despeckle → connected components → range-normalised size gate → motion →
   association → track lifecycle.
6. **Two-tier duty cycling.**
7. **D3 Count** and the deployed profile.

### The three risks worth naming now

- **Blob separation is doing all the work.** Corner mount means no head
  detection, so two people abreast either separate or they do not. §11 of the BLE
  plan calls this the risk most likely to decide whether the paper has a result,
  and nothing in the current design splits merged people.
- **Background subtraction at grazing incidence.** A wall at an oblique angle
  returns very little. The reliable-zone percentage from D1 is the early warning.
- **Never adapt the background under a live track.** Standard background models
  absorb static objects; do that and **a person sitting still vanishes from the
  count** — the exact failure this design exists to prevent.

---

## 6. Stage 4 — the paper

Contribution and venues in [paper.md](paper.md). Three claims, each measured:

1. **Energy breakdown per frame** — integration, I²C transfer, processing, radio
   — at every resolution and rate.
2. **Accuracy versus zone count**, with the point of diminishing returns. The
   useful finding would be that counting saturates well below 2268 zones.
3. **A measured multi-month battery operating point**, with a discharge curve
   rather than a spreadsheet projection.

Plus the one experiment that justifies the sensor: **two people walking abreast**,
resolved at high zone counts and merged at low ones.

### What has to be true first

- Every `TEMPORARY` switch off, under a `measurement` profile.
- **Sensor and MCU rails measured separately.** The Power Profiler does one rail
  at a time, and the whole claim is a per-component breakdown. *Open question for
  Victor: can the board do this?*
- The 1 MHz bus, if the accuracy-versus-rate axis is to reach useful rates.
- **The 9.6 m gate stated as a limitation.** UM3683 §2.6.1 fixes the ranging
  period at 64 ns, which bounds corner tilt to ≳41° and coverage to ~10 m².
  Reviewers will find it; better to own it.

### The correction that changes the framing

`paper.md` opened by calling 150 mW "high enough that battery operation is not
obviously viable". **The real figure for this profile is 450–800 mW** (UM3683
Table 23, verified 2026-09-11). The premise gets *stronger* — at 3–5× the assumed
draw the viability question stops being rhetorical — but **every duty-cycle and
battery-life figure derived before that date is wrong by the same factor** and
must be recomputed rather than adjusted.

---

## 7. The next three things

1. **Measure full-resolution SNR and the range presets.** One bench session,
   recorded. Unblocks stage 3 and produces the first citable numbers.
2. **1 kΩ pull-ups**, then 1 MHz. Victor's hardware change; on the critical path.
3. **A `measurement` build profile**, so the demo and the paper stop sharing a
   configuration that has `EXPOSURE_BACKOFF` in it.
