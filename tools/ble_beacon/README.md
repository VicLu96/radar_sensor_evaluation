# Zephyr `beacon` on water_sense_board

Zephyr's own beacon sample — about forty lines, non-connectable advertising,
nothing else — built for this board with the console on RTT. **It shares none
of our code.**

```bash
west flash --build-dir tools/ble_beacon/build
```

It advertises as **"Test beacon"** (the sample's own name), non-connectable,
with an Eddystone URL payload. Scan with nRF Connect on a phone.

| Result | Meaning |
|---|---|
| **Visible** | Our application is at fault, and it is somewhere in ~3,000 lines across four GATT services, a streaming thread, a sensor driver and a staged bring-up. A real result and a tractable search. |
| **Invisible** | Our code is exonerated completely. Nothing we write will fix this, and the question moves to the controller, the board, or the module. |

91,584 B against firmware_test's 190,928.

## Why this rather than a spectrum analyser

Same logic as `tools/radio_test`, but it stays inside BLE and produces something
a phone can see — no RF instrument, no antenna access. It is the cheapest
remaining way to draw a line between "our firmware" and "everything below it",
and after 2026-09-11 that line is the thing most worth drawing.

## The other instrument-free test

A **Nordic Power Profiler (PPK2)** on the MCU rail settles it differently and
needs no RF access either. Advertising at +8 dBm every 30–60 ms produces
unmistakable current spikes — roughly 15–20 mA for about a millisecond, three
times per advertising event, one per channel.

- spikes present → the radio IS transmitting, and the fault is that nothing can
  decode what comes out
- spikes absent  → the radio is not transmitting, whatever the controller says

That distinction is exactly the one nothing in software has been able to make,
and `docs/plan/implementation.md` already assumes a Power Profiler for the
energy work.
