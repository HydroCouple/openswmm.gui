# Profile Attribute Tracks — Verification & Fix Handoff

**Date:** 2026-08-16
**Implements:** `workplans/PROFILE_ATTRIBUTE_TRACKS_PLAN_2026-08-16.md` (all phases; deviations in §5)
**Change-set status:** written and reviewed by a second agent (12 findings, all required ones fixed), **NOT COMPILED** — the implementing agent had no Qt toolchain. Expect to fix residual compile errors yourself; §4 tells you where they are most likely.

## 0. What was built

A collapsible pane of stacked mini-charts ("tracks") under the profile plot in `ProfilePlotDialog`. One track per selected result attribute (6 node + 5 link), each with its own y-axis; all share the profile's x-axis (virtual chainage) and stay pixel-column-aligned through zoom/pan in either pane. Tracks animate with the simulation clock, draw a min/max envelope for the primary source, tint overlaid sources with their scenario color, and are styled via a Q_PROPERTY options object (toolbar "Tracks ▾" menu + a new Attribute Tracks tab in Display Options). Collapse three ways: drag the splitter handle down, the toolbar toggle (`showAttributeTracks`, state persisted), or auto-hide when no attribute is selected.

## 1. Build + unit tests (first task)

```bash
cd /Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui
cmake --preset <usual> && cmake --build build -j
ctest --test-dir build -R "profile_attribute_tracks" --output-on-failure
ctest --test-dir build -L gui --output-on-failure   # collateral check
```

New test binary `test_profile_attribute_tracks` (9 tests): options generic↔Q_PROPERTY parity, one-changed()-per-edit, QSettings round-trip (QPen through @Variant INI), canonical visible-attribute order, sampler classification, the pixel↔virtual-x mapping contract, mapping round-trip, minimumHeight vs track count, and no-echo from `setVisibleXRange`. All were hand-traced to pass; a reviewer confirmed after the ThemeColors include fix.

Also rerun `test_dialog_layout_persistence` — the dialog gained a named splitter (`profileSplit`) and a named toggle (`showAttributeTracks`) that ride the automatic persistence.

## 2. Files

| File | What |
|---|---|
| `include/plot/profileattributesampler.h` + `src/plot/profileattributesampler.cpp` | **New.** Generic per-attribute path sampler (`fetch` → `[pathIdx][period]` + envelopes). Classification helpers are **inline in the header** so UI/tests don't link the engine chain. Mirrors `profilesourcefetcher.cpp` incl. the inclusive-`end_period` gotcha and empty-row-means-no-data. |
| `include/plot/profileattributetrackoptions.h/.cpp` | **New.** Options QObject: 11×(visible+pen) Q_PROPERTYs + chrome (trackHeightPx, showTrackTitles, envelopesVisible, envelopeOpacity); generic accessors; meta-object-driven `writeTo/readFrom(QSettings)`; `displayLabelFor`. |
| `include/plot/profileattributetrackswidget.h/.cpp` | **New.** QPainter tracks pane. Host pushes resolved `Track`s; node attrs = polyline (breaks at gaps), link attrs = per-link horizontal segments; envelope band per contiguous run; x-pan/wheel-zoom emit `visibleXRangeChanged`; `setVisibleXRange` **never** emits (no echo). `pixelForVirtualX/virtualXForPixel` public for the alignment test. |
| `include/plot/profileplotwidget.h` + `src/plot/profileplotwidget.cpp` | **Modified.** New signal `visibleXRangeChanged(double,double)` emitted (deduped, incl. NaN-sentinel first emission) from all 8 x-mutation sites; new `setVisibleXRange()`; `virtualToRealChainage` made public; `virtualChainageTable()`, `chartLeftMarginPx()/chartRightMarginPx()` accessors. No behavioral change for existing consumers. |
| `include/ui/dialogs/profileplotdialog.h/.cpp` | **Modified.** Splitter `"profileSplit"` (plot non-collapsible / pane collapsible) with the pane in a `QScrollArea` (11 tracks would otherwise force a ~1300 px dialog); "Tracks ▾" menu + `showAttributeTracks` master toggle; both-way x-sync behind `m_syncingX`; `rebuildTracks()` worker mirroring `rebindSources()` (own cookie `m_trackLoadCookie`, per-(layer,attr) cache `m_attrCache`); animation hook in `onAnimationTimeChanged`; export renders the splitter when the pane is visible; `showEvent` reconciles a persisted-collapsed splitter with a persisted-checked toggle; cache invalidation extended to the attr cache **and now bumps both load cookies on layer destruction** (fixes a latent dangling-key bug that also existed in `rebindSources`). |
| `include/ui/dialogs/profileoptionsdialog.h/.cpp` | **Modified.** `setTrackOptions()` adds the "Attribute Tracks" QPropertyModel tab. |
| `CMakeLists.txt`, `tests/gui/CMakeLists.txt` | 3 headers + 3 sources registered; test target added (links options+widget+plotattribute+thememanager only). |
| `CHANGELOG.md` | `[Unreleased]/Added` entry. |

Settings: selection/styles under `ProfilePlot/AttributeTracks` (app-global, written on every options change); splitter + toggle under `Dialogs/ProfilePlotDialog/…` automatically.

## 3. Manual verification (needs a display + a model with results)

Run a simulation, select a path, Analysis ▸ Plot Profile.

1. **Baseline regression:** with no attribute selected the dialog must look exactly as before — no splitter handle, no pane. Toolbar gains "Tracks ▾" and a (disabled) "Show Attribute Tracks" toggle.
2. **Add tracks:** Tracks ▾ → check Link ▸ Velocity, Node ▸ Depth. Pane appears with two tracks, titles like "Velocity (ft/s)", each with its own y-axis.
3. **Alignment (the core promise):** zoom into the profile (wheel or rubber-band). A node's manhole glyph and that node's vertex in the Depth track must sit on the same pixel column. Check at a pump/weir if the model has one — the synthetic gap must appear identically in both panes.
4. **Both-way sync:** wheel-zoom and drag inside a track → the profile follows. Fit (Home) → both reset. No jitter/feedback loops.
5. **Animation:** play the simulation — track curves move with the HGL; envelope band stays put. Envelope off via Display Options ▸ Attribute Tracks.
6. **Collapse, three ways:** (a) drag handle to bottom → toggle unchecks; (b) toggle off/on → pane hides/returns at prior proportions; (c) uncheck all attributes → pane and handle vanish, toggle disables. Reopen dialog: state remembered. Also: collapse by drag, close, reopen — toggle must come back UNchecked (showEvent reconcile).
7. **Styling:** Display Options ▸ Attribute Tracks — change Velocity pen color/width, track height, titles off. Toolbar menu checks must mirror tree edits and vice versa.
8. **Multi-source:** check two sources in Sources ▾ — two tinted curves per track, envelope only for the primary.
9. **Sparse data:** pick a path with an element missing from the .out (or a short comparison run) — curve and envelope must show a GAP there, not zeros; short series stop animating past their last period.
10. **Export PNG:** with tracks visible the export contains both panes.
11. **Perf:** all 11 attributes on a large .out — first build shows one fetch delay, playback stays smooth (per-frame work is indexing only), pane scrolls rather than growing the dialog.

## 4. Known risks / where compile errors will bite first

- **`rebuildTracks()` lambda captures** (`jobs`, `specs` of function-local struct types) and `QFutureWatcher<TracksResult>` with a local struct — legal C++20/Qt6, but template error spew lands here if anything's off.
- **`ProfileOptionsDialog::setTrackOptions`** under `#ifdef HAVE_QPROPERTYMODEL` — verify both branches compile (the #else uses QLabel, included).
- **Q_PROPERTY plumbing:** 44 longhand accessors in `profileattributetrackoptions.cpp` — a typo there produces moc/link errors naming the exact accessor.
- **`QPen` in QSettings INI:** should serialize as `@Variant(...)`; if `readFrom` comes back with default pens, check `QMetaProperty::write` conversion from the stored QVariant.
- **Off-thread layer reads** in the sampler follow the existing `rebindSources` contract (QPointer-guarded snapshot). The destroyed-layer window is now closed by the cookie bump; if you see a crash on project close with a fetch in flight, look at `ensureCacheInvalidationWired`'s destroyed lambda first.
- The reviewer flagged (accepted, not fixed): chrome-only option edits (track height, opacity) trigger a full — cache-hit, cheap — `rebuildTracks`; two open profile dialogs share the `ProfilePlot/AttributeTracks` settings group last-writer-wins without live cross-sync.

## 5. Deviations from the plan (deliberate, documented)

- **Options shape:** per-attribute *visible + pen* plus four chrome knobs — not the plan's fuller list (per-attribute envelope brush/visibility became one global `envelopesVisible` + `envelopeOpacity` derived from the pen color; y-axis number-format option and `showValueAtCursor` readout dropped). CLAUDE.md §2 minimum-code; all are additive later.
- **Auto-scale-y:** tracks always use the full-path envelope extent (stable during animation); the plan's visible-range autoscale option was dropped for v1.
- **Persistence location:** app-global settings group rather than per-model (matches how the profile's own layer toggles behave).
- **Right-click attribute menu inside the pane** (plan 4b) not implemented — toolbar menu + options tree cover selection.

## 6. If the alignment check (§3.3) fails

The contract is: same margins (`chartLeftMarginPx/chartRightMarginPx`), same virtual-chainage table (`virtualChainageTable()` pushed via `syncTracksAxes()` after every `setPath`/`setSeries`), same linear map. Instrument `pixelForVirtualX` vs `ProfilePlotWidget::dataToPixel(...).x()` for one node: any delta means a missed `syncTracksAxes()` call site (terrain toggle re-`setPath` is the sneaky one) or a range the sync didn't propagate — check that `visibleXRangeChanged` fired (it dedupes; NaN sentinels guarantee the first).

## 7. Reporting

PASS/FAIL per §1 and §3.1–3.11 with observed behavior; for compile fixes, list file/line and what was changed so the implementing agent's assumptions can be corrected.
