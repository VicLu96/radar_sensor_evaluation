# Can BLE carry full-resolution frames, and at what rate?

Written 2026-09-10, at Victor's request. **Every number here is an ESTIMATE derived from
protocol arithmetic, not a measurement.** BLE is not even enabled in the current build
(`CONFIG_BT` is unset), so nothing here has been run.

> **This analysis describes a path the project has ruled out.** `CLAUDE.md`: *"Counts leave
> the device, frames never do. The privacy claim is architectural and free — do not add a
> raw-frame transmit path."* The arithmetic is still worth having: it quantifies what the
> rule costs, which is exactly the sort of thing a reviewer asks about, and it settles
> whether the radio or the sensor bus is the real constraint. It is not a proposal to build
> it.

---

## The short answer

**~2.5 fps, and BLE is not the reason.** The I²C read of a 54×42 frame is the bottleneck by
a factor of two to four. Raising the bus to 1 MHz lifts the ceiling to ~6 fps, at which
point BLE and I²C become comparable and the radio starts to matter.

| Link | Full frame (14,842 B) | Distance-only (4,536 B) |
|---|---|---|
| **I²C @ 400 kHz** (today) | **~2.5 fps** | same — the whole frame is read regardless |
| **I²C @ 1 MHz** (Fm+) | **~6.2 fps** | same |
| BLE 2M PHY, practical | ~7–12 fps | ~22–38 fps |
| BLE 2M PHY, theoretical max | ~12 fps | ~38 fps |

The sensor reads the entire frame off the device whichever fields you later transmit, so
trimming the BLE payload does not raise the frame rate — it only reduces radio time and
energy.

---

## Where the BLE numbers come from

Assuming the best case the nRF54L15 supports: **LE 2M PHY**, Data Length Extension with
251-byte PDUs, ATT MTU 247 (244 bytes of payload per notification), and a connection
interval long enough to pack many packets per event.

Air time for one full notification at 2 Mbps:

```
preamble 2 B + access address 4 B + header 2 B + payload 251 B + MIC 4 B + CRC 3 B
  = 266 B = 2128 bits ÷ 2 Mbps       = 1.064 ms
+ T_IFS                               = 0.150 ms
+ central's empty response (~10 B)    = 0.040 ms
+ T_IFS                               = 0.150 ms
                                        --------
per notification carrying 244 B         1.404 ms
```

→ **174 kB/s (1.39 Mbps) theoretical.** Real stacks reach roughly 60–80% of that once
connection-event boundaries, scheduling and retransmissions are accounted for, so
**~105–140 kB/s** is the honest working figure.

| Payload | Bytes | @105 kB/s | @140 kB/s | @174 kB/s |
|---|---|---|---|---|
| Full raw frame | 14,842 | 141 ms → 7.1 fps | 106 ms → 9.4 fps | 85 ms → 11.7 fps |
| Distance + validity only | 4,536 | 43 ms → 23 fps | 32 ms → 31 fps | 26 ms → 38 fps |
| Counts (what we actually send) | ~10 | negligible | negligible | negligible |

"Distance + validity" is 2 bytes per zone and needs no packing work: the 15-bit distance
and the validity flag are already one 16-bit word on the wire.

---

## Why I²C wins the bottleneck

From [frame-rate-budget.md](frame-rate-budget.md), corrected against ST's source on
2026-09-01: a 54×42 frame is **14,842 bytes** — three 16-bit zone planes (13,608) plus the
1,134-byte DSS array plus the fixed 100-byte status line.

| I²C clock | Effective | Transfer | Max fps |
|---|---|---|---|
| 400 kHz | ~40 KB/s | ~404 ms | **~2.5** |
| 1 MHz | ~100 KB/s | ~162 ms | **~6.2** |

*(A pure 9-bits-per-byte calculation gives 334 ms at 400 kHz. The 404 ms above includes
START/STOP, ACK and inter-byte overhead and is the figure to plan against; 334 ms is a
floor, not an expectation.)*

ST's own figure for comparison, UM3683 Table 1: **4 fps** at 54×42 over I²C — which implies
~1 MHz, since our 400 kHz cannot reach it.

So at 400 kHz the radio has roughly **3–5× headroom** over the bus for full frames. At
1 MHz the two land in the same range and BLE becomes co-limiting at the conservative end.

---

## The energy answer, which matters more here

Frame rate is not the interesting constraint for this project — energy is.

Rough per-frame comparison at full resolution *(estimates; the sensor figure is ST's
unmeasured 150 mW headline and the radio figure assumes ~6 mA TX average)*:

| | Time | Energy @3.3 V |
|---|---|---|
| Sensor active + 404 ms bus read | ~450 ms | **~60 mJ** |
| BLE full frame, 14.8 kB | ~140 ms | ~2.8 mJ |
| BLE distance-only, 4.5 kB | ~43 ms | ~0.85 mJ |
| BLE counts, ~10 B | <1 ms | negligible |

So transmitting a full raw frame adds only **~5%** to the per-frame energy. The radio is
not what makes frame streaming expensive.

**What makes it expensive is what it prevents.** Streaming forces a connection interval
short enough to move 14.8 kB per frame, which keeps the radio scheduled and the SoC out of
its deepest sleep states between frames. At the 0.05–0.2 Hz dwell rate this design targets
(see [room-occupancy.md](room-occupancy.md)), the sensor is active a few percent of the
time and *everything else is idle current*. A connection maintained for streaming would
dominate the multi-month battery claim far more than the 2.8 mJ per frame suggests.

That is the real argument for counts-only, and it is an energy argument as much as a
privacy one.

---

## What would have to be measured before trusting any of this

1. **Actual BLE throughput on this SoC and stack.** Everything above is protocol
   arithmetic. Zephyr's achieved notification rate on the nRF54L15 with 2M PHY has not been
   measured here.
2. **The 404 ms I²C read**, on the scope. It is derived, not timed. The driver already
   reports capture duration, so this is nearly free.
3. **Sensor active power.** Still ST's 150 mW headline with nothing measured behind it —
   flagged since 2026-09-04 and the largest single uncertainty in the table above.
4. **Idle current with a BLE connection maintained**, if anyone ever wants to argue the
   streaming case properly. That is the number that would actually decide it.
