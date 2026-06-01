@page manual_about 19 — About and Licenses

## What you'll do

Audit every third-party library the application ships, read its license
verbatim, and copy diagnostic info for bug reports.

## Where to find it

`Help → About` in the menu bar.

## Step-by-step

### Reading the dialog

```
┌── About OpenSWMM GUI ─────────────────────────────────────────────────┐
│ OpenSWMM GUI 6.0.0                              [Copy environment]    │
│ Build: Apr 22 2026 04:38                                              │
│ Qt:    6.10.2                                                         │
│ OS:    macOS Tahoe 26.0  (darwin 25.4.0)                              │
│ Arch:  arm64                                                          │
├──────────────────────────────────────────┬────────────────────────────┤
│ [Filter components…]                     │ GDAL                       │
│ ▾ Engine                                 │ Version: 3.9.x             │
│   • OpenSWMMCore                         │ Role:    Vector / raster IO│
│   • Triangle (Shewchuk)                  │ License: MIT               │
│ ▾ Frameworks                             │ Source:  vcpkg             │
│   • Qt 6                                 │ Homepage: https://gdal.org │
│   • QPropertyModel                       │ ───────────────────────── │
│ ▾ Geospatial                             │ Copyright (c) The GDAL/OGR │
│   • GDAL  ◀ selected                     │ Project authors            │
│   • PROJ                                 │ Permission is hereby …     │
│   • GeoTIFF                              │ … (full license text)      │
│ ▾ I/O                                    │                            │
│   • SQLite                               │                            │
│   • Expat                                │ [Copy License]             │
│   • libxml2                              │ [Open Homepage]            │
│ ▾ Numerics                               │ [Open Source]              │
│   • CVODE (SUNDIALS)                     │                            │
│   • OpenMP runtime                       │                            │
└──────────────────────────────────────────┴────────────────────────────┘
                                                              [Close]
```

| Action | What it does |
|--------|--------------|
| **Filter components…** | Live-filter the master list by name. |
| Click a category | Expands / collapses the group. |
| Click a component | Loads its metadata + license into the right pane. |
| **Copy License** | Copies the full license text to the clipboard. |
| **Open Homepage** | Opens the project homepage in your default browser. |
| **Open Source** | Opens the upstream source-download URL. |
| **Copy environment** (header) | Copies the build / Qt / OS summary block to the clipboard — paste into a bug report. |

### Adding a new dependency to the dialog

The dialog is **data-driven**; no recompile is needed for typical updates.

1. Drop the upstream `LICENSE` text into
   `resources/licenses/<name>.txt`.
2. Add a row to `resources/about/components.json`:

   ```json
   {
     "category":   "Numerics",
     "name":       "Eigen",
     "version":    "3.4.0",
     "role":       "Linear-algebra primitives",
     "homepage":   "https://eigen.tuxfamily.org",
     "source":     "https://gitlab.com/libeigen/eigen",
     "spdx":       "MPL-2.0",
     "provenance": "vcpkg",
     "licenseFile": ":/licenses/eigen.txt"
   }
   ```

3. Add the file to [resources/about.qrc](../../resources/about.qrc) under
   the `/licenses` prefix.
4. Rebuild — the new entry appears under its category.

### When to use it

- **Compliance audits** — confirm every shipped binary's license at a
  glance without grepping a packaged `LICENSE` file.
- **Bug reports** — *Copy environment* gives a diff-friendly summary
  block: app version, build date, Qt version, OS / kernel / arch.
- **Discovery** — see what numerical, geospatial, and IO libraries are
  doing the heavy lifting.

## Tips and gotchas

- The license texts shipped today are a **mix of canonical text and
  pointers to the upstream `LICENSE` file**. A packaging-time CMake step
  will eventually pull in the verbatim text from each vcpkg / submodule
  dependency at install time. The pointers are clearly marked.
- The `version` field is **hand-rolled in the JSON** for now — a planned
  CMake helper will rewrite it from `find_package` queries at configure
  time so a stale version can never show up in the dialog.
- The *Open Source* button opens the upstream's download page, not the
  binary you're running — useful for bug reports and license-redistribution
  workflows, less useful for "what code is in this binary right now".

## Related

- [Implementation plan — License and About](../GUI_IMPLEMENTATION_PLAN.md)
  — full specification and follow-up tasks.
