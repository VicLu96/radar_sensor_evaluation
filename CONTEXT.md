# Current State
Last updated: 2026-09-13

## Read this first
**[docs/plan/implementation.md](docs/plan/implementation.md)** — the single ordered
build plan: bench gates, work packages with done-when criteria, the corrected detection
specification, and the interfaces. Rewritten 2026-09-13. Where another plan file
disagrees, implementation.md wins.

## Where the project is
- **Stage 1 driver: done.** 54×42, 2268 zones. Tag `working-54x42-2026-09-11`.
- **Stage 2 BLE + web interface: done.** Advertise → connect → boot sensor → stream;
  live heatmap; config round-trip. Tag `demo-2026-09-11`.
- **Demo features (2026-09-12): built, not yet tested in detail by Victor.** SHORT/LONG
  range context, five named measurement modes with editable settings, recording to CSV
  and `.wstof` v1.
- **Stage 3 detection: fully planned, nothing implemented.** Two images: image 1
  evaluates A1/A2/A3/A4 + a mass control over a D2 detect stream with a label plane and
  a true count; image 2 is built after the evaluation gate and only advertises the count.
- **Stage 4 paper measurements: not started.** Only on image 2.

## Hardware, as it is now
| | |
|---|---|
| Sensor | VL53L9CX at I²C 0x29 — optical dToF, **not radar**; FoV 54°×42°, 1° per zone |
| MCU | ISP2454-LX (nRF54L15), `water_sense_board/nrf54l15/cpuapp`, NCS v3.3.0 |
| Radio | **needs the HFXO/LFXO internal load capacitors in the application overlay** — without them TX is invisible |
| AP_CLK | 12 MHz external crystal; GRTC clock output deleted |
| I²C | 400 kHz on 4.7 kΩ (out of spec); 1 MHz waits on 1 kΩ pull-ups |
| IMU | LSM6DSV16BX at 0x6B — silent since 2026-09-10 |

Board files `firmware_test/boards/ethzurich/**` are Victor's: hands-off unless he asks in
that message. Use the application overlay.

**Shield re-spin (adds the 12 MHz oscillator):** reviewed 2026-09-13 against DS14879 Rev 8 —
[docs/hardware/radar-shield-review-2026-09-13.md](docs/hardware/radar-shield-review-2026-09-13.md).
SDA/SCL labels crossed at A11/A12 (harness compensates), three decoupling caps never placed,
thermal pads off by up to 0.13 mm, host IO voltage still unrecorded with no level translation.
Checklist at the end of the report.

## Next — in order (full detail in implementation.md §6)
**Victor, bench:**
1. **B1 full-resolution SNR** — amplitude at 54×42 against 126 at 24×20. Can move all
   detection work to 24×20 if it fails.
2. **B2 range presets** — white card, three distances per mode; exposures are estimates.
3. **B5 1 kΩ pull-ups**, then enable Fast-mode Plus in the overlay.
4. **B4 overnight empty-room recording** — once WP1 exists. B3 FoV markers and B6 test-site
   geometry before the scenario library.

**Software, can start now without any bench result:**
1. **WP1 long recordings** — the recorder stops at 4000 frames (~27 min) in browser
   memory; stream to disk, add decimation.
2. **WP2 `lib/detect` harness** — no Zephyr, integer-only, PC replay of `.wstof`,
   synthetic frames, state hash.
3. Then WP3 calibration → WP4 front end + A1 → WP5 firmware glue → WP6/WP7 detect stream
   in the web interface with `.wstof` v2 and the true count.

## Open questions for Victor (defaults in implementation.md §7)
- Test site: ceiling height, room size, mount position and tilt.
- Separate sensor and MCU rails — needs a board revision; phase 4 only.
- Host compiler for the PC harness; where recordings are stored.
- Paper venue or deadline.
- Real load-capacitor values for the ISP2454-LX (ask Insight SiP); 15000/17000 fF work
  but are Nordic DK values.

## Watch
The VL53L9CX shipped mid-2026. The characterisation gap this paper occupies is open
because the part is new, and it will not stay open.

## Hard rules that keep getting tested
- **Check the version string on the board before interpreting any bench result.**
- Every figure carries its **source and date**; `VERIFY` is never promoted silently.
- **Counts leave the device, frames never do** — in image 2 that is a property of the binary.
- `DECISIONS.md` is append-only. This file is rewritten each session.
