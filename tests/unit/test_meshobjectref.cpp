/*!
 * \file   test_meshobjectref.cpp
 * \brief  Slice §V.VA — MeshObjectRef encode/parse round-trip tests.
 *
 * MeshObjectRef builds SWMMObjectRef instances naming mesh vertices and
 * edges; the encoded form has to round-trip through QSet (qHash) and the
 * SelectionManager so future Property-Browser / Attribute-Table views see
 * the same identity the toolbar selected.
 *
 * The layer pointer fed to MeshObjectRef::vertex / ::edge is only used to
 * extract a "mesh::<id>" key; calling with nullptr is permitted (yields
 * a `<null>` key) so these tests don't need a real SWMM2DMeshLayer.
 *
 * Tests:
 *   1. vertex(nullptr,N) round-trips through parseVertex
 *   2. edge(nullptr, T, E) round-trips through parseEdge
 *   3. Edge ref with edgeLocal out of [0..2] is rejected by parseEdge
 *   4. Parse rejects refs of the wrong ObjectType
 *   5. qHash distinguishes vertex/edge refs that share a name string
 *   6. Two refs with the same (type, name) are equal and collapse in QSet
 */

#include <gtest/gtest.h>

#include <QSet>

#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"

using mesh::MeshObjectRef;

TEST(MeshObjectRef, VertexRoundTrip)
{
    const SWMMObjectRef ref = MeshObjectRef::vertex(QString(), 1234);
    EXPECT_EQ(ref.objectType, SWMMObjectRef::MeshVertex);
    EXPECT_TRUE(ref.name.contains("#v1234"));

    QString layerKey;
    int vidx = -1;
    ASSERT_TRUE(MeshObjectRef::parseVertex(ref, &layerKey, &vidx));
    EXPECT_EQ(vidx, 1234);
    EXPECT_EQ(layerKey, MeshObjectRef::layerKey(QString()));
}

TEST(MeshObjectRef, EdgeRoundTrip)
{
    const SWMMObjectRef ref = MeshObjectRef::edge(QString(), 567, 2);
    EXPECT_EQ(ref.objectType, SWMMObjectRef::MeshEdge);
    EXPECT_TRUE(ref.name.contains("#e567:2"));

    QString layerKey;
    int tri = -1, e = -1;
    ASSERT_TRUE(MeshObjectRef::parseEdge(ref, &layerKey, &tri, &e));
    EXPECT_EQ(tri, 567);
    EXPECT_EQ(e, 2);
}

TEST(MeshObjectRef, EdgeRejectsOutOfRangeLocal)
{
    const SWMMObjectRef bad(SWMMObjectRef::MeshEdge,
                            MeshObjectRef::layerKey(QString()) + QStringLiteral("#e10:5"));
    QString layerKey;
    int tri = -1, e = -1;
    EXPECT_FALSE(MeshObjectRef::parseEdge(bad, &layerKey, &tri, &e));
}

TEST(MeshObjectRef, ParseRejectsWrongObjectType)
{
    // A vertex ref string with edge-type kind should fail parseVertex,
    // and vice versa.
    const SWMMObjectRef asEdge(SWMMObjectRef::MeshEdge,
                               MeshObjectRef::vertex(QString(), 3).name);
    QString lk;
    int vi = -1;
    EXPECT_FALSE(MeshObjectRef::parseVertex(asEdge, &lk, &vi));

    const SWMMObjectRef asVertex(SWMMObjectRef::MeshVertex,
                                 MeshObjectRef::edge(QString(), 1, 0).name);
    int tri = -1, e = -1;
    EXPECT_FALSE(MeshObjectRef::parseEdge(asVertex, &lk, &tri, &e));
}

TEST(MeshObjectRef, QHashDistinguishesVertexAndEdgeWithSameNameString)
{
    // Construct two refs that share a name but differ in objectType.
    const QString shared = MeshObjectRef::layerKey(QString()) + QStringLiteral("#collide");
    const SWMMObjectRef a(SWMMObjectRef::MeshVertex, shared);
    const SWMMObjectRef b(SWMMObjectRef::MeshEdge,   shared);
    EXPECT_NE(a, b);

    QSet<SWMMObjectRef> s;
    s.insert(a);
    s.insert(b);
    EXPECT_EQ(s.size(), 2);
}

TEST(MeshObjectRef, EqualRefsCollapseInQSet)
{
    const SWMMObjectRef r1 = MeshObjectRef::vertex(QString(), 42);
    const SWMMObjectRef r2 = MeshObjectRef::vertex(QString(), 42);
    EXPECT_EQ(r1, r2);
    QSet<SWMMObjectRef> s;
    s.insert(r1);
    s.insert(r2);
    EXPECT_EQ(s.size(), 1);
}
