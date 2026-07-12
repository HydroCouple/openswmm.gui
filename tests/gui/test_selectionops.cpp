/*!
 * \file   test_selectionops.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Coverage for the three toolbar selection features:
 *
 *           1. SelectionOps::invert  — category-scoped Invert Selection.
 *           2. SelectionOps::trace   — Select Upstream / Downstream, with
 *                                      subcatchment outlet chaining.
 *           3. AttributeTablePanel::selectionAsTsv — Copy in the Attribute
 *                                      Table.
 *
 *         Fixtures:
 *           - `selection_trace_fixture.inp` — see its [TITLE] block for the
 *             topology. S3 drains into S2 (subcatchment → subcatchment), which
 *             is what the chaining assertions hang on.
 *           - `typed_selection_fixture.inp` — reused for the name-collision
 *             case (a rain gage and a subcatchment both named "S1"; a junction
 *             and a conduit both named "X1"), proving invert's scope test keys
 *             on the typed ref rather than the bare name.
 *
 *         Each slot opens its own layer so slots stay order-independent.
 */
#include "layers/swmmmodellayer.h"
#include "selection/selectionmanager.h"
#include "selection/selectionops.h"
#include "ui/panels/attributetablepanel.h"

#include <QDir>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

#include <algorithm>
#include <memory>

#include "plot/profilenetworkadapter.h"
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

std::unique_ptr<SWMMModelLayer> openLayer(const QString &fixture)
{
    auto layer = std::make_unique<SWMMModelLayer>(QDir(dataDir()).filePath(fixture),
                                                  nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

SWMMObjectRef node(const QString &n) { return {SWMMObjectRef::Node, n}; }
SWMMObjectRef link(const QString &n) { return {SWMMObjectRef::Link, n}; }
SWMMObjectRef sub (const QString &n) { return {SWMMObjectRef::Subcatchment, n}; }
SWMMObjectRef gage(const QString &n) { return {SWMMObjectRef::RainGage, n}; }

//! Sorted "Type:Name" strings — readable diffs on failure, and order-stable
//! unlike QSet iteration.
QStringList spell(const QSet<SWMMObjectRef> &refs)
{
    QStringList out;
    for (const SWMMObjectRef &r : refs) {
        QString t;
        switch (r.objectType) {
        case SWMMObjectRef::Node:         t = QStringLiteral("N");    break;
        case SWMMObjectRef::Link:         t = QStringLiteral("L");    break;
        case SWMMObjectRef::Subcatchment: t = QStringLiteral("S");    break;
        case SWMMObjectRef::RainGage:     t = QStringLiteral("G");    break;
        case SWMMObjectRef::MeshCell:     t = QStringLiteral("MESH"); break;
        default:                          t = QStringLiteral("?");    break;
        }
        out << t + QLatin1Char(':') + r.name;
    }
    out.sort();
    return out;
}

} // namespace

class TestSelectionOps : public QObject
{
    Q_OBJECT

private slots:
    // ── Invert ─────────────────────────────────────────────────────────────
    void invertScopesToHomogeneousCategory();
    void invertAcrossAllWhenMixed();
    void invertEmptySelectsEverything();
    void invertPassesMeshRefsThrough();
    void invertIsNameCollisionSafe();

    // ── Trace ──────────────────────────────────────────────────────────────
    void traceUpstreamFromOutfallTakesWholeNetwork();
    void traceUpstreamChainsSubcatchments();
    void traceUpstreamFromLinkSeedsBothEnds();
    void traceDownstreamFromSubcatchFollowsOutletChain();
    void traceDownstreamFromNodeTakesNoSubcatchments();
    void traceTraversesEveryNodeAndLinkKind();
    void traceWithoutSeedsReportsNoSeeds();

    // ── Attribute-table copy ───────────────────────────────────────────────
    void tsvCopiesSelectedRowsWithHeader();
    void tsvFallsBackToAllVisibleRows();

    // ── External-model probe (env-gated; skips in CI) ──────────────────────
    void probeExternalModelTrace();
};

// ---------------------------------------------------------------------------
// Invert
// ---------------------------------------------------------------------------

void TestSelectionOps::invertScopesToHomogeneousCategory()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    // Two junctions selected → homogeneous → invert stays inside Junctions.
    const QSet<SWMMObjectRef> cur{node("J1"), node("J2")};
    const QStringList got = spell(SelectionOps::invert(layer.get(), cur));
    QCOMPARE(got, (QStringList{"N:J3", "N:J4"}));
}

void TestSelectionOps::invertAcrossAllWhenMixed()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    // A junction + an outfall are two DIFFERENT categories (fine-grained
    // scope), so this is mixed and inverts across all eleven categories.
    const QSet<SWMMObjectRef> cur{node("J1"), node("O1")};
    const QStringList got = spell(SelectionOps::invert(layer.get(), cur));

    // 14 objects in the fixture, minus the 2 selected.
    QCOMPARE(got, (QStringList{"G:RG1", "L:C1", "L:C2", "L:C3", "L:P1",
                               "N:J2", "N:J3", "N:J4",
                               "S:S1", "S:S2", "S:S3", "S:S4"}));
}

void TestSelectionOps::invertEmptySelectsEverything()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    const QSet<SWMMObjectRef> got = SelectionOps::invert(layer.get(), {});
    QCOMPARE(got.size(), 14);
    QVERIFY(got.contains(node("J1")));
    QVERIFY(got.contains(link("P1")));
    QVERIFY(got.contains(sub("S3")));
    QVERIFY(got.contains(gage("RG1")));
}

void TestSelectionOps::invertPassesMeshRefsThrough()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    // 2D elements are outside invert's universe: they must survive untouched
    // AND must not make the (otherwise junction-only) selection look mixed.
    const SWMMObjectRef mesh(SWMMObjectRef::MeshCell, QStringLiteral("mesh1:42"));
    const QSet<SWMMObjectRef> cur{node("J1"), mesh};

    const QStringList got = spell(SelectionOps::invert(layer.get(), cur));
    QCOMPARE(got, (QStringList{"MESH:mesh1:42", "N:J2", "N:J3", "N:J4"}));
}

void TestSelectionOps::invertIsNameCollisionSafe()
{
    auto layer = openLayer(QStringLiteral("typed_selection_fixture.inp"));
    QVERIFY(layer);

    // The fixture has a junction "X1" AND a conduit "X1". Selecting the
    // junction must scope invert to Junctions — the identically-named conduit
    // must not be mistaken for part of the selection, nor drag Conduits in.
    const QSet<SWMMObjectRef> got =
        SelectionOps::invert(layer.get(), {node("X1")});
    QCOMPARE(spell(got), (QStringList{"N:J1"}));

    // Same for the gage "S1" vs the subcatchment "S1": inverting the gage
    // yields nothing (it is the only gage), and leaves the subcatchment alone.
    const QSet<SWMMObjectRef> gotGage =
        SelectionOps::invert(layer.get(), {gage("S1")});
    QVERIFY(gotGage.isEmpty());
}

// ---------------------------------------------------------------------------
// Trace
// ---------------------------------------------------------------------------

void TestSelectionOps::traceUpstreamFromOutfallTakesWholeNetwork()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    const auto res = SelectionOps::trace(layer.get(), {node("O1")}, /*upstream=*/true);
    QVERIFY(!res.noSeeds);
    QCOMPARE(res.nodeCount,     5);   // O1 J3 J2 J1 J4
    QCOMPARE(res.linkCount,     4);   // C1 C2 C3 P1
    QCOMPARE(res.subcatchCount, 4);   // S1 S2 S3 S4
    QCOMPARE(spell(res.refs),
             (QStringList{"L:C1", "L:C2", "L:C3", "L:P1",
                          "N:J1", "N:J2", "N:J3", "N:J4", "N:O1",
                          "S:S1", "S:S2", "S:S3", "S:S4"}));
}

void TestSelectionOps::traceUpstreamChainsSubcatchments()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    // Upstream of J2: J1 via C1. S2 drains to J2, and S3 drains into S2 — the
    // transitive subcatchment→subcatchment hop is the assertion here. S4 (on
    // the J4 pump branch) and J3/O1 (downstream) must stay out.
    const auto res = SelectionOps::trace(layer.get(), {node("J2")}, /*upstream=*/true);
    QCOMPARE(spell(res.refs),
             (QStringList{"L:C1", "N:J1", "N:J2", "S:S1", "S:S2", "S:S3"}));
}

void TestSelectionOps::traceUpstreamFromLinkSeedsBothEnds()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    // C2 spans J2→J3, so both endpoints seed. Upstream of J3 reaches the pump
    // branch (J4/P1/S4); upstream of J2 reaches J1/C1/S1/S2/S3.
    const auto res = SelectionOps::trace(layer.get(), {link("C2")}, /*upstream=*/true);
    QCOMPARE(spell(res.refs),
             (QStringList{"L:C1", "L:C2", "L:P1",
                          "N:J1", "N:J2", "N:J3", "N:J4",
                          "S:S1", "S:S2", "S:S3", "S:S4"}));
}

void TestSelectionOps::traceDownstreamFromSubcatchFollowsOutletChain()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    // S3 → S2 → J2 → (C2) J3 → (C3) O1. The trace must walk the subcatchment
    // outlet chain, then hand off to the link graph at J2. S1/S4 and the
    // upstream J1/J4 must NOT appear.
    const auto res = SelectionOps::trace(layer.get(), {sub("S3")}, /*upstream=*/false);
    QVERIFY(!res.noSeeds);
    QCOMPARE(spell(res.refs),
             (QStringList{"L:C2", "L:C3", "N:J2", "N:J3", "N:O1", "S:S2", "S:S3"}));
}

void TestSelectionOps::traceDownstreamFromNodeTakesNoSubcatchments()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    // Drainage runs subcatchment → node, never the reverse: a subcatchment is
    // always UPSTREAM of its outlet. So a downstream trace from a node picks
    // up no subcatchments at all — including S1, which drains into the seed.
    const auto res = SelectionOps::trace(layer.get(), {node("J1")}, /*upstream=*/false);
    QCOMPARE(res.subcatchCount, 0);
    QCOMPARE(spell(res.refs),
             (QStringList{"L:C1", "L:C2", "L:C3", "N:J1", "N:J2", "N:J3", "N:O1"}));
}

void TestSelectionOps::traceTraversesEveryNodeAndLinkKind()
{
    // selection_trace_alltypes_fixture.inp chains every node kind (junction,
    // storage, divider, outfall) through every link kind (conduit, orifice,
    // weir, outlet, pump):
    //   J1 -C1-> J2 -OR1-> ST1 -W1-> DV1 -{OT1,C2}-> J3 -P1-> O1
    // with S1->J1, S2->ST1, S3->S2, S4->DV1. A trace must not stop at any of
    // them — the user-visible requirement is "select every link and node
    // object type in the graph, subcatchments included".
    auto layer = openLayer(QStringLiteral("selection_trace_alltypes_fixture.inp"));
    QVERIFY(layer);

    const auto up = SelectionOps::trace(layer.get(), {node("O1")}, /*upstream=*/true);
    QVERIFY(!up.noSeeds);
    QCOMPARE(spell(up.refs),
             (QStringList{"L:C1", "L:C2", "L:OR1", "L:OT1", "L:P1", "L:W1",
                          "N:DV1", "N:J1", "N:J2", "N:J3", "N:O1", "N:ST1",
                          "S:S1", "S:S2", "S:S3", "S:S4"}));

    // And the mirror image: downstream from the head junction crosses the
    // orifice, storage, weir, divider, outlet, pump chain to the outfall.
    const auto down = SelectionOps::trace(layer.get(), {node("J1")}, /*upstream=*/false);
    QVERIFY(!down.noSeeds);
    QCOMPARE(spell(down.refs),
             (QStringList{"L:C1", "L:C2", "L:OR1", "L:OT1", "L:P1", "L:W1",
                          "N:DV1", "N:J1", "N:J2", "N:J3", "N:O1", "N:ST1"}));
}

void TestSelectionOps::traceWithoutSeedsReportsNoSeeds()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    // Empty selection, and a selection holding only untraceable refs (a rain
    // gage, a mesh cell), both report "nothing to trace" rather than silently
    // selecting the world.
    QVERIFY(SelectionOps::trace(layer.get(), {}, true).noSeeds);

    const SWMMObjectRef mesh(SWMMObjectRef::MeshCell, QStringLiteral("mesh1:7"));
    const auto res = SelectionOps::trace(layer.get(), {gage("RG1"), mesh}, false);
    QVERIFY(res.noSeeds);
    QVERIFY(res.refs.isEmpty());
}

// ---------------------------------------------------------------------------
// Attribute-table copy
// ---------------------------------------------------------------------------

void TestSelectionOps::tsvCopiesSelectedRowsWithHeader()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    SelectionManager selMgr;
    AttributeTablePanel panel;
    panel.setProject(layer.get(), &selMgr, nullptr);

    // Default category is the first non-empty one — Junctions.
    selMgr.select(QSet<SWMMObjectRef>{node("J1"), node("J3")},
                  SelectionManager::Replace);

    const QStringList lines = panel.selectionAsTsv().split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 3);                       // header + 2 selected rows
    QVERIFY(lines.at(0).startsWith(QStringLiteral("Name\t")));
    // Rows copy in on-screen order, and the view restores the user's
    // persisted per-category sort from QSettings (restoreColumnWidths →
    // QHeaderView::restoreState carries the sort indicator) — so the order
    // here is host-dependent. Assert membership, not order.
    QStringList names{lines.at(1).section(QLatin1Char('\t'), 0, 0),
                      lines.at(2).section(QLatin1Char('\t'), 0, 0)};
    std::sort(names.begin(), names.end());
    QCOMPARE(names, (QStringList{QStringLiteral("J1"), QStringLiteral("J3")}));

    // Every line carries the same column count — the paste lands aligned.
    const int cols = lines.at(0).count(QLatin1Char('\t'));
    QVERIFY(cols > 0);
    QCOMPARE(lines.at(1).count(QLatin1Char('\t')), cols);
    QCOMPARE(lines.at(2).count(QLatin1Char('\t')), cols);
}

void TestSelectionOps::tsvFallsBackToAllVisibleRows()
{
    auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
    QVERIFY(layer);

    SelectionManager selMgr;
    AttributeTablePanel panel;
    panel.setProject(layer.get(), &selMgr, nullptr);

    // Nothing selected → copy the whole visible table (4 junctions + header).
    const QStringList lines = panel.selectionAsTsv().split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 5);
    QVERIFY(lines.at(0).startsWith(QStringLiteral("Name\t")));
}

// Diagnostic probe against a real model, mirroring test_asyncload's
// profileExternalModel: gated on SWMM_TRACE_PROBE_INP so it skips in CI.
// Seeds every outfall and traces upstream — on a fully-connected drainage
// network that should visit (nearly) everything, so the per-category
// coverage report pinpoints which object types a trace fails to cross.
//
//   QT_QPA_PLATFORM=offscreen SWMM_TRACE_PROBE_INP="/path/model.inp" \
//     ./build/tests/gui/test_selectionops probeExternalModelTrace
void TestSelectionOps::probeExternalModelTrace()
{
    const QString path = qEnvironmentVariable("SWMM_TRACE_PROBE_INP");
    if (path.isEmpty())
        QSKIP("Set SWMM_TRACE_PROBE_INP=<model.inp> to run the external trace probe.");

    auto layer = std::make_unique<SWMMModelLayer>(path, nullptr);
    QList<QString> warnings, errors;
    QVERIFY2(layer->loadModel(warnings, errors),
             qPrintable(QStringLiteral("loadModel failed: %1")
                            .arg(errors.join(QStringLiteral(" | ")))));

    QSet<SWMMObjectRef> seeds;
    const int outfalls = layer->categoryCount(SWMMModelLayer::CatOutfalls);
    for (int i = 0; i < outfalls; ++i)
        seeds.insert(SWMMObjectRef(SWMMObjectRef::Node,
                                   layer->objectNameAt(SWMMModelLayer::CatOutfalls, i)));
    QVERIFY(!seeds.isEmpty());

    const auto res = SelectionOps::trace(layer.get(), seeds, /*upstream=*/true);
    QVERIFY(!res.noSeeds);
    qWarning().noquote() << QStringLiteral(
        "[trace-probe] upstream from %1 outfall(s): nodes=%2 links=%3 subcatch=%4")
        .arg(outfalls).arg(res.nodeCount).arg(res.linkCount).arg(res.subcatchCount);

    // Graph diagnostics — which engine links never made it into the BFS
    // graph, can a trace cross a pump, and how connected is the network
    // ignoring direction? Separates "trace drops a TYPE" (a bug) from
    // "the model's directed flow paths end elsewhere" (correct semantics).
    {
        SWMM_Engine eng = layer->engine();
        const int linkCount = swmm_link_count(eng);
        const int nodeCount = swmm_node_count(eng);
        const auto g = ProfileNetworkAdapter::buildGraphFromModel(layer.get());
        QSet<int> inGraph;
        for (const auto &e2 : g.edges) inGraph.insert(e2.linkId);
        int shown = 0;
        QHash<int,int> missByType, totByType;
        for (int i = 0; i < linkCount; ++i) {
            int type = -9; swmm_link_get_type(eng, i, &type);
            ++totByType[type];
            if (inGraph.contains(i)) continue;
            ++missByType[type];
            if (shown < 12) {
                int from = -1, to = -1;
                const int rcF = swmm_link_get_from_node(eng, i, &from);
                const int rcT = swmm_link_get_to_node(eng, i, &to);
                qWarning().noquote() << QStringLiteral(
                    "[trace-probe]   DROPPED link %1 '%2' type=%3 from=%4(rc %5) to=%6(rc %7) nodeCount=%8")
                    .arg(i).arg(QString::fromUtf8(swmm_link_id(eng, i)))
                    .arg(type).arg(from).arg(rcF).arg(to).arg(rcT).arg(nodeCount);
                ++shown;
            }
        }
        for (auto it = totByType.cbegin(); it != totByType.cend(); ++it)
            qWarning().noquote() << QStringLiteral(
                "[trace-probe]   graph coverage type %1: %2/%3 in graph")
                .arg(it.key()).arg(it.value() - missByType.value(it.key())).arg(it.value());

        // Can a trace CROSS a pump at all? Trace upstream from each pump's
        // to-node and check the pump + its from-node land in the result.
        int pumpsChecked = 0;
        for (int i = 0; i < linkCount && pumpsChecked < 3; ++i) {
            int type = -9; swmm_link_get_type(eng, i, &type);
            if (type != 1) continue;   // pumps
            ++pumpsChecked;
            int from = -1, to = -1;
            swmm_link_get_from_node(eng, i, &from);
            swmm_link_get_to_node(eng, i, &to);
            const QString pumpName = QString::fromUtf8(swmm_link_id(eng, i));
            const QString fromName = QString::fromUtf8(swmm_node_id(eng, from));
            const QString toName   = QString::fromUtf8(swmm_node_id(eng, to));
            const auto up = SelectionOps::trace(
                layer.get(), {SWMMObjectRef(SWMMObjectRef::Node, toName)}, true);
            qWarning().noquote() << QStringLiteral(
                "[trace-probe]   pump %1 (%2 -> %3): upstream-from-toNode crosses it? link=%4 fromNode=%5")
                .arg(pumpName, fromName, toName)
                .arg(up.refs.contains(SWMMObjectRef(SWMMObjectRef::Link, pumpName)))
                .arg(up.refs.contains(SWMMObjectRef(SWMMObjectRef::Node, fromName)));
        }

        // Undirected reachability from the outfall seeds — how much of the
        // node universe is even CONNECTED to an outfall by 1D links?
        QHash<int, QVector<int>> undirected;
        for (const auto &e2 : g.edges) {
            undirected[e2.fromNode].push_back(e2.toNode);
            undirected[e2.toNode].push_back(e2.fromNode);
        }
        QSet<int> reach;
        QList<int> q;
        for (const SWMMObjectRef &s : seeds) {
            const int idx = swmm_node_index(eng, s.name.toUtf8().constData());
            if (idx >= 0) { reach.insert(idx); q.push_back(idx); }
        }
        while (!q.isEmpty()) {
            const int n = q.takeFirst();
            for (int nx : undirected.value(n))
                if (!reach.contains(nx)) { reach.insert(nx); q.push_back(nx); }
        }
        qWarning().noquote() << QStringLiteral(
            "[trace-probe]   undirected reachability from outfalls: %1/%2 nodes")
            .arg(reach.size()).arg(nodeCount);
    }

    struct CatSpec { SWMMModelLayer::Category cat; SWMMObjectRef::ObjectType type;
                     const char *name; };
    const CatSpec cats[] = {
        { SWMMModelLayer::CatJunctions,     SWMMObjectRef::Node,         "junctions" },
        { SWMMModelLayer::CatOutfalls,      SWMMObjectRef::Node,         "outfalls" },
        { SWMMModelLayer::CatStorage,       SWMMObjectRef::Node,         "storage" },
        { SWMMModelLayer::CatDividers,      SWMMObjectRef::Node,         "dividers" },
        { SWMMModelLayer::CatConduits,      SWMMObjectRef::Link,         "conduits" },
        { SWMMModelLayer::CatPumps,         SWMMObjectRef::Link,         "pumps" },
        { SWMMModelLayer::CatOrifices,      SWMMObjectRef::Link,         "orifices" },
        { SWMMModelLayer::CatWeirs,         SWMMObjectRef::Link,         "weirs" },
        { SWMMModelLayer::CatOutlets,       SWMMObjectRef::Link,         "outlets" },
        { SWMMModelLayer::CatSubcatchments, SWMMObjectRef::Subcatchment, "subcatchments" },
    };
    for (const CatSpec &c : cats) {
        const int total = layer->categoryCount(c.cat);
        if (total == 0) continue;
        int hit = 0;
        QStringList missed;
        for (int i = 0; i < total; ++i) {
            const QString name = layer->objectNameAt(c.cat, i);
            if (res.refs.contains(SWMMObjectRef(c.type, name)))
                ++hit;
            else if (missed.size() < 8)
                missed << name;
        }
        qWarning().noquote() << QStringLiteral("[trace-probe]   %1: %2/%3%4")
            .arg(QLatin1String(c.name)).arg(hit).arg(total)
            .arg(hit == total ? QString()
                              : QStringLiteral("  missed e.g. %1")
                                    .arg(missed.join(QStringLiteral(", "))));
    }
}

QTEST_MAIN(TestSelectionOps)
#include "test_selectionops.moc"
