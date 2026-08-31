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
