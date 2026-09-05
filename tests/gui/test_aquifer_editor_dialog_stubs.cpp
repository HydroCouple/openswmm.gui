/*!
 * \file  test_aquifer_editor_dialog_stubs.cpp
 * \brief Link-only stubs so test_aquifer_editor_dialog can build without the
 *        full model-layer and pattern-editor chains.
 *
 * aquifereditordialog.cpp reaches SWMMModelLayer::ensurePatternRegistry() and
 * PatternEditorDialog::pickPattern() only from the evaporation-pattern "…"
 * button, which is disabled when the dialog has no layer — the case the unit
 * test drives (it binds a bare AquiferRegistry, as test_landuse_unified_editor
 * does). Trivial definitions satisfy the linker without pulling in the
 * ~5000-line swmmmodellayer.cpp or the chart-backed pattern editor — the same
 * technique as test_nodepropertyadapter_stubs.cpp. The real implementations
 * live in src/layers/swmmmodellayer.cpp and src/ui/dialogs/patterneditordialog.cpp
 * and are covered by the app build.
 */

#include "layers/swmmmodellayer.h"
#include "ui/dialogs/patterneditordialog.h"

QObject *SWMMModelLayer::ensurePatternRegistry() { return nullptr; }

QString openswmmvis::ui::PatternEditorDialog::pickPattern(
    openswmmvis::pattern::PatternRegistry *, QUndoStack *,
    const QString &, QWidget *)
{
    return {};
}
