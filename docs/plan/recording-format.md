# Recording format

The web interface records live frames to a file. Two formats, both carrying the
**full configuration including changes made during the recording**.

Written 2026-09-12.

## Which format, and why both

| | 54×42, distance only | 54×42, all three planes |
|---|---|---|
| **binary `.wstof`** | 4,560 B/frame — **0.68 MB/min** | 13,632 B/frame — **2.04 MB/min** |
| **CSV** | 13,668 B/frame — 2.05 MB/min | 40,884 B/frame — 6.13 MB/min |

*(at 2.5 fps, the bus-limited rate at 400 kHz)*

**CSV is the default.** It opens in pandas, MATLAB, Excel and a text editor; it
carries its own documentation in the header; and for the 30-second-to-2-minute
recordings most of this work needs, 2–12 MB is nothing.

**Binary is ~3× smaller and exact.** Use it for long runs — the duty-cycle
sweeps, and anything over a few minutes at full resolution. A ten-minute
three-plane recording is 20 MB binary against 61 MB CSV.

### What was rejected

- **One row per zone per frame ("long" CSV).** Tidy in principle; 2268 × 150 =
  340,000 rows a minute in practice. Unusable.
- **JSON / NDJSON.** A 2268-element array of integers is ~10 KB per frame as
  text — worse than CSV and harder to stream.
- **PNG or video.** Lossy, or quantised to 8 bits. Distances are 15-bit
  millimetres and amplitudes are diagnostic; both matter.
- **`.npy`.** Efficient and Python-native, but carries no metadata — and the
  configuration is the half of this that is easy to lose.

## What is in the header, and why

Both formats carry the same header: JSON in the binary, `#`-prefixed JSON in the
CSV.

The part that matters most is **`configs`** — an array of every configuration the
node reported while recording, each with the `fromFrameIndex` it took effect at.

> A file that says "2268 zones" but not at what exposure, in which ranging
> context, or that someone switched from **Room detection** to **Close · detail**
> ninety seconds in, is not a measurement — it is a picture. That is exactly the
> distinction this project cannot afford to lose, since every number in the paper
> has to carry its conditions.

Also worth knowing:

- **`invalidDistance: -1`.** A zone with no valid measurement is **−1**, not 0.
  Zero would read as "target at zero distance", and the two are opposite.
- **`timeBase`.** Every timestamp is the **host's** clock at frame completion.
  The device deliberately has no clock (decided 2026-09-10), so inter-frame
  timing includes BLE transport jitter. `capture_ms` is the device's own measure
  of how long the capture took, and is the honest one for energy.
- **`zoneOrderVerified: false`.** Zone order is assumed row-major, origin
  top-left. **This is still an unverified `VERIFY`** — the hardware-validated
  community driver flips 180° by default. Point the sensor at something
  asymmetric and check before trusting any spatial conclusion.

## CSV layout

```
# water-sense ToF recording
# Decode in pandas with:  pd.read_csv(path, comment="#")
# ... the JSON header, one "# " per line ...
seq,device_frame,t_host_ms,capture_ms,temperature_raw,valid_count,d0,d1,...,a0,...,b0,...
```

One row per frame. `d<i>` is distance in mm (−1 invalid), `a<i>` amplitude,
`b<i>` ambient — the last two present only if those planes were streamed.
`i = row * cols + col`.

```python
import json, re
import numpy as np, pandas as pd

def load_csv(path):
    meta = json.loads("".join(
        l[2:] for l in open(path, encoding="utf-8")
        if l.startswith("# ") and not re.match(r"# [A-Za-z]", l)))
    df = pd.read_csv(path, comment="#")
    n = meta["zones"]
    dist = df[[f"d{i}" for i in range(n)]].to_numpy(np.int16)
    dist = dist.reshape(-1, meta["rows"], meta["cols"])
    return meta, df, np.ma.masked_equal(dist, -1)   # masked where invalid

meta, df, dist = load_csv("tof-20260912-183000-close-detail.csv")
print(meta["configs"])     # every configuration, with the frame it started at
print(dist.shape)          # (frames, rows, cols), invalid zones masked
```

## Binary `.wstof` layout

Little-endian throughout.

```
offset  size  field
0       4     magic "WSTF"
4       4     u32 format version (currently 1)
8       4     u32 header length in bytes
12      H     header, UTF-8 JSON (the same object as the CSV comment block)
12+H    ...   frames, each exactly frameBytes long
```

Each frame:

```
0    u32    seq                driver frame counter
4    u32    device_frame       the DEVICE's own counter; a gap here against seq
                               means the sensor missed one, not the link
8    f64    t_host_ms          host clock, ms since Unix epoch
16   u16    capture_ms
18   u16    temperature_raw    scaling is VERIFY
20   u16    valid_count
22   u16    padding            keeps the arrays 4-byte aligned
24   i16 x zones   distance, mm, -1 = no valid measurement
     u16 x zones   amplitude   (only if planes.amplitude)
     u16 x zones   ambient     (only if planes.ambient)
```

`frameBytes = 24 + zones*2*(1 + amplitude + ambient)`, derivable from the header
— so the whole body is one `numpy` view with no loop:

```python
import json, numpy as np

def load_wstof(path):
    buf = open(path, "rb").read()
    assert buf[:4] == b"WSTF", "not a wstof file"
    hlen = int.from_bytes(buf[8:12], "little")
    meta = json.loads(buf[12:12 + hlen])

    n = meta["zones"]
    dt = np.dtype([
        ("seq", "<u4"), ("device_frame", "<u4"), ("t_host_ms", "<f8"),
        ("capture_ms", "<u2"), ("temperature_raw", "<u2"),
        ("valid_count", "<u2"), ("_pad", "<u2"),
        ("distance", "<i2", n),
        *([("amplitude", "<u2", n)] if meta["planes"]["amplitude"] else []),
        *([("ambient", "<u2", n)] if meta["planes"]["ambient"] else []),
    ])

    rec = np.frombuffer(buf, dtype=dt, offset=12 + hlen)
    dist = rec["distance"].reshape(-1, meta["rows"], meta["cols"])
    return meta, rec, np.ma.masked_equal(dist, -1)

meta, rec, dist = load_wstof("tof-20260912-183000-close-detail.wstof")
print(meta["configs"])
print(dist.shape, rec["capture_ms"].mean(), "ms mean capture")
```

## Limits

- **4000 frames**, about 25 minutes at 2.5 fps, ~55 MB in memory at full
  resolution with three planes. The cap exists so a recording left running fails
  visibly rather than by killing the tab. `truncated: true` in the header says it
  was hit.
- Frames are held in memory until you save. Closing the tab loses them.
- **Dropped frames are absent, not interpolated.** Compare `seq` against
  `device_frame` to tell "the link dropped it" from "the sensor never made it".

## What this is for

Two things, and they pull in the same direction.

**Re-scoring detection offline.** `ble-streaming-and-web-ui.md` §8.4 chose an
observer log over frame logging and stated the cost plainly: an observer log
cannot be re-scored, so every change to `fg_threshold_mm`, the blob-size model or
the temporal window means repeating the experiment with people in the room.
Recorded frames remove that — the ~10 detection tunables can be fitted against a
fixed dataset instead of a fresh session each time.

**Range characterisation.** The measurement session in
[measurement-range.md](measurement-range.md) produces exactly this data, and
recordings make it re-analysable rather than a number copied into a notebook.

Neither changes the privacy architecture: **this is the dev-stream profile, and
the deployed build has no frame service to record from.**
