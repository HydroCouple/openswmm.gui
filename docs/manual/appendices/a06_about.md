@page manual_about A6 — About, Credits and Licences

## What you'll do

Find the version and build information for a bug report, read the licence of
any library SWMMVis ships, and understand the licence agreement shown at first
launch.

## Where to find it

- **Help → About** — the About dialog.
- The **License Agreement** dialog appears automatically at launch until you
  opt out of it.

## Step-by-step

### The About dialog

Window title **About SWMMVis**. A header strip across the top, a filterable
master list of components on the left, and licence text plus metadata on the
right.

\figtodo{a06_about_dialog.png, The About dialog with a component selected and its licence in the right pane}

The header strip shows, and the **Copy environment** button copies:

```
SWMMVis <major>.<minor>.<patch>
Build:    <compile date> <compile time>
Qt:       <Qt runtime version>
OS:       <pretty product name> (<kernel type> <kernel version>)
Arch:     <CPU architecture>
```

That block is the first thing to attach to a bug report — its tooltip says so
(*Copy the build / OS / Qt summary to the clipboard*).

With nothing selected, the right pane shows the application overview:
*Open-source Qt6 GUI for the OpenSWMM engine. Select a component on the left to
view its license and metadata.*

| Control | What it does |
|---|---|
| **Filter components…** | live-filters the component tree by name |
| a category row | expands or collapses that group |
| a component row | loads its metadata and licence into the right pane |
| **Copy License** | copies the full licence text to the clipboard |
| **Open Homepage** | opens the project homepage in your browser |
| **Open Source** | opens the upstream source-download URL |
| **Copy environment** | copies the header block above |

Selecting a component shows **Version**, **Role**, **License** (SPDX
identifier), **Source** (provenance — `vcpkg`, `in-tree`, `vendored`, `system`)
and a **Homepage** link, with the verbatim licence text beneath. A component
whose manifest names a missing file shows *(license file not found: …)*; one
with no licence declared shows *(no license file declared in the manifest)*.

### Components shipped

The list is data-driven, so it reflects the build you are running. As shipped:

| Category | Component | Licence | Provenance |
|---|---|---|---|
| Engine | Open-Source SWMM Engine | MIT | in-tree subdirectory |
| Engine | Triangle (Shewchuk) | Triangle (custom) | vendored in the engine's 2D module |
| Frameworks | Qt 6 | LGPL-3.0-only | system or vendor install |
| Frameworks | QPropertyModel | MIT | vendored sibling project |
| Geospatial | GDAL / OGR | MIT | vcpkg |
| Geospatial | PROJ | MIT | vcpkg |
| Geospatial | GeoTIFF | MIT | vcpkg |
| I/O | SQLite | public domain | vcpkg |
| I/O | Expat | MIT | vcpkg |
| I/O | libxml2 | MIT | vcpkg |
| Numerics | nanoflann | BSD-2-Clause | vcpkg |
| Numerics | CVODE (SUNDIALS) | BSD-3-Clause | in-tree, when 2D is enabled |
| Numerics | OpenMP runtime | Apache-2.0 WITH LLVM-exception (libomp) | system |

HDF5 is linked for the 2D results reader but does not currently have a manifest
entry.

### The licence agreement

At launch, SWMMVis shows a modal **License Agreement** dialog headed
**SWMMVis — GNU General Public License v3**, with the licence text, a
**Show this agreement on startup** checkbox, and two buttons —
**Yes, I Agree** and **No, Exit**. Declining exits the application
immediately.

Unchecking the box suppresses the dialog on later launches. The preference is
stored in the application's settings under
`SWMMVis/LicenseAgreement/showOnStartup`; deleting that key brings the dialog
back.

\figtodo{a06_license_agreement.png, The startup License Agreement dialog}

### Licensing summary

**SWMMVis itself is GPL-3.0-or-later**, © 2026 HydroCouple. The OpenSWMM engine
is MIT-licensed and can be used independently of the GUI. Qt 6 is used under
LGPL-3.0.

The bundled licence texts are currently a mix of canonical text and pointers to
the upstream `LICENSE` file, and the pointers are clearly marked as such. The
version strings come from the manifest rather than from the build system, so a
stale version can appear in the dialog even though the linked library is
current — check the About dialog against your build environment before quoting
a version in a compliance report.

### Adding a dependency to the dialog

The dialog is data-driven, so most updates need no recompile:

1. Put the upstream licence text in `resources/licenses/<name>.txt`.
2. Add a row to `resources/about/components.json`:

   ```json
   {
     "category":    "Numerics",
     "name":        "Eigen",
     "version":     "3.4.0",
     "role":        "Linear-algebra primitives",
     "homepage":    "https://eigen.tuxfamily.org",
     "source":      "https://gitlab.com/libeigen/eigen",
     "spdx":        "MPL-2.0",
     "provenance":  "vcpkg",
     "licenseFile": ":/licenses/eigen.txt"
   }
   ```

3. Register the file in the About resource bundle under the `/licenses` prefix.
4. Rebuild — the entry appears under its category.

### Project links

| Resource | URL |
|---|---|
| Report an Issue | https://github.com/HydroCouple/openswmm.engine/issues |
| Engine repository | https://github.com/HydroCouple/openswmm.engine |
| GUI repository | https://github.com/HydroCouple/openswmm.gui |
| Engine API reference | https://www.hydrocouple.org/openswmm.engine |
| User manuals | https://www.hydrocouple.org/openswmm.engine/d3/dae/manuals.html |

The first four are also on the Welcome page's *Learn SWMM* panel.

## Tips and gotchas

- **Open Source** opens the upstream project's download page, not the exact
  binary you are running. It is the right link for licence redistribution, and
  the wrong one for "what code is in this binary".
- The **Copy environment** block is deliberately plain text and diff-friendly.
  Paste it verbatim rather than retyping it.
- Two GUI network layers send a user-agent string naming a repository URL that
  is not the canonical one. Report issues to the address in the table above.

## Related

- \ref manual_introduction — what SWMMVis is and how to install it
- \ref manual_troubleshooting — how to file a useful bug report
- \ref manual_plugins — which engine components your build actually has
- \ref manual_performance — build-time options that change what ships
