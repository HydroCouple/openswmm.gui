# Sources and fluxes in the GUI — Strategy (2026-09-01)

**What this covers.** Every editor whose job is *"which elements get which
value for which quantity"* — heat sources, water-age sources, radiative and
bed attributes (now per-element, PE2), transport boundaries and sources,
runtime forcing, and the 2D surface sources and boundary conditions when
overland transport lands.

**Why a strategy and not just another dialog.** Two of these editors already
exist and they are already two copies of one shape. The next four would make
six, and the sixth arrives at the same time as a requirement none of them
currently meets.

---

## 1. What exists, measured

| Editor | File | Lines | Storage |
|---|---|---|---|
| Water Age Sources (G3g) | `wateragesourcesdialog.cpp` | 312 | `QTableWidget` |
| Heat Configuration (G4g) | `heatconfigdialog.cpp` | 735 | `QTableWidget` |
| Reaction System (G2g) | `reactionsystemeditordialog.cpp` | 1150 | mixed |
| Initial Quality | `initialqualitydialog.cpp` | 371 | `QTableWidget` |

Both source editors independently define:

```cpp
struct SourceRow { int code; const char *label; };
const SourceRow kSources[] = { ... };
```

…then build a **global** `QTableWidget` and an **override** `QTableWidget`,
load through the C API, and apply back under a `writeIfChanged` discipline.
That is the same program written twice.

Meanwhile the repo *does* have proper models —
`src/ui/models/transectstationtablemodel.cpp`,
`src/ui/panels/timeseriestablemodel.cpp`,
`qualityfunctiontablemodels.cpp` — so the pattern is present and simply was
not used for these dialogs.

## 2. The problem that decides the strategy

`CLAUDE.md` §5.1:

> Generally I want to utilize model view controller architecture model as the
> same data may be edited via different ui but must remain synchronized.

**`QTableWidget` cannot satisfy that**, because it is a view and a store in
one. A heat source edited in the dialog does not exist anywhere the map, the
attribute table, or a second dialog can see until OK is pressed. Today that
is invisible because each quantity has exactly one editor. It stops being
invisible the moment per-element attributes land:

- PE2 puts `SHADE_FACTOR` on **links**. A user will expect to set it from the
  link property panel and from a multi-select on the map, not only from a
  modal heat dialog.
- PE2's `TAG` scope means editing `[TAGS]` **changes which elements an
  override applies to**. Two editors, one truth, no signal between them.
- PE4's per-element climate is runtime state a scenario panel will want to
  show live, while the dialog shows configuration.

So the strategy is not "make the next dialog nicer". It is: **give these
quantities a model, once, and let every view attach to it.**

## 3. The shape they all share

Every one of these editors, current and planned, is a list of rows of:

```
<quantity>  <scope>  [element or tag]  <mode>  <value | timeseries>
```

| Editor | quantity | scope | mode |
|---|---|---|---|
| `[HEAT_SOURCES]` | 7 source pathways | GLOBAL, NODE | value |
| `[WATER_AGE_SOURCES]` | 7 source pathways | GLOBAL, NODE | value |
| `[RADIATIVE_FLUXES]` (PE2) | 6 attributes | GLOBAL, TAG, LINK, NODE | value |
| `[SEDIMENT_EXCHANGE]` (PE2) | 8 attributes | GLOBAL, TAG, LINK | value, timeseries |
| `[TRANSPORT_BOUNDARIES]` | per species | NODE | value, timeseries |
| `[TRANSPORT_SOURCES]` | per species | LINK | value, timeseries |
| `[HEAT_FLUXES]` | 5 module toggles | GLOBAL | on/off |
| 2D sources / BCs (future) | per species | CELL, EDGE, GROUP | value, timeseries |
| Runtime forcing (PE4) | 4 climate + N others | LINK, NODE | OVERRIDE/ADD, RESET/PERSIST |

**One row type covers all of it.** That is not a coincidence — the engine
converged on the same shape independently: `HeatOverrideRow{attr, scope,
name, value}` with a single `kAttrTable` driving the parser, the resolver and
the serializer, pinned by `static_assert` against `HeatAttr::COUNT_`.

## 4. Strategy

### S-GUI-1 — one descriptor, mirroring the engine's

```cpp
// src/ui/models/scopedvaluedescriptor.h
struct ScopedQuantity {
    int          code;           // engine enum value (HeatAttr, HeatSource, …)
    QString      key;            // deck spelling, for tooltips and errors
    QString      label;          // display
    QString      units;          // display; the model does no conversion
    ScopeMask    scopes;         // GLOBAL|TAG|LINK|NODE|CELL2D
    ValueKind    kind;           // Double, Fraction, Toggle, TimeseriesOrValue
    double       lo = -inf, hi = +inf;   // mirrors the engine validator
    bool         has_flag = false;       // "configured" vs "defaulted"
};

struct ScopedTableSpec {
    QString                  section;    // "[RADIATIVE_FLUXES]"
    QVector<ScopedQuantity>  quantities;
    // Adapters — the ONLY place C API calls live.
    std::function<int(SWMM_Engine, int code, int scope, const char* name, double* v)> get;
    std::function<int(SWMM_Engine, int code, int scope, const char* name, double  v)> set;
    std::function<int(SWMM_Engine, int* count)> rowCount;
};
```

**The descriptor is a translation of the engine's own table, not a second
source of truth.** Where the engine has `kAttrTable`, the GUI has a
`ScopedTableSpec` built from the same key strings. Ranges are *mirrored*, and
the mirror is documented as a convenience: **the engine's validator remains
authoritative** and the dialog surfaces its refusal rather than pre-empting
it. A GUI that silently clamps what the parser would refuse is how a deck
comes to differ from what the user saw.

### S-GUI-2 — one `QAbstractTableModel`, `ScopedValueTableModel`

Backed by the engine through the descriptor's adapters. Emits
`dataChanged` on edit, `modelReset` on reload. **This is the whole of the MVC
requirement**: a dialog, a property panel and a map multi-select all attach
to the *same* model instance and see each other's edits immediately.

Two sub-views come free:
- **Global view** — one row per quantity, filtered to `GLOBAL` scope. This is
  what today's two dialogs' upper tables are.
- **Override view** — the sparse rows, with scope and target columns. This is
  the lower table, generalised from NODE-only to the full scope set.

### S-GUI-3 — scope editing is a delegate, not four dialogs

`ScopeDelegate` renders the scope column as a combo (GLOBAL / TAG / LINK /
NODE), and the *target* column swaps its editor accordingly:

- `TAG` → a combo populated from the model's distinct `[TAGS]` values;
- `LINK`/`NODE` → the existing `dataobjectpickereditor`, which already exists
  and already knows how to pick network objects;
- `GLOBAL` → the target cell is disabled and blank.

**The scope combo is filtered by the quantity's `scopes` mask**, so the GUI
cannot offer `NODE` for a bed attribute — which the engine refuses by name
(`"the bed zone exists beneath CONDUITS only"`). An option a user can select
and the engine then rejects is worse than an option that was never offered.

### S-GUI-4 — precedence is *shown*, not just stored

The override view gains a computed, read-only **"Effective"** column: for the
selected element, what value actually applies after `GLOBAL < TAG < element`
resolution.

This is the single highest-value thing in this strategy. PE2's precedence is
correct and invisible; a user who sets `SHADE_FACTOR TAG RIPARIAN 0.85` and
then `LINK C7 0.95` has no way to see that C7 ignores the tag except by
reasoning about a rule. **Show the resolved value and the rule stops being
folklore.**

Cheapest correct implementation: read it back from the engine after apply
(the resolver already computed it) rather than re-implementing precedence in
the GUI. **A second implementation of precedence is a second thing that can
disagree** — which is the same argument that made the engine's resolver
order-based rather than comparison-based.

### S-GUI-5 — one dialog shell, N pages

`ScopedEditorDialog` takes a list of `ScopedTableSpec` and renders one tab
per section, with the global/override split inside each. Heat becomes four
tabs (sources, fluxes, radiative, sediment) of *the same widget*, not 735
lines of bespoke layout.

Existing dialogs migrate **one at a time**, and each migration must be
behaviour-preserving: same rows, same values, same `writeIfChanged`
semantics. The Water Age dialog is the right first migration — 312 lines, one
scope, and a shipped test to keep honest.

### S-GUI-6 — timeseries-or-value is one editor, everywhere

`[SEDIMENT_EXCHANGE] GROUND_TEMPERATURE`, `[TRANSPORT_BOUNDARIES]`, PE3's
per-element series and the 2D BCs all need "a constant **or** a named
series". One `ValueOrSeriesDelegate`: a mode combo plus either a spin box or
the existing timeseries picker.

**Note the engine gap this exposes:** `swmm_heat_get_shortwave_timeseries` /
`_get_cloud_timeseries` landed (`d868b2c3`) precisely because the GUI could
not read back a bound series *name*. Every quantity this delegate serves
needs the same getter, and **the audit for that belongs in this round, not in
the round that discovers a blank combo.**

## 5. Sequencing

| Step | Scope | Why here |
|---|---|---|
| **F1** | `ScopedQuantity` / `ScopedTableSpec` / `ScopedValueTableModel` + unit tests against a stub adapter | No UI, no engine — the model is testable alone |
| **F2** | Migrate Water Age Sources onto it, behaviour-preserving | Smallest editor, existing test, proves the shape |
| **F3** | `ScopeDelegate` + `ValueOrSeriesDelegate` + the Effective column | The three pieces PE2 needs |
| **F4** | Heat: migrate G4g's four tabs; add the PE2 per-element pages | The payoff — and the first editor that could not have been written the old way |
| **F5** | Transport boundaries/sources (G7g) | New editor, written on the shape rather than a sixth copy |
| **F6** | Runtime forcing panel (PE4) — live, non-modal, RESET/PERSIST visible | Different lifetime; see §6 |
| **F7** | 2D sources and BCs | After the overland engine work lands |

**F1–F3 are the investment; F4–F7 are each smaller than the dialog they
replace.** If only F1–F4 ever happen, the duplication is already retired.

## 6. ⚠ The distinction that must not be blurred

**Configuration and runtime forcing are different things and must look
different.**

- Configuration (`[HEAT_SOURCES]`, `[RADIATIVE_FLUXES]`, PE2 overrides) is
  **saved to the deck**, survives close/reopen, and is edited before a run.
- Runtime forcing (PE4's `swmm_forcing_element_climate`, every
  `swmm_forcing_*`) is **not saved**, applies to the running simulation, and
  most of it **auto-clears every step** under `RESET`.

A single table showing both would be actively misleading: the user would set
a per-link air temperature, save, reopen, and find it gone — with no error,
because nothing went wrong. So:

- Forcing lives in a **non-modal panel**, not the configuration dialog.
- It shows `RESET`/`PERSIST` **per row**, because that is the difference
  between a value that lasts one step and one that lasts the run.
- It is visually distinct (the codebase's existing "live/scenario" styling)
  and **disabled when no simulation is running**.

This is F6, and it is deliberately last: it is the one page where getting the
metaphor wrong teaches a user something false.

## 7. What this does not change

- **The engine stays authoritative.** Ranges, refusals, precedence and
  serialization are the engine's; the GUI mirrors and surfaces them.
- **`writeIfChanged` stays.** A dialog that opens and closes must not dirty
  the project. The model must therefore track a baseline, and `apply()` must
  diff against it rather than writing every row.
- **No new deck syntax.** This is a UI strategy; every quantity here already
  has a parser and a serializer, or will have one before its page is built.

## 8. Owed before F4

An audit of **read-back coverage**: for every quantity in §3's table, can the
GUI read back what it wrote — including a bound timeseries *name* and the
"configured vs defaulted" flag? `d868b2c3` closed two such gaps for
shortwave and cloud. PE2's rows, `[TRANSPORT_BOUNDARIES]` and
`[SEDIMENT_EXCHANGE]`'s ground-temperature series have not been checked.

**A missing getter is a blank combo that silently discards the user's
choice on the next save** — the exact shape of the step-3 data-loss finding,
one layer up.
