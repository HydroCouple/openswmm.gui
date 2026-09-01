# D5 — `DynamicWave.cpp:2932` SEMI_IMPLICIT `sumdqdh` Sign Analysis (owner ruling)

**Status:** DoD package for owner review (Task 5, 2026-07-29). Not committed.
**The line:** `double denom = surf_area - 0.5 * dt * xnode_.sumdqdh[ui];` followed by
`denom = std::max(denom, min_surf_area_);` (`DynamicWave.cpp:2932-2933`).
**Why it matters now:** the windowless-coupling conductance scatter (plan §5.4) adds
`G ≥ 0` into `sumdqdh` and is therefore gated to `NODE_CONTINUITY EXPLICIT` until this
sign is ruled — under the current `-` a larger conductance would *destabilize* the
SEMI_IMPLICIT branch instead of damping it.

## 1. The sign convention the producers use

Every `dqdh` producer accumulates a **non-negative** quantity of dimension
d|Q|/dH (flow response magnitude per unit head):

- dry-link parity form `dqdh = g·dt·aMid/length · barrels` (`DynamicWave.cpp:1944`) — positive;
- normal-flow / momentum forms `dqdh = (1/denom)·g·dt·aWtd/length · barrels`
  (`:2267-2269`, `:2359`) — positive (denom > 0);
- the scatter (`SWMMEngine.cpp:2666-2667`) adds link `dqdh` to **both** end nodes
  unconditionally (no sign by flow direction), matching legacy `dynwave.c:565-575`.

Legacy's own use confirms the meaning: the EXPLICIT surcharged branch solves
`dy = dQ / sumdqdh` (`:2999-3001`) — driving net inflow excess `dQ` to zero requires
`sumdqdh = d(net outflow)/dH > 0`. The SEMI_IMPLICIT branch's own comment block agrees:
*"dQ_net/dH = sumdqdh (positive: higher head ⟶ more net outflow)"* (`:2917-2918`).

## 2. The Crank–Nicolson algebra

With `Q(H)` = net **inflow** and `sumdqdh = −dQ/dH ≥ 0` (more head → less net inflow):

```
A·dH        = ½·(Q_old + Q_new)·dt              (trapezoidal)
Q_new       ≈ Q_now − sumdqdh·dH                 (linearization)
A·dH        = ½·dt·(Q_old + Q_now) − ½·dt·sumdqdh·dH
dH·(A + ½·dt·sumdqdh) = dV                       (dV = ½·dt·(Q_old+Q_now), as coded)
```

The denominator is `A + ½·dt·sumdqdh`. **The code subtracts.** With the producers'
positive convention, the current `-`:

- *shrinks* the denominator as flow sensitivity grows — the head update is **amplified**
  exactly where the physics says it should be damped (the opposite of implicitness);
- relies on the `max(denom, min_surf_area_)` clamp to avoid a sign flip — i.e. for any
  meaningfully surcharged node (`½·dt·sumdqdh ≳ A`) the branch silently degenerates to
  `dV/min_surf_area`, a huge explicit step;
- inverts the intended "sumdqdh takes over when A shrinks" transition described in the
  comment at `:2926-2929` (the takeover currently *removes* stability instead of adding it).

## 3. The one-character candidate fix

```cpp
double denom = surf_area + 0.5 * dt * xnode_.sumdqdh[ui];   // was '-'
```

The `max(denom, min_surf_area_)` guard can stay (now purely a dry-node floor). After the
fix, the conductance scatter's EXPLICIT-only gate (`SWMMEngine.cpp:2677-2678`) can drop —
`G` lands in `sumdqdh` with a guaranteed-damping sign in both continuity modes.

## 4. Which tests gate it

- `tests/unit/engine/test_routing.cpp:711-841` — the two SEMI_IMPLICIT fixtures
  (EXTRAN+SEMI_IMPLICIT anderson-acceleration gating; SLOT kink-skip guard). They assert
  *gating flags*, not head values, so they likely stay green — must be confirmed.
- Full suite (110): SEMI_IMPLICIT is **not** the default (`EXPLICIT` is), and legacy
  parity runs never enable it (it is an engine extension) — QA bit-parity is not at risk.
- **New test to land with the fix:** a surcharged two-node fixture stepped under
  SEMI_IMPLICIT asserting (a) monotone head convergence where the `-` form oscillates or
  pins to `min_surf_area_`, and (b) Picard iteration count does not increase vs EXPLICIT.
- Follow-up once ruled: un-gate the coupling conductance and re-run the §8.4
  hover-at-crown damping gate under SEMI_IMPLICIT.

## 5. Recommendation

Rule the sign `+`, land the one-character fix with the new fixture, keep the floor.
Risk is confined to the non-default SEMI_IMPLICIT mode; the change makes that mode do
what its own derivation comment says it does.
