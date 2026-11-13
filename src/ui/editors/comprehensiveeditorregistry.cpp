/**
 * @file      comprehensiveeditorregistry.cpp
 * @brief     Single source of truth mapping each non-spatial data category to its comprehensive
 *            MVC editor.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/editors/comprehensiveeditorregistry.h"
#include <type_traits>

#include "aquifer/aquiferregistry.h"
#include "curve/curveregistry.h"
#include "inlet/inletregistry.h"
#include "landuse/landuseregistry.h"
#include "lid/lidcontrolregistry.h"
#include "pattern/patternregistry.h"
#include "pollutant/pollutantregistry.h"
#include "snowpack/snowpackregistry.h"
#include "street/streetregistry.h"
#include "timeseries/timeseriesregistry.h"
#include "transect/transectregistry.h"
#include "ui/dialogs/aquifereditordialog.h"
#include "ui/dialogs/curveeditordialog.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "ui/dialogs/inleteditordialog.h"
#include "ui/dialogs/landuseeditordialog.h"
#include "ui/dialogs/lidcontroleditordialog.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/dialogs/pollutanteditordialog.h"
#include "ui/dialogs/ruleseditordialog.h"
#include "ui/dialogs/snowpackeditordialog.h"
#include "ui/dialogs/streeteditordialog.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/dialogs/transecteditordialog.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPointer>
#include <QUndoStack>

namespace
{

using openswmmvis::aquifer::AquiferRegistry;
using openswmmvis::curve::CurveRegistry;
using openswmmvis::inlet::InletRegistry;
using openswmmvis::landuse::LandUseRegistry;
using openswmmvis::lid::LidControlRegistry;
using openswmmvis::pattern::PatternRegistry;
using openswmmvis::pollutant::PollutantRegistry;
using openswmmvis::snowpack::SnowpackRegistry;
using openswmmvis::street::StreetRegistry;
using openswmmvis::timeseries::TimeseriesRegistry;
using openswmmvis::transect::TransectRegistry;
using openswmmvis::ui::AquiferEditorDialog;
using openswmmvis::ui::CurveEditorDialog;
using openswmmvis::ui::InletEditorDialog;
using openswmmvis::ui::LandUseEditorDialog;
using openswmmvis::ui::LidControlEditorDialog;
using openswmmvis::ui::PatternEditorDialog;
using openswmmvis::ui::PollutantEditorDialog;
using openswmmvis::ui::RulesEditorDialog;
using openswmmvis::ui::SnowpackEditorDialog;
using openswmmvis::ui::StreetEditorDialog;
using openswmmvis::ui::TimeseriesEditorDialog;
using openswmmvis::ui::TransectEditorDialog;

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed undo stack passed to dialog/factory; dialog must not outlive
 *   required stack.
 * @brief Ensure typed Timeseries registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openTimeseriesCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
    if (!reg)
        return;
    auto *dlg = TimeseriesEditorDialog::createNew(reg, stack, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

// Review/browse launch (2026-08-31) — the Data-menu / ribbon "data object"
// actions open the editor without creating anything; the user picks from
// the editor's own list and creates via its Add/New button. Each
// openXxxBrowse below mirrors its openXxxCreateNew sibling's construction
// and engine-flush wiring, minus the create-card / invokeNew() step.

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed undo stack passed to dialog/factory; dialog must not outlive
 *   required stack.
 * @brief Ensure typed Timeseries registry and construct list editor without deliberately creating a
 *   new entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openTimeseriesBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
    if (!reg)
        return;
    auto *dlg = new TimeseriesEditorDialog(reg, stack,
                                           /*initialSelection=*/nullptr, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

template <class Registry>
void syncRegistryOnClose(QDialog *dialog, Registry *registry, SWMMModelLayer *layer)
{
    QObject::connect(dialog, &QDialog::finished, dialog,
                     [reg = QPointer<Registry>(registry), owner = QPointer<SWMMModelLayer>(layer),
                      engine = layer->engine()] {
                         if (reg && owner && owner->engine() == engine)
                             reg->saveToEngine(engine);
                     });
}

// Reuse a visible editor only for its owning model. A closed window can
// still have a live QPointer until Qt processes its deferred deletion.
template <class Dialog>
Dialog *projectEditor(SWMMModelLayer *layer, const std::function<Dialog *()> &create)
{
    static QPointer<SWMMModelLayer> owner;
    static QPointer<Dialog> window;
    if (window && window->isVisible() && owner == layer)
        return window;
    if (window && window->isVisible())
        window->close();
    owner = layer;
    window = create();
    window->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, window.data(), &QDialog::close);
    QObject::connect(layer, &QObject::destroyed, window.data(), &QDialog::close);
    return window;
}

HydrographGroupEditor *hydrographEditor(SWMMModelLayer *layer, QWidget *parent)
{
    return projectEditor<HydrographGroupEditor>(
        layer, [=] { return new HydrographGroupEditor(layer, parent); });
}

void openHydrographsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *editor = hydrographEditor(layer, parent);
    editor->setUndoStack(stack); // edits reach the model at Apply, one undo step each
    editor->beginNewGroup();
}

void openHydrographsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *editor = hydrographEditor(layer, parent);
    editor->setUndoStack(stack);
    editor->openForGroup({});
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed undo stack passed to dialog/factory; dialog must not outlive
 *   required stack.
 * @brief Ensure typed Patterns registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note This wrapper installs no additional engine-flush-on-close callback; dialog/registry
 *   implement persistence and undo.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openPatternsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<PatternRegistry *>(layer->ensurePatternRegistry());
    if (!reg)
        return;
    auto *dlg = PatternEditorDialog::createNew(reg, stack, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed undo stack passed to dialog/factory; dialog must not outlive
 *   required stack.
 * @brief Ensure typed Patterns registry and construct list editor without deliberately creating a
 *   new entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note This wrapper installs no additional engine-flush-on-close callback; dialog/registry
 *   implement persistence and undo.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openPatternsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<PatternRegistry *>(layer->ensurePatternRegistry());
    if (!reg)
        return;
    auto *dlg = new PatternEditorDialog(reg, stack, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed undo stack passed to dialog/factory; dialog must not outlive
 *   required stack.
 * @brief Ensure typed Curves registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note This wrapper installs no additional engine-flush-on-close callback; dialog/registry
 *   implement persistence and undo.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openCurvesCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<CurveRegistry *>(layer->ensureCurveRegistry());
    if (!reg)
        return;
    auto *dlg = CurveEditorDialog::createNew(reg, stack, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed undo stack passed to dialog/factory; dialog must not outlive
 *   required stack.
 * @brief Ensure typed Curves registry and construct list editor without deliberately creating a new
 *   entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse existing
 *   windows.
 * @note This wrapper installs no additional engine-flush-on-close callback; dialog/registry
 *   implement persistence and undo.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openCurvesBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<CurveRegistry *>(layer->ensureCurveRegistry());
    if (!reg)
        return;
    auto *dlg = new CurveEditorDialog(reg, stack, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed undo stack passed to dialog/factory; dialog must not outlive
 *   required stack.
 * @brief Ensure typed Transects registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openTransectsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<TransectRegistry *>(layer->ensureTransectRegistry());
    if (!reg)
        return;
    auto *dlg = TransectEditorDialog::createNew(reg, layer, stack, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Auto-flush the registry to the engine when the dialog closes. The
    // existing apply* helpers already mutate the engine inline, so this is
    // belt-and-braces for paths that mutate the provider directly (drags,
    // grid edits, etc.) bypassing the layer.
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed undo stack passed to dialog/factory; dialog must not outlive
 *   required stack.
 * @brief Ensure typed Transects registry and construct list editor without deliberately creating a
 *   new entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openTransectsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<TransectRegistry *>(layer->ensureTransectRegistry());
    if (!reg)
        return;
    auto *dlg = new TransectEditorDialog(reg, layer, stack, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed project undo stack; one Apply becomes one step on it.
 * @brief Ensure typed Streets registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openStreetsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<StreetRegistry *>(layer->ensureStreetRegistry());
    if (!reg)
        return;
    auto *dlg = StreetEditorDialog::createNew(reg, layer, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Auto-flush the registry to the engine when the dialog closes (mirrors
    // the transect path; inline apply* helpers already mutate the engine, so
    // this covers direct provider edits that bypass the layer).
    dlg->setUndoStack(stack); // edits reach the model at Apply, one undo step each
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed project undo stack; one Apply becomes one step on it.
 * @brief Ensure typed Streets registry and construct list editor without deliberately creating a
 *   new entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openStreetsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<StreetRegistry *>(layer->ensureStreetRegistry());
    if (!reg)
        return;
    auto *dlg = new StreetEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setUndoStack(stack); // edits reach the model at Apply, one undo step each
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed project undo stack; one Apply becomes one step on it.
 * @brief Ensure typed Pollutants registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openPollutantsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry());
    if (!reg)
        return;
    auto *dlg = PollutantEditorDialog::createNew(reg, layer, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setUndoStack(stack); // edits reach the model at Apply, one undo step each
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed project undo stack; one Apply becomes one step on it.
 * @brief Ensure typed Pollutants registry and construct list editor without deliberately creating a
 *   new entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openPollutantsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry());
    if (!reg)
        return;
    auto *dlg = new PollutantEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setUndoStack(stack);
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed project undo stack; one Apply becomes one step on it.
 * @brief Ensure typed Aquifers registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openAquifersCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<AquiferRegistry *>(layer->ensureAquiferRegistry());
    if (!reg)
        return;
    auto *dlg = AquiferEditorDialog::createNew(reg, layer, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setUndoStack(stack); // edits reach the model at Apply, one undo step each
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed project undo stack; one Apply becomes one step on it.
 * @brief Ensure typed Aquifers registry and construct list editor without deliberately creating a
 *   new entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openAquifersBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<AquiferRegistry *>(layer->ensureAquiferRegistry());
    if (!reg)
        return;
    auto *dlg = new AquiferEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setUndoStack(stack);
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @note Unnamed stack argument ignored; this launcher creates no undo command itself.
 * @brief Ensure typed LandUses registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Dialog finished queues synchronous registry saveToEngine(layer->engine()) in dialog
 *   context, guarded by QPointers to both layer/registry; errors ignored. It runs on accepted or
 *   rejected close, not just successful edits.
 * @note Also ensures pollutant registry and tracks it for live buildup/washoff dimensions; null
 *   typed registry passed through if unavailable.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openLandUsesCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<LandUseRegistry *>(layer->ensureLandUseRegistry());
    if (!reg)
        return;
    auto *dlg = LandUseEditorDialog::createNew(reg, layer, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Buildup/Washoff rows re-dimension live with the pollutant set.
    dlg->trackPollutantRegistry(
        qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry()));
    dlg->setUndoStack(stack); // edits reach the model at Apply, one undo step each
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @note Unnamed stack argument ignored; this launcher creates no undo command itself.
 * @brief Ensure typed LandUses registry and construct list editor without deliberately creating a
 *   new entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Dialog finished queues synchronous registry saveToEngine(layer->engine()) in dialog
 *   context, guarded by QPointers to both layer/registry; errors ignored. It runs on accepted or
 *   rejected close, not just successful edits.
 * @note Also ensures pollutant registry and tracks it for live buildup/washoff dimensions; null
 *   typed registry passed through if unavailable.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openLandUsesBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<LandUseRegistry *>(layer->ensureLandUseRegistry());
    if (!reg)
        return;
    auto *dlg = new LandUseEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Buildup/Washoff rows re-dimension live with the pollutant set.
    dlg->trackPollutantRegistry(
        qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry()));
    dlg->setUndoStack(stack); // edits reach the model at Apply, one undo step each
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @note Unnamed stack argument ignored; this launcher creates no undo command itself.
 * @brief Ensure typed Snowpacks registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Project-owned snowpack fields commit through the shared checked writer. Closing the
 *   dialog does not flush unrelated registry drafts.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openSnowpacksCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<SnowpackRegistry *>(layer->ensureSnowpackRegistry());
    if (!reg)
        return;
    auto *dlg = SnowpackEditorDialog::createNew(reg, layer, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setUndoStack(stack); // edits reach the model at Apply, one undo step each
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @note Unnamed stack argument ignored; this launcher creates no undo command itself.
 * @brief Ensure typed Snowpacks registry and construct list editor without deliberately creating a
 *   new entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Project-owned snowpack fields commit through the shared checked writer. Closing the
 *   dialog does not flush unrelated registry drafts.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openSnowpacksBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<SnowpackRegistry *>(layer->ensureSnowpackRegistry());
    if (!reg)
        return;
    auto *dlg = new SnowpackEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setUndoStack(stack); // edits reach the model at Apply, one undo step each
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed undo stack passed to dialog/factory; dialog must not outlive
 *   required stack.
 * @brief Ensure typed Inlets registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openInletsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<InletRegistry *>(layer->ensureInletRegistry());
    if (!reg)
        return;
    auto *dlg = InletEditorDialog::createNew(reg, layer, stack, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @param[in] stack Optional borrowed undo stack passed to dialog/factory; dialog must not outlive
 *   required stack.
 * @brief Ensure typed Inlets registry and construct list editor without deliberately creating a new
 *   entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse existing
 *   windows.
 * @note Edits reach the model at Apply, one step on \p stack; closing with unapplied edits
 *   asks Apply / Discard / Cancel.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openInletsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<InletRegistry *>(layer->ensureInletRegistry());
    if (!reg)
        return;
    auto *dlg = new InletEditorDialog(reg, layer, stack, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @note Unnamed stack argument ignored; this launcher creates no undo command itself.
 * @brief Ensure typed LidControls registry and invoke createNew factory, returning silently on null
 *   result. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Project-owned fields commit immediately through the shared checked writer. Closing
 *   dismisses the editor without flushing unrelated registry drafts.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openLidControlsCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<LidControlRegistry *>(layer->ensureLidControlRegistry());
    if (!reg)
        return;
    auto *dlg = LidControlEditorDialog::createNew(reg, layer, parent);
    if (!dlg)
        return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

/**
 * @param[in] layer Optional borrowed model; null no-op. Initialized registry/model must stay live
 *   while dialog uses it.
 * @param[in] parent Optional owner used only when constructing a new window; null parent does not
 *   retain model lifetime.
 * @note Unnamed stack argument ignored; this launcher creates no undo command itself.
 * @brief Ensure typed LidControls registry and construct list editor without deliberately creating
 *   a new entry. New dialog is WA_DeleteOnClose and shown modeless; this launcher does not reuse
 *   existing windows.
 * @note Project-owned fields commit immediately through the shared checked writer. Closing
 *   dismisses the editor without flushing unrelated registry drafts.
 * @note GUI-thread only. No success return; layer/registry/factory rejection leaves UI unchanged
 *   except any ensure/factory side effects. Modeless interactions may later mutate engine; caller
 *   retains external model lifetime.
 */
void openLidControlsBrowse(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer)
        return;
    auto *reg = qobject_cast<LidControlRegistry *>(layer->ensureLidControlRegistry());
    if (!reg)
        return;
    auto *dlg = new LidControlEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

RulesEditorDialog *rulesEditor(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent,
                               bool createNew = false)
{
    return projectEditor<RulesEditorDialog>(layer, [=] {
        return createNew ? RulesEditorDialog::createNew(layer, stack, parent)
                         : new RulesEditorDialog(layer, stack, parent);
    });
}

void openControlsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *dialog = rulesEditor(layer, stack, parent);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void openControlsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer)
        return;
    auto *dialog = rulesEditor(layer, stack, parent, true);
    if (dialog->isVisible())
        dialog->invokeNew();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void openHydrographForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                             QWidget *parent)
{
    auto *editor = hydrographEditor(layer, parent);
    editor->setUndoStack(stack);
    editor->openForGroup(name);
}

void openControlForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                          QWidget *parent)
{
    rulesEditor(layer, stack, parent)->openForRule(name);
}

void openPatternForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                          QWidget *parent)
{
    auto *reg = qobject_cast<PatternRegistry *>(layer->ensurePatternRegistry());
    if (!reg || !reg->findByName(name))
        return;
    auto *dialog = projectEditor<PatternEditorDialog>(
        layer, [=] { return new PatternEditorDialog(reg, stack, parent); });
    dialog->openForPattern(name);
}

void openCurveForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                        QWidget *parent)
{
    auto *reg = qobject_cast<CurveRegistry *>(layer->ensureCurveRegistry());
    if (!reg || !reg->findByName(name))
        return;
    auto *dialog = projectEditor<CurveEditorDialog>(
        layer, [=] { return new CurveEditorDialog(reg, stack, parent); });
    dialog->openForCurve(name);
}

void openTransectForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                           QWidget *parent)
{
    auto *reg = qobject_cast<TransectRegistry *>(layer->ensureTransectRegistry());
    if (!reg || !reg->findByName(name))
        return;
    auto *dialog = projectEditor<TransectEditorDialog>(layer, [=] {
        return new TransectEditorDialog(reg, layer, stack, parent); // edits reach the model at Apply
    });
    dialog->openForTransect(name);
}

void openInletForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                        QWidget *parent)
{
    auto *reg = qobject_cast<InletRegistry *>(layer->ensureInletRegistry());
    if (!reg || !reg->findByName(name))
        return;
    auto *dialog = projectEditor<InletEditorDialog>(layer, [=] {
        return new InletEditorDialog(reg, layer, stack, parent); // edits reach the model at Apply
    });
    dialog->openForInlet(name);
}

void openTimeseriesForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                             QWidget *parent)
{
    auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
    auto *provider = reg ? reg->findByName(name) : nullptr;
    if (!provider)
        return;
    auto *dialog = new TimeseriesEditorDialog(reg, stack, provider, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

/*! Shared body for the registry-backed list editors (pollutant, land use,
 *  aquifer, snowpack, LID control, street). Reuses one window per kind while
 *  it is bound to the same registry; a project switch (new registry) closes
 *  the stale window first. Snowpack fields commit immediately; the pollutant, aquifer and
 *  street editors write the model at Apply; the others keep their close-time registry
 *  synchronization.
 * @tparam Dlg Dialog constructible from registry, layer and parent, with finished/open member
 *   support.
 * @tparam Reg QObject registry providing saveToEngine(engine).
 * @param[in,out] slot Weak retained dialog slot; replaced after registry mismatch close.
 * @param[in,out] slotReg Weak registry identity slot set when a new dialog is made.
 * @param[in] reg Optional typed borrowed registry; null no-op.
 * @param[in] layer Borrowed model required by dialog, engine accessor on close guarded by QPointer.
 * @param[in] parent Optional owner for newly created dialog.
 * @param[in] openFn Required nonnull member pointer selecting/opening object; invoked
 *   synchronously.
 * @param[in] name Object ID forwarded verbatim to openFn.
 * @note If old dialog belongs to another registry, calls close then drops slot regardless of close
 *   acceptance; constructs replacement if absent, marks delete-on-close, wires guarded saveToEngine
 *   on finished, invokes open member. Does not track a changed layer when same registry supplied.
 *   No exception rollback, success return or stack support. GUI-thread serialized static callers
 *   only.
 */
template <class Dlg, class Reg>
void openRegistryEditorFor(QPointer<Dlg> &slot, QPointer<Reg> &slotReg, Reg *reg,
                           SWMMModelLayer *layer, QWidget *parent,
                           void (Dlg::*openFn)(const QString &), const QString &name)
{
    if (!reg)
        return;
    if (slot && (slotReg != reg || !slot->isVisible())) {
        slot->close();
        slot = nullptr;
    }
    if (!slot) {
        slot = new Dlg(reg, layer, parent);
        slot->setAttribute(Qt::WA_DeleteOnClose);
        slotReg = reg;
        QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, slot.data(), &QDialog::close);
        // Editors with explicit Apply write the model themselves.
        if constexpr (!std::is_same_v<Dlg, SnowpackEditorDialog> &&
                      !std::is_same_v<Dlg, LidControlEditorDialog> &&
                      !std::is_same_v<Dlg, PollutantEditorDialog> &&
                      !std::is_same_v<Dlg, AquiferEditorDialog> &&
                      !std::is_same_v<Dlg, StreetEditorDialog> &&
                      !std::is_same_v<Dlg, LandUseEditorDialog>)
            syncRegistryOnClose(slot.data(), reg, layer);
    }
    (slot.data()->*openFn)(name);
}

/**
 * @param[in] layer Optional borrowed model; null no-op.
 * @param[in] name Exact object/group ID passed through; wrapper does not reject empty/unknown name
 *   itself.
 * @param[in] parent Optional window owner, used only on new construction.
 * @param[in] stack Optional borrowed project undo stack; one Apply becomes one step on it.
 * @brief Ensure typed registry, then use per-kind static QPointer window/registry slots through
 *   openRegistryEditorFor. Registry changes close old window before constructing new; selected
 *   object opening delegated to kind-specific method. No close-time flush: edits reach the
 *   model at Apply; no success/error return.
 * @note GUI-thread only; registry ensure can initialize model state. Borrowed model/stack must
 *   outlive dialog use; no direct notification/undo command in this wrapper.
 */
void openPollutantForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                            QWidget *parent)
{
    if (!layer)
        return;
    static QPointer<PollutantEditorDialog> editor;
    static QPointer<PollutantRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry()),
                          layer, parent, &PollutantEditorDialog::openForPollutant, name);
    if (editor)
        editor->setUndoStack(stack);
}

/**
 * @param[in] layer Optional borrowed model; null no-op.
 * @param[in] name Exact object/group ID passed through; wrapper does not reject empty/unknown name
 *   itself.
 * @param[in] parent Optional window owner, used only on new construction.
 * @note Unnamed stack argument ignored.
 * @brief Ensure typed registry, then use per-kind static QPointer window/registry slots through
 *   openRegistryEditorFor. Registry changes close old window before constructing new; selected
 *   object opening delegated to kind-specific method. Close-time flush guarded by layer/registry
 *   pointers; no success/error return.
 * @note GUI-thread only; registry ensure can initialize model state. Borrowed model/stack must
 *   outlive dialog use; no direct notification/undo command in this wrapper.
 */
void openLandUseForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                          QWidget *parent)
{
    if (!layer)
        return;
    static QPointer<LandUseEditorDialog> editor;
    static QPointer<LandUseRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<LandUseRegistry *>(layer->ensureLandUseRegistry()), layer,
                          parent, &LandUseEditorDialog::openForLandUse, name);
    if (editor)
        editor->setUndoStack(stack);
}

/**
 * @param[in] layer Optional borrowed model; null no-op.
 * @param[in] name Exact object/group ID passed through; wrapper does not reject empty/unknown name
 *   itself.
 * @param[in] parent Optional window owner, used only on new construction.
 * @param[in] stack Optional borrowed project undo stack; one Apply becomes one step on it.
 * @brief Ensure typed registry, then use per-kind static QPointer window/registry slots through
 *   openRegistryEditorFor. Registry changes close old window before constructing new; selected
 *   object opening delegated to kind-specific method. No close-time flush: edits reach the
 *   model at Apply; no success/error return.
 * @note GUI-thread only; registry ensure can initialize model state. Borrowed model/stack must
 *   outlive dialog use; no direct notification/undo command in this wrapper.
 */
void openAquiferForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                          QWidget *parent)
{
    if (!layer)
        return;
    static QPointer<AquiferEditorDialog> editor;
    static QPointer<AquiferRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<AquiferRegistry *>(layer->ensureAquiferRegistry()), layer,
                          parent, &AquiferEditorDialog::openForAquifer, name);
    if (editor)
        editor->setUndoStack(stack);
}

/**
 * @param[in] layer Optional borrowed model; null no-op.
 * @param[in] name Exact object/group ID passed through; wrapper does not reject empty/unknown name
 *   itself.
 * @param[in] parent Optional window owner, used only on new construction.
 * @note Unnamed stack argument ignored.
 * @brief Ensure typed registry, then use per-kind static QPointer window/registry slots through
 *   openRegistryEditorFor. Registry changes close old window before constructing new; selected
 *   object opening delegated to kind-specific method. Close-time flush guarded by layer/registry
 *   pointers; no success/error return.
 * @note GUI-thread only; registry ensure can initialize model state. Borrowed model/stack must
 *   outlive dialog use; no direct notification/undo command in this wrapper.
 */
void openSnowpackForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                           QWidget *parent)
{
    if (!layer)
        return;
    static QPointer<SnowpackEditorDialog> editor;
    static QPointer<SnowpackRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<SnowpackRegistry *>(layer->ensureSnowpackRegistry()), layer,
                          parent, &SnowpackEditorDialog::openForSnowpack, name);
    if (editor)
        editor->setUndoStack(stack);
}

/**
 * @param[in] layer Optional borrowed model; null no-op.
 * @param[in] name Exact object/group ID passed through; wrapper does not reject empty/unknown name
 *   itself.
 * @param[in] parent Optional window owner, used only on new construction.
 * @note Unnamed stack argument ignored.
 * @brief Ensure typed registry, then use per-kind static QPointer window/registry slots through
 *   openRegistryEditorFor. Registry changes close old window before constructing new; selected
 *   object opening delegated to kind-specific method. Close-time flush guarded by layer/registry
 *   pointers; no success/error return.
 * @note GUI-thread only; registry ensure can initialize model state. Borrowed model/stack must
 *   outlive dialog use; no direct notification/undo command in this wrapper.
 */
void openLidControlForObject(SWMMModelLayer *layer, QUndoStack * /*stack*/, const QString &name,
                             QWidget *parent)
{
    if (!layer)
        return;
    static QPointer<LidControlEditorDialog> editor;
    static QPointer<LidControlRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<LidControlRegistry *>(layer->ensureLidControlRegistry()),
                          layer, parent, &LidControlEditorDialog::openForLidControl, name);
}

/**
 * @param[in] layer Optional borrowed model; null no-op.
 * @param[in] name Exact object/group ID passed through; wrapper does not reject empty/unknown name
 *   itself.
 * @param[in] parent Optional window owner, used only on new construction.
 * @param[in] stack Optional borrowed project undo stack; one Apply becomes one step on it.
 * @brief Ensure typed registry, then use per-kind static QPointer window/registry slots through
 *   openRegistryEditorFor. Registry changes close old window before constructing new; selected
 *   object opening delegated to kind-specific method. No close-time flush: edits reach the
 *   model at Apply; no success/error return.
 * @note GUI-thread only; registry ensure can initialize model state. Borrowed model/stack must
 *   outlive dialog use; no direct notification/undo command in this wrapper.
 */
void openStreetForObject(SWMMModelLayer *layer, QUndoStack *stack, const QString &name,
                         QWidget *parent)
{
    if (!layer)
        return;
    static QPointer<StreetEditorDialog> editor;
    static QPointer<StreetRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<StreetRegistry *>(layer->ensureStreetRegistry()), layer,
                          parent, &StreetEditorDialog::openForStreet, name);
    if (editor)
        editor->setUndoStack(stack);
}

/*! Populates the registry with every non-spatial category in
 *  `SWMMModelLayer::DataCategory`. Shipped categories get a non-null
 *  `openCreateNew`; gap categories carry only a `gapSliceLabel` so the
 *  three consuming surfaces can render a disabled action with tooltip.
 * @param[in,out] reg Registry receiving 13 builtin category entries, replacing any same keys.
 * @brief Install translated title and create/browse/object function pointers for every current
 *   nonspatial data category.
 * @note No dialogs/layers are created during registration. Despite name, helper has no internal
 *   once guard; instance() owns that policy. Stored strings translated at population time only.
 */
void populateOnce(ComprehensiveEditorRegistry &reg)
{
    using DC = SWMMModelLayer;
    using Entry = ComprehensiveEditorRegistry::Entry;

    // Shipped (four).
    reg.registerEditor(
        DC::DataTimeSeries,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Time Series Editor"),
              QString(), &openTimeseriesCreateNew, &openTimeseriesBrowse,
              &openTimeseriesForObject});

    reg.registerEditor(
        DC::DataHydrographs,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Hydrograph Group Editor"),
              QString(), &openHydrographsCreateNew, &openHydrographsBrowse,
              &openHydrographForObject});

    reg.registerEditor(
        DC::DataPatterns,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Pattern Editor"),
              QString(), &openPatternsCreateNew, &openPatternsBrowse, &openPatternForObject});

    reg.registerEditor(
        DC::DataCurves,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Curve Editor"), QString(),
              &openCurvesCreateNew, &openCurvesBrowse, &openCurveForObject});

    reg.registerEditor(
        DC::DataControls,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Rules Editor"), QString(),
              &openControlsCreateNew, &openControlsBrowse, &openControlForObject});

    // Every non-spatial data category now ships a comprehensive editor — no
    // gap placeholders remain. (gapTooltip()/gapSliceLabel stay in the API for
    // any future category registered without an editor.)
    reg.registerEditor(
        DC::DataTransects,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Transect Editor"),
              QString(), &openTransectsCreateNew, &openTransectsBrowse, &openTransectForObject});
    reg.registerEditor(
        DC::DataLIDControls,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "LID Control Editor"),
              QString(), &openLidControlsCreateNew, &openLidControlsBrowse,
              &openLidControlForObject});
    reg.registerEditor(
        DC::DataPollutants,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Pollutant Editor"),
              QString(), &openPollutantsCreateNew, &openPollutantsBrowse, &openPollutantForObject});
    reg.registerEditor(
        DC::DataLandUses,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Land Use Editor"),
              QString(), &openLandUsesCreateNew, &openLandUsesBrowse, &openLandUseForObject});
    reg.registerEditor(
        DC::DataAquifers,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Aquifer Editor"),
              QString(), &openAquifersCreateNew, &openAquifersBrowse, &openAquiferForObject});
    reg.registerEditor(
        DC::DataSnowpacks,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Snowpack Editor"),
              QString(), &openSnowpacksCreateNew, &openSnowpacksBrowse, &openSnowpackForObject});
    reg.registerEditor(
        DC::DataStreets,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Street Editor"),
              QString(), &openStreetsCreateNew, &openStreetsBrowse, &openStreetForObject});
    reg.registerEditor(
        DC::DataInlets,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", "Inlet Editor"), QString(),
              &openInletsCreateNew, &openInletsBrowse, &openInletForObject});
}

} // anonymous namespace

ComprehensiveEditorRegistry &ComprehensiveEditorRegistry::instance()
{
    static ComprehensiveEditorRegistry registry;
    static bool populated = false;
    if (!populated) {
        populated = true; // set first so re-entrant lookups during
                          // populateOnce see a non-empty map
        populateOnce(registry);
    }
    return registry;
}

void ComprehensiveEditorRegistry::registerEditor(SWMMModelLayer::DataCategory cat, Entry entry)
{
    m_entries.insert(static_cast<int>(cat), std::move(entry));
}

const ComprehensiveEditorRegistry::Entry *
ComprehensiveEditorRegistry::find(SWMMModelLayer::DataCategory cat) const noexcept
{
    auto it = m_entries.constFind(static_cast<int>(cat));
    return it == m_entries.constEnd() ? nullptr : &it.value();
}

bool ComprehensiveEditorRegistry::hasEditor(SWMMModelLayer::DataCategory cat) const noexcept
{
    const Entry *e = find(cat);
    return e && static_cast<bool>(e->openCreateNew);
}

QString ComprehensiveEditorRegistry::gapTooltip(SWMMModelLayer::DataCategory cat) const
{
    const Entry *e = find(cat);
    if (!e || e->openCreateNew)
        return QString();
    return e->gapSliceLabel;
}

QString ComprehensiveEditorRegistry::editorTitle(SWMMModelLayer::DataCategory cat) const
{
    const Entry *e = find(cat);
    return e ? e->editorTitle : QString();
}

bool ComprehensiveEditorRegistry::openForObject(SWMMModelLayer::DataCategory cat,
                                                SWMMModelLayer *layer, QUndoStack *undoStack,
                                                const QString &name, QWidget *parent) const
{
    const Entry *e = find(cat);
    if (!e || !e->openForObject || !layer || name.isEmpty())
        return false;
    const QString id = existingObjectName(cat, layer, name, parent);
    if (id.isEmpty())
        return false;
    e->openForObject(layer, undoStack, id, parent);
    return true;
}

QString ComprehensiveEditorRegistry::existingObjectName(SWMMModelLayer::DataCategory cat,
                                                        SWMMModelLayer *layer, const QString &name,
                                                        QWidget *parent)
{
    if (!layer || name.isEmpty())
        return {};
    const int count = layer->dataObjectCount(cat);
    for (int row = 0; row < count; ++row) {
        const QString id = layer->dataObjectNameAt(cat, row);
        if (id.compare(name, Qt::CaseInsensitive) == 0)
            return id;
    }
    if (parent)
        QMessageBox::information(parent, QObject::tr("Edit data object"),
                                 QObject::tr("“%1” is no longer in this model.").arg(name));
    return {};
}
