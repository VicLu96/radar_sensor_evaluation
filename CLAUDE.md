# radar_sensor_evaluation — Claude instructions

## What this repo is
Firmware and evaluation for a **low-power people-counting node**: the ST **VL53L9CX**
time-of-flight sensor on an **Insight SiP ISP2454-LX** (Nordic nRF54L15), in Zephyr /
nRF Connect SDK, reporting counts over BLE. The end goal is a **published paper** on
the energy-accuracy trade-off of high-resolution dToF sensing.

Owner: Victor, Zurich. He designed the hardware.

## Read first, every session
1. `CONTEXT.md` — current state and the ordered TODO. Always.
2. Whatever `docs/` file the task touches. Do not bulk-read `docs/`.

## Two corrections to keep making
- **It is not a radar.** VL53L9CX is an optical dToF sensor (SPAD + 940 nm VCSEL). The
  repo name is historical. This changes the failure modes and the relevant literature.
- **The `claude/stocks-2vn27c` branch is unrelated** — a stock-price CLI produced by a
  one-word prompt against this repo when it was empty. Leave it alone. Radar work lives
  on `main`.

## Hard rules
- **Every figure carries its source and date.** A number without one does not go in.
- **`VERIFY` means not confirmed.** Never design against a VERIFY item without saying
  so out loud; never quietly promote one to fact.
- **Sensor energy dominates.** 150 mW active sensor versus a few mA of MCU. Optimising
  MCU cycles is nearly pointless; reducing active sensor time is everything. Check any
  proposed optimisation against this before spending effort on it.
- **Counts leave the device, frames never do.** The privacy claim is architectural and
  free — do not add a raw-frame transmit path.
- **There is no DK and no ST eval board** — a single custom PCB carries both parts, and
  Victor writes the board files. So bring-up has no known-good reference: never assume
  a silent sensor is a software bug. Work the staged gates in the implementation plan,
  and treat the **logic analyser as the reference instrument**.
- **BOARD FILES ARE VICTOR'S. NEVER EDIT THEM — this is absolute.**
  Everything under `firmware_nrf_board_testing/boards/` (the `water_sense_board`
  definition: `.dts`, `-pinctrl.dtsi`, `Kconfig.board`, `Kconfig.defconfig`,
  `board.cmake`, `*_defconfig`, `*.yaml`) is off limits. Do not "fix" them, do not
  reformat them, do not add a node to them, not even when they are the cause of a
  build failure. Report what is wrong and let Victor change it.
  To add hardware the driver needs, use an **application-level `.overlay`** in the app
  directory — that is not a board file and is the correct Zephyr mechanism anyway.
  (The older `firmware/boards/pbl/vl53l9_node/` was written by Claude and is superseded
  by `water_sense_board`; it is not protected, but it is also not the target.)
- **`DECISIONS.md` is append-only.** Never edit a past entry.
- Commit and push finished work to `main`. Never force-push, never rewrite pushed
  history, never touch the stocks branch.

## Conventions
- Write the full part name, not just an abbreviation: "VL53L9CX", "ISP2454-LL".
- Dates ISO: `YYYY-MM-DD`.
- Units always explicit, and energy in µJ or mJ per frame rather than "low".
- Pin the nRF Connect SDK version in `west.yml`; record any change in `DECISIONS.md`.
- Session log in `notes/YYYY-MM-DD.md`.

## Context hygiene
`CONTEXT.md` is **rewritten** each session, not appended to — keep it under a page.
History belongs in `DECISIONS.md`. Old notes are not consulted unless asked. If a
measured figure is older than the hardware or firmware it describes, re-measure rather
than quoting it.

## End of every session
Rewrite `CONTEXT.md`, append to `DECISIONS.md` if a decision was made, commit, push.

## What you cannot do
No access to the hardware. Every energy or accuracy number in this repo must come from
Victor's bench, not from an estimate — estimates are marked as estimates and are for
sizing decisions only, never for the paper.
