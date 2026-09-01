# HANDOFF — GUI: Multi-Column Series Files, Column Pickers & Persistence (phases 5–7)

Audience: implementing agent. Spec: `workplans/MULTICOLUMN_SERIES_SINGLE_READ_2026-08-17.md` (read fully — verified file/line evidence throughout). Obey `CLAUDE.md` (MVC, surgical changes, transparent test IO, no commits).

**Prerequisite**: the engine phases add `swmm_gage_get_file_column` / `swmm_gage_set_file_column` / `swmm_gage_get_file_format` to `openswmm.engine/include/openswmm/engine/openswmm_gages.h`. Confirm those declarations exist in the sibling repo (`../openswmm.engine`) before starting task 4 below; tasks 1–3 do not depend on them. The GUI will not link until the user rebuilds the engine — expected; note it, don't fight it.

## Scope (spec §4, tasks 1–5)
1. **Shared util** `src/ui/util/externalcolumnfile.{h,cpp}`: `readHeaders(path)` + `readColumn(path, columnName)` for CSV/TSV/TSF. Build on `io/timeseriesparse.{h,cpp}`: add an AM/PM format string to `tryParseTimestamp` (`timeseriesparse.cpp:32-41`) and `IDs:` 3-row TSF header handling. Extract the column-enumeration logic currently inline at `timeserieseditordialog.cpp:1423-1489` (header-vs-data probe, `col_%1` fabrication, selector resolution) so dialog + rain gage + `observedcsvrunlayer` can share it. Match the engine's detection rules (spec §3.1).
2. **Timeseries dialog** (`src/ui/dialogs/timeserieseditordialog.cpp`): switch `loadExternalFileIntoProvider_` to the util; add `*.tsf` to the filter at `:1263` and tooltip `:1064`. **Fix B3**: `:1896` passes `columnHeadersOut=nullptr` so the combo is never repopulated on series switch (gate at `:1168` then disables it) — pass real headers; after Browse (`:1267`) sync `columnSelector` with the combo's shown item.
3. **Persistence — fix B4** (`src/timeseries/timeseriesregistry.cpp:137` currently skips non-inline providers): on `saveToEngine`, write file-backed providers as engine FILE timeseries with the composed `path:col` token via `swmm_file_path_set(…, SWMM_FILE_TIMESERIES_DATA, name, …)`; on model load, a FILE timeseries whose token carries `:col` becomes a file-backed provider with `columnSelector` parsed out (drive-letter-aware colon split — mirror `CatchmentHandler.cpp:317-331` semantics). The user never types the colon; the GUI composes/splits it.
4. **Rain-gage implicit column picker**:
   - `src/ui/properties/swmmraingagepropertyadapter.{h,cpp}`: add `Q_PROPERTY(QString fileColumn READ fileColumn WRITE setFileColumn NOTIFY changed)` → new C-API; display label per the `:31-32` pattern.
   - `src/ui/panels/swmmattributetablemodel.cpp`: "Rain File (path)" (`:853-857`) becomes a file-browse editor with filters `*.csv *.tsv *.tsf *.txt *.dat`; add a "Rain File Column" combo column populated via `readHeaders(resolvedFilePath)`; register `gage_file_column` getter/setter in the dispatch table (`:1641-1660` pattern).
   - `src/ui/panels/propertiespanel.cpp` (`:1061-1077`): add `setRowEditable(pm, gageAdapter, "fileColumn", isFile)`; gate Station ID to STAN_PRCP-format gages using `swmm_gage_get_file_format`.
   - MVC: property panel and attribute table must stay in sync through the existing `changed()`/`objectEdited` fan-out (`swmmvis.cpp:3115-3143`) — no new sync paths.
5. Keep `DataObjectRef`-based series references untouched — other objects depend on a file column only through its *named* timeseries.

## Tests
- Extend `tests/unit/test_timeseries_parse.cpp`: AM/PM timestamps; TSF `IDs:` header enumeration; header-vs-data probe via the new util (this test builds without full GUI — keep it QtCore-only if possible).
- New `tests/unit/test_externalcolumnfile.cpp`: `readHeaders`/`readColumn` on CSV/TSV/TSF fixtures (put fixtures in `tests/unit/data/` or the existing test-data convention — check `tests/` layout first).
- GUI-level cases from spec §6 (persistence round-trip, combo repopulation regression, MVC sync) — implement if a test harness for dialogs exists (check `tests/gui/`); otherwise document them as manual test steps in your verification section.

## Build & verification (sandbox has NO Qt; user builds locally on macOS)
Repo at `/sessions/magical-serene-heisenberg/mnt/openswmm.gui` in bash; sibling engine at `/sessions/magical-serene-heisenberg/mnt/openswmm.engine`.
- Try `apt-get install -y qt6-base-dev` (may fail offline). If Qt available: `pip install cmake ninja --break-system-packages`, configure, build only the touched targets/tests, run `ctest -R "timeseries_parse|externalcolumnfile"`.
- If Qt unavailable: (a) careful `-fsyntax-only` is NOT possible for Qt code — instead verify non-Qt logic by keeping `externalcolumnfile` parsing logic in plain-C++ helpers where feasible and compiling those standalone under `tests/standalone_externalcolumn/` (main.cpp + README + fixtures, user-reviewable, never /tmp); (b) re-read every edited file after editing to self-review; (c) grep for all call sites of every symbol whose signature you change.
- APPEND `## Verification results (GUI)` to THIS file: what ran, pass/fail, and an explicit list of items requiring the user's local Qt build (expected: full GUI compile, dialog behavior, persistence round-trip in the app).

## Hard constraints
- No git commits. Surgical diffs; match style; no formatting churn; MVC per CLAUDE.md §5.1.
- Do not modify `observedcsvrunlayer.cpp` beyond (optionally) switching it to the shared util — if that swap grows risky, leave it and note the remaining duplication.
- `CHANGELOG.md`: don't edit; suggest entries in your verification section.
- On spec-vs-source conflict: stop and report, don't improvise.

## Definition of done
Tasks 1–5 implemented; parse-level tests written and (if toolchain allows) run; verification recorded; final message = concise change summary + verification table + explicit user-action list (engine rebuild, local Qt build, manual test steps).

---

## Verification results (GUI)

Recorded 2026-08-17 by an independent review pass (implementation was already
complete; this pass audited it, re-ran what the sandbox allows, and recorded
what only the local macOS Qt build can prove). **No production code was changed
by this pass** — every item below is either verified-as-implemented or listed as
an observation/risk for the user to decide on.

Environment: Linux sandbox, `apt` proxy-blocked ⇒ **no Qt6** (`/usr/include/*/qt6`,
`qmake6`, `moc` all absent), so no GUI compile and no `ctest`. `pip install cmake
ninja --break-system-packages` → already satisfied (cmake 4.4.2, ninja 1.13.0),
but without Qt they cannot configure this project.

### Commands run

| # | Command | Result |
|---|---------|--------|
| 1 | `git status --porcelain` + `git diff` (all 20 modified files) + `cat` of the 4 new sources/tests and 6 new fixtures | reviewed hunk-by-hunk |
| 2 | `cd tests/standalone_externalcolumn && c++ -std=c++17 -Wall -Wextra -o standalone_externalcolumn main.cpp` | **rebuilt from current source**, zero warnings |
| 3 | `./standalone_externalcolumn` | `ALL CHECKS PASSED`, exit 0 |
| 4 | `ls /usr/include/x86_64-linux-gnu/qt6 /usr/include/qt6 /usr/lib/qt6; which qmake6 qmake moc` | none present ⇒ Qt build skipped (expected) |
| 5 | Rule-by-rule diff of `include/ui/util/externalcolumnfilecore.h` against `openswmm.engine/src/engine/input/MultiColumnSeriesFile.cpp` and `handlers/CatchmentHandler.cpp:312-331` | mirrors engine (details below) |
| 6 | Signature/call-site greps: `readHeaders`/`readColumn`, `saveToEngine`/`loadFromEngine`, `EditorKind::*` switches, `ColumnSpec{` aggregate-init, `Qt::UserRole + N` collisions, `SWMMVIS_TEST_SOURCES`/`PROJECT_SOURCES` membership | all clean (details below) |
| 7 | Engine-side confirmations: `swmm_gage_get/set_file_column`, `swmm_gage_get_file_format` declarations + impls; `swmm_file_path_get/set` signatures and `SWMM_FILE_TIMESERIES_DATA = 10`; `InpWriter.cpp:1864` FILE-emit condition; `PostParseResolver.cpp:120-205` `:col` split; B1 fix in `openswmm_gages_impl.cpp` | all present and compatible |

### Per-task verification

| Handoff task | Status | Evidence |
|---|---|---|
| 1 — shared util `src/ui/util/externalcolumnfile.{h,cpp}` + Qt-free `externalcolumnfilecore.h` | **PASS** (static + standalone) | TSF `IDs:` detection is case-insensitive, tab-delimited, skips the parameter + units rows (2 `readLine()`s) exactly like the engine; quote-aware cell split, BOM/`\r` strip, `;`/`#` comments, `strtod` value cells, ascending sort, NaN-cell skip all mirror `MultiColumnSeriesFile.cpp`. `parseSeriesDateTime` is a line-for-line mirror of `parse_series_datetime` (ISO-8601, US `M/D/Y`, trailing AM/PM with 12 AM→00 / 12 PM→12, and the fixed `n<4`/`n<6` zeroing). AM/PM formats were also added to `io::tryParseTimestamp` as the handoff required. Delimiter sniff keeps the GUI's semicolon extension on top of the engine's tab-vs-comma rule (documented, agrees on every engine-loadable file). |
| 2 — timeseries dialog | **PASS** (static) | `loadExternalFileIntoProvider_` now delegates CSV/TSV/TSF to `readColumn` (the `.dat` whitespace branch is untouched and still fabricates its `value` header). `*.tsf` is in the Browse filter (`:1278`) and the button tooltip. **B3 fixed on both halves**: `rebindActiveProvider_` no longer passes `columnHeadersOut = nullptr` — it repopulates on *every* series switch (re-enumerating headers cheaply when the point cache is already resident) and `populateColumnCombo_` runs *before* `refreshSourceModeCardForProvider_`, so the `count() > 0` enable gate can no longer read a stale count; after Browse the provider's `columnSelector` is synced to the shown item. Combo items carry the real header name in `userData` and `onColumnSelectorChanged_` / `onReloadExternalFile_` now read `itemData`/`currentData` instead of display text, so the "(no header — single column)" placeholder can never leak into a stored selector. |
| 3 — persistence (B4) | **PASS** (static) | `saveToEngine` no longer skips ExternalFile providers that carry a path: it `swmm_timeseries_add`s when absent and writes `composePathColumn()` through `swmm_file_path_set(…, SWMM_FILE_TIMESERIES_DATA, name, …)`; an unchanged reference is left verbatim so a relative `.inp` token stays relative; Inline providers now *clear* a stale FILE token (a Detach→Inline series would otherwise keep writing as FILE). `loadFromEngine` splits the slot token back out drive-letter-aware. Signatures match `openswmm_model.h` exactly (7-arg get, 4-arg set, role 10). Confirmed in the engine that the resolver deliberately **keeps** the `:column` decorator in `FilePathPair.absolute` (`PostParseResolver.cpp:88-95`), so preferring `absBuf` over `origBuf` on load is correct, and that `InpWriter.cpp:1864` emits `name FILE "token"` whenever the slot is non-empty regardless of point count. |
| 4 — rain-gage column picker | **PASS** (static) | Adapter: `Q_PROPERTY(QString fileColumn …)` + `fileColumn()`/`setFileColumn()` over `swmm_gage_get/set_file_column`, display label `"Rain File Column"` per the `:31-32` pattern, plus non-property `fileFormat()` over `swmm_gage_get_file_format`. Attribute table: `"Rain File (path)"` is now `EditorKind::FileBrowse` with `*.csv *.tsv *.tsf *.txt *.dat`; new `"Rain File Column"` column is `EditorKind::FileColumn`, options served from `data()` via `kFileColumnOptionsRole` → `readHeaders(resolved gage path)`; `gage_file_column` registered in the dispatch table with the raw C-API pointers (signatures match `setFnS`/`getFnS` exactly — no wrapper needed). Panel: `setRowEditable(…, "fileColumn", isFile)` added and Station ID gated to `isFile && fmt != 6`; both recompute through the existing `changed()` connection, so no new sync path (MVC per CLAUDE.md §5.1). Verified the options role reads the *same* storage the browse setter writes (both `SWMM_FILE_RAINGAGE_DATA`), and that the gage path slot never carries the colon (`CatchmentHandler` strips it into `col_name`; `InpWriter:1035` re-composes it) — so `readHeaders(path)` is correct there. |
| 5 — colon never typed | **PASS** | The only two places a colon is produced/consumed are `extcol::composePathColumn` / `splitPathColumn`. The user picks a column from a combo (or types a bare column *name*). Verified no UI string asks for `path:col`. |
| Tests registered | **PASS** | `test_externalcolumnfile` added to `tests/unit/CMakeLists.txt` with the util + `timeseriesparse.cpp` sources; unit tests run with `WORKING_DIRECTORY …/data`, which is where the new `extcol_*` fixtures live, so the relative fixture names resolve. All 6 fixtures were read and their contents match every expected count/value/timestamp in the tests (incl. the empty `RG_WEST` cell that must be skipped and the `1:30:00 PM` → 13:30 row). Both gui-test targets that compile `timeserieseditordialog.cpp` (lines 817, 875) got `src/ui/util/externalcolumnfile.cpp` added (833, 891); full-app gui tests pick it up automatically because the root `PROJECT_SOURCES` edit is inside the `set(PROJECT_SOURCES …)` block that `tests/gui/CMakeLists.txt:1974` re-roots. GUI fixtures `tests/gui/data/extcol_*` are present and use the established `SWMMVIS_GUI_TEST_DATA` env pattern. |
| No regressions in shared plumbing | **PASS** | `EditorKind` gained two enumerators — the only `switch` over it that needs cases is `installColumnDelegates` (both added); `promptBulkValue` falls through its `default:` (text prompt) which is acceptable for bulk-apply. `ColumnSpec` gained a trailing field and is never aggregate-initialized anywhere. `kFileColumnOptionsRole = Qt::UserRole + 21` is unique repo-wide. `saveToEngine`'s return value is ignored by all 20+ call sites, so the changed count semantics break nothing. `setAllPoints` has no source-mode guard, so `loadFromEngine` setting ExternalFile mode before pushing points is safe. No `-Werror` in the build, so the `sizeof(buf)`→`int` narrowing in the adapter (which matches the surrounding pre-existing style) is not a build risk. |

### Requires the user's local macOS Qt build — nothing below could run in the sandbox

1. **Engine rebuild first.** The GUI will not link until `openswmm.engine` is rebuilt/installed with `swmm_gage_get_file_column`, `swmm_gage_set_file_column`, `swmm_gage_get_file_format`. (Declarations + implementations confirmed present in the sibling repo working tree — they are *not* yet compiled into whatever engine binary the GUI currently links.)
2. **Full GUI compile** (`SWMMVis` target). Everything Qt-dependent in this change set — `externalcolumnfile.cpp`, the dialog refactor, the two new delegates, the model role, the adapter property, the panel gating — has been reviewed by re-reading each hunk and grepping all call sites, but never compiled.
3. **`ctest -R test_externalcolumnfile`** — 12 new gtest cases (readHeaders/readColumn over CSV/TSV/TSF/headerless + the `path:col` token core).
4. **`ctest -R test_timeseries_parse`** — 3 new AM/PM / single-digit-US timestamp cases.
5. **`ctest -R "test_timeseries_editor_dialog|test_timeseries_registry_engine"`** — the new TSF-load case, the **B3** combo-repopulation regression, and the five **B4** persistence cases (compose, load-split, drive-letter split, verbatim-token preservation, Inline clears stale token).
6. **`ctest -R "test_attributetableschema|test_selectionops"`** (and any other full-app gui test) — these compile the whole app source list, so they are the cheapest smoke test that the new schema columns and delegates don't break existing expectations. `test_attributetableschema` in particular may assert gage column counts/order — the new "Rain File Column" inserts a column after "Rain File (path)".
7. **In-app dialog behavior** — items 1–4 of the manual steps below.
8. **Persistence round-trip through a real `.inp`** — manual step 5.
9. **MVC sync between property panel and attribute table** — manual step 6.

### Manual test steps (local, after the engine + GUI rebuild)

1. **TSF in the timeseries editor.** Time Series editor → new series → Source = External File → Browse → pick `tests/unit/data/extcol_sample.tsf` (filter must list `*.tsf`). Expect: combo lists `RG1`, `RG2`; 3 points load; `12:00:00 AM` renders as 00:00 and `1:30:00 PM` as 13:30. Switch the combo to `RG2` → values become 0.05 / 0.15 / 0.25.
2. **B3 — series switch.** With the series above still selected, create a second (inline) series, click it in the list, then click back to the file-backed one. Expect: the column combo is repopulated (2 items, not the previous series' items, not empty), **enabled**, and showing the column the provider actually holds.
3. **B3 — Browse consistency.** Browse to a multi-column CSV. Expect: the combo shows the first real header **and** the series' stored column matches it (no display/state divergence). Then Reload → same column reloads (not column 0).
4. **Headerless CSV.** Browse to `tests/unit/data/extcol_noheader.csv`. Expect: combo shows `col_1`/`col_2` and the *stored* selector stays empty (see risk R1 — do not expect `col_2` to survive a save/engine reload).
5. **B4 — persistence round-trip.** With a file-backed series named e.g. `RAIN_TS` and column `RG2`, save the project, then inspect the written `.inp`: `[TIMESERIES]` must contain `RAIN_TS FILE "…/extcol_sample.tsf:RG2"` (relative if the file is under the project dir). Reopen the model: the series must come back as External File with column `RG2`, and a rain gage using `TIMESERIES RAIN_TS` must still resolve and produce non-zero rainfall on a run.
6. **Rain gage + MVC sync.** Select a FILE-source rain gage. In the Property Browser: "Rain File (path)" and the new "Rain File Column" are editable, Series Name greyed. Set the path to a multi-column CSV, then set the column → Station ID greys out (format flipped to USER_CSV). Open the Attribute Table on Rain Gages for the same gage: the path cell has a "…" browse button, the "Rain File Column" cell is a combo listing that file's headers, and both cells show the values just set. Change the column in the table → the Property Browser row updates. Save → `[RAINGAGES]` line reads `FILE "path:col"`.
7. **B1 regression.** On a *different* gage that already has a `path:col` binding, edit Station ID in either editor → the first gage's column and format must be unchanged, and the `.inp` must still emit `path:col`.
8. **Standalone parse harness** (no Qt needed, works on macOS too): `cd tests/standalone_externalcolumn && c++ -std=c++17 -Wall -Wextra -o standalone_externalcolumn main.cpp && ./standalone_externalcolumn` → `ALL CHECKS PASSED`.

### Observations / open risks — reviewed and deliberately left alone

- **R1 — headerless multi-column CSVs cannot round-trip a non-first column.** The GUI fabricates `col_1…col_N` when the first content line parses as data (preserving the old dialog/`ObservedCsvRunLayer` behavior); the engine has no such probe — it always treats line 1 as headers, so a stored `path:col_3` token would fail `find_column` with `column "col_3" not found` (`PostParseResolver.cpp:198-203`), and the engine would also consume the first data row as a header. The implementation defends the common case (Browse refuses to store a fabricated selector, so `""`/first-column is what persists), but a user who *manually* picks `col_3` from the combo can still persist an unresolvable token. Real fix needs a decision: warn in the GUI, or add an index/`col_N` form to the engine grammar. Out of this spec's scope.
- **R2 — an unmatched selector silently falls back to the first column in the GUI, but is a hard error in the engine** for timeseries (`ERR_TABLE_FILE_READ`). Only reachable when the file's headers change under a saved model; the GUI would then preview column 1 while the run errors. Documented in `externalcolumnfile.h`.
- **R3 — the rain-gage attribute table does not auto-pick a column after Browse**, unlike the timeseries dialog. A user who sets a multi-column path but no column leaves `file_format` at UNKNOWN/STAN_PRCP, so `InpWriter` emits the legacy `FILE "path" Station Units` grammar for a file that has no station column. Auto-setting it would mean one cell edit writing a second cell (new undo-command shape) — a design change beyond task 4.
- **R4 — the attribute table has no per-row applicability gating for the gage source columns** (pre-existing: "Rain File (path)" and "Station ID" are always editable there; only the Property Browser greys them). The new column follows the existing convention rather than introducing a new gating mechanism.
- **R5 — stale-but-disabled combo items.** Switching from a file-backed series to an Inline or path-less series leaves the previous series' items in the column combo; the `hasFile` gate disables it, so it is cosmetic only. Clearing it would be a one-line addition if the user wants it.
- **R6 — remaining duplication.** `observedcsvrunlayer.cpp` was left untouched per the handoff, so its column-enumeration heuristic is still a second copy; `comparisonplotdialog.cpp:1039` and `calibrationdatadialog.cpp:98` still filter `*.csv *.tsv *.dat` without `*.tsf` (they feed the observed-data path, which cannot parse TSF yet). Both are follow-up candidates, not defects in this scope.
- **R7 — `:` split rule differs slightly across the engine's own two readers** — `CatchmentHandler` (gages) uses first-colon-after-drive-letter, `PostParseResolver` (timeseries) uses `rfind(':')`. The GUI mirrors the CatchmentHandler rule everywhere. They agree unless a path contains more than one colon; worth an engine-side unification someday.

### Suggested CHANGELOG entries (not applied — CLAUDE.md §5.2 says on release)

```
### Added
- Time Series editor: PCSWMM `.tsf` support (tab-delimited, `IDs:` 3-row header,
  12-hour AM/PM timestamps) and a shared CSV/TSV/TSF column reader
  (`ui/util/externalcolumnfile`) matching the engine's detection rules.
- Rain gages: file-browse editor for the rain-file path and an implicit
  "Rain File Column" picker in both the Property Browser and the Attribute
  Table, backed by the new `swmm_gage_get/set_file_column` C-API. Station ID is
  hidden for multi-column (USER_CSV) gages.

### Fixed
- File-backed time series are now persisted to the model as
  `name FILE "path:column"` and restored on load — a chosen file/column was
  previously dropped on save (B4). Detaching a series to Inline clears the
  stale FILE reference.
- Time Series editor: the column selector is repopulated (and re-enabled) when
  switching series, and stays consistent with the stored column after Browse /
  Reload (B3).
```

### Not done by this pass
No production code was modified. The only file changes are this section and a
rebuild of `tests/standalone_externalcolumn/standalone_externalcolumn` from
current source (Linux binary — rebuild locally on macOS with the command in
that directory's README).

---

## Review fixes (GUI)

Recorded 2026-08-17 in response to `openswmm.engine/plans/REVIEW_MULTICOLUMN_SERIES_2026-08-17.md`
(sections A–E). This pass **did** change production code. A separate agent
applied the mirror-image engine fixes concurrently; the four cross-repo
decisions below were agreed before either side started and both sides
implement them identically.

### Cross-repo decisions this pass mirrors

| Decision | Engine side | GUI side |
|---|---|---|
| **Unified colon split** (replaces the engine's two divergent rules): take the LAST `:`; ignore a Windows drive-letter colon (index 1, alphabetic index 0); treat it as a column separator only when the suffix holds no `/` or `\`; else no column. | `CatchmentHandler.cpp` + `PostParseResolver.cpp` unify on it | `include/ui/util/externalcolumnfilecore.h` `splitPathColumn()` |
| **Quote-aware delimiter sniff** (counts only outside-quote delimiters, like the cell splitter). | `MultiColumnSeriesFile.cpp` sniff | `externalcolumnfilecore.h` `sniffDelimiter()` |
| `InpWriter` emits `FILE "path"` with **no trailing colon** when the column is empty. | `InpWriter.cpp` | nothing to do — the GUI already composes a bare path for an empty column (`composePathColumn`), and its split accepts either form |
| New `swmm_gage_set_file_format` so USER_CSV ⇄ STAN_PRCP is no longer one-way. | new C-API | **not consumed yet** — see "Deliberately left" below |

### Finding → fix → verification

| Review finding | Decision implemented | Where | Verified by |
|---|---|---|---|
| **B-3 / R2** — a non-empty column selector that matches no header was a hard engine error but a silent fall-back to column 1 in the GUI: the dialog previewed/charted column 1 while the run refused to open the file. | The GUI no longer substitutes. `readColumn` **fails** (returns −1) with `column "X" not found in <file>`, points are empty, and headers are still reported so the picker can repopulate. The dialog surfaces the reason in the status bar (the file's only user-visible pattern — no new error-dialog pattern), clears the provider's points when it was already bound to that file, and adds a `X (not in file)` combo item so display and provider state stay equal. | `src/ui/util/externalcolumnfile.cpp` column-resolution block; `include/ui/util/externalcolumnfile.h` docs; `src/ui/dialogs/timeserieseditordialog.cpp` — `loadExternalFileIntoProvider_` (new `errorOut`), `populateColumnCombo_`, `onColumnSelectorChanged_`, `onReloadExternalFile_`, `onBrowseExternalFile_`, `linkExternalFile`, `rebindActiveProvider_` | new gtest `ExternalColumnFile.ReadColumn_UnmatchedSelectorFailsInsteadOfFirstColumn`; new Qt case `linkExternalFile_UnmatchedColumnLoadsNothingAndShowsIt` (**needs local Qt**) |
| **B-4 / R1** — headerless CSVs: the GUI could persist `path:col_2`, which the engine cannot resolve, and the two disagreed by one data row. | (a) A fabricated name is **never** persisted: `readHeaders`/`readColumn` now report fabrication through a `fabricatedOut` flag (no `"col_1"` string-sniffing — a real file may legitimately have that header), every fabricated combo item carries an **empty** selector, and Browse only syncs real header names. (b) Picking a non-first column of a headerless file is **refused** with a clear message; the combo reverts to the item matching the provider's actual state. (c) A headerless file resolves **no** name at all (parity: the engine matches against a header row it does not have), so even `col_2` fails instead of previewing as if bindable. (d) **Off-by-one fixed**: the first content line is spent as the header row exactly as the engine spends it (`MultiColumnSeriesFile.cpp` header loop — verified there is no header-vs-data probe on the engine side), so the GUI no longer shows a row the run never sees. (e) The rain-gage attribute table stops **offering** fabricated names (empty option list ⇒ empty selector ⇒ first data column). | `src/ui/util/externalcolumnfile.cpp`; `include/ui/util/externalcolumnfilecore.h` (`fabricatedColumnName`); `timeserieseditordialog.cpp`; `src/ui/panels/swmmattributetablemodel.cpp` `kFileColumnOptionsRole` | new/updated gtests `ReadHeaders_HeaderlessFileFabricatesColN`, `ReadHeaders_RealHeadersAreNotFlaggedFabricated`, `ReadColumn_HeaderlessFirstLineIsSpentAsHeader`, `ReadColumn_HeaderlessRejectsAnyNamedColumn`; fixture `tests/unit/data/extcol_noheader.csv` grew a third line so the off-by-one is visible (3 lines ⇒ 2 points); new Qt case `linkExternalFile_HeaderlessKeepsEmptySelectorAndRefusesOtherColumns` (**needs local Qt**) |
| **B-8** — the delimiter sniff counted delimiters inside quoted header names, so `Date<TAB>"a,b,c"` sniffed as comma and mis-split every row. | `sniffDelimiter` now skips quoted regions, matching `splitCells`. The GUI's `;` extension and the engine's tab-vs-comma tie rule are unchanged. | `externalcolumnfilecore.h` `sniffDelimiter()` | standalone harness (3 new checks) + gtest `ExternalColumnFileCore.SniffDelimiter_IgnoresDelimitersInsideQuotes`; **mutation-tested** (see below) |
| **E-3.6** — the harness's `sniffDelimiter` tie-break check was dead: `"a\tb,c\td"` is 2 tabs vs 1 comma, so `tabs >= commas` was never exercised. | Replaced with a real tie: `"a\tb,c"` (1 vs 1). It fails if the rule is weakened to `tabs > commas`. | `tests/standalone_externalcolumn/main.cpp` | mutation M2 below |
| **B-2 / R7** — the engine's gage and timeseries loaders split the colon by different rules, so a colon in the path yielded two cache keys (two parses, one of the wrong file); the GUI mirrored only the gage rule. | `splitPathColumn` implements the unified rule. Round-trip is now lossless for paths containing colons, and the GUI derives the same path the engine will. | `externalcolumnfilecore.h` `splitPathColumn()` (+ file-header doc) | harness `testPathColumnTokens` — the 5 requested cases (plain relative, `C:\dir\f.csv`, `C:\dir\f.csv:COL`, colon in a directory name, `f.csv:COL`) plus drive-relative `C:rain.csv`, the Windows colon-in-directory variants and compose→split round-trips; gtests `SplitPathColumn_WindowsDriveLetter` (extended) and `SplitPathColumn_ColonInsideADirectoryName`; **mutation-tested** |
| **R6** — observed-data filters omitted `*.tsf`. | Added, one line each. Checked first that this does not expose a broken path: `ObservedCsvRunLayer::load` picks tab via `io::guessDelimiter`, takes the `IDs:` row as labels (its first cell is not a timestamp), drops the parameter/units rows via the `parseRow_` failure `continue`, and `io::tryParseTimestamp` already handles `M/d/yyyy h:mm:ss AP` — so a `.tsf` loads correctly there. `observedcsvrunlayer.cpp` was **not** restructured. | `src/ui/dialogs/comparisonplotdialog.cpp`, `src/ui/dialogs/calibrationdatadialog.cpp` | source review only (**needs local Qt**) |
| **R5** — switching to an Inline/path-less series left the previous series' items in the (disabled) column combo. | The combo is cleared on that branch of `rebindActiveProvider_`. | `timeserieseditordialog.cpp` | source review (**needs local Qt**) |
| **NIT (§D)** — a 78 KB Linux aarch64 ELF sat in `tests/standalone_externalcolumn/`, so `git add` of that directory would commit a binary. | Deleted. Source, fixture and README remain; the README documents the one-line rebuild. | — | `ls` |

### Commands run (Linux sandbox — still no Qt, so no `ctest`, no app build)

| # | Command | Result |
|---|---------|--------|
| 1 | `cd tests/standalone_externalcolumn && c++ -std=c++17 -Wall -Wextra -o standalone_externalcolumn main.cpp && ./standalone_externalcolumn` | **72 checks, `ALL CHECKS PASSED`, exit 0**, zero warnings — rebuilt from the edited source |
| 2 | Mutation matrix against **copies** of `externalcolumnfilecore.h` outside both repos (`~/mutcheck`, restored after each run; no repo file touched) | every new rule is pinned — see table |
| 3 | Re-read of every edited hunk + `grep` of all call sites of the four changed signatures (`readHeaders`, `readColumn`, `loadExternalFileIntoProvider_`, `populateColumnCombo_`) | all call sites updated; the two util signatures grew **defaulted** out-params, so no external caller breaks |
| 4 | Engine ground truth re-read: `MultiColumnSeriesFile.cpp` (header loop, sniff, `find_column`), `CatchmentHandler.cpp:312-331`, `openswmm_gages.h` | confirmed: the engine has **no** header-vs-data probe (basis for the off-by-one direction) and `find_column` never matches column 0 |

Mutation results (each mutation applied alone, then reverted; baseline 0 failures):

| Mutation of the parsing core | Harness result |
|---|---|
| M1 — sniff counts delimiters inside quotes (pre-fix behavior) | 1 FAIL (`sniffDelimiter("Date\t\"a,b,c\"")`) |
| M2 — tie-break weakened to `tabs > commas` | 1 FAIL (`sniffDelimiter("a\tb,c")`) — the previously dead check now bites |
| M3 — revert to the old GUI rule (first colon after drive letter) | 8 FAILs (both colon-in-directory shapes + round-trips) |
| M4 — drop the drive-letter exemption | 1 FAIL (`C:rain.csv`) — the exemption is load-bearing only for drive-relative tokens; the path-separator guard covers `C:\dir\f.csv` |
| M5 — drop the path-separator guard (plain `rfind`, the old timeseries rule) | 4 FAILs |

### Deliberately left alone (with rationale)

- **R3 — no column auto-picked after Browse in the rain-gage attribute table.**
  Left. Verified the mechanics: `FileBrowseDelegate::setModelData` issues exactly
  one `setData`, and `SWMMAttributeTableModel::setData` wraps it in a single
  `AttributeEditCommand(row, col, old, new)` whose `undo()`/`redo()` replay only
  that one cell. Writing a second engine field from inside `commitValueDirect`
  would land outside the command's captured state, so undo would revert the path
  and leave the column — a genuine correctness bug. Doing it properly needs a
  `QUndoStack` macro wrapping two commands at the `setData` level, i.e. a new
  undo-command shape for the table: not low-risk, and the engine's new
  `swmm_gage_set_file_format` is the cleaner lever (an explicit format control)
  whenever this is picked up.
- **A-2's GUI consequence — Station ID stays greyed out once a gage is USER_CSV.**
  The engine now has `swmm_gage_set_file_format`, but wiring a GUI control for it
  (or auto-reverting the format when the column is cleared) is a new UI affordance
  beyond this review's GUI findings. Flagged for a follow-up; the row gate in
  `propertiespanel.cpp` is unchanged.
- **B-8 NIT — the GUI recognizes `;` as a delimiter, the engine does not.** A
  semicolon-delimited file therefore previews with real column names, lets a
  column be picked, and persists a token the engine rejects (its header splits
  to one cell ⇒ ERROR 363). Unchanged because the fix direction is a product
  decision (teach the engine `;`, or drop it from the GUI and lose semicolon
  previews for observed data, where `io::guessDelimiter` also accepts it). This
  is now the **last remaining GUI/engine preview-vs-run divergence** in this
  feature.
- **R4** — no per-row applicability gating in the attribute table (pre-existing
  convention; unchanged).
- **`observedcsvrunlayer.cpp`** — still a second copy of the column-enumeration
  heuristic, per the handoff's hard constraint. Note it keeps the old
  "first line is data" behavior on purpose: it is a plotting overlay, not a
  model reference, so it has no engine to agree with.
- **`TimeseriesRegistry`** — no change beyond inheriting the new split rule. It
  is not a second enforcement point: the dialog is the only place a column can be
  chosen, and fabricated names cannot leave it. A hand-edited `.inp` carrying
  `path:col_2` still round-trips verbatim (and the engine errors on it loudly,
  which is now the intended behavior).
- **CHANGELOG.md** — untouched per CLAUDE.md §5.2; entries suggested below.

### Additional items requiring the user's local macOS Qt build

Everything in the earlier "Requires the user's local macOS Qt build" list still
applies (engine rebuild first, full `SWMMVis` compile, the five `ctest`
targets). New/changed since then:

1. **`ctest -R test_externalcolumnfile`** — now **18** cases (was 12): 4
   added/changed around the headerless + unmatched-selector rules, 1 fabricated-
   flag negative case, and 2 new core cases (colon-in-directory split,
   quote-aware sniff).
2. **`ctest -R test_timeseries_editor_dialog`** — 2 new cases
   (`linkExternalFile_UnmatchedColumnLoadsNothingAndShowsIt`,
   `linkExternalFile_HeaderlessKeepsEmptySelectorAndRefusesOtherColumns`).
   Both drive `QComboBox::findData`/`itemData` semantics that only Qt can
   confirm.
3. **`ctest -R test_timeseries_registry_engine`** — unchanged code, but
   `loadFromEngine_DriveLetterTokenSplitsAfterDrive` now exercises the *unified*
   rule; it should still pass (same expected path/column).
4. **Attribute-table combo for a headerless rain file** — the options list is
   now intentionally empty (source review only).

### Revised manual test steps (supersede steps 4 and 6 of the earlier list)

- **4 (revised) — headerless CSV.** Browse to `tests/unit/data/extcol_noheader.csv`
  (3 lines). Expect: **2** points (line 1 is the header row, as the engine reads
  it), the combo lists `col_1`/`col_2`, the stored selector stays empty, and
  picking `col_2` shows *"…has no header row, so only its first column can be
  selected…"* while the combo snaps back to `col_1` and the data does not change.
- **New — stale column.** Bind a column, then edit the file on disk to rename
  that column, reopen the series. Expect: no points, a status message naming the
  missing column, and a combo item `<name> (not in file)` selected. Saving and
  running must fail on the same reference (that agreement is the point).
- **6 (addendum) — headerless rain gage.** Point a gage's rain file at a
  headerless CSV: the "Rain File Column" combo offers **no** options (empty
  column ⇒ first data column). A multi-column file with real headers is
  unchanged.

### Suggested CHANGELOG entries (additions to the earlier block; not applied)

```
### Fixed
- Time Series editor / rain gages: a column name that a file does not contain
  is now reported instead of silently loading the first column — the preview
  and the simulation agree (previously the editor charted column 1 while the
  run refused to open the file).
- Headerless CSVs: the editor no longer offers a column whose name the engine
  cannot resolve, and it spends the first line on the header row exactly as the
  engine does, so the preview no longer shows one extra row.
- Multi-column file references with a colon in a directory name are now split
  identically by the GUI and both engine readers, so such a file is still read
  only once.
- Delimiter sniffing ignores delimiters inside quoted column names.
- Observed-data file pickers accept PCSWMM `.tsf`.
```

---

## Local verification on the user's machine (macOS arm64, 2026-08-17)

Closes items 1–6 and 8 of the "Requires the user's local macOS Qt build" list
(items 7 and 9 are in-app manual steps and remain for the user). Environment:
macOS 26 / Darwin 25.5, Qt6 via vcpkg, Ninja, Release, `QT_QPA_PLATFORM=offscreen`.

### Prerequisite — engine (item 1): already satisfied

`openswmm.engine/install/Darwin` exports `swmm_gage_get_file_column`,
`swmm_gage_set_file_column`, `swmm_gage_get_file_format` (and
`swmm_gage_set_file_format`) and carries the multicolumn loader, so the GUI
links without a re-install. The engine's own local verification is recorded in
`openswmm.engine/plans/HANDOFF_MULTICOLUMN_SERIES_ENGINE_2026-08-17.md`
("Local verification on the user's machine").

### Build and test results

| Item | Command | Result |
|---|---|---|
| 2 — full app compile | `cmake --build build --target SWMMVis` | **clean** — compiles and links; the only warning is the pre-existing `ld: ignoring duplicate libraries` |
| 3 — `test_externalcolumnfile` | `ctest -R test_externalcolumnfile` | **pass — 18/18 cases** (matches the documented count) |
| 4 — `test_timeseries_parse` | `ctest -R test_timeseries_parse` | **pass — 10/10 cases** |
| 5 — dialog + registry | `ctest -R "test_timeseries_editor_dialog\|test_timeseries_registry_engine"` | **pass after one test fix** — dialog 34/34, registry pass |
| 6 — full-app gui tests + everything else | `ctest -j4` (whole project) | **199/199 pass, 0 failures** (149 gui + 50 unit) — `test_attributetableschema` and `test_selectionops` included, so the new "Rain File Column" schema column broke no existing expectation |
| 8 — standalone parse harness on macOS | `c++ -std=c++17 -Wall -Wextra main.cpp` (binary built outside the repo) | **`ALL CHECKS PASSED`**, 72 checks, exit 0, zero warnings under AppleClang |

A reconfigure (`cmake -S . -B build`) was needed first so the new
`src/ui/util/externalcolumnfile.cpp` / headers entered `PROJECT_SOURCES`.

### The one real defect found — B3 test grabbed the wrong widget

`test_timeseries_editor_dialog::seriesSwitch_RepopulatesColumnCombo` (the B3
regression case) **failed** on first run:
`selectByName("TS_INLINE") returned FALSE`.

Cause: the test located the series list with `dlg.findChild<QListView *>()`.
`buildSourceModeCard_()` parents `m_sourceCard` to the dialog at
`timeserieseditordialog.cpp:1013`, **before** `m_splitter` is created at
`:472`→`buildListPane_()`, so the depth-first `findChild` walks the source-mode
card first and returns the **column combo's popup view** — a `QListView` whose
items are `rain_a` / `rain_b`, not the series names. The test was therefore
searching the combo for series names and could never pass.

Fix (test-only, `tests/gui/test_timeseries_editor_dialog.cpp`): select the view
whose model is the `QSortFilterProxyModel` — the series list is the only
`QListView` in the dialog driven by a proxy model. 34/34 after the fix.

This is the sort of thing only a Qt build can catch: the production B3 fix
itself is sound (the case asserts combo repopulation, item order, the selected
index and `currentData() == provider.columnSelector()`, and all four now hold).
No production code needed to change.

### Still requires the user, in the running app

Manual steps 7 and 9 of the earlier list — in-app dialog behavior and the
property-panel ↔ attribute-table MVC sync — plus the revised steps 4/6 and the
"stale column" step from the review-fixes section. Nothing automated can
substitute for those; everything they depend on now compiles and its
non-interactive half is covered by the 199 green tests.

Also unchanged: the open risks R1–R7 and the "deliberately left alone" list are
untouched by this pass, and `CHANGELOG.md` is still unedited (entries suggested
above).

**Note:** a stale zero-byte `.git/index.lock` is present in this repo too;
`rm .git/index.lock` before your first git write.

### Second pass — all failures fixed, suite now 200/200

Re-run after the engine-side fixes (see the engine handoff's "Pre-existing
failures also fixed"). The GUI tree moved under this pass: another session
committed `38f469d feat(sectionview): …` and added
`tests/gui/test_raingagefilecolumn.cpp` at 08:35.

| Symptom | Verdict |
|---|---|
| `test_nonspatial_adapters` **link failure**: undefined `openswmmvis::ui::reconcileColumnSelector(QString const&, QString const&)`, referenced from `SWMMRainGagePropertyAdapter::setFilePath` | **Real, fixed.** The concurrent session added `reconcileColumnSelector` (`src/ui/util/externalcolumnfile.cpp:222`) and called it from the rain-gage adapter, but that test target compiles the adapter without the util. Added `src/ui/util/externalcolumnfile.cpp` + `src/io/timeseriesparse.cpp` to the target (`tests/gui/CMakeLists.txt`) — the same dependency the two dialog targets already carry. |
| `test_selectionbeacon`, `test_raingagefilecolumn` failing under `ctest -j4` | **Not defects** — both pass in isolation; the other session was relinking those binaries while the parallel run was in flight. Green after a quiet rebuild. |

Final: **`ctest -j4` → 200/200 pass, 0 failures** (150 gui + 50 unit), full
`SWMMVis` build clean.

**Caveat for the user:** the GUI still links `openswmm.engine/install/Darwin`
from 07:14. The engine-side Api2D coupling fixes are **not** in that install —
re-install the engine when your concurrent water-age / species-ID work settles.
Nothing in this change set needs it (the GUI is green as-is).

---

## Third pass — re-verification against the moved tree (2026-08-17, later)

The second pass's 200/200 was recorded before the tree moved again: `38f469d
feat(sectionview)` is now HEAD, and a concurrent session edited
`src/ui/util/externalcolumnfile.cpp` (08:41) and `tests/unit/test_externalcolumnfile.cpp`
(08:33) after that run. This pass re-verifies the handoff against the tree **as it
now stands**. **No production code was changed.**

| Check | Command | Result |
|---|---|---|
| Reconfigure | `cmake -S . -B build` | exit 0 |
| Full build | `cmake --build build -j 6` | exit 0, **0 errors**; only the pre-existing warnings |
| Full suite | `QT_QPA_PLATFORM=offscreen ctest -j 4` | **200/200 pass, 0 failures** (150 gui + 50 unit) |
| Handoff targets | `ctest -R "test_externalcolumnfile\|test_timeseries_parse\|test_timeseries_editor_dialog\|test_timeseries_registry_engine\|test_raingagefilecolumn\|test_attributetableschema\|test_nonspatial_adapters"` | **7/7 pass** |
| Prereq (item 1) | `nm -gU install/Darwin/lib/libopenswmm*.dylib` | all four `swmm_gage_*_file_{column,format}` exported — GUI links, no engine re-install needed |

Case counts drifted upward from the second pass, consistent with the concurrent
session's additions: `test_externalcolumnfile` **22** cases (was 18),
`test_timeseries_parse` **10** (unchanged).

### Tasks 1–5 confirmed present in source (not merely recorded)

| Task | Evidence in the current tree |
|---|---|
| 1 — shared util | `externalcolumnfile.h:53/78/102` (`readHeaders`, `readColumn`, `reconcileColumnSelector`); `externalcolumnfilecore.h:114/202/220/247` (`sniffDelimiter`, `splitPathColumn`, `composePathColumn`, `fabricatedColumnName`) |
| 2 — dialog | `.tsf` in the Browse filter (`:1308`) and tooltip (`:1065`); **B3** fixed — `rebindActiveProvider_` now repopulates (`:1991`, with the explanatory comment at `:1970`) instead of passing `columnHeadersOut = nullptr` |
| 3 — persistence (B4) | `timeseriesregistry.cpp` reads/writes `SWMM_FILE_TIMESERIES_DATA` through `splitPathColumn`/`composePathColumn` on both the load (`:118-123`) and save (`:211-228`) paths |
| 4 — rain gage | `Q_PROPERTY(QString fileColumn …)` (`swmmraingagepropertyadapter.h:86`); `swmm_gage_set_file_column`/`get_file_format` bound (`.cpp:262/287/296`); `setRowEditable(…, "fileColumn", isFile)` (`propertiespanel.cpp:1076`); table column `"Rain File Column"` as `EditorKind::FileColumn` with `kFileColumnOptionsRole` and the `gage_file_column` dispatch entry (`swmmattributetablemodel.cpp:860-872/1673/2231`) |
| 5 — colon never typed | unchanged; the colon is produced/consumed only inside `composePathColumn`/`splitPathColumn` |

### Still open — unchanged by this pass

Nothing automated remains. What is left is exactly what earlier passes flagged:
in-app manual steps 7 and 9, the revised steps 4/6 and the "stale column" step;
the deliberately-left items **R3** (no column auto-picked after Browse — needs a
two-command undo macro), **A-2** (`swmm_gage_set_file_format` present in the
engine but not consumed by any GUI control), **B-8 NIT** (the GUI accepts `;` as
a delimiter, the engine does not — the last preview-vs-run divergence),
**R4**, and the `observedcsvrunlayer.cpp` duplication. `CHANGELOG.md` is still
unedited; entries are suggested in the two blocks above.

The change set remains **uncommitted**, per this handoff's hard constraint.
