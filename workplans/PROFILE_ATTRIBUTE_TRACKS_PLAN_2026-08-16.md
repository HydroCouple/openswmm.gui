# Profile Attribute Tracks — Synced Attribute Charts Below the Profile Plot

**Date:** 2026-08-16
**Status:** Proposed
**Decisions (user-confirmed):** stacked sub-panes ("tracks", one per attribute, own y-axis each) · animated with the simulation clock + toggleable min/max envelope · custom QPainter widget (not Qt Charts)
**Relates to:** `workplans/LIVE_1D_RESULTS_GUI_PLAN_2026-08-04.md` (short-series contract), `workplans/2D_PROFILE_WSE_EXTRAPOLATION_PLAN_2026-08-02.md` (fetch-once / index-per-frame), `workplans/DIALOG_WINDOW_MANAGEMENT_PLAN_2026-08-16.md` (owns the dialog's window flags — do not disturb)

## Feature

A chart area at the bottom of `ProfilePlotDialog` showing along-the-path profiles of additional result attributes for the path's nodes or links — depth, head, volume, inflow, overflow (nodes); flow, depth, velocity, volume, capacity (links). Each selected attribute renders as its own thin "track" with an independent y-axis; all tracks share the profile plot's x-axis (station along the path), stay aligned during zoom/pan, animate with the simulation clock, and are user-styleable through the existing Display Options property tree.

## Architecture (MVC, per CLAUDE.md §5.1 and existing conventions)

| Layer | New file(s) | Role |
|---|---|---|
| Model (pure) | `include/plot/profileattributesampler.h`, `src/plot/profileattributesampler.cpp` | Fetch per-attribute `[pathIdx][period]` arrays from a results layer for a `PathStatic`; no Qt Widgets deps; unit-testable |
| Options (QObject) | `include/plot/profileattributetrackoptions.h`, `src/plot/profileattributetrackoptions.cpp` | `Q_PROPERTY`-based styling + visibility, single `changed()` signal — same shape as `ProfilePlotOptions` |
| View | `include/plot/profileattributetrackswidget.h`, `src/plot/profileattributetrackswidget.cpp` | QPainter widget stacking N tracks, sharing the profile's x mapping |
| Controller (glue only) | edits to `src/ui/dialogs/profileplotdialog.cpp` | splitter, attribute menu, rebind + animation wiring |

Files register manually in `CMakeLists.txt` (plot headers ~line 312-331, plot sources ~927-946).

The same data (attribute selection, styles) is edited from two UIs — the toolbar attribute menu and the Display Options tree — so both must go through `ProfileAttributeTrackOptions` and react to its `changed()` signal; neither writes widget state directly.

---

## Phase 1 — X-axis sync plumbing in `ProfilePlotWidget`

The prerequisite everything else stands on. `ProfilePlotWidget` (src/plot/profileplotwidget.cpp) currently has no signal when its view changes and no setter for its x-range.

**1a. Expose the shared x model.** The synced quantity is **virtual chainage**, not real chainage — zero-length links (pumps/weirs/orifices) occupy a synthetic gap (`kZeroLengthGapFrac`, profileplotwidget.cpp:44), and syncing real chainage would misalign every path containing one. Add to the public API:

- `double virtualXForNode(int nodeIdx) const` and `double virtualXAlongLink(int linkIdx, double frac) const` (promote the existing private helpers at :695/:729).
- `QPair<double,double> visibleVirtualXRange() const` (derived from `m_dataXMin/Max`).
- `int leftMarginPx() const` / `int rightMarginPx() const` returning `kMarginLeft`/`kMarginRight` (:33-37, currently file-private) so the tracks widget can reproduce the identical horizontal pixel mapping. Promote the constants to the header or expose via these accessors — do not duplicate the numbers.
- `double virtualToRealChainage(double vx) const` made public for tick labeling in the tracks (labels must show REAL station like the profile's x ticks do, :744-766).

**1b. Range-change signal.** `signals: void visibleXRangeChanged(double vxMin, double vxMax);` emitted from every x-mutation site: `fitToExtent()` :772, `zoomBy()` :780, `setAxisEdgeValue()` :838, pan in `mouseMoveEvent` :1310-1322, zoom in `mouseReleaseEvent` :1371-1424, `wheelEvent` :1439-1464, and `recomputeBounds()` when `m_fitMode` (reached from `setPath`/`setSeries`). Emit only when the values actually changed.

**1c. Range setter for the reverse direction.** `void setVisibleXRange(double vxMin, double vxMax);` — pan/zoom initiated inside a track pushes back up. Guard both directions with a `m_syncingX` re-entrancy flag, the house idiom from `ComparisonPlotDialog::wireXAxisSync` (comparisonplotdialog.cpp:666-684).

→ verify: unit-test the mapping helpers (pure math, no window needed offscreen); manual: zoom/pan the profile and confirm the signal fires once per gesture (qDebug counter), no recursion.

## Phase 2 — Attribute sampling (model layer)

**2a. `ProfileAttributeSampler`.** `ProfileSourceFetcher::fetch` (src/plot/profilesourcefetcher.cpp:57-90) hard-codes three engine variables (node head, node depth, link velocity). Add alongside it (do not disturb the existing fetch — surgical-changes rule):

```cpp
struct AttributeProfile {                 // one attribute × one source
    openswmmvis::plot::PlotAttribute attribute;
    bool  isNodeAttribute;                // nodes: value per nodes[i]; links: per links[i]
    QVector<QVector<float>> byPath;       // [pathIdx][period] — ragged/short rows PERMITTED
    QVector<float> minByPath, maxByPath;  // envelope, computed once at fetch
};
AttributeProfile fetchAttributeProfile(SWMMResultsLayer *layer,
                                       const ProfileBuilder::PathStatic &path,
                                       openswmmvis::plot::PlotAttribute attr);
```

Engine variable codes come from the existing `SwmmOutRunLayer::variableCodeFor(attr, kind)` switch (src/plot/swmmoutrunlayer_codes.cpp:19-71) — reuse, don't re-tabulate. Loop `swmm_output_get_node_series` / `..._link_series` per path element, mirroring `fillNodeSeries`/`fillLinkSeries` in the existing fetcher. Per the LIVE_1D plan contract, short series (fewer periods than the longest source) are accepted and simply render fewer frames.

**2b. Caching.** Extend the dialog's per-layer cache pattern: key `QHash<QPair<SWMMResultsLayer*, PlotAttribute>, std::shared_ptr<const AttributeProfile>>`, invalidated by the exact same signals `ensureCacheInvalidationWired()` already handles (profileplotdialog.cpp:1188-1208): `resultsOpened`, `resultsFilePathChanged`, `destroyed`. Fetch once per (layer, attribute); animation only indexes `byPath[i][period]` — the fetch-once/index-per-frame split proven by the 2D profile plan.

→ verify: unit test against a bundled example .out file — correct array shapes (`nodes.size()` vs `links.size()`), envelope = elementwise min/max, graceful empty result for an attribute the file lacks.

## Phase 3 — Options object (styling + selection state)

**3a. `ProfileAttributeTrackOptions`** (`QObject`, one `changed()` signal, same shape as `ProfilePlotOptions`):

- Per-attribute (all 11 from `nodePlotAttributes()` + `linkPlotAttributes()`, plotattribute.h:38-94): `bool <attr>Visible` (default false), `QPen <attr>Pen` (seeded from a shared color cycle), `QBrush <attr>EnvelopeBrush`, `bool <attr>EnvelopeVisible` (default true).
- Track chrome: `int trackHeightPx` (default ~110, min 60), `bool showTrackTitles`, `bool showValueAtCursor` (numeric readout of the value under the animation cursor), y-axis number format (`AxisNumberFormat` enum + printf override, mirroring `ProfilePlotOptions::yAxisNumberFormat`), `bool autoScaleY` (per-visible-range) vs full-extent y.
- `Q_INVOKABLE QString displayLabelFor(propertyName)` for friendly rows in the property tree, like profileplotoptions.h:184.

Rather than 44 hand-written properties, group as one `Q_GADGET`-free map keyed by `PlotAttribute` exposed through `Q_PROPERTY` per attribute — decide at implementation time; the constraint is only that `QPropertyModel` can edit it in a tree and every mutation emits `changed()`.

**3b. Display Options integration.** Add a root/section for the tracks options in `ProfileOptionsDialog`'s Display tab (`buildDisplayTab()`, profileoptionsdialog.cpp:109) — the `QTreeView` + `QPropertyModel` + `QPropertyItemDelegate` machinery makes any `Q_PROPERTY` object user-editable with zero new editor code. This is what "styleable" costs: one options object done properly.

**3c. Persistence.** Style + selection persist under the existing profile settings group (`ProfilePlot/…`, alongside the source styles written at profileplotdialog.cpp:878-904) so a reopened profile dialog remembers which tracks were on and how they looked. Per-source pens are out of scope for v1 (all sources share the attribute's pen, distinguished by the source's `profileLineColor` if multiple sources are checked — see Phase 4c).

→ verify: toggle/edit properties in Display Options; `changed()` fires once per edit; settings round-trip across dialog close/reopen.

## Phase 4 — `ProfileAttributeTracksWidget` (view)

**4a. Rendering.** One QWidget painting N stacked tracks (N = visible attributes, in a fixed canonical order: node attrs then link attrs). Per track:

- Plot rect uses the profile's `leftMarginPx()`/`rightMarginPx()` for x; fixed `trackHeightPx` per track; widget's `sizeHint` = N × trackHeight + axis strip. Vertical scrollbar via host `QScrollArea` if N tracks exceed the pane (rare; 11 max).
- X mapping: identical linear map from the synced `(vxMin, vxMax)` virtual-chainage range to the shared pixel columns. **Node attributes** plot a point at each node's `virtualXForNode(i)` connected by straight segments; **link attributes** plot a horizontal segment per link spanning `virtualXAlongLink(i, 0) → virtualXAlongLink(i, 1)` (step style — a link's flow/velocity is one value along its length; interpolating across nodes would invent data).
- Per track: envelope band (min/max fill) under the current-time polyline; left y-axis with 3-4 ticks, auto-scaled per options; track title ("Velocity (ft/s)" via `labelWithUnits(attr, UnitSystem)`); optional cursor readout.
- Bottom-most track draws the shared x tick labels (real chainage via `virtualToRealChainage`) so the column of tracks reads as one instrument; intermediate tracks draw gridlines only.
- Theme chrome from `ThemeManager` tokens (`plotBackground`/`plotAxis`/`plotGrid`), matching profileplotwidget.cpp:67-70. Data pens/brushes come exclusively from the options object.

**4b. Interaction.** Wheel-zoom and drag-pan in x forward to `setVisibleXRange` upstream (guarded, Phase 1c); no y interaction in v1 beyond auto-scale. Right-click → context menu with the attribute toggles (same actions as the toolbar menu, Phase 5a).

**4c. Multi-source.** When multiple result sources are checked in the Sources menu, each track draws one polyline per source, tinted by that source's existing `profileLineColor` (swmmresultslayer.h) with the attribute pen's width/style. Envelope shown for the primary source only (visual noise otherwise).

**4d. Animation.** `setCurrentPeriod(int)` slot indexing the cached arrays — no fetching per frame. Also draw the dashed current-x time marker if the profile shows one, for visual continuity.

→ verify: offscreen paint test — construct widget with a synthetic 3-node/2-link path and fabricated arrays, render to QImage, assert non-empty painting within expected rects and that node-attr x positions equal the profile widget's `virtualXForNode` values (pixel-exact alignment test, the core promise of the feature).

## Phase 5 — Dialog integration (controller)

**5a. Layout — collapsible pane.** In `buildLayout()` (profileplotdialog.cpp:191-687): replace the bare `m_plot` in the centre layout with a `QSplitter(Qt::Vertical)` named `"profileSplit"` containing `m_plot` (stretch 3) and the tracks pane (stretch 1). The name is load-bearing: `DialogLayoutWatcher` persists named splitter state automatically under `Dialogs/ProfilePlotDialog/splitter/profileSplit` — zero persistence code (dialoglayoutpersistence.cpp:176-182), and `QSplitter::saveState()` includes the collapsed/expanded state, so the pane reopens how the user left it.

The pane is collapsible three ways, all kept in sync through the master toggle action (5b), never by writing widget state directly:

- **Drag-to-collapse:** `setCollapsible(1, true)` on the tracks index — dragging the handle to the bottom collapses it; dragging back restores. `m_plot` stays non-collapsible (`setCollapsible(0, false)`).
- **One-click toggle:** the checkable toolbar action from 5b collapses/expands the pane (collapse = `setSizes({total, 0})`; expand = restore last non-zero sizes, remembered in a member, defaulting to the 3:1 split). Listening to `QSplitter::splitterMoved` keeps the action's checked state true to a manual drag-collapse, so the two affordances never disagree.
- **Auto-hide:** with no attribute checked the pane (and the splitter handle) is hidden entirely — the dialog looks exactly as today; zero regression for users who never touch the feature. Checking a first attribute shows and expands it.

**5b. Attribute selection UI.** Toolbar `QToolButton` + checkable `QMenu` ("Attribute Tracks ▾") listing the 11 attributes grouped Node/Link, checked state bound to the options object. A checkable QAction named `"showAttributeTracks"` is the master collapse/expand toggle from 5a; naming it gets its checked state persisted free via the toggle group of `saveDialogLayout` — the ComparisonPlotDialog precedent (comparisonplotdialog.cpp:370).

**5c. Data wiring.** Extend `rebindSources()` (profileplotdialog.cpp:980-1180): the existing `QtConcurrent::run` job additionally fetches `AttributeProfile`s for checked attributes on cache miss (same worker, same `m_loadCookie` stale-guard, same `QFutureWatcher` merge). On completion push arrays to the tracks widget. Attribute toggled on later → same pipeline, fetch just the missing (layer, attribute) pairs.

**5d. Sync wiring.** `connect(m_plot, &ProfilePlotWidget::visibleXRangeChanged, tracks, &...::setVisibleXRange)` and the reverse, both through the `m_syncingX` guard. `fitToExtent` (toolbar Fit) naturally propagates through the signal.

**5e. Animation wiring.** `onAnimationTimeChanged` (profileplotdialog.cpp:1325-1333) additionally calls `tracks->setCurrentPeriod(period)` — one line.

**5f. Export PNG.** Widen the export (profileplotdialog.cpp:457-466) to render the splitter contents (profile + visible tracks) into one image.

→ verify: existing `test_profile_*` GUI tests still pass (layout change must not break node/link click routing); new GUI test: open dialog offscreen, toggle two attributes, assert splitter child count; collapse via the action, assert `sizes()[1] == 0` and action unchecked stays consistent after a simulated `splitterMoved`; toggle all attributes off, assert pane hides.

## Phase 6 — Tests, docs, changelog

- Unit: sampler shapes/envelopes (Phase 2), virtual-x mapping helpers (Phase 1), options round-trip (Phase 3).
- GUI (offscreen): pixel-alignment test (Phase 4), dialog integration test (Phase 5), persistence of splitter + toggle state through the existing `test_dialog_layout_persistence` conventions.
- Manual (needs display): zoom/pan sync feel in both directions, animation playback smoothness with 4+ tracks on a large .out file, Display Options styling live-updates, multi-source tinting.
- `CHANGELOG.md` under `[Unreleased] / Added` upon release (CLAUDE.md §5.2).

## Explicit non-goals (v1)

- Subcatchment attributes (no geometric meaning along a node-link path).
- Per-source pen overrides per attribute (source tint + shared pen only).
- Y-axis interactive zoom in tracks; cross-hair cursor synchronized in y.
- Qt Charts anywhere in this feature.
- Reordering tracks by drag; order is canonical.

## Risks & mitigations

- **Pixel misalignment** (the feature's core promise): mitigated by sharing margins and the virtual-chainage mapping through accessors rather than duplicated constants, plus the Phase 4 alignment test. If `ProfilePlotWidget` margins ever become dynamic, the accessors keep the tracks correct automatically.
- **rebindSources complexity creep:** the function is already ~200 lines; keep the attribute fetch a self-contained helper called from the job lambda, not inlined.
- **Sync feedback loops:** both directions guarded by one flag; the ComparisonPlotDialog idiom is proven in-tree.
- **Performance with 11 tracks × large files:** fetch-once/index-per-frame bounds per-frame cost to painting; envelope precomputed at fetch. If fetch time grows, per-attribute fetches are independent and can be parallelized inside the existing worker.

## Execution order

```
1. Phase 1 (widget API + signal)        → unit tests, no visible change
2. Phase 2 (sampler + cache)            → unit tests, no visible change
3. Phase 3 (options object + tree tab)  → visible in Display Options only
4. Phase 4 (tracks widget)              → offscreen paint/alignment tests
5. Phase 5 (dialog integration)         → feature visible end-to-end
6. Phase 6 (tests/docs/changelog)
```

Each step compiles and passes tests independently; the feature is invisible to users until Phase 5 lands.
