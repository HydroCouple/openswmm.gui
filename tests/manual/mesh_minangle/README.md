# Why forcing a minimum angle fails / hangs — measured 2026-08-20

Reported: *"The triangulation code to force a minimum angle yields an error
dialog showing degeneracies sometimes. I cannot do very large areas."*

All numbers below are from the probes in this folder, run against the vendored
`vendor/triangle/triangle.c` exactly as the GUI builds it.

## Headline

**The error message is a misdiagnosis, and the real failure is unbounded
refinement driven by the smallest feature separation in the PSLG — not by the
area, and not by degenerate geometry.**

## 1. The dialog blames the wrong thing

`MeshGenerator::generate` (`src/mesh/meshgenerator.cpp:556`) reports a single
string for *every* Triangle failure:

> Triangle fatal error — check PSLG for degenerate geometry (duplicate/coincident
> vertices, crossing or zero-length constraint segments, boundary not forming a
> closed ring).

But `triangulate_safe` (`vendor/triangle/triangle.c:1491`) returns `-1` on *any*
`triexit()`, and Triangle has ~30 distinct fatal exits. The ones that actually
matter here are:

| Triangle's real message | What it means |
|---|---|
| `Error: Ran out of precision at (x, y).` + *"Try increasing the area criterion and/or reducing the minimum allowable angle"* | `-q` refinement split a segment below double resolution |
| `Error: Out of memory.` | the refinement exploded |

Triangle prints those to **stdout**, and the GUI never captures stdout
(`genOpts.quiet = true` is hardcoded at
`src/ui/dialogs/meshgenerationdialog.cpp:3947`; no `freopen`/`dup2` anywhere).
So Triangle names the exact remedy and the GUI throws it away and substitutes a
message about degeneracy. That is why the dialog "shows degeneracies" for
models whose geometry is fine.

## 2. A lone small input angle is NOT the cause

`minangle_probe.c` — a 1°-apex wedge, min angle 26°:

| case | result |
|---|---|
| 1° wedge @ origin, q26 | ok, 11 triangles |
| 1° wedge @ UTM (500000, 5400000), q26 | ok, 11 triangles |
| 60° wedge, q26 | ok, ~189 triangles |

Triangle deliberately gives up on input angles it cannot improve. So the obvious
hypothesis is wrong.

**Coordinate magnitude also made no difference** — identical results at the
origin and at UTM scale. A local-origin translation would *not* have fixed this,
which is worth knowing before anyone proposes one.

## 3. The real driver: close features

`minangle_case.c` — a 1000 × 1000 m box plus one interior constraint segment
running parallel to the bottom edge at height `gap`. Min angle 26°, no area cap:

| gap between the two features | result |
|---|---:|
| 10 m | ok, 194 triangles |
| 1 m | ok, 2,112 triangles |
| 0.01 m | ok, **186,501** triangles (36 MB) |
| 0.001 m | **HANG** (>25 s, non-terminating) |
| 0.0001 m and below | **HANG** |

Identical at UTM-scale coordinates. **A 1 mm gap anywhere in a 1 km domain is
enough to make min-angle meshing non-terminating.**

## 4. Lowering the angle only postpones it

Same geometry, `gap = 0.001 m`:

| min angle | triangles |
|---:|---:|
| 0° | **6** |
| 5° | 208,669 |
| 10° | 461,195 |
| 15° | 816,249 |
| 20° … 26° | **HANG** |

A domain that needs 6 triangles needs 208,669 at only 5°. The blow-up is in the
feature separation, not the angle — the angle just decides how violently it
expresses itself.

## 5. Why "large areas" specifically

Area itself is not the problem. Larger extents simply contain more constraint
features, so the chance that *at least one* pair sits a millimetre apart
approaches 1 — and one such pair is sufficient. That also explains "sometimes":
it depends on whether the extent happens to include a pathological pair.

## 6. Two levers already exist and are not being used

**a. The cancellation hook fires during the runaway.** `minangle_hook.c` builds
with `-DEXTERNAL_TEST` and counts `triunsuitable()` calls: the hook was reached
**500,000 times** in the hanging case and aborting through it returned cleanly
(`rc=-1`, no crash). So Cancel works, and a *vertex budget* that aborts with a
useful message is implementable — Triangle calls back constantly.

**b. Triangle's `-S` Steiner cap already bounds it.** `minangle_scap.c`, same
hanging case (`gap = 1 mm`, 26°):

| `-S` cap | result |
|---|---|
| 1,000 | ok, 1,487 triangles — instant |
| 10,000 | ok, 14,700 triangles |
| 100,000 | ok, 153,043 triangles |
| unlimited (`-1`) | **HANG** |

The cap degrades quality gracefully instead of failing. The GUI exposes this as
"Max Steiner points" (`meshgenerationdialog.cpp:2713`) but **defaults it to
`(unlimited)`** — i.e. to the one setting that hangs.

## Reproducing

```sh
cc -DTRILIBRARY -DANSI_DECLARATORS -DNO_TIMER -Ivendor/triangle -w \
   tests/manual/mesh_minangle/minangle_case.c vendor/triangle/triangle.c \
   -o tests/manual/mesh_minangle/minangle_case -lm
timeout 25 tests/manual/mesh_minangle/minangle_case 0 0 0.001 26   # hangs
timeout 25 tests/manual/mesh_minangle/minangle_case 0 0 0.01  26   # 186k tris
```

`minangle_hook.c` needs `-DEXTERNAL_TEST` (it supplies `triunsuitable`).
`minangle_probe2.c` is superseded by `minangle_case.c` — it runs every case in
one process and therefore hangs on the first bad one.
