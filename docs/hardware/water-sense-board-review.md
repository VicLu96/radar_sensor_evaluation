# `water_sense_board` — review against nRF Connect SDK v3.3

Reviewed 2026-09-04. **Claude has not edited and will not edit any board file** — see
CLAUDE.md. Everything below is a proposal for Victor to apply or reject.

Confirmed inputs: module **ISP2454-LX**, SDK **NCS v3.3**, pins SCL P1.08, SDA P1.13,
SDI P2.04, CS P2.05, SDO P2.02, SCK P2.01.

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

### 1. SPI chip select is missing — P2.05 appears nowhere

You listed CS on P2.05. It is not in the pinctrl file and not on the `&spi2` node.

This is not an oversight in the pinctrl file, because **CS is not a pinctrl signal on
nRF**. Zephyr's SPIM driver drives chip select as an ordinary GPIO, so it belongs on the
controller node:

```dts
&spi2 {
	cs-gpios = <&gpio2 5 GPIO_ACTIVE_LOW>;
	/* ... */
};
```

Without it, `sdhc0` has no way to select the card and the SD interface cannot work. The
`reg = <0>` on `sdhc0` indexes into `cs-gpios`, so the node is already written expecting
this line to exist.

### 2. The wrong GPIO ports are enabled

`&gpio0` is enabled and nothing uses it. `&gpio1` (I²C) and `&gpio2` (SPI, and CS above)
are not enabled.

I²C and SPI data lines survive this — nRF pinctrl programs the peripheral's PSEL
registers and does not need the GPIO driver. `cs-gpios` does need it, so this defect and
the one above will present together as "the SD card does not work".

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
