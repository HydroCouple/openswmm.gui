/*!
 * \file   comprehensiveeditorregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/editors/comprehensiveeditorregistry.h"

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
#include <QPointer>
#include <QUndoStack>

namespace {

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

void openTimeseriesCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
    if (!reg) return;
    auto *dlg = TimeseriesEditorDialog::createNew(reg, stack, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Phase 6.7.3.7 — auto-flush newly-created provider to engine on close.
    QPointer<TimeseriesRegistry> regPtr(reg);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr]() {
        if (regPtr) regPtr->saveToEngine();
    });
    dlg->show();
}

// Review/browse launch (2026-08-31) — the Data-menu / ribbon "data object"
// actions open the editor without creating anything; the user picks from
// the editor's own list and creates via its Add/New button. Each
// openXxxBrowse below mirrors its openXxxCreateNew sibling's construction
// and engine-flush wiring, minus the create-card / invokeNew() step.

void openTimeseriesBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
    if (!reg) return;
    auto *dlg = new TimeseriesEditorDialog(reg, stack,
                                           /*initialSelection=*/nullptr, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<TimeseriesRegistry> regPtr(reg);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr]() {
        if (regPtr) regPtr->saveToEngine();
    });
    dlg->show();
}

// Singleton-raise: opening from two surfaces must reuse one dialog
// instance ([[feedback_mvc_synchronized_uis]]). Shared between the
// create-new and browse launches so they never spawn parallel windows.
static QPointer<HydrographGroupEditor> sHydrographEditor;

void openHydrographsCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    if (!sHydrographEditor)
        sHydrographEditor = new HydrographGroupEditor(layer, parent);
    sHydrographEditor->beginNewGroup();
}

void openHydrographsBrowse(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    if (!sHydrographEditor)
        sHydrographEditor = new HydrographGroupEditor(layer, parent);
    // Empty name = show without selecting or creating anything.
    sHydrographEditor->openForGroup(QString());
}

void openPatternsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<PatternRegistry *>(layer->ensurePatternRegistry());
    if (!reg) return;
    auto *dlg = PatternEditorDialog::createNew(reg, stack, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void openPatternsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<PatternRegistry *>(layer->ensurePatternRegistry());
    if (!reg) return;
    auto *dlg = new PatternEditorDialog(reg, stack, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void openCurvesCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<CurveRegistry *>(layer->ensureCurveRegistry());
    if (!reg) return;
    auto *dlg = CurveEditorDialog::createNew(reg, stack, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void openCurvesBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<CurveRegistry *>(layer->ensureCurveRegistry());
    if (!reg) return;
    auto *dlg = new CurveEditorDialog(reg, stack, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void openTransectsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<TransectRegistry *>(layer->ensureTransectRegistry());
    if (!reg) return;
    auto *dlg = TransectEditorDialog::createNew(reg, layer, stack, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Auto-flush the registry to the engine when the dialog closes. The
    // existing apply* helpers already mutate the engine inline, so this is
    // belt-and-braces for paths that mutate the provider directly (drags,
    // grid edits, etc.) bypassing the layer.
    QPointer<TransectRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>   layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr)
            regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openTransectsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<TransectRegistry *>(layer->ensureTransectRegistry());
    if (!reg) return;
    auto *dlg = new TransectEditorDialog(reg, layer, stack, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<TransectRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>   layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr)
            regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openStreetsCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<StreetRegistry *>(layer->ensureStreetRegistry());
    if (!reg) return;
    auto *dlg = StreetEditorDialog::createNew(reg, layer, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Auto-flush the registry to the engine when the dialog closes (mirrors
    // the transect path; inline apply* helpers already mutate the engine, so
    // this covers direct provider edits that bypass the layer).
    QPointer<StreetRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>  layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr)
            regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openStreetsBrowse(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<StreetRegistry *>(layer->ensureStreetRegistry());
    if (!reg) return;
    auto *dlg = new StreetEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<StreetRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>  layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr)
            regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openPollutantsCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry());
    if (!reg) return;
    auto *dlg = PollutantEditorDialog::createNew(reg, layer, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<PollutantRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>     layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr)
            regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openPollutantsBrowse(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry());
    if (!reg) return;
    auto *dlg = new PollutantEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<PollutantRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>     layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr)
            regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openAquifersCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<AquiferRegistry *>(layer->ensureAquiferRegistry());
    if (!reg) return;
    auto *dlg = AquiferEditorDialog::createNew(reg, layer, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<AquiferRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>   layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr)
            regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openAquifersBrowse(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<AquiferRegistry *>(layer->ensureAquiferRegistry());
    if (!reg) return;
    auto *dlg = new AquiferEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<AquiferRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>   layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr)
            regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openLandUsesCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<LandUseRegistry *>(layer->ensureLandUseRegistry());
    if (!reg) return;
    auto *dlg = LandUseEditorDialog::createNew(reg, layer, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Buildup/Washoff rows re-dimension live with the pollutant set.
    dlg->trackPollutantRegistry(
        qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry()));
    QPointer<LandUseRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>   layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr)
            regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openLandUsesBrowse(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<LandUseRegistry *>(layer->ensureLandUseRegistry());
    if (!reg) return;
    auto *dlg = new LandUseEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Buildup/Washoff rows re-dimension live with the pollutant set.
    dlg->trackPollutantRegistry(
        qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry()));
    QPointer<LandUseRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>   layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr)
            regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openSnowpacksCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<SnowpackRegistry *>(layer->ensureSnowpackRegistry());
    if (!reg) return;
    auto *dlg = SnowpackEditorDialog::createNew(reg, layer, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<SnowpackRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>    layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr) regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openSnowpacksBrowse(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<SnowpackRegistry *>(layer->ensureSnowpackRegistry());
    if (!reg) return;
    auto *dlg = new SnowpackEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<SnowpackRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>    layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr) regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openInletsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<InletRegistry *>(layer->ensureInletRegistry());
    if (!reg) return;
    auto *dlg = InletEditorDialog::createNew(reg, layer, stack, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<InletRegistry>  regPtr(reg);
    QPointer<SWMMModelLayer> layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr) regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openInletsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<InletRegistry *>(layer->ensureInletRegistry());
    if (!reg) return;
    auto *dlg = new InletEditorDialog(reg, layer, stack, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<InletRegistry>  regPtr(reg);
    QPointer<SWMMModelLayer> layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr) regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openLidControlsCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<LidControlRegistry *>(layer->ensureLidControlRegistry());
    if (!reg) return;
    auto *dlg = LidControlEditorDialog::createNew(reg, layer, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<LidControlRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>     layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr) regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

void openLidControlsBrowse(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<LidControlRegistry *>(layer->ensureLidControlRegistry());
    if (!reg) return;
    auto *dlg = new LidControlEditorDialog(reg, layer, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<LidControlRegistry> regPtr(reg);
    QPointer<SWMMModelLayer>     layerPtr(layer);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr, layerPtr]() {
        if (regPtr && layerPtr) regPtr->saveToEngine(layerPtr->engine());
    });
    dlg->show();
}

// Per-session singleton: once the dialog closes (WA_DeleteOnClose),
// the QPointer auto-nullifies and the next launch constructs a fresh
// instance. Simultaneous entry points (Object Browser Add-New,
// PropertiesPanel browse, Data-menu browse) raise the same window
// ([[feedback_mvc_synchronized_uis]]).
static QPointer<RulesEditorDialog> sRulesEditor;

void openControlsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    if (!sRulesEditor) {
        sRulesEditor = RulesEditorDialog::createNew(layer, stack, parent);
        if (sRulesEditor) sRulesEditor->setAttribute(Qt::WA_DeleteOnClose);
    } else {
        sRulesEditor->invokeNew();
    }
    if (sRulesEditor) {
        sRulesEditor->show();
        sRulesEditor->raise();
        sRulesEditor->activateWindow();
    }
}

void openControlsBrowse(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    if (!sRulesEditor) {
        sRulesEditor = new RulesEditorDialog(layer, stack, parent);
        sRulesEditor->setAttribute(Qt::WA_DeleteOnClose);
    }
    sRulesEditor->show();
    sRulesEditor->raise();
    sRulesEditor->activateWindow();
}

// ── Open-for-object launches ───────────────────────────────────────────────
// One modeless window per editor kind, reused across the Object Browser
// (leaf Edit… / double-click), the Properties panel "Open in…" button and
// the Attribute Table row "Edit in…" action. Moved here from
// ObjectBrowserPanel::openComprehensiveEditorFor so every surface reads the
// same table that answers hasEditor().

void openHydrographForObject(SWMMModelLayer *layer, QUndoStack * /*stack*/,
                             const QString &name, QWidget *parent)
{
    if (!layer) return;
    if (!sHydrographEditor)
        sHydrographEditor = new HydrographGroupEditor(layer, parent);
    sHydrographEditor->openForGroup(name);
}

void openCurveForObject(SWMMModelLayer *layer, QUndoStack *stack,
                        const QString &name, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<CurveRegistry *>(layer->ensureCurveRegistry());
    if (!reg) return;
    static QPointer<CurveEditorDialog> editor;
    if (!editor) editor = new CurveEditorDialog(reg, stack, parent);
    editor->openForCurve(name);
}

void openPatternForObject(SWMMModelLayer *layer, QUndoStack *stack,
                          const QString &name, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<PatternRegistry *>(layer->ensurePatternRegistry());
    if (!reg) return;
    static QPointer<PatternEditorDialog> editor;
    if (!editor) editor = new PatternEditorDialog(reg, stack, parent);
    editor->openForPattern(name);
}

void openTransectForObject(SWMMModelLayer *layer, QUndoStack *stack,
                           const QString &name, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<TransectRegistry *>(layer->ensureTransectRegistry());
    if (!reg) return;
    static QPointer<TransectEditorDialog> editor;
    if (!editor) editor = new TransectEditorDialog(reg, layer, stack, parent);
    editor->openForTransect(name);
}

void openInletForObject(SWMMModelLayer *layer, QUndoStack *stack,
                        const QString &name, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<InletRegistry *>(layer->ensureInletRegistry());
    if (!reg) return;
    static QPointer<InletEditorDialog> editor;
    if (!editor) editor = new InletEditorDialog(reg, layer, stack, parent);
    editor->openForInlet(name);
}

void openControlForObject(SWMMModelLayer *layer, QUndoStack *stack,
                          const QString &name, QWidget *parent)
{
    if (!layer) return;
    if (!sRulesEditor) {
        sRulesEditor = new RulesEditorDialog(layer, stack, parent);
        sRulesEditor->setAttribute(Qt::WA_DeleteOnClose);
    }
    sRulesEditor->openForRule(name);
}

void openTimeseriesForObject(SWMMModelLayer *layer, QUndoStack *stack,
                             const QString &name, QWidget *parent)
{
    using openswmmvis::timeseries::TimeseriesProvider;
    if (!layer) return;
    auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
    if (!reg) return;
    TimeseriesProvider *p = reg->findByName(name);
    if (!p) {
        // Engine has it but registry didn't load — recreate empty so the
        // editor at least opens and the user can see the rejection state.
        p = reg->create(name);
        if (!p) return;
    }
    auto *dlg = new TimeseriesEditorDialog(reg, stack, p, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<TimeseriesRegistry> regPtr(reg);
    QObject::connect(dlg, &QDialog::finished, dlg, [regPtr]() {
        if (regPtr) regPtr->saveToEngine();
    });
    dlg->show();
}

/*! Shared body for the registry-backed list editors (pollutant, land use,
 *  aquifer, snowpack, LID control, street). Reuses one window per kind while
 *  it is bound to the same registry; a project switch (new registry) closes
 *  the stale window first. The engine flush on close mirrors openXxxBrowse. */
template <class Dlg, class Reg>
void openRegistryEditorFor(QPointer<Dlg> &slot, QPointer<Reg> &slotReg,
                           Reg *reg, SWMMModelLayer *layer, QWidget *parent,
                           void (Dlg::*openFn)(const QString &),
                           const QString &name)
{
    if (!reg) return;
    if (slot && slotReg != reg) {
        slot->close();
        slot = nullptr;
    }
    if (!slot) {
        slot = new Dlg(reg, layer, parent);
        slot->setAttribute(Qt::WA_DeleteOnClose);
        slotReg = reg;
        QPointer<Reg>            regPtr(reg);
        QPointer<SWMMModelLayer> layerPtr(layer);
        QObject::connect(slot.data(), &QDialog::finished, slot.data(),
                         [regPtr, layerPtr]() {
            if (regPtr && layerPtr)
                regPtr->saveToEngine(layerPtr->engine());
        });
    }
    (slot.data()->*openFn)(name);
}

void openPollutantForObject(SWMMModelLayer *layer, QUndoStack * /*stack*/,
                            const QString &name, QWidget *parent)
{
    if (!layer) return;
    static QPointer<PollutantEditorDialog> editor;
    static QPointer<PollutantRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry()),
                          layer, parent, &PollutantEditorDialog::openForPollutant, name);
}

void openLandUseForObject(SWMMModelLayer *layer, QUndoStack * /*stack*/,
                          const QString &name, QWidget *parent)
{
    if (!layer) return;
    static QPointer<LandUseEditorDialog> editor;
    static QPointer<LandUseRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<LandUseRegistry *>(layer->ensureLandUseRegistry()),
                          layer, parent, &LandUseEditorDialog::openForLandUse, name);
}

void openAquiferForObject(SWMMModelLayer *layer, QUndoStack * /*stack*/,
                          const QString &name, QWidget *parent)
{
    if (!layer) return;
    static QPointer<AquiferEditorDialog> editor;
    static QPointer<AquiferRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<AquiferRegistry *>(layer->ensureAquiferRegistry()),
                          layer, parent, &AquiferEditorDialog::openForAquifer, name);
}

void openSnowpackForObject(SWMMModelLayer *layer, QUndoStack * /*stack*/,
                           const QString &name, QWidget *parent)
{
    if (!layer) return;
    static QPointer<SnowpackEditorDialog> editor;
    static QPointer<SnowpackRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<SnowpackRegistry *>(layer->ensureSnowpackRegistry()),
                          layer, parent, &SnowpackEditorDialog::openForSnowpack, name);
}

void openLidControlForObject(SWMMModelLayer *layer, QUndoStack * /*stack*/,
                             const QString &name, QWidget *parent)
{
    if (!layer) return;
    static QPointer<LidControlEditorDialog> editor;
    static QPointer<LidControlRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<LidControlRegistry *>(layer->ensureLidControlRegistry()),
                          layer, parent, &LidControlEditorDialog::openForLidControl, name);
}

void openStreetForObject(SWMMModelLayer *layer, QUndoStack * /*stack*/,
                         const QString &name, QWidget *parent)
{
    if (!layer) return;
    static QPointer<StreetEditorDialog> editor;
    static QPointer<StreetRegistry> editorReg;
    openRegistryEditorFor(editor, editorReg,
                          qobject_cast<StreetRegistry *>(layer->ensureStreetRegistry()),
                          layer, parent, &StreetEditorDialog::openForStreet, name);
}

/*! Populates the registry with every non-spatial category in
 *  `SWMMModelLayer::DataCategory`. Shipped categories get a non-null
 *  `openCreateNew`; gap categories carry only a `gapSliceLabel` so the
 *  three consuming surfaces can render a disabled action with tooltip. */
void populateOnce(ComprehensiveEditorRegistry &reg)
{
    using DC = SWMMModelLayer;
    using Entry = ComprehensiveEditorRegistry::Entry;

    // Shipped (four).
    reg.registerEditor(DC::DataTimeSeries,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Time Series Editor"),
              QString(), &openTimeseriesCreateNew, &openTimeseriesBrowse,
              &openTimeseriesForObject});

    reg.registerEditor(DC::DataHydrographs,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Hydrograph Group Editor"),
              QString(), &openHydrographsCreateNew, &openHydrographsBrowse,
              &openHydrographForObject});

    reg.registerEditor(DC::DataPatterns,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Pattern Editor"),
              QString(), &openPatternsCreateNew, &openPatternsBrowse,
              &openPatternForObject});

    reg.registerEditor(DC::DataCurves,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Curve Editor"),
              QString(), &openCurvesCreateNew, &openCurvesBrowse,
              &openCurveForObject});

    reg.registerEditor(DC::DataControls,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Rules Editor"),
              QString(), &openControlsCreateNew, &openControlsBrowse,
              &openControlForObject});

    // Every non-spatial data category now ships a comprehensive editor — no
    // gap placeholders remain. (gapTooltip()/gapSliceLabel stay in the API for
    // any future category registered without an editor.)
    reg.registerEditor(DC::DataTransects,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Transect Editor"),
              QString(), &openTransectsCreateNew, &openTransectsBrowse,
              &openTransectForObject});
    reg.registerEditor(DC::DataLIDControls,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "LID Control Editor"),
              QString(), &openLidControlsCreateNew, &openLidControlsBrowse,
              &openLidControlForObject});
    reg.registerEditor(DC::DataPollutants,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Pollutant Editor"),
              QString(), &openPollutantsCreateNew, &openPollutantsBrowse,
              &openPollutantForObject});
    reg.registerEditor(DC::DataLandUses,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Land Use Editor"),
              QString(), &openLandUsesCreateNew, &openLandUsesBrowse,
              &openLandUseForObject});
    reg.registerEditor(DC::DataAquifers,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Aquifer Editor"),
              QString(), &openAquifersCreateNew, &openAquifersBrowse,
              &openAquiferForObject});
    reg.registerEditor(DC::DataSnowpacks,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Snowpack Editor"),
              QString(), &openSnowpacksCreateNew, &openSnowpacksBrowse,
              &openSnowpackForObject});
    reg.registerEditor(DC::DataStreets,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Street Editor"),
              QString(), &openStreetsCreateNew, &openStreetsBrowse,
              &openStreetForObject});
    reg.registerEditor(DC::DataInlets,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Inlet Editor"),
              QString(), &openInletsCreateNew, &openInletsBrowse,
              &openInletForObject});
}

} // anonymous namespace

ComprehensiveEditorRegistry &ComprehensiveEditorRegistry::instance()
{
    static ComprehensiveEditorRegistry registry;
    static bool populated = false;
    if (!populated) {
        populated = true;        // set first so re-entrant lookups during
                                 // populateOnce see a non-empty map
        populateOnce(registry);
    }
    return registry;
}

void ComprehensiveEditorRegistry::registerEditor(SWMMModelLayer::DataCategory cat,
                                                 Entry entry)
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
    if (!e || e->openCreateNew) return QString();
    return e->gapSliceLabel;
}

QString ComprehensiveEditorRegistry::editorTitle(SWMMModelLayer::DataCategory cat) const
{
    const Entry *e = find(cat);
    return e ? e->editorTitle : QString();
}

bool ComprehensiveEditorRegistry::openForObject(SWMMModelLayer::DataCategory cat,
                                                SWMMModelLayer *layer,
                                                QUndoStack     *undoStack,
                                                const QString  &name,
                                                QWidget        *parent) const
{
    const Entry *e = find(cat);
    if (!e || !e->openForObject || !layer || name.isEmpty()) return false;
    e->openForObject(layer, undoStack, name, parent);
    return true;
}
