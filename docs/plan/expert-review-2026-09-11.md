# Expert review of the plan, 2026-09-11

Three specialist reviews commissioned at Victor's request: a **BLE systems** expert, a
**people detection and tracking** expert, and a **dToF sensor physics** expert. Each read
[ble-streaming-and-web-ui.md](ble-streaming-and-web-ui.md) adversarially.

**Everything marked ✅ VERIFIED below I checked myself against UM3683 Rev 3 or the SDK
before recording it.** The reviews also contained claims I have not verified; those are
marked accordingly. Several findings are corrections to my own work.

---

## The three that change the project, not the plan

### A. ✅ VERIFIED — the sensor cannot range beyond 9.6 m. Ever.

UM3683 §2.6.1, verbatim:

> *"The cycle consists of a fixed ranging period of **64 ns (which corresponds to a maximum
> ranging distance of 9.6 m)** followed by a blanking period."*

`64 ns × c/2 = 9.6 m`. This is a **gate**, not a link-budget limit: a photon arriving after
it closes is never counted. It explains the bench measurement exactly — valid returns spanned
**0.9–9.5 m**, which is the gate, not the room.

**Consequence for the corner mount.** The floor is inside the window only where
`sin δ > h/9.6`. At h = 2.7 m that is **δ > 16.3°**. At 30° tilt, 7.3° of the 42° vertical
field of view — **~17% of rows, ~390 zones** — can never return anything. They are blind, not
noisy.

**So tilt is bounded from below by physics.** For margin, δ_min ≈ 20° forces **tilt ≥ ~41°**,
which gives the ~3.0–6.6 m slant span and a floor patch of roughly **10 m²** — only ~1.6×
the ~6 m² overhead footprint that `room-occupancy.md` already called insufficient for a room.

**Action: delete the 30° option from the design space.**

### B. ✅ VERIFIED — 150 mW is the wrong profile. Ours draws 450–800 mW.

UM3683 Table 23, verbatim:

| | VR headset | Outdoor lidar |
|---|---|---|
| Description | Precision mode | **Ambient mode** |
| Exposure | 5 ms | **16 ms** |
| **Power** | **150 mW** | **450–800 mW** |
| Context | Short | **Long** |
| Switchover | 325 | **650** |
| RTN short offset | 0 | **2** |
| Step number | 7 | **6** |
| Power mode | Ultralow power | **Regular** |

**The good news, and I verified this by comparison: our firmware implements the ambient
profile correctly and completely.** Exposure 16 ms ✓, context LONG ✓, switchover 650 ✓,
rtn_short_offset 2 ✓, DSS_LONG ✓, step number 6 (reset) ✓, power mode Regular ✓, and
`set_context(LONG)` supplies cal_prog_offset −8/−1 ✓. Nothing is misconfigured.

**The bad news: every energy figure in this repository is anchored to 150 mW, and that is the
5 ms precision profile.** `CLAUDE.md`'s hard rule — *"150 mW active sensor"* — is the wrong
number for the profile we run. The multi-block battery claim is **3–5× optimistic**.

Note also that ST gives a *range*, 450→800 mW: DSS opens the SPAD array up under ambient
light, so **power draw is a function of how sunlit the room is.** That is a genuinely
interesting paper result and a hazard for the budget.

**Action: strike 150 mW from the plan and `CLAUDE.md` until a measured figure replaces it.**

### C. NOT VERIFIED, but the arithmetic is sound — full resolution may have no SNR at all

Binning 54×42 → 12×10 sums ~19 native zones. Signal ×19, ambient ×19, so detection SNR
improves by **√19 ≈ 4.4×**.

The bench measurement — amplitude **9** against ambient **13**, 73–103 of 120 zones valid —
was taken **at 12×10**. That is the *binned, best case*. At native 54×42 the same scene gives
per-zone amplitude below 1 LSB.

If that holds, it undermines the project's central premise: 2268 zones, the zone-footprint
table, the range-normalised size gate, the 14,842-byte frame, the 404 ms read, the 2.5 fps
ceiling, and the whole 1 kΩ pull-up argument.

**This is a one-hour measurement, and it should come before anything else in this document:**
capture the same static scene at 12×10 and at 54×42 and compare per-zone amplitude against
ambient. Also ✅ VERIFIED from Table 22: **DSS computation at full resolution costs 13.5 ms
per frame**, in parallel with exposure — so there is a fixed ~13.5 ms floor at 54×42 that no
exposure reduction can go below.

---

## Findings by discipline

### dToF physics

| | Finding | Status |
|---|---|---|
| **C1** | 9.6 m gate bounds tilt — see A | ✅ VERIFIED |
| **C2** | **Occlusion is geometric, not a segmentation problem.** From 2.7 m, a 1.7 m person at horizontal distance d shadows the floor from d to **2.7 d** — a person at 5 m shadows out to 13.5 m, i.e. the rest of the field of view. One standing person can delete the detection volume for everyone behind them. | arithmetic sound |
| **C3** | Power profile — see B | ✅ VERIFIED |
| **C4** | **Class 1 compliance is a function of registers, not silicon.** UM3683 §2.6.1: blanking *"is defined based on the duration of the optical pulse as well as its power to comply with the power limits of laser Class 1"*. Tier 3 exposes profile registers over an **unbonded** BLE link on a 940 nm emitter. Also: rebooting forever around a laser safety interlock defeats the interlock. | ✅ VERIFIED (the quote) |
| **C5** | Full-resolution SNR — see C | needs measurement |
| **M1** | **Floor return falls as 1/r³, not 1/r².** For a floor, `cos θᵢ = h/r`, so returned power ∝ ρ·h/r³ — **−10.5 dB from near rows to far rows** across a 45° FoV. Expect **35–50% of zones usable**, not the 85% used illustratively. But a person's torso is near-vertical and falls off only as 1/r², so at the far edge **a person can be brighter than the background needed to detect them** — and the plan's flow excludes exactly those zones. Fix: add a second foreground rule — *"was invalid, now valid at a plausible in-gate range"* is foreground. | plausible, unverified |
| **M2** | Ambient is not fixable by exposure: the 16→30 ms headroom is only **√(30/16) = 1.37×**. Frame **averaging** at the watch tier gives √N on a genuinely static scene — a clean, tunable energy-vs-detection axis the plan lacks. Use the **ambient plane** as the discriminator: Δdistance significant AND Δambient small ⇒ a person, not sunlight. | sound |
| **M3** | **Multipath poisons calibration, not frames.** A bistable multipath zone alternates between two ranges; a 16-frame **mean** lands on a value that never occurs, so `bg − dist` exceeds 300 mm about half the time forever — a stable population of false blobs at floor-wall corners and on shiny floors, indistinguishable from seated people. No histogram is exposed, so it cannot be detected downstream. **Fix: use the median, store per-zone MAD, reject high-MAD zones.** | sound and cheap |
| **M4** | Skipping ST's calibration: per-pixel offsets **do** cancel in background subtraction (the plan is accidentally right), but **`RAD2PERP_FOV_GAIN` does not** — at the FoV corner, 1/cos(34.2°) = **1.21, a 21% error ≈ 800 mm at 4 m**, which is 2.7× the foreground threshold. It cancels per-zone but not in the size model, the floor geometry, or the association metric. Amplitude calibration is skipped too, so `min_amplitude` is a global constant against an uncorrected plane. | sound |
| **M5** | **Duty cycling has three rungs, not two**: ultralow-power between frames (✅ VERIFIED Table 22: **LP exit 3.5 ms**), STANDBY with the blob resident (`COMMAND 12`, fast restart), and full power-down (306 ms reload). Crossover: `T* ≈ 0.306 s × P_boot/P_standby`. Estimated **T* ≈ 61 s**, so at 0.1 Hz **standby wins by ~6×** and *"full power-down is clearly correct"* is backwards. | needs P_standby |
| **M6** | The intermittent streaming stop may be **supply, not optics**: `ERROR_STATUS` bits are mostly supply faults (VHV under/over-voltage, SPAD supply overload, current limit). Ambient mode at 450–800 mW on 3.3 V is 136–242 mA average with much higher pulsed peaks. **Predicts the fault worsens as exposure and duty rise — i.e. the Phase 8 energy sweep will trigger it systematically.** | consistent with the bench |
| **m1** | Breathing detection is dead at room range — 4–12 mm against 50–200 mm σ needs ~280× more integration. Remove it from the approach list. | sound |
| **m2** | No temperature compensation. Free fix: per-frame **global auto-zero** — median of `(bg − dist)` over reliable, untracked zones, subtracted. Also absorbs cover-glass crosstalk drift. | sound and cheap |
| **m3** | Zone footprint is **anisotropic**: 35 mm cross-range vs **0.52 m along-range at 9 m** — 15:1. The size model treats zones as square. | sound |

### People detection and tracking

| | Finding | Status |
|---|---|---|
| **C1** | **Nothing splits merged people, and merging is the normal case.** The 3×3 majority despeckle *fills* the 1–3 zone gap between two adjacent people, then 8-connectivity — the most merging option — labels them as one blob, and segmentation runs on a **binary mask** that has discarded the 20–60 cm depth step separating them. The size gate cannot recover it: a two-person blob either exceeds max (count 0) or fits (count 1). **Fix: open-only despeckle, 4-connectivity, a depth-similarity join predicate, and an explicit split stage.** | sound — this is the top algorithmic defect |
| **C2** | **The pipeline never leaves the zone grid.** Projecting to world coordinates and a **plan-view height map** (Harville-style, the canonical solution for oblique depth people-tracking) costs ~7k MACs — under 0.1 ms — and dissolves four of the plan's own open problems at once: the size model becomes metres, height-above-floor priors become available, geometric wake gating becomes possible, and a **synthetic geometric background** (where each ray meets the floor) comes free. | strongly recommended |
| **C3** | **`dormant_timeout` must be simultaneously > 3 hours and < 5 minutes.** It must hold a still person for hours; but a coat left inside a dormant footprint freezes those zones forever and the room reports a phantom occupant all day. **A timeout cannot answer "is this static blob a person?"** `room-occupancy.md` had the right answer — depth micro-variation — and the newer plan dropped it. But see dToF **m1**: micro-variation is not available at room range either. **This is an unsolved problem, not a parameter choice.** | genuine open problem |
| **C4** | Permanently excluding low-validity zones creates **silent blind regions exactly where the corner mount needs to see** — and with only 16 samples the 75% gate is not reproducible between calibrations (a zone at true 80% has ~coin-flip odds of scoring ≤12/16), so neither is the install metric. **Fix: exclude from background subtraction only, never from detection; use 32–64 frames.** | sound |
| **C5** | A **fixed** 300 mm threshold both misses and false-fires. Misses: someone seated with their back 0.3 m from a wall gives `bg − dist ≈ 250 mm` — never detected, and that is a common seating arrangement. False fires: far-field σ is 100–300 mm, so 300 mm is 1–3σ there. **Fix: store per-zone σ (2.3 KB) and use `max(floor, k·σ_z)`.** | sound, cheapest high-value fix |
| **C6** | No occlusion **state**. A track occluded by another person ages to `LOST` and the count decrements. Needs an `OCCLUDED` state driven by a shadow-volume test. | sound |
| **M1** | The size model is wrong in the **row** direction — vertical extent depends on height, mount and *position in the FoV*, not range, so a person spans ~26 rows near and ~15 far. A gate tuned mid-range **rejects the same person as they walk closer.** Seated people have 3–4× less area than standing. | sound |
| **M2** | Greedy nearest-neighbour at 1.5–2.5 fps is at its breaking point (0.56 m/frame vs 0.6–1.2 m spacing). The count-changing failure is **merge/split**, not ID switch. Fixes: an α-β predictor, global (Hungarian) assignment — microseconds for ≤8×16 — and **change the count only on boundary entry/exit events**, never on mid-room appearance. | sound |
| **M3** | The watch tier **does** need association — deciding "this blob is that dormant track" *is* association — and it has no motion signal at all, so it can never confirm. | sound |
| **M4** | **The 3-hour arithmetic is unforgiving**: 1080 watch frames, and a 10⁻³ per-frame drop probability gives a ~66% chance of losing the person. **Fix: make the count sticky — the watch tier may maintain but never decrement; decrementing requires a positive exit event.** | sound and important |
| **M5** | **The watch tier runs full resolution** — 404 ms of bus to answer "did anything change?". Running it at 12×10 (880 B, ~18 ms) or 18×14 is a **10–22× cut in the tier that is >93% of the duty cycle.** The single largest energy lever, and absent from the plan. *(Caveat: measure the STANDBY→config→restart cost first — if a mode switch costs ~300 ms, hopping per-frame is a net loss.)* | strongly recommended |
| **M6** | **Can the distance plane be read alone over I²C?** If the planes are separately addressable, 404 ms → ~140 ms, **7 fps at 400 kHz** — which resolves the track tier's margin, the association rate, the pull-up question and the 1 MHz question together. Half an hour to check. | highest leverage per hour |
| **M7** | The **IMU already on the board** measures gravity, so it gives mount tilt to <1° and detects the node being knocked — currently an undetectable silent failure. And the calibration frames contain the floor: fit a plane and recover height *and* tilt from the data. Self-calibrating extrinsics is a nice systems result. | sound |
| **M8** | **The energy and accuracy claims conflict.** For five people sitting still, activity ≈ 0 and *"duty scales with activity"* reduces to "we ran the sensor slowly". The honest and better result: **the energy cost of occupancy sensing in a dwell environment is dominated by re-verifying stillness, not by tracking motion.** | reframing worth taking |
| **m1** | The memory figure is ~3× low — it omits the 14.8 kB frame buffer and the previous distance plane. Realistic ~47 KB with all additions, against ~25 KB of slack after BLE. Fits, but not comfortably, and `CONFIG_ARM_MPU=n` means an overrun corrupts silently. | sound |
| **m2** | `confidence` is declared everywhere and **defined nowhere**. | mine to fix |
| **m3** | No check for a **person present during calibration** — and the foreground test is one-sided, so their zones become permanently dead. The plan has no negative-foreground test. | sound |
| **m4** | Frame recording deferred is a **methodology defect**: ~10 tunables cannot be fitted with an experiment that runs once per setting. Build it before phase 7. | sound |

### BLE

| | Finding | Status |
|---|---|---|
| **C1** | **My UUID base is not a UUID.** `53l9XXXX-…` — `l` is not a hex digit. And the phase-9 privacy gate `strings zephyr.elf \| grep 53l90002` **could never work anyway**: `BT_UUID_128_ENCODE` emits binary, not ASCII. The gate passes vacuously, and the plan cites it as architectural proof. | ✅ my error, confirmed |
| **C2** | `CONFIG_BT_CTLR_PHY_2M` is only a **capability**. `BT_AUTO_PHY_PERIPHERAL_NONE` is the default, so the peripheral never initiates a PHY update — the link may run at **1M, halving every throughput figure**. And `BT_CTLR_DATA_LENGTH_MAX` defaults to **27**, so DLE negotiates 27 regardless of `BT_BUF_ACL_TX_SIZE`. | needs `BT_AUTO_PHY_PERIPHERAL_2M=y`, `BT_CTLR_DATA_LENGTH_MAX=251` |
| **C3** | 105–140 kB/s is not reachable. Defaults: connection interval **30–50 ms**, event length 7.5 ms, `BT_BUF_ACL_TX_COUNT=3`. That is ~**40 kB/s** at the controller, before Chrome's per-notification IPC. **Treat distance-only as the design point, not the fallback.** | sound |
| **C4** | **Advertising stops the moment anyone connects.** With one advertising set, counts stop reaching the bridge while any device is connected — and with no pairing, anyone can connect and hold the link. A one-line denial of service. **Fix: advertise the count non-connectable, and make connectability a bounded window.** | sound and serious |
| **C5** | **The Background Model cannot be a `Read`** — GATT attributes cap at **512 bytes** and it is 4.8 KB. | ✅ my error |
| **M1** | **My energy justification for connectionless is wrong.** Per `CLAUDE.md`'s own rule, the sensor dominates: watch tier ≈ 6.75 mW at 150 mW-equivalent, advertising is tens of µW. BLE is **under 1% either way**. And per event a connection is *cheaper* than connectable advertising (1 RX vs 3 TX + 3 RX). What kills streaming is that 2.5 fps means the **sensor** runs ~100% of the time. Justify connectionless on N-nodes-to-one-listener, no supervision blackouts, no pairing — not on energy. | ✅ my error |
| **M2** | **"No NVS" forecloses bonding and privacy.** Bonds and the IRK live in `settings`. Without them: re-pair every boot (and Windows caches stale keys, so a human must "Remove device" after every flash), and no resolvable private address — so a **static, permanently trackable MAC** alongside a stable `instance_id` and a live occupancy count. | needs a decision |
| **M3** | Unauthenticated `REBOOT` in a loop means the node never counts — and since calibration does not persist, **every reboot destroys the background model**. Cheapest fix: do not compile the config service into the deployed build at all. | sound |
| **M4** | Hand-rolled fragmentation is right, but because **Web Bluetooth exposes no L2CAP API** — not for the reason given. And the link layer already retransmits, so the "drop rate" measures *our own starvation and Chrome's event loop*, not the link. Label it correctly or the paper's number misleads. Derive fragment size from the **negotiated** MTU, not a hard-coded 240. | sound |
| **M5** | **The advert payload is 8 bytes, not 10** (1+1+1+1+1+2+1). And the 31-byte budget is tighter than claimed: Flags (3) + manufacturer element (4+8) = 15, plus a name ~9 = 24 — a 128-bit service UUID is 18 bytes and **does not fit**. Put it in the scan response, or filter on `manufacturerData`. | ✅ my arithmetic error |
| **M6** | `report_seq` without an **age** field leaves up to a full advertising interval of unbounded error against the observer log — a systematic error of up to 10 s in the paper's core accuracy result. **Fix: one byte, `secs_since_change`.** | sound |
| **M7** | Streaming needs a **second frame buffer** (another 14.8 kB) — you cannot read frame N+1 into the one you are still notifying from. | sound |
| **M8** | Three Web Bluetooth traps absent from the plan: **GATT service caching on Windows** (stale table until the device is removed from Bluetooth settings — and this will bite on every GATT change across phases 1, 4, 6, 7), *"GATT operation already in progress"* needing a serialised queue, and **object invalidation on reconnect**. | sound |
| **M9** | `watchAdvertisements()` may be **unflagged** (unlike `requestLEScan()`) and would let the browser read counts from one already-chosen node with no bridge — worth 30 minutes to check. But `noble` on Windows is its own trap; **run the bridge on Linux/BlueZ**, which is also the gateway a deployed system needs. | worth checking |
| **m1–m7** | Legacy advertising is right (state why); a GATT server cannot initiate MTU exchange; the air-time model double-counts the MIC; express advertising cost in **µJ per event**, not duty-%; `0xFFFF` collides with every other unbadged device so the bridge must filter; **`battery_pct` has no hardware source**; and §11 item 5 is resolvable now — the SoftDevice Controller uses zero-latency IRQs and TWIM is DMA-driven, so the 404 ms read cannot break the link. | mostly mine to fix |

---

## What all three cleared

Worth recording, because it is the part that does not need rework:

- **Motion promotes, background holds** — the right shape, and *"never adapt the background
  under a live track"* is the trap most implementations miss. Necessary but not sufficient.
- **Head detection is impossible from a corner** — correct, correctly reasoned, and stated
  rather than fudged.
- **On-MCU counting, counts-only** — correct on energy and privacy, and the arithmetic behind
  it holds.
- **Our register profile is ST's ambient profile, implemented completely** — verified above.
- **I²C is the bottleneck, not BLE**, and trimming the BLE payload cannot raise the frame
  rate. Often missed.
- **4.7 kΩ is out of spec at 400 kHz** — correct as stated.
- **`report_seq` in a stateless advertisement** — correct and routinely forgotten.
- **No device clock; receiver stamps** — right call.
- **Transactional config writes**, and the STANDBY sequencing.
- **`device_frame` alongside `seq`** so a gap distinguishes our drop from the sensor's.
- **Reporting reliable-zone percentage as an install-quality metric** — the right instrument,
  and it will fire early.
- **Phase 0 as a non-optional gate** — called the single best decision in the plan.
- **Keeping RTT** — the only channel that works when BLE is what is broken.
- **§11's own risk ranking** — "segmentation may not survive real rooms" was correctly
  identified as the top risk. The criticism is that §6 did not act on it.

---

## The measurements that should come before any more design

Ranked by how much they change:

1. **Amplitude and ambient at 12×10 versus 54×42**, same static scene. Decides whether full
   resolution is usable at all — i.e. whether the project's premise holds. *One hour.*
2. **Maximum range on a person in dark clothing**, walking slowly away, at both resolutions.
   The dToF reviewer estimates **2–4 m at 12×10 and under ~1.5 m at 54×42**. If that lands
   anywhere near, the corner mount cannot count across a room. *One hour.*
3. **Can the distance plane be read alone?** 404 ms → ~140 ms if yes. *Half an hour.*
4. **`ERROR_STATUS` / `LDD_STATUS` at the streaming stop** — already built, just needs a run.
   Distinguishes a supply fault from an optics fault, and they share no fix.
5. **Measured active power** at 16 ms exposure. Replaces a 150 mW figure that is 3–5× wrong.
6. **Standby current**, to settle `T* = 0.306 s × P_boot/P_standby` and whether full
   power-down is right at all.
7. **Mode-switch cost** (STANDBY → reconfigure → restart). Decides whether per-tier
   resolution switching is a 10–22× win or a net loss.
