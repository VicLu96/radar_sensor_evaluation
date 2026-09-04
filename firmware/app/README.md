# Bring-up application

Walks the staged gates from `../drivers/vl53l9cx/README.md` in order and prints enough
at each one to tell success from the specific way it failed. That matters more than
usual here: this board has no known-good reference, so "nothing happened" is the
expensive outcome.

## Build

```bash
west build -b vl53l9_node/nrf54l15/cpuapp firmware/app
```

The app's `CMakeLists.txt` points `BOARD_ROOT` at `firmware/` and adds the driver as an
out-of-tree module, so no environment setup is needed beyond a west workspace.

**Nothing here has ever been built.** It was written without an SDK installed. Expect
the first build to fail on peripheral instance names — see the header of
`../boards/pbl/vl53l9_node/vl53l9_node_nrf54l15_cpuapp.dts`.

## What it does, and what each stage tells you

| Gate | Prints | If it fails |
|---|---|---|
| 0 | VDDA, VDDIO, AP_CLK, address, from devicetree | Nothing fails here — this exists so a wrong placeholder is visible **before** anything confusing happens |
| 1 | device ready | `vl53l9_init()` did not complete. Scope AP_CLK, then the rail, then XSHUT, then the address |
| 2 | blob upload duration, ms | — |
| 3 | one 12×10 frame, valid-zone count, distance min/mean/max | Small transfer (880 B). If this works and gate 4 does not, the problem is transfer size |
| 4 | one 54×42 frame | Full transfer, 14,842 B, ~404 ms at 400 kHz |
| 5 | power cycle, then blob reload duration | The stage-4 architecture in miniature. Put the Power Profiler on the sensor rail here |
| 6 | one frame every 10 s, sensor off in between | The dwell loop this node actually runs |

## Two numbers worth watching from the first run

**Blob upload duration** (gates 2 and 5). It sets the cost of `TURN_OFF` and therefore
decides whether powering the sensor down between readings beats standby. Expect roughly
250 ms at 400 kHz for 9,865 bytes. If it is far longer, look at `blob-chunk-size` and at
whether `vl53l9_wait_ms` is being rounded up to a full tick.

**Device frame counter versus driver sequence.** Both are printed. If the device counter
stops advancing while ours does, the driver is re-reading a stale buffer; if it jumps
ahead, the sensor produced frames nobody collected. Neither is visible any other way,
and both are easy to mistake for a sensor fault.

## Before the battery measurements

Turn the console off. `CONFIG_SERIAL` and the log backend are on for bring-up, and a
UART held enabled is not free in a design whose contribution is an energy number.
