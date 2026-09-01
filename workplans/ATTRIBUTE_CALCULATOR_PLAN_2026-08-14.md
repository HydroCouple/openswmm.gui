# Attribute Table Field Calculator — Design Plan

**Date:** 2026-08-14
**Status:** DRAFT — for review, not yet approved for implementation
**Repos touched:** `openswmm.engine` (new public C API), `openswmm.gui` (dialog + model)
**Governing docs:** `CLAUDE.md` §1 (surface tradeoffs), §2 (simplicity), §4.1 (reviewable test IO),
§5.1 (MVC), §5.2 (CHANGELOG); `workplans/TRANSPORT_QUALITY_GUI_PLAN_2026-08-12.md:21`
(*"Engine validator is authoritative — no client-side parsing"*);
`workplans/UI_REDESIGN_ITER4_WORKPLAN.md` Phase 7 (expression-editor recipe);
`workplans/MESH_ATTRIBUTE_TABLES_PLAN_2026-08-04.md` (plan format + MVC precedent).

---

## 0. User-approved decisions (2026-08-14)

Recorded up front, per the MESH_ATTRIBUTE_TABLES_PLAN convention.

| # | Decision | Choice |
|---|---|---|
| D1 | Expression parse/eval location | **Expose engine `openswmm::mathexpr` via a new public C API.** No client-side arithmetic parser. |
| D2 | Output targets | **All three:** existing editable column, new `[USER_FLAGS]` field, preview-only virtual column. |
| D3 | Variable scope | **All three:** same-row attributes, simulation results, topology references. |
| D4 | Row scope | **All three:** all rows in category, selected rows only, rows matching a WHERE filter. |

### 0.1 Scope pushback (CLAUDE.md §2)

D2 + D3 together are roughly 4× the Phase-1 minimum, and two of the pieces carry
real architectural risk:

- **Topology variables (D3c)** need a reference resolver (link → upstream/downstream node,
  subcatchment → outlet) plus a naming scheme, and they make the variable table
  category-dependent in a second dimension. This is the single largest chunk of new code
  in the whole feature.
- **Preview-only virtual column (D2c)** is the only item that requires changing
  `SWMMAttributeTableModel`'s public shape (a transient non-engine-backed column).
  Everything else composes from existing seams.

**Recommendation:** ship the phases in §6 in order and re-review after Phase 1. Each phase
is independently useful and independently verifiable. Phases 4–5 (results + topology
variables) are additive to the variable-provider interface and cost nothing to defer.
If you want the whole thing in one drop, say so and I will collapse the phasing — but
the plan is written so you do not have to decide that now.

---

## 1. User-facing behavior

Entry points, both on `AttributeTablePanel`:
- Toolbar button **Field Calculator…** (enabled only for SWMM + mesh sources; disabled for
  the read-only GIS vector source).
- Column-header context menu → **Field Calculator…**, which pre-selects that column as the
  output target.

The dialog (modal, `SWMMCalculatorDialog`) presents:

1. **Output** — three exclusive modes:
   - *Update existing field*: combo of columns where `ColumnSpec::setter` is non-empty
     **and** `editor ∈ {Numeric, Integer}`. Enum/Text/Compound/Interval columns are excluded.
   - *Create new field*: name + type (REAL / INTEGER) + optional description. Creates a
     `[USER_FLAGS]` definition, which `appendUserFlagColumns()` then surfaces automatically.
   - *Preview only*: computes and displays without writing anything.
2. **Row scope** — exclusive radios: *All rows in category* / *Selected rows only (N)* /
   *Rows matching:* `<where clause>`. The selected-rows radio is disabled with a count of 0
   when nothing is selected. The where field reuses the existing query-bar grammar.
3. **Variables & functions** — a two-group tree. Double-click or Enter inserts at the cursor.
   Each leaf shows its value for the current row in a side label, so the user can sanity-check
   a name before committing to it.
4. **Expression** — syntax-highlighted editor with completion (Ctrl+Space, auto after 2 chars)
   and a debounced validity banner (● Valid / ⚠ message / Validating…), cloned from
   `TreatmentExpressionEdit`.
5. **Preview** — read-only table, first 200 rows in scope: `Name | Current | New | Status`.
   Refreshes on demand and on successful validation.
6. **Buttons** — `Preview`, `Apply`, `Close`. Apply is disabled while the expression is invalid.

On Apply, a single-line result summary is shown in the dialog status label and mirrored to the
status bar: *"Wrote 412 of 430 rows — 12 skipped (non-finite), 6 skipped (out of range)."*
The whole write is one undo macro: Ctrl+Z reverts it entirely.

### 1.1 Grammar available to the user

Whatever `openswmm::mathexpr` supports, and nothing more:

- Operators: `+ - * / ^`, unary minus, parentheses
- Functions: `abs sgn sqrt log log10 exp sin cos tan asin acos atan cot sinh cosh tanh coth acot step min max`
- Numeric literals and bound variable names

**Not available, and worth stating in the UI:** comparison operators, boolean operators,
and conditionals (`if(...)`). `mathexpr` has no tokens for them. Adding them would mean
editing an engine module shared by Controls, Treatment, and Groundwater — out of scope here.
The *Rows matching* filter covers the common conditional case (compute only where a condition
holds), and that is how the dialog should teach it.

### 1.2 Units

Verified during design: `SWMMAttributeTableModel` performs **no numeric unit conversion**.
`UnitKind` resolves to a header *label* only (`unitLabel()`, `swmmattributetablemodel.cpp:148`);
`data()` and `commitValueDirect()` pass engine-native values straight through.

Therefore the calculator inherits the table's existing convention: expressions operate on the
same numbers the table displays, and no conversion layer is needed. This must be stated in the
dialog's help text so nobody assumes otherwise. **If this ever changes, the calculator changes
with it** — add a note to that effect in the model header.

---

## 2. Engine work — new public expression API

### 2.1 Why

`openswmm::mathexpr` (`openswmm.engine/src/engine/math/MathExpr.hpp`) is a complete
shunting-yard parser + stack-machine evaluator, already unit-tested
(`tests/unit/engine/test_mathexpr.cpp`), already used by Controls, Treatment, and Groundwater.
It is not reachable from the GUI: it lives under `src/`, not `include/`, so it is not part of
the installed surface, and no `swmm_mathexpr_*` C API exists. The only expression entry point
today is `swmm_treatment_validate_expression`, which is bound to the treatment grammar
(requires an `R =` / `C =` LHS, fixed 8-variable table) and is not reusable.

Per D1 and the TRANSPORT_QUALITY rule, we expose mathexpr rather than write a second parser.

### 2.2 Gap to close first: `parse()` has no diagnostics

```cpp
int parse(const std::string& expr_str, Expression& result);   // returns 0 or -1. That's it.
```

An editor needs a message and a character offset. Precedent for how to add them:
`openswmm::treatment::validate(expr, msg, col)` at
`openswmm.engine/src/engine/quality/Treatment.cpp:346`, which runs its own validation pass
**cross-checked against `parse()`** so the verdict cannot drift from what the engine accepts.

**Add** to `MathExpr.hpp` / `.cpp`:

```cpp
/// Validate an infix expression. Variables not present in name_table are
/// reported as unknown. Cross-checked against parse() so the verdict cannot
/// drift. Returns 0 when valid, -1 otherwise (msg/col filled).
int validate(const std::string& expr_str,
             const std::vector<std::string>& var_names,
             std::string& msg, int& col);
```

No change to existing `parse` / `evaluate` signatures — Controls, Treatment, and Groundwater
are untouched.

### 2.3 New public header: `include/openswmm/engine/openswmm_expr.h`

Engine-handle-free: this is pure arithmetic over caller-supplied values, reads no model state.
(Note the deliberate departure from `swmm_treatment_validate_expression`, which takes an
`SWMM_Engine` it documents as unused. Flagged as **Q1** in §8.)

```c
/** Opaque compiled-expression handle. */
typedef struct SWMM_Expr_s* SWMM_Expr;

/** Validate without compiling. Safe per keystroke. errbuf/col_out may be NULL.
 *  @returns SWMM_OK when valid, SWMM_ERR_BADPARAM otherwise. */
SWMM_ENGINE_API int swmm_expr_validate(
    const char* expr, const char* const* var_names, int n_vars,
    char* errbuf, int buflen, int* col_out);

/** Compile and bind variables by name. Caller owns the handle. */
SWMM_ENGINE_API int swmm_expr_compile(
    const char* expr, const char* const* var_names, int n_vars,
    SWMM_Expr* out, char* errbuf, int buflen, int* col_out);

/** Evaluate one row. vars is indexed to match the compile-time name table. */
SWMM_ENGINE_API int swmm_expr_eval(
    SWMM_Expr expr, const double* vars, int n_vars, double* result);

/** Evaluate n_rows rows in one call. vars is row-major, n_rows x n_vars.
 *  results must hold n_rows doubles; non-finite results are written as NaN.
 *  @param[out] n_bad Receives the count of non-finite results. May be NULL.
 *  @returns SWMM_OK even when some rows are non-finite; check n_bad. */
SWMM_ENGINE_API int swmm_expr_eval_batch(
    SWMM_Expr expr, const double* vars, int n_rows, int n_vars,
    double* results, int* n_bad);

SWMM_ENGINE_API int swmm_expr_free(SWMM_Expr expr);
```

`swmm_expr_eval_batch` is the load-bearing call. A per-row FFI round-trip over a
20 000-conduit table is the kind of full-table hot path
`plans/GUI_LOAD_PERF_REVIEW_2026-08-13.md` warns about; one batch call over a packed
`QVector<double>` is a single crossing plus `evaluate_fast()` per row (pre-bound indices,
fixed C-stack, no heap, no string compares).

### 2.4 Engine files

| File | Change |
|---|---|
| `src/engine/math/MathExpr.hpp` / `.cpp` | Add `validate(expr, var_names, msg, col)`. No existing signature changes. |
| `include/openswmm/engine/openswmm_expr.h` | **New.** Public API above. |
| `src/engine/core/openswmm_expr_impl.cpp` | **New.** Thin wrapper; `SWMM_Expr_s` owns an `openswmm::mathexpr::Expression` + its bound name table. |
| `CMakeLists.txt` | Add the new source; the new header is picked up by the existing `install(DIRECTORY include/openswmm ...)` at :331. |
| `python/openswmm/engine/_expr.pyx`, `_common.pxd` | *(Phase 6, optional)* Cython binding, mirroring `_quality.pyx:296`. |
| `tests/unit/engine/test_expr_api.cpp` | **New.** See §7. |
| `CHANGELOG.md` | Per §5.2. |

---

## 3. GUI architecture (MVC, per CLAUDE.md §5.1)

The same data is editable from the Attribute Table, the Property Browser, and now the
Calculator, so the calculator must not become a fourth write path. It composes existing seams:
it computes values, then hands them to `QAbstractItemModel::setData(..., Qt::EditRole)` —
the exact call the table's own cell editors make. Undo, engine dispatch, `objectEdited`
cross-view sync, and read-only enforcement all come for free and stay in one place.

### 3.1 Model (no widget dependencies — headless-testable)

**`include/ui/calculator/calculatorvariables.h` / `src/...cpp`**

```cpp
namespace openswmmvis::calc {

struct VariableDef {
    QString name;        // identifier as used in expressions
    QString label;       // tree display
    QString group;       // "Attributes" | "Results" | "Topology"
    QString tooltip;
};

/*! One source of per-row variable values. */
class IVariableProvider {
public:
    virtual ~IVariableProvider() = default;
    virtual QList<VariableDef> definitions() const = 0;
    /*! Fill out[def.name] for the given source row. Return false to skip the row. */
    virtual bool valuesForRow(int row, QHash<QString, double> &out) const = 0;
};

} // namespace
```

Three implementations, added across phases:

| Class | Phase | Source | Naming |
|---|---|---|---|
| `AttributeVariableProvider` | 1 | `columnSpecs()` filtered to `EditorKind::Numeric/Integer` (incl. REAL/INTEGER user flags) + per-column `data(index, Qt::EditRole)` — see note | sanitized column label, e.g. `InvertElev`, `MaxDepth` |
| `ResultsVariableProvider` | 4 | output file via the `analysis_output_*` layer, when results are loaded | `res_` prefix — `res_MaxDepth`, `res_PeakFlow` |
| `TopologyVariableProvider` | 5 | link → its nodes, subcatchment → its outlet | `us_` / `ds_` / `outlet_` prefix — `us_InvertElev` |

Name sanitization must be deterministic and collision-checked: strip non-identifier
characters, and on collision append `_2`. The tree is the source of truth for what the user
types, so collisions must never be silent.

> **Access note (found during plan verification).**
> `SWMMAttributeTableModel::rowData(int)` is **private** (`swmmattributetablemodel.h:174`),
> so providers cannot call it. Two options:
> **(a) *(recommended, zero model change)*** read each numeric column via the public
> `data(model->index(row, col), Qt::EditRole)`. Values are identical — `data()` reads
> editable columns straight through the engine getter — and it costs nothing extra because
> the model already caches rows.
> **(b)** promote `rowData()` to public as `QVariantMap rowValues(int row) const`.
> Cheaper per row when *every* column is needed, and it is also what the WHERE-clause path
> wants, since `evaluateQuery()` takes a `QVariantMap`. Widens the model's public surface.
>
> Plan currently specifies **(a)** for variables. The WHERE path in §3.3 needs a `QVariantMap`
> regardless; the panel already builds one for the existing query bar, so reuse that call
> site rather than adding an accessor. Tracked as **Q7** in §8.

**`include/ui/calculator/attributecalculatormodel.h` / `src/...cpp`**

```cpp
class AttributeCalculatorModel {
public:
    enum class Output { ExistingColumn, NewUserFlag, PreviewOnly };
    enum class Scope  { AllRows, SelectedRows, WhereMatch };

    struct PreviewRow { QString name; QVariant oldValue; double newValue; QString status; };

    struct Result {   // same shape as MeshAttributeAssignDialog::SampleResult, which is a
                      // private nested struct (meshattributeassigndialog.h:63) — analogy,
                      // not a shared type. Do not try to reuse it.
        int scanned = 0, written = 0;
        int skippedReadOnly = 0, skippedNonFinite = 0, skippedOutOfRange = 0, skippedNoVars = 0;
        QString error;
    };

    void setSource(SWMMAttributeTableModel *model, SelectionManager *sel);
    void setExpression(const QString &expr);
    void setOutput(Output, int column, const NewFieldSpec &);
    void setScope(Scope, const QString &whereClause);

    bool validate(QString *msg, int *col) const;          // -> swmm_expr_validate
    QVector<PreviewRow> preview(int maxRows, QString *err) const;
    Result apply(QUndoStack *undo);
};
```

`apply()` is the only method that writes. It:
1. resolves the row set (§3.3),
2. packs a row-major `QVector<double>` of variable values,
3. calls `swmm_expr_compile` once + `swmm_expr_eval_batch` once,
4. opens `undo->beginMacro(tr("Field calculator: %1").arg(columnLabel))`,
5. per row: skips non-finite, clamps/skips against `ColumnSpec::minValue/maxValue`,
   skips cells whose `flags()` lack `ItemIsEditable`, then `setData(idx, v, Qt::EditRole)`,
6. `endMacro()`, returns the `Result`.

Step 5's skip logic is deliberately identical in shape to
`AttributeTablePanel::applyValueToSelectedRows` (`attributetablepanel.cpp:1955`) — that is
the existing, working bulk-write precedent and there is no reason to invent a second one.

### 3.2 View

| File | Role |
|---|---|
| `include/ui/dialogs/attributecalculatordialog.h` / `src/...cpp` | **New.** The dialog. Structure follows `meshattributeassigndialog.cpp` (preview/apply split, scope radios, `SampleResult` status line); testability convention follows `userflagsdialog.h` (takes the model, not the layer; public `applyChanges(QString*)`; `setConfirmationsEnabled(bool)` test hook). `setObjectName("SWMMCalculatorDialog")` so `DialogLayoutWatcher` persists geometry automatically. |
| `include/ui/widgets/calculatorexpressionedit.h` / `src/...cpp` | **New.** Clone of `TreatmentExpressionEdit` with one structural change: `setVariableNames(QStringList)` at runtime, because unlike treatment's fixed 8 variables the table varies per category. Highlighter + completer are re-pointed at that list; validation calls `swmm_expr_validate`. Carry the header's "VOCAB DRIFT GUARD" note forward — the engine remains authoritative for the verdict. |

### 3.3 Controller

`AttributeTablePanel` gains:

```cpp
void onFieldCalculatorClicked(int preselectColumn = -1);
```

It owns the wiring the dialog must not reach for itself:
- row scope: *Selected* → existing `selectedSourceRows()` (`attributetablepanel.cpp:1939`);
  *Where* → existing `parseQuery` / `evaluateQuery` from `core/queryparser.h`;
  *All* → `0..rowCount-1`. No new selection or filter machinery.
- undo stack: `m_model->undoStack()`, or the canvas stack for the mesh source — the same
  ternary already in `applyValueToSelectedRows`.
- post-apply: `refreshObject(name)` per written row and forward `objectEdited`, guarded by
  the existing `m_suppressEditForward` reentrancy flag.

### 3.4 New-field creation (Phase 2)

`swmm_userflag_define(engine, name, type, description)` /
`swmm_userflag_undefine(engine, name)` already exist
(`openswmm_model.h:664`, `:677`), as does `openswmmvis::ui::UserFlagsModel`.
Per-object values go through `swmm_userflag_value_set(engine, objType, objName, flagName, value)`
as strings — which is exactly what the `"userflag"` setter tag already does, so the write path
needs no change.

One new command is required so field creation participates in undo:

```cpp
// include/ui/commands/userflagcommands.h  (new)
class UserFlagDefineCommand : public QUndoCommand;   // redo: define; undo: undefine
```

It is pushed **first inside the calculator's macro**, so a single Ctrl+Z removes both the new
field and every value written into it. Note the sequencing constraint: the field must exist
and `appendUserFlagColumns()` must have run before the value writes can address a column.

### 3.5 Preview-only virtual column (Phase 3)

Two options; the plan recommends the smaller one.

- **(a) Dialog-local preview table only** *(recommended)*. The preview table already exists in
  the dialog. Add a *Copy to clipboard* / *Export CSV…* button. Zero change to
  `SWMMAttributeTableModel`.
- **(b) Transient column in the table.** `setVirtualColumn(QString name, QVector<double>)` /
  `clearVirtualColumn()`. Read-only, never persisted, cleared on `reload()` / `setSource()`.
  Touches `columnCount`, `data`, `headerData`, `flags`, and every existing column-index
  assumption in the panel and its delegates.

(b) is the only part of the feature that changes the table model's contract, for a
view-only convenience. Recommend (a) for Phase 3 and revisit only if the preview table
proves insufficient in use. **Q2** in §8.

---

## 4. Files summary

### `openswmm.engine`

| File | New/Changed |
|---|---|
| `src/engine/math/MathExpr.hpp` / `.cpp` | Changed — add `validate()` |
| `include/openswmm/engine/openswmm_expr.h` | New |
| `src/engine/core/openswmm_expr_impl.cpp` | New |
| `tests/unit/engine/test_expr_api.cpp` | New |
| `CMakeLists.txt`, `CHANGELOG.md` | Changed |

### `openswmm.gui`

| File | New/Changed |
|---|---|
| `include/ui/calculator/calculatorvariables.h` + `src/ui/calculator/calculatorvariables.cpp` | New |
| `include/ui/calculator/attributecalculatormodel.h` + `src/ui/calculator/attributecalculatormodel.cpp` | New |
| `include/ui/dialogs/attributecalculatordialog.h` + `src/ui/dialogs/attributecalculatordialog.cpp` | New |
| `include/ui/widgets/calculatorexpressionedit.h` + `src/ui/widgets/calculatorexpressionedit.cpp` | New |
| `include/ui/commands/userflagcommands.h` + `src/ui/commands/userflagcommands.cpp` | New (Phase 2) |
| `include/ui/panels/attributetablepanel.h` + `src/ui/panels/attributetablepanel.cpp` | Changed — toolbar action, header context-menu item, `onFieldCalculatorClicked` |
| `tests/gui/test_attribute_calculator_model.cpp` | New |
| `tests/gui/test_calculator_expression_editor.cpp` | New |
| `CMakeLists.txt` | Changed — headers ~:160–700, sources ~:800–1260, test target |
| `docs/manual/` (new page) + `docs/manual/09_mesh_attribute_tables.md` | Changed |
| `CHANGELOG.md` | Changed |

**Note:** every new file must be hand-added to the 111 KB top-level `CMakeLists.txt`;
there is no glob.

---

## 5. Order of work with verification gates

```
1. Engine: mathexpr::validate()          -> verify: test_mathexpr passes, new diag cases pass
2. Engine: openswmm_expr.h + impl        -> verify: test_expr_api green; batch == N single evals
3. GUI: calculatorvariables (attrs only) -> verify: headless test, junction/conduit var lists correct
4. GUI: attributecalculatormodel         -> verify: headless apply on a fixture INP; Ctrl+Z restores
5. GUI: calculatorexpressionedit         -> verify: highlight + completion + debounced banner
6. GUI: dialog + panel wiring            -> verify: manual pass on an example INP; smoke gate
7. Docs + CHANGELOG                      -> verify: manual page renders
```

---

## 6. Phasing

| Phase | Content | Independently shippable? |
|---|---|---|
| **1** | Engine expr API; dialog; same-row attribute variables; all three row scopes; write to existing editable columns | Yes — this is a usable field calculator |
| **2** | *Create new field* (`[USER_FLAGS]` + `UserFlagDefineCommand`) | Yes |
| **3** | Preview-only output (recommend §3.5(a)) | Yes |
| **4** | `ResultsVariableProvider` — `res_*` variables when results are loaded | Yes, additive |
| **5** | `TopologyVariableProvider` — `us_*` / `ds_*` / `outlet_*` | Yes, additive |
| **6** | *(Optional)* Cython binding for `swmm_expr_*` | Yes |

---

## 7. Testing (CLAUDE.md §4.1 — reviewable paths, no temp dirs)

Fixtures and any generated artifacts go to `openswmm.gui/tests/gui/data/` and
`openswmm.gui/tests/gui/output/`, both committed-visible. No `QTemporaryDir`.

**Engine — `tests/unit/engine/test_expr_api.cpp`**
- valid expressions across every supported operator and function
- unknown variable → `SWMM_ERR_BADPARAM` with a plausible `col_out`
- unbalanced parens, trailing operator, empty string, NULL expr
- `swmm_expr_eval_batch` over 1 000 rows equals 1 000 `swmm_expr_eval` calls bit-for-bit
- division by zero and `sqrt(-1)` → NaN in results, counted in `n_bad`
- compile/free with no leak (ASan preset)

**GUI — `tests/gui/test_attribute_calculator_model.cpp`** (headless, no dialog)
- variable list for junctions and for conduits matches the editable numeric columns
- name-collision sanitization is deterministic
- `apply()` writes the right values on a fixture INP; single `Ctrl+Z` reverts all of them
- read-only column selected → `skippedReadOnly` equals the row count, `written == 0`
- out-of-range value against `ColumnSpec::min/maxValue` → counted, not written
- each of the three scopes selects the expected row set
- 20 000-row category: one `swmm_expr_compile`, one `swmm_expr_eval_batch`
  (assert call counts via a seam, not wall-clock)

**GUI — `tests/gui/test_calculator_expression_editor.cpp`**
- mirrors `tests/gui/test_treatment_expression_editor.cpp`: highlighting, completer popup,
  debounce, `validationChanged` payload

---

## 8. Open questions for review

**Q1 — Should `swmm_expr_*` take an `SWMM_Engine` handle?**
It needs none. `swmm_treatment_validate_expression` takes one and documents it as unused, so
there is a consistency argument for taking it anyway. Taking it makes the calculator dependent
on an open model for what is pure arithmetic; not taking it is a first for this API surface.
Plan currently specifies **engine-free**.

**Q2 — Preview-only: dialog table (§3.5a) or real virtual column (§3.5b)?**
Plan currently specifies (a).

**Q3 — Conditionals.** `mathexpr` has no `if()` and no comparison operators. Plan currently
routes this need through the *Rows matching* filter rather than extending an engine module
shared by Controls / Treatment / Groundwater. Confirm that is acceptable.

**Q4 — Missing referenced doc — actionable, not just a question.**
**19 files** across the repo cite `docs/USER_FLAGS_UI_PLAN_2026-06-03.md` (including
`swmmattributetablemodel.h:170`, `userflagsdialog.h:6`, `userflagsmodel.h:8`,
`swmmmodellayer.h:1758`, `swmmvis.h:414`, and 4 test files), and the file is not in the
working tree. It was deleted in commit `97ac26d` ("Remove plans").

**Recover it before Phase 2 starts:**
`git show 97ac26d^:docs/USER_FLAGS_UI_PLAN_2026-06-03.md`

Phase 2 (*create new field*) depends on the user-flag UI conventions that doc sets, and 19
dangling references is a documentation defect worth fixing regardless of this feature.
Separately, `swmmattributetablemodel.cpp:1641` cites an `ATTRIBUTE_EDITOR_WIRING follow-up
(2026-06-04)` with no corresponding file — same treatment.

**Q5 — Mesh source.** `MeshAttributeTableModel` reuses `ColumnSpec` and has its own command
family (`MeshSetTriangleAttributeCommand`, ids 43/45/46) on the canvas undo stack. The design
supports it with no extra abstraction. Include mesh in Phase 1, or SWMM-only first?

**Q6 — Engine/GUI release coupling.** Phase 1 GUI work cannot land before the engine ships
`openswmm_expr.h`. Is the GUI built against a pinned engine version, and does that gate the
schedule?

**Q7 — Row-value access.** See the access note in §3.1. Plan specifies public
`data(idx, Qt::EditRole)` per column rather than promoting the private `rowData()`. Confirm,
or approve widening `SWMMAttributeTableModel`'s public surface with `rowValues(int)`.

---

## 10. Verification record

Plan claims were fact-checked against both repos on 2026-08-14; 14 of 15 spot-checked
assertions verified exactly (file paths, line numbers, signatures, access levels). Corrections
already folded in: the `rowData()` access defect (§3.1 note, Q7), the `SampleResult` privacy
caveat (§3.1), and the Q4 reference count + recovery commit. Everything else in this document
was confirmed against the code as written.

---

## 9. Risks

| Risk | Mitigation |
|---|---|
| Full-table scan on a large model stalls the UI (`GUI_LOAD_PERF_REVIEW_2026-08-13.md`) | Preview computes ≤200 rows lazily; full scan only on Apply; `QProgressDialog` above ~5 000 rows; single batch FFI call |
| Silent unit misinterpretation | §1.2 — no conversion exists today; state it in dialog help and add a note to the model header so a future conversion layer must revisit this |
| Variable-name collisions after sanitization | Deterministic `_2` suffix, surfaced in the variable tree; unit-tested |
| A second grammar drifting from the engine's | D1 forbids client-side parsing; `validate()` is cross-checked against `parse()`; "VOCAB DRIFT GUARD" note carried into the new editor |
| Partial write leaves the model inconsistent | One `beginMacro`/`endMacro` per apply — atomic to undo |
| Writes bypassing the shared edit path | Calculator only calls `setData(..., Qt::EditRole)`; never touches `swmm_*_set_*` directly |
