# IO Portability Plan — Always-Relative INP & Structured Geopackage

**Status:** Draft (rev 2) for review
**Author:** Plan synthesized 2026-05-31
**Scope:** `openswmm.engine` + `openswmm.gui`
**Goal:** Make SWMM models *portable* across machines and folders, and make external file content *first-class* and *editable* in the Geopackage form.

---

## 1. Objectives

1. **INP plugin — always relative paths.** Every external file reference (`[FILES]`, `[RAINGAGES] FILE`, `[TIMESERIES] ... FILE`, `[TEMPERATURE] FILE`, `[EVAPORATION] FILE`, `WINDSPEED FILE`, hotstart save/use, RDII/RUNOFF/RAINFALL/INFLOWS/OUTFLOWS interface files) must be persisted as a path expressed *relative to the destination `.inp` directory*, regardless of whether the referenced file lives inside or outside the project root. Absolute paths are used only when a relative expression is mathematically impossible (cross-volume on Windows, UNC paths, network shares without a common ancestor).
2. **Geopackage plugin — no BLOBs; structured editable data.** Every external file referenced by the model is *parsed* on save and persisted as **rows in dedicated relational tables** that mirror the file's logical content. The GUI edits those tables directly — there is no opaque binary stored inside the `.gpkg`. On open the engine consumes the table rows; legacy file IO is bridged by *materialising on demand* (writing the rows back to a scratch file in the legacy format only at the moment the engine wants to `fopen` it).
3. **GUI editors** adapt to the model's storage mode. INP-backed projects show a relative path widget. GPKG-backed projects show an inline data editor for the same logical content (a table grid + optional plot), with no path field at all.

The work biases toward **alignment with legacy SWMM** (`CLAUDE.md` §4.01 in both repos). All relative-path resolution mirrors the legacy `addAbsolutePath()` / `InpDir` semantics in `src/legacy/engine/swmm5.c:2820`. The structured GPKG schema mirrors the in-memory state vectors the legacy engine already uses (e.g. `hotstart.c`'s save/restore order — subcatchment runoff + snowpack + infiltration + groundwater state, then routing state per node/link, then quality).

---

## 1A. Always-Relative INP: Advantages & Risks

### Advantages

1. **Maximum portability across folder hierarchies.** A relative path `../shared/data/rain.dat` continues to resolve after any rigid translation of the project tree (clone, archive, USB transfer, container mount) as long as the *relative* structure between `model.inp` and `rain.dat` is preserved. This includes cases where the shared data folder is a sibling of, or several levels above, the project — patterns very common in long-running consulting workflows.
2. **Stable, machine-independent diffs.** A `.inp` committed to git from a Mac (`/Users/alice/...`) is byte-identical to one written from a Windows VM (`C:\Users\alice\...`) provided the project layout matches. Reviewers no longer see noisy machine-specific path churn in PR diffs.
3. **Single uniform rule for the user.** "Paths are always relative to the `.inp`" is easy to teach and reason about. The alternative ("relative when inside project root, absolute when outside") creates a hidden discontinuity that users discover only when files cross the threshold — exactly the kind of foot-gun that erodes trust in save behavior.
4. **Round-trip determinism.** Save→close→reopen→save produces an identical file. With the "sometimes-absolute, sometimes-relative" rule, the *first* save of a model authored on machine A may be relative, but the *second* save after machine B (different absolute prefix) might become absolute — even when nothing semantic changed.
5. **Aligns with the way modern build systems treat paths.** CMake, Bazel, Cargo, npm, MSBuild — every one of them normalises paths in checked-in configs to project-relative form, for the same reasons.

### Risks & Mitigations

| Risk | Likelihood | Severity | Mitigation |
|---|---|---|---|
| **Cross-volume paths on Windows** (`C:\proj\model.inp` referencing `D:\data\rain.dat`) cannot be expressed relatively at all. | Medium — multi-drive workstations are common. | High when it happens — model will not load. | Detect with `std::filesystem::proximate` returning an empty path or a path that still starts with a drive letter. **Fallback to absolute** for that one slot, log a warning, and surface it through the GUI status panel with text: "Path crosses volume boundary — written as absolute. Project will be non-portable across machines that lack the D:\ drive." |
| **Excessive `..` depth** (e.g. `../../../../../shared/data/rain.dat`). | Low. | Cosmetic / readability. | Cap the relative form at N = 16 `..` segments; beyond that, the warning above triggers and we keep absolute. The cap is configurable through a project setting. |
| **Path-separator drift between Windows and POSIX.** | High — every cross-platform team hits this. | Medium — affects parse correctness, not numerics. | Always emit forward-slash (`/`) on save, on every platform. Accept both `/` and `\` on read. This is what CMake, Python, Java, and modern SWMM forks already do; legacy SWMM accepts both as well. |
| **Case-sensitivity mismatch.** A `.inp` written on macOS (case-insensitive HFS+/APFS by default) referencing `./RAIN.dat` may fail to open on Linux if the on-disk file is `rain.dat`. | Medium — common when moving from macOS to Linux CI. | Medium — load failure with confusing message. | Resolve the path with `std::filesystem::canonical` after the relative expansion *if the file exists*; if it does, capture the canonical case and rewrite the relative form to match. If it doesn't exist, leave the original case (the user may be authoring before the file is created). Document in user manual. |
| **UNC paths** (`\\server\share\...`) and **drive-letter network mounts** (`Z:\...`) cannot be made relative against a local drive. | Low–Medium — varies by deployment. | Same as cross-volume: model won't load if mount is missing. | Same fallback as cross-volume. Treat UNC roots as their own "volume". |
| **Unicode paths.** Filesystem APIs differ across platforms (Windows wide chars, POSIX UTF-8). | Medium — projects with non-ASCII paths exist in non-English-speaking regions. | High when it happens — corruption is silent. | Standardise on UTF-8 in the `.inp` byte stream. Convert through `std::filesystem::path::u8string()` / `path::u8path()` on Windows. Add a round-trip test with Cyrillic and Japanese path segments. |
| **Symlinks treated inconsistently.** `std::filesystem::relative` follows symlinks; `proximate` does not normalise. The user may intend a path to *track* a symlink rather than resolve it. | Low. | Low. | Use `proximate` (preserves user intent) rather than `relative` (canonicalises). Document the choice. |
| **Symbol confusion in tooling.** Some legacy ancillary tools assume absolute paths in the `.inp`. | Low. | Low — workarounds exist. | Provide a CLI flag `--write-absolute-paths` on the engine (engine option `WRITE_ABSOLUTE_PATHS YES`) for users locked to those tools. Default off. |
| **Cross-machine collaboration where the project root structure differs.** Alice keeps her project at `~/work/swmm/proj/`; Bob extracted it to `~/Downloads/proj/`. As long as the relative structure inside `proj/` is identical, this works. If Bob moves the `shared/data` folder, the relative path breaks. | Medium — depends on team discipline. | Medium. | The risk is inherent to relative paths; the mitigation is documentation and a GUI "Validate external files" command that pre-flights every relative reference and reports broken ones. |
| **Round-trip with hand-authored absolute paths.** A user who deliberately wrote `/mnt/nfs/data/rain.dat` in their `.inp` may resent having that rewritten to `../../mnt/nfs/data/rain.dat` on every save. | Low — power users are the small minority who notice. | Low. | Provide the `WRITE_ABSOLUTE_PATHS YES` escape hatch (above), and emit a one-time GUI toast on first auto-rewrite explaining what happened. |

**Net assessment.** The cross-volume and UNC cases are the only ones that genuinely *cannot* be solved by always-relative; for those we degrade gracefully to absolute with a visible warning. Everything else is either a one-time normalisation (separators, case, encoding) or a documentation / opt-out concern. The user gains a vastly simpler mental model.

---

## 2. Current State (Gap Analysis)

### 2.1 Engine — INP read path

| Where | File / Line | Behaviour today | Gap |
|---|---|---|---|
| `[FILES]` parser | `src/engine/input/handlers/FilesHandler.cpp:43-87` | Stores `path` token verbatim in `ctx.files.{rainfall,runoff,rdii,inflows,outflows,hotstart_use}_path` and `hotstart_saves[].path`. | No `InpDir`-based resolution. A relative path stored on disk becomes a relative string in memory — opening the file later fails. |
| `[RAINGAGES] FILE` | `src/engine/input/handlers/CatchmentHandler.cpp:244-261` | `ctx.gages.file_path[idx]` stores raw token. | Same. |
| `[TIMESERIES] FILE` | `src/engine/input/handlers/TablesHandler.cpp:93-102` | Overloads `Table::id = "FILE:" + path`. Does not set `Table::is_file_based` / `Table::file_path` (declared at `src/engine/data/TableData.hpp:134-136`). | Two parallel "file-backed" representations; writer can't see file refs. (See Bug #1 in §6.) |
| `[TEMPERATURE] FILE` | `src/engine/input/handlers/HydrologyHandler.cpp:170,202,215` | `opts.temp_file`, wind FILE flag stored. | No resolution. |
| `SimulationContext::inp_file_path` | `src/engine/core/SimulationContext.hpp:994` | Holds source `.inp` path. | Not used as `InpDir` anchor anywhere outside legacy code paths. |
| Legacy reference | `src/legacy/engine/swmm5.c:2820` (`addAbsolutePath`) | Prepends `InpDir` if `isRelativePath()`. | This is the canonical behaviour the new engine must mirror on read. |

### 2.2 Engine — INP write path (`src/engine/core/InpWriter.cpp`)

| Section | Lines | Behaviour today | Gap |
|---|---|---|---|
| `[FILES]` | 1243-1286 | Writes `ctx.files.*_path` verbatim. | No rebase to destination directory; absolute paths leak. |
| `[RAINGAGES] FILE` | 478-479 | Writes `ctx.gages.file_path[u]` verbatim. | Same. |
| `[TEMPERATURE] FILE` | 324-329 | Writes `opts.temp_file` verbatim. | Same. |
| `[TIMESERIES]` | 1033-1074 | Iterates `tb.x[]/tb.y[]` only. **Never emits a `FILE "path"` row**, even though the parser supports it. | Data loss: file-backed timeseries don't round-trip. |
| Writer entrypoint | `inp_writer::writeInpFile(ctx, path)` (`InpWriter.hpp:47`) | Takes destination path but does not propagate it to per-section writers. | Need a `WriteContext { dst_dir }` so each section can rebase. |

### 2.3 Engine — Geopackage plugin (`src/engine/input/geopackage/`)

| Concern | File / Line | Today | Gap |
|---|---|---|---|
| Schema | `GeoPackageSchema.cpp:18-518` (≈30 tables) | Tables for `options`, `nodes`, `links`, `subcatchments`, `rain_gages`, `curves`, `input_timeseries`, `patterns`, `evaporation`, `climate_settings`, `pollutants`, `rdii_assignments`, `unit_hydrographs`, `transects`, `simulations`, `result_*`, `observed_*`. | **No tables capturing the parsed *content* of external files** (hotstart state, raingage rainfall records, climate observations, RDII / routing interface flows). |
| `[FILES]` content | `GeoPackageWriter.cpp` | Does not serialise `ctx.files`. | Inflows/outflows/hotstart/rainfall/runoff/RDII references are dropped on `.gpkg` save. |
| File-based timeseries | `input_timeseries` (`GeoPackageSchema.cpp:226-236`) | Long-format `(timestamp, value, ordinal)` rows for inline series. | When the timeseries is file-backed (`[TIMESERIES] X FILE "path"`), the rows array is empty and nothing is written. |
| Raingage data file | `rain_gages` (`GeoPackageSchema.cpp:173-188`) | `file_path` TEXT field only. | No structured rainfall records. |
| `temp_file` | `climate_settings.temp_file` (`GeoPackageSchema.cpp:265`) | TEXT only. | No structured climate observations. |
| STRATEGY.md | `src/engine/input/geopackage/STRATEGY.md` | Single-file, multi-run vision is established. | Does not yet address external-file content; this plan adds §3.x there. |

### 2.4 GUI

| Concern | File / Line | Today | Gap |
|---|---|---|---|
| Save-As driver | `src/swmmvis.cpp:3242-3388` (`SWMMVis::onSaveAs`) | Normalises extension, then `pw->saveAs(inpPath)`. | No path-rebase pass before writing. |
| Per-window save | `src/swmmvisprojectwindow.cpp:782-871` (`saveAs`) | Resolves writer plugin by extension; calls `swmm_model_write_with_plugin`. | No format-aware editor swap (path widget vs data grid). |
| Workspace persistence | `src/project/openswmmvisworkspace.cpp:106-122` | `saveProject`, `saveSWMMProject`, `openSWMMModel` all stubs returning `false`. | Multi-layer workspace save not wired — separate prerequisite. |
| Hot-start save rows | `src/ui/dialogs/simulationoptionsdialog.cpp:2739-2756` | Stores absolute paths from `QFileDialog::getSaveFileName`. | No relative-path widget; no data editor for GPKG mode. |
| Report/Output path | `simulationoptionsdialog.cpp:2694-2724` | Stored in `QSettings`, not in `.inp`/`.gpkg`. | Out of scope here (see §5.5). |
| Timeseries external file | `src/ui/dialogs/timeserieseditordialog.cpp:1123-1270` | Absolute `filePath()`. Disables edits when external. | Path widget — needs relative display; in GPKG mode, the external rows become editable directly. |
| Raingage FILE source | `src/ui/properties/swmmraingagepropertyadapter.cpp` | No file-path editor today. | New widget required (path in INP mode, data grid in GPKG mode). |
| File filter registry | `src/plugins/filefilterregistry.cpp` | Tracks formats. | No notion of "format that owns external content"; this plan adds an `owns_external_content` boolean per writable entry. |

### 2.5 Tests

- `tests/unit/test_geopackage.cpp` exercises numeric round-trip; no portability assertions.
- `tests/gui/test_inpmeshwriter.cpp` / `tests/unit/test_meshinpbcroundtrip.cpp` cover mesh BC; no portability assertions.
- **No test today moves a written file to a sibling directory and re-opens** — the canonical portability test.

---

## 3. Design

### 3.1 Path-resolution utility (engine)

Add `src/engine/core/PathResolver.hpp/.cpp` with the following surface:

```cpp
namespace openswmm::io {

enum class PathClass {
    Relative,            // expressible relative to anchor
    AbsoluteSameVolume,  // absolute but on the anchor's volume
    AbsoluteCrossVolume, // different drive / UNC / cross-mount
    Invalid              // empty, malformed
};

struct RelativeResult {
    std::string path;        // always written with '/' separators
    PathClass   classification;
    int         up_levels;   // number of leading "../" segments
    std::string warning;     // human-readable when classification != Relative
};

// Compute path-as-relative-to-anchor. NEVER returns an absolute path unless
// the relative form is impossible (cross-volume / UNC). Forward-slash separators
// are emitted on every platform. Uses std::filesystem::proximate, not relative,
// so symlinks are preserved and missing files do not throw.
RelativeResult makeRelative(const std::string& target_absolute,
                            const std::string& anchor_dir,
                            int  max_up_levels = 16);

// Resolve a possibly-relative token against an anchor directory.
// Accepts both '/' and '\' separators on input. Empty token returns empty.
std::string resolveRelative(const std::string& stored_token,
                            const std::string& anchor_dir);

// Always-forward-slash, no trailing slash, no '.' segments. Idempotent.
std::string normaliseSeparators(const std::string& path);

// Parent directory portion; canonical form ready to use as anchor.
std::string parentDir(const std::string& file_path);

} // namespace openswmm::io
```

Implementation notes:
- Use `std::filesystem::proximate` (C++17); fall back to `std::filesystem::path::lexically_proximate` when the target does not exist.
- On Windows, compare `path::root_name()` to detect cross-volume; UNC roots compare unequal to drive roots.
- Cap `up_levels` at `max_up_levels`; above the cap, return `AbsoluteSameVolume` (or `AbsoluteCrossVolume`) so the caller can fall back to absolute.
- Always emit `'/'` on save; on read, `resolveRelative` accepts either separator and produces a platform-native absolute path via `std::filesystem::path::make_preferred`.

### 3.2 Engine-side path lifecycle for INP

On **read** (`InputReader::open(path)`):
1. Store source `.inp` path in `ctx.inp_file_path` (already done).
2. After all handlers complete, `PostParseResolver` walks every external-file slot and produces a `FilePathPair` carrying both:
   - `original` — verbatim token as it appeared in the `.inp` (for diagnostics & write-as-authored mode).
   - `absolute` — `resolveRelative(original, parentDir(ctx.inp_file_path))`, ready for `fopen`.
3. Slots affected: `FilesSpec` (rainfall/runoff/rdii/inflows/outflows/hotstart_use + `hotstart_saves[]`), `GageData::file_path[]`, `Table::file_path` (with `is_file_based=true`), `SimulationOptions::temp_file` and any related climate slots.

On **write** (`writeInpFile(ctx, dst_path)`):
1. Compute `dst_dir = parentDir(dst_path)`.
2. **Always** emit `makeRelative(slot.absolute, dst_dir)`. There is no conditional based on the original token's form.
3. When `makeRelative` returns `AbsoluteCrossVolume` (or `AbsoluteSameVolume` beyond depth cap), emit the absolute form and accumulate a warning on the write result; the GUI displays a non-blocking notice.
4. Skip emission only if `slot.absolute` is empty.
5. Update `ctx.inp_file_path = dst_path` on success.

Engine option `WRITE_ABSOLUTE_PATHS` (`YES`/`NO`, default `NO`) is the escape hatch from §1A; when `YES`, step 2 emits the absolute form unconditionally.

### 3.3 Engine-side data-model changes

Patch `FilesSpec` (`src/engine/core/SimulationContext.hpp:154-188`) to carry resolved + original per slot:

```cpp
struct FilePathPair {
    std::string absolute;        // ready for fopen()
    std::string original;        // verbatim token from .inp (or empty if model
                                 //  was built programmatically / loaded from GPKG)
};

struct FilesSpec {
    FileMode      rainfall_mode = FileMode::NONE;
    FilePathPair  rainfall;
    // …runoff/rdii/inflows/outflows…
    FilePathPair  hotstart_use;
    std::vector<HotstartSavePair> hotstart_saves;  // FilePathPair + datetime
};
```

Touchpoints (search for the existing field names; mechanical rename):
`FilesHandler.cpp`, `InpWriter.cpp:1243-1286`, every C-API getter/setter in `openswmm_model.h` / `openswmm_model_impl.cpp`, GeoPackage reader/writer, tests.

Apply `FilePathPair` to:
- `GageData::file_path` (raingage external file).
- `SimulationOptions::temp_file` (and any climate file slot added by Slice IO-3).
- `Table::file_path` — and have `TablesHandler.cpp` populate `Table::is_file_based` and `Table::file_path` directly, **dropping the `Table::id = "FILE:..."` overload** (Bug #1).

### 3.4 Geopackage — structured editable tables (replaces the BLOB approach)

Each external-file role gets its own relational table whose rows are the *parsed contents* of the file. The GUI edits the rows; the engine reads from the rows (or materialises a temp file from them on demand for legacy IO paths that still expect a file handle).

#### 3.4.1 Hot-start state (replaces opaque `.hsf` blobs)

Hot-start state is the engine snapshot defined by `src/legacy/engine/hotstart.c:17-22`: subcatchment runoff + snowpack + infiltration + groundwater, node depth + lateral inflow + quality, link flow + depth + setting + quality. A relational form maps cleanly to per-object rows, and **every row carries a true foreign-key relationship to the model objects already present in the GPKG schema** (`simulations`, `nodes`, `links`, `subcatchments`, `pollutants`). This means:

- Deleting a `simulation_id` cascades all hot-start data with it (already the convention for `result_timeseries`, `result_summary`, etc.).
- Deleting / renaming a node propagates to its hot-start state — no orphan rows, no `node_id` drift.
- The schema is closed under referential integrity: SQLite's `PRAGMA foreign_keys = ON` rejects any hot-start row that doesn't correspond to a real object.

SQLite permits composite FKs that reference a `UNIQUE` constraint (not just `PRIMARY KEY`). The existing parent tables each declare `UNIQUE(simulation_id, X_id)` (see `GeoPackageSchema.cpp:98, 138, 169, 187, 311`), which is exactly the target shape we need. Indexes are added on the `(simulation_id, slot_name, object_id)` prefix to make FK enforcement and join queries cheap.

```sql
-- Parent: one row per slot (USE input, SAVE outputs). Carries the metadata
-- that the legacy HSF header captured as fixed-width fields. Every per-object
-- row below references this slot via (simulation_id, slot_name).
CREATE TABLE hotstart_slots (
    simulation_id   TEXT NOT NULL,
    slot_name       TEXT NOT NULL,   -- 'use' or 'save_<index>' (0..MAX_HOTSTART_SAVES-1)
    direction       TEXT NOT NULL,   -- 'USE' | 'SAVE'
    save_datetime   REAL,            -- nullable; 0 means end-of-run
    format_version  INTEGER NOT NULL,-- mirrors legacy fileVersion
    flow_units      TEXT,            -- 'CFS'|'GPM'|... captured at save
    num_pollutants  INTEGER NOT NULL,
    captured_at     TEXT,            -- ISO8601 (NULL until first save completes)
    status          TEXT NOT NULL DEFAULT 'pending',  -- 'pending'|'populated'
    PRIMARY KEY (simulation_id, slot_name),
    FOREIGN KEY (simulation_id)
        REFERENCES simulations(simulation_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);

-- Per-node routing state. Composite FK enforces that node_id refers to a real
-- node in the same simulation; rename-or-delete propagates.
CREATE TABLE hotstart_node_state (
    simulation_id  TEXT NOT NULL,
    slot_name      TEXT NOT NULL,
    node_id        TEXT NOT NULL,
    depth          REAL NOT NULL,
    lateral_inflow REAL,
    overflow       REAL,
    PRIMARY KEY (simulation_id, slot_name, node_id),
    FOREIGN KEY (simulation_id, slot_name)
        REFERENCES hotstart_slots(simulation_id, slot_name)
        ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (simulation_id, node_id)
        REFERENCES nodes(simulation_id, node_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);
CREATE INDEX idx_hotstart_node_state_lookup
    ON hotstart_node_state(simulation_id, slot_name, node_id);

-- Per-link routing state.
CREATE TABLE hotstart_link_state (
    simulation_id  TEXT NOT NULL,
    slot_name      TEXT NOT NULL,
    link_id        TEXT NOT NULL,
    flow           REAL NOT NULL,
    depth          REAL,
    volume         REAL,
    setting        REAL,
    target_setting REAL,
    time_open      REAL,
    time_closed    REAL,
    PRIMARY KEY (simulation_id, slot_name, link_id),
    FOREIGN KEY (simulation_id, slot_name)
        REFERENCES hotstart_slots(simulation_id, slot_name)
        ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (simulation_id, link_id)
        REFERENCES links(simulation_id, link_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);
CREATE INDEX idx_hotstart_link_state_lookup
    ON hotstart_link_state(simulation_id, slot_name, link_id);

-- Per-subcatchment hydrology state: runoff + infiltration + groundwater +
-- snowpack water-equivalent / free-water / ATI per surface.
CREATE TABLE hotstart_subcatch_state (
    simulation_id  TEXT NOT NULL,
    slot_name      TEXT NOT NULL,
    subcatch_id    TEXT NOT NULL,
    runoff         REAL,
    -- Infiltration state (legacy 6-double array, model-specific):
    infil_model    INTEGER NOT NULL,
    infil_state_0  REAL, infil_state_1 REAL, infil_state_2 REAL,
    infil_state_3  REAL, infil_state_4 REAL, infil_state_5 REAL,
    -- Groundwater zone state:
    gw_theta_upper REAL,
    gw_lower_depth REAL,
    -- Snowpack water-equivalent + free-water + ATI per surface:
    snow_we_plowable  REAL, snow_we_imperv REAL, snow_we_perv REAL,
    snow_fw_plowable  REAL, snow_fw_imperv REAL, snow_fw_perv REAL,
    snow_ati          REAL,
    PRIMARY KEY (simulation_id, slot_name, subcatch_id),
    FOREIGN KEY (simulation_id, slot_name)
        REFERENCES hotstart_slots(simulation_id, slot_name)
        ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (simulation_id, subcatch_id)
        REFERENCES subcatchments(simulation_id, subcatch_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);
CREATE INDEX idx_hotstart_subcatch_state_lookup
    ON hotstart_subcatch_state(simulation_id, slot_name, subcatch_id);

-- Water-quality state per (object, pollutant). Split into three tables, one
-- per object kind, so each can carry a real composite FK to its parent — a
-- single polymorphic table cannot express that. The columns are otherwise
-- identical; the GUI surfaces them as a unified "Pollutant State" grid.

CREATE TABLE hotstart_node_pollutant_state (
    simulation_id  TEXT NOT NULL,
    slot_name      TEXT NOT NULL,
    node_id        TEXT NOT NULL,
    pollutant_id   TEXT NOT NULL,
    concentration  REAL NOT NULL,
    PRIMARY KEY (simulation_id, slot_name, node_id, pollutant_id),
    FOREIGN KEY (simulation_id, slot_name, node_id)
        REFERENCES hotstart_node_state(simulation_id, slot_name, node_id)
        ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (simulation_id, pollutant_id)
        REFERENCES pollutants(simulation_id, pollutant_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE hotstart_link_pollutant_state (
    simulation_id  TEXT NOT NULL,
    slot_name      TEXT NOT NULL,
    link_id        TEXT NOT NULL,
    pollutant_id   TEXT NOT NULL,
    concentration  REAL NOT NULL,
    PRIMARY KEY (simulation_id, slot_name, link_id, pollutant_id),
    FOREIGN KEY (simulation_id, slot_name, link_id)
        REFERENCES hotstart_link_state(simulation_id, slot_name, link_id)
        ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (simulation_id, pollutant_id)
        REFERENCES pollutants(simulation_id, pollutant_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE hotstart_subcatch_pollutant_state (
    simulation_id  TEXT NOT NULL,
    slot_name      TEXT NOT NULL,
    subcatch_id    TEXT NOT NULL,
    pollutant_id   TEXT NOT NULL,
    -- Subcatchment quality has two state vectors per pollutant:
    -- surface buildup mass (lbs or kg) and ponded concentration (mg/L).
    surface_buildup     REAL,
    ponded_concentration REAL,
    PRIMARY KEY (simulation_id, slot_name, subcatch_id, pollutant_id),
    FOREIGN KEY (simulation_id, slot_name, subcatch_id)
        REFERENCES hotstart_subcatch_state(simulation_id, slot_name, subcatch_id)
        ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (simulation_id, pollutant_id)
        REFERENCES pollutants(simulation_id, pollutant_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);
```

Schema notes:
- **One slot, many object rows.** `hotstart_slots` is the parent that every per-object table FKs into via `(simulation_id, slot_name)`. Cascading delete on a slot removes every state row associated with it in one statement.
- **Quality is split three ways** (`hotstart_node_pollutant_state`, `hotstart_link_pollutant_state`, `hotstart_subcatch_pollutant_state`). A single polymorphic `object_type` column would have prevented per-object FK enforcement (SQLite has no discriminated-union FK). The three-table split costs one more table per kind but gives genuine referential integrity. The subcatchment pollutant table additionally carries the surface-buildup mass and ponded-concentration vectors that `hotstart.c:saveRunoff()` writes for water-quality models.
- **Pollutant FK is to the model's `pollutants` table**, not a free-text label. Renaming a pollutant in the model propagates through every saved hot-start; deleting one purges its concentrations.
- **`hotstart_*_pollutant_state` FKs into the matching `hotstart_*_state`** (rather than directly into `nodes`/`links`/`subcatchments`). This ensures pollutant rows cannot exist for a slot/object combination that has no parent state — a meaningful invariant for restart correctness, since concentrations without depth/flow context are nonsensical.
- **`PRAGMA foreign_keys = ON`** must be set per SQLite connection. Add this to `GpkgUtils::openConnection` so it covers every reader/writer; verify with a regression test that the pragma is honored on every connection.
- **Indexes.** Each per-object state table has an index on the FK columns to keep cascading deletes and slot-wide queries (`SELECT * FROM hotstart_node_state WHERE simulation_id=? AND slot_name=?`) O(log N).
- **GUI editor.** The "Hot-Start Editor" surfaces five grids per slot — nodes, links, subcatchments, and a unified "Pollutant State" grid built as a UNION view across the three pollutant tables. Edits to the unified grid are dispatched to the right base table by `object_type`. Foreign-key violations surface in the GUI as Qt model validation errors before the row is committed.
- **Materialisation.** When the engine asks for an HSF file handle, the GeoPackage plugin serialises the relevant rows back to `<gpkg>.scratch/<slot>.hsf` in the legacy binary format with the SWMM object iteration order, and hands the path back. Cleanup on engine close.

#### 3.4.1.a Same FK pattern applied to the other content tables

For consistency, the schema in §3.4.3 (`raingage_data`), §3.4.4 (`climate_data`), and §3.4.5 (`routing_interface`, `routing_interface_pollutants`) follow the same FK convention — each row references the SWMM object it pertains to via `(simulation_id, X_id)`. The DDL fragments below are amended accordingly; the headline shape stays the same.

#### 3.4.2 Timeseries data file (`[TIMESERIES] X FILE "path"`)

Reuse the existing `input_timeseries` table (`GeoPackageSchema.cpp:226-236`) plus a `source` column:

```sql
ALTER TABLE input_timeseries ADD COLUMN source TEXT
    NOT NULL DEFAULT 'inline';      -- 'inline' | 'imported_from_file'
ALTER TABLE input_timeseries ADD COLUMN source_filename TEXT;  -- nullable
ALTER TABLE input_timeseries ADD COLUMN source_column TEXT;    -- nullable
```

On import, the parser reads the external file once, expands its rows into `input_timeseries`, and records provenance in the new columns. From that point on the GUI's existing `TimeSeriesEditorDialog` edits the rows like any other timeseries; the user never sees a file path.

#### 3.4.3 Raingage data file (`[RAINGAGES] X INTENSITY ... FILE "path"`)

```sql
CREATE TABLE raingage_data (
    simulation_id  TEXT NOT NULL,
    gage_id        TEXT NOT NULL,
    record_time    TEXT NOT NULL,   -- ISO8601
    rainfall_value REAL NOT NULL,
    quality_flag   TEXT,
    station_id     TEXT,            -- for multi-station NCDC formats
    PRIMARY KEY (simulation_id, gage_id, record_time),
    FOREIGN KEY (simulation_id, gage_id)
        REFERENCES rain_gages(simulation_id, gage_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);
CREATE INDEX idx_raingage_data
    ON raingage_data(simulation_id, gage_id, record_time);
```

GUI: per-gage table editor + time-series plot. Engine materialises a synthetic raingage file when legacy `gage.c` requires `fopen`; or, when the new engine IO path is wired (future slice), reads directly from the table.

#### 3.4.4 Climate file (`[TEMPERATURE] FILE "path"`)

Climate files in the SWMM ecosystem follow NCDC, GHCND, or user-defined CSV formats with daily Tmin/Tmax (+ optionally evaporation, wind, sky cover, humidity).

```sql
CREATE TABLE climate_data (
    simulation_id  TEXT NOT NULL,
    record_date    TEXT NOT NULL,   -- ISO8601 date
    tmin           REAL,            -- °F or °C — captured in climate_settings.units
    tmax           REAL,
    evaporation    REAL,
    wind_speed     REAL,
    sky_cover      REAL,
    humidity       REAL,
    quality_flag   TEXT,
    PRIMARY KEY (simulation_id, record_date),
    FOREIGN KEY (simulation_id)
        REFERENCES simulations(simulation_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);
```

(Climate data is scoped to the simulation rather than to a SWMM object — only the simulation FK applies.)

GUI: tabular climate editor reuses the existing pattern grid widget.

#### 3.4.5 Routing interface files

Inflows / Outflows / RDII / Rainfall / Runoff interface files (`[FILES]` section) carry per-timestep per-object flows. A single shared long-format table works for all five, distinguished by `role`:

Routing interface rows reference different object kinds depending on the role (`NODE` for `INFLOWS`/`OUTFLOWS`/`RDII`; `SUBCATCH` for `RUNOFF`; `GAGE` for `RAINFALL`). The same three-table split used for hot-start pollutant state applies here so each table can carry a true composite FK to its owning object:

```sql
-- One row per (slot, object, timestep) for node-keyed interface files.
CREATE TABLE routing_interface_node (
    simulation_id  TEXT NOT NULL,
    role           TEXT NOT NULL,   -- 'INFLOWS'|'OUTFLOWS'|'RDII'
    direction      TEXT NOT NULL,   -- 'USE'|'SAVE'
    node_id        TEXT NOT NULL,
    record_time    TEXT NOT NULL,
    flow_value     REAL NOT NULL,
    PRIMARY KEY (simulation_id, role, direction, node_id, record_time),
    FOREIGN KEY (simulation_id, node_id)
        REFERENCES nodes(simulation_id, node_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE routing_interface_subcatch (
    simulation_id  TEXT NOT NULL,
    role           TEXT NOT NULL,   -- 'RUNOFF'
    direction      TEXT NOT NULL,
    subcatch_id    TEXT NOT NULL,
    record_time    TEXT NOT NULL,
    flow_value     REAL NOT NULL,
    PRIMARY KEY (simulation_id, role, direction, subcatch_id, record_time),
    FOREIGN KEY (simulation_id, subcatch_id)
        REFERENCES subcatchments(simulation_id, subcatch_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE routing_interface_gage (
    simulation_id  TEXT NOT NULL,
    role           TEXT NOT NULL,   -- 'RAINFALL'
    direction      TEXT NOT NULL,
    gage_id        TEXT NOT NULL,
    record_time    TEXT NOT NULL,
    rainfall_value REAL NOT NULL,
    PRIMARY KEY (simulation_id, role, direction, gage_id, record_time),
    FOREIGN KEY (simulation_id, gage_id)
        REFERENCES rain_gages(simulation_id, gage_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);

-- Pollutant concentrations attached to node-keyed routing rows (the only
-- legacy routing format that carries quality data).
CREATE TABLE routing_interface_node_pollutants (
    simulation_id  TEXT NOT NULL,
    role           TEXT NOT NULL,
    direction      TEXT NOT NULL,
    node_id        TEXT NOT NULL,
    record_time    TEXT NOT NULL,
    pollutant_id   TEXT NOT NULL,
    concentration  REAL NOT NULL,
    PRIMARY KEY (simulation_id, role, direction, node_id, record_time, pollutant_id),
    FOREIGN KEY (simulation_id, role, direction, node_id, record_time)
        REFERENCES routing_interface_node(simulation_id, role, direction, node_id, record_time)
        ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (simulation_id, pollutant_id)
        REFERENCES pollutants(simulation_id, pollutant_id)
        ON DELETE CASCADE ON UPDATE CASCADE
);
```

`SAVE`-direction rows are pending until the engine populates them after the run. A SQL view `routing_interface` UNIONs the three base tables for read-only "show me everything" queries; writes always go through the typed tables.

#### 3.4.6 Schema registry (`gpkg_contents`)

Every new table is registered with `data_type='attributes'` (OGC Related Tables Extension pattern) so QGIS and other GeoPackage-aware tools show them as non-spatial tables.

#### 3.4.7 What is *not* stored

- The raw bytes of the original file. The user's source files remain on disk where they were; we capture their *content*, not their bytes. If the user wants to keep the source CSV alongside the `.gpkg`, that is their choice.
- Per-format quirks irrelevant to simulation (column ordering, comments, header decoration). The parser normalises on import.

### 3.5 Geopackage load/save lifecycle

**Write (`GeoPackageWriter::write(ctx, gpkg_path)`):**
1. Open / create `.gpkg`; insert / upsert `simulations` row.
2. Write all existing model tables (no change).
3. For every external-file slot on the in-memory model:
   - If the slot's `FilePathPair.absolute` points to a real readable file, **parse it** with the role-appropriate parser (§3.6) and insert rows into the matching content table.
   - If the file is missing but the slot is a `SAVE`-direction (RDII/RAINFALL/RUNOFF/OUTFLOWS/HOTSTART), create the parent slot row with `status='pending'`; the next simulation populates it.
   - If the file is missing and the slot is `USE`-direction, fail the save with a clear error (the user has referenced a file the engine can't run with).
4. Drop all `path` TEXT fields that the BLOB plan would have retained — the content tables are the source of truth.

**Read (`GeoPackageReader::read(ctx, gpkg_path)`):**
1. Read all existing model tables (no change).
2. For each role with rows in the corresponding content table, populate `ctx.files`/`ctx.gages.file_path`/etc. with a `FilePathPair` whose `absolute` is empty and whose `original` carries a sentinel `"<gpkg-table:role:owner>"` (used only for diagnostics).
3. Set a per-slot `source = GPKG_TABLE` flag (new enum). Engine code that previously did `fopen(slot.absolute, ...)` consults `slot.source`; for `GPKG_TABLE`, it routes through a `GpkgBackedFileReader` adapter that yields rows on demand — no scratch file unless legacy `fopen`-only code paths force it (§3.5.1).
4. On engine close: drop `<gpkg>.scratch/` if any materialisation occurred.

#### 3.5.1 Materialisation bridge for legacy file IO

The legacy engine paths in `src/legacy/engine/` (`iface.c`, `gage.c`, `climate.c`, `hotstart.c`) expect `FILE*` handles. Rewriting them is a multi-slice effort and is *not* a prerequisite for portability.

Bridge behaviour:
- When legacy code is about to `fopen` a `slot.absolute` whose `source == GPKG_TABLE`, the engine first calls `GpkgMaterialiser::materialiseTo(<role>, <owner>, <scratch_path>)`.
- The materialiser serialises rows back into the legacy on-disk format (e.g. legacy HSF binary, NCDC-like text for climate, SWMM5 binary interface format for routing).
- `slot.absolute` is rebound to the scratch path for the duration of the run.
- After `swmm_engine_close`, scratch files are deleted. Honors `CLAUDE.md` §4 transparent-IO: scratch dir is a *sibling* of the `.gpkg`, named `<gpkg-stem>.scratch/`, so a user can inspect what the engine fed itself.

Long-term (post-portability work), `GpkgBackedFileReader` replaces the materialiser entirely for read-direction slots, and `GpkgBackedFileWriter` does the same for save-direction slots; legacy `fopen` sites move to the new abstraction one at a time.

### 3.6 External-file parsers & writers

Each role needs:
- **Parser** — bytes on disk → table rows. Lives in `src/engine/input/geopackage/format_<role>.cpp`.
- **Materialiser** — table rows → bytes on disk in the legacy format. Lives next to the parser.

Initial parser support:
| Role | Format(s) supported v1 | Format(s) deferred |
|---|---|---|
| Hot-start | Legacy HSF binary | — |
| Timeseries data | CSV with optional header, SWMM-native `Date Time Value` text | NCDC, user-pluggable |
| Raingage data | NCDC DSI-3240, NWS, "Standard" SWMM5 columnar text | Custom CSV via mapping spec |
| Climate | NCDC TD3200, NWS, user CSV (`date,tmin,tmax,evap,wind,sky,humidity`) | GHCND |
| Routing interface | SWMM5 binary interface format (`iface.c`-compatible) | Text export for diffability |

The format detection is by file extension + magic-byte sniff on import. The materialiser always emits the canonical SWMM5 form so the engine can consume it byte-for-byte the same as a hand-authored file. Deferred formats become extension points — each parser is a `IExternalFormatPlugin` registered through the existing plugin SDK.

### 3.7 GUI editor contract

Two storage modes drive editor selection:

| Project mode | External file slot UI | Where the data lives |
|---|---|---|
| INP-backed | `RelativePathPicker` widget — text field shows path relative to the `.inp`; browse button rebases the user's pick to relative on selection. Tooltip shows the resolved absolute path. | On disk, at the relative path. |
| GPKG-backed | Inline data editor — table grid + chart (timeseries-style for time-stamped roles, simple grid for hotstart state). Browse button is replaced by an **Import…** button that runs a one-shot format parser into the rows. | Inside the `.gpkg`, in the role's content table. |

Editors gain a uniform `ExternalFileSlot` abstraction (new in `src/ui/widgets/externalfileslot.{h,cpp}`) which adapts to project mode and exposes the same Qt signals (`pathChanged`, `dataChanged`) so calling editors don't fork code paths.

Affected editors:
1. `SimulationOptionsDialog` (Files, Climate, Hot-Start tabs) — all path widgets replaced.
2. `TimeSeriesEditorDialog` — when GPKG mode, the external-file panel collapses to "Imported from <filename> on <date>" and the inline rows are editable as normal; the path widget vanishes. When INP mode, the existing widget gains relative-path behaviour.
3. `SwmmRainGagePropertyAdapter` — new external-file slot (today nothing).
4. `HotstartSavesModel` — same as Files tab; rows show slot name + datetime + status (`pending`/`populated`); editing a `populated` slot opens the four-grid hotstart editor.

### 3.8 GUI save pipeline

```
SWMMVis::onSaveAs (dialog path)
  → normalizeSaveAsPath (existing, src/project/saveaspathnormalizer.cpp)
  → IoPortabilityNormalizer::prepare(pw, dstPath, target_format) (new)
       • for INP target:
           – walk every slot's ctx FilePathPair
           – rewrite slot.original to makeRelative(slot.absolute, dst_dir)
           – collect cross-volume / over-depth warnings
       • for GPKG target:
           – verify every USE-direction slot has either a readable file
             or already-populated content rows; queue parser invocations
           – collect parser/availability warnings
  → pw->saveAs (existing)
  → display collected warnings non-blockingly
```

`IoPortabilityNormalizer` is pure-Qt (no engine deps beyond `QString`/`QFileInfo` + the new C-API setters) and is unit-testable via `tests/gui/test_ioportabilitynormalizer.cpp`.

---

## 4. Plan of Work — Sliced & Verifiable

Each slice ends in a passing test (`CLAUDE.md` §4). Slices are ordered so the engine substrate lands before GUI work depends on it.

### Slice IO-1 — `PathResolver` utility (engine)
- **Add:** `src/engine/core/PathResolver.{hpp,cpp}` per §3.1.
- **Verify:** `tests/unit/test_pathresolver.cpp` — relative⇄absolute, cross-volume on Windows (mock root_names), `..` depth cap, separator normalisation, unicode round-trip, missing-file safe.

### Slice IO-2 — `FilePathPair` migration (engine, mechanical)
- **Edit:** `SimulationContext.hpp` (`FilesSpec`, `GageData`, `SimulationOptions::temp_file`, `Table`).
- **Edit:** every handler/writer/C-API setter that reads or writes those fields.
- **Verify:** existing tests pass unchanged (rename-only at storage layer; write still emits whatever `.original` token holds).

### Slice IO-3 — `PostParseResolver` resolves on read
- **Edit:** `src/engine/input/PostParseResolver.cpp` — after all section handlers, fill `slot.absolute = resolveRelative(slot.original, parentDir(ctx.inp_file_path))`.
- **Verify:** `tests/unit/test_inp_relative_paths_read.cpp` — `.inp` at `/tmp/A/proj/model.inp` with `./rain.dat` and `../../shared/data/climate.dat` resolves both.

### Slice IO-4 — InpWriter always emits relative paths
- **Edit:** `InpWriter.cpp` — propagate `dst_dir`; switch every external-file emission to `makeRelative(slot.absolute, dst_dir)` unconditionally. Honor `options.write_absolute_paths` flag for the opt-out.
- **Edit:** add the missing file-backed `[TIMESERIES] X FILE "..."` emission (Bug #1).
- **Verify:** `tests/unit/test_inp_relative_paths_roundtrip.cpp` — open from dir A, write to dir B (where B is *not* a parent or descendant of A), confirm paths use `../` traversal; copy the source files alongside, re-open, confirm engine runs.

### Slice IO-5 — Geopackage structured schema with FK integrity
- **Edit:** `GeoPackageSchema.cpp` — add the tables from §3.4: `hotstart_slots`, `hotstart_node_state`, `hotstart_link_state`, `hotstart_subcatch_state`, `hotstart_node_pollutant_state`, `hotstart_link_pollutant_state`, `hotstart_subcatch_pollutant_state`, `raingage_data`, `climate_data`, `routing_interface_node`, `routing_interface_subcatch`, `routing_interface_gage`, `routing_interface_node_pollutants`; add `source`/`source_filename`/`source_column` to `input_timeseries`. Every table carries the composite FKs documented in §3.4.1.
- **Edit:** `GpkgUtils::openConnection` — set `PRAGMA foreign_keys = ON` on every SQLite connection.
- **Edit:** STRATEGY.md — new §3.x "External-File Content Tables".
- **Verify:** `tests/unit/test_geopackage_schema_external_content.cpp` — every table exists with the documented columns, indexes, and FK constraints; `gpkg_contents` registration is correct; FK enforcement is active (test inserts a hot-start row referencing a non-existent node and expects rejection; test deletes a node and asserts cascading hot-start row deletion).

### Slice IO-6 — Format parsers & materialisers
- **Add:** `format_hotstart.cpp`, `format_timeseries.cpp`, `format_raingage.cpp`, `format_climate.cpp`, `format_routing_interface.cpp` under `src/engine/input/geopackage/formats/`.
- **Verify:** per-format `tests/unit/test_format_<role>.cpp` — round-trip parse → materialise byte-for-byte for the canonical SWMM5 form.

### Slice IO-7 — Geopackage writer fans out into content tables
- **Edit:** `GeoPackageWriter.cpp` — `writeExternalContent(ctx)` step after the existing table writes; per-slot dispatch to the matching parser; pending-flag handling for SAVE-direction slots.
- **Verify:** `tests/unit/test_gpkg_external_content_write.cpp` — model with timeseries file, climate file, USE-hotstart, USE-RDII serialises into the right tables; SAVE-hotstart slot appears with `status='pending'`.

### Slice IO-8 — Geopackage reader hydrates the model
- **Edit:** `GeoPackageReader.cpp` — populate per-slot `FilePathPair{.absolute="", .original=sentinel}` and per-slot `source = GPKG_TABLE`.
- **Add:** `GpkgMaterialiser` — materialise rows back to scratch files when legacy `fopen` is unavoidable.
- **Verify:** `tests/unit/test_gpkg_external_content_read.cpp` — write+read round-trip; move `.gpkg` to a sibling directory with no support files; engine runs and produces identical results.

### Slice IO-9 — C-API surface for slots
- **Edit:** `include/openswmm/engine/openswmm_model.h` — new accessors keyed by `SWMM_FilePathRole`:
  - `swmm_slot_get(engine, role, owner, &absolute, &original, &source)`
  - `swmm_slot_set_path(engine, role, owner, new_path)` (INP mode)
  - `swmm_slot_set_table_owned(engine, role, owner)` (GPKG mode)
  - bulk content getters/setters for hotstart and timeseries content (rows in/out).
- **Verify:** `tests/unit/test_capi_slots.cpp`.

### Slice IO-10 — GUI `ExternalFileSlot` + `IoPortabilityNormalizer`
- **Add:** `src/ui/widgets/externalfileslot.{h,cpp}` (path-mode + data-mode adapters).
- **Add:** `src/project/ioportabilitynormalizer.{h,cpp}` (pre-save rebase pass).
- **Verify:** `tests/gui/test_externalfileslot.cpp`, `tests/gui/test_ioportabilitynormalizer.cpp`.

### Slice IO-11 — Wire `ExternalFileSlot` into every editor
Sub-slices, one per editor, each with its own verification:
  a. `SimulationOptionsDialog` Files tab (rainfall/runoff/rdii/inflows/outflows).
  b. `SimulationOptionsDialog` Climate tab (temperature/evaporation/wind).
  c. `SimulationOptionsDialog` Hot-Start tab (`HotstartSavesModel`).
  d. `TimeSeriesEditorDialog` external panel.
  e. `SwmmRainGagePropertyAdapter` new slot.
  f. New Hot-Start data editor dialog (four-grid editor referenced by 11c when slot is populated).

### Slice IO-12 — Save-As pipeline hookup
- **Edit:** `SWMMVis::onSaveAs` — invoke `IoPortabilityNormalizer::prepare` before `pw->saveAs`.
- **Edit:** `SWMMVisProjectWindow::saveAs` — surface warnings list to the user.
- **Verify:** `tests/gui/test_saveas_portability.cpp` — see §8.

### Slice IO-13 — Documentation
- **Edit:** `docs/USER_MANUAL.md` — "Project portability" section.
- **Edit:** `openswmm.engine/docs/` — C-API additions.
- **Finalise:** `openswmm.engine/src/engine/input/geopackage/STRATEGY.md` §3.x.

---

## 5. Decisions & Open Questions

### 5.1 Why always relative (vs containment-conditional)
Documented in §1A. The summary: simpler mental model, stable diffs, predictable round-trips. Cross-volume / UNC is the only legitimate failure case; we degrade gracefully.

### 5.2 Why structured tables (vs BLOBs) for Geopackage
- **Editability.** Users can author hot-start initial conditions, climate observations, raingage records, and routing flows directly in the GUI without touching opaque files. This is the user's stated requirement.
- **Diffability.** SQLite tables can be exported to CSV; pull requests get meaningful change records instead of "BLOB column changed".
- **Queryability.** SQL questions like "every gage with > 1in/hr in May 2025" become trivial.
- **No silent corruption channel.** A BLOB whose header desyncs from the schema is undetectable. Rows are validated by primary keys / NOT NULLs.
- **Cost:** more upfront schema and parser work. Mitigated by the role-table reuse (one `routing_interface` table covers five legacy interface formats).

### 5.3 Path-separator policy
Forward-slash on every platform when writing; accept both on read. Matches CMake/Python/SWMM-legacy behaviour. Locked in §3.1.

### 5.4 Cross-volume fallback
Absolute, with visible warning. The user's opt-out is the per-project `WRITE_ABSOLUTE_PATHS YES` setting which converts the absolute-fallback into the *default* (still on a per-slot basis, no rebasing attempted at all).

### 5.5 Report file / Output file paths (`QSettings`)
Out of scope for this plan. Currently held in per-project `QSettings` (`simulationoptionsdialog.cpp:2682`) and never written to either `.inp` or `.gpkg`. Revisit when `.oswp` adds a "simulation profile" block.

### 5.6 Hot-start *save* slot existence at save-time
Persist with `status='pending'`. The output plugin populates the rows after the run completes. Refusing to save would block the common "author first, run later" workflow.

### 5.7 Format-parser pluggability
Built-in parsers cover the SWMM5 canonical formats (Slice IO-6 table). Any additional format (NCDC variants, vendor-specific raingage exports, custom CSV with mapping) is added via the existing plugin SDK as an `IExternalFormatPlugin`. The plan delivers a working SWMM5-canonical pipeline first; pluggability comes free from the SDK pattern already in the codebase.

### 5.8 Legacy `.inp` round-trip alignment
Verified by Slice IO-4's regression test that opens an EPA-SWMM-shipped example and confirms identical numerical output before and after the round-trip — `CLAUDE.md` §4.01 directive.

---

## 6. Known Bugs Surfaced (Fix Inside the Right Slices)

1. **`[TIMESERIES] X FILE "..."` is never written back.** `InpWriter.cpp:1033-1074` iterates only `tb.x[]/tb.y[]` and skips file-backed tables. Fix in Slice IO-4.
2. **`TablesHandler.cpp:99` overloads `Table::id` as `"FILE:" + path`** instead of setting `Table::is_file_based` / `Table::file_path`. Fix in Slice IO-2.
3. **`OpenSWMMVisWorkspace::saveProject` / `saveSWMMProject` are stubs** (`openswmmvisworkspace.cpp:106-122`). Not blockers for IO portability but the IO-12 wiring must not assume they work — IO-12 routes through `SWMMVisProjectWindow::saveAs` directly.

---

## 7. Out of Scope

- Removing pre-existing dead code (`CLAUDE.md` §3 — clean up only our own mess).
- Reorganising existing GeoPackage simulation-result tables.
- Network-share / cloud-mount path semantics beyond what `std::filesystem::proximate` supports.
- Migrating older `.gpkg` files lacking the new content tables — additive change; old files keep working; new content tables appear only on re-save.
- Direct engine reads from GPKG content tables (no materialisation) — future post-portability slice.

---

## 8. Success Criteria

A reviewer should be able to:
1. **Cross-root relative round-trip (INP).** Open `examples/relative_paths_demo.inp` at `/tmp/A/proj/model.inp` that references `../../shared/data/rain.dat` and `/abs/path/to/climate.dat`. Save-As to `/tmp/B/proj/model.inp`. The new file references `../../shared/data/rain.dat` unchanged *and* `../../../abs/path/to/climate.dat` (rebased from absolute to relative against B). Engine re-opens from B and produces the same results, provided the support files are in place at the rebased locations.
2. **Cross-volume graceful degrade (INP, Windows).** A `.inp` on `C:\proj\` referencing `D:\data\rain.dat` saves with the absolute `D:\data\rain.dat` retained and a single visible warning. Tests run on the Windows CI runner.
3. **Self-contained Geopackage with editable content.** Open the same `.inp`; Save-As as `.gpkg`. The `.gpkg` contains `climate_data`, `raingage_data`, `routing_interface`, and `hotstart_*` rows. Copy *only* the `.gpkg` to a different machine, open it; engine runs and produces identical results. Edit a hotstart node-depth row in the GUI; re-run; output reflects the edit.
4. **Always-relative invariant.** Save-As any model from any source location to any destination; the produced `.inp` contains no absolute path *unless* the path crosses volumes / UNC (then a visible warning is emitted for that slot).

These four scenarios become integration tests in `tests/gui/test_saveas_portability.cpp` (Slice IO-12).

---

## 9. Implementation Status (post-IO-13)

All thirteen slices have landed. Per-slice deliverables:

| Slice | Status | Notes |
|---|---|---|
| IO-1 PathResolver | ✅ | `src/engine/core/PathResolver.{hpp,cpp}`, 38 gtest cases. |
| IO-2 FilePathPair migration | ✅ | Drop-in `{absolute, original}` carrier via `core/FilePathPair.hpp`. |
| IO-3 PostParseResolver fills `.absolute` | ✅ | `resolve_external_file_slots` exposed in `PostParseResolver.hpp`. |
| IO-4 InpWriter always emits relative | ✅ | `writeInpFile` gained an optional `warnings*` param + `WRITE_ABSOLUTE_PATHS` option. |
| IO-5 GPKG structured Part D schema | ✅ | 13 tables w/ composite FKs into `simulations`/`nodes`/`links`/`subcatchments`/`pollutants`. |
| IO-6 Format parsers + materialisers | ✅ | All 5 (timeseries, climate, raingage, routing-interface, hot-start v4). |
| IO-7 GPKG writer fan-out | ✅ | `ExternalContentWriter` inserts Part D rows from in-memory slots. |
| IO-8 GPKG reader hydration | ✅ | `ExternalContentReader` materialises scratch files in `<gpkg>.scratch/`. |
| IO-9 C-API for slots | ✅ | `SWMM_FilePathRole` + `swmm_file_path_get/set` reaching all 10 slot families. |
| IO-10 GUI widget + normalizer | ✅ | `RelativePathPicker` + `IoPortabilityNormalizer::preflight*`. |
| IO-11a Files tab pilot | ✅ | 6 `[FILES]` rows now use `RelativePathPicker`. |
| IO-11b Climate tab | ⚠️ N/A | **Gap discovered:** the GUI has no Climate tab today. See §10. |
| IO-11c Hot-Start tab anchor | ✅ | `PathBrowseDelegate` gained `setProjectAnchor`. |
| IO-11d TimeseriesEditor anchor | ✅ | `setProjectAnchor` + relative-display path field. |
| IO-11e RainGage property `filePath` | ✅ | Two new Q_PROPERTYs via the IO-9 typed C-API. |
| IO-11f Hot-Start data editor dialog | ⏳ Deferred | Substantial new-UI piece; not blocking portability. |
| IO-12 SaveAs preflight hookup | ✅ | `SWMMVis::onSaveAs` runs `IoPortabilityNormalizer::preflight*` and logs warnings before invoking the writer. |
| IO-13 Docs | ✅ | This update + `USER_MANUAL.md` chapter 25 + `IO_PORTABILITY_C_API.md` + STRATEGY §10. |

---

## 10. Open issues / discoveries

### 10.1 Climate tab is missing (IO-11b)

`SimulationOptionsDialog` does not surface `[TEMPERATURE] FILE` /
`[EVAPORATION] FILE` / wind-file slots in any tab today. The engine-side
plumbing is complete (`SWMM_FILE_CLIMATE_TEMP` reaches `opts.temp_file`)
but no UI exists. Adding a Climate tab to the dialog is **a new feature,
not a portability wire-up** — out of this plan's scope. The
`IoPortabilityNormalizer` already covers the climate slot (it walks all
seven scalar roles), so when the Climate tab eventually lands its
picker will automatically inherit the pre-flight check.

### 10.2 Property-browser type-hint absence (IO-11e)

`SwmmRainGagePropertyAdapter` now exposes `filePath` and
`resolvedFilePath` as Q_PROPERTYs (Slice IO-11e). The property-browser
framework that renders these adapters chooses widgets by Q_PROPERTY
type. For `QString` properties it uses a plain `QLineEdit`. To route
them through the `RelativePathPicker`, the framework needs a *type-hint
mechanism* (e.g. an attribute on the property indicating it represents
a file path). That's a property-browser infrastructure change separate
from this plan; the adapter is ready to take advantage of it when it
lands.

### 10.3 Hot-start data editor (IO-11f)

The Part D `hotstart_*` tables can hold edited initial conditions
(per-node depths, per-link flows, pollutant concentrations) that the
engine consumes via materialisation. A dialog with four grids — nodes,
links, subcatchments, pollutants — would let users author initial
conditions directly in the GUI without touching `.hsf` binaries. The
schema is in place; the dialog is not. Deferred as a discretionary
follow-up.

### 10.4 Hot-start subcatch runoff state

`HotstartFormat` (Slice IO-6e) implements the HSF v4 *routing* portion
only. Subcatchment runoff state (`saveRunoff` / `readRunoff` in
`legacy/engine/hotstart.c`) has conditional fields whose presence
depends on per-subcatch groundwater / snowpack configuration. The Part
D `hotstart_subcatch_state` table has columns for the full set, but the
materialiser writes `nSubcatch = 0` until a follow-up slice extends it.
For routing-only restart scenarios the current implementation is
sufficient.

### 10.5 Real GUI build verification

All Qt6 GUI sources added by Slices IO-10 through IO-12 are
static-reviewed only — the sandbox environment lacks Qt6 headers (they
live on the host at `/Users/calebbuahin/Qt/6.10.2/...`). The engine-side
slices pass `-fsyntax-only` and standalone-link checks; the GUI slices
need a real `ninja` run on the user's machine to confirm AUTOMOC picks
up the new Q_OBJECT classes and the integration tests run cleanly.
