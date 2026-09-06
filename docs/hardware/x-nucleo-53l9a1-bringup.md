# Driving the X-NUCLEO-53L9A1 from the nRF54L15

Prepared 2026-09-06. Every hardware fact below is from **UM3656 Rev 1** (in
`vendor/x-cube-53l9a1/Documentation/`), Table 1 and section 3.3.2.2, unless marked
`VERIFY`.

## Why this is worth doing

The custom shield has never acknowledged on I²C, and the causes still open are: no usable
AP_CLK, no level shifting, and a supply that folds back at 100 mA. ST's board has an
onboard oscillator, level shifters with a jumper-selected host reference, and its own
regulators — so it removes all three at once.

It does **not** diagnose the custom board. What it gives is the thing this project has
never had and that `CLAUDE.md` names explicitly as missing: **a known-good reference.**
After this runs, a silent custom board is a hardware fault, not an open question about the
driver.

## Jumpers and the switch — set these before wiring anything

| Control | Set to | Why |
|---|---|---|
| **SW1** | **INT** (bottom) | AP_CLK from the **onboard 12 MHz oscillator**. This is the entire point. |
| **J6** SENSOR IOVDD | **1V8** (top) | Sensor IO domain. Matches `vddio-microvolt` in the overlay. |
| **J1** EXT IOVDD | **the nRF54L15's IO voltage** | See the warning below. |

> **J1 is the one that can destroy something.** It sets the host side reference for the
> board's level shifters. On a Nucleo it matches JP4; here it must match whatever the
> ISP2454-LX actually drives its GPIOs at. Set it wrong and you either get no
> communication at all or you overdrive a translator.
>
> This is also the fix for finding 3 in `radar-shield-review.md` — the custom board has no
> level shifting anywhere, and `V_Host` is a dead-end net. ST's board does the translation
> that ours does not, but only if J1 is right. **Measure the nRF54L15's VDD before setting
> it.** That measurement is still outstanding from 2026-09-06 and is needed either way.

## Wiring

The shield is an Arduino UNO R3 connector. The nRF pins below are the ones the firmware
already uses, so nothing on the Nordic side changes.

| Shield (Arduino) | Signal | nRF54L15 | Note |
|---|---|---|---|
| D15 | SCL | **P1.08** | |
| D14 | SDA | **P1.13** | |
| D1 | XSHUT | **P1.07** | **Reversed polarity** — see below |
| D0 | INTR | **P0.01** | Falling edge |
| A3 | SYNC_IN | *leave* | Follower mode only; driver holds `SYNC_MANUAL` |
| D11 | CLK_IN | *leave* | Only used when SW1 = EXT |
| GND | — | **GND** | Not optional — a shared return, wired short |
| 3V3 / 5V | — | `VERIFY` | UM3656 is the *software* manual and does not state the shield's supply pins or current. Confirm against the X-NUCLEO-53L9A1 board manual or its schematic before powering it. |

`VERIFY`: UM3656 does not say whether the shield carries its own I²C pull-ups. Ours are
enabled in the board file's pinctrl (`bias-pull-up`), which is harmless alongside external
ones but will not rescue a bus with none. If the bus times out rather than NAKs, that is
the first thing to check — the distinction between `-ETIMEDOUT` and `-EIO` has already
earned its keep once on this project.

## The one that will bite: XSHUT is inverted

UM3656 Table 1 lists D1 as *"XSHUT — Shutdown pin — **Reversed polarity**"*. The custom
board is active **high**; this board is active **low**, and the overlay sets
`GPIO_ACTIVE_LOW` accordingly.

Getting this backwards holds the sensor in reset for the whole session while every line of
the log looks healthy — which is indistinguishable from the failure already being chased.
If it does not answer, this is suspect number one, and the driver's existing
`try_inverted_polarity()` fallback will say so in the log rather than leaving you guessing.

## Before you blame the wiring: the three power-on conditions

UM3683 Rev 3 §2.5.1 — the device leaves `POWER_OFF` only while **all three** hold: the
three supplies (AVDD, DVDD, IOVDD) are up, **XSHUT is high** at IOVDD level, and **the
external clock is active**. ST adds that if any one *"becomes invalid, the sensor returns
to the off state"*.

That is why a dropped clock looks identical to a dead sensor, and why SW1 on INT is the
point of this whole exercise. Full notes in
`docs/research/um3683-power-on-and-boot.md`.

The firmware now tells you which of these is unmet rather than leaving you to guess:

```
device id 0x53334c39 ("S3L9") — correct
state machine: 0x01 (READY_TO_BOOT — expected here)
```

Both lines mean the three conditions are genuinely met and the only remaining risk is the
firmware blob upload. `0x00 (NONE)` from a part that just answered means a supply or the
clock is present but not holding.

## Building — one tick-box

This is a **Zephyr snippet**, so switching boards is a checkbox in the nRF Connect
extension, not a command-line argument.

**In VS Code:**

1. nRF Connect side bar → your build configuration → **Add Build Configuration**
   (or the pencil icon to edit an existing one).
2. Under **Optional snippets**, tick **`x-nucleo`**.
3. **Build Configuration**.

**To go back to the custom shield: untick it.** Nothing else changes between the two —
same board, same `prj.conf`, same everything. Keep one build configuration of each and
switch by selecting it in the side bar.

From a terminal the equivalent is `-S x-nucleo`:

```bash
west build -b water_sense_board/nrf54l15/cpuapp firmware_test -p always -S x-nucleo
```

No `-DBOARD_ROOT` and no `-DEXTRA_DTC_OVERLAY_FILE` are needed any more: `.vscode/settings.json`
carries `nrf-connect.boardRoots` for the extension, and `CMakeLists.txt` appends the board
root for non-sysbuild command-line builds.

Verified 2026-09-06, both under sysbuild with no extra arguments: custom board FLASH
65,720 B, X-NUCLEO FLASH 65,440 B, RAM 45,232 B. The snippet was confirmed to take effect
in the generated devicetree — `vdda-microvolt` reads 0x325AA0 (3.3 V) without it and
0x2AB980 (2.8 V) with it.

## What the overlay changes, and what each costs

| | Custom shield | X-NUCLEO-53L9A1 |
|---|---|---|
| `vdda-microvolt` | 3 300 000 | **2 800 000** — UM3656 §3.3.2.2 says `.vdda` must be 2V8 |
| `ext-clock-frequency` | 8 000 000 (GRTC) | **12 000 000** (onboard oscillator) |
| `xshut-gpios` | active **high** | active **low** |
| `power-gpios` | P0.02 | **deleted** — no such pin exists |
| GRTC `clkout-fast` | 8 MHz on P0.00 | **deleted** — the SoC generates no clock |

**VDDA is not cosmetic.** It is written into the device and configures the analogue front
end. Carrying 3.3 V across would not fail loudly; it would return plausible rubbish, which
is worse than a NAK.

**Losing `power-gpios` has a real cost.** `PM_DEVICE_ACTION_TURN_OFF` has nothing to drive,
so the sensor cannot be powered down between frames. This board can prove the driver and
measure active-mode energy. It **cannot** carry the duty-cycling work or the multi-month
battery claim — that needs hardware which can cut the rail, i.e. the custom board, once it
talks.

## A measurement this unlocks for free

Deleting the GRTC output also compiles out the SYSCOUNTER keep-alive, via
`VL53L9CX_APCLK_FROM_GRTC` in the driver. So idle current measured with this overlay
**is** the "AP_CLK removed" arm of the A/B in `docs/plan/ap-clk-always-on.md`, which has
been waiting for a number since 2026-09-04. Take it while the board is on the bench.

## The zero-code alternative, if the above stalls

ST's own firmware on a **NUCLEO-H563ZI** — the package in `vendor/x-cube-53l9a1` targets
exactly that board, uses PB8/PB9 (D15/D14) for the bus, PB6 XSHUT, PB7 INTR, PB1 SYNC_IN,
and needs nothing written. It is the faster answer to "does this sensor work at all",
and it settles the driver-versus-hardware question from the other direction.

It is a detour, not the destination: it proves the part, not the Nordic port, and the paper
needs the Nordic port. Worth it only if the wiring above does not produce an answer
quickly.
