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
- **Lead application:** overhead battery-powered people counter, counts over BLE
- **Paper angle:** energy-accuracy characterisation, not "we counted people"

## Next session — TODO, in order

**Phase 0 first. All four could force a redesign, and all are answerable in days.**

1. **Firmware blob size and I²C load time.** If cold boot takes seconds, power-cycling
   the sensor is uneconomic and the whole sleep architecture changes. Highest priority.
2. **Which reduced-resolution modes exist.** If 54×42 is the only mode, the paper's
   central curve has one point on it. Check X-CUBE-53L9A1 headers and UM3656.
3. **Standby current, and whether firmware survives standby.**
4. **Max I²C clock** — sensor and nRF54L15 both. 400 kHz vs 1 MHz halves bus energy.
5. **Can the board measure sensor and MCU rails separately?** *(Phase 0.5)* Schematic
   question. If the rails are shared, the paper's central claim needs a shunt — decide
   before Phase 2, not after. **The single highest-value thing to check.**
6. **Get to an I²C ACK from the sensor** *(gate 1.2)*. With no reference board this is
   what separates "hardware is fine" from "hardware is broken", and everything after it
   is cheaper. An afternoon's work.
7. Read `github.com/earlynerd/VL53L9-Arduino`'s init sequence — with no eval board it
   is the nearest thing to a reference implementation.

## Open questions for Victor
- **Is the board fabricated and populated, or still in design?** Changes whether 0.5 is
  a schematic edit or a bodge wire.
- **Does more than one populated board exist?** Two failing identically means a design
  fault; one failing could be assembly. Matters a lot during bring-up.
- Do you have a **logic analyser**? With no reference board it is the primary diagnostic
  instrument, not an accessory.
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
