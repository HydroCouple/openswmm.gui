#!/bin/bash
# Falsification: connecting to a Web Feature Service and taking a
# collection off it.
#
# Every mutation still connects, still lists collections and still adds a
# layer. What they break is how much comes back, where on the earth it is
# from, whether a layer's features survive the call that created it, and
# whether a refusal is told apart from an empty answer.

set -u
cd "$(dirname "$0")/../.." || exit 1

BUILD=build
TEST=$BUILD/tests/gui/test_wfsservice
LOG=verification/wfs/falsify_wfsservice.log
: > "$LOG"

FILES=(src/layers/wfslayer.cpp src/ui/dialogs/addbasemapdialog.cpp)

backup() { for f in "${FILES[@]}"; do cp "$f" "$f.orig"; done; }
restore() { for f in "${FILES[@]}"; do cp "$f.orig" "$f"; touch "$f"; done; }
cleanup() { restore; for f in "${FILES[@]}"; do rm -f "$f.orig"; done; }
trap cleanup EXIT

backup

run_mutation() {
  local name="$1"; shift
  restore
  "$@" || { echo "MUTATION $name: could not be applied" | tee -a "$LOG"; return; }

  local changed=0
  for f in "${FILES[@]}"; do cmp -s "$f" "$f.orig" || changed=1; done
  if [ "$changed" -eq 0 ]; then
    echo "MUTATION $name: NOT APPLIED (stale pattern) -- inconclusive" | tee -a "$LOG"; return
  fi

  for f in "${FILES[@]}"; do touch "$f"; done

  if ! cmake --build $BUILD -j 8 --target test_wfsservice >> "$LOG" 2>&1; then
    echo "MUTATION $name: BUILD FAILED (inconclusive)" | tee -a "$LOG"; return
  fi

  local out
  out=$(QT_QPA_PLATFORM=offscreen SWMMVIS_GUI_TEST_DATA="$PWD/tests/gui/data" \
        "$TEST" 2>&1)
  local status=$?
  echo "=== $name ===" >> "$LOG"; echo "$out" | grep '^FAIL' >> "$LOG"

  if echo "$out" | grep -q '^FAIL'; then
    echo "MUTATION $name: CAUGHT -- $(echo "$out" | grep '^FAIL' | head -2 | sed 's/FAIL!  : TestWfsService:://' | cut -c1-60 | tr '\n' ' ')" | tee -a "$LOG"
  elif [ "$status" -ne 0 ]; then
    echo "MUTATION $name: CAUGHT (the suite died, status $status)" | tee -a "$LOG"
  else
    echo "MUTATION $name: *** SURVIVED ***" | tee -a "$LOG"
  fi
}

# ── the layer's bytes ───────────────────────────────────────────────────────

# NOTE on mutations 1 and 2. Both survive, and the reason is worth writing
# down rather than papering over: GDAL's GeoJSON driver parses the whole
# document when the dataset is opened, so once open it never reads the
# backing bytes again and neither borrowing them nor sharing the file name
# can be observed. They are kept because GDAL's contract does not promise
# that — a driver may read lazily, and this build's GISVectorLayer holds
# its dataset open for the layer's life — but no test here can bite them,
# and claiming otherwise would be worse than saying so.
#
# 1. The bytes are borrowed rather than copied, so the dataset reads a
#    buffer the caller has already destroyed -- the features are there at
#    first and gone, or wrong, the moment anything else uses that memory.
m_borrow_the_buffer() {
  perl -0pi -e 's/        reinterpret_cast<GByte \*>\(CPLMalloc\(static_cast<size_t>\(body\.size\(\)\)\)\),\n        body\.size\(\), TRUE\);/        reinterpret_cast<GByte *>(const_cast<char *>(body.constData())),\n        body.size(), FALSE);/' src/layers/wfslayer.cpp
  perl -0pi -e 's/    VSIFSeekL\(file, 0, SEEK_SET\);\n    VSIFWriteL\(body\.constData\(\), 1, static_cast<size_t>\(body\.size\(\)\), file\);\n//' src/layers/wfslayer.cpp
}

# 2. Every layer names its in-memory file the same, so a second layer pulls
#    the first one's open dataset out from under it.
m_shared_vsimem_name() {
  perl -0pi -e 's/        QStringLiteral\("\/vsimem\/swmmvis-wfs-%1"\)\n            \.arg\(QUuid::createUuid\(\)\.toString\(QUuid::WithoutBraces\)\);/        QStringLiteral("\/vsimem\/swmmvis-wfs");/' src/layers/wfslayer.cpp
}

# 3. An answer holding nothing is adopted, so an empty collection joins the
#    map as an empty layer nobody can explain.
m_accept_empty() {
  perl -0pi -e 's/    if \(count == 0\) \{/    if (false) {/' src/layers/wfslayer.cpp
}

# 4. A refusal is reported as gibberish, so the service's own account --
#    which names what is wrong -- is thrown away.
m_lose_the_refusal() {
  perl -0pi -e 's/        const QString said = refusalText\(body\);/        const QString said = QString();/' src/layers/wfslayer.cpp
}

# 5. The layer reports the in-memory path GDAL was handed, which names
#    nothing a user could open and does not survive the session.
m_expose_vsimem_path() {
  perl -0pi -e 's/QString WFSLayer::sourceDescription\(\) const\n\{/QString WFSLayer::sourceDescription() const\n{\n    return m_vsiPath;/' src/layers/wfslayer.cpp
}

# ── what is asked for ───────────────────────────────────────────────────────

# 6. The collection is fetched over everything the service holds rather
#    than over the ground the map is looking at.
m_ignore_the_view() {
  perl -0pi -e 's/    if \(!request\.crs\.isEmpty\(\) && !m_preferredExtent\.isNull\(\)\)\n        request\.extent = m_preferredExtent;/    \/\/ mutation: the view is ignored/' src/ui/dialogs/addbasemapdialog.cpp
}

# 7. Whatever format the collection lists first is asked for, which for
#    this service is GML -- readable only with a schema and a driver.
m_no_format_preference() {
  perl -0pi -e 's/    request\.outputFormat =\n        HydroCouple::Ogc::preferredOutputFormat\(\*type, m_wfsCaps\.outputFormats\);//' src/ui/dialogs/addbasemapdialog.cpp
}

# 8. The dialog accepts before the features are in hand, so a collection
#    holding nothing over that ground closes it on nothing at all.
m_accept_before_fetching() {
  perl -0pi -e 's/        if \(m_tabs->currentIndex\(\) == Wfs\) \{\n            fetchWFSThenAccept\(\);\n            return;\n        \}//' src/ui/dialogs/addbasemapdialog.cpp
}

# 9. Anything typed is fetched, so a stray word becomes a request to
#    whatever host Qt makes of it.
m_fetch_anything_typed() {
  perl -0pi -e 's/    if \(url\.isEmpty\(\)\) \{\n        m_wfsStatusText = tr\("That is not a web address\."\);\n        m_wfsStatus->setText\(m_wfsStatusText\);\n        return;\n    \}//' src/ui/dialogs/addbasemapdialog.cpp
}

# 10. An address that is not a feature service reads as one holding
#     nothing, so the service's refusal never reaches the user.
m_ignore_capabilities_failure() {
  perl -0pi -e 's/    if \(!m_wfsCaps\.ok\) \{/    if (false) {/' src/ui/dialogs/addbasemapdialog.cpp
}

run_mutation "1-borrow-the-buffer"        m_borrow_the_buffer
run_mutation "2-shared-vsimem-name"       m_shared_vsimem_name
run_mutation "3-accept-empty"             m_accept_empty
run_mutation "4-lose-the-refusal"         m_lose_the_refusal
run_mutation "5-expose-vsimem-path"       m_expose_vsimem_path
run_mutation "6-ignore-the-view"          m_ignore_the_view
run_mutation "7-no-format-preference"     m_no_format_preference
run_mutation "8-accept-before-fetching"   m_accept_before_fetching
run_mutation "9-fetch-anything-typed"     m_fetch_anything_typed
run_mutation "10-ignore-capabilities-failure" m_ignore_capabilities_failure

restore
touch "${FILES[@]}"
cmake --build $BUILD -j 8 --target test_wfsservice >> "$LOG" 2>&1
echo
echo "--- restored, clean run ---"
QT_QPA_PLATFORM=offscreen SWMMVIS_GUI_TEST_DATA="$PWD/tests/gui/data" "$TEST" 2>&1 | tail -2
