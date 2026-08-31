# ISP2454-LL / nRF54L15 — MCU reference

Researched 2026-08-31. **VERIFY** marks anything not confirmed.

## The module

**Insight SiP ISP2454-LL** — a System-in-Package built around Nordic's **nRF54L15**,
with RF matching, embedded antenna and both crystals integrated. 8 × 8 × 1 mm LGA.

| Parameter | Value |
|---|---|
| SoC | Nordic **nRF54L15** |
| Core | Arm **Cortex-M33 @ 128 MHz**, plus a Cortex-M0+ for power management |
| Memory | **1.5 MB** non-volatile, **256 KB** RAM |
| Radio | 4th-gen 2.4 GHz; **Bluetooth 6.0**, Mesh, Thread, Matter, NFC-A, proprietary |
| Notable | TrustZone, Channel Sounding, AoA direction finding |
| Integrated | RF matching, antenna, 32 MHz + 32 kHz crystals, DC-DC |
| Temperature | to +105 °C |
| Variants | -LL, -LX, -LP share the footprint — **VERIFY** what distinguishes -LL |

## Why the memory numbers are comfortable here

- **1.5 MB flash** against a sensor firmware blob of ~84 KB (VL53L8CX scale, VL53L9CX
  **VERIFY**). Even at 4× that, the blob costs well under a quarter of flash. Fine.
- **256 KB RAM** against ~9 KB for one full-resolution distance frame. Holding several
  frames for temporal filtering, or a background model, is comfortable. A classical
  tracker fits easily; a small CNN is plausible.

Neither is the constraint. **Sensor energy is.**

## The one architectural feature that matters for power

The nRF54L15 splits into **separate power domains** — a fast MCU domain, a radio
domain, a peripheral domain, and a **low-power domain**. Peripheral instances exist in
different domains and *cost different amounts of energy for the same work*: Nordic's
own measurements on the DK show **SPIM30 (low-power domain) consuming less than SPIM20**
for the same transfer.

**Consequence for this design:** the I²C instance used to talk to the sensor should be
chosen from the low-power domain wherever the pin mapping allows, and the choice should
be *measured*, not assumed. Over a multi-year deployment moving 9 KB per frame, the
difference between peripheral instances is not a rounding error.

Confirm the exact instance names and domain mapping in the datasheet, and confirm the
ISP2454-LL brings out the right pins. **VERIFY.**

## I²C — Victor's decision, 2026-08-31

The VL53L9CX offers I²C and I3C on shared pins. **We use I²C.**

- **Whether the nRF54L15 has an I3C peripheral at all is unconfirmed** and, given this
  decision, does not need to be resolved. Worth one line in the paper as a stated
  limitation rather than an oversight.
- Practical ceiling: the nRF54L15 TWIM supports standard rates; **whether 1 MHz is
  available on this part must be VERIFIED**, and it is worth the check — it halves the
  per-frame bus time and energy versus 400 kHz.
- I3C would have offered 12.5 MHz. Choosing I²C costs roughly an order of magnitude in
  transfer time. At the ~1 Hz frame rates this application wants, that is an acceptable
  trade for a far simpler bring-up — but it does cap any future high-frame-rate work,
  and the paper should say so plainly.

## Toolchain

- **nRF Connect SDK** (Zephyr-based). nRF54L15 support is recent; **pin the SDK version
  in `west.yml` from day one** and record it in `DECISIONS.md` — Nordic moves fast on
  new silicon and an unpinned tree will not reproduce.
- Board support: the nRF54L15-DK is the reference. A custom board file will be needed
  for the ISP2454-LL; start from the DK's and strip it.
- No in-tree Zephyr driver for VL53L9CX exists (**VERIFY** — Zephyr has drivers for
  older VL53L0X/L1X). Expect to wrap ST's ULD-style driver ourselves. See the
  implementation plan.

## Development hardware worth having early

- **nRF54L15-DK** — bring up firmware before the custom board exists.
- **X-NUCLEO-53L9A1** or **STEVAL-VL53L9** — a known-good sensor reference. When our
  I²C bring-up returns nothing, the only fast way to tell "broken driver" from "broken
  board" is a board that already works.
- **Power Profiler Kit II** — non-negotiable for this project. The entire contribution
  is an energy argument; it has to be measured, not modelled.

## Open questions — **VERIFY**

1. What distinguishes ISP2454-**LL** from -LX and -LP (antenna? RF path? pinout?).
2. Which pins the module brings out, and whether a low-power-domain TWIM instance can
   reach them.
3. Maximum I²C clock supported by the nRF54L15 TWIM on this part.
4. nRF Connect SDK version with stable nRF54L15 support — then pin it.
5. Whether Insight SiP publishes a Zephyr board definition for the ISP2454.

## Sources

- [Nordic nRF54L15 product page](https://www.nordicsemi.com/Products/nRF54L15)
- [nRF54L15/L10/L05 datasheet (v0.8)](https://www.mouser.com/datasheet/2/297/nRF54L15_nRF54L10_nRF54L05_Datasheet_v0_8-3568773.pdf)
- [Insight SiP ISP2454 product page](https://www.insightsip.com/products/bluetooth-le-modules/isp2454)
- [ISP2454 datasheet](https://www.insightsip.com/fichiers_insightsip/pdf/ble/ISP2454/isp_ble_DS2454.pdf)
- [Insight SiP variant announcement](https://www.insightsip.com/news/in-the-press/745-insight-sip-expands-its-isp2454-series-product-range-to-offer-more-flexible-options-for-customers)
- [Nordic DevZone — nRF54L15 low-power domain](https://devzone.nordicsemi.com/f/nordic-q-a/120201/nrf54l15-low-power-domain)
