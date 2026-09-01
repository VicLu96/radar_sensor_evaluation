# Implementation plan

Written 2026-08-31, restructured around Victor's four stages.

**Hardware is a given.** Multiple fabricated boards carrying the ISP2454-LL and the
VL53L9CX, Victor writes the Zephyr board files, logic analyser and Power Profiler Kit II
available, and he debugs hardware faults himself. This plan assumes the board works.

## The four stages

| Stage | Deliverable | Why in this order |
|---|---|---|
| **1. Driver** | ST ULD ported to Zephyr over I²C | Everything else is blocked on frames existing |
| **2. Telemetry** | Frames/counts over BLE to a web interface | Makes the system observable — you cannot tune what you cannot see |
| **3. Algorithm** | On-device people counting | Needs stage 2 to collect and validate against |
| **4. Power** | Per-domain gating, duty-cycle optimisation | **Last on purpose** — see below |

### Why power comes last, and why that is right

Optimising before the algorithm exists means optimising the wrong thing: you do not yet
know the frame rate, resolution or wake pattern the counting actually needs, so any
power architecture built now is a guess that later constrains the algorithm.

Stage 4 is also where the paper is. Doing it last means it is measured against a
*working* system rather than a stub, which is the difference between a real result and a
microbenchmark.

**One exception.** Stage 1 must measure the **firmware blob reload time and energy**,
because that number decides the entire stage-4 architecture and it is nearly free to
capture during driver work. See the crossover below.

---

## Stage 1 — The driver *(the big task)*

Full design in [`driver-port.md`](driver-port.md). Summary:

ST ships a platform-independent ULD in C. **We do not rewrite it** — we implement the
five platform functions it expects, wrap it in a Zephyr out-of-tree module, and expose
a device driver. A rewrite would mean owning ST's calibration and init sequencing, which
is a large amount of subtle work with no upside.

Steps:

1. Download **X-CUBE-53L9A1** from ST; keep it in `vendor/` (gitignored — licensed).
2. Out-of-tree Zephyr module skeleton: `zephyr/module.yml`, `Kconfig`, `CMakeLists.txt`.
3. **Devicetree binding** for the sensor on I²C, with the reset/interrupt GPIOs and the
   power-domain control lines the board provides.
4. **Platform layer** — ST's `RdByte`/`WrByte`/`RdMulti`/`WrMulti`/`WaitMs` mapped onto
   Zephyr's `i2c_write_dt` / `i2c_burst_read_dt` / `k_sleep`.
5. **Blob in flash**, streamed to the sensor at init. Measure this: time and energy.
6. Frame read, exposed as a Zephyr device API plus a raw-frame callback.
7. RTT/UART frame dump and a host-side viewer in `tools/`.

**Done when:** a 54×42 frame renders on a laptop, reproducibly from cold power-on, and
the blob load time is measured.

## Stage 2 — Telemetry to a web interface

The point is observability, not product polish.

- **Custom GATT service**: config (mode, resolution, rate), status, and a data
  characteristic. Frames are large — use notifications with a chunked frame protocol,
  and expect to need a **larger ATT MTU and 2M PHY** to move 9 KB in reasonable time.
- **Two data paths, deliberately separate:**
  - **Debug path** — raw frames, high bandwidth, used during development only
  - **Product path** — counts and events only, a few bytes
- **Web interface**: a browser page using **Web Bluetooth** talking directly to the
  device is the least-infrastructure option and works on desktop Chrome. A Raspberry Pi
  bridge to a small web app is the fallback if Web Bluetooth proves limiting.
- Live depth heat-map plus a count readout. This becomes the demo *and* the debugging
  tool for stage 3.

**Note for the paper:** the raw-frame path must be **compile-time removable**. The
privacy claim is that frames never leave the device; that has to be architectural, not
a runtime setting.

**Done when:** a browser shows a live depth map from the board.

## Stage 3 — People counting

Classical first, on-device, mirrored in Python for offline iteration on recorded frames.

1. Per-zone **background depth model** (running median), confidence-gated
2. **Foreground segmentation** against background
3. **Connected-component clustering**; centroid, area, min-depth per cluster
4. **Track association** across frames, with track lifetime
5. **Line-crossing count** with direction; occupancy as live track count

Record raw frames per scenario via stage 2 so algorithm changes can be re-run offline
without re-staging every walk-through. **Recorded scenarios are the reusable asset** —
stage each one once, carefully.

Scenario list is unchanged and is in [`../research/people-detection.md`](../research/people-detection.md);
the headline experiment is **two people abreast**, which is where 2268 zones earn the
part over a 64-zone one.

**Done when:** counts match ground truth on a scripted walk-through.

## Stage 4 — Power

The board can **gate each power domain separately during idle** — this is the lever the
whole paper turns on.

### The crossover that decides the architecture

Powering the sensor fully off saves idle current but forces a **firmware blob reload**
on the next wake. Reloading ~84 KB (VL53L8CX scale; VL53L9CX **VERIFY**) over I²C at
400 kHz is on the order of **2 seconds of bus activity**, which is not free.

So there is a threshold idle period `T*` where the two strategies cross:

```
E_reload  vs  P_standby x T

full power-down wins when   T > E_reload / P_standby
```

**Measure `E_reload` and `P_standby` in stage 1, then compute `T*`.** Below it, keep
the sensor in standby; above it, power the domain off. For a 1 Hz counter, `T` is one
second and standby almost certainly wins — but that is a prediction, and the measurement
is a genuinely publishable figure because nobody has it for this part.

### Sweep for the paper

- **Resolution** — every mode available *(Phase 0.2 unknown — see below)*
- **Frame rate** — 0.2, 0.5, 1, 2, 5 Hz
- **Duty strategy** — continuous low-rate vs. burst-on-event vs. full power-down
- **Peripheral instance** — the nRF54L15 has peripherals in different power domains, and
  Nordic's own data shows the low-power-domain instance costing less for the same
  transfer. Measure both; it is a free result.
- **BLE** — connection vs. connectionless advertising for a 1 Hz scalar

Output: energy per correct count, and projected battery life per operating point.

### Then validate

Multi-day deployment with independent ground truth, and a **measured discharge curve**
against the projection. A prediction that survives contact with a real battery is a much
stronger claim than either number alone.

---

## Still unknown — carry these into stage 1

These do not block starting, but each has a consequence worth knowing early.

| # | Question | Consequence |
|---|---|---|
| 1 | **Blob size and I²C load time** | Sets `T*` and the whole stage-4 architecture. **Measure during stage 1** |
| 2 | **Which reduced-resolution modes exist** | If 54×42 is the only one, the paper's central curve collapses to a point; fall back to frame rate as the swept axis |
| 3 | **Standby current, and whether the blob survives standby** | The other half of `T*` |
| 4 | **Max I²C clock** — sensor and nRF54L15 both | 400 kHz vs 1 MHz halves per-frame bus energy |
| 5 | **Can sensor and MCU rails be measured separately?** | The paper claims a per-component breakdown. Shared rail → shunt, or a weaker differential measurement stated as a limitation |

## Repository layout

```
firmware/
  drivers/vl53l9cx/   out-of-tree Zephyr driver wrapping ST's ULD
  app/                sampling, detection, BLE
  boards/             ISP2454-LL board definition (Victor)
tools/                host frame viewer, web interface, energy post-processing
vendor/               ST X-CUBE-53L9A1  (gitignored, licensed)
data/raw/             recorded frame sets  (gitignored, large)
docs/                 research and plans
notes/                YYYY-MM-DD.md session log
```
