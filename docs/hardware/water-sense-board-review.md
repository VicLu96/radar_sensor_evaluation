# `water_sense_board` — review against nRF Connect SDK v3.3

Reviewed 2026-09-04. **Claude has not edited and will not edit any board file** — see
CLAUDE.md. Everything below is a proposal for Victor to apply or reject.

Confirmed inputs: module **ISP2454-LX**, SDK **NCS v3.3**, pins SCL P1.08, SDA P1.13,
SDI P2.04, **CS P2.05**, SDO P2.02, SCK P2.01, **AP_CLK P0.00**.
(The 2026-09-04 P0.00 correction turned out to be about AP_CLK, not CS; CS is P2.05 as
originally given, and AP_CLK's first value P0.13 is not a pin on this SoC.)

---

## The headline

**The board files cannot build under NCS v3.3 as written, and no application overlay
can rescue them.** They are written against the nRF54L15 *preview* generation, and the
failures are in the board's own includes and `chosen` nodes — before any overlay is
merged. This is a migration, and it is Victor's to make.

Three independent things all say "preview era":

| In the board file | Under NCS 3.x |
|---|---|
| `boards/arm/<board>/` with `Kconfig.board`, `Kconfig.defconfig`, `<board>_defconfig` | **Hardware model v1.** Introduced-then-deprecated in Zephyr 3.7 and removed in the Zephyr 4.x line that NCS 3.x is built on. The board is simply not discovered |
| `#include <nordic/nrf54L15_M33.dtsi>` | Path does not exist. The SoC include is `nordic/nrf54l15_cpuapp.dtsi` (lowercase `l`, `cpuapp` not `M33`) |
| `CONFIG_SOC_NRF54L15_M33=y`, `depends on SOC_NRF54L15_M33` | Symbol does not exist. It is `SOC_NRF54L15_CPUAPP` |

Consequences that follow from the same change: `&flash0` becomes `&cpuapp_rram`,
`&sram0` becomes `&cpuapp_sram`, `&gpiote` becomes a numbered instance, and the board
target on the command line becomes `water_sense_board/nrf54l15/cpuapp` rather than
`water_sense_board`.

**Confidence:** high on the direction, and it should be checked against the actual tree
rather than taken on faith — `zephyr/boards/nordic/nrf54l15dk/` in the installed SDK is
the authoritative template, and diffing against it will be faster than reading this.

---

## Defects independent of the SDK version

These are wrong regardless of which SDK you build with.

### 1. SPI chip select — Victor drives it from the port file

**Decided 2026-09-04: CS on P2.05 is handled in software in the port file, not by
Zephyr.** Withdrawn as a defect; the pinctrl file is correct to omit it, since CS is not
a pinctrl signal on nRF in any case.

Nothing about the SPIM peripheral objects to this. SPIM does not drive chip select
itself — Zephyr's driver toggles a plain GPIO from `cs-gpios` — so a port layer toggling
the same GPIO around a transaction is doing exactly what the driver would have done, and
is the normal arrangement for SD-over-SPI where CS must stay asserted across several
transactions.

**But it conflicts with the `sdhc0` node already in the board file.** `sdhc0` is
`compatible = "zephyr,sdhc-spi-slot"` with `reg = <0>`, and a `mmc` child that is
`zephyr,sdmmc-disk`. That is Zephyr's SD-over-SPI stack, and it identifies the card as
SPI device 0 on the bus — meaning it expects the controller's `cs-gpios[0]` to exist and
be driven for it. With CS owned by the port file instead, that stack has no chip select
and the two approaches fight over the same pin.

So one of the two has to go, and it is Victor's choice which:

| | Keep `sdhc0` | Port file owns CS |
|---|---|---|
| Board file | add `cs-gpios = <&gpio2 5 GPIO_ACTIVE_LOW>;` to the SPI node | drop the `sdhc0` and `mmc` nodes |
| Card access | Zephyr's disk/FAT stack, `CONFIG_SDMMC_SUBSYS` | raw SPI transactions from your own code |
| Cost | none, it is the stock path | you write the SD command layer |

The two are not combinable: Zephyr will assert and de-assert CS around each transfer as
soon as the SPI device has a `cs-gpios` entry, and manual toggling on top of that
produces glitches that look like card timeouts.

**This is only about the SD card.** It has no bearing on the VL53L9CX, which is on I²C
and uses no chip select at all.

### 2. The wrong GPIO ports are enabled

`&gpio0` is enabled and nothing uses it. `&gpio1` (I²C) and `&gpio2` (SPI, and CS above)
are not enabled.

I²C and SPI data lines survive this — nRF pinctrl programs the peripheral's PSEL
registers and does not need the GPIO driver. **A GPIO does need it**, so this one bites
whichever way the CS question above is answered: `cs-gpios` needs `gpio2`, and so does a
port file toggling P2.05 itself.

### 3. No console

Nothing sets `zephyr,console` and no UART is enabled. `printk` and logging go nowhere,
so a board that boots perfectly and a board that never boots look identical. On a
first-ever bring-up with no known-good reference that is expensive.

Either enable a UART with pinctrl and add `zephyr,console`/`zephyr,shell-uart`, or use
RTT (`CONFIG_USE_SEGGER_RTT=y`, `CONFIG_RTT_CONSOLE=y`) which costs no pins — attractive
here given the pin budget is already committed.

### 4. `zephyr,code-partition` requires an MCUboot build

`zephyr,code-partition = &slot0_partition` links and flashes the image at `0xc000`.
Correct **with** MCUboot in the build; without a bootloader the chip never jumps to it
and the board is indistinguishable from dead. Either build with sysbuild + MCUboot, or
drop that `chosen` line until you do.

---

## The partition table

**The current table covers 512 KB of the 1428 KB available to the application core.**
48 + 220 + 220 + 24 = 512 KB, leaving 916 KB — about two thirds of the memory — unmapped
and unusable. The 220 KB image slots are also tight: this application already carries
ST's driver plus a 9,865-byte firmware blob, and adding BLE will not fit comfortably.

Your `.yaml` already states the right numbers (`ram: 188`, `flash: 1428`) — those are the
nRF54L15 application-core figures, with the remainder reserved for the FLPR core. The
partitions just never caught up with them.

### Proposed, MCUboot layout — spans the full 1428 KB

```dts
&cpuapp_rram {
	partitions {
		compatible = "fixed-partitions";
		#address-cells = <1>;
		#size-cells = <1>;

		boot_partition: partition@0 {
			label = "mcuboot";
			reg = <0x00000000 DT_SIZE_K(64)>;
		};

		slot0_partition: partition@10000 {
			label = "image-0";
			reg = <0x00010000 DT_SIZE_K(668)>;
		};

		slot1_partition: partition@b7000 {
			label = "image-1";
			reg = <0x000b7000 DT_SIZE_K(668)>;
		};

		storage_partition: partition@15e000 {
			label = "storage";
			reg = <0x0015e000 DT_SIZE_K(28)>;
		};
	};
};
```

The arithmetic, so it can be checked rather than trusted:

| Partition | Start | Size | Ends |
|---|---|---|---|
| mcuboot | `0x00000` | 64 KB (`0x10000`) | `0x10000` |
| image-0 | `0x10000` | 668 KB (`0xA7000`) | `0xB7000` |
| image-1 | `0xB7000` | 668 KB (`0xA7000`) | `0x15E000` |
| storage | `0x15E000` | 28 KB (`0x7000`) | `0x165000` |

`0x165000` = 1,462,272 bytes = **1428 KB exactly**. No gaps, no overlap, nothing left
stranded.

Note the node is `&cpuapp_rram`, not `&flash0` — part of the same migration. The
nRF54L15's non-volatile memory is RRAM.

### If you would rather not run MCUboot yet

Drop `zephyr,code-partition` and give the application everything:

```dts
&cpuapp_rram {
	partitions {
		compatible = "fixed-partitions";
		#address-cells = <1>;
		#size-cells = <1>;

		slot0_partition: partition@0 {
			label = "image-0";
			reg = <0x00000000 DT_SIZE_K(1400)>;
		};

		storage_partition: partition@15e000 {
			label = "storage";
			reg = <0x0015e000 DT_SIZE_K(28)>;
		};
	};
};
```

Simpler, boots from a plain `west flash`, and removes defect 4 outright. DFU can come
later — adding MCUboot then is a partition change and a rebuild, not a redesign.

---

## Smaller things

- `label = "GPIO0"` — the devicetree `label` property was removed in Zephyr 3.x. Inert,
  but it is a marker of the template's age.
- `&i2c1` has no `clock-frequency`, so it defaults to 100 kHz. Fine for the SD-card work;
  it needs `<I2C_BITRATE_FAST>` before the VL53L9CX, where it is the difference between a
  404 ms frame and a 1.6 s one.
- `spi-max-frequency = <24000000>` — worth confirming that SPIM instance reaches 24 MHz.
- The I²C pinctrl has no `bias-pull-up`. Correct if the board has external pull-ups;
  worth confirming, since it is silent either way until the bus does not work.
- `Kconfig.board`'s prompt string is `"water_sense_board-pinctrl"` — cosmetic, looks
  copy-pasted from the pinctrl file.
- `water_sense_board.yaml` has no trailing newline and no `supported:` list. Twister-only.

---

## NCS 3.3 migration checklist

Everything needed to make `water_sense_board` build under NCS v3.3. Work down it; the
first item is structural and the rest are renames that follow from it.

**Before starting, settle one thing:** open `zephyr/boards/nordic/nrf54l15dk/` in the
installed tree. That is the authoritative template, it answers every naming question
below at once, and diffing against it is faster than working from this list. Everything
here is high-confidence but written without an SDK to check against.

`firmware/boards/pbl/vl53l9_node/` in this repository is already a hardware-model v2
board for the same SoC. It has never been built either, but the *shape* is right and it
may be a quicker starting point than converting in place.

### 1. Layout — hardware model v1 to v2

This is the structural change and the reason nothing currently builds: an hwmv1 board is
not discovered at all by the Zephyr 4.x line NCS 3.x sits on.

| Now | Under hwmv2 |
|---|---|
| `boards/arm/water_sense_board/` | `boards/<vendor>/water_sense_board/` — the `arm/` level goes away |
| `Kconfig.board` | `Kconfig.water_sense_board` |
| `Kconfig.defconfig` | kept, same name |
| — | **`board.yml`** — new, and required. Names the board and its SoC |
| `water_sense_board_defconfig` | `water_sense_board_nrf54l15_cpuapp_defconfig` |
| `water_sense_board.dts` | `water_sense_board_nrf54l15_cpuapp.dts` |
| `water_sense_board-pinctrl.dtsi` | `water_sense_board_nrf54l15_cpuapp-pinctrl.dtsi` |
| `-b water_sense_board` | `-b water_sense_board/nrf54l15/cpuapp` |

`board.yml` is the one genuinely new file:

```yaml
board:
  name: water_sense_board
  full_name: Water sense board (ISP2454-LX)
  vendor: ethzurich
  socs:
    - name: nrf54l15
```

And `Kconfig.water_sense_board` becomes a select rather than a `depends on`:

```
config BOARD_WATER_SENSE_BOARD
	select SOC_NRF54L15_CPUAPP if BOARD_WATER_SENSE_BOARD_NRF54L15_CPUAPP
```

### 2. SoC identity

| Now | Under NCS 3.3 |
|---|---|
| `#include <nordic/nrf54L15_M33.dtsi>` | `#include <nordic/nrf54l15_cpuapp.dtsi>` |
| `CONFIG_SOC_NRF54L15_M33=y` | `CONFIG_SOC_NRF54L15_CPUAPP=y` |
| `CONFIG_SOC_SERIES_NRF54X=y` | `CONFIG_SOC_SERIES_NRF54LX=y` — **verify the exact series symbol** |
| `depends on SOC_NRF54L15_M33` | see the `select` above |

### 3. Memory nodes

| Now | Under NCS 3.3 | Why |
|---|---|---|
| `zephyr,sram = &sram0` | `&cpuapp_sram` | per-core naming |
| `zephyr,flash = &flash0` | `&cpuapp_rram` | the nRF54L15 has **RRAM**, not flash |
| `&flash0 { partitions ... }` | `&cpuapp_rram { partitions ... }` | same |

Combine this with the resized partition table above — one edit, not two.

### 4. Peripheral instances

The nRF54L15 numbers peripherals by power domain, not sequentially. There is no `i2c1`,
no `spi2`, no bare `gpiote`.

| Now | Becomes | Note |
|---|---|---|
| `&i2c1` | `&i2c20`, `&i2c21` or `&i2c22` | The 20-series lives in the domain that serves **P1** pins, which is where SCL P1.08 and SDA P1.13 are. Consistent |
| `&spi2` | most likely **`&spi00`** | The 00 instance is the fast one and serves **P2** pins — where SCK P2.01, SDI P2.04, SDO P2.02 are. It is also the only one likely to reach the `spi-max-frequency = <24000000>` already in the node; the 20-series instances are slower. **Verify both claims** against the SDK's pin/instance tables |
| `&gpiote` | `&gpiote20` and/or `&gpiote30` | numbered per domain |
| `&wdt0` (alias) | `&wdt30` or `&wdt31` | `wdt0` probably does not exist |
| `&adc` | likely unchanged | but check `NRF_SAADC_AIN2`/`AIN4` still resolve — nRF54L has its own SAADC binding header |

**The pin domains are the useful cross-check here.** Your I²C is entirely on P1 and your
SPI entirely on P2, which is exactly how the nRF54L15 splits its peripheral domains. That
is a good sign the hardware design is coherent; it also means the instance choice is
largely forced, not free.

### 5. Carried over from the review above

Not NCS 3.3 issues, but they are in the same files and this is one editing pass:

- enable `&gpio1` and `&gpio2`, drop the unused `&gpio0` (or leave it, but it does
  nothing)
- resolve CS: either `cs-gpios` on the SPI node, or drop `sdhc0`/`mmc` and drive it from
  the port file
- add a console, or accept a silent board
- drop `zephyr,code-partition` unless MCUboot is in the build
- remove `label = "GPIO0"` — the property was removed in Zephyr 3.x

### 6. What is already done on this side

- `west.yml` pinned to **v3.3.0**.
- The driver, its bindings, the platform layer and the bring-up app use no APIs that
  moved between 3.7 and 4.x. `DEVICE_DT_INST_DEFINE`, `PM_DEVICE_DT_INST_DEFINE`,
  `pm_device_driver_init`, the I²C/GPIO/PWM `_dt` accessors and the logging macros are
  all current. No changes expected there — though "expected" is doing real work in that
  sentence, since none of it has been compiled.
- One thing to watch rather than fix in advance: **NCS 3.x runs sysbuild by default**. If
  MCUboot ends up in the build, its configuration goes in `sysbuild.conf`, not `prj.conf`.

---

## What the VL53L9CX will need, and where it goes

Not in the board file. The sensor node, the AP_CLK PWM and the I²C speed belong in an
**application-level `.overlay`**, which is the correct Zephyr mechanism for
application-specific hardware and leaves `water_sense_board` untouched.

That overlay is not written yet, deliberately: it has to reference node labels
(`&i2c1` or whatever it becomes, the PWM instance) that the migration above may change.
It is half an hour's work once the board builds.

One item from it does need Victor's attention early, because it is a hardware question
rather than a software one: **AP_CLK**. The sensor does not acknowledge its I²C address
until a 6–27 MHz clock is running on that pin, and the current pin list does not mention
one. Either the board has an oscillator for it, or a PWM-capable pin has to be found and
committed. See `docs/plan/st-package-audit.md` §7.
