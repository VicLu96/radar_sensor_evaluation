# webinterface

Live frame viewer, configuration and health for the VL53L9CX node, over Web
Bluetooth. Phases 1–5 of [`docs/plan/ble-streaming-and-web-ui.md`](../docs/plan/ble-streaming-and-web-ui.md).

```bash
npm install
npm run dev
```

Then open **http://localhost:3000 in Chrome or Edge** and press **Connect**.

## Three things that will bite, once each

- **Chromium only.** Web Bluetooth is not implemented in Firefox or Safari and
  there is no polyfill. The page says so rather than failing mysteriously.
- **Secure context required.** `localhost` counts, so `npm run dev` is fine as
  it is. A plain-HTTP LAN address is not — if you want to open it from a phone,
  that needs HTTPS.
- **A user gesture is mandatory** for `requestDevice()`. That is why Connect is
  a real button and nothing connects on load.

## What you should see

1. **Connect** opens Chrome's device chooser, filtered to `water-sense*`.
2. The node's configuration, health and energy load immediately — the sensor is
   **idle** and not ranging.
3. **Start streaming** begins captures. The heatmap updates, and the stats bar
   fills in frame rate, throughput, drop rate and the amplitude split.

If the node advertises but the chooser is empty, it is almost always the name
filter: the firmware advertises `CONFIG_BT_DEVICE_NAME`, currently
`water-sense-tof`.

## The layout

```
app/page.tsx           orchestration and all the state
lib/protocol.ts        the wire format — MIRROR of firmware_test/src/ble/app_ble.h
lib/ble.ts             Web Bluetooth: connect, subscribe, read, write
components/
  FrameCanvas.tsx      the heatmap, drawn off the React path
  ConfigPanel.tsx      Tier 1: resolution, exposure, period, planes
  HealthPanel.tsx      the device's own verdict on itself, plus energy
  StatsBar.tsx         fps, kB/s, drop rate, zone validity, amplitude split
```

**`lib/protocol.ts` and `firmware_test/src/ble/app_ble.h` are the same
definitions written twice.** Nothing checks that they agree; the only guard is
`PROTOCOL_VERSION`, which both sides refuse to work across. Change one, change
the other, and bump the version if any struct moved.

## Two design decisions worth knowing

**Frames never enter React state.** At 54×42 a frame is 2,268 zones arriving up
to ~2.5 times a second. The newest frame sits in a ref that the BLE callbacks
overwrite; the canvas draws it on `requestAnimationFrame`, and only summary
numbers — sampled four times a second — reach React. Rendering is decoupled from
arrival, so a slow frame shows up as a number rather than as a stutter.

**There is no retransmission.** At ~2.5 fps a lost frame is cheaper than a
stall, so drops are expected and the *drop rate* is what says whether the link
keeps up. It is counted and displayed rather than papered over — a throughput
claim nobody can check is not a measurement.

## Recording

The **Recording** panel captures live frames to a file, with the **full
configuration in the header - including changes made mid-recording**.

- **CSV** (default) opens in anything and documents itself in a `#` comment
  block: `pd.read_csv(path, comment="#")`
- **Binary `.wstof`** is ~3x smaller (0.68 against 2.05 MB per minute at 54x42
  distance-only) and exact. Use it for long runs.

Distances are millimetres and **-1 means no valid measurement** - not zero
distance. Timestamps are the host's clock at frame completion; the device has no
clock by design.

Format specification and Python decoders for both:
[../docs/plan/recording-format.md](../docs/plan/recording-format.md).

## What is not here yet

Counting mode (phase 7) broadcasts the count in an **advertisement**, and
**Chrome cannot scan advertisements** without
`chrome://flags/#enable-experimental-web-platform-features`. The plan's answer
is a small Node + `noble` bridge relaying counts to this page over a WebSocket —
which is also the gateway a deployed system needs anyway. Nothing here depends
on the source, so that can be added without the UI caring.

Calibration (`CALIBRATE`, `CLEAR_CALIBRATION`) has frozen opcodes and the
firmware answers `-ENOTSUP` today.
