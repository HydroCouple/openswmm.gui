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

#include <QListWidget>
#include <QObject>
#include <QScrollArea>
#include <QSet>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
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
    // Models / Processes — transport matrix hub
    "transportMatrixTable",
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
    void structure();
};

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

/*! T0 — sidebar rows, in order. Tab titles are added as pages gain tabs. */
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
}

QTEST_MAIN(TestSimulationOptionsGates)
#include "test_simulationoptions_gates.moc"
