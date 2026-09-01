# Single-Read Multi-Column Series Files (CSV/TSV + TSF) & Implicit GUI Column Selection

Status: **vetted specification — ready for implementation** (supersedes the unvetted draft `multicolumn_series_single_read.md`).
Date: 2026-08-17. Scope: `openswmm.engine` (loader, C-API, python) + `openswmm.gui` (column pickers, persistence).
Mirrored at `openswmm.gui/workplans/MULTICOLUMN_SERIES_SINGLE_READ_2026-08-17.md` — keep both copies in sync.

Decisions locked with the user (2026-08-17):
- **D1 — Eager parse-once cache.** Parse each file once at resolve time, copy columns into each consumer's existing `x/y` arrays, free the cache. Do **not** wire the dormant streaming reader in `TableData.cpp` (see §1.3); leave it untouched with a code comment pointing here.
- **D2 — PCSWMM `.tsf` is in scope** (engine + GUI).

## 1. Verified current state

An earlier claim that single-read multi-column support "is not feasible" is **wrong**. Multi-column *selection* already works for rain gages; what is missing is the single read, the timeseries-side column selection, `.tsf`, and all GUI surfacing/persistence.

### 1.1 What already works (engine, tested)
- `[RAINGAGES] … FILE "rain.csv:COLUMN"` parses (`src/engine/input/handlers/CatchmentHandler.cpp:317-331`, drive-letter-aware colon split), stores `col_name` (`src/engine/data/GageData.hpp:133`), sets `RainFileFormat::USER_CSV = 6` (`GageData.hpp:74`), writes back `FILE "path:col"` (`src/engine/core/InpWriter.cpp:1030-1040`).
- User-documented: `docs/manuals/user/manual/AppendixD.md:267`, `Chapter13.md:481-503` (§13.9).
- Tests pass: `tests/unit/engine/test_gage_rain_series.cpp` (`UserCsvGageLoadsItsColumn`, `UserCsvColumnsAreIndependent`), `test_gage_file_roundtrip.cpp:147-161`, fixture `tests/unit/engine/data/rain_series/rain_multi.csv`.
- Every runtime consumer reads a gage/timeseries through one accessor — `openswmm::gage::gageRainSeries` (`src/engine/hydrology/Gage.cpp:130-143`) returns `&gages.rain_series[g]` (FILE) or `&tables[ts]` (TIMESERIES) — so a parse-once/copy-out refactor needs **no runtime changes**.

### 1.2 Confirmed gaps
| # | Gap | Evidence |
|---|-----|----------|
| P1 | No cache: `load_external_rain_files` (`src/engine/input/PostParseResolver.cpp:400`) loops per gage (`:412`) → `load_rain_file_user_csv` (`:303`) `fopen`s per call (`:308`). N gages on one file ⇒ N full scans (stats loop `:346-366` has no early exit). Same per-table re-read in `load_external_timeseries_files` (`:120`, fopen `:155`). Co-gage dedup (`:1084`) is TIMESERIES-only. `swmm_gage_reload_rain_files` (`src/engine/core/openswmm_gages_impl.cpp:365-371`) re-triggers the whole pass. |
| P2 | `char line[4096]` (`PostParseResolver.cpp:317`) + `fgets`: a wide row is silently *split* across reads (tail becomes a bogus row counted into `unparsed_rows` `:353`); a wide **header** spills into the first data read and shifts column indices. Related small buffers: `:180` (`line[256]`, timeseries), `:476-477` (STAN_PRCP). |
| P3 | `[TIMESERIES] name FILE "file:col"`: `:col` suffix is stripped and **discarded** (`PostParseResolver.cpp:139-144`); loader accepts only whitespace 3-token `date time value` (`:193 sscanf`), so a comma CSV yields an **empty series silently** (`:160` "Skip silently"). The suffix survives in `Table::file_path` for round-trip (`:215-217`) but is inert. Note: the `:col` form is only a source doc-comment (`src/engine/input/handlers/TablesHandler.cpp:29-33`); the user manual's `[TIMESERIES]` section does **not** document it — add docs when implemented. |
| P4 | No `.tsf` support anywhere in either repo (repo-wide grep). |

### 1.3 Existing assets the draft plan missed
- **Dormant multi-column reader (dead code):** `src/engine/data/TableData.{hpp,cpp}` ships a complete header-aware, offset-indexed, sliding-cache file reader — `table_open_file` (`cpp:417`), `column_ids`/`column_map` (`hpp:167-168`), `table_lookup_column` (`cpp:657`), `table_step_column` (`cpp:710`) — with **zero callers**. Per D1 we do not wire it; per repo guidelines do not delete it either. Add a comment at `table_open_file` noting this plan chose the eager path and why (runtime consumers copy into `x/y`; streaming would change the lookup path).
- **Two datetime parsers already exist:** `csv_parse_datetime` (`PostParseResolver.cpp:279-301`; ISO + 24-h US, no AM/PM) and the dead reader's `parse_csv_datetime` (`TableData.cpp:94-115`). This plan relocates/extends the former (§3.1); leave the latter untouched.
- **Master-plan lineage:** this closes `plans/MASTER_IMPLEMENTATION_PLAN.md` items R10 (`:67`) / P3-T09 (`:715`) — "Multi-column CSV time series (name:COLUMN_NAME) … CSV reader TODO". Tick them on completion.

### 1.4 Pre-existing bugs that are prerequisites
| # | Bug | Evidence |
|---|-----|----------|
| B1 | **Data loss:** `swmm_gage_set_filename` and `swmm_gage_set_station_id` unconditionally force `file_format = STAN_PRCP` (`openswmm_gages_impl.cpp:140`, `:152`), destroying a USER_CSV gage's column reference. The GUI calls `set_station_id` from `swmmraingagepropertyadapter.cpp:184`, so editing Station ID in the GUI silently corrupts USER_CSV gages **today**. |
| B2 | Empty `col_name` is documented as "use the first/only data column" (`GageData.hpp:128-132`) but hard-errors (`PostParseResolver.cpp:330` rejects `col <= 0` → `ERR_RAIN_FILE_FORMAT`). |
| B3 | GUI timeseries dialog: on series switch the column combo is never repopulated (`timeserieseditordialog.cpp:1896` passes `columnHeadersOut = nullptr`; enable gate `:1168` then leaves it disabled/stale); after Browse (`:1267`) the combo shows item 0 while `columnSelector` stays `""` (display/state divergence). |
| B4 | **GUI file-backed timeseries are never persisted.** `TimeseriesRegistry::saveToEngine` skips every non-inline provider (`src/timeseries/timeseriesregistry.cpp:137`), and providers are absent from project serialization — a chosen path+column is silently dropped on save. |

## 2. Goals & non-goals

**Goals**
- Read each external series file **exactly once** per resolve pass, shared across the rain-gage and timeseries load passes, for CSV/TSV and TSF.
- Remove the fixed-width line limit (arbitrary column count/row width).
- Make `[TIMESERIES] FILE "file:col"` actually select the named column (P3), and error loudly — not silently — on unparseable files.
- Add `.tsf` parsing (engine + GUI): tab-delimited, 3-row `IDs:` header, 12-hour AM/PM datetimes.
- Fix B1–B4.
- Expose gage `col_name` (and `file_format`) through the C-API and python bindings.
- GUI: implicit column selection (user never types the colon) for **both** timeseries and rain gages; `.tsf` in file filters; **persist** file-backed timeseries to the engine using the `path:col` convention so the `.inp` round-trips and other dependencies (gage `seriesName`, inflows, outfalls — all of which reference a *named* timeseries via `DataObjectRef`) can depend on a column of a shared file through its named timeseries.

**Non-goals**
- No `.inp` grammar change — `FILE "path:col"` stays the on-disk convention (matches `MASTER_IMPLEMENTATION_PLAN.md:1928` design decision).
- No change to the legacy `STAN_PRCP` path or single-series `.dat` files (back-compat).
- No wiring of the dormant `TableData` streaming reader (D1); no deletion of that dead code.

## 3. Engine design

### 3.1 New unit: `src/engine/input/MultiColumnSeriesFile.{hpp,cpp}`
Pure, unit-testable parser + cache (eager, per D1).

```cpp
struct ParsedSeriesFile {
    std::vector<double>               dates;      // row datetimes (whole file)
    std::vector<std::string>          headers;    // col 0 = time label, 1..N = names
    std::vector<std::vector<double>>  columns;    // columns[i] aligned to dates (i>=1)
    double first_date = 0.0, last_date = 0.0;
    int  find_column(std::string_view name) const; // case-insensitive; -1 if none/col0
};

// Parse once; format auto-detected. Dynamic lines (std::ifstream + std::getline —
// replaces the fixed fgets buffers, killing P2).
bool parse_multicolumn_series_file(const std::string& abs_path,
                                   ParsedSeriesFile& out,
                                   std::vector<std::string>& errors);

class MultiColumnFileCache {                 // keyed by resolved absolute path
    std::unordered_map<std::string, ParsedSeriesFile> files_;
public:
    const ParsedSeriesFile* get_or_parse(const std::string& abs_path,
                                         std::vector<std::string>& errors);
    int parse_count() const;                 // test hook for the single-read assertion
};
```

**Format detection**
- **TSF**: first line begins with `IDs:` → tab-delimited PCSWMM. Column names = line-1 tokens 1..N; skip line 2 (`Date/Time`/`Rainfall`) and line 3 (units, e.g. `in.`); data rows `datetime \t v1 \t v2 …`.
- **CSV/TSV**: sniff delimiter (comma vs tab) from the header row; row 1 = headers, column 0 = time.

**Datetime parsing** — move `csv_parse_datetime` (`PostParseResolver.cpp:279-301`) into this unit and extend it with **12-hour `MM/DD/YYYY hh:mm:ss AM/PM`** (required by TSF), keeping ISO-8601 and 24-hour US. While relocating, fix the `n<5` asymmetry at `:296` (mirror the ISO branch's zeroing). Do not touch the duplicate in `TableData.cpp` (dead code).

**Raw values only.** The cache stores unconverted values in file units. Each consumer applies its own transform on copy-out: STAN_PRCP keeps its baked quantization/INTENSITY/CUMULATIVE handling (`PostParseResolver.cpp:458-511`, unchanged); USER_CSV keeps deferring to `convertGageValue` (`Gage.cpp:161-181`). **Optional, flagged off by default:** honor the TSF units row for rainfall conversion.

**Per-column stats** — `first_date`, `last_date`, per-column `periods_precip` over the whole file (preserves the "Rainfall File Summary").

### 3.2 File-centric loaders (single read)
In `PostParseResolver.cpp`:
- Create **one** `MultiColumnFileCache` for the whole resolve pass; pass it to both loaders so a file referenced by gages *and* timeseries parses once. This automatically fixes the `swmm_gage_reload_rain_files` re-read path too.
- `load_external_rain_files`: per USER_CSV gage → `cache.get_or_parse(abs)`, `find_column(col_name)`; **empty `col_name` selects the first data column (fixes B2)**; copy into `gages.rain_series[g]` applying the sim window `[win_lo, win_hi]` and the existing sort (`:378-390`); set `file_first_date/last_date/periods_precip`.
- `load_external_timeseries_files`: if the FILE token carries `:col` **or** the file is detected as multi-column → cache + column select (fixes P3); otherwise keep the legacy 3-column whitespace path. Replace the silent-skip (`:160`) with a reported error/warning when a referenced file parses to zero rows.
- Free the cache at end of pass (columns are copied into each consumer's `x/y`; `Table::file_path` keeps the original token for round-trip, as today `:215-217`).

### 3.3 Data / writer
- `RainFileFormat::USER_CSV = 6` (`GageData.hpp:74`) now means "multi-column text file, column-by-name (CSV/TSV/TSF)"; format auto-detected from content. **No `USER_TSF` enum** — auto-detection keeps the `.inp` unchanged.
- `InpWriter.cpp:1030-1040` (gages) unchanged; timeseries emit (`:1864-1868`) already round-trips the verbatim token — verify with the existing round-trip tests and add a column-form case.
- Fix **B1**: `swmm_gage_set_filename` must preserve/auto-detect the format instead of forcing `STAN_PRCP`; `swmm_gage_set_station_id` must not touch `file_format` at all.

### 3.4 C-API + python (needed by the GUI)
- New: `swmm_gage_get_file_column` / `swmm_gage_set_file_column` (reads/writes `col_name`; setting a non-empty column on a FILE gage implies `USER_CSV`), and `swmm_gage_get_file_format` (read-only is sufficient for row gating). Declare in `include/openswmm/engine/openswmm_gages.h`, implement in `src/engine/core/openswmm_gages_impl.cpp`, bind in `python/openswmm/engine/_gages.pyx` (`set_file(path, station_id)` gains a `column` overload or sibling).
- Timeseries side needs no new API: the GUI writes the composed `path:col` token through the existing `swmm_file_path_set(…, SWMM_FILE_TIMESERIES_DATA, …)` registry (role 10; gage data is role 9, `openswmm_model.h:326`).

### 3.5 Engine files to touch
- **NEW** `src/engine/input/MultiColumnSeriesFile.{hpp,cpp}`
- `src/engine/input/PostParseResolver.cpp` — rewrite `load_external_rain_files` / `load_rain_file_user_csv` / `load_external_timeseries_files` around the cache; relocate datetime parsing.
- `src/engine/core/openswmm_gages_impl.cpp` + `include/openswmm/engine/openswmm_gages.h` + `python/openswmm/engine/_gages.pyx` — B1 fix + new column accessors.
- `src/engine/data/GageData.hpp` — doc update only.
- `src/engine/data/TableData.cpp` — comment on the dormant reader only (no functional change).
- `docs/manuals/user/manual/AppendixD.md` + `Chapter13.md` — document the `[TIMESERIES] FILE "path:col"` form and `.tsf`.
- `CHANGELOG.md` — entry on release (per CLAUDE.md §5.2; note the shipped USER_CSV feature was never logged — log both).

## 4. GUI design (openswmm.gui)

The MVC path is confirmed and absorbs this cleanly: engine is the single source of truth; adapters (`Q_PROPERTY` → C-API) fan out via `changed()` → `objectEdited` → `swmmvis.cpp:3115-3143`. The blockers are the missing engine C-API (§3.4) and persistence (B4), not architecture.

**Tasks (order matters):**

1. **Shared util** `src/ui/util/externalcolumnfile.{h,cpp}` — `readHeaders(path)` + `readColumn(path, col)` for CSV/TSV/**TSF**. Build on the existing shared primitives in `io/timeseriesparse.{h,cpp}` (`guessDelimiter`, `tryParseTimestamp`, `parseRow`): add an AM/PM format to `tryParseTimestamp` and `IDs:` 3-row-header handling. Column *enumeration* is currently duplicated in `timeserieseditordialog.cpp:1423-1489` and `observedcsvrunlayer.cpp` — extract it here so the rain-gage editor doesn't become a third copy. Mirror the engine's detection rules exactly.
2. **Timeseries dialog** — switch `loadExternalFileIntoProvider_` to the shared util; add `*.tsf` to the filter (`timeserieseditordialog.cpp:1263`); **fix B3** (repopulate the combo on series switch by passing real `columnHeadersOut`; after Browse, initialize `columnSelector` to the combo's shown item).
3. **Persistence (fixes B4, enables the convention):** `TimeseriesRegistry::saveToEngine` must write file-backed providers to the engine as `FILE` timeseries with the composed `path:col` token (via `swmm_file_path_set(…, SWMM_FILE_TIMESERIES_DATA, …)` keyed by series name), instead of skipping them (`timeseriesregistry.cpp:137`). On model load, FILE timeseries with a `:col` suffix become file-backed providers with `columnSelector` parsed out. This is what lets *other dependencies* (gage `seriesName`, inflows, outfalls — all `DataObjectRef`/name-based) reference a specific column of a shared file through its named timeseries, with the engine reading the file once.
4. **Rain-gage FILE source — implicit column picker:**
   - Attribute table (`swmmattributetablemodel.cpp:846-894`): today "Rain File (path)" is `EditorKind::Text` with no browse and no filters. Add a file-browse editor with `*.csv *.tsv *.tsf *.txt *.dat` filters and a "Rain File Column" combo column populated from `readHeaders`; getter/setter via the new `swmm_gage_get/set_file_column` (registration pattern at `:1641-1660`).
   - Property adapter (`swmmraingagepropertyadapter.{h,cpp}`): add `Q_PROPERTY(QString fileColumn …)` → new C-API; ensure the Station ID setter path no longer corrupts format (engine B1 fix).
   - Properties panel (`propertiespanel.cpp:1061-1077`): add `setRowEditable(pm, gageAdapter, "fileColumn", isFile)` alongside the existing four rows; show Station ID only for STAN_PRCP-format gages (use `swmm_gage_get_file_format`).
5. The GUI always composes and stores `path:col` — the user never types the colon ("implicit" requirement).

## 5. Single-read guarantee
One shared `MultiColumnFileCache` per resolve pass, consumers resolved against it by absolute path ⇒ each file parsed once for any number of gages/timeseries, CSV or TSF. Target complexity **O(Σ unique file bytes)**, not O(consumers × bytes). Assert via `parse_count()` in tests.

## 6. Test plan

**Engine (gtest)**
- Wide CSV (≈2000 columns, rows > 4096 B) loads uncorrupted; header wider than 4 KB keeps correct column indices (kills P2).
- N gages + M timeseries referencing one file ⇒ `parse_count() == 1` (kills P1).
- TSF fixture with AM/PM datetimes → correct 24-hour times + correct column (kills P4).
- `[TIMESERIES] FILE "f.csv:COL"` selects the column; comma CSV without `:col` no longer yields a silent empty series (kills P3).
- Regression B1: `swmm_gage_set_station_id` / `set_filename` on a USER_CSV gage preserves `file_format` and `col_name`; round-trip still emits `path:col`.
- Regression B2: empty `col_name` loads the first data column.
- Round-trip: writer emits `FILE "path:col"` for gage **and** timeseries; re-parse equal (extend `test_timeseries_file_roundtrip.cpp`, `test_gage_file_roundtrip.cpp`).
- Sim-window filtering and existing `rain_us.inp` fixtures still pass; Rainfall File Summary values unchanged for existing fixtures.

**GUI**
- Open a `.tsf` in the timeseries dialog → combo lists the IDs; selecting one loads that column.
- Regression B3: reopening a file-backed series repopulates + enables the combo; Browse leaves combo and `columnSelector` consistent.
- Persistence B4: file-backed series with a column → save → `.inp` contains `name FILE "path:col"` → reload → provider restored with the same column; a gage referencing that series by name still resolves.
- Rain gage: pick file + column in property panel → attribute table shows the same values (MVC sync); `.inp` writes `path:col`; editing Station ID on another gage doesn't clobber it.

## 7. Acceptance criteria
- A 1543-gage model backed by a single wide CSV **or** TSF loads with non-zero rainfall, file read **once** (asserted), no width truncation.
- Timeseries `FILE "path:col"` works end-to-end and is user-documented.
- Both editors offer implicit column selection including `.tsf`; the chosen column survives save/reload; other model objects can depend on a column via its named timeseries.
- B1–B4 have regression tests.

## 8. Sequencing
1. Engine B1 + B2 fixes (+ regressions) — small, unblocks GUI safely.
2. `MultiColumnSeriesFile` unit + parser tests (CSV/TSV/TSF, AM/PM, wide rows).
3. Wire both loaders to the shared cache; P3 column selection; loud errors; single-read + round-trip tests.
4. C-API/python column accessors.
5. GUI shared util (+ AM/PM + TSF in `timeseriesparse`) + `.tsf` filters + B3 fixes.
6. GUI persistence (B4) via `path:col` through the file-path registry.
7. Rain-gage browse + column picker (adapter, table, panel).
8. Integration tests; docs (AppendixD/Chapter13); tick MASTER_IMPLEMENTATION_PLAN R10/P3-T09; CHANGELOG on release.
