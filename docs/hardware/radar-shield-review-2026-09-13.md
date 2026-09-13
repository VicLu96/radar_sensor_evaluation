# `radar_shield` schematic and layout review, before the AP_CLK oscillator re-spin

Reviewed 2026-09-13 for Victor, ahead of a board revision that adds the 12 MHz AP_CLK
oscillator. Read-only: the KiCad files were not modified (checksums compared before and
after the KiCad CLI runs).

## What was compared

| Input | Version |
|---|---|
| `C:\Users\luder\Documents\Radar\radar_shield\radar_shield.kicad_sch` | saved 2026-09-05 10:17 |
| `radar_shield.kicad_pcb` | saved 2026-09-05 10:29. **Identical to the fabricated board** except two value strings (`MIC23150` → `MIC23150-GYMT`/`-SYMT`): diffed against the backup written 4 minutes before the Gerbers of 2026-08-10 21:18 |
| `my_Radar_Library_Footprints.pretty/vl53l9cx.kicad_mod` | 2026-08-04; the PCB's embedded copy is geometrically identical |
| **ST DS14879 Rev 8** (VL53L9CX datasheet, `Downloads\DS_vl53l9cx.pdf`) | §2.6 pinout, §2.7 application schematic, §2.9 PCB guidelines, §2.10 supplies, §2.11 clock, §7 electrical, §9 Fig. 24–25 substrate pads, §12.1 Fig. 30 solder pattern |
| **ST DB5799 Rev 1** (X-NUCLEO-53L9A1 schematic, ST's own board) | sensor, oscillator and level-shifter sheets, as a second ST reference |
| **UM3683 Rev 3** | §2.5.1 power-on conditions |
| KiCad 9.0.0 CLI | netlist export, ERC, DRC with schematic parity |

Method: the PCB's pad-to-net list is taken as the truth for connectivity; symbol pin names
come from the exported netlist; footprint pad coordinates are compared numerically against
the land pattern reconstructed from DS14879 Fig. 25 and Fig. 30.

---

## Summary

| # | Severity | Finding |
|---|---|---|
| 1 | **High** | **SDA and SCL are crossed at the sensor.** Copper takes J6 pin 1 (labelled SDA) to ball A11, which ST defines as **SCL**. It works on the bench, so the wiring from the host must cross them back. |
| 2 | **High, unresolved since 2026-09-06** | **No level translation and no isolation.** All six IOs run straight from the host to a 1.98 V absolute-maximum IO domain. `V_Host` goes nowhere. The host's IO voltage is still not recorded. Separately, when the shield's rails are off, the shared I²C bus can back-power the sensor through its IO pins. |
| 3 | **Medium** | **Three schematic capacitors were never placed.** C207, C212 and C216 are missing from the PCB, so the built board has none of them. DVDD's only 4.7 µF sits 3 mm from the pin, with a ground return of about 6 mm. |
| 4 | **Medium** | **Thermal pads in the footprint are off** by up to **0.132 mm**, and the gap between the pad rows is 0.115 mm instead of 0.200 mm. The perimeter pads are accurate. The footprint has no courtyard. |
| 5 | **Medium** | **Thermal pads are built differently from ST's guidance.** There are 10 open vias in the pads (none in B9 or D9). The top GND pour joins all 12 pads with only thin solder-mask strips between them. ST asks for 12 independent pads, one open mask area and tented vias. |
| 6 | **Medium** | **The oscillator to add.** Requirements and a suggested circuit are in §6. |
| 7 | Low–medium | `XSHUT`, `SYNC_IN` and `/Power_Enable` have no pull resistors, so they float whenever the host pin is high-impedance. |
| 8 | Low–medium | VBAT decoupling: 10 µF in 0402 loses much of its value at 4 V bias; the capacitor ground pads face away from the laser-driver ground (VSS_DRIVER); there is no GND via within 1.5 mm of A1–A4. |
| 9 | Low | Housekeeping: ERC (7 errors, 38 warnings), DRC (2 courtyard overlaps), J4 in the schematic vs J1 on the PCB, MIC23150 footprints marked `through_hole`, no silkscreen labels. |
| — | **Correct** | 40 of 42 balls match ST. Every symbol pin name matches ST. Pad sizes are exact. The footprint is not mirrored. Rails, reserved pins and ground pins are right. The pin headers are clear of the field-of-view and illumination cones. |

---

## 1. SDA and SCL are crossed at the sensor

DS14879 Rev 8 Table 6: **A11 = SCL, A12 = SDA** (A12 is the corner pad). ST's own
X-NUCLEO-53L9A1 wires it the same way: `I3C_SDA → A12`, `I3C_SCL → A11`.

This board:

| | Symbol pin | Net label | J6 pin |
|---|---|---|---|
| Ball **A11** | SCL ✓ | **`/SDA`** ✗ | 1 |
| Ball **A12** | SDA ✓ | **`/SCL`** ✗ | 2 |

The symbol is right and the labels on those two pins are swapped. The PCB follows the
schematic.

**Why the board still works.** Firmware drives SCL on P1.08 and SDA on P1.13
(`water_sense_board_nrf54l15_cpuapp-pinctrl.dtsi`, unchanged since 2026-09-04). The sensor
has answered on I²C and streamed frames since 2026-09-10, so **P1.08 (SCL) must reach
J6 pin 1 (labelled SDA)**. The crossing is undone somewhere in the wiring between the two
boards. There is no silkscreen on J6, so nothing on the board shows it.

**Check before changing anything:** continuity from nRF P1.08 to shield J6 pin 1.

**Fix, if the check confirms it:** swap the two net labels at J6, so pin 1 is `SCL` and
pin 2 is `SDA`. **The copper does not change** and the working harness stays compatible.
Only re-route the shield if the host side has a fixed connector pin order that requires
SDA on pin 1. Add pin labels to the J6, J2 and J3 silkscreen either way.

This supersedes finding 1 of [`radar-shield-review.md`](radar-shield-review.md)
(2026-09-06). That review found the same crossing but did not have the datasheet to
decide which side was wrong. The datasheet and ST's reference board both say it is the labels.

## 2. IO voltage, level translation and back-powering

Nothing has changed since 2026-09-06. `/SDA`, `/SCL`, `/XSHUT`, `/SYNC_IN`, `/Interrupt`
and `/AP_CLK` run straight from the headers to the sensor. The only thing on those lines
is the pads. `V_Host` (J3 pin 2) connects to nothing.

- **IOVDD absolute maximum is 1.98 V** (DS14879 Table 15). The host drives its IOs at the
  nRF54L15's VDD, **which is not recorded anywhere in this repository** (open since 2026-09-06). If it is above ~1.98 V,
  every one of those lines exceeds the absolute maximum.
- ST's reference design does not connect the host directly. It uses two PI4ULS3V204
  4-bit bidirectional translators with a jumper-selected host reference (DB5799 Fig. 4).
- **Back-powering, whatever the host voltage.** The I²C bus is shared with the IMU on the
  host board. When `/Power_Enable` switches the shield's rails off (the duty cycling
  planned for the deployed firmware image), the host's pull-ups keep SDA and SCL high.
  Current can then flow through the sensor's IO protection diodes into the dead 1.8 V
  rail. That can load the whole bus low (the IMU included) and adds leakage the energy
  measurement would count. DS14879 does not say the IOs tolerate voltage while unpowered;
  this is `VERIFY`, not established.

**For the re-spin:**

1. **Measure the nRF54L15 VDD first.** Both options below depend on it.
2. **Host above 1.8 V:** a translator is required. Use `V_Host` as the host-side reference
   and `+1V8` as the sensor side. Choose a part whose datasheet guarantees
   high-impedance IOs when either supply is off. That also fixes back-powering.
   PI4ULS3V204 is ST's choice and the obvious candidate to evaluate.
3. **Host at 1.8 V:** no translation needed, but still isolate the bus when the shield is
   off, with a bus switch or a translator that supports power-down. Otherwise keep
   `+1V8` powered whenever the host uses the bus.
4. Add I²C pull-up footprints to `+1V8` on the sensor side. Fit **2.2 kΩ**, the value
   DS14879 §2.7 gives for 1 Mbps, if the translator separates the two bus segments.
   This also unblocks the 1 MHz bus without touching the host board.

## 3. The schematic and the PCB are out of sync

`kicad-cli pcb drc --schematic-parity` reports three capacitors that are in the schematic
and **missing from the PCB**. Since the PCB matches the Gerbers, the **built board does not
have them**.

| Ref | Value | Net | Schematic block | Role per DS14879 §2.7 |
|---|---|---|---|---|
| C207 | 4.7 µF | `+1V2` | 1.2 V supply | output capacitor for the 1.2 V buck (U202) |
| C212 | 4.7 µF | `+3V3` | Decoupling | **AVDD capacitor at the sensor** |
| C216 | 1 µF | `+1V8` | Decoupling | **IOVDD capacitor at the sensor** |

Also reported: J4 (schematic) and J1 (PCB) are the same CSI-2 header under different
reference designators.

The schematic matches ST's application schematic exactly: 10 µF + 10 µF VBAT, 1 µF IOVDD,
4.7 µF AVDD, 4.7 µF DVDD. What the PCB actually has, measured from the capacitor pad to
the nearest sensor pad on the same net:

| Rail | Capacitors on the PCB | Distance to sensor | Note |
|---|---|---|---|
| VBAT_LDD / VBAT_RX | C214 10 µF, C215 10 µF, C313 100 nF | 2.1–2.4 mm to B1 | see §8 |
| AVDD (E6/E7) | C210 4.7 µF, C209 100 nF — also the 3.3 V buck's output capacitors | 2.7 mm | GND return continuous on F.Cu |
| IOVDD (E8) | C218 4.7 µF, C217 100 nF — also the 1.8 V buck's output capacitors | 1.7–2.7 mm | fine |
| **DVDD (C12)** | **C213 4.7 µF, C206 100 nF, at the 1.2 V buck** | **3.0–3.8 mm** | **ground return ~6 mm; weakest rail** |

**For the re-spin:** run *Update PCB from Schematic*, then place C216 and C212 (or at least
a 100 nF plus the DS value) **at E8 and E6/E7**, and C213 **at C12**. Give each capacitor
its own GND via.

## 4. The VL53L9CX footprint

**Orientation, pad names and pad sizes are correct.** In the top view A1 is bottom-right
and E12 top-left, as DS14879 Fig. 30 shows. Nothing is mirrored. All four pad sizes match
ST exactly: corner 1.255 × 0.880, row 0.600 × 0.880, side 1.255 × 0.600, thermal
1.008 × 1.225 mm.

Coordinates below are in the footprint frame (origin at A1's centre; KiCad x and y, mm).
The expected values come from Fig. 25 and Fig. 30. ST's tolerance is ±0.040 mm.

| Pads | Footprint | DS14879 | Error |
|---|---|---|---|
| A1–A12, B1, C1, D1, E1, E2 | — | — | **exact** |
| E3–E11 | x +0.007, y −0.010 | — | ≤ 0.012 (within tolerance) |
| B12, C12, D12, E12 | x −11.100, y −1.285/−2.410/−3.535/−4.800 | x −11.105, y −1.265/−2.390/−3.515/−4.780 | 0.020 in y (within tolerance) |
| **B4** | (−2.400, −1.740) | (−2.5315, −1.6695) | **0.132 / 0.070** |
| **D4** | (−2.410, −3.110) | (−2.5315, −3.0945) | **0.122** |
| B5 / D5 | x −3.660 | x −3.7405 | **0.081** |
| B6 / D6, B7 / D7 | x −4.900 / −6.110 | x −4.9485 / −6.1565 | 0.046–0.048 |
| B8 / D8 | x −7.360 | x −7.3655 | 0.005 (y 0.040–0.045) |
| **D9** | y −3.000 | y −3.0945 | **0.095** |

The thermal pads were placed by hand. Their spacing across the row runs 1.21–1.26 mm
instead of 1.208, and the gap between the B and D rows is **0.115–0.145 mm instead of
0.200 mm**. All 12 pads are GND, so nothing is shorted. But a pad offset by 13% of its width
with slivers of solder mask between pads makes solder pooling and voids more likely on the
module's main heat path.

**Correct thermal-pad centres** (footprint frame):

| Column | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|
| x | −2.5315 | −3.7405 | −4.9485 | −6.1565 | −7.3655 | −8.5735 |

B row y = **−1.6695**, D row y = **−3.0945**. Also snap E3–E12 to y = −4.780 and
column 12 to x = −11.105.

**Footprint cleanup:**
- Add an **F.CrtYd** rectangle. The module body is 12.83 × 6.10 mm, centred at
  (−5.5525, −2.390): x −11.9675 … 0.8625, y −5.440 … 0.660. Add 0.25 mm around it. The
  footprint has no courtyard today, so DRC cannot check neighbouring parts against it.
- Add the body outline on F.Fab.
- Remove the stray `B.SilkS` layer from pad E1 (it prints a small rectangle on the bottom
  silkscreen) and `B.Adhes` from pad E12.

## 5. Thermal-pad construction

DS14879 §2.9.1 asks for *"a large open zone on the solder mask"* and *"12 independent
central pads"*. §2.9.2 asks for *"as many thermal vias as possible"* and *"a complete GND
layer below the module"*. Fig. 30's note says vias in the central pads should be tented.

| | This board |
|---|---|
| GND layer under the module | **In1 GND covers 95%** of the module area (the gap is the AP_CLK via's clearance at the edge) ✓ |
| Thermal vias | **10 vias in the pads, 0.3 mm drill, one each in B4–B8 and D4–D8; none in B9 or D9** |
| Via tenting | set to front and back, **but a via inside a pad's mask opening cannot be tented**, so all 10 are open under solder paste → solder wicks into them |
| Independent pads | **no**: the F.Cu GND pour fills solid between all 12 thermal pads and into the GND perimeter pads (A4, E9, …). Only mask openings the size of the pads separate them, with slivers of 0.115–0.25 mm |

**For the re-spin**, pick one:
- **Filled and capped vias in the pads** (IPC-4761 type VII), with 2–4 per pad including
  B9 and D9. Write it in the fab notes.
- **Or** keep the vias open but window the paste so it clears each via, at 60–80% paste
  coverage per pad.

Also add a copper keep-out on F.Cu across the central-pad area so the 12 pads are defined
by copper and independent, as ST describes, with one mask opening around all of them.

## 6. Adding the AP_CLK oscillator

### What the sensor requires

| Requirement | Value | Source |
|---|---|---|
| Frequency | 6–27 MHz; firmware is set for **12 MHz** | DS14879 §2.11 and Table 6; `ext-clock-frequency = <12000000>` in the application overlay |
| Level | **same as IOVDD** — V<sub>IH</sub> ≥ 0.9·IOVDD, V<sub>IL</sub> ≤ 0.1·IOVDD | DS14879 §2.11, Table 20 |
| Absolute maximum | 1.98 V at 1.8 V IOVDD | DS14879 Table 15 |
| Jitter | ≤ 200 ps | DS14879 §2.11 |
| Accuracy | ±100 ppm | DS14879 §2.11 |
| Must run continuously | if the clock stops, the sensor drops back to OFF | UM3683 §2.5.1 (the explanation adopted for the intermittent GRTC clock, DECISIONS 2026-09-06) |
| ST's reference | 12 MHz 50 ppm XO, powered from **1V8**, 100 nF, OE to GND via a DNP 0 Ω | DB5799 Fig. 5 |

The V<sub>IH</sub>/V<sub>IL</sub> window (90% / 10% of IOVDD) is tighter than normal CMOS
thresholds. **Power the oscillator from the `+1V8` net itself**, so its output swing
follows IOVDD exactly. That also removes the host's GPIO voltage from the AP_CLK path, and
the oscillator turns off with the sensor rails. It cannot be gated while the sensor is on.

### Suggested circuit

```
+1V8 ──┬───────────── Y1.4 VDD
      C_xo 100nF      Y1.1 OE/Tri-state ── +1V8   (tie; do not leave floating)
       │              Y1.2 GND ── GND via
      GND             Y1.3 OUT ── R_s (0 Ω, footprint for 22–33 Ω) ──┬── /AP_CLK ── U1.E11
                                                                    │
                       J2.2 ── R_ext (0 Ω, DNP) ────────────────────┘   external clock option
```

Fit R_s **or** R_ext, never both. This is the same INT/EXT choice as ST's SW1, done with
two resistors.

**A candidate part you already have a datasheet for:** the ECS-2520MVLC family
(`Desktop\Zivi_SMS\04_Electronics\Datasheets`). According to that datasheet (Rev 2022):

- supply 1.6–3.6 V, 2.5 × 2.0 × 0.8 mm;
- **12.000 MHz is a stock frequency** (code 120);
- ±50 ppm total (option B), which includes tolerance, temperature, supply, load and reflow,
  over −40 to +85 °C (option N);
- output low ≤ 10% and high ≥ 90% of VDD;
- rise and fall ≤ 7 ns; start-up ≤ 5 ms; duty 45/55%;
- phase jitter 150 fs;
- **≤ 1.5 mA at 1.8 V** (max, no load, the 20 MHz column);
- pin 1 tri-state (≥ 0.7·VDD or NC = active), 2 GND, 3 output, 4 VDD.

That gives a part number of **ECS-2520MVLC-120-BN-TR**, decoded from the datasheet's
numbering guide. Availability is **not checked**.

One caveat: the ≥ 90% VDD output-high guarantee equals the sensor's 90% V<sub>IH</sub>
minimum, so on paper the margin is zero. It is specified at 15 pF load; the real load here
is one input and a short trace. Keep the trace short and do not add capacitance to the
line.

**For the paper:** ~1.5 mA at 1.8 V (≤ 2.7 mW) flows whenever `+1V8` is on. For
comparison, DVDD standby is 8 mA at 1.2 V (DS14879 Table 18). Measure the oscillator's
share on its own rail rather than estimating it.

### Placement and routing

- **Space:** J1, the CSI-2 header, is not used by the firmware. Output is over I²C
  (DECISIONS 2026-09-05), and DS14879 §2.7 says CSI-2 is not mandatory. Removing J1 frees
  the strip right of the module (x ≈ 162.7–169.5 mm). Whether the unused CSI balls
  (A5, A6, A8, A9) can simply be left unrouted is not stated by ST: `VERIFY`, low risk.
- **Route AP_CLK away from I²C.** Today it runs on B.Cu under the module, **0.65 mm from
  `/SDA` over ~7 mm**, with SDA and SCL themselves only 0.2 mm apart. Give it ≥ 3× the trace
  width of clearance (≥ 0.6 mm at 0.2 mm width), GND on both sides, stitching vias, and
  ideally route it on F.Cu over In1 GND.
- Keep the oscillator and its capacitor away from the laser-driver current loop
  (B1/D1 ↔ C214/C215 ↔ A1–A4).
- At 12 MHz a 10 mm trace over solid ground is electrically short. Coupling into I²C is the
  risk, not the length.
- **If a frequency other than 12 MHz is chosen, change `ext-clock-frequency` in
  `firmware_test/boards/water_sense_board_nrf54l15_cpuapp.overlay` in the same commit.**
  A mismatch here cost four bench sessions: the sensor reported a laser fault, not a clock
  fault (DECISIONS 2026-09-11).

## 7. Undefined logic levels

| Net | Today | Risk | Suggested |
|---|---|---|---|
| `/XSHUT` | no pull | floats while the host is in reset or programming → sensor state undefined | **100 kΩ to GND**: off until the host enables it. ST pulls up instead (180 kΩ + 10 nF to 1V8) |
| `/SYNC_IN` | no pull | active low **triggers a frame** (DS14879 Table 6); a floating CMOS input also leaks current | **100 kΩ to `+1V8`** |
| `/Power_Enable` | no pull, 48 mm on B.Cu | floats at host reset → rails may start on their own | **100 kΩ to GND** |
| `/Interrupt` | none | INTR is push-pull by default (UM3683 `INTR_OUTPUT_MODE` = 0) | nothing, unless open-drain mode is used (then pull up on the host side) |

## 8. VBAT decoupling and the laser-driver ground

- **0402 10 µF at 3.7–4.2 V bias** typically keeps well under half of its rated value (general MLCC DC-bias behaviour; check the chosen part's curve). ST's board
  uses 10 µF rated 10 V (DB5799 Fig. 5). Use 0603/0805, ≥ 10 V X5R/X7R, and put the
  manufacturer part number in the schematic. **No capacitor in this schematic has a part
  number or voltage rating**, so derating cannot be checked.
- C214, C215 and C313 have their **GND pads on the far side** (y = 93.89) from VSS_DRIVER
  (A2/A3). The return path has to go around the 0.5 mm `+VBat_switched` bus. The nearest
  GND vias are 1.5–2.4 mm from the capacitors and 1.6–1.8 mm from A1–A4.
- **For the re-spin:** turn the capacitors so their GND pads face A1–A4, and add GND vias
  at both capacitor ground pads and next to A1–A4. That makes the laser-driver loop as
  small as possible.
- The load-switch output (S2.1) reaches the capacitor bus through 0.2 mm tracks. That is
  adequate for the average current; widen it to ≥ 0.4 mm if there is room.

## 9. Supply headroom — a question, not a fault

- AVDD comes from a 3.3 V buck (U203) fed by `+BATT`. DS14879 Table 16 requires
  **3.13–3.45 V** in 3.3 V mode. **If `+BATT` is a single Li-ion cell**, a buck cannot hold
  3.13 V once the cell falls toward ~3.3 V, which is a large part of the discharge curve.
  AVDD at **2.8 V** (2.65–2.95 V, selected by the `vdda-microvolt` driver setting) has more
  headroom. **What is the `+BATT` range?**
- All three sensor rails come from switching bucks. ST's reference board uses LDOs for
  every sensor rail (DB5799 Fig. 3). Bucks are the right choice for the energy paper, but
  switching ripple on AVDD could show up as ranging noise. It is worth an A/B test
  (LDO vs buck on AVDD) if the precision numbers look worse than ST's tables.

## 10. Housekeeping

- **ERC:** 7 × *power pin not driven*. Add `PWR_FLAG` to `+1V2`, `+1V8`, `+3V3`,
  `+VBat_switched`, `+BATT` and `GND`. There are also 38 warnings: symbols come from
  `PCB_Master_Thesis-rescue`, `-cache` and `myLibrary`, which are not in the library table.
  The symbols are embedded, so the design is intact, but they cannot be updated from a
  library.
- **DRC:** courtyard overlaps J3–L202 and J6–L203; J3's footprint is modified from the
  library; J3 silkscreen clipped by mask.
- **MIC23150 footprints (U202–U204) have the `through_hole` attribute but SMD pads and no
  courtyard.** A position file exported with *SMD only* leaves them out, so an assembler
  would not place them.
- **No silkscreen at all:** no reference designators and no connector pin names. Adding
  J2/J3/J6 pin names would have made finding 1 visible.
- Not reviewed against their own datasheets: SiP4282 load switch and MIC23150 bucks
  (footprints, exposed pads). The rails measure correctly on the bench.

## 11. What is correct

- **40 of 42 balls** match DS14879 Rev 8 (table below). **Every symbol pin name** matches ST.
- Rails: DVDD C12 → `+1V2`, AVDD E6/E7 → `+3V3`, IOVDD E8 → `+1V8`, VBAT_LDD B1 and VBAT_RX
  D1 → `+VBat_switched`. The firmware matches: `vdda-microvolt = 3300000`,
  `vddio-microvolt = 1800000`.
- VSS_DRIVER (A2/A3) and all VSS balls go to GND; RSVD1 (E3) and RSVD2 (E12) are left
  unconnected, as required.
- **One difference between ST documents:** DS14879 Rev 8 lists **E5 as VSS** and this
  board grounds it. ST's X-NUCLEO schematic labels E5 **VPP** and leaves it open. The
  datasheet is the customer-facing specification and the board works, so no change; noted
  in case ST is ever asked.
- Stackup: 4 layers, F.Cu with 0.1 mm to In1 GND; In2 is a `+BATT` plane.
- Field of view: the pin headers stand about 8.5 mm tall (typical 2.54 mm male header) and sit about 8 mm sideways from
  the module centre, well outside the ±27.5° field of view and ±32° illumination cone
  (DS14879 Table 3, Table 4, Table 37).

### Pin-by-pin

| Ball | ST name (DS14879 Rev 8, Table 6) | Symbol pin name | Net on PCB | Verdict |
|---|---|---|---|---|
| A1 | VSS | Vss | `GND` | OK |
| A2 | VSS_DRIVER | Vss_Driver | `GND` | OK |
| A3 | VSS_DRIVER | Vss_Driver | `GND` | OK |
| A4 | VSS | Vss | `GND` | OK |
| A5 | DATA_P | Data_P | `/Data_P` | OK |
| A6 | DATA_N | Data_N | `/Data_N` | OK |
| A7 | VSS | Vss | `GND` | OK |
| A8 | CLK_P | CLK_P | `/CLK_P` | OK |
| A9 | CLK_N | CLK_N | `/CLK_N` | OK |
| A10 | INTR | INTR | `/Interrupt` | OK |
| A11 | SCL | SCL | `/SDA` | **CROSSED** |
| A12 | SDA | SDA | `/SCL` | **CROSSED** |
| B1 | VBAT_LDD | VBAT_LDD | `+VBat_switched` | OK |
| B4–B9 | VSS (thermal) | VSS | `GND` | OK |
| B12 | XSHUT | XSHUT | `/XSHUT` | OK |
| C1 | VSS | VSS | `GND` | OK |
| C12 | DVDD | DVDD | `+1V2` | OK |
| D1 | VBAT_RX | VBAT_RX | `+VBat_switched` | OK |
| D4–D9 | VSS (thermal) | VSS | `GND` | OK |
| D12 | SYNC_IN | SYNC_IN | `/SYNC_IN` | OK |
| E1, E2 | VSS | Vss | `GND` | OK |
| E3 | RSVD1 (DNC) | RSVD1 | unconnected | OK |
| E4, E5 | VSS | Vss | `GND` | OK |
| E6, E7 | AVDD | AVDD | `+3V3` | OK |
| E8 | IOVDD | IOVDD | `+1V8` | OK |
| E9, E10 | VSS | Vss | `GND` | OK |
| E11 | AP_CLK | AP_CLK | `/AP_CLK` | OK |
| E12 | RSVD2 (DNC) | RSVD2 | unconnected | OK |

---

## Re-spin checklist

- [ ] Continuity P1.08 → J6.1. Then swap the SDA/SCL labels at J6 (copper unchanged) and add pin names to the silkscreen
- [ ] Measure nRF54L15 VDD. Add translation/isolation per §2, using `V_Host` as the host reference
- [ ] I²C pull-up footprints to `+1V8` (2.2 kΩ for 1 Mbps)
- [ ] Oscillator per §6, on `+1V8`, OE tied high, series-R and J2.2 options, 12 MHz (or update the overlay)
- [ ] Update PCB from schematic: place C212/C216 at E6–E8 and C213 at C12, each with its own GND via
- [ ] Footprint: thermal pads to the §4 coordinates, snap E row and column 12, add courtyard and fab outline, remove stray layers
- [ ] Thermal pads: filled and capped vias (B9/D9 too) or windowed paste; copper keep-out between the pads
- [ ] Pulls: XSHUT 100 kΩ↓, SYNC_IN 100 kΩ↑ to 1V8, Power_Enable 100 kΩ↓
- [ ] VBAT capacitors: 0603/0805 ≥ 10 V, GND pads toward A1–A4, GND vias at both ends
- [ ] Decide AVDD 3.3 V vs 2.8 V once the `+BATT` range is known
- [ ] PWR_FLAGs, fix the libraries, MIC23150 footprint attribute → SMD, J1/J4 annotation, courtyard overlaps
- [ ] Re-run ERC and DRC with schematic parity: 0 errors, 0 parity issues
