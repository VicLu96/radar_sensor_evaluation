# UM3683: the power-on conditions, and what they explain

Source: **UM3683 Rev 3**, *VL53L9CX programming guide*, added to the repo 2026-09-06 at
`docs/um3683-programming-guide-stmicroelectronics.pdf`. Section and table numbers below are
from that document.

## The three conditions (§2.5.1) — and the sentence that matters most

The device leaves `POWER_OFF` only when **all three** hold:

1. **All three supplies up**: AVDD, DVDD, IOVDD.
2. **XSHUT high**, at IOVDD level. *(Victor, 2026-09-06 — and ST states it outright.)*
3. **The external clock is active.**

They may be met in any order. Then the part enters `READY_TO_BOOT`.

> *"If at any point, one of the three conditions becomes invalid, the sensor returns to the
> off state (or RESET for the XSHUT pin)."*

**That sentence retroactively explains the whole bring-up failure.** An intermittent AP_CLK
does not degrade communication or corrupt a frame — it **returns the part to POWER_OFF**.
The clock ran in bursts that tracked CPU activity, so the sensor was being reset
continuously, and a device in `POWER_OFF` cannot acknowledge its address at any speed, at
either address, no matter how healthy the bus looks. The clean NAKs and correct pin levels
were exactly what this failure should look like.

It also settles the priority order: **there was never any point testing I²C before the
clock was continuous.** Condition 3 is not a precondition for good data, it is a
precondition for the device existing on the bus at all.

**Three supplies, not two.** The driver models `vdda` and `vddio`; DVDD is the third and
the custom board's `+1V2` rail is it. Nothing in firmware configures it, but it is a
power-on condition, so it belongs on the bench checklist.

## DEVICE_ID is a mandated check, and we were not doing it (§2.5.2)

Register `0x0000` must read **`0x53334C39`** — `"S3L9"` in ASCII. ST lists this as the
firmware start-up check the driver *must* perform.

The driver read this value and logged it without ever comparing it. That passes as long as
anything acknowledges, so a wrong part at the address, or a half-powered device returning
zeros, both counted as success and failed later somewhere less informative. Now validated,
at the point where the clean path and the bus-recovery path converge, with the all-zeros
and all-ones cases called out separately since those mean "bus idle level", not "device".

## SYSTEM_FSM: ask the device where it thinks it is (Table 8)

| Register | Value | State |
|---|---|---|
| `0x008C` | `0x00` | `FSM_NONE` — has not left POWER_OFF |
| | `0x01` | `FSM_READY_TO_BOOT` |
| | `0x02` | `FSM_STANDBY` |
| | `0x03` | `FSM_STREAMING` |

`COMMAND_ERROR` at `0x008D` (`NONE` / `FORBIDDEN` / `INVALID`) and `FRAME_READY` at
`0x008E` sit alongside it.

The driver now logs the FSM state right after a successful probe. Expect `READY_TO_BOOT`
there. Reading `NONE` from a part that just answered is the precise signature of a supply
or clock that is present but not *holding* — which is the failure mode §2.5.1 describes,
made directly observable instead of inferred.

## The boot sequence (§2.5.2), against what the driver does

ST's required order:

1. Check `DEVICE_ID`.
2. Write `EXT_CLOCK` with the clock frequency in Hz.
3. Write `VDDA_CFG` / `VDDIO_CFG` **only if they differ from the defaults**.
4. Write the firmware patch to `0x1800`, set `INSTALL_PATCH`, issue `BOOT`.

**The defaults are AVDD 2.8 V and IOVDD 1.8 V.** Worth noting for both boards:

- **X-NUCLEO-53L9A1** (2.8 V / 1.8 V) sits exactly on the defaults — nothing to write.
- **The custom board** at 3.3 V needs `VDDA_CFG` written. This is the value that was a
  placeholder at 2.8 V until 2026-09-04, and it is now clear why a wrong value fails
  quietly: it is a *configuration* write, not a check, so it succeeds and misconfigures the
  analogue front end.

VDDIO is a two-way choice, 1.2 V or 1.8 V, which confirms the binding's
`VDDIO_1V2 = 0, VDDIO_1V8 = 1` encoding.

## Two smaller confirmations

**Frame synchronisation (§2.7.2)** — three streaming modes: Autonomous (frame-period
register), Manual (host triggers each frame), Follower (`SYNC_IN`, **active low**). The
driver holds `SYNC_MANUAL`, so `SYNC_IN` cannot start an exposure. The pin still wants a
defined level rather than a float.

**Addressing (§2.8.1)** — the default is **`0x29`, stated as 7-bit**. That closes the
0x29-versus-0x52 question from the other direction: ST's `0x52` is the 8-bit form, exactly
as the driver's README argued. A custom address is possible via OTP, so a device programmed
by someone else could differ — not a concern for new parts.

## What this changes on the bench

1. **Confirm the clock is continuous before anything else.** Not a preference — a device
   without it is in `POWER_OFF` and cannot answer.
2. **Check all three rails**, including `+1V2` (DVDD), which nothing in firmware touches.
3. **Read the new log lines.** `device id 0x53334c39 ("S3L9") — correct` followed by
   `state machine: 0x01 (READY_TO_BOOT — expected here)` means the three power-on
   conditions are genuinely met and the remaining risk is the blob upload. Anything else
   now names its own failure.
