# Current State
Last updated: 2026-09-01

## What exists
Research, planning, and **a complete, buildable-shaped stage 1**: driver, board
definition, bring-up app, west manifest. **Never compiled, never run** — no toolchain on
this machine and no hardware. The only missing inputs are three values off the
schematic and the pin assignments.

| Document | What it holds |
|---|---|
| `docs/hardware/water-sense-board-review.md` | **Review of Victor's board file against NCS 3.3, with ready-to-paste snippets** |
| `firmware/app/README.md` | How to build it, and what each bring-up gate tells you |
| `docs/plan/st-package-audit.md` | What X-CUBE-53L9A1 provides, and the three things it corrected |
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
- **MCU:** **ISP2454-LX** (Nordic nRF54L15), Zephyr / nRF Connect SDK. Corrected from
  "-LL" on 2026-09-04 — Victor confirmed the fitted part is the **LX** variant
- **Interface: I²C** (Victor, 2026-08-31) — confirmed viable 2026-09-01: ST's own
  reference port runs the STM32 I3C peripheral in legacy I²C mode, and
  `PLATFORM_BUS_I2C` is a first-class option in their interface header
- **Hardware: a single custom PCB** carrying both parts. **No DK, no ST eval board.**
- **The board is `water_sense_board`**, now at
  `firmware_nrf_board_testing/boards/ethzurich/water_sense_board/`, hardware model v2,
  target `water_sense_board/nrf54l15/cpuapp`. **Victor owns it: hands off unless he asks
  in that message** — see CLAUDE.md. `firmware/boards/pbl/vl53l9_node/` was Claude's
  placeholder and is superseded
- **AP_CLK is P0.13**, 8 MHz from `pwm20` (Victor, 2026-09-04)
- **SPI chip select (P2.05) is driven by the port file**, not `cs-gpios` (Victor,
  2026-09-04). `sdhc0` is disabled because the two cannot both own the pin
- **Lead application: room / desk-cluster occupancy and dwell** (Victor, 2026-08-31) —
  people who STAY. Doorway counting demoted to a secondary demo
- **Paper angle:** energy-accuracy characterisation, not "we counted people"
- **ST's driver is tracked in-repo** (BSD-3-Clause) at `firmware/drivers/vl53l9cx/st/`;
  the full 39 MB package stays gitignored at `vendor/x-cube-53l9a1/`

## Project shape — Victor's four stages
1. **Driver** — ST's VL53L9 driver ported to Zephyr over I²C  *(written, with board
   file and bring-up app; needs a build)*
2. **Telemetry** — frames/counts over BLE to a web interface
3. **Algorithm** — on-device occupancy and dwell detection
4. **Power** — per-domain gating and duty-cycle optimisation. **Last on purpose**, and
   where the paper is

## Next session — TODO, in order
1. **Build and flash the board test.** `west build -b water_sense_board/nrf54l15/cpuapp
   firmware_nrf_board_testing`, then `west flash`, then open RTT. It touches no
   peripheral: if it prints a heartbeat, the toolchain, the SoC target, the flash offset
   and the debug path are all proven, and nothing else on the board is implicated.
2. **Fix whatever the first build says.** The likely candidates are the peripheral
   instance names (`i2c21`, `spi20`, `pwm20`, `gpiote20/30`) and whether **P0.13 exists**
   — the nRF54L15's P0 port is short, and P0.13 may be an ISP2454-LX module pin rather
   than an SoC port.pin.
3. **Build `firmware/app` against the same board** to compile the VL53L9CX driver for
   the first time — it has still never met a compiler.
4. **Write the application `.overlay`** adding the VL53L9CX node under the I²C bus.
   Everything it needs is now known except the final instance labels.
5. **Decide the SD path**: `cs-gpios` plus Zephyr's stack, or the port file owns CS and
   `sdhc0` goes. It is disabled until then.
6. Then the gates in `firmware/app/README.md`.

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
- 2026-09-01 (later still): Added the board definition
  (`vl53l9_node/nrf54l15/cpuapp`), a staged bring-up app, and `west.yml`. AP_CLK set to
  **8 MHz** — 16 MHz / 2, the only frequency an nRF PWM reaches exactly inside the
  sensor's 6-27 MHz window, so ST's 12 MHz is deliberately not copied. Also found and
  fixed a repeated-start bug in the read path: this part does not support a repeated
  start and latches into NAK-everything if it sees one.
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
