# Current State
Last updated: 2026-09-01

## What exists
Research, planning, and driver scaffolding. **No firmware builds yet, no hardware
measured.** ST's driver arrived today and stage 1 is unblocked.

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
1. **Driver** — ST's VL53L9 driver ported to Zephyr over I²C  *(in progress)*
2. **Telemetry** — frames/counts over BLE to a web interface
3. **Algorithm** — on-device occupancy and dwell detection
4. **Power** — per-domain gating and duty-cycle optimisation. **Last on purpose**, and
   where the paper is

## Next session — TODO, in order
1. **Rewrite `vl53l9cx_platform.[ch]`** against ST's real thirteen-function contract.
   The current file is written to the L5/L8 convention and is wrong. Unblocked, offline,
   and everything else waits on it.
2. **Write `vl53l9cx.c`** — the Zephyr device wrapper: init, PM actions (`SUSPEND` /
   `TURN_OFF`), frame plumbing. Now writable against a visible API.
3. **Extend the devicetree binding** with `vdda`, `vddio`, `ext-clock-frequency`,
   `xshut-gpios`, `int-gpios` — mirroring ST's own `vl53l9_device_t`. **Blocked on the
   schematic** for the three values.
4. **Set `output_interface` and `signaling_mode`** for interrupt-pad signalling, not
   in-band interrupt — there is no IBI on plain I²C.
5. **Log `vl53l9_get_status()` from the first frame.** Eight health bits, and on a board
   with no reference they are the only second opinion available.
6. At bring-up, capture cheaply and immediately: **blob upload duration**, **integration
   time per binning mode** (the last unknown in the frame budget), and a **bus scan** to
   settle the address.

## The two questions that gate everything
1. **VDDA, VDDIO and the external clock frequency.** No longer curiosities —
   `vl53l9_init()` writes all three into the device, and a wrong value misconfigures the
   analogue front end rather than failing loudly. **The port cannot be finished without
   the schematic.**
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
- 2026-09-01: Victor supplied X-CUBE-53L9A1. Audited it, tracked the BSD-3-Clause driver
  and ST's reference platform port in-repo, and corrected three things it exposed: the
  platform scaffolding follows the wrong driver generation, 54×42 frames are 14,842
  bytes not 13,608, and the six-mode span is 72.8× not ~150× because of a fixed 100-byte
  status line. Also resolved that binning preserves the field of view only within the
  wide family, so the paper's curve is binning 2 / 6 / 8.
- 2026-08-31: Reframed to room occupancy and dwell. Worked out the frame-rate budget.
  Source-verified sensor facts from the community drivers. Wrote the four-stage plan.
