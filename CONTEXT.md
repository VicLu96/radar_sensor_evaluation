# Current State
Last updated: 2026-08-31

## What exists
Research and planning only. **No firmware written, no hardware measured.**
`main` was created as an orphan branch today; the unrelated stocks CLI stays on
`claude/stocks-2vn27c` and is not touched.

| Document | What it holds |
|---|---|
| `docs/hardware/sensor-vl53l9cx.md` | VL53L9CX specs, the I²C bandwidth analysis, failure modes |
| `docs/hardware/mcu-isp2454ll.md` | ISP2454-LL / nRF54L15, power domains, toolchain |
| `docs/research/people-detection.md` | Mounting geometry, classical pipeline, literature |
| `docs/research/applications.md` | Ranked demo candidates, and what was rejected |
| `docs/plan/implementation.md` | 6 phases, test rig, scenario list, risks |
| `docs/plan/paper.md` | The contribution, venues, threats |

## Decided
- **Sensor:** VL53L9CX (`VL53L9CXV0VE/1`) — optical dToF, **not radar**
- **MCU:** ISP2454-LL (Nordic nRF54L15), Zephyr / nRF Connect SDK
- **Interface: I²C** (Victor, 2026-08-31) — not I3C
- **Hardware: a single custom PCB** carrying both parts. **No DK, no ST eval board.**
  Victor writes the Zephyr board files and has a Power Profiler Kit II.
- **Lead application: room / desk-cluster occupancy and dwell** (Victor, 2026-08-31) —`  people who STAY, not a dynamic doorway. Doorway counting demoted to a secondary demo
- **Paper angle:** energy-accuracy characterisation, not "we counted people"

## Project shape — Victor's four stages
1. **Driver** — ST ULD ported to Zephyr over I2C  *(in progress: scaffolding written)*
2. **Telemetry** — frames/counts over BLE to a web interface
3. **Algorithm** — on-device people counting
4. **Power** — per-domain gating and duty-cycle optimisation. **Last on purpose**, and
   where the paper is

## Next session — TODO, in order
1. **Download X-CUBE-53L9A1** into `vendor/` (licensed, gitignored). Everything in
   stage 1 is blocked on it.
2. **Verify the ULD platform signatures** against that package and fix
   `vl53l9cx_platform.h` — written to the VL53L5CX/L8CX convention, unconfirmed for L9.
3. **Write `vl53l9cx.c`** — the Zephyr device wrapper: init, PM actions, frame
   plumbing. Deliberately not guessed at before ST's API is visible.
4. **Confirm the I2C address** is 0x29 (7-bit) not 0x52 (8-bit) in the board devicetree.
5. **Measure the blob upload duration** during bring-up — it decides the whole stage-4
   architecture and is nearly free to capture now.
6. Check which **reduced-resolution modes** exist in the ST headers. If only 54x42
   exists, the paper's central curve collapses to a point.

## The question that now gates the test setup
**What room, and how big?** One unit covers roughly 3 x 2 m at ceiling height - a desk
cluster or a small meeting table, not a whole room. Coverage is the binding constraint
and everything follows from it: mount height, whether one unit suffices, and what the
paper can claim.

## Open questions for Victor
- Can the board measure **sensor and MCU rails separately**? The paper claims a
  per-component energy breakdown and the Power Profiler measures one rail at a time.
- Which **GPIOs** control XSHUT, the data-ready interrupt, and the sensor power domain?
  Needed for the devicetree binding.
- Is there a paper deadline or venue in mind? It changes the sequencing.
- Ceiling height and doorway width of the intended test site — sets the FoV footprint.

## Watch
The VL53L9CX shipped mid-2026. **The characterisation gap this paper occupies is open
because the part is new, and it will not stay open.** Sequencing matters more than
usual.

## Last session
- 2026-08-31: Cloned the repo, found it contained an unrelated stocks CLI from a
  misdirected prompt. Created `main`, researched sensor, MCU, detection literature and
  applications, and wrote the implementation and paper plans.
