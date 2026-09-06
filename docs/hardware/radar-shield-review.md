# `radar_shield` KiCad review — two findings

Reviewed 2026-09-06 from `radar_shield.kicad_sch` / `.kicad_pcb`, against the symptoms
seen on the bench: a 100 mA supply limit on power enable, and a VL53L9CX that never
acknowledges on I²C.

Method: pad-to-net extracted from the PCB (the authoritative connectivity), pin names
extracted from the schematic symbol, and the two compared.

---

## 1. SDA and SCL look crossed at the sensor — and this would explain the NAK

The symbol's pin names and the nets attached to them disagree on exactly two balls:

| Ball | Symbol pin name | Net attached |
|---|---|---|
| **A11** | **SCL** | **`/SDA`** |
| **A12** | **SDA** | **`/SCL`** |

Every other signal matches: `A10 INTR → /Interrupt`, `B12 XSHUT → /XSHUT`,
`D12 SYNC_IN → /SYNC_IN`, `E11 AP_CLK → /AP_CLK`, and all four CSI-2 lanes.

The nets are consistent from the connector inward — `J6` is
`1:/SDA, 2:/SCL, 3:/XSHUT, 4:/SYNC_IN` — so whatever the host calls SDA arrives at the
ball the symbol calls SCL.

**Why this fits the evidence exactly.** With clock and data exchanged the device never
sees a valid I²C clock, so it cannot acknowledge — at any address. That is precisely what
the bench shows: a healthy bus (clean NAKs, ~10 ms each, no timeouts since the pull-ups
were fixed), correct pin levels, and silence at both 0x29 and 0x52. It also explains why
the LSM6DSV worked on the same bus: the crossing is at the sensor's own balls, not on the
bus.

**What this does not settle.** Whether the *symbol* is right. Two readings:

- the symbol is correct and the schematic wires SDA to the SCL ball — a wiring error;
- the symbol's pin names are swapped relative to the real ball map, and the nets were
  labelled by true function — two errors cancelling, and the hardware is fine.

Only the datasheet ball map distinguishes them, and it is not in this repository.

**But it is testable in one line, with no hardware change.** Swap `TWIM_SCL` and
`TWIM_SDA` in the board's `i2c1_default` pinctrl group and rebuild. If the sensor
answers, the schematic is crossed. If nothing changes, the symbol was wrong and the
wiring was right all along. Either way the question closes in one boot, which is cheaper
than reading a ball map under a microscope.

---

## 2. The 100 mA limit is expected, not a fault

**No short exists.** Every two-terminal part was checked: no component has both pads on
one net, every capacitor sits between a supply and GND, and each inductor runs from its
converter's switch node to its output. Nothing is miswired on the power side.

What `/Power_Enable` actually does is start **four regulators simultaneously**:

| Part | | Rail | Output capacitance |
|---|---|---|---|
| S2 | SIP4282ADNP3 load switch | `+VBat_switched` | 20.1 µF |
| U202 | MIC23150-4YMT buck | `+1V2` | 4.8 µF |
| U203 | MIC23150-SYMT buck | `+3V3` | 4.8 µF |
| U204 | MIC23150-GYMT buck | `+1V8` | 4.8 µF |
| | | **total** | **34.5 µF** |

All four share one enable, so all four inrush at once.

The load switch alone accounts for it. Charging 20.1 µF from a battery rail is
`I = C·dV/dt`: a 100 µs ramp at 3.7 V draws about **740 mA**; even a 1 ms ramp draws
about **74 mA**, before the three converters have charged anything. *(Arithmetic from the
schematic capacitance — an estimate for sizing, not a measurement.)*

So a 100 mA bench limit is below this design's normal switch-on transient. That is a
limit set too low, not a board fault.

**The two findings interact, and the order matters.** A supply in fold-back holds the
rails below the sensor's operating minimum, so the part never boots — producing the same
silence as finding 1. Raise the limit to 500 mA or more first, then retest the I²C
question. Testing them in the other order proves nothing.

**And for the paper**, this is where the real numbers come from. The energy model
currently carries ST's 150 mW headline and nothing measured. The inrush, the per-rail
steady current and the VCSEL peak are all measurable on this board once the limit is
raised, and all three matter more than the headline.

---

## Smaller observations

- `RSVD1` (E3) and `RSVD2` (E12) are left unconnected. Correct if the datasheet says so;
  worth one check, since reserved balls are occasionally required to be grounded.
- `VBAT_LDD` (B1) and `VBAT_RX` (D1) both sit on `+VBat_switched`, straight from the
  battery through the load switch. That is the laser driver supply, and it is the rail
  whose peak current the paper needs to characterise.
- `SYNC_IN` is brought out to `J6` pin 4 rather than tied. It is active low and triggers
  a frame, so it must not float. The driver now holds the device in `SYNC_MANUAL` so the
  pin cannot start an exposure, but the pin itself still wants a defined level.
