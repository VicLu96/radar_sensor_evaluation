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
5. **Can the board measure sensor and MCU rails separately?** A hardware question, and
   it must be settled while the board can still change — the paper's energy claim
   depends on phase attribution.
6. Order what is missing: nRF54L15-DK, X-NUCLEO-53L9A1 or STEVAL-VL53L9, Power
   Profiler Kit II.

## Open questions for Victor
- What state is the hardware in — designed, fabricated, populated, working?
- Do you already have the DK, an ST eval board, and a PPK II?
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
