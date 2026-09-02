# Current State
Last updated: 2026-09-01

## What exists
Research, planning, and **the stage-1 driver written in full** — platform layer, Zephyr
driver, binding, power management. **Never compiled, never run.** No toolchain on this
machine and no hardware.

| Document | What it holds |
|---|---|
| `docs/plan/st-package-audit.md` | **What X-CUBE-53L9A1 provides, and the three things it corrected** |
| `docs/plan/driver-port.md` | Port strategy and ST's real platform contract |
| `docs/plan/room-occupancy.md` | The dwell reframe: what it fixes, what it breaks |
| `docs/plan/frame-rate-budget.md` | Bandwidth arithmetic (corrected 2026-09-01) |
| `docs/plan/implementation.md` | 6 phases, test rig, scenario list, risks |
| `docs/plan/paper.md` | The contribution, venues, threats |
| `docs/hardware/sensor-vl53l9cx.md` | VL53L9CX specs, resolved facts, remaining VERIFY |
| `docs/hardware/mcu-isp2454ll.md` | ISP2454-LL / nRF54L15, power domains, toolchain |
| `docs/research/people-detection.md` | Mounting geometry, classical pipeline, literature |
| `docs/research/applications.md` | Ranked demo candidates, and what was rejected |

## Decided
- **Sensor:** VL53L9CX (`VL53L9CXV0VE/1`) — optical dToF, **not radar**
- **MCU:** ISP2454-LL (Nordic nRF54L15), Zephyr / nRF Connect SDK
- **Interface: I²C** (Victor, 2026-08-31) — confirmed viable 2026-09-01: ST's own
  reference port runs the STM32 I3C peripheral in legacy I²C mode, and
  `PLATFORM_BUS_I2C` is a first-class option in their interface header
- **Hardware: a single custom PCB** carrying both parts. **No DK, no ST eval board.**
- **Lead application: room / desk-cluster occupancy and dwell** (Victor, 2026-08-31) —
  people who STAY. Doorway counting demoted to a secondary demo
- **Paper angle:** energy-accuracy characterisation, not "we counted people"
- **ST's driver is tracked in-repo** (BSD-3-Clause) at `firmware/drivers/vl53l9cx/st/`;
  the full 39 MB package stays gitignored at `vendor/x-cube-53l9a1/`

## Project shape — Victor's four stages
1. **Driver** — ST's VL53L9 driver ported to Zephyr over I²C  *(written; needs a
   build)*
2. **Telemetry** — frames/counts over BLE to a web interface
3. **Algorithm** — on-device occupancy and dwell detection
4. **Power** — per-domain gating and duty-cycle optimisation. **Last on purpose**, and
   where the paper is

## Next session — TODO, in order
1. **Get it to compile.** Needs an nRF Connect SDK workspace and a board file; neither
   exists yet, and there is no C toolchain on this machine. Expect ordinary first-build
   errors — a missing include, a moved Zephyr API. The *shape* of the port came from
   ST's headers and should not need rework.
2. **Write the board devicetree node.** Blocked on three values only:
   `vdda-microvolt`, `vddio-microvolt`, `ext-clock-frequency`. They are `required: true`
   on purpose, so the build fails rather than the firmware lying.
3. **Decide how AP_CLK is sourced** — board oscillator, or MCU. If MCU, PWM probably
   cannot make 12 MHz cleanly; see DECISIONS 2026-09-01.
4. **Pin the nRF Connect SDK version in `west.yml`** — the workspace does not exist yet,
   so this has never been done.
5. Then bring-up, in the order in `firmware/drivers/vl53l9cx/README.md`: scope AP_CLK,
   probe 0x29, watch the blob upload, log the status line, check orientation.

## The two questions that gate everything
1. **VDDA, VDDIO and AP_CLK.** The rails are two-way enums off the schematic (2.8 or
   3.3 V; 1.2 or 1.8 V) and the driver will not build without them. **The clock is the
   serious one: the sensor does not acknowledge its I²C address until a 6-27 MHz
   external clock is running on AP_CLK** — 12 MHz on every reference design. Does the
   board have an oscillator, or must the nRF54L15 generate it? Either way it must be
   gated with the sensor domain in stage 4.
2. **What room, and how big?** One unit covers roughly 3 × 2 m at ceiling height — a
   desk cluster or a small meeting table, not a whole room. Coverage is the binding
   constraint on the test setup and on what the paper can claim.

## Open questions for Victor
- Which **GPIOs** control XSHUT, the data-ready interrupt, and the sensor power domain?
- Can the board measure **sensor and MCU rails separately**? The paper claims a
  per-component energy breakdown; the Power Profiler measures one rail at a time.
- Is there a paper deadline or venue in mind? It changes the sequencing.
- Ceiling height of the intended test site — sets the FoV footprint.

## Watch
The VL53L9CX shipped mid-2026. **The characterisation gap this paper occupies is open
because the part is new, and it will not stay open.** Sequencing matters more than usual.

## Last session
- 2026-09-01 (later): Wrote the whole stage-1 port against ST's API — 13 platform
  functions, the Zephyr driver, PM actions, a rewritten binding. Every ST symbol checked
  against their headers; nothing compiled. Also settled the I²C address at 0x29 from
  ST's own `set_com_config()`, and changed the public API to take a frame *period* in ms
  rather than a rate in Hz, because dwell needs 0.05-0.2 Hz.
- 2026-09-01: Victor supplied X-CUBE-53L9A1. Audited it, tracked the BSD-3-Clause driver
  and ST's reference platform port in-repo, and corrected three things it exposed: the
  platform scaffolding follows the wrong driver generation, 54×42 frames are 14,842
  bytes not 13,608, and the six-mode span is 72.8× not ~150× because of a fixed 100-byte
  status line. Also resolved that binning preserves the field of view only within the
  wide family, so the paper's curve is binning 2 / 6 / 8.
- 2026-08-31: Reframed to room occupancy and dwell. Worked out the frame-rate budget.
  Source-verified sensor facts from the community drivers. Wrote the four-stage plan.
