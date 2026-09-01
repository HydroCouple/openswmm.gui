# Object Creation Defaults — Plan (2026-08-03)

## Problem

Newly drawn objects carry engine zeros for every hydraulic property. A fresh
conduit has 0 diameter, 0 roughness, 0 length (unless auto-length); a junction
has 0 max depth; a subcatchment has 0 width/slope/%imperv (engine seeds only
`n_imperv=0.013`, `n_perv=0.1`). A modeler who forgets to edit even one field
gets a silently wrong simulation instead of an obviously incomplete model.

Goal: per-object-type creation defaults, configurable in Preferences, shipped
with conservative engineering values, applied by every creation path (draw
tools, GIS import, undo-of-delete excluded — see Integration).

Decisions recorded from the 2026-08-03 session:
- **All 11 object types** (junction, outfall, storage, divider, conduit, pump,
  orifice, weir, outlet, subcatchment, rain gage).
- **Conservative engineering values** (table below), not EPA SWMM5's classic
  Project Defaults numbers.
- **GIS import included** — unmapped fields get defaults; mapped attributes win.
- Virtual junctions excluded: they inherit from the split conduit by design.

---

## Shipped defaults table (PENDING USER SIGN-OFF)

Two parallel sets keyed by unit system (US customary / SI), stored verbatim in
project display units — no 0.3048-style converted numbers in the UI. Weir Cd
and outlet rating C are unit-system-*semantic*, which is why parallel sets are
required rather than a single converted set.

### Nodes

| Property | US | SI | Rationale |
|---|---|---|---|
| Junction max depth | 0 (= auto: highest connecting crown) | 0 | Engine semantic: 0 means computed from crown — the safe value IS zero |
| Junction initial / surcharge depth, ponded area | 0 / 0 / 0 | 0 / 0 / 0 | Exposed for users who model ponding |
| Outfall type | FREE | FREE | No stage data needed; never blocks flow |
| Outfall flap gate | NO | NO | |
| Storage max depth | 15 ft | 4.5 m | Nonzero — a 0-depth storage node is always wrong |
| Storage shape | FUNCTIONAL, A = 0·d^0 + 1000 | A = 0·d^0 + 100 | Constant area 1000 ft² / 100 m² |
| Storage seepage | 0 | 0 | |
| Divider type | OVERFLOW | OVERFLOW | Simplest physically-consistent diversion; no cutoff/table params to forget |

### Links

| Property | US | SI | Rationale |
|---|---|---|---|
| Conduit shape | CIRCULAR | CIRCULAR | |
| Conduit diameter (geom1) | 1.0 ft | 0.3 m | Common minimum storm pipe |
| Conduit roughness | 0.013 | 0.013 | Concrete; conservative vs EPA's 0.01 |
| Conduit length | 400 ft | 120 m | Applied ONLY when auto-length is off |
| Conduit barrels / losses / flap | 1 / 0 / NO | 1 / 0 / NO | |
| Pump | ideal (no curve), init ON | same | Ideal pump passes inflow — runs stably without a curve |
| Pump startup / shutoff depth | 0 / 0 | 0 / 0 | |
| Orifice type | SIDE | SIDE | |
| Orifice shape / diameter | CIRCULAR, 1.0 ft | CIRCULAR, 0.3 m | |
| Orifice Cd | 0.65 | 0.65 | Standard sharp-edged |
| Weir type | TRANSVERSE | TRANSVERSE | |
| Weir height (geom1) / length (geom2) | 3.0 ft / 3.0 ft | 1.0 m / 1.0 m | |
| Weir Cd | 3.33 | 1.84 | Unit-semantic (ft½/s vs m½/s basis) |
| Weir end contractions / flap | 0 / NO | 0 / NO | |
| Outlet rating | FUNCTIONAL / DEPTH, Q = C·h^n | same | |
| Outlet C / n | 10.0 / 0.5 | 0.5 / 0.5 | C is unit-semantic |

### Subcatchments

| Property | US | SI | Rationale |
|---|---|---|---|
| Area | 5 ac | 2 ha | Applied ONLY when auto-area is off |
| Width | 500 ft | 150 m | |
| Slope | 0.5 % | 0.5 % | |
| % Imperv | 25 | 25 | |
| N-imperv / N-perv | 0.012 / 0.15 | 0.012 / 0.15 | N-perv 0.15 (dense grass) over engine's 0.1 |
| Dstore-imperv / Dstore-perv | 0.06 in / 0.15 in | 1.5 mm / 3.8 mm | ASCE typical, nonzero |
| % zero-imperv | 25 | 25 | |
| Horton f0 / fmin / decay / dry time | 3.0 in/hr / 0.5 in/hr / 4 hr⁻¹ / 7 d | 76 mm/hr / 13 mm/hr / 4 hr⁻¹ / 7 d | Applied per the PROJECT's infiltration model |
| Green-Ampt suction / Ksat / IMD | 3.5 in / 0.5 in/hr / 0.26 | 89 mm / 13 mm/hr / 0.26 | Silt-loam class |
| Curve number / dry time | 80 / 7 d | 80 / 7 d | |

### Rain gages

| Property | US | SI |
|---|---|---|
| Rain format | INTENSITY | INTENSITY |
| Interval | 0:15 | 0:15 |
| Snow catch factor | 1.0 | 1.0 |
| Data source | TIMESERIES (unassigned) | same |

---

## Architecture (mirrors the TwoDDefaults precedent throughout)

### Phase 1 — `PreferencesManager::ObjectDefaults`

`include/core/preferencesmanager.h`, next to `SimulationDefaults` (:383) and
`TwoDDefaults` (:450):

- Nested POD `ObjectDefaults` holding per-type sub-structs (`Junction`,
  `Outfall`, `Storage`, `Divider`, `Conduit`, `Pump`, `Orifice`, `Weir`,
  `Outlet`, `Subcatchment`, `RainGage`).
- Because US and SI seed values differ, in-class member initializers cannot be
  the single source of truth for both. Deviation from the bundle convention,
  stated explicitly: static factories `ObjectDefaults::usSeed()` /
  `ObjectDefaults::siSeed()` carry the table above as literals; the getter
  seeds from the factory for the requested system, then overlays QSettings.
- Accessors: `objectDefaults(bool si)` / `setObjectDefaults(const ObjectDefaults&, bool si)`.
- Keys: `SWMMVis/Preferences/ObjectDefaults/<US|SI>/<Type>/<prop>`, one
  `readObjDefault<T>` template mirroring `readTwoDSetting<T>`
  (preferencesmanager.cpp:1521-1527).
- `emit preferenceChanged("Defaults", "ObjectDefaults")` on set.

Gate: `tests/gui/test_object_defaults_prefs.cpp` round-trips both sets
(modelled on test_twod_defaults_prefs.cpp) — green.

### Phase 2 — Preferences page

`src/ui/dialogs/preferencesdialog.cpp`, the five-touchpoint pattern
(build / register at :84-97 next to "Simulation Defaults" / load in
`readFromManager` / save in `writeToManager` / reset in `onResetToDefaults`):

- `buildObjectDefaultsPage()`: top strip = unit-system selector
  (`QComboBox` US/SI, initialised from `UnitSystem::instance()->isSI()` when a
  project is open); body = `QTabWidget` with tabs Nodes / Links /
  Subcatchments / Rain Gages, `QGroupBox` + `QFormLayout` per type.
  Switching the selector re-loads widgets from the other set (both sets are
  edited in one dialog session; Apply writes both).
- Intro label mirrors :544-549: "Applied to newly created objects. Existing
  objects are not modified."
- Reset block duplicates factory literals with the same keep-in-sync comment
  as :1785-1788.

Gate: page opens, edits persist across dialog reopen, Reset restores table
values. (Manual — dialog not constructible headless is FALSE here: other
dialog tests exist; add construction smoke to the Phase 1 test if cheap.)

### Phase 3 — Apply at creation (`src/map/mapundostack.cpp`)

**Commands, not tools, not `applyXxxAdd`.** `applyXxxAdd` is the replay
primitive for `DeleteObjectCommand::undo` (:818-882) and must stay
defaults-free or delete→undo becomes lossy. Tools miss the GIS-import and
virtual-junction paths. `Add*Command::redo()` is the shared choke point and
already hosts policy (auto-length :559-567, auto-area + `UnitSystem` :629-641,
terrain inverts :425-430).

Ordering inside each `redo()` — defaults first, geometry-derived values win:

```
applyXxxAdd → applyObjectDefaults(type) → auto-length/auto-area → terrain invert/offsets
```

- One free function `applyObjectDefaults(SWMM_Engine, int idx, kind, const ObjectDefaults&)`
  (new `src/map/objectdefaultsapplier.{h,cpp}` or static in mapundostack.cpp —
  decide by size; likely its own file since GIS import shares it).
- Writes via direct engine setters (all verified present in
  openswmm.engine/include/openswmm/engine/): `swmm_node_set_max_depth`,
  `swmm_node_set_storage_functional`, `swmm_node_set_outfall_type`,
  `swmm_node_set_divider_type`, `swmm_link_set_xsect`,
  `swmm_link_set_roughness`, `swmm_link_set_length`,
  `swmm_link_set_discharge_coeff`, `swmm_link_set_weir_type`,
  `swmm_link_set_orifice_type`, `swmm_link_set_outlet_rating_type`,
  `swmm_link_set_outlet_expon`, `swmm_link_set_pump_init_state`,
  `swmm_subcatch_set_width/slope/imperv_pct/n_imperv/n_perv/ds_imperv/ds_perv`,
  `swmm_subcatch_set_infil_horton/green_ampt/curve_number`,
  `swmm_gage_set_rain_type/rain_interval/snow_factor`.
- Unit set chosen by `UnitSystem::instance()->isSI()` (proven available in the
  command layer, :637).
- Subcatchment infiltration: write only the parameter family matching the
  project's INFILTRATION option.
- Conduit length default skipped when `canvas->property("autoLength")` is on;
  subcatchment area default skipped when auto-area on (existing flags).
- Undo: `rollbackTail*Add` pops the whole engine row — no new bookkeeping.

Gate: creation-path test — headless layer + command: drawn conduit lands with
configured diameter/roughness under both CFS and CMS projects; drawn storage
node has nonzero depth; delete→undo of an *edited* object restores edited
values, not defaults.

### Phase 4 — GIS import (`src/ui/dialogs/import/featurelayerimporter.cpp`)

RESOLVED DURING IMPLEMENTATION (2026-08-03): the importer creates objects by
pushing the same `Add*Command`s that Phase 3 instrumented, so imported
objects receive defaults with **zero importer changes**; the importer's
`SetAdapterPropertiesCommand` runs after the add, so mapped attributes win.
The planned `kKindCatch` adapter branch was NOT added: `TargetKind` has no
subcatchment entry — the importer cannot create subcatchments at all — so
the branch would have no consumer (CLAUDE.md §3). Recorded as a follow-up
for whenever subcatchment import lands.

Gate: import test or manual smoke — imported node with only geometry mapped
gets defaults; imported node with max-depth mapped keeps the mapped value.

### Phase 5 — Verification

1. Full `ctest -L gui` green.
2. Manual: set a distinctive conduit diameter in Preferences, draw a conduit,
   check the property panel; flip project to SI, repeat.
3. CHANGELOG at release only (§5.2). Conventional commits; never push.

## Deliberately NOT doing

- Naming-page merge (ID prefixes into this page) — SWMM5 co-locates them, but
  it is a pure UI move; separate change if wanted. The two hardcoded prefixes
  (`maptooladdsubcatchment.cpp:54` "Sub", `maptooladdgage.cpp:35` "RG"
  bypassing the Naming preference) are a pre-existing defect — reported, not
  fixed here.
- Pump/orifice/weir curve *references* — defaults can't invent curves.
- Retroactive application to existing objects.
