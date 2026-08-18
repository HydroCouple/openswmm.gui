# Chapter 25 — Project Portability

OpenSWMM treats projects as movable artifacts. Whether you save your model
as a SWMM `.inp` text file or a GeoPackage `.gpkg` container, the goal is
the same: hand the file to a colleague, drop it into a different folder,
zip it for an archive, and have it just open. This chapter explains how
that works and what to watch out for.

## 25.1 What "portable" means in OpenSWMM

A SWMM model can reference many external files — rainfall data, hot-start
snapshots, climate observations, routing-interface flows. If those
references are baked in as absolute paths (`C:\Users\alice\proj\rain.dat`),
the project breaks the moment anyone else opens it. If they are stored
relative to the project's location (`rain.dat`, `../shared/climate.dat`),
the project keeps working as long as the *relative* layout of files is
preserved.

OpenSWMM uses two complementary strategies:

- **For `.inp` projects:** every external file reference is written as
  a path *relative to the directory of the `.inp` file itself*. Move the
  whole project tree anywhere, and references continue to resolve.

- **For `.gpkg` projects:** external file *contents* are imported into
  structured tables inside the `.gpkg`. The geopackage becomes a single
  self-contained file you can copy anywhere, no support files required.

Both strategies are explained in detail below.

## 25.2 INP — always relative paths

When you **Save** or **Save As** a `.inp` file, OpenSWMM rebases every
external-file reference (rainfall, runoff, RDII, inflows, outflows,
hot-start save/use, climate, raingage data, file-backed timeseries)
against the destination directory.

### What gets relativised

| Where in the `.inp` | Slot |
|---|---|
| `[FILES]` | RAINFALL / RUNOFF / RDII / INFLOWS / OUTFLOWS / HOTSTART save & use |
| `[TEMPERATURE]` | `FILE "..."` reference |
| `[RAINGAGES]` | Per-gage `FILE "..."` source |
| `[TIMESERIES]` | Per-series `FILE "..."` source (with optional `:column` suffix) |
| `[2D_MESH_FILE]` | External `.2dm` mesh reference (see below) |
| `[2D_OPTIONS]` | `OUTPUT_FILE` 2D results file |
| `[LID_USAGE]` | Per-unit LID report file |
| `[PROCESS_COMPONENTS]` | `config=` file, copied alongside on Save As |

`[PLUGINS]` paths are the one deliberate exception: they name installed
shared libraries, not model data, so they stay absolute.

How far a relative path may reach is capped at 16 `../` levels. Beyond
that the relative form is longer and less readable than the absolute one,
so the absolute path is written instead — the same fallback as the
cross-volume case below.

### The external 2D mesh travels with the model

An external `.2dm` behaves differently from the other references, because
the mesh is part of the model rather than an input feeding it. When you
save, OpenSWMM writes the **current in-memory mesh** to a `.2dm` beside
the destination `.inp` and keeps the reference local. So a Save As into a
new folder produces a complete, self-contained pair of files, and edits
you made after loading the mesh (vertex elevations, conveyance, boundary
conditions) are carried into it. The original `.2dm` is never modified.

The one case that behaves like the other slots is a mesh that could not
be loaded — a missing or unreadable `.2dm`. There is nothing in memory to
write, so the reference is re-anchored to keep pointing at the file it
originally named, rather than silently resolving to a non-existent file
in the new folder.

### Cross-volume / UNC fallback

A reference that *cannot* be expressed relative to the destination — for
example, a Windows path on a different drive (`D:\data\rain.dat` while
your project is on `C:`) — is preserved in absolute form and surfaces as
a warning in the log panel:

> *Portability check: RAINFALL: cannot express relatively (different
> volume/root)*

The save still succeeds; the warning is a heads-up so you know that
particular reference will not survive a move to a machine without the
same drive layout.

### The escape hatch: `WRITE_ABSOLUTE_PATHS`

Some legacy tools refuse to honor relative paths in `.inp` files. For
that case, set the project-level option:

```
[OPTIONS]
WRITE_ABSOLUTE_PATHS  YES
```

When this option is `YES`, OpenSWMM emits paths in their absolute form
unconditionally. Default is `NO`. See the engine option reference for
details.

The setting is a genuine project option, so it round-trips: a deck saved
with `WRITE_ABSOLUTE_PATHS YES` reopens with the escape hatch still
armed, and one saved without it reopens relative.

## 25.3 Geopackage — structured embedded content

When you **Save As** a `.gpkg`, OpenSWMM imports the *content* of every
external file referenced by your model into dedicated relational tables
inside the geopackage:

| External file kind | GeoPackage tables |
|---|---|
| Timeseries CSV | `input_timeseries` rows (with `source = 'imported_from_file'`, `source_filename`, `source_column`) |
| Raingage data | `raingage_data` (per-gage records) |
| Climate file | `climate_data` (one row per day) |
| Routing interface (INFLOWS / OUTFLOWS / RDII) | `routing_interface_node` + `routing_interface_node_pollutants` |
| Routing interface (RUNOFF) | `routing_interface_subcatch` |
| Routing interface (RAINFALL) | `routing_interface_gage` |
| Hot-start file | `hotstart_slots` + `hotstart_{node,link,subcatch}_state` + `hotstart_{node,link,subcatch}_pollutant_state` |

Every row carries a *true foreign-key relationship* into the model's
existing tables (`nodes`, `links`, `subcatchments`, `rain_gages`,
`pollutants`, `simulations`). Deleting or renaming a model object
cascades through every dependent row; orphan rows are rejected at
insert time. `PRAGMA foreign_keys = ON` is set on every connection.

### When the engine wants a file, not a row

The simulation engine still expects to `fopen()` legacy on-disk formats
(HSF binary for hot-start, SWMM5 text for routing interface, CSV for
timeseries, etc.). To bridge that, OpenSWMM **materialises a scratch
file** for each role when the geopackage is opened. Scratch files land
in a sibling directory named after the project:

```
my_project.gpkg
my_project.scratch/
    climate.csv
    raingage_G1.dat
    routing_INFLOWS_USE.txt
    use.hsf
    ...
```

The scratch directory is regenerated from the geopackage every time you
open it; you can inspect it to verify what the engine is consuming.

### USE files must exist at save time

GeoPackage **USE-direction** slots (INFLOWS, RDII, HOTSTART_USE,
RAINFALL, climate, raingage data, timeseries) must reference readable
files on disk when you save. Otherwise the import fails and the save is
rolled back. **SAVE-direction** slots (OUTFLOWS, RUNOFF, HOTSTART_SAVE)
are written by the engine *after* the simulation runs; a missing file
at save time is the expected state and produces no error. Hot-start
SAVE slots are recorded with `status = 'pending'`; the engine populates
them at run end.

## 25.4 Editor behaviour

Wherever the dialog asks for an external file path, the field shows
that path *relative to your project* whenever it can. Hover over the
field to see the resolved absolute path in a tooltip.

The Simulation Options dialog's **Files** tab covers all six `[FILES]`
secondary references (RAINFALL / RUNOFF / RDII / INFLOWS / OUTFLOWS /
HOTSTART USE) and the hot-start save schedule. The Time Series editor's
external-file panel uses the same anchored-display convention.

## 25.5 Pre-save portability check

When you click **Save As**, OpenSWMM runs a quick pre-flight check
**before** writing the file. The check walks every external-file slot
on the model and surfaces warnings in the log panel for:

- Slots that cannot be made relative (cross-volume / UNC).
- USE-direction slots whose referenced file is missing when you target
  a `.gpkg` save.

The pre-flight is non-blocking: warnings appear, but the save itself
proceeds. The authoritative rebase happens inside the engine writer at
save time; the pre-flight is purely for visibility.

## 25.6 Common moves and their effects

| Action | INP project | GPKG project |
|---|---|---|
| Copy the whole project folder to a USB stick | Just works. | Just works — copy only the `.gpkg`. |
| Send a colleague just the `.inp` file | Breaks references they don't already have. | N/A — they'd want the `.gpkg`. |
| Open the project from a cloud sync folder | Works if the relative structure is preserved. | Works regardless. |
| Run on Windows after authoring on macOS | Forward-slash separators are accepted; case-sensitivity may bite if a referenced file's case differs from disk. | Works regardless — content travels with the geopackage. |
| Cross-volume `D:` drive reference | Falls back to absolute + warning. | Content is imported into the geopackage; the original path is irrelevant after save. |

## 25.7 Troubleshooting

| Symptom | Likely cause |
|---|---|
| "Cannot express relatively" warning on save | Reference is on a different drive / volume. Either move the file under the project tree, or accept the absolute path. |
| "USE-direction file not found" when saving as `.gpkg` | The referenced file is missing on disk. Restore the file, then re-save. |
| Engine errors about missing data file after move | The relative reference no longer resolves on the new machine. Check the file's expected location vs. where it actually lives. |
| Hot-start save slot shows `status='pending'` after run | Engine didn't reach the save datetime. Check the simulation completed past that point. |
