# radar_sensor_evaluation

Low-power people counting on a **VL53L9CX** time-of-flight sensor and an
**ISP2454-LL** (Nordic nRF54L15) BLE module, in Zephyr — built toward a paper on the
energy-accuracy trade-off of high-resolution dToF sensing.

> **Two notes on the name.** The VL53L9CX is an *optical* direct time-of-flight sensor
> (SPAD array, 940 nm VCSEL), **not a radar** — which changes both its failure modes and
> the literature that applies. And the `claude/stocks-2vn27c` branch holds an unrelated
> stock-price CLI produced by a misdirected prompt; radar work lives on `main`.

## Status

**Research and planning. No firmware yet, no measurements yet.**

## Start here

| File | What it is |
|---|---|
| [CONTEXT.md](CONTEXT.md) | Current state and the ordered TODO. Read first |
| [DECISIONS.md](DECISIONS.md) | Append-only: what was decided and why |
| [docs/plan/implementation.md](docs/plan/implementation.md) | Six phases, test rig, scenarios, risks |
| [docs/plan/paper.md](docs/plan/paper.md) | The contribution, venues, what threatens it |

## Research

| File | What it is |
|---|---|
| [docs/hardware/sensor-vl53l9cx.md](docs/hardware/sensor-vl53l9cx.md) | Sensor specs, I²C bandwidth analysis, failure modes |
| [docs/hardware/mcu-isp2454ll.md](docs/hardware/mcu-isp2454ll.md) | nRF54L15 module, power domains, toolchain |
| [docs/research/people-detection.md](docs/research/people-detection.md) | Mounting geometry, classical pipeline, literature |
| [docs/research/applications.md](docs/research/applications.md) | Ranked demos, and what was rejected |

## The three numbers that shape the design

- **2268 zones** (54 × 42) at 1° angular resolution — 35× a VL53L8CX, and the reason
  two people walking abreast can be separated rather than merged.
- **150 mW** typical sensor power. The sensor *is* the power budget; the MCU is noise
  beside it. Every design decision follows from that.
- **~9 KB per frame over I²C** — roughly 225 ms at 400 kHz. The **bus**, not the sensor,
  caps the frame rate at a few fps, and costs real energy doing it.

## Conventions

- Every figure carries its source and date. **`VERIFY` means unconfirmed** — never
  designed against silently.
- Counts leave the device; frames never do. The privacy property is architectural.
- Development on the nRF54L15-DK before the custom board, always.

Nothing in `docs/` is a measurement. Every energy and accuracy figure in the paper must
come from the bench.
