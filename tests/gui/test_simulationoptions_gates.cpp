/**
 * @file      test_simulationoptions_gates.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Structural contract for the Simulation Options dialog restructure
 *         (workplans/OPTIONS_DIALOG_TABBED_RESTRUCTURE_PLAN_2026-09-07.md §6).
 *
 * Landed at phase T0, against the CURRENT (untabbed) layout, so the properties
 * the restructure must preserve are pinned BEFORE any widget moves:
 *
 *   objectNameCensus  every objectName the three page-level tests look up
 *                     still resolves, and its parent chain reaches the page
 *                     stack. A page built but never registered fails here.
 *   reachability      every widget tagged with an `optionKey` is laid out, and
 *                     every key the dialog writes has a tagged editor behind
 *                     it. A control orphaned by a page move fails here.
 *   structure         the sidebar rows, in order. Tab titles join this test as
 *                     each page gains its QTabWidget.
 *
 * Widgets are reached only through findChild(objectName) and the public
 * lastWriteKeys() accessor — the dialog has no friend classes and gains none.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/swmmmodellayer.h"
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/simoptions/datespage.h"
#include "ui/dialogs/simoptions/filespage.h"
#include "ui/dialogs/hotstartsavesmodel.h"
#include "ui/widgets/relativepathpicker.h"
#include <openswmm/engine/openswmm_hotstart.h>
#include "ui/dialogs/simoptions/modelspage.h"
#include "ui/dialogs/simoptions/qualitypage.h"
#include "ui/dialogs/simoptions/ardtransportmodels.h"
#include "ui/dialogs/processcomponentsmodel.h"
#include <openswmm/engine/openswmm_process_components.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_transport.h>
#include <QAbstractItemModelTester>
#include <QDir>
#include <QLabel>
#include "ui/dialogs/climatologydialog.h"
#include "ui/dialogs/mesh2dgroundwaterdialog.h"
#include "ui/configurationsession.h"
#include <openswmm/engine/openswmm_climate.h>
#include <openswmm/engine/openswmm_heat.h>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QTimer>
#include <QUndoStack>

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLocale>
#include <QScopeGuard>
#include <QTimeZone>
#include <ctime>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <openswmm/engine/openswmm_model.h>
#include <QCheckBox>
#include <QCoreApplication>
#include <QListWidget>
#include <QObject>
#include <QScrollArea>
#include "ui/dialogs/simoptions/enginecapabilities.h"
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QTest>
#include <QWidget>

#include <memory>
#include <cmath>

namespace
{

/** @brief Read the GUI fixture/output root.
 * @return Owned SWMMVIS_GUI_TEST_DATA value or dot fallback; no I/O. */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/** @brief Join a controlled fixture/output name beneath the GUI data root.
 * @param[in] name Relative filename copied without sanitization.
 * @return Owned joined path; no existence check, parent creation or reservation. */
QString fixture(const QString &name)
{
    return QDir(dataDir()).filePath(name);
}

/** @brief Load a model layer with exclusive RAII ownership.
 * @param[in] path Existing deck filename copied into the layer.
 * @return Owning loaded layer, or null after load refusal/cleanup.
 * Discards load diagnostics and starts no simulation. */
std::unique_ptr<SWMMModelLayer> openLayer(const QString &path)
{
    auto layer = std::make_unique<SWMMModelLayer>(path, nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

/*! Walk up from \a w; true when \a ancestor is on the chain. */
/** @brief Test whether a widget or one of its parents equals the requested ancestor.
 * @param[in] w Borrowed live start widget, or null for an empty chain.
 * @param[in] ancestor Borrowed ancestor pointer to compare, without dereferencing it.
 * @return True for the first identity match including w itself; false otherwise.
 * Requires parent chain stability, transfers no ownership and mutates no widget. */
bool hasAncestor(const QWidget *w, const QWidget *ancestor)
{
    for (const QWidget *p = w; p; p = p->parentWidget())
        if (p == ancestor)
            return true;
    return false;
}

/*! The object names the 2D-output, processes-hub and groundwater page tests
 *  look up. Every one must survive the page split verbatim: they are the
 *  contract those three tests hold the dialog to. */
const char *const kLoadBearingNames[] = {
    // Models / Processes — transport matrix hub + the module toggle the 2D
    // sidebar row gates on, and the read-only FLOW_ROUTING mirror
    "transportMatrixTable",
    "module2DBox",
    "routingMirrorLabel",
    // page headers that gate their own tabs (PLAN §2.1)
    "flowRoutingCombo",
    "qualitySolverCombo",
    // 2D › Processes
    "infil2DModeCombo",
    "infil2DStepSameBox",
    "infil2DStepEdit",
    "infil2DMethodCombo",
    "infil2DDestCombo",
    "editInfilCellsBtn",
    "evap2DCombo",
    // 2D › Groundwater
    "gw2DEnableCombo",
    "gw2DEtCombo",
    "gw2DStatusLabel",
    "gw2DEditBtn",
    "gwLinkSeepageCombo",
    "gwWiltingAuto",
    "gwWiltingSuction",
    "gwEtAllocationLabel",
    // 2D › Output
    "output2DPrecisionCombo",
    "output2DCompressionSpin",
    "output2DSizeLabel",
    "report2DStepSameBox",
    "report2DStepEdit",
    "report2DVarsList",
    "report2DAllSpeciesBox",
    "report2DSpeciesList",
    "report2DPreset_DEFAULT",
    "report2DPreset_MINIMAL",
    "report2DPreset_ALL",
    // Files / Output / Plugins — process components
    "processComponentsView",
    // dialog chrome the gate tests navigate by
    "categories",
    "pages",
};

} // namespace

class TestSimulationOptionsGates : public QObject
{
    Q_OBJECT

private slots:
    void objectNameCensus();
    void emptyProjectCanReachHeatActivation();
    void reachability();
    void twoDRowGate();
    void rowRedirect();
    void hydraulicsTabGates();
    void qualityTabGates();
    void tabRedirect();
    void noScrollAt1280x800();
    void structure();
    void aquiferProcessInputsPreserveAutomaticAndConvertUnits();
    void automaticEtFollowsAquiferMode();
    void reportStartFollowsEditedSimulationWindow();
    void reportStartReloadsEarlierSimulationWindow();
    void nestedCancelRevertsAcceptedEdits();
    void applyIsOneUndoEntryForNestedAndPageEdits();
    void modelCloseDuringNestedEditorInvalidatesWholeSession();
    void climateUndoIsExactAndRefusesExternalEdits();
    void groundwaterThermalConfigurationUndoTracksTransportKeys();
    void groundwaterThermalShortcutJoinsOuterCancelUndoSegment();
    void heatConfigurationSharesOuterUndoAndCancelSegment();
    void runtimeOverviewNavigatesToTwoDSettings();
    void hotstartSaveFormatsSurviveFilesPage();
    void durationAcrossDaylightSavingIsWallClock();
    void routingStepTypedUnderDecimalCommaLocaleIsWritten();
    void reviewG05G06OptionsHaveEditorsAndApply();
    /** @brief Review G-02: the ARD transport table models offer the model's nodes, conduits
     *  and reaction species, refuse each bad row by name, and write the rows they validate. */
    void ardTransportRowsAreCheckedBeforeApply();
    /** @brief Review G-02: the Quality page writes nothing on an unchanged Apply; an edited
     *  Apply writes the dispersion settings and tables, registers <model>.ard even when the
     *  Files page edits its component table in the same pass, undoes and redoes as one step,
     *  and saves the file a reopened model reads back. */
    void ardTransportPageAppliesUndoesAndSaves();
};

namespace
{

/*! Sidebar row index whose text is \a title, or -1. */
/** @brief Find the first sidebar item with an exact title.
 * @param[in] cats Non-null live list with valid item pointers, unchanged.
 * @param[in] title Case-sensitive text to match.
 * @return Zero-based first matching row or -1 when absent. */
int rowOf(const QListWidget *cats, const QString &title)
{
    for (int i = 0; i < cats->count(); ++i)
        if (cats->item(i)->text() == title)
            return i;
    return -1;
}

/*! Enabled state of a sidebar row (rows are grayed, never hidden — PLAN §4.3). */
/** @brief Read a sidebar item's enabled flag.
 * @param[in] cats Non-null live list, unchanged.
 * @param[in] row Valid index whose item exists; unchecked before dereference.
 * @return True when the item has Qt::ItemIsEnabled; visibility is not tested. */
bool rowEnabled(const QListWidget *cats, int row)
{
    return cats->item(row)->flags().testFlag(Qt::ItemIsEnabled);
}

/*! Select FLOW_ROUTING by its engine token. */
/** @brief Select a routing combo item by canonical engine token.
 * @param[in,out] dlg Live options dialog whose pending widget state may change.
 * @param[in] token Non-null Latin-1 item-data token expected to be present.
 * Qt fatal assertions return from this helper on absent combo/item. Changing
 * selection invokes normal page-gating signals, without applying to the engine. */
void setRouting(SimulationOptionsDialog &dlg, const char *token)
{
    auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("flowRoutingCombo"));
    QVERIFY(combo);
    const int idx = combo->findData(QLatin1String(token));
    QVERIFY(idx >= 0);
    combo->setCurrentIndex(idx);
}

} // namespace

/** @brief Require report-start bounds to follow both edited simulation endpoints, including earlier dates and second precision.
 * Tests temporary inverted-window editing followed by a valid later window;
 * changes page widgets without applying or running a model. */
void TestSimulationOptionsGates::reportStartFollowsEditedSimulationWindow()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    openswmmvis::ui::SimOptionsContext ctx(layer->engine(), nullptr, nullptr, {});
    openswmmvis::ui::DatesPage page(ctx);
    page.read();
    auto editor = [&page](const char *key) -> QDateTimeEdit * {
        for (auto *edit : page.findChildren<QDateTimeEdit *>())
            if (edit->property("optionKey").toString().split(',').contains(QLatin1String(key)))
                return edit;
        return nullptr;
    };
    auto *start = editor("START_DATE");
    auto *end = editor("END_DATE");
    auto *report = editor("REPORT_START_DATE");
    QVERIFY(start && end && report);

    const QDateTime original(QDate(2026, 10, 14), QTime(11, 0));
    end->setDateTime(original.addSecs(7200));
    start->setDateTime(original);
    report->setDateTime(original.addSecs(1800));

    // Moving the window earlier must allow a report time before the old start.
    const QDateTime earlier = original.addDays(-1);
    start->setDateTime(earlier);
    report->setDateTime(earlier.addSecs(1800));
    QCOMPARE(report->dateTime(), earlier.addSecs(1800));

    // Both ends of the current window constrain reporting, including seconds.
    end->setDateTime(earlier.addSecs(1207));
    QCOMPARE(report->dateTime(), end->dateTime());
    report->setDateTime(earlier.addSecs(1800));
    QCOMPARE(report->dateTime(), end->dateTime());
    end->setDateTime(original.addDays(1));
    report->setDateTime(original.addSecs(3600));
    QCOMPARE(report->dateTime(), original.addSecs(3600));
    start->setDateTime(original.addSecs(5400));
    QCOMPARE(report->dateTime(), start->dateTime());

    // Users can temporarily invert the window while editing either endpoint.
    start->setDateTime(original.addDays(2));
    end->setDateTime(original.addDays(3));
    report->setDateTime(original.addDays(2).addSecs(600));
    QCOMPARE(report->dateTime(), original.addDays(2).addSecs(600));
}

/** @brief Require DatesPage reread to discard stale report-time bounds for earlier and later engine windows.
 * Writes page values back and checks exact report date/seconds rather than
 * reusing an old minimum/maximum. */
void TestSimulationOptionsGates::reportStartReloadsEarlierSimulationWindow()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    auto e = layer->engine();
    auto setWindow = [e](const char *date, const char *start, const char *end, const char *report) {
        return swmm_options_set(e, "START_DATE", date) == SWMM_OK &&
               swmm_options_set(e, "START_TIME", start) == SWMM_OK &&
               swmm_options_set(e, "END_DATE", date) == SWMM_OK &&
               swmm_options_set(e, "END_TIME", end) == SWMM_OK &&
               swmm_options_set(e, "REPORT_START_DATE", date) == SWMM_OK &&
               swmm_options_set(e, "REPORT_START_TIME", report) == SWMM_OK;
    };
    QVERIFY(setWindow("10/14/2026", "11:00:00", "13:00:00", "11:30:00"));
    openswmmvis::ui::SimOptionsContext ctx(e, nullptr, nullptr, {});
    openswmmvis::ui::DatesPage page(ctx);
    page.read();
    QDateTimeEdit *report = nullptr;
    for (auto *edit : page.findChildren<QDateTimeEdit *>())
        if (edit->property("optionKey").toString().split(',').contains("REPORT_START_DATE"))
            report = edit;
    QVERIFY(report);

    // read() also runs after Apply: clear the old bounds before loading values.
    QVERIFY(setWindow("10/13/2026", "09:00:00", "10:00:00", "09:30:07"));
    page.read();
    // SWMM dates are UTC-labeled wall clock (owner rule 2026-11-12, review E-14).
    QCOMPARE(report->dateTime(), QDateTime(QDate(2026, 10, 13), QTime(9, 30, 7), QTimeZone::UTC));
    page.write();
    QCOMPARE(ctx.option("REPORT_START_DATE"), QStringLiteral("10/13/2026"));
    QCOMPARE(ctx.option("REPORT_START_TIME"), QStringLiteral("09:30:07"));

    // Expanding the window later must also release its previous upper bound.
    QVERIFY(setWindow("10/15/2026", "14:00:00", "17:00:00", "16:00:03"));
    page.read();
    QCOMPARE(report->dateTime(), QDateTime(QDate(2026, 10, 15), QTime(16, 0, 3), QTimeZone::UTC));
    page.write();
    QCOMPARE(ctx.option("REPORT_START_TIME"), QStringLiteral("16:00:03"));
}

/*! T0 — the names exist and are parented into the page stack. */
/** @brief Require the Quality and Transport category to remain enabled for an empty editable engine.
 * Constructs/destroys a real options dialog without topology or a simulation. */
void TestSimulationOptionsGates::emptyProjectCanReachHeatActivation()
{
    auto engine = swmm_engine_new();
    QVERIFY(engine);
    {
        SimulationOptionsDialog dialog(engine, nullptr, "6.0.0", nullptr, nullptr);
        auto *categories = dialog.findChild<QListWidget *>("categories");
        QVERIFY(categories);
        bool found = false;
        for (int i = 0; i < categories->count(); ++i)
            if (categories->item(i)->text() == "Quality & Transport") {
                found = true;
                QVERIFY(categories->item(i)->flags() & Qt::ItemIsEnabled);
            }
        QVERIFY(found);
    }
    swmm_engine_destroy(engine);
}

/** @brief Require every declared page-test widget objectName to resolve and nonchrome widgets to belong to the page stack.
 * Detects missing or built-but-unparented controls; no engine Apply is invoked. */
void TestSimulationOptionsGates::objectNameCensus()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SWMM_Engine e = layer->engine();
    QVERIFY(e != nullptr);

    SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);

    auto *pages = dlg.findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY2(pages, "the page stack lost its objectName \"pages\"");

    QStringList missing, unparented;
    for (const char *name : kLoadBearingNames) {
        auto *w = dlg.findChild<QWidget *>(QLatin1String(name));
        if (!w) {
            missing << QLatin1String(name);
            continue;
        }
        // The chrome itself is not inside the stack; everything else must be.
        if (qstrcmp(name, "categories") == 0 || qstrcmp(name, "pages") == 0)
            continue;
        if (!hasAncestor(w, pages))
            unparented << QLatin1String(name);
    }
    QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("objectName(s) no longer resolve: %1")
                                               .arg(missing.join(QStringLiteral(", ")))));
    QVERIFY2(unparented.isEmpty(),
             qPrintable(QStringLiteral("built but not laid out under the page stack: %1")
                            .arg(unparented.join(QStringLiteral(", ")))));
}

/*! T0 — every written key has a tagged editor, and every tagged editor is
 *  reachable from the page stack. */
/** @brief Require all tagged option editors to belong to the page stack and every Apply-recorded key to have a tag.
 * Splits comma-paired date/time keys, invokes real Apply and checks the recorded
 * key set is nonempty; does not require that each key changed its value. */
void TestSimulationOptionsGates::reachability()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SWMM_Engine e = layer->engine();
    QVERIFY(e != nullptr);

    SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    auto *pages = dlg.findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY(pages);

    // (i) every tagged widget is laid out under the stack
    // `optionKey` is a comma-separated list: a QDateTimeEdit owns both the
    // DATE and the TIME half of its key pair.
    QSet<QString> tagged;
    QStringList orphans;
    for (QWidget *w : dlg.findChildren<QWidget *>()) {
        const QVariant key = w->property("optionKey");
        if (!key.isValid())
            continue;
        const QStringList keys = key.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &k : keys)
            tagged.insert(k);
        if (!hasAncestor(w, pages))
            orphans << keys.join(QLatin1Char('+'));
    }
    QVERIFY2(!tagged.isEmpty(), "no widget carries an optionKey property — the seam is missing");
    QVERIFY2(orphans.isEmpty(),
             qPrintable(QStringLiteral("option editors built but never laid out: %1")
                            .arg(orphans.join(QStringLiteral(", ")))));

    // (ii) every key the dialog writes has a tagged editor
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
    const QStringList written = dlg.lastWriteKeys();
    QVERIFY2(!written.isEmpty(), "Apply recorded no keys — lastWriteKeys() is not wired");

    QStringList untagged;
    for (const QString &k : written)
        if (!tagged.contains(k) && !untagged.contains(k))
            untagged << k;
    QVERIFY2(untagged.isEmpty(), qPrintable(QStringLiteral("keys written with no tagged editor: %1")
                                                .arg(untagged.join(QStringLiteral(", ")))));
}

/*! Test 4a — the 2D row follows the module toggle, grayed with a reason. */
/** @brief Require the 2D category to gray out with an explanatory tooltip when its module toggle is off, then reenable without tooltip when on. */
void TestSimulationOptionsGates::twoDRowGate()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr,
                                nullptr);
    auto *cats = dlg.findChild<QListWidget *>(QStringLiteral("categories"));
    QVERIFY(cats);
    const int row = rowOf(cats, QStringLiteral("2D Surface Routing"));
    QVERIFY2(row >= 0, "no 2D Surface Routing row");

    auto *module2D = dlg.findChild<QCheckBox *>(QStringLiteral("module2DBox"));
    QVERIFY2(module2D, "the 2D module checkbox lost its objectName");

    module2D->setChecked(false);
    QVERIFY2(!rowEnabled(cats, row), "2D row stayed enabled with the module off");
    QVERIFY2(!cats->item(row)->toolTip().isEmpty(), "a gated row must say WHY it is off");
    module2D->setChecked(true);
    QVERIFY2(rowEnabled(cats, row), "2D row did not come back with the module on");
    QVERIFY(cats->item(row)->toolTip().isEmpty());
}

/*! Test 5 — turning off the page you are viewing must land you somewhere usable. */
/** @brief Require disabling the currently selected 2D category to redirect to a different enabled sidebar row. */
void TestSimulationOptionsGates::rowRedirect()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr,
                                nullptr);
    auto *cats = dlg.findChild<QListWidget *>(QStringLiteral("categories"));
    QVERIFY(cats);
    const int row = rowOf(cats, QStringLiteral("2D Surface Routing"));
    QVERIFY(row >= 0);
    auto *module2D = dlg.findChild<QCheckBox *>(QStringLiteral("module2DBox"));
    QVERIFY(module2D);
    module2D->setChecked(true);
    cats->setCurrentRow(row);
    QCOMPARE(cats->currentRow(), row);

    module2D->setChecked(false);
    QVERIFY2(cats->currentRow() != row, "left the user on a grayed page");
    QVERIFY2(rowEnabled(cats, cats->currentRow()), "redirected onto another disabled row");
}

/*! Test 4b — the Hydraulics tabs follow FLOW_ROUTING (the headline of the
 *  restructure: contextual activation instead of one 35-control column). */
/** @brief Require routing-dependent hydraulics tab availability and an enabled current tab across STEADY/KINWAVE/DYNWAVE/FV.
 * Accounts for optional native implicit tab, requires Routing always enabled,
 * DW only for DYNWAVE and FV/unsteady tabs disabled for incompatible modes.
 * It does not force capability-dependent FV/unsteady availability when compatible. */
void TestSimulationOptionsGates::hydraulicsTabGates()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr,
                                nullptr);
    auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("hydraulicsTabs"));
    QVERIFY2(tabs, "Routing & Hydraulics has no hydraulicsTabs");
    const bool native =
        openswmmvis::ui::probeEngineCapabilities(layer->engine(), QStringLiteral("6.0.0"))
            .implicitFv;
    QCOMPARE(tabs->count(), native ? 5 : 4);

    // Routing is every solver's tab and never gates off; Dynamic Wave is
    // DYNWAVE-only; Finite Volume is FV-only. Unsteady friction rides on an
    // engine capability probe, so it is asserted as "off under STEADY /
    // KINWAVE" rather than against a hardcoded expectation.
    struct Case {
        const char *routing;
        bool dw;
        bool fv;
        bool ufPossible;
    };
    const Case cases[] = {
        {"STEADY", false, false, false},
        {"KINWAVE", false, false, false},
        {"DYNWAVE", true, false, true},
        {"FV", false, true, true},
    };
    for (const Case &c : cases) {
        setRouting(dlg, c.routing);
        QVERIFY2(
            tabs->isTabEnabled(0),
            qPrintable(QStringLiteral("Routing tab off under %1").arg(QLatin1String(c.routing))));
        QVERIFY2(
            tabs->isTabEnabled(1) == c.dw,
            qPrintable(
                QStringLiteral("Dynamic Wave tab wrong under %1").arg(QLatin1String(c.routing))));
        // FV also needs the engine capability; a build without it keeps the
        // tab off, which is still correct.
        if (!c.fv)
            QVERIFY2(
                !tabs->isTabEnabled(2),
                qPrintable(
                    QStringLiteral("Finite Volume tab on under %1").arg(QLatin1String(c.routing))));
        if (!c.ufPossible)
            QVERIFY2(!tabs->isTabEnabled(3),
                     qPrintable(QStringLiteral("Unsteady Friction tab on under %1")
                                    .arg(QLatin1String(c.routing))));
        // Whatever is showing must be reachable.
        QVERIFY(tabs->isTabEnabled(tabs->currentIndex()));
    }
}

/*! Test 4b — the Quality tabs follow QUALITY_SOLVER. */
/** @brief Require the four quality tabs to follow LEGACY/ARD/LAGRANGIAN selection while General/Reserved Species remain available.
 * Skips when this engine lacks the quality option surface; selected tab must
 * remain enabled after each change. */
void TestSimulationOptionsGates::qualityTabGates()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr,
                                nullptr);
    auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("qualityTabs"));
    QVERIFY2(tabs, "Quality & Transport has no qualityTabs");
    QCOMPARE(tabs->count(), 4);
    auto *solver = dlg.findChild<QComboBox *>(QStringLiteral("qualitySolverCombo"));
    QVERIFY2(solver, "the QUALITY_SOLVER header combo lost its objectName");
    if (!solver->isEnabled())
        QSKIP("this engine build has no quality/transport option surface");

    struct Case {
        const char *token;
        bool ard;
        bool lard;
    };
    const Case cases[] = {
        {"LEGACY", false, false},
        {"EULERIAN_ARD", true, false},
        {"LAGRANGIAN", false, true},
    };
    for (const Case &c : cases) {
        const int idx = solver->findData(QLatin1String(c.token));
        QVERIFY(idx >= 0);
        solver->setCurrentIndex(idx);
        QVERIFY2(tabs->isTabEnabled(0), "General tab must never gate off");
        QVERIFY2(
            tabs->isTabEnabled(1) == c.ard,
            qPrintable(
                QStringLiteral("Eulerian ARD tab wrong under %1").arg(QLatin1String(c.token))));
        QVERIFY2(tabs->isTabEnabled(2) == c.lard,
                 qPrintable(
                     QStringLiteral("Lagrangian tab wrong under %1").arg(QLatin1String(c.token))));
        QVERIFY2(tabs->isTabEnabled(3), "Reserved Species tab must never gate off");
        QVERIFY(tabs->isTabEnabled(tabs->currentIndex()));
    }
}

/*! Test 5 — switching FLOW_ROUTING while viewing the tab that just went away. */
/** @brief Require switching from DYNWAVE to STEADY while viewing Dynamic Wave to redirect to an enabled hydraulics tab. */
void TestSimulationOptionsGates::tabRedirect()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr,
                                nullptr);
    auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("hydraulicsTabs"));
    QVERIFY(tabs);

    setRouting(dlg, "DYNWAVE");
    QVERIFY(tabs->isTabEnabled(1));
    tabs->setCurrentIndex(1); // Dynamic Wave
    QCOMPARE(tabs->currentIndex(), 1);

    setRouting(dlg, "STEADY"); // Dynamic Wave goes away
    QVERIFY2(tabs->currentIndex() != 1, "left the user on a grayed tab");
    QVERIFY2(tabs->isTabEnabled(tabs->currentIndex()), "redirected onto another disabled tab");
}

/*! Test 7 — no page scrolls at 1280x800, on any of its tabs. The whole point
 *  of the restructure: at most ~13 controls per tab instead of one long
 *  column. Mesh and Files legitimately scroll their tables, not their pages. */
/** @brief Require non-table-dominated option pages to avoid vertical page scrolling at a 1280-by-800 dialog size.
 * Shows each first nested tab with event/layout waits, exempting Mesh and Files
 * category tables; does not inspect every control's internal scrolling. */
void TestSimulationOptionsGates::noScrollAt1280x800()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr,
                                nullptr);
    auto *cats = dlg.findChild<QListWidget *>(QStringLiteral("categories"));
    auto *pages = dlg.findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY(cats && pages);

    dlg.resize(1280, 800);
    dlg.show();
    QTest::qWait(50);

    const QStringList tableDominated{
        QStringLiteral("Mesh"),
        QStringLiteral("Files / Output / Plugins"),
    };
    QStringList overflows;
    for (int r = 0; r < cats->count(); ++r) {
        const QString title = cats->item(r)->text();
        if (tableDominated.contains(title))
            continue;
        cats->setCurrentRow(r);
        auto *sa = qobject_cast<QScrollArea *>(pages->widget(r));
        if (!sa)
            continue;
        QWidget *page = sa->widget();
        const QList<QTabWidget *> inner =
            page ? page->findChildren<QTabWidget *>() : QList<QTabWidget *>{};
        const int nTabs = inner.isEmpty() ? 1 : inner.first()->count();
        for (int t = 0; t < nTabs; ++t) {
            if (!inner.isEmpty())
                inner.first()->setCurrentIndex(t);
            QCoreApplication::processEvents();
            QTest::qWait(10);
            if (sa->verticalScrollBar()->maximum() > 0) {
                overflows
                    << (inner.isEmpty()
                            ? title
                            : QStringLiteral("%1 \u203A %2").arg(title, inner.first()->tabText(t)));
            }
        }
    }
    QVERIFY2(overflows.isEmpty(), qPrintable(QStringLiteral("page(s) still scroll at 1280x800: %1")
                                                 .arg(overflows.join(QStringLiteral(", ")))));
}

/*! Test 8 — sidebar rows in order, and every inner tab set, in order. */
/** @brief Require exact ordered sidebar labels and all specified inner tab sets, adding Implicit FV only when capability-probed.
 * Normalizes escaped ampersands to visible captions before comparing. */
void TestSimulationOptionsGates::structure()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SWMM_Engine e = layer->engine();
    QVERIFY(e != nullptr);

    SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    auto *cats = dlg.findChild<QListWidget *>(QStringLiteral("categories"));
    QVERIFY2(cats, "the sidebar lost its objectName \"categories\"");

    const QStringList expected{
        QStringLiteral("Title / Notes"),       QStringLiteral("Models / Processes"),
        QStringLiteral("Dates & Times"),       QStringLiteral("Routing & Hydraulics"),
        QStringLiteral("Quality & Transport"), QStringLiteral("System / Performance"),
        QStringLiteral("Spatial & CRS"),       QStringLiteral("Mesh"),
        QStringLiteral("2D Surface Routing"),  QStringLiteral("Files / Output / Plugins"),
    };
    QStringList actual;
    for (int i = 0; i < cats->count(); ++i)
        actual << cats->item(i)->text();
    QCOMPARE(actual, expected);

    // Every inner tab set, by objectName, against PLAN §2.
    const struct {
        const char *tabs;
        QStringList titles;
    } kTabSets[] = {
        {"modelsTabs",
         {QStringLiteral("Domains & Processes"), QStringLiteral("Modules"),
          QStringLiteral("Flags")}},
        {"datesTabs",
         {QStringLiteral("Simulation Window"), QStringLiteral("Time Steps"),
          QStringLiteral("Events")}},
        {"hydraulicsTabs",
         {QStringLiteral("Routing"), QStringLiteral("Dynamic Wave"),
          QStringLiteral("Finite Volume"), QStringLiteral("Unsteady Friction")}},
        {"qualityTabs",
         {QStringLiteral("General"), QStringLiteral("Eulerian ARD"),
          QStringLiteral("Lagrangian (LARD)"), QStringLiteral("Reserved Species")}},
        // Five, not PLAN §2's four: §1.3 reserved a Processes tab after
        // Coupling and that content (U1/U5) now exists.
        {"twoDTabs",
         {QStringLiteral("Hydrodynamics"), QStringLiteral("Wetting & Drying"),
          QStringLiteral("Coupling"), QStringLiteral("Processes"),
          QStringLiteral("Performance")}},
    };
    for (const auto &set : kTabSets) {
        auto *tabs = dlg.findChild<QTabWidget *>(QLatin1String(set.tabs));
        QVERIFY2(tabs,
                 qPrintable(QStringLiteral("no QTabWidget named %1").arg(QLatin1String(set.tabs))));
        QStringList got;
        // QTabWidget returns the source label: && paints one literal &.
        // Compare the visible captions used by the expected list below.
        for (int i = 0; i < tabs->count(); ++i)
            got << tabs->tabText(i).replace(QStringLiteral("&&"), QStringLiteral("&"));
        QStringList expectedTitles = set.titles;
        if (qstrcmp(set.tabs, "hydraulicsTabs") == 0 &&
            openswmmvis::ui::probeEngineCapabilities(e, QStringLiteral("6.0.0")).implicitFv)
            expectedTitles << QStringLiteral("Implicit FV");
        QVERIFY2(got == expectedTitles,
                 qPrintable(QStringLiteral("%1: %2 != %3")
                                .arg(QLatin1String(set.tabs), got.join(QStringLiteral(" | ")),
                                     expectedTitles.join(QStringLiteral(" | ")))));
    }
}

QTEST_MAIN(TestSimulationOptionsGates)
#include "test_simulationoptions_gates.moc"

/** @brief Require CMS/CFS groundwater process controls to preserve AUTO provenance on no-edit Apply and convert explicit suction to the same 30 m physical value.
 * Checks displayed units/default 150 m equivalent, BOTH/TWO_WAY edits and
 * unchanged custom suction on reopen; performs no groundwater timestep. */
void TestSimulationOptionsGates::aquiferProcessInputsPreserveAutomaticAndConvertUnits()
{
    for (const char *units : {"CMS", "CFS"}) {
        auto layer = openLayer(fixture("mini_2d.inp"));
        QVERIFY(layer);
        auto e = layer->engine();
        QVERIFY(swmm_options_set(e, "FLOW_UNITS", units) == SWMM_OK);
        QVERIFY(swmm_gw2d_process_options_set(e, "AUTO", "DEFAULT", "AUTO", 0) == SWMM_OK);
        auto option = [&](const char *key) {
            char b[80]{};
            swmm_gw2d_option_get(e, key, b, sizeof b);
            return QString::fromUtf8(b);
        };
        SimulationOptionsDialog dlg(e, layer.get(), "6.0.0", nullptr, nullptr);
        auto *et = dlg.findChild<QComboBox *>("gw2DEtCombo");
        auto *link = dlg.findChild<QComboBox *>("gwLinkSeepageCombo");
        auto *automatic = dlg.findChild<QCheckBox *>("gwWiltingAuto");
        auto *value = dlg.findChild<QDoubleSpinBox *>("gwWiltingSuction");
        QVERIFY(et && link && automatic && value);
        QCOMPARE(et->currentData().toString(), QString("AUTO"));
        QCOMPARE(link->currentData().toString(), QString("DEFAULT"));
        QVERIFY(automatic->isChecked());
        const bool us = QString(units) == "CFS";
        QVERIFY(std::abs(value->value() - (us ? 150 / .3048 : 150)) < 1e-8);
        QCOMPARE(value->suffix(), us ? QString(" ft") : QString(" m"));
        const auto authored = option("OPTIONS_AUTHORED");
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QCOMPARE(option("WILTING_SUCTION"), QString("AUTO"));
        QCOMPARE(option("OPTIONS_AUTHORED"), authored);
        et->setCurrentIndex(et->findData("BOTH"));
        link->setCurrentIndex(link->findData("TWO_WAY"));
        automatic->setChecked(false);
        value->setValue(us ? 30 / .3048 : 30);
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(std::abs(option("WILTING_SUCTION").toDouble() * (us ? .3048 : 1) - 30) < 1e-8);
        QCOMPARE(option("GW_ET"), QString("BOTH"));
        QCOMPARE(option("LINK_SEEPAGE"), QString("TWO_WAY"));
        const auto custom = option("WILTING_SUCTION");
        SimulationOptionsDialog reopened(e, layer.get(), "6.0.0", nullptr, nullptr);
        QVERIFY(QMetaObject::invokeMethod(&reopened, "onApply", Qt::DirectConnection));
        QCOMPARE(option("WILTING_SUCTION"), custom);
    }
}

/** @brief Require automatic wilting controls enabled only for active MESH aquifers and disabled when GW is off, retaining authored AUTO ET.
 * Checks MESH and PER_SUBCATCH configurations without running either. */
void TestSimulationOptionsGates::automaticEtFollowsAquiferMode()
{
    for (const char *mode : {"MESH", "PER_SUBCATCH"}) {
        auto layer = openLayer(fixture("mini_2d.inp"));
        QVERIFY(layer);
        auto e = layer->engine();
        QCOMPARE(swmm_gw2d_row_add(e, 0, nullptr, -1, 1., 5., .45, .1, 2.), SWMM_OK);
        QCOMPARE(swmm_gw2d_option_set(e, "MODE", mode), SWMM_OK);
        QCOMPARE(swmm_gw2d_process_options_set(e, "AUTO", "DEFAULT", "AUTO", 0), SWMM_OK);
        SimulationOptionsDialog dialog(e, layer.get(), "6.0.0", nullptr, nullptr);
        auto *enabled = dialog.findChild<QComboBox *>("gw2DEnableCombo");
        QVERIFY(enabled);
        enabled->setCurrentIndex(enabled->findData("YES"));
        auto *automatic = dialog.findChild<QCheckBox *>("gwWiltingAuto");
        QVERIFY(automatic);
        QCOMPARE(automatic->isEnabled(), QString(mode) == "MESH");
        enabled->setCurrentIndex(enabled->findData("NO"));
        QVERIFY(!automatic->isEnabled());
        char et[80]{};
        QCOMPARE(swmm_gw2d_option_get(e, "GW_ET", et, sizeof et), SWMM_OK);
        QCOMPARE(QString(et), QString("AUTO"));
    }
}

namespace
{

/// Edits the next modal Climatology dialog with \a edit and accepts it.
/** @brief Arrange asynchronous editing/acceptance of the next modal ClimatologyDialog.
 * @param[in] edit Nonempty callable copied into a repeating timer callback; captured
 * references and the expected modal controls must remain alive until invoked.
 * Polls every 10 ms without a timeout. On a match stops/deletes the timer, edits
 * the borrowed dialog and clicks its expected OK button. If no dialog appears
 * the unparented timer remains active; no ownership of the dialog is taken. */
void editNextClimatology(std::function<void(ClimatologyDialog &)> edit)
{
    auto *timer = new QTimer;
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, [timer, edit] {
        auto *dialog = qobject_cast<ClimatologyDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        timer->stop();
        timer->deleteLater();
        edit(*dialog);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    timer->start();
}

/** @brief Drive the next heat-to-groundwater modal shortcut to porous-equilibrium thermal settings.
 * Uses an unparented 10 ms timer and owned heap phase/attempt counters, first
 * clicking the groundwater-settings shortcut and then selecting the thermal
 * combo/OK. After over 300 polls records qFail, rejects an active modal and cleans
 * its timer/counters. Caller must pump events and expose expected control names;
 * no completion result is returned. */
void editNextGroundwaterThermalSettings()
{
    auto *timer = new QTimer;
    auto *phase = new int(0);
    auto *attempts = new int(0);
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, [timer, phase, attempts] {
        if (++*attempts > 300) {
            QTest::qFail("Timed out waiting for the groundwater thermal settings dialog", __FILE__,
                         __LINE__);
            if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                modal->reject();
            timer->stop();
            timer->deleteLater();
            delete phase;
            delete attempts;
            return;
        }
        auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!modal)
            return;
        if (*phase == 0) {
            auto *open = modal->findChild<QPushButton *>(QStringLiteral("hc_groundwaterSettings"));
            if (!open)
                return;
            *phase = 1;
            open->click();
            return;
        }
        auto *thermal = modal->findChild<QComboBox *>(QStringLiteral("gwTransport_THERMAL_MODEL"));
        if (!thermal)
            return;
        timer->stop();
        timer->deleteLater();
        const int porous = thermal->findData(QStringLiteral("POROUS_EQUILIBRIUM"));
        if (porous >= 0)
            thermal->setCurrentIndex(porous);
        if (auto *buttons = modal->findChild<QDialogButtonBox *>())
            if (auto *accept = buttons->button(QDialogButtonBox::Ok))
                accept->click();
        delete phase;
        delete attempts;
    });
    timer->start();
}

/** @brief Arrange changing the next modal heat configuration and accepting it.
 * @param[in] enabled Heat checkbox state copied into the asynchronous callback.
 * Polls every 10 ms up to over 300 attempts, matching dialog/control names. On
 * match applies the state and clicks OK, then cleans owned timer/counter; timeout
 * records qFail and rejects an active modal. Caller pumps events and keeps the
 * expected dialog interaction alive; no engine handle is retained directly. */
void editNextHeatConfiguration(bool enabled)
{
    auto *timer = new QTimer;
    auto *attempts = new int(0);
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, [timer, attempts, enabled] {
        if (++*attempts > 300) {
            QTest::qFail("Timed out waiting for the heat configuration dialog", __FILE__, __LINE__);
            if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                modal->reject();
            timer->stop();
            timer->deleteLater();
            delete attempts;
            return;
        }
        auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!modal || modal->objectName() != QStringLiteral("heatConfigDialog"))
            return;
        auto *heat = modal->findChild<QCheckBox *>(QStringLiteral("hc_heatEnabled"));
        auto *buttons = modal->findChild<QDialogButtonBox *>();
        if (!heat || !buttons || !buttons->button(QDialogButtonBox::Ok))
            return;
        timer->stop();
        timer->deleteLater();
        heat->setChecked(enabled);
        buttons->button(QDialogButtonBox::Ok)->click();
        delete attempts;
    });
    timer->start();
}

/** @brief Read climate latitude for configuration-state assertions.
 * @param[in] e Live engine handle, borrowed.
 * @return Latitude degrees, or initial zero/unfilled output after ignored getter
 * refusal. Does not assert, convert or mutate the engine. */
double latitude(SWMM_Engine e)
{
    double v = 0;
    swmm_climate_get_latitude(e, &v);
    return v;
}

/** @brief Read a standard option, falling back to the extended option surface.
 * @param[in] e Live engine handle, borrowed.
 * @param[in] key Non-null standard or extended option key.
 * @return UTF-8 decoded 256-byte-buffer text, possibly empty/partial if both getters
 * fail. Extended-getter status is ignored; no independent error or mutation. */
QString option(SWMM_Engine e, const char *key)
{
    char buf[256] = {};
    if (swmm_options_get(e, key, buf, sizeof buf) != SWMM_OK)
        swmm_options_get_ext(e, key, buf, sizeof buf);
    return QString::fromUtf8(buf);
}

} // namespace

/*! Closure audit C2: an accepted nested editor belongs to the outer dialog's
 *  Apply boundary, so Cancel reverts it and leaves no history entry. */
/** @brief Require an accepted nested climatology edit to revert on outer Cancel with no dirty flag or undo/redo history.
 * Uses a timed real modal interaction and checks latitude degrees before/after. */
void TestSimulationOptionsGates::nestedCancelRevertsAcceptedEdits()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine e = layer->engine();
    const double lat0 = latitude(e);
    QUndoStack stack;
    SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    dlg.setUndoStack(&stack);
    editNextClimatology([lat0](ClimatologyDialog &c) {
        c.findChild<QDoubleSpinBox *>(QStringLiteral("clim_latitude"))->setValue(lat0 + 3);
    });
    emit dlg.findChild<openswmmvis::ui::ModelsPage *>()->showSnowSettingsRequested();
    QCOMPARE(latitude(e), lat0 + 3);
    dlg.reject();
    QCOMPARE(latitude(e), lat0);
    QCOMPARE(stack.count(), 0);
    QVERIFY(!stack.canRedo());
    QVERIFY(!dlg.wroteAnyChanges());
}

/*! One Apply = one Undo entry for the nested climate edit and the page edit;
 *  Undo and Redo move both; a later Cancel keeps what was applied. */
/** @brief Require one Apply to group nested latitude and routing edits into a single undo entry.
 * Outer Cancel after Apply must keep the committed segment; undo/redo move both
 * values together. */
void TestSimulationOptionsGates::applyIsOneUndoEntryForNestedAndPageEdits()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine e = layer->engine();
    const double lat0 = latitude(e);
    const QString routing0 = option(e, "FLOW_ROUTING");
    const char *routing1 = routing0 == QLatin1String("KINWAVE") ? "DYNWAVE" : "KINWAVE";
    QUndoStack stack;
    SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    dlg.setUndoStack(&stack);
    editNextClimatology([lat0](ClimatologyDialog &c) {
        c.findChild<QDoubleSpinBox *>(QStringLiteral("clim_latitude"))->setValue(lat0 + 2);
    });
    emit dlg.findChild<openswmmvis::ui::ModelsPage *>()->showSnowSettingsRequested();
    setRouting(dlg, routing1);
    dlg.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.undoText(), QStringLiteral("Edit simulation options"));
    QCOMPARE(option(e, "FLOW_ROUTING"), QString::fromLatin1(routing1));
    QCOMPARE(latitude(e), lat0 + 2);
    dlg.reject(); // nothing open: the applied segment stays
    QCOMPARE(latitude(e), lat0 + 2);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(option(e, "FLOW_ROUTING"), routing0);
    QCOMPARE(latitude(e), lat0);
    stack.redo();
    QCOMPARE(option(e, "FLOW_ROUTING"), QString::fromLatin1(routing1));
    QCOMPARE(latitude(e), lat0 + 2);
}

/*! Closing the model while a nested editor is open must invalidate both
 *  dialogs before either can accept a stale engine handle or create history. */
/** @brief Require closing the model during a modal climatology edit to disable/reject both dialogs and prevent stale accept/history creation.
 * A repeating timer performs the close; checks null engine, unchanged write flag
 * and empty undo/redo stack after the modal loop returns. */
void TestSimulationOptionsGates::modelCloseDuringNestedEditorInvalidatesWholeSession()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    QUndoStack stack;
    SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr,
                                nullptr);
    dlg.setUndoStack(&stack);

    bool closedDuringNestedEditor = false;
    QTimer closer;
    closer.setInterval(10);
    QObject::connect(&closer, &QTimer::timeout, [&] {
        auto *nested = qobject_cast<ClimatologyDialog *>(QApplication::activeModalWidget());
        if (!nested)
            return;
        auto *latitudeEditor = nested->findChild<QDoubleSpinBox *>(QStringLiteral("clim_latitude"));
        QVERIFY(latitudeEditor);
        latitudeEditor->setValue(latitudeEditor->value() + 5.0);
        layer->closeEngine();
        closedDuringNestedEditor = true;
        QVERIFY(!nested->isEnabled());
        QVERIFY(!dlg.isEnabled());
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onAccept", Qt::DirectConnection));
        closer.stop();
    });
    closer.start();
    emit dlg.findChild<openswmmvis::ui::ModelsPage *>()->showSnowSettingsRequested();
    closer.stop();

    QVERIFY(closedDuringNestedEditor);
    QVERIFY(layer->engine() == nullptr);
    QCOMPARE(dlg.result(), int(QDialog::Rejected));
    QVERIFY(!dlg.wroteAnyChanges());
    QCOMPARE(stack.count(), 0);
    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
}

/*! A climate edit restores every value and provenance flag exactly, in both
 *  directions, and refuses to overwrite an edit made outside the history. */
/** @brief Require climate configuration undo/redo to preserve monthly evaporation values, type, authoring flags and latitude exactly.
 * After an out-of-history latitude change, undo must refuse overwrite, retain
 * 10.5 degrees, report one failure and remove the invalid history entry. */
void TestSimulationOptionsGates::climateUndoIsExactAndRefusesExternalEdits()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine e = layer->engine();
    const QString anchor = QFileInfo(layer->modelFilePath()).absolutePath();
    const auto state = [e] {
        QString s;
        double m[12];
        swmm_climate_get_adjust_evaporation(e, m, 12);
        for (double v : m)
            s += QString::number(v, 'g', 17) + ',';
        int type = -1;
        swmm_climate_get_evap_type(e, &type);
        unsigned flags = 0;
        swmm_heat_get_authoring_flags(e, &flags);
        return s + QString::number(type) + '|' + QString::number(flags) + '|' +
               QString::number(latitude(e), 'g', 17);
    };
    const QString before = state();
    OpenSWMMVis::ConfigSnapshot snapshot(swmm_config_snapshot_create(e));
    const QString identity = OpenSWMMVis::configurationIdentity(e, anchor, {});
    {
        ClimatologyDialog c(e, layer.get());
        c.setProjectAnchor(anchor);
        c.findChild<QDoubleSpinBox *>(QStringLiteral("clim_latitude"))
            ->setValue(latitude(e) + 1.25);
        auto *type = c.findChild<QComboBox *>(QStringLiteral("clim_evapType"));
        type->setCurrentIndex(type->findData(0));
        c.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(c.wroteAnyChanges());
    }
    const QString after = state();
    QVERIFY(after != before);
    QStringList failures;
    QUndoStack stack;
    QVERIFY(OpenSWMMVis::pushConfigurationUndo(
        &stack, layer.get(), QStringLiteral("Edit climatology"), std::move(snapshot), identity,
        anchor, {}, [&failures](const QString &m) { failures << m; }));
    stack.undo();
    QCOMPARE(state(), before);
    stack.redo();
    QCOMPARE(state(), after);
    // Edited outside the history: Undo must refuse rather than overwrite.
    QCOMPARE(swmm_climate_set_latitude(e, 10.5), SWMM_OK);
    stack.undo();
    QCOMPARE(latitude(e), 10.5);
    QCOMPARE(failures.size(), 1);
    QCOMPARE(stack.count(), 0);
}

/** @brief Require a configuration snapshot identity/undo command to include groundwater THERMAL_MODEL.
 * Checks WATER_ONLY versus POROUS_EQUILIBRIUM across undo/redo without errors. */
void TestSimulationOptionsGates::groundwaterThermalConfigurationUndoTracksTransportKeys()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine engine = layer->engine();
    const QString anchor = QFileInfo(layer->modelFilePath()).absolutePath();
    const QStringList keys = openswmmvis::ui::Mesh2DGroundwaterDialog::configurationOptionKeys();
    const QString before = OpenSWMMVis::configurationIdentity(engine, anchor, keys);
    OpenSWMMVis::ConfigSnapshot snapshot(swmm_config_snapshot_create(engine));
    QVERIFY(snapshot);
    QCOMPARE(swmm_gw_transport_option_set(engine, "THERMAL_MODEL", "POROUS_EQUILIBRIUM"), SWMM_OK);
    const QString after = OpenSWMMVis::configurationIdentity(engine, anchor, keys);
    QVERIFY(after != before);
    QStringList failures;
    QUndoStack stack;
    QVERIFY(OpenSWMMVis::pushConfigurationUndo(
        &stack, layer.get(), QStringLiteral("Edit groundwater thermal settings"),
        std::move(snapshot), before, anchor, keys,
        [&failures](const QString &error) { failures << error; }));
    char model[80]{};
    QCOMPARE(swmm_gw_transport_option_get(engine, "THERMAL_MODEL", model, sizeof model), SWMM_OK);
    QCOMPARE(QString::fromUtf8(model), QStringLiteral("POROUS_EQUILIBRIUM"));
    stack.undo();
    QCOMPARE(swmm_gw_transport_option_get(engine, "THERMAL_MODEL", model, sizeof model), SWMM_OK);
    QCOMPARE(QString::fromUtf8(model), QStringLiteral("WATER_ONLY"));
    stack.redo();
    QCOMPARE(swmm_gw_transport_option_get(engine, "THERMAL_MODEL", model, sizeof model), SWMM_OK);
    QCOMPARE(QString::fromUtf8(model), QStringLiteral("POROUS_EQUILIBRIUM"));
    QCOMPARE(failures.size(), 0);
}

/** @brief Require a nested groundwater thermal shortcut edit to join the outer options cancellation boundary.
 * Real modal timer interaction sets porous equilibrium, then outer reject restores
 * water-only with no history or wroteAnyChanges. */
void TestSimulationOptionsGates::groundwaterThermalShortcutJoinsOuterCancelUndoSegment()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine engine = layer->engine();
    char model[80]{};
    QCOMPARE(swmm_gw_transport_option_get(engine, "THERMAL_MODEL", model, sizeof model), SWMM_OK);
    QCOMPARE(QString::fromUtf8(model), QStringLiteral("WATER_ONLY"));
    QUndoStack stack;
    SimulationOptionsDialog dialog(engine, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    dialog.setUndoStack(&stack);
    editNextGroundwaterThermalSettings();
    auto *quality = dialog.findChild<openswmmvis::ui::QualityPage *>();
    QVERIFY(quality);
    emit quality->showHeatSettingsRequested();
    QCOMPARE(swmm_gw_transport_option_get(engine, "THERMAL_MODEL", model, sizeof model), SWMM_OK);
    QCOMPARE(QString::fromUtf8(model), QStringLiteral("POROUS_EQUILIBRIUM"));
    dialog.reject();
    QCOMPARE(swmm_gw_transport_option_get(engine, "THERMAL_MODEL", model, sizeof model), SWMM_OK);
    QCOMPARE(QString::fromUtf8(model), QStringLiteral("WATER_ONLY"));
    QCOMPARE(stack.count(), 0);
    QVERIFY(!dialog.wroteAnyChanges());
}

/** @brief Require accepted heat activation and routing edits to share one Apply undo command, while later un-applied heat disable reverts on Cancel.
 * Checks exact enable/routing states across undo/redo and empty history for the
 * canceled segment. */
void TestSimulationOptionsGates::heatConfigurationSharesOuterUndoAndCancelSegment()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine engine = layer->engine();
    int heatEnabled = -1;
    QCOMPARE(swmm_heat_get_enabled(engine, &heatEnabled), SWMM_OK);
    QCOMPARE(heatEnabled, 0);
    const QString routingBefore = option(engine, "FLOW_ROUTING");
    const char *routingAfter = routingBefore == QLatin1String("KINWAVE") ? "DYNWAVE" : "KINWAVE";

    QUndoStack stack;
    SimulationOptionsDialog applyDialog(engine, layer.get(), QStringLiteral("6.0.0"), nullptr,
                                        nullptr);
    applyDialog.setUndoStack(&stack);
    auto *quality = applyDialog.findChild<openswmmvis::ui::QualityPage *>();
    QVERIFY(quality);
    editNextHeatConfiguration(true);
    emit quality->showHeatSettingsRequested();
    QCOMPARE(swmm_heat_get_enabled(engine, &heatEnabled), SWMM_OK);
    QCOMPARE(heatEnabled, 1);
    setRouting(applyDialog, routingAfter);
    applyDialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.undoText(), QStringLiteral("Edit simulation options"));
    QCOMPARE(option(engine, "FLOW_ROUTING"), QString::fromLatin1(routingAfter));
    applyDialog.reject();

    stack.undo();
    QCOMPARE(swmm_heat_get_enabled(engine, &heatEnabled), SWMM_OK);
    QCOMPARE(heatEnabled, 0);
    QCOMPARE(option(engine, "FLOW_ROUTING"), routingBefore);
    stack.redo();
    QCOMPARE(swmm_heat_get_enabled(engine, &heatEnabled), SWMM_OK);
    QCOMPARE(heatEnabled, 1);
    QCOMPARE(option(engine, "FLOW_ROUTING"), QString::fromLatin1(routingAfter));

    stack.clear();
    SimulationOptionsDialog cancelDialog(engine, layer.get(), QStringLiteral("6.0.0"), nullptr,
                                         nullptr);
    cancelDialog.setUndoStack(&stack);
    quality = cancelDialog.findChild<openswmmvis::ui::QualityPage *>();
    QVERIFY(quality);
    editNextHeatConfiguration(false);
    emit quality->showHeatSettingsRequested();
    QCOMPARE(swmm_heat_get_enabled(engine, &heatEnabled), SWMM_OK);
    QCOMPARE(heatEnabled, 0);
    cancelDialog.reject();
    QCOMPARE(swmm_heat_get_enabled(engine, &heatEnabled), SWMM_OK);
    QCOMPARE(heatEnabled, 1);
    QCOMPARE(stack.count(), 0);
    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
}

/** @brief Require keyboard activation of accessible runtime-overview 2D navigation to close nested heat UI and select the outer 2D page.
 * A bounded timer drives modal phases, checks accessible action text/description,
 * and verifies pending heat enable is reverted by outer Cancel with no history. */
void TestSimulationOptionsGates::runtimeOverviewNavigatesToTwoDSettings()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SimulationOptionsDialog dialog(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr,
                                   nullptr);
    auto *quality = dialog.findChild<openswmmvis::ui::QualityPage *>();
    auto *categories = dialog.findChild<QListWidget *>(QStringLiteral("categories"));
    auto *pages = dialog.findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY(quality);
    QVERIFY(categories);
    QVERIFY(pages);
    QUndoStack stack;
    dialog.setUndoStack(&stack);

    QString failure;
    int phase = 0;
    int attempts = 0;
    auto *driver = new QTimer;
    driver->setInterval(10);
    QObject::connect(driver, &QTimer::timeout, [driver, &failure, &phase, &attempts, categories] {
        if (!failure.isEmpty()) {
            if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                modal->reject();
                return;
            }
            driver->stop();
            driver->deleteLater();
            return;
        }
        if (++attempts > 300) {
            failure = QStringLiteral("The runtime overview navigation did not complete");
            return;
        }
        if (phase == 2 && categories->currentItem() &&
            categories->currentItem()->text().contains(QStringLiteral("2D")))
            return;

        auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (phase == 0) {
            if (!modal || modal->objectName() != QStringLiteral("heatConfigDialog"))
                return;
            auto *overview = modal->findChild<QPushButton *>(QStringLiteral("hc_runtimeOverview"));
            auto *heat = modal->findChild<QCheckBox *>(QStringLiteral("hc_heatEnabled"));
            if (!overview || !heat) {
                failure = QStringLiteral(
                    "Heat Configuration did not expose its editor and overview controls");
                return;
            }
            heat->setChecked(true);
            phase = 1;
            QTimer::singleShot(0, overview, [overview] { overview->click(); });
            return;
        }
        if (phase == 1) {
            if (!modal || modal->objectName() != QStringLiteral("hc_runtimeOverviewDialog"))
                return;
            auto *openTwoD =
                modal->findChild<QPushButton *>(QStringLiteral("hc_overviewOpenTwoDSettings"));
            if (!openTwoD) {
                failure = QStringLiteral("The runtime overview did not expose 2D navigation");
                return;
            }
            if (openTwoD->accessibleName().isEmpty() ||
                openTwoD->accessibleDescription().isEmpty()) {
                failure = QStringLiteral(
                    "The 2D navigation action is missing its accessible name or description");
                return;
            }
            phase = 2;
            openTwoD->setFocus(Qt::OtherFocusReason);
            QTest::keyClick(openTwoD, Qt::Key_Space);
        }
    });
    driver->start();
    emit quality->showHeatSettingsRequested();
    driver->stop();
    driver->deleteLater();

    int twoDRow = -1;
    for (int row = 0; row < categories->count(); ++row)
        if (categories->item(row)->text().contains(QStringLiteral("2D"))) {
            twoDRow = row;
            break;
        }
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(twoDRow >= 0);
    QCOMPARE(categories->currentRow(), twoDRow);
    QCOMPARE(pages->currentIndex(), twoDRow);
    QVERIFY(dialog.wroteAnyChanges());
    int heatEnabled = 0;
    QCOMPARE(swmm_heat_get_enabled(layer->engine(), &heatEnabled), SWMM_OK);
    QCOMPARE(heatEnabled, 1);
    dialog.reject();
    QCOMPARE(swmm_heat_get_enabled(layer->engine(), &heatEnabled), SWMM_OK);
    QCOMPARE(heatEnabled, 0);
    QCOMPARE(stack.count(), 0);
}

namespace
{

/*! The SAVE HOTSTART lines of the `.inp` the engine writes to \a path. */
/** @brief Serialize a model and collect its apparent SAVE HOTSTART rows.
 * @param[in] e Live writable model handle, borrowed.
 * @param[in] path Controlled destination .inp, potentially overwritten; parent exists.
 * @return Owned whitespace-simplified matching rows, empty on write/open refusal
 * or no rows. Does not distinguish those outcomes.
 * Matches case-insensitive line prefixes after simplification; no full section
 * parser, file deletion or engine ownership transfer. */
QStringList savedHotstartRows(SWMM_Engine e, const QString &path)
{
    if (swmm_model_write(e, path.toUtf8().constData()) != SWMM_OK)
        return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QStringList rows;
    for (const QString &line : QString::fromUtf8(f.readAll()).split('\n'))
        if (line.simplified().startsWith(QLatin1String("SAVE HOTSTART"), Qt::CaseInsensitive))
            rows << line.simplified();
    return rows;
}

} // namespace

// Simulation Options > Files keeps each SAVE HOTSTART row's format: a row
// read from the file shows it, OK without an edit leaves the [FILES] rows
// as they were, and a changed format reaches the engine and the `.inp`.
// Written decks: tests/gui/data/hotstart_save_formats_out/ (for review).
/** @brief Require scheduled Auto/Native/Legacy formats to hydrate, remain unchanged on no-edit write and persist an edited format.
 * Writes retained before/unedited/edited decks, rereads model rows and checks
 * SAVE/USE HOTSTART paths rebase correctly into subdirectories. Does not create
 * a hotstart payload or run a simulation. */
void TestSimulationOptionsGates::hotstartSaveFormatsSurviveFilesPage()
{
    auto layer = openLayer(fixture(QStringLiteral("hotstart_save_formats.inp")));
    QVERIFY(layer);
    SWMM_Engine e = layer->engine();
    const QString out = fixture(QStringLiteral("hotstart_save_formats_out"));
    QVERIFY(QDir().mkpath(out));
    const int fileFormats[] = {SWMM_HOTSTART_FORMAT_AUTO, SWMM_HOTSTART_FORMAT_NATIVE,
                               SWMM_HOTSTART_FORMAT_LEGACY};
    for (int i = 0; i < 3; ++i) { // the project load keeps what the file says
        int f = -1;
        QCOMPARE(swmm_hotstart_saves_get_format(e, i, &f), SWMM_OK);
        QCOMPARE(f, fileFormats[i]);
    }

    openswmmvis::ui::SimOptionsContext ctx(e, layer.get(), nullptr, {});
    openswmmvis::ui::FilesPage page(ctx);
    page.read();
    auto *model = page.findChild<HotstartSavesModel *>();
    QVERIFY(model);
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(model->formatAt(0), int(SWMM_HOTSTART_FORMAT_AUTO));
    QCOMPARE(model->formatAt(1), int(SWMM_HOTSTART_FORMAT_NATIVE));
    QCOMPARE(model->formatAt(2), int(SWMM_HOTSTART_FORMAT_LEGACY));
    QCOMPARE(
        model->data(model->index(1, HotstartSavesModel::ColFormat), Qt::DisplayRole).toString(),
        QStringLiteral("Native"));

    const QStringList before = savedHotstartRows(e, out + QStringLiteral("/before.inp"));
    QCOMPARE(before.size(), 3);
    QVERIFY(!before[0].contains(QLatin1String("NATIVE")) &&
            !before[0].contains(QLatin1String("LEGACY")));
    QVERIFY(before[1].endsWith(QLatin1String("NATIVE")));
    QVERIFY(before[2].endsWith(QLatin1String("LEGACY")));

    page.write(); // OK with nothing edited
    QCOMPARE(savedHotstartRows(e, out + QStringLiteral("/unedited.inp")), before);

    QVERIFY(model->setData(model->index(0, HotstartSavesModel::ColFormat),
                           int(SWMM_HOTSTART_FORMAT_NATIVE), Qt::EditRole));
    page.write();
    int fmt = -1;
    QCOMPARE(swmm_hotstart_saves_get_format(e, 0, &fmt), SWMM_OK);
    QCOMPARE(fmt, int(SWMM_HOTSTART_FORMAT_NATIVE));
    const QStringList edited = savedHotstartRows(e, out + QStringLiteral("/edited.inp"));
    QCOMPARE(edited.size(), 3);
    // The page rebuilds every row on an edit. The engine writes paths
    // relative to the output deck, so all three still resolve to the same
    // files from this subfolder.
    QCOMPARE(edited[0], QStringLiteral("SAVE HOTSTART \"../end_auto.hsf\" NATIVE"));
    QCOMPARE(edited[1],
             QStringLiteral("SAVE HOTSTART \"../dated_native.hsf\" 01/01/2020 03:00:00 NATIVE"));
    QCOMPARE(edited[2], QStringLiteral("SAVE HOTSTART \"../end_legacy.hsf\" LEGACY"));

    page.read(); // what reopening the dialog shows
    QCOMPARE(model->formatAt(0), int(SWMM_HOTSTART_FORMAT_NATIVE));
    QCOMPARE(model->formatAt(2), int(SWMM_HOTSTART_FORMAT_LEGACY));
    QCOMPARE(model->pathAt(0), QStringLiteral("end_auto.hsf")); // shown relative

    // Other [FILES] paths the page writes are rebased the same way.
    auto *use = page.findChild<openswmmvis::ui::RelativePathPicker *>(
        QStringLiteral("hotstartUsePathEdit"));
    QVERIFY(use);
    use->setPath(QStringLiteral("warm.hsf"));
    page.write();
    QVERIFY(QDir().mkpath(out + QStringLiteral("/sub")));
    QVERIFY(swmm_model_write(e, (out + QStringLiteral("/sub/use.inp")).toUtf8().constData()) ==
            SWMM_OK);
    QFile written(out + QStringLiteral("/sub/use.inp"));
    QVERIFY(written.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(written.readAll());
    QVERIFY2(text.contains(QStringLiteral("\"../../warm.hsf\"")),
             qPrintable(text.section(QStringLiteral("[FILES]"), 1, 1).left(400)));
    QVERIFY(text.contains(QStringLiteral("\"../../end_auto.hsf\" NATIVE")));
}

/*! Review E-14 (workplans/GUI_COMPREHENSIVE_REVIEW_2026-11-12.md): the page's
 *  dates are local-time QDateTimes, so a window spanning a daylight-saving
 *  change measured 47 h for two wall-clock days. The engine counts wall clock. */
void TestSimulationOptionsGates::durationAcrossDaylightSavingIsWallClock()
{
    const QByteArray oldTz = qgetenv("TZ");
    qputenv("TZ", "America/New_York");
    tzset();
    auto restore = qScopeGuard([&oldTz] {
        if (oldTz.isNull())
            qunsetenv("TZ");
        else
            qputenv("TZ", oldTz);
        tzset();
    });

    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    auto e = layer->engine();
    // US clocks spring forward at 02:00 on Sunday 2024-03-10.
    QVERIFY(swmm_options_set(e, "START_DATE", "03/09/2024") == SWMM_OK);
    QVERIFY(swmm_options_set(e, "START_TIME", "00:00:00") == SWMM_OK);
    QVERIFY(swmm_options_set(e, "END_DATE", "03/11/2024") == SWMM_OK);
    QVERIFY(swmm_options_set(e, "END_TIME", "00:00:00") == SWMM_OK);
    QVERIFY(swmm_options_set(e, "REPORT_START_DATE", "03/09/2024") == SWMM_OK);
    QVERIFY(swmm_options_set(e, "REPORT_START_TIME", "00:00:00") == SWMM_OK);
    openswmmvis::ui::SimOptionsContext ctx(e, nullptr, nullptr, {});
    openswmmvis::ui::DatesPage page(ctx);
    page.read();
    QCOMPARE(page.durationSeconds(), qint64(2 * 86400));

    // A start inside the spring-forward hour does not exist in local time; it
    // must still read and write back unchanged (it was moved to 03:30).
    QVERIFY(swmm_options_set(e, "START_DATE", "03/10/2024") == SWMM_OK);
    QVERIFY(swmm_options_set(e, "START_TIME", "02:30:00") == SWMM_OK);
    QVERIFY(swmm_options_set(e, "REPORT_START_DATE", "03/10/2024") == SWMM_OK);
    QVERIFY(swmm_options_set(e, "REPORT_START_TIME", "02:30:00") == SWMM_OK);
    openswmmvis::ui::DatesPage gapPage(ctx);
    gapPage.read();
    gapPage.write();
    QCOMPARE(option(e, "START_DATE"), QStringLiteral("03/10/2024"));
    QCOMPARE(option(e, "START_TIME"), QStringLiteral("02:30:00"));
    QCOMPARE(option(e, "REPORT_START_TIME"), QStringLiteral("02:30:00"));
}

/*! Review E-17: the routing-step box validated in the SYSTEM locale but was
 *  parsed in the C locale. Under a decimal-comma locale the box accepted
 *  "0,5", the C-locale parse failed and the value was silently not written.
 *  Contract: a comma is never accepted, and what is written is what is shown. */
void TestSimulationOptionsGates::routingStepTypedUnderDecimalCommaLocaleIsWritten()
{
    const QLocale oldDefault;
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    auto restore = qScopeGuard([&oldDefault] { QLocale::setDefault(oldDefault); });

    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    auto e = layer->engine();
    openswmmvis::ui::SimOptionsContext ctx(e, nullptr, nullptr, {});
    openswmmvis::ui::DatesPage page(ctx);
    page.read();
    QLineEdit *routing = nullptr;
    for (auto *edit : page.findChildren<QLineEdit *>())
        if (edit->property("optionKey").toString().split(',').contains(QLatin1String("ROUTING_STEP")))
            routing = edit;
    QVERIFY(routing);

    routing->clear();
    QTest::keyClicks(routing, QStringLiteral("0,5"));
    QVERIFY2(!routing->text().contains(QLatin1Char(',')), qPrintable(routing->text()));
    page.write();
    bool shownOk = false;
    const double shown = QLocale::c().toDouble(routing->text(), &shownOk);
    QVERIFY(shownOk);
    QCOMPARE(option(e, "ROUTING_STEP").toDouble(), shown);

    routing->clear();
    QTest::keyClicks(routing, QStringLiteral("0.5"));
    QCOMPARE(routing->text(), QStringLiteral("0.5"));
    page.write();
    QCOMPARE(option(e, "ROUTING_STEP").toDouble(), 0.5);
}

/*! Review G-05/G-06: the [OPTIONS] and [2D_OPTIONS] keys that had no editor now have one that
 *  reads the engine and writes it back on Apply. TEMPDIR is left out on purpose (the engine
 *  ignores it); LINK_OFFSETS stays on the status-bar switch, which converts the offsets. */
void TestSimulationOptionsGates::reviewG05G06OptionsHaveEditorsAndApply()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SWMM_Engine e = layer->engine();
    SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    auto editorFor = [&](const char *key) -> QWidget * {
        for (QWidget *w : dlg.findChildren<QWidget *>())
            if (w->property("optionKey").toString().split(QLatin1Char(',')).contains(QLatin1String(key)))
                return w;
        return nullptr;
    };
    for (const char *key : {"FRONT_REBUILD", "COUPLING_IN_FLOODING", "HEAT_SURFACE_EXCHANGE",
                            "HEAT_RADIATIVE_EXCHANGE", "WATER_DENSITY",
                            "WATER_SPECIFIC_HEAT_CAPACITY", "WIND_FUNC_COEFF_A",
                            "WIND_FUNC_COEFF_B", "PRESSURE_RATIO", "FV_NODE_FEEDBACK_DT",
                            "WRITE_ABSOLUTE_PATHS"})
        QVERIFY2(editorFor(key), key);

    auto *flooding = qobject_cast<QCheckBox *>(editorFor("COUPLING_IN_FLOODING"));
    auto *front = qobject_cast<QComboBox *>(editorFor("FRONT_REBUILD"));
    auto *density = qobject_cast<QLineEdit *>(editorFor("WATER_DENSITY"));
    auto *windA = qobject_cast<QLineEdit *>(editorFor("WIND_FUNC_COEFF_A"));
    auto *feedback = qobject_cast<QCheckBox *>(editorFor("FV_NODE_FEEDBACK_DT"));
    auto *absolute = qobject_cast<QCheckBox *>(editorFor("WRITE_ABSOLUTE_PATHS"));
    QVERIFY(flooding && front && density && windA && feedback && absolute);
    QCOMPARE(density->text().toDouble(), 1000.0);
    QCOMPARE(windA->text().toDouble(), 1.505e-8);
    flooding->setChecked(true);
    front->setCurrentIndex(front->findData(QStringLiteral("YES")));
    density->setText(QStringLiteral("998.2"));
    windA->setText(QStringLiteral("2e-8"));
    feedback->setChecked(true);
    absolute->setChecked(true);
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));

    char buf[64] = {};
    QCOMPARE(swmm_options_get_ext(e, "COUPLING_IN_FLOODING", buf, sizeof buf), 0);
    QCOMPARE(QString::fromLatin1(buf), QStringLiteral("YES"));
    QCOMPARE(swmm_options_get_ext(e, "FRONT_REBUILD", buf, sizeof buf), 0);
    QCOMPARE(QString::fromLatin1(buf), QStringLiteral("YES"));
    QCOMPARE(swmm_options_get(e, "WATER_DENSITY", buf, sizeof buf), 0);
    QCOMPARE(QString::fromLatin1(buf).toDouble(), 998.2);
    QCOMPARE(swmm_options_get(e, "WIND_FUNC_COEFF_A", buf, sizeof buf), 0);
    QCOMPARE(QString::fromLatin1(buf).toDouble(), 2e-8);
    QCOMPARE(swmm_options_get(e, "FV_NODE_FEEDBACK_DT", buf, sizeof buf), 0);
    QCOMPARE(SimulationOptionsDialog::parseEngineBool(QString::fromLatin1(buf)), Qt::Checked);
    QCOMPARE(swmm_options_get(e, "WRITE_ABSOLUTE_PATHS", buf, sizeof buf), 0);
    QCOMPARE(SimulationOptionsDialog::parseEngineBool(QString::fromLatin1(buf)), Qt::Checked);
}

namespace
{
constexpr const char *kArdComponent = "org.hydrocouple.openswmm.transport.ard";

/** @brief A fresh copy of the G-02 fixture (model + reactions file) in a reviewable folder.
 * @param[in] name Run folder under tests/output/simulationoptions_gates_g02/.
 * @return The copied model path; empty when a copy failed. */
QString ardFixtureCopy(const QString &name)
{
    const QString dir = QDir(dataDir()).absoluteFilePath(
        QStringLiteral("../../output/simulationoptions_gates_g02/") + name);
    QDir(dir).removeRecursively();
    if (!QDir().mkpath(dir))
        return {};
    for (const char *file : {"ard_transport.inp", "ard_transport.rxn"}) {
        const QString source = fixture(QStringLiteral("ard_transport/") + QLatin1String(file));
        if (!QFile::copy(source, QDir(dir).filePath(QLatin1String(file))))
            return {};
    }
    return QDir(dir).filePath(QStringLiteral("ard_transport.inp"));
}

int ardCount(SWMM_Engine e, int (*count)(SWMM_Engine, int *))
{
    int n = -1;
    return count(e, &n) == SWMM_OK ? n : -1;
}
} // namespace

void TestSimulationOptionsGates::ardTransportRowsAreCheckedBeforeApply()
{
    using openswmmvis::ui::ArdConduitDispersionModel;
    using openswmmvis::ui::ArdTransportRowModel;
    auto layer = openLayer(ardFixtureCopy(QStringLiteral("rows")));
    QVERIFY2(layer, "ard_transport.inp failed to load");
    SWMM_Engine e = layer->engine();

    ArdTransportRowModel boundaries(ArdTransportRowModel::Kind::Boundary);
    QAbstractItemModelTester tester(&boundaries,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(boundaries.load(e), QString());
    QCOMPARE(boundaries.rowCount(), 0);
    QCOMPARE(boundaries.species(), (QStringList{QStringLiteral("NH3"), QStringLiteral("CL2")}));
    QCOMPARE(boundaries.elements(),
             (QStringList{QStringLiteral("J1"), QStringLiteral("J2"), QStringLiteral("O1")}));
    QCOMPARE(boundaries.validateCommit(e), QString()); // clean: nothing to check

    const auto refuses = [&](const QString &text) {
        const QString error = boundaries.validateCommit(e);
        return error.contains(QStringLiteral("Boundary row 2")) && error.contains(text);
    };
    boundaries.addRow({QStringLiteral("J1"), QStringLiteral("NH3"), false, 2.0, {}});
    boundaries.addRow({QStringLiteral("C1"), QStringLiteral("CL2"), false, 1.0, {}});
    QVERIFY2(refuses(QStringLiteral("not a node")), qPrintable(boundaries.validateCommit(e)));
    QVERIFY(boundaries.setData(boundaries.index(1, ArdTransportRowModel::ElementColumn),
                               QStringLiteral("J2"), Qt::EditRole));
    QVERIFY(boundaries.setData(boundaries.index(1, ArdTransportRowModel::SpeciesColumn),
                               QStringLiteral("TSS"), Qt::EditRole));
    QVERIFY2(refuses(QStringLiteral("pollutants enter through inflows")),
             qPrintable(boundaries.validateCommit(e)));
    QVERIFY(boundaries.setData(boundaries.index(1, ArdTransportRowModel::SpeciesColumn),
                               QStringLiteral("CL2"), Qt::EditRole));
    QVERIFY(boundaries.setData(boundaries.index(1, ArdTransportRowModel::ValueFromColumn),
                               QStringLiteral("Time series"), Qt::EditRole));
    QVERIFY(!(boundaries.flags(boundaries.index(1, ArdTransportRowModel::ValueColumn)) &
              Qt::ItemIsEditable));
    QVERIFY2(refuses(QStringLiteral("choose a time series")),
             qPrintable(boundaries.validateCommit(e)));
    QVERIFY(boundaries.setData(boundaries.index(1, ArdTransportRowModel::SeriesColumn),
                               QStringLiteral("TS1"), Qt::EditRole));
    QCOMPARE(boundaries.validateCommit(e), QString());
    boundaries.addRow({QStringLiteral("J1"), QStringLiteral("NH3"), false, 3.0, {}});
    QVERIFY(boundaries.validateCommit(e).contains(QStringLiteral("already has a row")));
    QVERIFY(boundaries.setData(boundaries.index(2, ArdTransportRowModel::ElementColumn),
                               QStringLiteral("O1"), Qt::EditRole));
    QVERIFY(boundaries.setData(boundaries.index(2, ArdTransportRowModel::ValueColumn),
                               QStringLiteral("-1"), Qt::EditRole));
    QVERIFY(boundaries.validateCommit(e).contains(QStringLiteral("cannot be negative")));
    boundaries.removeRowAt(2);
    QCOMPARE(boundaries.validateCommit(e), QString());
    QCOMPARE(boundaries.commit(e), QString());
    boundaries.acceptChanges();
    QVERIFY(!boundaries.isDirty());
    ArdTransportRowModel reread(ArdTransportRowModel::Kind::Boundary);
    QCOMPARE(reread.load(e), QString());
    QCOMPARE(reread.rows(), boundaries.rows());
    // A later commit replaces the engine's rows rather than adding to them.
    reread.removeRowAt(0);
    QCOMPARE(reread.commit(e), QString());
    QCOMPARE(ardCount(e, swmm_transport_boundary_count), 1);

    // Sources: conduits only (the orifice is not offered), negative rates extract.
    ArdTransportRowModel sources(ArdTransportRowModel::Kind::Source);
    QCOMPARE(sources.load(e), QString());
    QCOMPARE(sources.elements(), (QStringList{QStringLiteral("C1"), QStringLiteral("C2")}));
    sources.addRow({QStringLiteral("OR1"), QStringLiteral("NH3"), false, -0.1, {}});
    QVERIFY(sources.validateCommit(e).contains(QStringLiteral("not a conduit")));
    QVERIFY(sources.setData(sources.index(0, 0), QStringLiteral("C1"), Qt::EditRole));
    QCOMPARE(sources.validateCommit(e), QString());
    QCOMPARE(sources.commit(e), QString());
    QCOMPARE(ardCount(e, swmm_transport_source_count), 1);

    ArdConduitDispersionModel conduits;
    QCOMPARE(conduits.load(e), QString());
    conduits.addRow({QStringLiteral("OR1"), 1.0});
    QVERIFY(conduits.validateCommit(e).contains(QStringLiteral("not a conduit")));
    QVERIFY(conduits.setData(conduits.index(0, 0), QStringLiteral("C2"), Qt::EditRole));
    QVERIFY(conduits.setData(conduits.index(0, 1), QStringLiteral("-2"), Qt::EditRole));
    QVERIFY(conduits.validateCommit(e).contains(QStringLiteral("not negative")));
    QVERIFY(conduits.setData(conduits.index(0, 1), QStringLiteral("0.123456789012345"),
                             Qt::EditRole));
    QCOMPARE(conduits.data(conduits.index(0, 1), Qt::EditRole).toString(),
             QStringLiteral("0.123456789012345")); // the editor sees every digit
    QCOMPARE(conduits.commit(e), QString());
    int link = -1;
    double value = 0.0;
    QCOMPARE(swmm_transport_get_conduit_disp(e, 0, &link, &value), SWMM_OK);
    QCOMPARE(QString::fromUtf8(swmm_link_id(e, link)), QStringLiteral("C2"));
    QCOMPARE(value, 0.123456789012345); // every digit kept
}

void TestSimulationOptionsGates::ardTransportPageAppliesUndoesAndSaves()
{
    using openswmmvis::ui::ArdConduitDispersionModel;
    using openswmmvis::ui::ArdTransportRowModel;
    const QString model = ardFixtureCopy(QStringLiteral("apply"));
    auto layer = openLayer(model);
    QVERIFY2(layer, "ard_transport.inp failed to load");
    SWMM_Engine e = layer->engine();
    QUndoStack stack;
    SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    dlg.setUndoStack(&stack);

    auto *mode = dlg.findChild<QComboBox *>(QStringLiteral("ardDispersionCombo"));
    auto *coefficient = dlg.findChild<QLineEdit *>(QStringLiteral("ardDispersionValue"));
    auto *spacing = dlg.findChild<QLineEdit *>(QStringLiteral("ardTargetDx"));
    auto *fileLabel = dlg.findChild<QLabel *>(QStringLiteral("ardFileLabel"));
    auto *speciesHint = dlg.findChild<QLabel *>(QStringLiteral("ardSpeciesHint"));
    auto *conduits = dlg.findChild<ArdConduitDispersionModel *>(QStringLiteral("ardConduitDispersion"));
    auto *boundaries = dlg.findChild<ArdTransportRowModel *>(QStringLiteral("ardBoundaries"));
    auto *sources = dlg.findChild<ArdTransportRowModel *>(QStringLiteral("ardSources"));
    auto *boundaryAdd = dlg.findChild<QPushButton *>(QStringLiteral("ardBoundariesAdd"));
    auto *page = dlg.findChild<openswmmvis::ui::QualityPage *>();
    QVERIFY(mode && coefficient && spacing && fileLabel && speciesHint && conduits &&
            boundaries && sources && boundaryAdd && page);
    QVERIFY2(fileLabel->text().contains(QStringLiteral("ard_transport.ard")),
             qPrintable(fileLabel->text()));
    QVERIFY(speciesHint->isHidden()); // the fixture has reaction species
    QVERIFY(boundaryAdd->isEnabled());
    QVERIFY(!coefficient->isEnabled()); // dispersion is Off

    // An Apply with nothing changed writes no transport setting and registers no file.
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
    int configured = -1;
    QCOMPARE(swmm_transport_get_configured(e, &configured), SWMM_OK);
    QCOMPARE(configured, 0);
    QVERIFY(swmm_process_component_find(e, kArdComponent) < 0);
    for (const char *key : {"TRANSPORT_OPTIONS", "CONDUIT_DISPERSION", "TRANSPORT_BOUNDARIES",
                            "TRANSPORT_SOURCES"})
        QVERIFY2(!dlg.lastWriteKeys().contains(QLatin1String(key)), key);
    const int stepsBefore = stack.count();

    mode->setCurrentIndex(mode->findData(int(SWMM_DISPERSION_VALUE)));
    QVERIFY(coefficient->isEnabled());
    coefficient->setText(QStringLiteral("0.25"));
    spacing->setText(QStringLiteral("10"));
    conduits->addRow({QStringLiteral("C1"), 1.5});
    QTest::mouseClick(boundaryAdd, Qt::LeftButton);
    QCOMPARE(boundaries->rowCount(), 1);
    QCOMPARE(boundaries->rows().first(),
             (ArdTransportRowModel::Row{QStringLiteral("J1"), QStringLiteral("NH3"), false, 0.0, {}}));
    QVERIFY(boundaries->setData(boundaries->index(0, ArdTransportRowModel::ValueColumn),
                                QStringLiteral("-1"), Qt::EditRole));
    QString warn;
    QVERIFY(!page->validate(&warn)); // refused before anything is written
    QVERIFY2(warn.contains(QStringLiteral("Boundary row 1")) &&
                 warn.contains(QStringLiteral("negative")),
             qPrintable(warn));
    QVERIFY(boundaries->setData(boundaries->index(0, ArdTransportRowModel::ValueColumn),
                                QStringLiteral("2"), Qt::EditRole));
    boundaries->addRow({QStringLiteral("J2"), QStringLiteral("CL2"), true, 0.0, QStringLiteral("TS1")});
    sources->addRow({QStringLiteral("C2"), QStringLiteral("NH3"), false, -0.1, {}});
    // The Files page edits its component table in the same Apply.
    auto *components = dlg.findChild<ProcessComponentsModel *>();
    QVERIFY(components);
    components->appendRow(QString(), QString());

    QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
    const auto applied = [&](SWMM_Engine engine) {
        int m = -1;
        double v = 0.0, dx = 0.0;
        swmm_transport_get_dispersion_mode(engine, &m);
        swmm_transport_get_dispersion_value(engine, &v);
        swmm_transport_get_target_dx(engine, &dx);
        return m == SWMM_DISPERSION_VALUE && v == 0.25 && dx == 10.0 &&
               ardCount(engine, swmm_transport_conduit_disp_count) == 1 &&
               ardCount(engine, swmm_transport_boundary_count) == 2 &&
               ardCount(engine, swmm_transport_source_count) == 1;
    };
    QVERIFY(applied(e));
    const int component = swmm_process_component_find(e, kArdComponent);
    QVERIFY2(component >= 0, "the transport file was not registered");
    char id[256]{}, config[1024]{}, resolved[1024]{};
    QCOMPARE(swmm_process_component_get(e, component, id, sizeof id, config, sizeof config,
                                        resolved, sizeof resolved),
             SWMM_OK);
    QCOMPARE(QString::fromUtf8(config), QStringLiteral("ard_transport.ard"));
    for (const char *key : {"TRANSPORT_OPTIONS", "CONDUIT_DISPERSION", "TRANSPORT_BOUNDARIES",
                            "TRANSPORT_SOURCES"})
        QVERIFY2(dlg.lastWriteKeys().contains(QLatin1String(key)), key);
    QCOMPARE(stack.count(), stepsBefore + 1);

    // One undo step takes it all back, including the registration; redo restores it.
    stack.undo();
    QCOMPARE(swmm_transport_get_configured(e, &configured), SWMM_OK);
    QCOMPARE(configured, 0);
    QCOMPARE(ardCount(e, swmm_transport_boundary_count), 0);
    QCOMPARE(ardCount(e, swmm_transport_conduit_disp_count), 0);
    QVERIFY(swmm_process_component_find(e, kArdComponent) < 0);
    stack.redo();
    QVERIFY(applied(e));
    QVERIFY(swmm_process_component_find(e, kArdComponent) >= 0);

    // Saving writes the transport file; a reopened model reads the same settings.
    const QString saved = QFileInfo(model).dir().filePath(QStringLiteral("saved.inp"));
    QCOMPARE(swmm_model_write(e, saved.toUtf8().constData()), SWMM_OK);
    QFile ard(QFileInfo(model).dir().filePath(QStringLiteral("ard_transport.ard")));
    QVERIFY2(ard.open(QIODevice::ReadOnly), "the transport file was not written");
    const QString text = QString::fromUtf8(ard.readAll());
    QVERIFY2(text.contains(QStringLiteral("TRANSPORT_BOUNDARIES")) &&
                 text.contains(QStringLiteral("TS1")),
             qPrintable(text));
    auto reopened = openLayer(saved);
    QVERIFY2(reopened, "the saved model failed to reopen");
    QVERIFY(applied(reopened->engine()));

    // Removing the component on the Files page sticks when no transport setting changed.
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection)); // re-read the pages
    int row = -1;
    for (int r = 0; r < components->rowCount(); ++r)
        if (components->idAt(r) == QLatin1String(kArdComponent))
            row = r;
    QVERIFY(row >= 0);
    QVERIFY(components->removeRows(row, 1));
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
    QVERIFY(swmm_process_component_find(e, kArdComponent) < 0);
}
