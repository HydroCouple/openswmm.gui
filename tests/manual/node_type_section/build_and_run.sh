#!/bin/sh
# Builds and runs tests/manual/node_type_section/node_type_probe — an offscreen
# renderer for the type-distinguishing node/link profiles. Standalone rather
# than a CMake target for the same reason as sp_render_probe: no moc is needed,
# so it stays out of the build graph and out of ctest.
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
HERE=$ROOT/tests/manual/node_type_section
QT=${QT_ROOT_DIR:-$HOME/Qt/6.9.3/macos}
ENG=$ROOT/../openswmm.engine/install/Darwin

clang++ -std=c++20 -fPIC -g -O0 -Wno-error=implicit-function-declaration \
  -I"$ROOT/include" -I"$ENG/include" \
  -F"$QT/lib" \
  -I"$QT/lib/QtCore.framework/Headers" \
  -I"$QT/lib/QtGui.framework/Headers" \
  -I"$QT/lib/QtWidgets.framework/Headers" \
  "$HERE/node_type_probe.cpp" \
  "$ROOT/src/ui/sectionview/sectiondiagram.cpp" \
  "$ROOT/src/ui/sectionview/sectionmodelbuilders.cpp" \
  "$ROOT/src/ui/sectionview/xsectsampler.cpp" \
  -L"$ENG/lib" -lopenswmm.engine -Wl,-rpath,"$ENG/lib" \
  -framework QtCore -framework QtGui -framework QtWidgets \
  -Wl,-rpath,"$QT/lib" \
  -o "$HERE/node_type_probe"

mkdir -p "$HERE/out"
QT_QPA_PLATFORM=offscreen "$HERE/node_type_probe" "$HERE/out"
