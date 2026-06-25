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

void openHydrographsCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    // Singleton-raise: opening from two surfaces must reuse one dialog
    // instance ([[feedback_mvc_synchronized_uis]]).
    static QPointer<HydrographGroupEditor> editor;
    if (!editor) editor = new HydrographGroupEditor(layer, parent);
    editor->beginNewGroup();
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

void openLandUsesCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<LandUseRegistry *>(layer->ensureLandUseRegistry());
    if (!reg) return;
    auto *dlg = LandUseEditorDialog::createNew(reg, layer, parent);
    if (!dlg) return;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
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

void openInletsCreateNew(SWMMModelLayer *layer, QUndoStack * /*stack*/, QWidget *parent)
{
    if (!layer) return;
    auto *reg = qobject_cast<InletRegistry *>(layer->ensureInletRegistry());
    if (!reg) return;
    auto *dlg = InletEditorDialog::createNew(reg, layer, parent);
    if (!dlg) return;
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

void openControlsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    // Per-session singleton: once the dialog closes (WA_DeleteOnClose),
    // the QPointer auto-nullifies and the next Add-New click constructs
    // a fresh instance. Two simultaneous entry points (Object Browser
    // Add-New, PropertiesPanel browse) raise the same window
    // ([[feedback_mvc_synchronized_uis]]).
    static QPointer<RulesEditorDialog> editor;
    if (!editor) {
        editor = RulesEditorDialog::createNew(layer, stack, parent);
        if (editor) editor->setAttribute(Qt::WA_DeleteOnClose);
    } else {
        editor->invokeNew();
    }
    if (editor) {
        editor->show();
        editor->raise();
        editor->activateWindow();
    }
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
              QString(), &openTimeseriesCreateNew});

    reg.registerEditor(DC::DataHydrographs,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Hydrograph Group Editor"),
              QString(), &openHydrographsCreateNew});

    reg.registerEditor(DC::DataPatterns,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Pattern Editor"),
              QString(), &openPatternsCreateNew});

    reg.registerEditor(DC::DataCurves,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Curve Editor"),
              QString(), &openCurvesCreateNew});

    reg.registerEditor(DC::DataControls,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Rules Editor"),
              QString(), &openControlsCreateNew});

    // Every non-spatial data category now ships a comprehensive editor — no
    // gap placeholders remain. (gapTooltip()/gapSliceLabel stay in the API for
    // any future category registered without an editor.)
    reg.registerEditor(DC::DataTransects,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Transect Editor"),
              QString(), &openTransectsCreateNew});
    reg.registerEditor(DC::DataLIDControls,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "LID Control Editor"),
              QString(), &openLidControlsCreateNew});
    reg.registerEditor(DC::DataPollutants,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Pollutant Editor"),
              QString(), &openPollutantsCreateNew});
    reg.registerEditor(DC::DataLandUses,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Land Use Editor"),
              QString(), &openLandUsesCreateNew});
    reg.registerEditor(DC::DataAquifers,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Aquifer Editor"),
              QString(), &openAquifersCreateNew});
    reg.registerEditor(DC::DataSnowpacks,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Snowpack Editor"),
              QString(), &openSnowpacksCreateNew});
    reg.registerEditor(DC::DataStreets,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Street Editor"),
              QString(), &openStreetsCreateNew});
    reg.registerEditor(DC::DataInlets,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Inlet Editor"),
              QString(), &openInletsCreateNew});
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
