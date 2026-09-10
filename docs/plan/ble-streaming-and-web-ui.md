# Plan: BLE frame streaming and the Next.js configuration UI

Written 2026-09-10. **Nothing here is implemented.** This is the design to build against.

---

## 0. The rule this crosses, and how to keep it intact

`CLAUDE.md`: *"Counts leave the device, frames never do. The privacy claim is architectural
and free — do not add a raw-frame transmit path."*

This plan streams raw frames. That is a real conflict and it needs a deliberate answer, not
a footnote.

**The answer: two firmware profiles, and the privacy claim attaches to one of them.**

| | `dev-stream` | `deployed` |
|---|---|---|
| Frames over BLE | yes | **never** |
| Counts over BLE | yes | yes |
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

### Commands, not settings

`START`, `STOP`, `SINGLE_SHOT`, `REBOOT`, `RECALIBRATE`.

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

### 3.4 Telemetry service — `53l92001-…`

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

**Phase 0 is not optional.** Every later phase assumes a throughput number nobody has yet,
and if Web Bluetooth cannot sustain it the whole streaming design changes shape. Two days
spent there is cheap against rewriting phases 3–5.

---

## 7. What could invalidate this plan

1. **Web Bluetooth throughput** below ~40 kB/s. Mitigation: distance-only (4,536 B), lower
   resolution, or request-a-frame instead of streaming.
2. **The sensor still does not range reliably.** Everything here assumes frames exist.
   Streaming a fault is not progress — finish the laser/supply question first.
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
- **Log frames to disk from the browser?** Useful for the paper — the File System Access
  API can stream to a file. Adds scope; worth deciding before phase 4.
