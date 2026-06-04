# Handoff: portable Windows deployment — GDAL/PROJ data, runtime paths, and DLLs

**Audience:** Lead developer / anyone packaging **SWMMVis** for a machine without a system GDAL/PROJ install.  
**Scope:** In-repo changes for bundled **`gdal/`** and **`proj/`** share trees, the **`setupBundledGisDataPaths()`** code path, **CMake** `POST_BUILD` copies, and the **shared libraries** you must place beside **`SWMMVis.exe`** (or on `PATH`) for a manual / folder-based run.

---

## 1. Problem this solves

- **GDAL** needs a data directory (historically `GDAL_DATA`: CSV/GML/CRS support files, not just `gdal.dll`).
- **PROJ** 6+ needs a directory containing **`proj.db`** (typically `PROJ_DATA`).

When the app is **not** run from a vcpkg/conda environment, the process often has **no** `GDAL_DATA` / `PROJ_DATA`, which breaks CRS pickers, reprojection, and many geospatial code paths.

We fix this in two ways:

1. **At runtime:** set `GDAL_DATA` / `PROJ_DATA` (and `CPLSetConfigOption` mirrors) to **`<dir-of-SWMMVis.exe>/gdal`** and **`<dir-of-SWMMVis.exe>/proj`** when those folders exist.
2. **At build time (vcpkg):** `POST_BUILD` copies the vcpkg **share** trees into the same directory as the linked **`SWMMVis.exe`** so Debug/Release outputs are self-contained for data (DLLs are a separate step; see §7).

---

## 2. Files added (new)

| File | Role |
|------|------|
| [`include/core/gisdatapaths.h`](../include/core/gisdatapaths.h) | Declares `void setupBundledGisDataPaths();` |
| [`src/core/gisdatapaths.cpp`](../src/core/gisdatapaths.cpp) | Implements detection of `gdal/` and `proj/` next to the executable and sets env + CPL options |
| [`docs/PORTABLE_GDAL_PROJ.md`](PORTABLE_GDAL_PROJ.md) (this file) | Deployment handoff, DLLs, and code walkthrough for the lead |

`gisdatapaths.cpp` is part of the **`SWMMVis`** target; it uses **`#include <cpl_conv.h>`** from GDAL (already linked via `GDAL::GDAL`).

---

## 3. Files modified (existing)

| File | Change |
|------|--------|
| [`CMakeLists.txt`](../CMakeLists.txt) | (1) Added `include/core/gisdatapaths.h` and `src/core/gisdatapaths.cpp` to `PROJECT_SOURCES`. (2) After `set_target_properties(SWMMVis …)`, added **`add_custom_command(… POST_BUILD …)`** to `copy_directory` vcpkg **`share/gdal`** → `$<TARGET_FILE_DIR:SWMMVis>/gdal` and **`share/proj`** → `…/proj` when `VCPKG_INSTALLED_DIR` and `VCPKG_TARGET_TRIPLET` are set and the source directories exist. |
| [`src/swmmvisapplication.cpp`](../src/swmmvisapplication.cpp) | `#include "core/gisdatapaths.h"`. **First line in each constructor body:** `setupBundledGisDataPaths();` for both `SWMMVisCoreApplication` and `SWMMVisApplication`, so the Qt core application object exists and `QCoreApplication::applicationDirPath()` is valid **before** any other initialization that might touch GDAL. |

No changes were required to `main.cpp` because both entry classes already encapsulate process setup.

---

## 4. Call order (why the hook is in `swmmvisapplication.cpp`)

1. `main` constructs either **`SWMMVisCoreApplication`** or **`SWMMVisApplication`**.
2. **`QCoreApplication` / `QApplication` base** is fully constructed, so `QCoreApplication::instance()` is non-null and **`applicationDirPath()`** is the directory containing the **`.exe`**.
3. **`setupBundledGisDataPaths()`** runs **immediately** in the constructor body.
4. Later, GIS code (e.g. `crsmanager.cpp` → `GDALAllRegister()`, `gisvectorlayer.cpp`, etc.) sees correct **`GDAL_DATA` / `PROJ_DATA`**.

**Important:** This must run **before** the first `GDALAllRegister()` or OGR/PROJ use anywhere in the process.

Reference — GUI app constructor (abbreviated):

```cpp
// src/swmmvisapplication.cpp
#include "core/gisdatapaths.h"

SWMMVisApplication::SWMMVisApplication(int &argc, char *argv[])
    : QApplication(argc, argv),
      mSWMMVisGUI(new SWMMVis()),
      mSWMMVisSplashScreen(nullptr)
{
    setupBundledGisDataPaths();
    // … organization name, version, UI style, splash, etc.
}
```

Reference — CLI core app (no GUI):

```cpp
SWMMVisCoreApplication::SWMMVisCoreApplication(int& argc, char* argv[])
    : QCoreApplication(argc, argv)
{
    setupBundledGisDataPaths();
    // …
}
```

---

## 5. `setupBundledGisDataPaths()` — behavior (full logic)

**Location:** `src/core/gisdatapaths.cpp`

**Responsibilities:**

- Early exit if `QCoreApplication::instance()` is null (defensive; should not happen in normal use).
- Build an ordered list of candidate roots and resolve `<root>/gdal` and `<root>/proj` from the first root that contains the expected marker files (portable paths via `QDir::filePath`, native separators in env strings).
- **Candidate roots (first match wins, per subdir):**
  1. `applicationDirPath()` — next to the executable. Covers Windows, Linux, and non-bundle macOS dev builds.
  2. On macOS only: `applicationDirPath()/../Resources` — the standard `.app/Contents/Resources/` bundle layout.
- **GDAL folder:** Treated as valid if either **`gml_registry.xml`** or **`gdalvrt.xsd`** exists (typical vcpkg `share/gdal` layout). Then:
  - `qputenv("GDAL_DATA", path)` so child processes / plugins see the same (if any).
  - `CPLSetConfigOption("GDAL_DATA", path.constData())` so the GDAL library in-process uses the path (critical when the env was not set before `gdal.dll` / `libgdal.dylib` / `libgdal.so` was loaded).
- **PROJ folder:** If **`proj.db`** exists, set **`PROJ_DATA`**, **`PROJ_LIB`** (compatibility for older expectations), and the same keys via **`CPLSetConfigOption`**.

**Why both `qputenv` and `CPLSetConfigOption`?**

- **Env:** Some GDAL/OGR/PROJ code paths read the environment.
- **CPL:** Ensures the running process picks up the path even if the shared library was loaded before the env was set, and keeps GDAL/PROJ consistent for in-process use.

**Reference implementation (current source):**

```cpp
// src/core/gisdatapaths.cpp  (abbreviated — see source for helpers)
void setupBundledGisDataPaths()
{
    if (!QCoreApplication::instance())
        return;

    const QString base = QCoreApplication::applicationDirPath();
    QStringList roots;
    roots << base;
#ifdef Q_OS_MACOS
    // macOS .app bundle: binary lives in Contents/MacOS/, data in
    // Contents/Resources/ (standard Apple bundle layout).
    roots << QDir::cleanPath(QDir(base).filePath(QStringLiteral("../Resources")));
#endif

    for (const QString &root : roots) {
        const QString gdalDir = QDir(root).filePath(QStringLiteral("gdal"));
        if (dirHasAnyMarker(gdalDir, { "gml_registry.xml", "gdalvrt.xsd" })) {
            publishDataPath("GDAL_DATA", gdalDir);  // qputenv + CPLSetConfigOption
            break;
        }
    }

    for (const QString &root : roots) {
        const QString projDir = QDir(root).filePath(QStringLiteral("proj"));
        if (QFileInfo::exists(QDir(projDir).filePath(QStringLiteral("proj.db")))) {
            publishDataPath("PROJ_DATA", projDir);
            publishDataPath("PROJ_LIB",  projDir);
            break;
        }
    }
}
```

**Header (contract for callers):**

```cpp
// include/core/gisdatapaths.h
void setupBundledGisDataPaths();
// Call once after QCoreApplication exists and before any GDAL/OGR/PROJ use.
```

---

## 6. CMake: `POST_BUILD` copy of share trees

**When it runs:** After **`SWMMVis`** links successfully, CMake copies the **vcpkg-installed** data directories into the SWMMVis output (per-config, e.g. `Release` / `Debug` on MSVC).

**Condition:** `DEFINED VCPKG_INSTALLED_DIR` **and** `DEFINED VCPKG_TARGET_TRIPLET` (set by the vcpkg CMake toolchain in manifest mode), **and** both source directories must exist (so non-vcpkg configures do not fail).

**Source paths (manifest build layout):**

- `${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/gdal`
- `${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/proj`

**Destination (platform-dependent generator expression):**

| Platform | Destination |
|---|---|
| Windows / Linux | `$<TARGET_FILE_DIR:SWMMVis>/{gdal,proj}` — directly beside the binary |
| macOS (`MACOSX_BUNDLE TRUE`) | `$<TARGET_BUNDLE_CONTENT_DIR:SWMMVis>/Resources/{gdal,proj}` — i.e. `SWMMVis.app/Contents/Resources/{gdal,proj}` |

The runtime resolver (§5) searches both locations on macOS, so dev builds that do not yet live inside a `.app` bundle continue to work.

**Excerpt (see `CMakeLists.txt` for full block):**

```cmake
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_SWMMVIS_GDAL_SHARE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/gdal")
    set(_SWMMVIS_PROJ_SHARE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/proj")
    if(EXISTS "${_SWMMVIS_GDAL_SHARE}" AND EXISTS "${_SWMMVIS_PROJ_SHARE}")
        if(APPLE)
            set(_SWMMVIS_GDAL_DEST "$<TARGET_BUNDLE_CONTENT_DIR:SWMMVis>/Resources/gdal")
            set(_SWMMVIS_PROJ_DEST "$<TARGET_BUNDLE_CONTENT_DIR:SWMMVis>/Resources/proj")
        else()
            set(_SWMMVIS_GDAL_DEST "$<TARGET_FILE_DIR:SWMMVis>/gdal")
            set(_SWMMVIS_PROJ_DEST "$<TARGET_FILE_DIR:SWMMVis>/proj")
        endif()
        add_custom_command(TARGET SWMMVis POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_SWMMVIS_GDAL_SHARE}" "${_SWMMVIS_GDAL_DEST}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_SWMMVIS_PROJ_SHARE}" "${_SWMMVIS_PROJ_DEST}"
            COMMENT "Copy GDAL/PROJ share data into SWMMVis output (portable)"
            VERBATIM
        )
    endif()
endif()
```

`copy_directory` merges/overwrites; if you need a clean tree, delete `gdal/` and `proj/` in the output dir before a rebuild.

**Typical `VCPKG_INSTALLED_DIR` (example):**  
`<build_dir>/vcpkg_installed`  
(e.g. `openswmm.gui/build/windows/vcpkg_installed`).

**Triplet (example):** `x64-windows`.

---

## 7. What “data” is not — and DLLs you were missing (Windows)

**`gdal/` and `proj/` are not substitutes for shared libraries.** The EXE must still find:

### 7.1 In-process / project DLLs (often not beside the EXE in a naïve build)

| DLL | Description | Typical build output location (order-of-magnitude; verify on your tree) |
|-----|-------------|--------------------------------|
| **`openswmm.engine.dll`** | Engine shared library (CMake target `openswmm_engine`, output name `openswmm.engine`) | e.g. `build/windows/bin/<Config>/` or post-build copy to `Release/` — **copy next to `SWMMVis.exe`** for manual runs. |
| **`QPropertyModel.dll`** | Property-grid Qt library (FetchContent / sibling) | e.g. `build/windows/_deps/qpropertymodel-build/<Config>/QPropertyModel.dll` |
| **`Qt6Charts.dll`** | Linked by SWMMVis for charts | From Qt kit: `…/Qt/<ver>/msvc2022_64/bin/Qt6Charts.dll` |

If Windows reports **“The code execution cannot proceed because … was not found”** for one of these, place the listed DLL in the **same directory as `SWMMVis.exe`** (or add that directory to `PATH`).

### 7.2 Qt platform + plugins (use `windeployqt`)

`windeployqt` from the **same kit** you built with (e.g. `…/msvc2022_64/bin/windeployqt.exe`) should be run against **`SWMMVis.exe`**. It deploys **Qt6Core, Gui, Widgets, Svg, Charts, …** DLLs and plugin folders such as **`platforms/`, `imageformats/`, `tls/`**, etc.

Example:

```bat
"%QT_DIR%\bin\windeployqt.exe" --release --compiler-runtime "path\to\Release\SWMMVis.exe"
```

**`windeployqt` does not** deploy GDAL/PROJ **data** trees (`gdal/`, `proj/`) or vcpkg’s non-Qt dependency DLLs; those are separate (§6, §7.3, §4–5 in code).

### 7.3 GDAL stack and vcpkg runtimes (many `.dll` files)

`gdal.dll` **depends** on a large set of vcpkg-built DLLs (PROJ, GEOS, SQLite, etc.). For a **portable** folder, copy **all** `*.dll` from the manifest install:

` <build_dir>/vcpkg_installed/x64-windows/bin/ `

**→** same folder as **`SWMMVis.exe`**, or ensure that `bin` directory is on **`PATH`**.

**Notable names (non-exhaustive):** `gdal.dll`, `proj_9.dll` (or similar versioned `proj_*.dll`), `geos_c.dll`, `geos.dll`, `sqlite3.dll`, `libcurl.dll`, `libxml2.dll`, `tiff.dll`, `zlib1.dll`, `json-c.dll`, `netcdf.dll`, `hdf5*.dll`, …

A full vcpkg `bin` copy avoids missing-dependency errors one at a time.

### 7.4 Data next to the EXE (after §6 or manual copy)

| Folder | Must contain (examples) |
|--------|-------------------------|
| **`gdal/`** | `gml_registry.xml`, EPSG/CSV and related files from vcpkg `share/gdal` |
| **`proj/`** | **`proj.db`**, and other `share/proj` files for your PROJ build |

The runtime code in §5 only activates when these look valid.

---

## 8. End-state folder layout (reference)

```text
Release/   (or the folder you ship)
  SWMMVis.exe
  openswmm.engine.dll
  QPropertyModel.dll
  gdal.dll
  proj_9.dll
  (… many vcpkg *.dll …)
  Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6Charts.dll, …
  platforms/   (qwindows.dll, etc.)
  imageformats/  (…)
  tls/          (…)
  gdal/         (data — from vcpkg share/gdal)
  proj/         (data — from vcpkg share/proj, includes proj.db)
  translations/ (if created by windeployqt)
```

---

## 9. Sibling repository: `QPropertyModel` (if building from a local checkout)

The GUI pulls **QPropertyModel** via CMake (sibling or FetchContent). For a **Windows** build from source, the following were needed in that repo in some environments; keep them in mind if you refresh from upstream or fork:

- **`cmake/QPropertyModelConfig.cmake.in`** for `configure_package_config_file`.
- **`CPACK_RESOURCE_***` paths** in `CMakeLists.txt` should use **`${PROJECT_SOURCE_DIR}`** (not `CMAKE_SOURCE_DIR` when the project is a subdirectory) so CPack and license paths resolve to QPropertyModel, not the parent GUI.
- **`qt_create_translation`**: use **`${CMAKE_CURRENT_SOURCE_DIR}`** so lupdate does not scan the parent **openswmm.gui** tree.
- **MSVC `stdafx.h`**: a minimal **`include/stdafx.h`** (or remove `#include "stdafx.h"` from sources) if PCH is not enabled.

(These are **not** in `openswmm.gui`’s `CMakeLists.txt`; they live in the QPropertyModel tree when using a local checkout.)

---

## 10. Quick verification checklist (lead dev)

- [ ] Build **SWMMVis** with vcpkg manifest; confirm **`build/.../Release/gdal/gml_registry.xml`** (or `gdalvrt.xsd`) and **`…/Release/proj/proj.db`** exist after build.
- [ ] Start the app; confirm no GDAL/PROJ data warnings in logs for basic CRS/vector operations.
- [ ] If distributing a **zip** of a folder, include **all** sections in §7 and §8, not only `gdal.dll`.
- [ ] Re-run **`windeployqt`** after changing Qt-linked code or Qt version; re-copy vcpkg **`bin` DLLs** if you upgrade vcpkg ports.

---

## 11. Related project docs

- [`README.md`](../README.md) — build overview, vcpkg, Qt, siblings **openswmm.engine** and **QPropertyModel**.

If this document drifts from the code, **trust the source** in `include/core/gisdatapaths.h`, `src/core/gisdatapaths.cpp`, `src/swmmvisapplication.cpp`, and the **`POST_BUILD`** block in `CMakeLists.txt`.
