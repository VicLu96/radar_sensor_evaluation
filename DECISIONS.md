# Decision Log (append-only)

Newest entries at the bottom. Never edit or delete a past entry — if a decision is
reversed, append a new one referencing the old. An edited past entry is a bug.

```
## YYYY-MM-DD — [Decision]
What:
Why:
Expected to be wrong if:
```

---

## 2026-08-31 — `main` created as an orphan branch; stocks CLI left alone
What: The repository's only branch was `claude/stocks-2vn27c`, containing a
stock-price CLI unrelated to radar or ToF work — the output of a one-word "stocks"
prompt aimed at this repo while it was empty. Radar work now lives on a new orphan
`main` with no shared history; the stocks branch is untouched.
Why: The two share nothing. An orphan branch keeps the histories clean and leaves the
stocks work recoverable rather than deleted.
Note: `origin/HEAD` currently points at `claude/stocks-2vn27c`, so the stocks branch is
still GitHub's default. Worth repointing to `main` once pushed. The stocks README's
claim that deleting the branch "removes it entirely" is stale — that branch *is* the
default, and deleting it would empty the repo.
Expected to be wrong if: Victor wants the stocks tool kept and developed, in which case
it belongs in its own repository rather than on a branch here.

## 2026-08-31 — I²C, not I3C
What: The host interface to the VL53L9CX is I²C. The part supports both on shared
SDA/SCL, with I3C reaching 12.5 MHz after dynamic address assignment.
Why: Victor's decision. Far simpler bring-up, and whether the nRF54L15 has an I3C
peripheral at all is unconfirmed.
Consequence, stated so it is not rediscovered later: a full 54×42 frame is roughly 9 KB,
so transfer costs ~225 ms at 400 kHz or ~90 ms at 1 MHz. **The bus, not the sensor, caps
frame rate at roughly 4–10 fps** and is a significant share of per-frame energy. The
target application wants ~1 Hz, so this is an acceptable trade — but it forecloses any
future high-frame-rate work and belongs in the paper as a stated limitation.
Expected to be wrong if: reduced-resolution modes turn out unavailable *and* the
application needs more than a few fps — then I3C becomes necessary and the MCU choice
needs re-examining.

## 2026-08-31 — The paper is an energy characterisation, not a people counter
What: The contribution is the energy-accuracy trade-off of high-resolution dToF sensing
— energy per frame broken down by phase, accuracy versus zone count, and a measured
battery operating point. Counting people is the vehicle, not the claim.
Why: People counting is solved and commercially deployed; a paper reporting good
counting accuracy is a datasheet with citations. What nobody can look up is what 2268
zones cost in energy and how much of that resolution counting actually needs.
Expected to be wrong if: the VL53L9CX exposes only one resolution mode, which collapses
the central curve to a point. Fall back to frame rate and duty cycle as the swept axis.
Also wrong if ST or a competitor publishes the same characterisation first — the part
is months old, so this is a real deadline rather than a hypothetical.

## 2026-08-31 — Custom PCB only: no DK, no ST evaluation board
What: Development targets a single custom PCB carrying the ISP2454-LL and the
VL53L9CX. Victor writes the Zephyr board files and has a Power Profiler Kit II. There
is no nRF54L15-DK and no STEVAL-VL53L9 / X-NUCLEO-53L9A1.
Why: Victor built the hardware; buying reference boards to duplicate it is avoidable
cost, and the firmware abstraction is genuinely unaffected — a board file is a board
file.
Consequence, which is NOT neutral and is the reason this is recorded: the plan had used
"DK first, always" as its main bring-up risk control. Without a known-good reference, a
silent sensor has four suspects at once — assembly, board design, our driver port, and
the init sequence — and no cheap way to separate them. The mitigation is replaced
rather than dropped: staged bring-up gates with the I2C address ACK (gate 1.2) as the
hardware/software divider, a logic analyser promoted to primary diagnostic instrument,
and the community VL53L9-Arduino port read as a reference init sequence.
Also promoted to Phase 0: whether the board can measure the sensor and MCU rails
separately. The paper claims a per-component energy breakdown, so a shared rail forces
either a shunt or a weaker differential measurement, and that is a schematic question
best answered before anything is built.
Expected to be wrong if: bring-up stalls at a gate for more than about a week with no
way to tell hardware from software apart. At that point one ST evaluation board becomes
much cheaper than the time being spent, and buying it is the right call rather than a
concession.

## 2026-08-31 — Four stages, power optimisation last
What: Driver -> BLE telemetry to a web interface -> people counting -> power
optimisation. The board can gate each power domain separately during idle.
Why: Victor's sequencing, and it is right. Optimising power before the algorithm exists
means optimising against a guessed frame rate, resolution and wake pattern, and any
architecture built on that guess later constrains the algorithm. Doing it last also
means the paper's measurements are taken against a working system rather than a stub.
One exception carved out: the firmware blob reload time must be measured during stage 1,
because it decides the entire stage-4 architecture and is nearly free to capture while
the driver is being brought up.
Expected to be wrong if: the sensor turns out to have no usable standby state, in which
case power architecture stops being a late optimisation and becomes a stage-1
constraint - every wake would pay a multi-second blob reload and the duty cycle would
have to be designed around it from the start.

## 2026-08-31 — ST driver used unmodified; we write only the platform layer
What: ST's ULD source stays byte-identical to their release. We implement the six
platform functions it calls (RdByte, WrByte, RdMulti, WrMulti, WaitMs, SwapBuffer) on
Zephyr I2C, wrap it in an out-of-tree module, and expose a small custom API.
Why: The ULD carries the init sequence, blob upload protocol, calibration and frame
unpacking, much of it undocumented outside the code and some of it timing-sensitive.
Rewriting means owning all of it with no reference when a frame comes back subtly
wrong. The port is a few hundred lines; a rewrite is the project.
Also decided: the public API is NOT Zephyr's sensor API. That API is built around scalar
channels fetched one at a time, and this device produces a 2268-zone frame - forcing it
through sensor_channel_get would mean either 2268 calls or a channel that lies about
what it returns.
Expected to be wrong if: ST's L9 driver turns out to differ enough from the
VL53L5CX/L8CX convention that the platform layer needs rewriting anyway, or if it is
distributed under a licence that forbids the integration shape we want.
