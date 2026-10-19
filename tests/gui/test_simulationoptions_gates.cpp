/*!
 * \file   test_simulationoptions_gates.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Structural contract for the Simulation Options dialog restructure
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
 */
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/simoptions/datespage.h"
#include "ui/dialogs/simoptions/modelspage.h"
#include "ui/dialogs/simoptions/qualitypage.h"
#include "ui/dialogs/climatologydialog.h"
#include "ui/dialogs/mesh2dgroundwaterdialog.h"
#include "ui/configurationsession.h"
#include <openswmm/engine/openswmm_climate.h>
#include <openswmm/engine/openswmm_heat.h>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QPushButton>
#include <QTimer>
#include <QUndoStack>

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <openswmm/engine/openswmm_model.h>
#include <QCheckBox>
#include <QCoreApplication>
#include <QListWidget>
#include <QObject>
#include <QScrollArea>
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

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixture(const QString &name) { return QDir(dataDir()).filePath(name); }

std::unique_ptr<SWMMModelLayer> openLayer(const QString &path)
{
    auto layer = std::make_unique<SWMMModelLayer>(path, nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

/*! Walk up from \a w; true when \a ancestor is on the chain. */
bool hasAncestor(const QWidget *w, const QWidget *ancestor)
{
    for (const QWidget *p = w; p; p = p->parentWidget())
        if (p == ancestor) return true;
    return false;
}

/*! The object names the 2D-output, processes-hub and groundwater page tests
 *  look up. Every one must survive the page split verbatim: they are the
 *  contract those three tests hold the dialog to. */
const char *const kLoadBearingNames[] = {
    // Models / Processes — transport matrix hub + the module toggle the 2D
    // sidebar row gates on, and the read-only FLOW_ROUTING mirror
    "transportMatrixTable", "module2DBox", "routingMirrorLabel",
    // page headers that gate their own tabs (PLAN §2.1)
    "flowRoutingCombo", "qualitySolverCombo",
    // 2D › Processes
    "infil2DModeCombo", "infil2DStepSameBox", "infil2DStepEdit",
    "infil2DMethodCombo", "infil2DDestCombo", "editInfilCellsBtn", "evap2DCombo",
    // 2D › Groundwater
    "gw2DEnableCombo", "gw2DEtCombo", "gw2DStatusLabel", "gw2DEditBtn",
    "gwLinkSeepageCombo", "gwWiltingAuto", "gwWiltingSuction", "gwEtAllocationLabel",
    // 2D › Output
    "output2DPrecisionCombo", "output2DCompressionSpin", "output2DSizeLabel",
    "report2DStepSameBox", "report2DStepEdit", "report2DVarsList",
    "report2DAllSpeciesBox", "report2DSpeciesList",
    "report2DPreset_DEFAULT", "report2DPreset_MINIMAL", "report2DPreset_ALL",
    // Files / Output / Plugins — process components
    "processComponentsView",
    // dialog chrome the gate tests navigate by
    "categories", "pages",
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
};

namespace {

/*! Sidebar row index whose text is \a title, or -1. */
int rowOf(const QListWidget *cats, const QString &title)
{
    for (int i = 0; i < cats->count(); ++i)
        if (cats->item(i)->text() == title) return i;
    return -1;
}

/*! Enabled state of a sidebar row (rows are greyed, never hidden — PLAN §4.3). */
bool rowEnabled(const QListWidget *cats, int row)
{
    return cats->item(row)->flags().testFlag(Qt::ItemIsEnabled);
}

/*! Select FLOW_ROUTING by its engine token. */
void setRouting(SimulationOptionsDialog &dlg, const char *token)
{
    auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("flowRoutingCombo"));
    QVERIFY(combo);
    const int idx = combo->findData(QLatin1String(token));
    QVERIFY(idx >= 0);
    combo->setCurrentIndex(idx);
}

} // namespace

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

void TestSimulationOptionsGates::reportStartReloadsEarlierSimulationWindow()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    auto e = layer->engine();
    auto setWindow = [e](const char *date, const char *start, const char *end,
                         const char *report) {
        return swmm_options_set(e, "START_DATE", date) == SWMM_OK
            && swmm_options_set(e, "START_TIME", start) == SWMM_OK
            && swmm_options_set(e, "END_DATE", date) == SWMM_OK
            && swmm_options_set(e, "END_TIME", end) == SWMM_OK
            && swmm_options_set(e, "REPORT_START_DATE", date) == SWMM_OK
            && swmm_options_set(e, "REPORT_START_TIME", report) == SWMM_OK;
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
    QCOMPARE(report->dateTime(), QDateTime(QDate(2026, 10, 13), QTime(9, 30, 7)));
    page.write();
    QCOMPARE(ctx.option("REPORT_START_DATE"), QStringLiteral("10/13/2026"));
    QCOMPARE(ctx.option("REPORT_START_TIME"), QStringLiteral("09:30:07"));

    // Expanding the window later must also release its previous upper bound.
    QVERIFY(setWindow("10/15/2026", "14:00:00", "17:00:00", "16:00:03"));
    page.read();
    QCOMPARE(report->dateTime(), QDateTime(QDate(2026, 10, 15), QTime(16, 0, 3)));
    page.write();
    QCOMPARE(ctx.option("REPORT_START_TIME"), QStringLiteral("16:00:03"));
}

/*! T0 — the names exist and are parented into the page stack. */
void TestSimulationOptionsGates::emptyProjectCanReachHeatActivation()
{
    auto engine=swmm_engine_new();QVERIFY(engine);
    {
        SimulationOptionsDialog dialog(engine,nullptr,"6.0.0",nullptr,nullptr);
        auto* categories=dialog.findChild<QListWidget*>("categories");QVERIFY(categories);
        bool found=false;
        for(int i=0;i<categories->count();++i)if(categories->item(i)->text()=="Quality & Transport") {
            found=true;QVERIFY(categories->item(i)->flags() & Qt::ItemIsEnabled);
        }
        QVERIFY(found);
    }
    swmm_engine_destroy(engine);
}

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
        if (!w) { missing << QLatin1String(name); continue; }
        // The chrome itself is not inside the stack; everything else must be.
        if (qstrcmp(name, "categories") == 0 || qstrcmp(name, "pages") == 0) continue;
        if (!hasAncestor(w, pages)) unparented << QLatin1String(name);
    }
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("objectName(s) no longer resolve: %1")
                            .arg(missing.join(QStringLiteral(", ")))));
    QVERIFY2(unparented.isEmpty(),
             qPrintable(QStringLiteral("built but not laid out under the page stack: %1")
                            .arg(unparented.join(QStringLiteral(", ")))));
}

/*! T0 — every written key has a tagged editor, and every tagged editor is
 *  reachable from the page stack. */
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
        if (!key.isValid()) continue;
        const QStringList keys = key.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &k : keys) tagged.insert(k);
        if (!hasAncestor(w, pages)) orphans << keys.join(QLatin1Char('+'));
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
        if (!tagged.contains(k) && !untagged.contains(k)) untagged << k;
    QVERIFY2(untagged.isEmpty(),
             qPrintable(QStringLiteral("keys written with no tagged editor: %1")
                            .arg(untagged.join(QStringLiteral(", ")))));
}

/*! Test 4a — the 2D row follows the module toggle, greyed with a reason. */
void TestSimulationOptionsGates::twoDRowGate()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(),
                                QStringLiteral("6.0.0"), nullptr, nullptr);
    auto *cats = dlg.findChild<QListWidget *>(QStringLiteral("categories"));
    QVERIFY(cats);
    const int row = rowOf(cats, QStringLiteral("2D Surface Routing"));
    QVERIFY2(row >= 0, "no 2D Surface Routing row");

    auto *module2D = dlg.findChild<QCheckBox *>(QStringLiteral("module2DBox"));
    QVERIFY2(module2D, "the 2D module checkbox lost its objectName");

    module2D->setChecked(false);
    QVERIFY2(!rowEnabled(cats, row), "2D row stayed enabled with the module off");
    QVERIFY2(!cats->item(row)->toolTip().isEmpty(),
             "a gated row must say WHY it is off");
    module2D->setChecked(true);
    QVERIFY2(rowEnabled(cats, row), "2D row did not come back with the module on");
    QVERIFY(cats->item(row)->toolTip().isEmpty());
}

/*! Test 5 — turning off the page you are viewing must land you somewhere usable. */
void TestSimulationOptionsGates::rowRedirect()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(),
                                QStringLiteral("6.0.0"), nullptr, nullptr);
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
    QVERIFY2(cats->currentRow() != row, "left the user on a greyed page");
    QVERIFY2(rowEnabled(cats, cats->currentRow()),
             "redirected onto another disabled row");
}

/*! Test 4b — the Hydraulics tabs follow FLOW_ROUTING (the headline of the
 *  restructure: contextual activation instead of one 35-control column). */
void TestSimulationOptionsGates::hydraulicsTabGates()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(),
                                QStringLiteral("6.0.0"), nullptr, nullptr);
    auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("hydraulicsTabs"));
    QVERIFY2(tabs, "Routing & Hydraulics has no hydraulicsTabs");
    QCOMPARE(tabs->count(), 4);

    // Routing is every solver's tab and never gates off; Dynamic Wave is
    // DYNWAVE-only; Finite Volume is FV-only. Unsteady friction rides on an
    // engine capability probe, so it is asserted as "off under STEADY /
    // KINWAVE" rather than against a hardcoded expectation.
    struct Case { const char *routing; bool dw; bool fv; bool ufPossible; };
    const Case cases[] = {
        { "STEADY",  false, false, false },
        { "KINWAVE", false, false, false },
        { "DYNWAVE", true,  false, true  },
        { "FV",      false, true,  true  },
    };
    for (const Case &c : cases) {
        setRouting(dlg, c.routing);
        QVERIFY2(tabs->isTabEnabled(0),
                 qPrintable(QStringLiteral("Routing tab off under %1")
                                .arg(QLatin1String(c.routing))));
        QVERIFY2(tabs->isTabEnabled(1) == c.dw,
                 qPrintable(QStringLiteral("Dynamic Wave tab wrong under %1")
                                .arg(QLatin1String(c.routing))));
        // FV also needs the engine capability; a build without it keeps the
        // tab off, which is still correct.
        if (!c.fv)
            QVERIFY2(!tabs->isTabEnabled(2),
                     qPrintable(QStringLiteral("Finite Volume tab on under %1")
                                    .arg(QLatin1String(c.routing))));
        if (!c.ufPossible)
            QVERIFY2(!tabs->isTabEnabled(3),
                     qPrintable(QStringLiteral("Unsteady Friction tab on under %1")
                                    .arg(QLatin1String(c.routing))));
        // Whatever is showing must be reachable.
        QVERIFY(tabs->isTabEnabled(tabs->currentIndex()));
    }
}

/*! Test 4b — the Quality tabs follow QUALITY_SOLVER. */
void TestSimulationOptionsGates::qualityTabGates()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(),
                                QStringLiteral("6.0.0"), nullptr, nullptr);
    auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("qualityTabs"));
    QVERIFY2(tabs, "Quality & Transport has no qualityTabs");
    QCOMPARE(tabs->count(), 4);
    auto *solver = dlg.findChild<QComboBox *>(QStringLiteral("qualitySolverCombo"));
    QVERIFY2(solver, "the QUALITY_SOLVER header combo lost its objectName");
    if (!solver->isEnabled())
        QSKIP("this engine build has no quality/transport option surface");

    struct Case { const char *token; bool ard; bool lard; };
    const Case cases[] = {
        { "LEGACY",       false, false },
        { "EULERIAN_ARD", true,  false },
        { "LAGRANGIAN",   false, true  },
    };
    for (const Case &c : cases) {
        const int idx = solver->findData(QLatin1String(c.token));
        QVERIFY(idx >= 0);
        solver->setCurrentIndex(idx);
        QVERIFY2(tabs->isTabEnabled(0), "General tab must never gate off");
        QVERIFY2(tabs->isTabEnabled(1) == c.ard,
                 qPrintable(QStringLiteral("Eulerian ARD tab wrong under %1")
                                .arg(QLatin1String(c.token))));
        QVERIFY2(tabs->isTabEnabled(2) == c.lard,
                 qPrintable(QStringLiteral("Lagrangian tab wrong under %1")
                                .arg(QLatin1String(c.token))));
        QVERIFY2(tabs->isTabEnabled(3), "Reserved Species tab must never gate off");
        QVERIFY(tabs->isTabEnabled(tabs->currentIndex()));
    }
}

/*! Test 5 — switching FLOW_ROUTING while viewing the tab that just went away. */
void TestSimulationOptionsGates::tabRedirect()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(),
                                QStringLiteral("6.0.0"), nullptr, nullptr);
    auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("hydraulicsTabs"));
    QVERIFY(tabs);

    setRouting(dlg, "DYNWAVE");
    QVERIFY(tabs->isTabEnabled(1));
    tabs->setCurrentIndex(1);              // Dynamic Wave
    QCOMPARE(tabs->currentIndex(), 1);

    setRouting(dlg, "STEADY");             // Dynamic Wave goes away
    QVERIFY2(tabs->currentIndex() != 1, "left the user on a greyed tab");
    QVERIFY2(tabs->isTabEnabled(tabs->currentIndex()),
             "redirected onto another disabled tab");
}

/*! Test 7 — no page scrolls at 1280x800, on any of its tabs. The whole point
 *  of the restructure: at most ~13 controls per tab instead of one long
 *  column. Mesh and Files legitimately scroll their tables, not their pages. */
void TestSimulationOptionsGates::noScrollAt1280x800()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY2(layer, "mini_2d.inp failed to load");
    SimulationOptionsDialog dlg(layer->engine(), layer.get(),
                                QStringLiteral("6.0.0"), nullptr, nullptr);
    auto *cats  = dlg.findChild<QListWidget *>(QStringLiteral("categories"));
    auto *pages = dlg.findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY(cats && pages);

    dlg.resize(1280, 800);
    dlg.show();
    QTest::qWait(50);

    const QStringList tableDominated{
        QStringLiteral("Mesh"), QStringLiteral("Files / Output / Plugins"),
    };
    QStringList overflows;
    for (int r = 0; r < cats->count(); ++r) {
        const QString title = cats->item(r)->text();
        if (tableDominated.contains(title)) continue;
        cats->setCurrentRow(r);
        auto *sa = qobject_cast<QScrollArea *>(pages->widget(r));
        if (!sa) continue;
        QWidget *page = sa->widget();
        const QList<QTabWidget *> inner =
            page ? page->findChildren<QTabWidget *>() : QList<QTabWidget *>{};
        const int nTabs = inner.isEmpty() ? 1 : inner.first()->count();
        for (int t = 0; t < nTabs; ++t) {
            if (!inner.isEmpty()) inner.first()->setCurrentIndex(t);
            QCoreApplication::processEvents();
            QTest::qWait(10);
            if (sa->verticalScrollBar()->maximum() > 0) {
                overflows << (inner.isEmpty()
                    ? title
                    : QStringLiteral("%1 \u203A %2").arg(title,
                                        inner.first()->tabText(t)));
            }
        }
    }
    QVERIFY2(overflows.isEmpty(),
             qPrintable(QStringLiteral("page(s) still scroll at 1280x800: %1")
                            .arg(overflows.join(QStringLiteral(", ")))));
}

/*! Test 8 — sidebar rows in order, and every inner tab set, in order. */
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
        QStringLiteral("Title / Notes"),
        QStringLiteral("Models / Processes"),
        QStringLiteral("Dates & Times"),
        QStringLiteral("Routing & Hydraulics"),
        QStringLiteral("Quality & Transport"),
        QStringLiteral("System / Performance"),
        QStringLiteral("Spatial & CRS"),
        QStringLiteral("Mesh"),
        QStringLiteral("2D Surface Routing"),
        QStringLiteral("Files / Output / Plugins"),
    };
    QStringList actual;
    for (int i = 0; i < cats->count(); ++i) actual << cats->item(i)->text();
    QCOMPARE(actual, expected);

    // Every inner tab set, by objectName, against PLAN §2.
    const struct { const char *tabs; QStringList titles; } kTabSets[] = {
        { "modelsTabs", { QStringLiteral("Domains & Processes"),
                          QStringLiteral("Modules"),
                          QStringLiteral("Flags") } },
        { "datesTabs",  { QStringLiteral("Simulation Window"),
                          QStringLiteral("Time Steps"),
                          QStringLiteral("Events") } },
        { "hydraulicsTabs", { QStringLiteral("Routing"),
                              QStringLiteral("Dynamic Wave"),
                              QStringLiteral("Finite Volume"),
                              QStringLiteral("Unsteady Friction") } },
        { "qualityTabs", { QStringLiteral("General"),
                           QStringLiteral("Eulerian ARD"),
                           QStringLiteral("Lagrangian (LARD)"),
                           QStringLiteral("Reserved Species") } },
        // Five, not PLAN §2's four: §1.3 reserved a Processes tab after
        // Coupling and that content (U1/U5) now exists.
        { "twoDTabs",   { QStringLiteral("Hydrodynamics"),
                          QStringLiteral("Wetting & Drying"),
                          QStringLiteral("Coupling"),
                          QStringLiteral("Processes"),
                          QStringLiteral("Performance & Output") } },
    };
    for (const auto &set : kTabSets) {
        auto *tabs = dlg.findChild<QTabWidget *>(QLatin1String(set.tabs));
        QVERIFY2(tabs, qPrintable(QStringLiteral("no QTabWidget named %1")
                                      .arg(QLatin1String(set.tabs))));
        QStringList got;
        // QTabWidget returns the source label: && paints one literal &.
        // Compare the visible captions used by the expected list below.
        for (int i = 0; i < tabs->count(); ++i)
            got << tabs->tabText(i).replace(QStringLiteral("&&"), QStringLiteral("&"));
        QVERIFY2(got == set.titles,
                 qPrintable(QStringLiteral("%1: %2 != %3")
                                .arg(QLatin1String(set.tabs),
                                     got.join(QStringLiteral(" | ")),
                                     set.titles.join(QStringLiteral(" | ")))));
    }
}

QTEST_MAIN(TestSimulationOptionsGates)
#include "test_simulationoptions_gates.moc"

void TestSimulationOptionsGates::aquiferProcessInputsPreserveAutomaticAndConvertUnits()
{
    for(const char* units:{"CMS","CFS"}) {
        auto layer=openLayer(fixture("mini_2d.inp"));QVERIFY(layer);auto e=layer->engine();
        QVERIFY(swmm_options_set(e,"FLOW_UNITS",units)==SWMM_OK);
        QVERIFY(swmm_gw2d_process_options_set(e,"AUTO","DEFAULT","AUTO",0)==SWMM_OK);
        auto option=[&](const char* key){char b[80]{};swmm_gw2d_option_get(e,key,b,sizeof b);return QString::fromUtf8(b);};
        SimulationOptionsDialog dlg(e,layer.get(),"6.0.0",nullptr,nullptr);
        auto* et=dlg.findChild<QComboBox*>("gw2DEtCombo");auto* link=dlg.findChild<QComboBox*>("gwLinkSeepageCombo");auto* automatic=dlg.findChild<QCheckBox*>("gwWiltingAuto");auto* value=dlg.findChild<QDoubleSpinBox*>("gwWiltingSuction");QVERIFY(et&&link&&automatic&&value);
        QCOMPARE(et->currentData().toString(),QString("AUTO"));QCOMPARE(link->currentData().toString(),QString("DEFAULT"));QVERIFY(automatic->isChecked());
        const bool us=QString(units)=="CFS";QVERIFY(std::abs(value->value()-(us?150/.3048:150))<1e-8);QCOMPARE(value->suffix(),us?QString(" ft"):QString(" m"));
        const auto authored=option("OPTIONS_AUTHORED");QVERIFY(QMetaObject::invokeMethod(&dlg,"onApply",Qt::DirectConnection));QCOMPARE(option("WILTING_SUCTION"),QString("AUTO"));QCOMPARE(option("OPTIONS_AUTHORED"),authored);
        et->setCurrentIndex(et->findData("BOTH"));link->setCurrentIndex(link->findData("TWO_WAY"));automatic->setChecked(false);value->setValue(us?30/.3048:30);
        QVERIFY(QMetaObject::invokeMethod(&dlg,"onApply",Qt::DirectConnection));QVERIFY(std::abs(option("WILTING_SUCTION").toDouble()*(us?.3048:1)-30)<1e-8);QCOMPARE(option("GW_ET"),QString("BOTH"));QCOMPARE(option("LINK_SEEPAGE"),QString("TWO_WAY"));
        const auto custom=option("WILTING_SUCTION");SimulationOptionsDialog reopened(e,layer.get(),"6.0.0",nullptr,nullptr);QVERIFY(QMetaObject::invokeMethod(&reopened,"onApply",Qt::DirectConnection));QCOMPARE(option("WILTING_SUCTION"),custom);
    }
}

void TestSimulationOptionsGates::automaticEtFollowsAquiferMode()
{
    for(const char* mode:{"MESH","PER_SUBCATCH"}) {
        auto layer=openLayer(fixture("mini_2d.inp"));QVERIFY(layer);auto e=layer->engine();
        QCOMPARE(swmm_gw2d_row_add(e,0,nullptr,-1,1.,5.,.45,.1,2.),SWMM_OK);
        QCOMPARE(swmm_gw2d_option_set(e,"MODE",mode),SWMM_OK);
        QCOMPARE(swmm_gw2d_process_options_set(e,"AUTO","DEFAULT","AUTO",0),SWMM_OK);
        SimulationOptionsDialog dialog(e,layer.get(),"6.0.0",nullptr,nullptr);
        auto* enabled=dialog.findChild<QComboBox*>("gw2DEnableCombo");QVERIFY(enabled);enabled->setCurrentIndex(enabled->findData("YES"));
        auto* automatic=dialog.findChild<QCheckBox*>("gwWiltingAuto");QVERIFY(automatic);
        QCOMPARE(automatic->isEnabled(),QString(mode)=="MESH");
        enabled->setCurrentIndex(enabled->findData("NO"));QVERIFY(!automatic->isEnabled());
        char et[80]{};QCOMPARE(swmm_gw2d_option_get(e,"GW_ET",et,sizeof et),SWMM_OK);QCOMPARE(QString(et),QString("AUTO"));
    }
}

namespace {

/// Edits the next modal Climatology dialog with \a edit and accepts it.
void editNextClimatology(std::function<void(ClimatologyDialog&)> edit)
{
    auto* timer = new QTimer;
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, [timer, edit] {
        auto* dialog = qobject_cast<ClimatologyDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        timer->stop();
        timer->deleteLater();
        edit(*dialog);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    timer->start();
}

void editNextGroundwaterThermalSettings()
{
    auto* timer=new QTimer;
    auto* phase=new int(0);
    auto* attempts=new int(0);
    timer->setInterval(10);
    QObject::connect(timer,&QTimer::timeout,[timer,phase,attempts]{
        if(++*attempts>300) {
            QTest::qFail("Timed out waiting for the groundwater thermal settings dialog",__FILE__,__LINE__);
            if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->reject();
            timer->stop();timer->deleteLater();delete phase;delete attempts;return;
        }
        auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!modal)return;
        if(*phase==0) {
            auto* open=modal->findChild<QPushButton*>(QStringLiteral("hc_groundwaterSettings"));
            if(!open)return;
            *phase=1;
            open->click();
            return;
        }
        auto* thermal=modal->findChild<QComboBox*>(QStringLiteral("gwTransport_THERMAL_MODEL"));
        if(!thermal)return;
        timer->stop();
        timer->deleteLater();
        const int porous=thermal->findData(QStringLiteral("POROUS_EQUILIBRIUM"));
        if(porous>=0)thermal->setCurrentIndex(porous);
        if(auto* buttons=modal->findChild<QDialogButtonBox*>())
            if(auto* accept=buttons->button(QDialogButtonBox::Ok))accept->click();
        delete phase;delete attempts;
    });
    timer->start();
}

void editNextHeatConfiguration(bool enabled)
{
    auto* timer = new QTimer;
    auto* attempts = new int(0);
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, [timer, attempts, enabled] {
        if (++*attempts > 300) {
            QTest::qFail("Timed out waiting for the heat configuration dialog", __FILE__, __LINE__);
            if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();
            timer->stop();
            timer->deleteLater();
            delete attempts;
            return;
        }
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal || modal->objectName() != QStringLiteral("heatConfigDialog")) return;
        auto* heat = modal->findChild<QCheckBox*>(QStringLiteral("hc_heatEnabled"));
        auto* buttons = modal->findChild<QDialogButtonBox*>();
        if (!heat || !buttons || !buttons->button(QDialogButtonBox::Ok)) return;
        timer->stop();
        timer->deleteLater();
        heat->setChecked(enabled);
        buttons->button(QDialogButtonBox::Ok)->click();
        delete attempts;
    });
    timer->start();
}

double latitude(SWMM_Engine e) { double v = 0; swmm_climate_get_latitude(e, &v); return v; }

QString option(SWMM_Engine e, const char* key)
{
    char buf[256] = {};
    if (swmm_options_get(e, key, buf, sizeof buf) != SWMM_OK) swmm_options_get_ext(e, key, buf, sizeof buf);
    return QString::fromUtf8(buf);
}

} // namespace

/*! Closure audit C2: an accepted nested editor belongs to the outer dialog's
 *  Apply boundary, so Cancel reverts it and leaves no history entry. */
void TestSimulationOptionsGates::nestedCancelRevertsAcceptedEdits()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine e = layer->engine();
    const double lat0 = latitude(e);
    QUndoStack stack;
    SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    dlg.setUndoStack(&stack);
    editNextClimatology([lat0](ClimatologyDialog& c) {
        c.findChild<QDoubleSpinBox*>(QStringLiteral("clim_latitude"))->setValue(lat0 + 3);
    });
    emit dlg.findChild<openswmmvis::ui::ModelsPage*>()->showSnowSettingsRequested();
    QCOMPARE(latitude(e), lat0 + 3);
    dlg.reject();
    QCOMPARE(latitude(e), lat0);
    QCOMPARE(stack.count(), 0);
    QVERIFY(!stack.canRedo());
    QVERIFY(!dlg.wroteAnyChanges());
}

/*! One Apply = one Undo entry for the nested climate edit and the page edit;
 *  Undo and Redo move both; a later Cancel keeps what was applied. */
void TestSimulationOptionsGates::applyIsOneUndoEntryForNestedAndPageEdits()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine e = layer->engine();
    const double lat0 = latitude(e);
    const QString routing0 = option(e, "FLOW_ROUTING");
    const char* routing1 = routing0 == QLatin1String("KINWAVE") ? "DYNWAVE" : "KINWAVE";
    QUndoStack stack;
    SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    dlg.setUndoStack(&stack);
    editNextClimatology([lat0](ClimatologyDialog& c) {
        c.findChild<QDoubleSpinBox*>(QStringLiteral("clim_latitude"))->setValue(lat0 + 2);
    });
    emit dlg.findChild<openswmmvis::ui::ModelsPage*>()->showSnowSettingsRequested();
    setRouting(dlg, routing1);
    dlg.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.undoText(), QStringLiteral("Edit simulation options"));
    QCOMPARE(option(e, "FLOW_ROUTING"), QString::fromLatin1(routing1));
    QCOMPARE(latitude(e), lat0 + 2);
    dlg.reject();   // nothing open: the applied segment stays
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
void TestSimulationOptionsGates::modelCloseDuringNestedEditorInvalidatesWholeSession()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    QUndoStack stack;
    SimulationOptionsDialog dlg(layer->engine(), layer.get(),
                               QStringLiteral("6.0.0"), nullptr, nullptr);
    dlg.setUndoStack(&stack);

    bool closedDuringNestedEditor = false;
    QTimer closer;
    closer.setInterval(10);
    QObject::connect(&closer, &QTimer::timeout, [&] {
        auto *nested = qobject_cast<ClimatologyDialog *>(QApplication::activeModalWidget());
        if (!nested) return;
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
        for (double v : m) s += QString::number(v, 'g', 17) + ',';
        int type = -1;
        swmm_climate_get_evap_type(e, &type);
        unsigned flags = 0;
        swmm_heat_get_authoring_flags(e, &flags);
        return s + QString::number(type) + '|' + QString::number(flags) + '|' + QString::number(latitude(e), 'g', 17);
    };
    const QString before = state();
    OpenSWMMVis::ConfigSnapshot snapshot(swmm_config_snapshot_create(e));
    const QString identity = OpenSWMMVis::configurationIdentity(e, anchor, {});
    {
        ClimatologyDialog c(e, layer.get());
        c.setProjectAnchor(anchor);
        c.findChild<QDoubleSpinBox*>(QStringLiteral("clim_latitude"))->setValue(latitude(e) + 1.25);
        auto* type = c.findChild<QComboBox*>(QStringLiteral("clim_evapType"));
        type->setCurrentIndex(type->findData(0));
        c.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(c.wroteAnyChanges());
    }
    const QString after = state();
    QVERIFY(after != before);
    QStringList failures;
    QUndoStack stack;
    QVERIFY(OpenSWMMVis::pushConfigurationUndo(&stack, layer.get(), QStringLiteral("Edit climatology"),
        std::move(snapshot), identity, anchor, {}, [&failures](const QString& m) { failures << m; }));
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

void TestSimulationOptionsGates::groundwaterThermalConfigurationUndoTracksTransportKeys()
{
    auto layer=openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine engine=layer->engine();
    const QString anchor=QFileInfo(layer->modelFilePath()).absolutePath();
    const QStringList keys=openswmmvis::ui::Mesh2DGroundwaterDialog::configurationOptionKeys();
    const QString before=OpenSWMMVis::configurationIdentity(engine,anchor,keys);
    OpenSWMMVis::ConfigSnapshot snapshot(swmm_config_snapshot_create(engine));
    QVERIFY(snapshot);
    QCOMPARE(swmm_gw_transport_option_set(engine,"THERMAL_MODEL","POROUS_EQUILIBRIUM"),SWMM_OK);
    const QString after=OpenSWMMVis::configurationIdentity(engine,anchor,keys);
    QVERIFY(after!=before);
    QStringList failures;
    QUndoStack stack;
    QVERIFY(OpenSWMMVis::pushConfigurationUndo(&stack,layer.get(),QStringLiteral("Edit groundwater thermal settings"),
        std::move(snapshot),before,anchor,keys,[&failures](const QString& error){failures<<error;}));
    char model[80]{};
    QCOMPARE(swmm_gw_transport_option_get(engine,"THERMAL_MODEL",model,sizeof model),SWMM_OK);
    QCOMPARE(QString::fromUtf8(model),QStringLiteral("POROUS_EQUILIBRIUM"));
    stack.undo();
    QCOMPARE(swmm_gw_transport_option_get(engine,"THERMAL_MODEL",model,sizeof model),SWMM_OK);
    QCOMPARE(QString::fromUtf8(model),QStringLiteral("WATER_ONLY"));
    stack.redo();
    QCOMPARE(swmm_gw_transport_option_get(engine,"THERMAL_MODEL",model,sizeof model),SWMM_OK);
    QCOMPARE(QString::fromUtf8(model),QStringLiteral("POROUS_EQUILIBRIUM"));
    QCOMPARE(failures.size(),0);
}

void TestSimulationOptionsGates::groundwaterThermalShortcutJoinsOuterCancelUndoSegment()
{
    auto layer=openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine engine=layer->engine();
    char model[80]{};
    QCOMPARE(swmm_gw_transport_option_get(engine,"THERMAL_MODEL",model,sizeof model),SWMM_OK);
    QCOMPARE(QString::fromUtf8(model),QStringLiteral("WATER_ONLY"));
    QUndoStack stack;
    SimulationOptionsDialog dialog(engine,layer.get(),QStringLiteral("6.0.0"),nullptr,nullptr);
    dialog.setUndoStack(&stack);
    editNextGroundwaterThermalSettings();
    auto* quality=dialog.findChild<openswmmvis::ui::QualityPage*>();
    QVERIFY(quality);
    emit quality->showHeatSettingsRequested();
    QCOMPARE(swmm_gw_transport_option_get(engine,"THERMAL_MODEL",model,sizeof model),SWMM_OK);
    QCOMPARE(QString::fromUtf8(model),QStringLiteral("POROUS_EQUILIBRIUM"));
    dialog.reject();
    QCOMPARE(swmm_gw_transport_option_get(engine,"THERMAL_MODEL",model,sizeof model),SWMM_OK);
    QCOMPARE(QString::fromUtf8(model),QStringLiteral("WATER_ONLY"));
    QCOMPARE(stack.count(),0);
    QVERIFY(!dialog.wroteAnyChanges());
}

void TestSimulationOptionsGates::heatConfigurationSharesOuterUndoAndCancelSegment()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SWMM_Engine engine = layer->engine();
    int heatEnabled = -1;
    QCOMPARE(swmm_heat_get_enabled(engine, &heatEnabled), SWMM_OK);
    QCOMPARE(heatEnabled, 0);
    const QString routingBefore = option(engine, "FLOW_ROUTING");
    const char* routingAfter = routingBefore == QLatin1String("KINWAVE") ? "DYNWAVE" : "KINWAVE";

    QUndoStack stack;
    SimulationOptionsDialog applyDialog(engine, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    applyDialog.setUndoStack(&stack);
    auto* quality = applyDialog.findChild<openswmmvis::ui::QualityPage*>();
    QVERIFY(quality);
    editNextHeatConfiguration(true);
    emit quality->showHeatSettingsRequested();
    QCOMPARE(swmm_heat_get_enabled(engine, &heatEnabled), SWMM_OK);
    QCOMPARE(heatEnabled, 1);
    setRouting(applyDialog, routingAfter);
    applyDialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
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
    SimulationOptionsDialog cancelDialog(engine, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    cancelDialog.setUndoStack(&stack);
    quality = cancelDialog.findChild<openswmmvis::ui::QualityPage*>();
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

void TestSimulationOptionsGates::runtimeOverviewNavigatesToTwoDSettings()
{
    auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
    QVERIFY(layer);
    SimulationOptionsDialog dialog(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
    auto* quality = dialog.findChild<openswmmvis::ui::QualityPage*>();
    auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("categories"));
    auto* pages = dialog.findChild<QStackedWidget*>(QStringLiteral("pages"));
    QVERIFY(quality);
    QVERIFY(categories);
    QVERIFY(pages);
    QUndoStack stack;
    dialog.setUndoStack(&stack);

    QString failure;
    int phase = 0;
    int attempts = 0;
    auto* driver = new QTimer;
    driver->setInterval(10);
    QObject::connect(driver, &QTimer::timeout, [driver, &failure, &phase, &attempts, categories] {
        if (!failure.isEmpty()) {
            if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
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

        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (phase == 0) {
            if (!modal || modal->objectName() != QStringLiteral("heatConfigDialog")) return;
            auto* overview = modal->findChild<QPushButton*>(QStringLiteral("hc_runtimeOverview"));
            auto* heat = modal->findChild<QCheckBox*>(QStringLiteral("hc_heatEnabled"));
            if (!overview || !heat) {
                failure = QStringLiteral("Heat Configuration did not expose its editor and overview controls");
                return;
            }
            heat->setChecked(true);
            phase = 1;
            QTimer::singleShot(0, overview, [overview] { overview->click(); });
            return;
        }
        if (phase == 1) {
            if (!modal || modal->objectName() != QStringLiteral("hc_runtimeOverviewDialog")) return;
            auto* openTwoD = modal->findChild<QPushButton*>(QStringLiteral("hc_overviewOpenTwoDSettings"));
            if (!openTwoD) {
                failure = QStringLiteral("The runtime overview did not expose 2D navigation");
                return;
            }
            phase = 2;
            openTwoD->click();
        }
    });
    driver->start();
    emit quality->showHeatSettingsRequested();
    driver->stop();
    driver->deleteLater();

    int twoDRow = -1;
    for (int row = 0; row < categories->count(); ++row)
        if (categories->item(row)->text().contains(QStringLiteral("2D"))) { twoDRow = row; break; }
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
