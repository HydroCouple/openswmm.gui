# SWMMVis

<p align="center">
  <img src="resources/images/swmmvis-demo.gif" alt="SWMMVis animating 2D flood results over an aerial basemap" width="900">
</p>

*Animated 2D flood results in SWMMVis.*

**Qt6/C++ graphical user interface for the OpenSWMM storm-water simulation engine (v6.0.0)**

[![Build and Package](../../actions/workflows/build_and_test.yml/badge.svg?branch=swmm6_gui)](../../actions/workflows/build_and_test.yml)
[![Documentation](../../actions/workflows/documentation.yml/badge.svg?branch=swmm6_gui)](../../actions/workflows/documentation.yml)
[![CodeQL](../../actions/workflows/codeql.yml/badge.svg?branch=swmm6_gui)](../../actions/workflows/codeql.yml)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/HydroCouple/openswmm.gui/badge)](https://securityscorecards.dev/viewer/?uri=github.com/HydroCouple/openswmm.gui)
[![Issues](https://img.shields.io/github/issues/HydroCouple/openswmm.gui)](https://github.com/HydroCouple/openswmm.gui/issues)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

[GUI documentation](https://www.hydrocouple.org/openswmm.gui/) · [Engine documentation](https://hydrocouple.org/openswmm.engine) · [Engine source](https://github.com/HydroCouple/openswmm.engine)

---

## Overview

A new GIS-based graphical user interface being developed for SWMM 6.0. The application is built with C++20 and the Qt 6 framework, providing tight integration with the next-generation OpenSWMM computational engine.

Key capabilities:

- Interactive map canvas with pan/zoom, CRS reprojection, and layer management (vector, raster, WMS/WMTS, XYZ tiles, SWMM model & results layers)
- SWMM model setup, editing, and validation
- Simulation control and results visualization
- Groundwater options, exterior head/flow and chemistry boundaries, bulk/SIGMA layer initial assignments, combined layered chemistry and porous soil heat, and per-species layer results with validation and undo (requires the matching development engine)
- GIS data import/export via GDAL

The matching development engine supports standalone CPU RK2 water, pollutants/MSX, age, transported water temperature and surface/aquifer reactions. The porous profile also supports RK2 soil energy, conduction, chemistry and age under constant, time-series or held air thermal boundaries, with sensible or latent soil evaporation. Fully covered surface/soil RK2 contacts also support chemistry, age and reactions with nonlinear radiation and sensible atmosphere (`EVAPORATION NO` for sensible exchange). Further heat owners and network/catchment coupling retain first-order requirements; see the [qualified transport profiles](https://github.com/HydroCouple/openswmm.engine/blob/swmm6_rel/docs/design/groundwater-transport.md).

## Dependencies

| Dependency | Version | Notes |
|---|---|---|
| [Qt](https://www.qt.io/) | 6.5+ | Widgets, OpenGL, Network, Concurrent, Svg, Charts |
| [GDAL](https://gdal.org/) | 3.x | GIS/CRS support — installed via vcpkg |
| [openswmm.engine](https://github.com/HydroCouple/openswmm.engine) | branch `swmm6_rel` | Engine — expected at `../openswmm.engine` |
| [QPropertyModel](https://github.com/cbuahin/QPropertyModel) | branch `dev` | Property editor — expected at `../QPropertyModel` |
| [vcpkg](https://github.com/microsoft/vcpkg) | `2025.03.19` | Package manager |
| CMake | 3.21+ | Build system |

## Building

### 1. Clone sibling repositories

All three repos must be siblings in the same parent directory:

```bash
# From the directory that will contain all repos:
git clone -b swmm6_gui  https://github.com/HydroCouple/openswmm.gui.git
git clone -b swmm6_rel  https://github.com/HydroCouple/openswmm.engine.git openswmm.engine
git clone -b dev        https://github.com/cbuahin/QPropertyModel.git QPropertyModel
git clone               https://github.com/microsoft/vcpkg.git

cd vcpkg
./bootstrap-vcpkg.sh   # Windows: bootstrap-vcpkg.bat
cd ..
```

### 2. Install Qt 6

Download and install Qt 6.5+ via the [Qt Online Installer](https://www.qt.io/download), then set the `QT_ROOT_DIR` environment variable to point to the kit directory:

```bash
# Linux example
export QT_ROOT_DIR=$HOME/Qt/6.7.0/gcc_64
# macOS example
export QT_ROOT_DIR=$HOME/Qt/6.7.0/macos
```

### 3. Configure and build

```bash
cd openswmm.gui

# macOS (Release)
cmake --preset=Darwin
cmake --build --preset=Darwin

# Linux (Release)
cmake --preset=Linux
cmake --build --preset=Linux

# Windows (Release) — from a Visual Studio Developer Prompt
cmake --preset=Windows
cmake --build --preset=Windows
```

Debug builds append `-debug` to the preset name (e.g., `Darwin-debug`).

### 4. Run tests

```bash
cmake --preset=Darwin -DSWMMVIS_BUILD_TESTS=ON
cmake --build --preset=Darwin
ctest --test-dir build/darwin -C Release --output-on-failure
```

## API Documentation

Generated with Doxygen and published to GitHub Pages on every push to `main`/`swmm6_gui`:

```bash
doxygen docs/Doxyfile   # output → docs/html/
```

## Contributing

Contributions are welcome — bug reports, fixes, new features, docs, tests, and examples.

1. Read [CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow and the [Code of Conduct](CODE_OF_CONDUCT.md).
2. Fork the repo and create a feature branch.
3. Build the application and ensure the relevant unit and GUI tests pass with `ctest` (see [Building](#building)).
4. Follow existing style and naming, and update documentation for user-facing changes.
5. Open a PR against `dev`.

### Contributor License Agreement

First-time contributors must sign the project [CLA](CLA.md) before a pull request can be merged. The CLA grants the project a perpetual, royalty-free copyright and patent license to your contributions and preserves the project's ability to relicense in the future; **you retain full copyright ownership** of your work.

Sign by posting the following comment on your pull request, or by email as described in [CLA §9](CLA.md#9-how-to-sign):

> I have read the CLA Document and I hereby sign the CLA.

Once signed, the CLA covers future contributions to this project. Automated CLA signing is not yet enabled; see the CLA for current instructions.

### Corporate Contributor License Agreement

Organizations contributing through their employees or designated contributors must also submit a [Corporate CLA (CCLA)](CCLA.md), as described in [CLA §6](CLA.md#6-corporate-contributors). The CCLA supplements the individual CLA. Complete Schedules A, B, and C and submit the agreement using the instructions in those documents.

## License

SWMMVis is distributed under the **GNU General Public License, version 3 or later** — see [LICENSE](LICENSE).

## Acknowledgements

SWMMVis builds on the [OpenSWMM engine](https://github.com/HydroCouple/openswmm.engine) and uses Qt and GDAL for its interface and GIS capabilities. See [AUTHORS.md](AUTHORS.md) for project contributors and acknowledgements.

## Disclaimer

This open-source project is provided on an "as is" basis and the user assumes responsibility for its use.
