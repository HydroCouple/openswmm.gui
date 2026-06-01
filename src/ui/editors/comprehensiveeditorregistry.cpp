/*!
 * \file   comprehensiveeditorregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/editors/comprehensiveeditorregistry.h"

#include "curve/curveregistry.h"
#include "pattern/patternregistry.h"
#include "timeseries/timeseriesregistry.h"
#include "transect/transectregistry.h"
#include "ui/dialogs/curveeditordialog.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/dialogs/ruleseditordialog.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/dialogs/transecteditordialog.h"

#include <QCoreApplication>
#include <QPointer>
#include <QUndoStack>

namespace {

using openswmmvis::curve::CurveRegistry;
using openswmmvis::pattern::PatternRegistry;
using openswmmvis::timeseries::TimeseriesRegistry;
using openswmmvis::transect::TransectRegistry;
using openswmmvis::ui::CurveEditorDialog;
using openswmmvis::ui::PatternEditorDialog;
using openswmmvis::ui::RulesEditorDialog;
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

void openControlsCreateNew(SWMMModelLayer *layer, QUndoStack *stack, QWidget *parent)
{
    if (!layer) return;
    // Per-session singleton: once the dialog closes (WA_DeleteOnClose),
    // the QPointer auto-nullifies and the next Add-New click constructs
    // a fresh instance. Two simultaneous entry points (Object Browser
    // Add-New, AttributePanel browse) raise the same window
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

    // Gap categories (nine). Each tooltip mirrors the legacy
    // ObjectBrowserPanel::gapTooltipFor mapping (Slice BM.0-Add-New)
    // verbatim so existing test assertions don't drift.
    auto gap = [](const char *title, const char *tip) {
        return Entry{QCoreApplication::translate("ComprehensiveEditorRegistry", title),
                     QCoreApplication::translate("ComprehensiveEditorRegistry", tip),
                     nullptr};
    };

    reg.registerEditor(DC::DataTransects,
        Entry{QCoreApplication::translate("ComprehensiveEditorRegistry",
                                           "Transect Editor"),
              QString(), &openTransectsCreateNew});
    reg.registerEditor(DC::DataLIDControls,
        gap("LID Control Editor",
            "Editor coming in Slice BO Phase 6.5.x (LIDControlEditor)."));
    reg.registerEditor(DC::DataPollutants,
        gap("Pollutant Editor",
            "Editor coming in Slice BP Phase 6.6.1 (PollutantEditor)."));
    reg.registerEditor(DC::DataLandUses,
        gap("Land Use Editor",
            "Editor coming in Slice BP Phase 6.6.2 (LandUseEditor)."));
    reg.registerEditor(DC::DataAquifers,
        gap("Aquifer Editor",
            "Editor coming in Slice BP Phase 6.6.x (AquiferEditor)."));
    reg.registerEditor(DC::DataSnowpacks,
        gap("Snowpack Editor",
            "Editor coming in Slice BP Phase 6.6.x (SnowpackEditor)."));
    reg.registerEditor(DC::DataStreets,
        gap("Street Editor",
            "Editor coming in Slice BO Phase 6.5.x (StreetEditor; "
            "engine gap BO-STREET-01)."));
    reg.registerEditor(DC::DataInlets,
        gap("Inlet Editor",
            "Editor coming in Slice BO Phase 6.5.x (InletEditor; "
            "engine gap BO-INLET-01)."));
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
