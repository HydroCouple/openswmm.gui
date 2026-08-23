/*!
 * \file   test_linkflip.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Coverage for SWMMModelLayer::applyLinkFlip and FlipLinkCommand.
 *
 *         A flip reverses a link's topology only: the upstream node becomes
 *         the downstream node and the physical link does not move. Three
 *         things make that harder than swapping node1/node2:
 *
 *         1. Interior vertices are stored upstream → downstream
 *            (swmm_spatial_get_link_vertices prepends the from-node coord and
 *            appends the to-node coord). Swap the endpoints without reversing
 *            them and the drawn polyline zig-zags — with no error anywhere,
 *            because both the engine and the caches remain internally
 *            consistent. flipReversesInteriorVertices() is the load-bearing
 *            case here.
 *
 *         2. A conduit's offsets and entry/exit loss coefficients are attached
 *            to a *specific end*. Leaving them on their slots silently moves
 *            the invert profile, so they are swapped with the endpoints.
 *
 *         3. Orifices/weirs/outlets carry a single crest offset in offset_up
 *            and never use offset_dn, so for them nothing is swapped — a swap
 *            would move the real value into a dead slot.
 *
 *         Fixture: selection_trace_fixture.inp (see its [TITLE] for topology).
 *           Nodes  J1 J2 J3 J4 (junctions), O1 (outfall)
 *           Links  C1 J1->J2, C2 J2->J3, C3 J3->O1, P1 J4->J3 (pump)
 *         The fixture carries no vertices, offsets or losses, so the tests
 *         seed those through the C API before flipping.
 */
#include "layers/swmmmodellayer.h"
#include "map/mapundostack.h"

#include <QDir>
#include <QObject>
#include <QPointF>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QVector>

#include <memory>

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

std::unique_ptr<SWMMModelLayer> openLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(
        QDir(dataDir()).filePath(QStringLiteral("selection_trace_fixture.inp")),
        nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

//! Endpoint node ids straight from the engine — the oracle the cached SoA
//! indices are checked against.
QPair<QString, QString> engineEndpoints(SWMM_Engine eng, int linkIdx)
{
    int from = -1, to = -1;
    swmm_link_get_from_node(eng, linkIdx, &from);
    swmm_link_get_to_node(eng, linkIdx, &to);
    return {QString::fromUtf8(swmm_node_id(eng, from)),
            QString::fromUtf8(swmm_node_id(eng, to))};
}

//! Interior vertices as the engine holds them (its get API wraps the endpoint
//! coords, so the first and last entries are stripped).
QVector<QPointF> engineInterior(SWMM_Engine eng, int linkIdx)
{
    int n = 0;
    if (swmm_spatial_get_link_vertex_count(eng, linkIdx, &n) != SWMM_OK || n <= 2)
        return {};
    QVector<double> xs(n), ys(n);
    if (swmm_spatial_get_link_vertices(eng, linkIdx, xs.data(), ys.data(), n) != SWMM_OK)
        return {};
    QVector<QPointF> interior;
    for (int i = 1; i < n - 1; ++i)
        interior.append(QPointF(xs[i], ys[i]));
    return interior;
}

} // namespace

class TestLinkFlip : public QObject
{
    Q_OBJECT

private slots:

    // ── Topology ───────────────────────────────────────────────────────────

    //! The request itself: J1->J2 becomes J2->J1, in the engine and in the
    //! layer's cached endpoint indices (which the renderer draws from).
    void flipSwapsEndpoints()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        const int idx = layer->linkIndex(QStringLiteral("C1"));
        QVERIFY(idx >= 0);

        const int j1 = swmm_node_index(eng, "J1");
        const int j2 = swmm_node_index(eng, "J2");
        QCOMPARE(engineEndpoints(eng, idx), qMakePair(QStringLiteral("J1"),
                                                      QStringLiteral("J2")));
        QCOMPARE(layer->linkFromNodeIdx(idx), j1);
        QCOMPARE(layer->linkToNodeIdx(idx), j2);

        QVERIFY(layer->applyLinkFlip(idx));

        QCOMPARE(engineEndpoints(eng, idx), qMakePair(QStringLiteral("J2"),
                                                      QStringLiteral("J1")));
        QCOMPARE(layer->linkFromNodeIdx(idx), j2);
        QCOMPARE(layer->linkToNodeIdx(idx), j1);
    }

    //! The load-bearing case. Interior vertices are direction-ordered, so a
    //! flip must reverse them: the drawn polyline has to come out as the exact
    //! reverse of what it was, not a zig-zag between the swapped endpoints.
    void flipReversesInteriorVertices()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        const int idx = layer->linkIndex(QStringLiteral("C1"));
        QVERIFY(idx >= 0);

        // C1 runs J1 (0,0) -> J2 (1000,0); bend it away from that straight
        // line so a reversal is distinguishable from doing nothing.
        const QVector<QPointF> bends{QPointF(300.0, 200.0), QPointF(700.0, -150.0)};
        QVERIFY(layer->applyLinkInteriorVertices(idx, bends));

        const QVector<QPointF> before = layer->cachedLinkPolyline(idx);
        QCOMPARE(before.size(), 4);

        QVERIFY(layer->applyLinkFlip(idx));

        // Cached interior is reversed...
        QVector<QPointF> expectedInterior = bends;
        std::reverse(expectedInterior.begin(), expectedInterior.end());
        QCOMPARE(engineInterior(layer->engine(), idx), expectedInterior);

        // ...and so the full polyline is exactly the old one walked backwards.
        QVector<QPointF> expectedFull = before;
        std::reverse(expectedFull.begin(), expectedFull.end());
        QCOMPARE(layer->cachedLinkPolyline(idx), expectedFull);
    }

    // ── End-attached attributes ────────────────────────────────────────────

    //! Offsets and entry/exit losses belong to an end, not a slot, so they
    //! travel with the endpoint swap and the invert profile stays put. The
    //! average loss coefficient is not end-attached and must survive intact.
    void flipSwapsConduitEndAttributes()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        const int idx = layer->linkIndex(QStringLiteral("C1"));
        QVERIFY(idx >= 0);

        QCOMPARE(swmm_link_set_offset_up(eng, idx, 1.0), SWMM_OK);
        QCOMPARE(swmm_link_set_offset_dn(eng, idx, 0.5), SWMM_OK);
        QCOMPARE(swmm_link_set_loss_coeff(eng, idx, 0.9, 0.4, 0.1), SWMM_OK);

        QVERIFY(layer->applyLinkFlip(idx));

        double up = 0.0, dn = 0.0;
        QCOMPARE(swmm_link_get_offset_up(eng, idx, &up), SWMM_OK);
        QCOMPARE(swmm_link_get_offset_dn(eng, idx, &dn), SWMM_OK);
        QCOMPARE(up, 0.5);
        QCOMPARE(dn, 1.0);

        double inlet = 0.0, outlet = 0.0, avg = 0.0;
        QCOMPARE(swmm_link_get_loss_coeff(eng, idx, &inlet, &outlet, &avg), SWMM_OK);
        QCOMPARE(inlet, 0.4);
        QCOMPARE(outlet, 0.9);
        QCOMPARE(avg, 0.1);
    }

    //! Non-conduits keep their single crest offset where it is: offset_dn is
    //! unused for them, so a swap would move the real value into a dead slot.
    void flipLeavesNonConduitOffsetsAlone()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        const int idx = layer->linkIndex(QStringLiteral("P1"));   // pump J4 -> J3
        QVERIFY(idx >= 0);
        QVERIFY(!layer->isConduit(idx));

        double up0 = 0.0;
        QCOMPARE(swmm_link_get_offset_up(eng, idx, &up0), SWMM_OK);

        QVERIFY(layer->applyLinkFlip(idx));

        QCOMPARE(engineEndpoints(eng, idx), qMakePair(QStringLiteral("J3"),
                                                      QStringLiteral("J4")));
        double up1 = 0.0;
        QCOMPARE(swmm_link_get_offset_up(eng, idx, &up1), SWMM_OK);
        QCOMPARE(up1, up0);
    }

    // ── Undo ───────────────────────────────────────────────────────────────

    //! FlipLinkCommand uses one call for redo and undo, which is only correct
    //! if the flip is genuinely self-inverse across every field it touches.
    void flipCommandIsSelfInverse()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        const int idx = layer->linkIndex(QStringLiteral("C1"));
        QVERIFY(idx >= 0);

        const QVector<QPointF> bends{QPointF(300.0, 200.0), QPointF(700.0, -150.0)};
        QVERIFY(layer->applyLinkInteriorVertices(idx, bends));
        QCOMPARE(swmm_link_set_offset_up(eng, idx, 1.0), SWMM_OK);
        QCOMPARE(swmm_link_set_offset_dn(eng, idx, 0.5), SWMM_OK);
        QCOMPARE(swmm_link_set_loss_coeff(eng, idx, 0.9, 0.4, 0.1), SWMM_OK);

        const auto ends0     = engineEndpoints(eng, idx);
        const auto polyline0 = layer->cachedLinkPolyline(idx);

        MapUndoStack stack;
        stack.push(new FlipLinkCommand(layer.get(), idx, nullptr));
        QVERIFY(engineEndpoints(eng, idx) != ends0);

        stack.undo();

        QCOMPARE(engineEndpoints(eng, idx), ends0);
        QCOMPARE(layer->cachedLinkPolyline(idx), polyline0);
        QCOMPARE(engineInterior(eng, idx), bends);
        QCOMPARE(layer->linkFromNodeIdx(idx), swmm_node_index(eng, "J1"));
        QCOMPARE(layer->linkToNodeIdx(idx), swmm_node_index(eng, "J2"));

        double up = 0.0, dn = 0.0, inlet = 0.0, outlet = 0.0, avg = 0.0;
        swmm_link_get_offset_up(eng, idx, &up);
        swmm_link_get_offset_dn(eng, idx, &dn);
        swmm_link_get_loss_coeff(eng, idx, &inlet, &outlet, &avg);
        QCOMPARE(up, 1.0);
        QCOMPARE(dn, 0.5);
        QCOMPARE(inlet, 0.9);
        QCOMPARE(outlet, 0.4);
        QCOMPARE(avg, 0.1);
    }

    // ── Refresh contract ───────────────────────────────────────────────────

    //! Every view that shows direction (map arrowheads, Properties From/To,
    //! Attribute Table, profile plot) reads endpoints live and repaints off
    //! these signals; without them a flip is invisible until reselection.
    void flipEmitsRefreshSignals()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        const int idx = layer->linkIndex(QStringLiteral("C1"));
        QVERIFY(idx >= 0);

        QSignalSpy paint(layer.get(), &SWMMModelLayer::repaintRequested);
        QSignalSpy edited(layer.get(), &SWMMModelLayer::modelEdited);
        QSignalSpy attr(layer.get(), &SWMMModelLayer::attributeChanged);

        QVERIFY(layer->applyLinkFlip(idx));

        QCOMPARE(paint.count(), 1);
        QCOMPARE(edited.count(), 1);
        QCOMPARE(attr.count(), 1);
        QCOMPARE(attr.first().first().toString(), QStringLiteral("C1"));
    }

    //! A bad index must not touch the model or announce anything.
    void flipRejectsInvalidIndex()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        QSignalSpy paint(layer.get(), &SWMMModelLayer::repaintRequested);

        QVERIFY(!layer->applyLinkFlip(-1));
        QVERIFY(!layer->applyLinkFlip(swmm_link_count(layer->engine())));
        QCOMPARE(paint.count(), 0);
    }
};

QTEST_MAIN(TestLinkFlip)
#include "test_linkflip.moc"
