/*!
 * \file   test_meshautocouple.cpp
 * \brief  Coincident-node matching behind the mesh toolbar's Auto-couple.
 *
 * Exercises mesh::findCoincidentNodes: exact hits, near-misses outside the
 * tolerance, already-coupled skips, selection-subset vs whole-mesh scans,
 * and nearest-wins disambiguation with an explicit tolerance.
 */
#include <gtest/gtest.h>

#include <QList>
#include <QPointF>
#include <QVector>

#include "mesh/meshautocouple.h"
#include "mesh/meshresult.h"

namespace {

/*! 3×1 strip of unit squares: vertices at x = 0,1,2,3 × y = 0,1. */
mesh::MeshResult makeStripMesh()
{
    mesh::MeshResult m;
    for (int x = 0; x <= 3; ++x) {
        m.vertices.append({{double(x), 0.0}, 0.0, 0, {}});
        m.vertices.append({{double(x), 1.0}, 0.0, 0, {}});
    }
    m.ok = true;
    return m;
}

} // namespace

TEST(MeshAutoCouple, ExactHitCouplesUncoupledVertex)
{
    const auto m = makeStripMesh();
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J1"), QPointF(1.0, 0.0) },   // vertex 2
    };
    const auto r = mesh::findCoincidentNodes(m, nodes);
    ASSERT_EQ(r.matches.size(), 1);
    EXPECT_EQ(r.matches.value(2), QStringLiteral("J1"));
    EXPECT_EQ(r.alreadyCoupled, 0);
    EXPECT_EQ(r.unmatchedNodes, 0);
}

TEST(MeshAutoCouple, NearMissOutsideTolIsUnmatched)
{
    const auto m = makeStripMesh();
    // Default tol ≈ 1e-6 × bbox diagonal (~3.16e-6) — 0.01 is a clear miss.
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J1"), QPointF(1.01, 0.0) },
    };
    const auto r = mesh::findCoincidentNodes(m, nodes);
    EXPECT_TRUE(r.matches.isEmpty());
    EXPECT_EQ(r.unmatchedNodes, 1);
}

TEST(MeshAutoCouple, AlreadyCoupledVertexIsSkipped)
{
    auto m = makeStripMesh();
    m.vertices[2].coupledNode = QStringLiteral("EXISTING");
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J1"), QPointF(1.0, 0.0) },   // vertex 2 — coupled
        { QStringLiteral("J2"), QPointF(2.0, 0.0) },   // vertex 4 — free
    };
    const auto r = mesh::findCoincidentNodes(m, nodes);
    ASSERT_EQ(r.matches.size(), 1);
    EXPECT_EQ(r.matches.value(4), QStringLiteral("J2"));
    EXPECT_EQ(r.alreadyCoupled, 1);
    EXPECT_EQ(r.unmatchedNodes, 0);
}

TEST(MeshAutoCouple, SelectionSubsetRestrictsCandidates)
{
    const auto m = makeStripMesh();
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J1"), QPointF(1.0, 0.0) },   // vertex 2 — selected
        { QStringLiteral("J2"), QPointF(2.0, 0.0) },   // vertex 4 — NOT selected
    };
    const auto r = mesh::findCoincidentNodes(m, nodes, /*targets=*/{ 2 });
    ASSERT_EQ(r.matches.size(), 1);
    EXPECT_EQ(r.matches.value(2), QStringLiteral("J1"));
    EXPECT_EQ(r.unmatchedNodes, 1) << "node outside the selection must not match";
}

TEST(MeshAutoCouple, WholeMeshWhenTargetsEmpty)
{
    const auto m = makeStripMesh();
    QVector<QPair<QString, QPointF>> nodes;
    for (int x = 0; x <= 3; ++x)
        nodes.append({ QStringLiteral("N%1").arg(x), QPointF(double(x), 1.0) });
    const auto r = mesh::findCoincidentNodes(m, nodes);
    EXPECT_EQ(r.matches.size(), 4);
    EXPECT_EQ(r.unmatchedNodes, 0);
}

TEST(MeshAutoCouple, NearestVertexWinsWithinExplicitTol)
{
    const auto m = makeStripMesh();
    // With tol = 0.6 both vertex 2 (1,0) and vertex 4 (2,0) are candidates
    // for a node at (1.4, 0) — vertex 2 is nearer (0.4 vs 0.6).
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J1"), QPointF(1.4, 0.0) },
    };
    const auto r = mesh::findCoincidentNodes(m, nodes, {}, /*tol=*/0.6);
    ASSERT_EQ(r.matches.size(), 1);
    EXPECT_EQ(r.matches.value(2), QStringLiteral("J1"));
}
