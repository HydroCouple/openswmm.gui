# Critical Review — Mesh Generation Scalability Improvement Plan (2026-08-03)

**Reviewed document:** "Mesh Generation Scalability — Improvement Plan" (handoff note, 2026-08-03)
**Reviewer:** Claude (independent verification against `openswmm.gui` source)
**Status:** AWAITING APPROVAL — implementation plan in §4; do not implement until approved.

---

## 1. Verification of the report's factual claims

Every file/line claim was checked against the current tree. Results:

| Claim | Verdict |
|---|---|
| `gStep` falls back to native pixel size at `meshgenerationdialog.cpp:881` | ✅ Confirmed (exact line) |
| Candidate grid ≈ 1.51 B points for the 55,107 × 61,770 domain at 1.5 spacing | ✅ Math checks out (36,738 × 41,180 ≈ 1.513e9) |
| `maxPoints` spin: range 0–500,000 (line ~2303), default 0 (line 2561), wired at 3168 | ✅ Confirmed |
| `meshMaxArea = 0.0` (unconstrained) default in `preferencesmanager.h` (~473) | ✅ Confirmed |
| `kMaxRetainedPointsDefault = 64 M` (actually 64·2²⁰ = 67.1 M), `kMaxGridBytesDefault = 2 GB`, `kMaxReadBufBytesDefault = 256 MB` | ✅ Confirmed (`dtmthinner.h:176–185`) |
| `MeshGenerator::generate()` has no input-size guard; packs points straight into `triangulateio` | ✅ Confirmed (`meshgenerator.cpp:140–300`) |
| `triangulate_safe()` catches only Triangle's `triexit()` via setjmp, not OS OOM | ✅ Confirmed (`meshgenerator.cpp:416–426`) |
| `MeshStageCache::kFormatVersion = 1`; stage caches keyed on options + file identity | ✅ Confirmed |
| Root-cause chain (unbounded candidates → millions of Steiners → uncapped Triangle → OOM) | ✅ Plausible and consistent with the code paths |

The diagnosis is sound. However, three findings materially change the fix plan:

---

## 2. Critical findings (corrections to the plan)

### C1 — F1 as written will NOT fix the crash ⛔

The proposed snippet raises the **local `gStep` variable** in the dialog. But the
thinner derives its own step independently:

```
dtmthinner.cpp:709:  const double step = (opts.gridSpacing > 0.0) ? opts.gridSpacing : pixelSize();
```

`gStep` in the dialog is only consumed by `gStepMesh` / `effSpacing` / boundary-buffer
math. Raising it leaves `opts.gridSpacing == 0`, so `generatePoints()` still samples
at native 1.5 m → still 1.5 B candidates → still crashes.

**Correct implementation:** apply the floor to a mutable copy of
`in.thinnerOpts` (or write back into it) **before** both the `gStep` computation
(line 881) and the `terrainKey()` computation (line ~954), so the thinner, the
downstream spacing math, and the cache key all see the raised value. The DTM-CRS
bbox needed for the calculation (`dx0..dy1`) is already in scope at that point.

### C2 — F2's "defense in depth" claim is wrong: `maxPoints` is not a cap ⚠️

`dtmthinner.h:99`: *"Checked BETWEEN passes, so the result may undershoot; **it is
not a hard cap**."* And `thinBandInPlace()` terminates when no point exceeds the
normal-dot threshold (`if (toRemove.isEmpty()) break;`) — the quota break
(`nActiveCore <= coreQuota`) never fires on rough terrain. On the urban/riverine
repro surface, thinning converges with counts far above any `maxPoints` value.

F2 is still a reasonable default-UX change (it stops passes early when the target
is reached on smooth terrain), but it provides **no OOM protection**. F3 is the
only real guard. Also note the proposed default (500,000) equals the spin's max —
users could never raise it; the spin max should be increased if a nonzero default
is shipped.

### C3 — Cache-version bump is the wrong invalidation mechanism ⚠️

The report says F1 requires bumping `kFormatVersion` because "the terrain-cache
key includes the effective spacing." It does not — `terrainKey()` hashes the raw
`opts.gridSpacing`. With the C1 write-back, the key changes **automatically and
only for runs where the floor triggers**. Bumping `kFormatVersion` would
needlessly invalidate every user's boundary + terrain + PSLG caches, including
perfectly valid small-domain entries (re-triggering the "~1 hour reload" the
cache was built to avoid). **Recommendation: no version bump; rely on the key
change.**

### C4 — F4 as specified degrades small models ⚠️

`maxArea = domainArea / 1e6` unconditionally: a 100 m × 100 m site would get
0.01 m² cells → ~1 M triangles for a parking lot. The report's "and the domain is
large" qualifier is essential but unspecified. Also note `maxArea` only bounds
*refinement* — it cannot coarsen below input Steiner density, so it contributes
nothing to crash prevention once F1 is in place.

**Recommendation:** gate F4 on the same trigger as F1 (spacing floor actually
raised) and derive it from the raised spacing, e.g. `maxArea ≈ 2·(gStepMesh)²`,
rather than a separate `domainArea/1e6` heuristic. Alternatively defer F4
entirely — it is a result-quality tweak, not part of the crash fix.

### Minor gaps

- **No-thinning path unaffected by F1:** `readPixels()` ignores `gridSpacing` and
  reads native pixels; its internal budget guard fails gracefully with an error.
  Acceptable, but the F1 status message should not imply the floor helps when
  thinning is disabled.
- **Late-failure UX:** the thinner's own ceiling is 67 M, so without F1 a user
  could thin for an hour and then hit F3's 5 M gate. Cheap addition: check the
  candidate count in the dialog pipeline right after thinning returns (~line
  1000) and fail fast with the same guidance.
- **F3 constant sanity:** 5 M points ≈ 10 M triangles ≈ 1.5–2.5 GB Triangle pools
  — the report's memory math is reasonable.

---

## 3. Verdict

| Item | Feasible? | Valid as written? | Disposition |
|---|---|---|---|
| F1 spacing floor | Yes (~25 lines) | **No — wrong variable (C1)** | Implement corrected version; primary fix |
| F2 maxPoints default | Yes (2 lines) | Overstated (C2) | Implement as UX default only; raise spin max |
| F3 Triangle input guard | Yes (~12 lines) | Yes | Implement as specified; the one true hard guard |
| F4 auto maxArea | Yes | **Unsafe for small domains (C4)** | Implement gated on F1 trigger, or defer |
| kFormatVersion bump | Yes | Counterproductive (C3) | Skip; key change suffices |

The report's §2 "do not touch" list (banded thinner, stage cache, PSLG prep) is
accurate and should be honored.

---

## 4. Implementation plan (for approval)

Priority order changed from the report: F3 first (independent, zero-risk guard),
then F1 (the actual fix), then F2, then F4-gated.

### Step 1 — F3: input guard in `MeshGenerator::generate()`
- `src/mesh/meshgenerator.cpp`: after dedupe/packing loops, before
  `triangulateio` pack: reject `points.size() > kMaxTriangleInputPoints = 5'000'000`
  with the report's actionable message.
- → verify: unit test feeding >5 M synthetic points returns `errorMsg`, no crash;
  existing mesh tests pass unchanged.

### Step 2 — F1 (corrected): domain-aware spacing floor on `thinnerOpts.gridSpacing`
- `src/ui/dialogs/meshgenerationdialog.cpp`, just after the DTM-CRS bbox
  (`dx0..dy1`) is computed and **before** line 881 and the `terrainKey()` call:
  compute candidate count from resolved step; if > `kMaxCandidateGridPoints = 8e6`,
  scale spacing up by `sqrt(n/cap)` and apply to the (local copy of)
  `thinnerOpts.gridSpacing` used by the thinner, the cache key, and the
  `gStep`/`gStepMesh` math. Log via `lcMeshPerf` + progress-text notice.
- Fail-fast: after thinning returns, if candidate count still exceeds the F3
  limit, error out in the dialog pipeline before Triangle.
- **No `kFormatVersion` bump** (C3).
- → verify: repro dataset (15 GB BigTIFF + 66,730-hole boundary) completes with
  triangles in 10^5–10^6 range, peak RSS < a few GB; a small-domain run produces
  a byte-identical mesh and still hits the existing terrain cache.

### Step 3 — F2: shipped default for `maxPoints`
- `meshgenerationdialog.cpp:2561`: `setValue(0)` → `setValue(500000)`; raise spin
  max (line 2303) to e.g. 5,000,000 so the default is not the ceiling. Document
  in the tooltip that it is a soft target, not a cap (per `dtmthinner.h`).
- → verify: dialog opens with new default; cache key changes only for runs using
  the new value (expected).

### Step 4 — F4 (gated): auto `maxArea` only when the F1 floor triggered
- When the floor raised spacing AND user `maxArea == 0`: set
  `maxArea = 2 × gStepMesh²` (post-raise) before invoking Triangle. Never applied
  to small/medium domains; never overrides a user-set value or region areas.
- → verify: small-domain meshes unchanged; repro-domain cell count stays bounded.

### Step 5 — Regression tests (`tests/gui/`)
- Synthetic large-extent DTM (small file, huge geotransform) → floor triggers,
  candidate count ≤ cap, spacing message emitted.
- Small DTM → floor does not trigger; output byte-stable vs. baseline.
- `MeshGenerator` >5 M-point guard test.
- Test outputs written under `tests/output/` (reviewable location, per CLAUDE.md §4.1).

### Files touched
- `src/ui/dialogs/meshgenerationdialog.cpp` (Steps 2, 3)
- `src/mesh/meshgenerator.cpp` (Steps 1, 4)
- `tests/gui/` (Step 5)
- NOT touched: `meshstagecache.h` (no version bump), `dtmthinner.cpp` (per §2 of the report)

### Validation checklist (adopted from report §6, amended)
1. Repro dataset completes; `[Mesh] Triangle returned: OK | triangles: N`, N ∈ 10^5–10^6.
2. Peak RSS under a few GB.
3. Small/medium meshes byte-stable; terrain cache still hits (no global invalidation).
4. ~~kFormatVersion bumped~~ — replaced by: cache key changes only when the floor triggers.
5. Cancellation responsive at every stage.
