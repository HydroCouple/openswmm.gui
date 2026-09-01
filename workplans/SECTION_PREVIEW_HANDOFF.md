# Slice SP — Section Preview Panels: HANDOFF

Status: **code complete, NEVER COMPILED.** Plan: `SECTION_PREVIEW_WORKPLAN.md`.
Mockups: `section_preview_examples/`.

> **Read this first.** The implementing session had no Qt toolchain and no
> macOS host — the workspace is Linux and the engine ships as a Darwin dylib.
> Every line here was written against the real headers
> (`openswmm.engine/install/Darwin/include/openswmm/engine/openswmm_xsect.h`)
> and the real call sites, but **nothing was built, run, or rendered**. Assume
> compile errors. Your first job is to make it build, then to look at it.

---

## 1. What changed, and why

Adds engine-accurate vector section drawings to three surfaces, all sharing
one renderer:

| Surface | What it shows |
|---|---|
| **Section View dock** (new) | Selected link → cross-section ⇄ profile toggle. Selected node → manhole profile with every connecting pipe at its invert offset + plan-view compass inset. |
| **Cross-section editor** | Live preview pane; procedural shape icons replacing the 26 static SVGs. |
| **LID control editor** | Per-type layer-stack diagram, active tab highlighted. |

**Design deviation from the reviewed plan (user-requested, mid-implementation):**
the property-grid preview became a **standalone dock** (`SectionViewPanel`)
instead of a pane embedded in `PropertiesPanel`. `PropertiesPanel` is therefore
**untouched**. The workplan's §3.1 still describes the embedded design.

### Two findings that changed the plan

1. **There is only ONE shape-numbering space.** The workplan (and the earlier
   exploration) warned that the engine's xsect module used different codes than
   `openswmm_links.h`. That was wrong: `openswmm_xsect.h` takes and returns
   `SWMM_XSectShape` codes from `openswmm_links.h`. No mapping table exists or
   is needed. `test_xsectsampler::shapeIdRoundTrips` is the guard if that ever
   changes.

2. **A real pre-existing bug was found and fixed.** `linkcompoundeditdialog.cpp`
   compared shapes against stale literals: `/*IRREGULAR*/ 19` — but
   `SWMM_XSECT_IRREGULAR` is **21** (19 is `VERT_ELLIPSE`). Effect: opening the
   cross-section dialog on an irregular conduit never showed the transect
   picker and left the raw index in the geom1 spin. Exactly the drift the
   `XsectShapeRow` comment warns about. All comparisons in that file now use
   named constants. **This fix is worth calling out in the commit message and
   is independently verifiable — see §4, check 6.**

---

## 2. File inventory

### New — `include/ui/sectionview/` + `src/ui/sectionview/`

| File | Role |
|---|---|
| `xsectsampler.{h,cpp}` | Move-only RAII wrapper over `SWMM_XSect`. `fromShape` / `fromLink` / `fromTransect` / `fromStreet` / `fromCurve`; `outline()` samples `swmm_xsect_width_of_depth_array` over a depth ladder and mirrors it. |
| `sectiondiagram.{h,cpp}` | `SectionDiagramModel` (plain value, model units) + `paintSectionDiagram()`. All colours from `QPalette`. Progressive decluttering: leaders → dims → footer as the widget shrinks. |
| `sectionpreviewwidget.{h,cpp}` | `QWidget` host. `renderToImage()` exists for tests. |
| `sectionmodelbuilders.{h,cpp}` | `buildLinkSection` / `buildLinkProfile` / `buildNodeProfile` / `buildXsectEditorPreview` / `buildSamplerPreview`. Engine → model, no widgets. |
| `lidlayerdiagram.{h,cpp}` | `lidLayersFor(type)` + `buildLidLayerDiagram()`. Takes a POD input struct, **not** a `LidControlProvider*`, so it carries no `lid/` dependency. |
| `xsecticonrenderer.{h,cpp}` | `xsectShapeIcon()` + `nominalGeomsFor()`. `QPixmapCache`-backed. |

### New — panel + tests

- `include/ui/panels/sectionviewpanel.h`, `src/ui/panels/sectionviewpanel.cpp`
- `tests/gui/test_xsectsampler.cpp` — engine-facing (11 cases, data-driven over every surfaced shape)
- `tests/gui/test_sectiondiagram.cpp` — model/painter/LID (14 cases, no engine)

### Modified

| File | Change |
|---|---|
| `include/ui/properties/xsectshapegeom.h` | +5 shapes (BASKETHANDLE, SEMICIRCULAR, FORCE_MAIN, CUSTOM, DUMMY); `kXsectCustomId`; `xsectGeomApplies()` excludes CUSTOM geom2. |
| `include/ui/dialogs/linkcompoundeditdialog.h`, `src/…cpp` | Third splitter pane (preview); procedural icons; `refreshXsectPreview()`; **stale-literal fix**; CUSTOM withheld from picker. |
| `include/ui/dialogs/lidcontroleditordialog.h`, `src/…cpp` | Third splitter pane (diagram); `refreshLayerDiagram_()`; tab-change wiring. |
| `include/swmmvis.h`, `src/swmmvis.cpp` | `mSectionViewPanel` member; dock created + tabified behind Properties; View ▸ Panels entry; project binding; selection dispatch; edit-sync connections. |
| `CMakeLists.txt`, `tests/gui/CMakeLists.txt` | Source + test registration. |
| `CHANGELOG.md` | Unreleased ▸ Added + Fixed. |

---

## 2a. Cross-section type coverage

All 26 `SWMM_XSectShape` values are handled somewhere; what differs is how the
geometry is obtained — and the tabulated shapes do **not** behave uniformly.

| Shape group | Source of geometry | Section View dock | XSection editor | Palette icon |
|---|---|---|---|---|
| 21 parametric shapes (CIRCULAR … ARCH, incl. FORCE_MAIN) | `swmm_xsect_create` from geom1..4 | ✅ | ✅ live per keystroke | ✅ from nominal geoms |
| **STREET** | dock: `swmm_street_get_params` (geom1 *is* the street index) → `swmm_xsect_create_street`. Editor: from the `StreetProvider` | ✅ any lifecycle state | ✅ | ✅ canned half street |
| **IRREGULAR** | dock: `swmm_link_create_xsect` only. Editor: from the `TransectProvider` | ⚠️ needs a resolved model | ✅ | ✅ canned compound channel |
| **CUSTOM** | `swmm_link_create_xsect` only | ⚠️ needs a resolved model | ⛔ withheld from picker | ✅ canned shape curve |
| **DUMMY** | none — has no geometry | ✅ explanatory message | ✅ explanatory message | dashed placeholder (correct) |

### Why IRREGULAR and STREET differ — read before "fixing" this

An earlier revision of this slice resolved **both** from their table index in
`geom1`. That is correct for STREET and **wrong for IRREGULAR**, and the wrong
version is more dangerous than no version: it would silently draw a *different*
transect whenever the value landed inside `[0, transectCount)`.

Verified against the live engine with `tests/scratch/sp_geom1_probe.inp`
(gitignored; three conduits on three different transects plus one street):

| link | `[XSECTIONS]` row | table index | `swmm_link_get_xsect` g1 |
|---|---|---|---|
| C1 | `IRREGULAR TSECT0` | 0 | **5** ← transect depth |
| C2 | `IRREGULAR TSECT1` | 1 | **9** ← transect depth |
| C3 | `IRREGULAR TSECT2` | 2 | **3** ← transect depth |
| C4 | `STREET ST_B` | 1 | **1** ✅ index |

Cause: the `[XSECTIONS]` parser doesn't populate `geom1..4` for irregular
sections, and `swmm_link_get_xsect` has a name→index resolution branch for
STREET but none for IRREGULAR, so it falls through to reporting *derived*
geometry (g1 = full depth, g2 = max width, g3 = area). There is currently no C
API that returns a conduit's transect index or name.

**To close this properly:** add an engine-side getter mirroring the STREET
branch, then restore the direct path. Do not infer the index from `geom1`.

**Knock-on inside the cross-section editor.** Fixing the `19` → `IRREGULAR`
literal (§1) *activated* a block that had never run, and that block seeded the
transect picker from `lround(geom1)`. Two sites were therefore changed to stop
inventing a name they cannot trust:

- the picker now opens **unselected** for irregular conduits instead of
  preselecting a possibly-wrong transect;
- `computeXsectSummary()` reports the transect the user picked in this dialog,
  or an em-dash — never a name derived from `geom1`.

Note the asymmetry that makes this genuinely undecidable from the GUI: for a
link whose xsection was set through the API *in this session*, `geom1` **is**
the index (`swmm_link_set_xsect` stores it raw); for a link parsed from an
`.inp` it is the derived depth. Same getter, same shape, two meanings, no flag
to tell them apart. Restoring preselection needs the engine getter, not a
heuristic. (`swmm_link_set_xsect` also has no IRREGULAR branch, so writing the
index does not itself attach the transect — worth confirming the editor's
irregular *write* path end-to-end during verification.)

### ⚠️ Engine bug found while verifying this (not a GUI issue, not fixed here)

**Irregular conduits lose their transect reference when a model is written.**
`InpWriter.cpp` has a `STREET_XSECT` branch (`:1485`) and a `CUSTOM` branch
(`:1497`) that emit the retained `pump_curve_name`, but **no IRREGULAR
branch** — irregular links fall through to the numeric path and are written as
derived numbers. Round-tripping the probe deck produces:

```
C1               IRREGULAR        5 12 0 0        1      <-- TSECT0 is gone
C4               STREET           ST_B   ...             <-- correct
```

Re-reading that file, the conduits no longer reference any transect. This is
silent data loss on save, independent of anything in this slice — it is worth
its own issue in `openswmm.engine`, and the fix looks like a ~6-line branch
mirroring the STREET one directly above it. Repro:
`tests/scratch/sp_geom1_probe.inp` → open → write → inspect `[XSECTIONS]`.

---

---

## 2b. Round 2 — legibility, materials, and two proposals

Follow-up requested after the first review. Mockups regenerated by
`section_preview_examples/gen_lid_and_structure_examples.py`.

### Implemented

1. **Smoother outlines.** `XsectSampler::outline()` now uses a cosine-spaced
   depth ladder at 240 intervals by default (was uniform at 96). Width changes
   fastest at the invert and crown, so a uniform ladder starved exactly the
   high-curvature ends — which is what made circular and egg sections look
   faceted. Endpoints and mid-depth stay exact, so the bounding-box-vs-`wMax`
   check still holds. Pinned by
   `test_xsectsampler::outlineIsDenserWhereCurvatureIsHighest`.

2. **Zoom / pan / zoom-extents** on every diagram: scroll to zoom about the
   cursor, middle-drag to pan, middle double-click to fit. `DiagramViewport`
   rides on top of the automatic fit, so "fit" is just a default-constructed
   value and nothing has to be recomputed when the model changes.
   - **Geometry scales; text does not.** That is the engineering-drawing
     convention and it is what makes zoom a *legibility* tool — the drawing
     spreads out from under crowded labels instead of magnifying the crowding.
   - **`setModel()` deliberately does NOT reset the view.** Hosts rebuild the
     model on every keystroke; resetting there would yank the drawing back to
     fit while the user is reading a zoomed dimension. Hosts call
     `zoomToExtents()` when the *subject* changes — new selection, new shape,
     new LID type, section⇄profile toggle.
   - Zoom anchoring uses the fit rect the painter reports back through the new
     `fitRectOut` parameter rather than the widget centre, which is off by the
     header/footer and adaptive-margin reservations.

3. **Illustrated LID diagrams** (mockups 7 and 8): material textures, planting
   by type, ponded water to the berm, perforated underdrain in section, and
   per-type ornaments. New model primitives: `DiagramTexture` on `DiagramPoly`,
   plus `DiagramVegetation`, `DiagramCircle` and `DiagramArrow`. Textures are
   generated from a **hash, not a RNG**, so a layer looks identical on every
   repaint — a texture that reshuffles on resize reads as noise, not material.
   They are drawn in screen space and clipped to the polygon, so the grain
   stays a constant visual size under zoom.

4. **Profile slope fidelity** (mockup 11). `buildLinkProfile` used
   `uniformScale = false`, which stretched both axes to fill the pane. That
   silently adopted whatever exaggeration the aspect ratio implied, so the
   apparent gradient of a pipe changed with the dock size — measured at 5:1 in
   a short pane and 10:1 in a tall one for the same 120 m / 0.25 % reach.

   The fit now holds an explicit V:H ratio and lets whichever axis runs out of
   room set the scale (the uniform-scale fit generalised). `Auto` derives the
   ratio from the MODEL's proportions, not the pane's: natural aspect ÷ a 6:1
   drawn target, floored at 1.0, capped at 10:1, snapped down to a
   conventional value. Identical in every dock size, which an earlier draft
   keyed to a pixel legibility floor was not — that still drifted with pane
   width. The achieved ratio is drawn on the profile.

   Model fields: `verticalExaggeration` (explicit), `targetDrawnAspect` and
   `maxVerticalExaggeration` (bound the automatic choice),
   `annotateExaggeration`. The painter reports the ratio through
   `achievedExaggerationOut`; `SectionPreviewWidget` re-exposes it as
   `achievedVerticalExaggeration()`. The dock has a **V:H** combo whose
   selection persists across selections and syncs both ways.

   **`targetDrawnAspect = 0` deliberately preserves the old fill-the-pane fit,
   unsnapped and uncapped.** Node profiles and LID stacks put an arbitrary unit
   on x (a normalised frame, a nominal plan width), so a V:H ratio there is
   arithmetic on nothing — an interim version snapped them too and would have
   collapsed a LID stack to 47 % of its width. Pinned by
   `test_sectiondiagram::legacyFitIsUntouchedWithoutATarget`.

   Verified numerically before implementation (`/tmp/ve3.py`, not committed):
   the ratio is identical across six pane sizes, held exactly for every
   explicit setting, content fits on both binding branches, short/steep reaches
   land at 1:1 and very long ones at the cap. Guarded by five tests asserting
   the ratio the widget reports rather than probing pixels — an earlier draft
   measured the ink bounding box and was silently measuring the exaggeration
   NOTE, which is anchored to the pane bottom and therefore grew with it.

### Proposals — NOT implemented, awaiting sign-off

| Mockup | Proposal |
|---|---|
| `9_orifice_opening_proposal.png` | Draw an orifice as what it physically is: an **opening in the wall between its two nodes**, with the shared structure wall, water at each node's head, flow through the opening, and the crest/crown elevations dimensioned. Today an orifice renders as a free-floating cross-section, which misrepresents it. |
| `10_pump_levels_proposal.png` | Pump **wet well with control levels** — startup and shutoff depths as annotated horizontal lines on the well, the water band between them, the inlet conduit, the pump and rising main, and a small pump-curve inset. |

Both need engine data the builders do not yet read:
`swmm_link_get_crest_height` / `_discharge_coeff` / `_flap_gate` for the
orifice; `swmm_link_get_pump_startup_depth` / `_shutoff_depth` / `_pump_curve`
/ `_pump_init_state` for the pump. All exist — see `openswmm_links.h`.
Estimated one slice each on top of the existing builders.

---

## 3. Build and test

```bash
cd ~/Documents/Projects/cbuahin_github/openswmm.gui
cmake --preset Darwin-debug            # or your usual preset
cmake --build --preset Darwin-debug -j
```

**Prerequisite to check first** — the sampler is the only consumer of
`openswmm_xsect.h`. Confirm it is exported by the installed engine package:

```bash
ls ~/Documents/Projects/cbuahin_github/openswmm.engine/install/Darwin/include/openswmm/engine/openswmm_xsect.h
nm -gU ~/Documents/Projects/cbuahin_github/openswmm.engine/install/Darwin/lib/libopenswmm.engine.dylib \
  | grep -c swmm_xsect_
# expected: header present, ~27 exported symbols
```
Both were verified present. If the header is *installed* but not listed in the
`OpenSWMMEngine::openswmm_engine` target's interface, `#include
<openswmm/engine/openswmm_xsect.h>` will fail — that is a CMake export fix on
the engine side, not a change to this code.

### Tests

```bash
ctest --preset Darwin-debug -L gui -R 'xsectsampler|sectiondiagram' --output-on-failure
ctest --preset Darwin-debug -L gui -R 'xsectgeominline' --output-on-failure   # regression: shape table grew
ctest --preset Darwin-debug -L gui --output-on-failure                        # full sweep
```

`test_xsectgeominline` covers the shape/geom table that Slice SP extended; if
it asserts on a **count** of shapes it will now fail legitimately and needs its
expectation updated (21 → 26 rows). Check before assuming a real regression.

### Expected first-build friction (most-likely-first)

1. `openswmm_xsect.h` not on the include interface → see above. This is the
   single most likely failure, and it is an engine-side CMake export issue.
2. `qsizetype` → `int` narrowing under `-Werror`; the known spots are cast, but
   `lidlayerdiagram.cpp` still loops with `int i` against `layers.size()`.
3. AUTOMOC: `sectionpreviewwidget.h` and `sectionviewpanel.h` carry `Q_OBJECT`
   and are listed in both the app target and the test targets. If moc misses
   them, the header is missing from a source list.
4. `LinkCompoundEditDialog::refreshXsectPreview` reaches into
   `TransectProvider` / `StreetProvider`; those headers were already included
   by that file, but the accessor names (`meanderFactor()`, `roadRoughness()`,
   `sides()`) were read from the headers, not exercised.
5. `actionToggleDockSectionView` may need an ActionRegistry catalog entry
   under `view.dock.*` — the other six docks have one; this was not checked.

---

## 3a. What WAS verified (and how)

No compiler ran, but the code was put through two passes worth trusting:

**A. Line-by-line review against the installed engine headers.** Every engine
call was checked for exact name, signature, argument order and return
convention against
`openswmm.engine/install/Darwin/include/openswmm/engine/*.h`. All ~25 of them
match, including the out-parameter order of `swmm_xsect_full_properties`
(`y_full, a_full, r_full, w_max, s_full, a_max`) and the fact that
`swmm_link_count` returns its value directly rather than through a pointer.
Namespaces, forward declarations and CMake registration also check out.

That review found **one blocker and four real defects, all now fixed:**

| Was | Fix |
|---|---|
| **BLOCKER** `computeBounds()` used `QRectF::isNull()` as its "nothing accumulated yet" flag — but a zero-size rect *is* null, so the accumulate branch never ran and every diagram returned null bounds. Nothing in the entire feature would have drawn anything but its title. | Explicit `bool have` flag. |
| Depth dimensions had a negative `pixelOffset`; since the offset runs along the pixel-space normal `(-dy, dx)`, a bottom→top dimension needs a *positive* offset to sit right of the drawing. Both the section and node-profile depth dims would have been drawn across the section. | Signs corrected; the reasoning is now a comment at each call site. |
| Open-channel outlines were stroked as two halves split at the ring midpoint, which left the *invert* unstroked as well as the top — visible on RECT_OPEN / TRAPEZOIDAL / MOD_BASKET, whose bottom edge is real geometry. | Ring rotated so both ends of the polyline land on the top seam; one polyline, invert stroked. |
| Plan-inset inbound arrowheads had a spurious `+ M_PI`, rendering them identical to outbound ones. | Removed. |
| Two `QPainter`s were still live on the surface being returned/copied (`renderToImage`, `placeholderIcon`). | Scoped so the painter ends first. |

Also addressed: HiDPI icon rendering (2× pixmap + DPR, cache key now includes
`Base`/`Highlight` too), a dead `m_hintLabel`, a dead slope guard, and unused
includes.

**B. Numeric simulation of the painter's geometry.** The fit transform,
dimension-normal placement and open-ring reordering were re-implemented in
Python and run against a sampled circular section (`/tmp/verify.py`, not
committed). Confirms: bounds come back non-degenerate; the depth dimension
lands right of the section (x 339 vs section right edge 305); the width
dimension lands above it (y 37 vs section top 63); neither runs off-canvas at
400×320; and the open-ring reorder omits exactly one edge, the top seam.

**What is still unverified:** everything a compiler or a screen would tell
you. Treat §4 as mandatory, not optional.

---

## 4. Manual verification checklist

Nothing below has been seen on screen. Compare against
`section_preview_examples/*.png` — those are the intended output.

1. **Dock exists.** View ▸ Panels ▸ Section View toggles it; it starts tabbed
   behind the Property Browser. Toggle action id
   `actionToggleDockSectionView` — if the ActionRegistry requires a catalog
   entry for `view.dock.*` ids, add one (the other six docks have them; this
   was not checked).
2. **Link selection.** Click a circular conduit → section with a depth
   dimension, width dimension, invert/crown leaders, and an `A / R / W` footer.
   The numbers must match the Property Browser's geom1 and the node inverts.
3. **Profile toggle.** Switch to Profile → both manholes, ground hatch,
   sloping barrel, `L` and `S` along the barrel. **Sanity-check the slope
   sign** against a conduit you know: it is computed as
   `100·(invUp − invDn)/length`, so a downhill pipe should read positive.
4. **Node selection.** Click a junction with ≥3 links → stubs left (inbound)
   and right (outbound) at distinct inverts, max-depth dimension, compass inset
   with one spoke per link. Verify a spoke's bearing against the map.
5. **Section View follows edits.** With a conduit selected, change geom1 in the
   Property Browser → the drawing must resize immediately. Same via the
   Attribute Table.
6. **The IRREGULAR fix.** Open the cross-section editor on a conduit and pick
   IRREGULAR. *Before* this change the transect picker stayed hidden; it must
   now appear, list transects by name, and the preview must draw the transect's
   natural-channel section. This is the highest-value single check here.
6a. **STREET vs IRREGULAR in the dock.** Select a *street* conduit on the map:
   the Section View must draw the crowned-road section with the street's name in
   the subtitle, **without** validating or running first (it reads `[STREETS]`
   directly). Then select an *irregular* conduit: mid-edit it is expected to show
   the "resolved when the model is validated or run" message, and to draw the
   channel after a run. If the irregular case ever shows a section *before* a
   run, someone has re-introduced the geom1-as-index bug — see §2a.
7. **Editor preview + icons.** Every tile in the shape palette shows a real
   section outline — including IRREGULAR (a compound channel) and STREET (a
   crowned road with curbs). The only dashed placeholder should be DUMMY. Type
   in a geom spin → preview redraws per keystroke and the edited dimension is
   accented.
8. **LID diagram.** Cycle the type combo: Rain Barrel → 1 layer, Green Roof →
   surface/soil/drainage-mat, Permeable Pavement → 4 layers. Layers with no
   entered value must render hatched with an em-dash, never as zero. Switching
   tabs moves the highlight.
9. **Dark mode.** View ▸ Appearance ▸ Dark — every diagram must stay legible
   (all colours derive from the palette; no hard-coded inks).
10. **Narrow dock.** Drag the dock narrow: leaders drop out ~300 px, dimensions
    ~220 px, footer below ~150 px tall. No overlapping text, no crash.
11. **A11y.** The new dock and the two modified dialogs must still pass
    `swmvis_test::assertDialogA11y` — mnemonics `&Section` / `&Profile` could
    collide with an existing accelerator in those dialogs.

---

## 5. Known gaps (deliberate, not oversights)

- **Storage-node chambers draw as a plain rectangle.** The silhouette from the
  storage shape/curve (`storageshapegeom.h`) is not implemented; the footer
  says so. Workplan §3.1 has the design.
- **LID pavement + drainmat layers are always "unknown."** The engine has
  `swmm_lid_set_pavement` / `_drainmat` but `LidControlEditorDialog` has no
  tabs for them, and the engine exposes **no LID getters at all**, so any
  control loaded from a file shows unknown layers until the user re-enters
  values. Adding those two tabs is the natural follow-up.
- **CUSTOM is withheld from the shape picker** — its geom2 is a shape-curve
  index and no curve picker exists. It is in the shape *table* (so the
  Property Browser and Attribute Table name it correctly) but not offered for
  selection. Surface it with the picker.
- **IRREGULAR and CUSTOM in the dock need a resolved model** (see §2a for the
  measured reason). Both fall back to `swmm_link_create_xsect`, which returns
  `SWMM_ERR_LIFECYCLE` while the model is BUILDING, so mid-edit the dock shows
  an explanatory message pointing at the cross-section editor — which *does*
  preview them, from the registries. IRREGULAR needs an engine getter; CUSTOM
  additionally needs the SHAPE-curve type code disambiguated
  (`openswmm_tables.h:79` says `4 = CURVE_SHAPE`, `:107` says `5 = SHAPE`), after
  which `XsectSampler::fromCurve()` — already written and tested — can be wired
  up. CUSTOM is also withheld from the shape picker pending a curve picker.
- **No golden-image test.** The painter is asserted via "did it draw ink,"
  not pixel comparison — deliberate, since text metrics differ per platform.
- **Old SVG assets left in place.** `resources/images/*_xsect.svg` and their
  `swmmvis.qrc` entries are now unreferenced. Left for the parity sign-off in
  check 7; delete them in a follow-up commit once the icons are confirmed.

---

## 6. Committing

⚠️ **The working tree already contained unrelated uncommitted work** before
this slice: `swmmdatetime.h`, `swmmdatetimeformat.h`, `datetimedelegate.*`,
the timeseries editor/chart/table changes, `plans/`, and `.claude/`. Do not
sweep those into this commit.

`CMakeLists.txt` and `tests/gui/CMakeLists.txt` contain **both** that work and
this slice's registrations — stage those two files hunk-by-hunk
(`git add -p`). Every other file below is exclusively this slice.

```bash
git add -p CMakeLists.txt tests/gui/CMakeLists.txt   # SP hunks only

git add include/ui/sectionview src/ui/sectionview \
        include/ui/panels/sectionviewpanel.h src/ui/panels/sectionviewpanel.cpp \
        tests/gui/test_xsectsampler.cpp tests/gui/test_sectiondiagram.cpp \
        include/ui/properties/xsectshapegeom.h \
        include/ui/dialogs/linkcompoundeditdialog.h src/ui/dialogs/linkcompoundeditdialog.cpp \
        include/ui/dialogs/lidcontroleditordialog.h src/ui/dialogs/lidcontroleditordialog.cpp \
        include/swmmvis.h src/swmmvis.cpp \
        CHANGELOG.md
```

**`workplans/` is gitignored** (`.gitignore:99`), as are the 34 workplans
already there — so this document, the plan and the mockups stay local by
design. Don't `git add -f` them; if any of that content needs to ship, move it
into `docs/` deliberately. The `user-guide.md` entry for the new dock is the
one piece that arguably belongs there and has **not** been written.

Suggested message:

```
feat(ui): engine-accurate section/profile diagrams (Slice SP)

Adds a dockable Section View, a live preview in the cross-section editor,
and a per-type layer diagram in the LID editor, all rendered from the
engine's swmm_xsect_* geometry API rather than static artwork. Surfaces the
five cross-section shapes the picker never offered and replaces the 26
hand-drawn *_xsect.svg thumbnails with procedural, theme-aware icons.

Fixes a pre-existing bug in LinkCompoundEditDialog: shape comparisons used
literals from before the 6.0 SWMM_XSectShape renumbering, so IRREGULAR (21)
was tested as 19 (VERT_ELLIPSE) and the transect picker never appeared.
```

Do not commit until `ctest -L gui` is green and checks 1–8 in §4 have been
seen on screen. If the diagrams need visual tuning, the constants worth
touching first are the layout block at the top of `sectiondiagram.cpp`
(`kAnnotationPad`, `kMinWidthFor*`) and the `pixelOffset` values in the
builders — no structural change needed.
