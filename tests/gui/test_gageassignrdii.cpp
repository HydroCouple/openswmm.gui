/*!
 * \file   test_gageassignrdii.cpp
 * \brief  Rain-gage assignment for RDII inflows
 *         (workplans/RAINGAGE_ASSIGNMENT_PERF_AND_RDII_PLAN_2026-10-02.md §5).
 *
 * SWMM binds the gage to the unit-hydrograph GROUP. Fixture
 * tests/gui/data/gage_assign_rdii.inp: UH1 serves J1 + J2 (west, 30 ac) and
 * J3 (east, 40 ac); UH2 serves J4 (west). Every group starts on GN.
 *
 *   - Thiessen: UH2 -> GW; UH1 keeps the larger-area gage (GE) and a copy,
 *     UH1_GW, with identical rows takes J1 + J2.
 *   - A re-run changes nothing (no command at all).
 *   - Undo restores the [RDII] entries, the group gages, and removes the copy;
 *     redo reproduces the applied state.
 *   - A partial run (only J3 selected) must not move J1/J2: UH1 stays on GN
 *     and J3 moves to a copy.
 *   - Interpolated: nodes land on generated gages, undo restores everything.
 */

#include "assignment/raingageassignment.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"

#include <QDir>
#include <QTest>
#include <QUndoCommand>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_nodes.h>

namespace rg = openswmmvis::assignment::raingage;

namespace {

QString fixturePath()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(QStringLiteral("gage_assign_rdii.inp"));
}

/*! Sorted "node|group|area" for every [RDII] entry. */
QStringList rdiiTable(SWMM_Engine eng)
{
    QStringList out;
    char buf[256];
    for (int i = 0, n = swmm_rdii_count(eng); i < n; ++i) {
        int ni = -1;
        double a = 0;
        swmm_rdii_get(eng, i, &ni, buf, sizeof buf, &a);
        out << QStringLiteral("%1|%2|%3").arg(QString::fromUtf8(swmm_node_id(eng, ni)),
                                              QString::fromUtf8(buf)).arg(a);
    }
    out.sort();
    return out;
}

/*! group -> gage for every group with a gage line. */
QMap<QString, QString> groupGages(SWMM_Engine eng)
{
    QMap<QString, QString> out;
    char g[256], gg[256];
    for (int i = 0, n = swmm_hydrograph_gage_count(eng); i < n; ++i)
        if (swmm_hydrograph_get_gage(eng, i, g, sizeof g, gg, sizeof gg) == SWMM_OK)
            out.insert(QString::fromUtf8(g), QString::fromUtf8(gg));
    return out;
}

/*! Sorted "month|resp|r|t|k|dmax|drecov|dinit" rows of one group. */
QStringList groupRows(SWMM_Engine eng, const QString &group)
{
    QStringList out;
    char buf[256];
    for (int i = 0, n = swmm_hydrograph_count(eng); i < n; ++i) {
        int m = 0, resp = 0;
        double r, t, k, a, b, c;
        if (swmm_hydrograph_get(eng, i, buf, sizeof buf, &m, &resp, &r, &t, &k, &a, &b, &c)
                != SWMM_OK || QString::fromUtf8(buf) != group)
            continue;
        out << QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                   .arg(m).arg(resp).arg(r).arg(t).arg(k).arg(a).arg(b).arg(c);
    }
    out.sort();
    return out;
}

QStringList groupNames(SWMM_Engine eng)
{
    QStringList out;
    char buf[256];
    for (int i = 0, n = swmm_hydrograph_group_count(eng); i < n; ++i)
        if (swmm_hydrograph_group_id(eng, i, buf, sizeof buf) == SWMM_OK)
            out << QString::fromUtf8(buf);
    out.sort();
    return out;
}

struct Rig {
    MapCanvas       canvas;
    SWMMModelLayer *layer = nullptr;

    bool load()
    {
        layer = new SWMMModelLayer(fixturePath(), nullptr);
        QList<QString> w, e;
        if (!layer->loadModel(w, e)) return false;
        canvas.addLayer(layer, false);
        return true;
    }
    SWMM_Engine eng() const { return layer->engine(); }

    rg::Plan plan(rg::Options o)
    {
        return rg::computePlan(rg::gatherInput(layer, o));
    }
    /*! Plan + push; returns false when the plan produced no command. */
    bool run(rg::Options o)
    {
        const rg::Plan p = plan(o);
        if (!p.error.isEmpty()) { qWarning() << p.error; return false; }
        QUndoCommand *cmd = rg::makeApplyCommand(layer, &canvas, p, QStringLiteral("t"));
        if (!cmd) return false;
        canvas.undoStack()->push(cmd);
        return true;
    }
};

rg::Options rdiiOnly(rg::Method m = rg::Method::Nearest)
{
    rg::Options o;
    o.method = m;
    o.subcatchments = false;
    o.rdii = true;
    return o;
}

} // namespace

class TestGageAssignRdii : public QObject
{
    Q_OBJECT
private slots:
    void nearestSplitsSharedGroup();
    void rerunIsANoOp();
    void undoRedoRestoreExactly();
    void partialRunNeverMovesOutOfScopeNodes();
    void interpolatedUsesGeneratedGagesAndUndoes();
};

void TestGageAssignRdii::nearestSplitsSharedGroup()
{
    Rig rig;
    QVERIFY(rig.load());
    const QStringList uh1Rows = groupRows(rig.eng(), QStringLiteral("UH1"));
    QVERIFY(rig.run(rdiiOnly()));

    const QMap<QString, QString> gg = groupGages(rig.eng());
    QCOMPARE(gg.value(QStringLiteral("UH2")), QStringLiteral("GW"));
    QCOMPARE(gg.value(QStringLiteral("UH1")), QStringLiteral("GE"));   // 40 ac > 30 ac
    QCOMPARE(gg.value(QStringLiteral("UH1_GW")), QStringLiteral("GW"));
    QCOMPARE(groupRows(rig.eng(), QStringLiteral("UH1_GW")), uh1Rows);

    QCOMPARE(rdiiTable(rig.eng()),
             QStringList({QStringLiteral("J1|UH1_GW|10"), QStringLiteral("J2|UH1_GW|20"),
                          QStringLiteral("J3|UH1|40"), QStringLiteral("J4|UH2|5")}));
}

void TestGageAssignRdii::rerunIsANoOp()
{
    Rig rig;
    QVERIFY(rig.load());
    QVERIFY(rig.run(rdiiOnly()));
    const rg::Plan again = rig.plan(rdiiOnly());
    QVERIFY(again.error.isEmpty());
    QVERIFY(again.newGroups.isEmpty());
    QVERIFY(again.groupGages.isEmpty());
    for (const rg::RowPlan &r : again.rows)
        QVERIFY2(!r.changed, qPrintable(r.object));
    QVERIFY(!rg::makeApplyCommand(rig.layer, &rig.canvas, again, QStringLiteral("t")));
}

void TestGageAssignRdii::undoRedoRestoreExactly()
{
    Rig rig;
    QVERIFY(rig.load());
    const QStringList table0 = rdiiTable(rig.eng());
    const QMap<QString, QString> gages0 = groupGages(rig.eng());
    const QStringList groups0 = groupNames(rig.eng());

    QVERIFY(rig.run(rdiiOnly()));
    const QStringList table1 = rdiiTable(rig.eng());
    const QMap<QString, QString> gages1 = groupGages(rig.eng());
    QVERIFY(table1 != table0);

    rig.canvas.undoStack()->undo();
    QCOMPARE(rdiiTable(rig.eng()), table0);
    QCOMPARE(groupGages(rig.eng()), gages0);
    QCOMPARE(groupNames(rig.eng()), groups0);

    rig.canvas.undoStack()->redo();
    QCOMPARE(rdiiTable(rig.eng()), table1);
    QCOMPARE(groupGages(rig.eng()), gages1);
}

void TestGageAssignRdii::partialRunNeverMovesOutOfScopeNodes()
{
    Rig rig;
    QVERIFY(rig.load());
    rg::Options o = rdiiOnly();
    o.selectedOnly = true;
    o.selectedNodes = {QStringLiteral("J3")};
    QVERIFY(rig.run(o));

    const QMap<QString, QString> gg = groupGages(rig.eng());
    QCOMPARE(gg.value(QStringLiteral("UH1")), QStringLiteral("GN"));   // pinned by J1, J2
    QCOMPARE(gg.value(QStringLiteral("UH1_GE")), QStringLiteral("GE"));
    QCOMPARE(gg.value(QStringLiteral("UH2")), QStringLiteral("GN"));   // J4 not selected
    QCOMPARE(rdiiTable(rig.eng()),
             QStringList({QStringLiteral("J1|UH1|10"), QStringLiteral("J2|UH1|20"),
                          QStringLiteral("J3|UH1_GE|40"), QStringLiteral("J4|UH2|5")}));
}

void TestGageAssignRdii::interpolatedUsesGeneratedGagesAndUndoes()
{
    Rig rig;
    QVERIFY(rig.load());
    const QStringList table0 = rdiiTable(rig.eng());
    const QMap<QString, QString> gages0 = groupGages(rig.eng());
    const int gageCount0 = swmm_gage_count(rig.eng());

    QVERIFY(rig.run(rdiiOnly(rg::Method::Interpolated)));
    const QMap<QString, QString> gg = groupGages(rig.eng());
    // Every group that now has nodes points at a generated gage.
    for (const QString &row : rdiiTable(rig.eng())) {
        const QString group = row.section(QLatin1Char('|'), 1, 1);
        QVERIFY2(gg.value(group).startsWith(QStringLiteral("NNG_")), qPrintable(row));
    }
    QVERIFY(swmm_gage_count(rig.eng()) > gageCount0);

    rig.canvas.undoStack()->undo();
    QCOMPARE(rdiiTable(rig.eng()), table0);
    QCOMPARE(groupGages(rig.eng()), gages0);
    QCOMPARE(swmm_gage_count(rig.eng()), gageCount0);
}

QTEST_MAIN(TestGageAssignRdii)
#include "test_gageassignrdii.moc"
