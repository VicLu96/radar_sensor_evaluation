# People detection and counting on multizone dToF — literature and method survey

Researched 2026-08-31.

## The framing that decides everything: mounting geometry

Almost all published accuracy differences come from **where the sensor is**, not from
which algorithm follows. Pick this before writing any detection code.

| Geometry | What it gives | What it costs |
|---|---|---|
| **Overhead, looking down** (ceiling / doorway lintel) | People appear as isolated blobs of *reduced* depth against a fixed floor. Occlusion is nearly eliminated; separating two adjacent people is tractable. **This is what the published systems use.** | Needs ceiling access; coverage per sensor is limited by FoV × height |
| **Doorway, horizontal across the opening** | Cheapest counting: a line-crossing problem with direction from the crossing order | Counts transitions, never occupancy; fails on two people abreast |
| **Corner / wall, looking out** | Widest area per sensor | Occlusion — people hide behind people. Much harder, and the reason wall-mounted counters underperform |

**Recommendation: overhead.** With a 54° × 42° FoV at 2.8 m ceiling height the footprint
is roughly 2.9 m × 2.2 m — a doorway or a corridor slice, not a whole room. Whole-room
occupancy from one unit is not on the table; **counting people across a boundary is**.
That reframing should be made explicitly and early, because it decides the application.

## The classical pipeline — and why it is the right starting point

Published overhead-ToF counters converge on roughly this, and it runs comfortably in
256 KB of RAM without a neural network:

1. **Background model.** Learn the static depth of the floor per zone (running median
   or slow exponential average). Robust to gradual drift; needs care around furniture
   moves.
2. **Foreground segmentation.** A zone is "occupied" where measured depth is
   meaningfully *closer* than background, gated on the per-zone confidence output.
3. **Clustering.** Connected components or mean-shift over the occupied zones. One
   cluster ≈ one person. **At 54 × 42 this is where the VL53L9CX earns its keep** —
   an 8 × 8 sensor merges two adjacent people into one blob; a 54 × 42 grid resolves
   head and shoulders and can separate them.
4. **Tracking.** Nearest-neighbour or Kalman association across frames, with a track
   lifetime to survive dropouts.
5. **Counting.** Either occupancy (tracks currently present) or a line-crossing tally
   with direction (tracks whose centroid crosses a virtual line).

Published results in this shape: a ceiling-mounted VL53L5 system using point clustering
plus a random-forest position estimator reports error around **0.4%**. A 2025 ETRI
Journal system uses an overhead ToF camera with **mean-shift clustering** and is
explicitly **labeling-free** — no training data required, which for a project without a
labelled dataset is a decisive practical advantage.

**Start classical.** It is interpretable, cheap, needs no dataset, and gives the
baseline any learned method must beat. A paper whose learned model beats *nothing* is
not a paper.

## Where learning is genuinely worth it

The active research line is **low-resolution IR arrays** (8×8 and similar) where
classical methods struggle and small CNNs help; there is published work on HW/SW
co-optimisation of DNNs for exactly this, on-device.

The honest observation: **that literature exists because the sensors are coarse.** At
2268 zones the segmentation problem is far easier, so the marginal value of a network
is smaller. Learning is likely worth it for:

- **Discriminating people from non-people** (a rolling chair, a box, a dog) where
  size/shape priors are weak
- **Crowded scenes** where clusters merge
- Nothing else, initially

Keep it as phase 3, gated on the classical baseline actually failing at something
measurable.

## Privacy — a real technical property, not marketing

Depth data at this resolution does not carry identity: no texture, no face, no colour.
This is the standard justification in the literature for ToF over cameras in occupancy
sensing, and it is defensible rather than hand-waved — but only if the device never
transmits raw frames. **If counts leave over BLE and frames never do, the privacy claim
is architectural.** Design it that way and the claim is free.

This has a real design consequence: process on-device, transmit counts. Which is also
what the energy budget wants, since a BLE frame dump would dominate radio time.

## Failure modes the literature keeps hitting

Worth testing explicitly rather than discovering in review:

- **Two people entering abreast** — the classic counting failure. High resolution helps;
  test it deliberately.
- **Loitering in the FoV** — a stationary person merges into a background model that
  keeps updating. Freeze background adaptation where a track is present.
- **Carried objects and pushed trolleys** — appear as clusters.
- **Dark hair and clothing** absorbing 940 nm — dropouts in the middle of a person.
  Use the confidence and reflectance planes rather than pretending it does not happen.
- **Sunlight** through a doorway raising the ambient IR floor.
- **Height variation** — children versus adults against a depth threshold.

## Sources

- [Privacy-preserving labeling-free occupancy counting sensor based on ToF camera and clustering (ETRI Journal, 2025)](https://onlinelibrary.wiley.com/doi/full/10.4218/etrij.2025-0022)
- [HW-SW optimization of DNNs for privacy-preserving people counting on low-resolution IR arrays (arXiv 2402.01226)](https://arxiv.org/pdf/2402.01226)
- [Efficient deep learning models for privacy-preserving people counting on low-resolution IR arrays (arXiv 2304.06059)](https://arxiv.org/html/2304.06059v2)
- [Optimizing occupancy sensor placement in smart environments (arXiv 2602.21098)](https://arxiv.org/pdf/2602.21098)
- [People occupancy detection and profiling with 3D depth sensors for building energy management (ScienceDirect)](https://www.sciencedirect.com/science/article/abs/pii/S0378778815000614)
- [ST UM2600 — Counting people with the VL53L1X](https://www.st.com/resource/en/user_manual/um2600-counting-people-with-the-vl53l1x-longdistance-ranging-timeofflight-sensor-stmicroelectronics.pdf)
