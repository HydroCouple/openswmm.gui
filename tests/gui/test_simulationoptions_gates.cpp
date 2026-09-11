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

#include <QComboBox>
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
    void reachability();
    void twoDRowGate();
    void rowRedirect();
    void hydraulicsTabGates();
    void qualityTabGates();
    void tabRedirect();
    void noScrollAt1280x800();
    void structure();
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

/*! T0 — the names exist and are parented into the page stack. */
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
        for (int i = 0; i < tabs->count(); ++i) got << tabs->tabText(i);
        QVERIFY2(got == set.titles,
                 qPrintable(QStringLiteral("%1: %2 != %3")
                                .arg(QLatin1String(set.tabs),
                                     got.join(QStringLiteral(" | ")),
                                     set.titles.join(QStringLiteral(" | ")))));
    }
}

QTEST_MAIN(TestSimulationOptionsGates)
#include "test_simulationoptions_gates.moc"
