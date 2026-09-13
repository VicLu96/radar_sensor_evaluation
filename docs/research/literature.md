# Literature — people counting on a corner-mounted dToF array

Reference list for the counting algorithm in
[../plan/counting-algorithms.md](../plan/counting-algorithms.md) and the plan in
[../plan/people-counting.md](../plan/people-counting.md).

Started 2026-09-13. **Add to this file rather than scattering links through the
plans.**

### How to read the Status column

Per `CLAUDE.md`, nothing is promoted to fact without saying so.

- **✓ verified** — citation details (authors, venue, year, DOI/link) confirmed by a
  web search on the date shown. Confirms the *reference exists as cited*, not that
  it says what we use it for.
- **reviewer** — cited by one of the 2026-09-12 expert reviews and **not yet
  independently checked**. Verify before citing in the paper.

---

## 1. The architecture we are building on

| Reference | Used for | Status |
|---|---|---|
| **M. Harville**, *Stereo person tracking with adaptive plan-view templates of height and occupancy statistics*, Image and Vision Computing 22(2):127–142, 2004. [ResearchGate](https://www.researchgate.net/publication/2924783_Stereo_Person_Tracking_with_Adaptive_Plan-View_Statistical_Templates) | **The foundation of Algorithm A.** Plan-view occupancy + height maps from an *oblique single depth view* — our geometry. Deproject, then track on the floor plane where size is range-independent. | ✓ 2026-09-13 |
| **M. Harville, D. Li**, *Fast, Integrated Person Tracking and Activity Recognition with Plan-View Templates from a Single Stereo Camera*, CVPR 2004. [ML Anthology](https://mlanthology.org/cvpr/2004/harville2004cvpr-fast/) | The single-camera version; closest to a single sensor node. | ✓ 2026-09-13 |
| **Multiple Human Tracking in RGB-D Data: A Survey**, arXiv:1606.04450. [arXiv](https://arxiv.org/pdf/1606.04450) | One-stop catalogue of failure modes for depth-based people tracking. | ✓ 2026-09-13 |

---

## 2. Background subtraction

| Reference | Used for | Status |
|---|---|---|
| **C. Stauffer, W.E.L. Grimson**, *Adaptive background mixture models for real-time tracking*, CVPR 1999, vol. 2, pp. 246–252. [PDF](http://www.ai.mit.edu/projects/vsam/Publications/stauffer_cvpr98_track.pdf) | The classical adaptive background model (MOG). **Cited to explain why we do NOT use it**: indoors at 940 nm there is no multimodal background to model, and adaptation absorbs a person sitting still. We use a frozen trimmed mean + spread instead. | ✓ 2026-09-13 |
| **O. Barnich, M. Van Droogenbroeck**, *ViBe: A Universal Background Subtraction Algorithm for Video Sequences*, IEEE TIP 2011. [ResearchGate](https://www.researchgate.net/publication/224206851_ViBe_A_Universal_Background_Subtraction_Algorithm_for_Video_Sequences) | Rejected on budget: 20 samples × 2268 zones × 2 B = 91 KB. | reviewer |
| *Modelling depth for nonparametric foreground segmentation using RGBD devices*, arXiv:1609.09240. [arXiv](https://arxiv.org/pdf/1609.09240) | A background model whose contribution is **handling invalid depth pixels** — our dominant problem at grazing incidence. Informs the three-channel foreground (shortening / appeared / shadow). | reviewer |

---

## 3. Connected components

| Reference | Used for | Status |
|---|---|---|
| **L. He, Y. Chao, K. Suzuki, K. Wu**, *Fast connected-component labeling*, Pattern Recognition 42(9):1977–1987, 2009. [doi:10.1016/j.patcog.2008.10.013](https://doi.org/10.1016/j.patcog.2008.10.013) | Two-scan labelling with provisional labels and an equivalence structure. | ✓ 2026-09-13 |
| **L. He, Y. Chao, K. Suzuki**, *A run-based two-scan labeling algorithm*, IEEE TIP 17:749–756, 2008. | **The run-based variant we use** — per-blob accumulators in closed form per run, and a provable 1,134-run bound on a 54×42 grid. | ✓ 2026-09-13 (referenced in the 2009 paper) |

---

## 4. Thresholding and the split test

| Reference | Used for | Status |
|---|---|---|
| **N. Otsu**, *A threshold selection method from gray-level histograms*, IEEE Trans. Systems, Man, and Cybernetics 9(1):62–66, 1979. [doi:10.1109/TSMC.1979.4310076](https://doi.org/10.1109/TSMC.1979.4310076) · [PDF](https://engineering.purdue.edu/kak/computervision/ECE661.08/OTSU_paper.pdf) | Between-class variance criterion. **Known bias toward the larger class** — which matters here, since the near person has ~7× the zones of a far one. | ✓ 2026-09-13 |
| **J. Kittler, J. Illingworth**, *Minimum error thresholding*, Pattern Recognition 19(1):41–47, 1986. [doi:10.1016/0031-3203(86)90030-0](https://doi.org/10.1016/0031-3203(86)90030-0) | **The criterion to use for the split.** Its prior-entropy term corrects the class-size imbalance that biases Otsu. | ✓ 2026-09-13 |
| **J.A. Hartigan, P.M. Hartigan**, *The Dip Test of Unimodality*, Annals of Statistics 13(1):70–84, 1985. [Project Euclid](https://projecteuclid.org/journals/annals-of-statistics/volume-13/issue-1/The-Dip-Test-of-Unimodality/10.1214/aos/1176346577.full) | A principled test of *whether* a blob's distribution is bimodal at all — which Otsu and KI cannot answer, since both always return a threshold. Candidate for the paper's statistical guard; the reviewer judged it feasible but not a replacement for the size guards. | ✓ 2026-09-13 |
| **P.M. Hartigan**, *Computation of the Dip Statistic to Test for Unimodality*, J. Royal Statistical Society C 34(3):320–325, 1985. [Oxford Academic](https://academic.oup.com/jrsssc/article/34/3/320/6985177) | The algorithm for computing it. Implementations: R `diptest`, Python `diptest`. | ✓ 2026-09-13 |

---

## 5. Mathematical morphology — modes, splitting, counting touching objects

| Reference | Used for | Status |
|---|---|---|
| **L. Vincent, P. Soille**, *Watersheds in digital spaces: an efficient algorithm based on immersion simulations*, IEEE PAMI 13(6):583–598, **1991**. [doi:10.1109/34.87344](https://doi.org/10.1109/34.87344) | Watershed. **Rejected in the zone raster** (the height parameter means nothing on a 45° slant surface) but **appropriate in metric plan view**. Note: 1991, not 1993 as sometimes cited. | ✓ 2026-09-13 |
| **L. Vincent**, *Morphological grayscale reconstruction in image analysis: applications and efficient algorithms*, IEEE TIP 2(2):176–201, 1993. [Semantic Scholar](https://www.semanticscholar.org/paper/Morphological-grayscale-reconstruction-in-image-and-Vincent/8ae9fc1e08c790f737d52c4ab6e20234aa269faa) | **Morphological reconstruction, the basis of the h-maxima transform** — the replacement for blur + non-maximum suppression in Algorithm A's mode finder. One parameter with a physical meaning. | ✓ 2026-09-13 |
| **P. Soille**, *Morphological Image Analysis: Principles and Applications*, Springer, 2nd ed. 2003. | Textbook reference for distance transforms, h-maxima, and the h-minima-before-watershed fix for over-segmentation. | reviewer |
| **P. Maragos**, *Pattern spectrum and multiscale shape representation*, IEEE PAMI 11(7):701–715, 1989. | **Granulometry / pattern spectrum** — the classical answer to *"count touching objects of known size"*. Openings of increasing size on the plan-view footprint; the reviewer rated it strictly better than dividing mass by a per-person constant. | ✓ 2026-09-13 |

---

## 6. Counting without segmentation — the control estimator

| Reference | Used for | Status |
|---|---|---|
| **A.C. Davies, J.H. Yin, S.A. Velastin**, *Crowd monitoring using image processing*, Electronics & Communication Engineering Journal 7(1):37–47, February 1995. [IET](https://digital-library.theiet.org/content/journals/10.1049/ecej_19950106) · [Kingston repository](https://eprints.kingston.ac.uk/id/eprint/8091/) | **Area regression.** Fitted a linear model *per scene* from **both** area and edge-pixel count, with a perspective correction — and reported the linearity breaking down under occlusion, which is our 3–5-people-in-10 m² regime. The basis of the Phase 3 control. | ✓ 2026-09-13 |
| **A.B. Chan, Z.-S.J. Liang, N. Vasconcelos**, *Privacy preserving crowd monitoring: Counting people without people models or tracking*, CVPR 2008. [PDF](http://www.svcl.ucsd.edu/publications/conference/2008/cvpr08/cvpr08_peoplecnt.pdf) | Holistic features → count, **no segmentation or tracking**. **Cite for the insight, not the method**: their regression is a Gaussian process (learned), which this project excludes. The point we take is that *a single feature saturates*, so the deterministic control uses a two-feature closed-form least-squares fit, `n = round(a·mass + b·perimeter + c)`. | ✓ 2026-09-13 |

---

## 7. Motion and track memory

| Reference | Used for | Status |
|---|---|---|
| **A.F. Bobick, J.W. Davis**, *The recognition of human movement using temporal templates*, IEEE PAMI 23(3):257–267, 2001. [PDF](https://www.cs.bu.edu/fac/betke/cs591/papers/bobick-davis.pdf) | **Motion History Image.** A decaying per-cell recency-of-motion map, ~3.6 KB in plan view. Separates two people who merged momentarily but arrived from different directions — pixel-level track-cardinality memory with no association step. | ✓ 2026-09-13 |

---

## 8. Geometry and calibration

| Reference | Used for | Status |
|---|---|---|
| **M.A. Fischler, R.C. Bolles**, *Random sample consensus: a paradigm for model fitting with applications to image analysis and automated cartography*, Communications of the ACM 24(6):381–395, 1981. [doi:10.1145/358669.358692](https://doi.org/10.1145/358669.358692) | **RANSAC floor fit** from the empty-room calibration frames → mount height and two tilt angles, making installation a zero-effort step. Run with a fixed seed so device and PC replay agree. | ✓ 2026-09-13 |
| *Uncertainty in multispectral lidar signals caused by incidence angle effects*, Interface Focus 8(2), 2018. [PMC](https://pmc.ncbi.nlm.nih.gov/articles/PMC5829180/) | Physics of grazing incidence: signal ∝ cos θ, and footprint stretching causes **range walk** (bias, not just noise). Why a single global foreground threshold fails on a corner mount. | reviewer |

---

## 9. The nearest prior art — low-resolution sensors

The geometry and data density closest to ours. Mostly overhead or doorway-shaped;
**none is a corner mount at 2268 zones**, which is the gap this work sits in.

| Reference | Relevance | Status |
|---|---|---|
| **ST UM2600**, *Counting people with the VL53L1X long-distance ranging Time-of-Flight sensor*. [PDF](https://www.st.com/resource/en/user_manual/um2600-counting-people-with-the-vl53l1x-longdistance-ranging-timeofflight-sensor-stmicroelectronics.pdf) | The industry baseline: two ROIs, line crossing. **A turnstile, not an occupancy sensor** — no capability for stationary people. | reviewer |
| **ST**, *VL53L5CX* and *VL53L8CX* product pages. [L5CX](https://www.st.com/en/imaging-and-photonics-solutions/vl53l5cx.html) · [L8CX](https://www.st.com/en/imaging-and-photonics-solutions/vl53l8cx.html) | ST market their own **8×8 (64-zone)** parts for presence and multi-target detection — evidence for the paper's "counting saturates well below 2268 zones" hypothesis. | ✓ 2026-09-12 |
| **Multi-Bernoulli Tracking for Occupancy Monitoring Using Low-Resolution Infrared Sensor Array**, Remote Sensing 13(16):3127, 2021. [doi:10.3390/rs13163127](https://doi.org/10.3390/rs13163127) | The most directly transferable *tracking* paper: random-finite-set occupancy tracking on an 8×8 array, handling detection multiplicity natively. | reviewer |
| **Shetty et al.**, *Detection and tracking of a human using the infrared thermopile array sensor Grid-EYE*, 2017. [ResearchGate](https://www.researchgate.net/publication/324725660_Detection_and_tracking_of_a_human_using_the_infrared_thermopile_array_sensor_-_Grid-EYE) | Essentially our pipeline on 8×8 thermal. **Cite as prior art for the failure** — it breaks when group members are too close to resolve as separate blobs. | reviewer |
| *HW-SW Optimization of DNNs for Privacy-preserving People Counting on Low-resolution Infrared Arrays*, arXiv:2402.01226. [arXiv](https://arxiv.org/pdf/2402.01226) | A learned approach — **not** our method, but it publishes RAM, code size and energy per inference: a direct numerical comparable for the efficiency claims. | reviewer |
| *A High-Computational Efficiency Human Detection and Flow Estimation Method Based on TOF Measurements*, Sensors 2019. [PMC](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC6387275/) | ToF-array human detection with an efficiency focus. | reviewer |
| *TinyML-Based Real-Time Doorway Activity Recognition with a Time-of-Flight Sensor*, Electronics 14(17):3533, 2025. [doi:10.3390/electronics14173533](https://doi.org/10.3390/electronics14173533) | Closest recent work on our sensor family. Learned, doorway-shaped. | reviewer |
| *Benchmark data and method for real-time people counting in cluttered scenes using depth sensors*, arXiv:1804.04339. [arXiv](https://arxiv.org/pdf/1804.04339) | Overhead depth counting, 90–98% — the accuracy bar, and explicitly **not** our operating regime. | ✓ 2026-09-12 |

---

## 10. Overhead head-detection — cited to explain why it does not apply

At a 41–50° corner tilt a head is **not** the closest point on a person, so this
entire family is inapplicable. Worth stating explicitly in the paper; it is the
clean justification for a different method.

| Reference | Status |
|---|---|
| *Water Filling: Unsupervised People Counting via Vertical Kinect Sensor*. [ResearchGate](https://www.researchgate.net/publication/261338441_Water_Filling_Unsupervised_People_Counting_via_Vertical_Kinect_Sensor) | reviewer |
| *Field seeding algorithm for people counting using KINECT depth image*. [Academia](https://www.academia.edu/28109298/Field_seeding_algorithm_for_people_counting_using_KINECT_depth_image) | reviewer |

---

## 11. Sparse point-cloud tracking — the closest match to our data density

A far person is ~30–100 zones; mmWave radar trackers work in exactly that regime.

| Reference | Relevance | Status |
|---|---|---|
| *Real-time People Tracking and Identification from Sparse mm-Wave Radar Point-clouds*, arXiv:2105.11368. [arXiv](https://arxiv.org/pdf/2105.11368) | DBSCAN → Kalman → Hungarian on ~50–200 points per frame. | reviewer |
| **TI GTRACK** group tracker, mmWave SDK. [MathWorks walkthrough](https://www.mathworks.com/help/fusion/ug/people-tracking-using-ti-mmwave-radar.html) | Gating + per-group EKF + explicit allocate/merge/split logic, on a Cortex-R4F. Architecturally the thing to copy for track management. | reviewer |

---

## 12. Evaluation methodology for the paper

| Reference | Used for | Status |
|---|---|---|
| **T. Gebru et al.**, *Datasheets for Datasets*. | The recognised convention for documenting a released dataset — collection, participants, consent, limitations. | reviewer |
| **C. Wohlin et al.**, *Experimentation in Software Engineering* — internal / external / construct / conclusion validity. | Structure for the threats-to-validity section. | reviewer |
| Two one-sided tests (TOST) for equivalence / non-inferiority. | Declaring the saturation point against a **pre-registered margin** rather than eyeballing a knee. | reviewer |

---

## Still to find

- ~~**The VL53L9CX optical model.**~~ **Closed 2026-09-13:** equal-angle. ST: 54°×42° at
  1° angular resolution, so each zone subtends 1°. Floor fit confirms empirically.
- **VL53L9CX power during the blob upload, and in standby.** Sets the
  power-down versus standby crossover. Not in UM3683 as far as we have found.
- **Kittler–Illingworth on unequal priors** — a reference establishing the
  class-size bias of Otsu formally, rather than relying on the review.
- **Plan-view principal-axis splitting** — the reviewer's best idea. Find whether
  it is published under another name before claiming it.
