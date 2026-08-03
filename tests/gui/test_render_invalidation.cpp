/*!
 * \file   test_render_invalidation.cpp
 * \brief  Regression tests for the "have to zoom in and out before it appears"
 *         class of stale-render bugs.
 *
 *   Two independent defects are covered:
 *
 *   1. SWMM2DMeshLayer::applyMeshVertexZ took an incremental fast path that
 *      updates the per-triangle z0/z1/z2/zAvg — the inputs to the hillshade —
 *      without bumping m_geomRevision. SWMM2DMeshQSGRenderer caches shaded
 *      per-triangle RGB, isobands and isolines behind a geomRevision-keyed
 *      check, so the caches reported a hit and re-uploaded the pre-edit
 *      colours: the frame redrew and the shading did not change.
 *
 *   2. The QSG renderers marked themselves dirty on a layer signal but had no
 *      way to tell MapCanvas, which caches the grabbed framebuffer. A change
 *      arriving after the canvas consumed (and cleared) its own dirty flag
 *      would never be regrabbed. contentRevision() makes that cache
 *      content-keyed as well as extent/layer/size-keyed.
 *
 *   The canvas-level channel wiring (invalidate(Scene) marking the QSG frame
 *   dirty) and the map tools' channel choices are exercised by the manual
 *   smoke steps in the handoff — MapCanvas is not constructible in the
 *   offscreen test harness without a live QQuickWidget and its private cache
 *   flags have no test seam.
 */

#include "layers/swmm2dmeshlayer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "mesh/meshresult.h"

#include <QPointF>
#include <QSet>
#include <QTest>

namespace {

//! Small regular grid mesh — enough triangles for a real vertex-triangle
//! adjacency so applyMeshVertexZ takes its incremental branch.
mesh::MeshResult makeGridMesh(int n = 8)
{
    mesh::MeshResult m;
    m.vertices.reserve((n + 1) * (n + 1));
    for (int r = 0; r <= n; ++r)
        for (int c = 0; c <= n; ++c) {
            mesh::MeshVertex v;
            v.xy = QPointF(c, r);
            v.z  = 0.5 * c + 0.25 * r;
            m.vertices.append(v);
        }
    m.triangles.reserve(n * n * 2);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            const int v00 = r * (n + 1) + c, v10 = v00 + 1;
            const int v01 = v00 + n + 1,     v11 = v01 + 1;
            mesh::MeshTriangle t1; t1.v0 = v00; t1.v1 = v10; t1.v2 = v11;
            mesh::MeshTriangle t2; t2.v0 = v00; t2.v1 = v11; t2.v2 = v01;
            m.triangles.append(t1);
            m.triangles.append(t2);
        }
    m.ok = true;
    return m;
}

} // namespace

class TestRenderInvalidation : public QObject
{
    Q_OBJECT

private slots:

    // ---- Defect 1: vertex-Z edits must invalidate the shading caches -------

    //! A real elevation change bumps geomRevision, so the renderer's
    //! geomRevision-keyed fill / isoband / isoline caches miss and the
    //! hillshade is recomputed from the new z0/z1/z2.
    void vertexZEdit_bumpsGeomRevision()
    {
        SWMM2DMeshLayer layer(makeGridMesh(), QString());

        const quint64 before = layer.geomRevision();
        QVERIFY(layer.applyMeshVertexZ(12, 42.0));
        QVERIFY2(layer.geomRevision() != before,
                 "applyMeshVertexZ must invalidate the shaded-RGB cache key; "
                 "without the bump the renderer re-uploads pre-edit colours "
                 "and the hillshade never updates");
    }

    //! Every subsequent edit keeps invalidating — not just the first.
    void vertexZEdit_bumpsOnEachEdit()
    {
        SWMM2DMeshLayer layer(makeGridMesh(), QString());

        quint64 prev = layer.geomRevision();
        for (int i = 0; i < 4; ++i) {
            QVERIFY(layer.applyMeshVertexZ(12, 10.0 + i));
            const quint64 now = layer.geomRevision();
            QVERIFY2(now != prev, "revision must advance on every Z edit");
            prev = now;
        }
    }

    //! A no-op write must NOT bump: the setter short-circuits on an unchanged
    //! elevation, and spuriously invalidating would throw away the shaded-RGB
    //! cache (and force a position-buffer rebuild) for nothing.
    void vertexZEdit_noOpDoesNotBump()
    {
        SWMM2DMeshLayer layer(makeGridMesh(), QString());

        QVERIFY(layer.applyMeshVertexZ(12, 7.5));
        const quint64 after = layer.geomRevision();
        QVERIFY(layer.applyMeshVertexZ(12, 7.5));   // same value again
        QCOMPARE(layer.geomRevision(), after);
    }

    //! An out-of-range index changes nothing and must not invalidate.
    void vertexZEdit_invalidIndexDoesNotBump()
    {
        SWMM2DMeshLayer layer(makeGridMesh(), QString());

        const quint64 before = layer.geomRevision();
        QVERIFY(!layer.applyMeshVertexZ(-1, 1.0));
        QVERIFY(!layer.applyMeshVertexZ(1 << 20, 1.0));
        QCOMPARE(layer.geomRevision(), before);
    }

    // ---- Defect 2: the renderer must publish a content revision -----------

    //! A mesh selection change reaches the renderer's content revision, which
    //! is what MapCanvas polls to decide whether its cached framebuffer is
    //! stale. Before this, a selection that landed after the canvas cleared
    //! m_qsgFrameDirty was never regrabbed — the highlight appeared only once
    //! a zoom changed the extent.
    void meshSelection_advancesRendererContentRevision()
    {
        SWMM2DMeshLayer layer(makeGridMesh(), QString());
        SWMM2DMeshQSGRenderer renderer;
        renderer.setLayer(&layer);

        const quint64 before = renderer.contentRevision();
        layer.setHighlightedVertices({0, 1, 2});
        QVERIFY2(renderer.contentRevision() != before,
                 "setHighlightedVertices -> repaintRequested must advance the "
                 "renderer's content revision so MapCanvas regrabs");

        const quint64 afterVerts = renderer.contentRevision();
        layer.setHighlightedTriangles({4, 5});
        QVERIFY(renderer.contentRevision() != afterVerts);

        const quint64 afterTris = renderer.contentRevision();
        layer.setHighlightedEdges({9});
        QVERIFY(renderer.contentRevision() != afterTris);
    }

    //! Re-selecting the same set is a no-op in the layer (the setters return
    //! early on an unchanged set), so it must not force a framebuffer regrab.
    void meshSelection_unchangedSetDoesNotAdvance()
    {
        SWMM2DMeshLayer layer(makeGridMesh(), QString());
        SWMM2DMeshQSGRenderer renderer;
        renderer.setLayer(&layer);

        layer.setHighlightedVertices({0, 1, 2});
        const quint64 after = renderer.contentRevision();
        layer.setHighlightedVertices({0, 1, 2});   // identical set
        QCOMPARE(renderer.contentRevision(), after);
    }

    //! A vertex-Z edit also has to reach the renderer — it emits
    //! repaintRequested, so it rides the same content-revision path.
    void vertexZEdit_advancesRendererContentRevision()
    {
        SWMM2DMeshLayer layer(makeGridMesh(), QString());
        SWMM2DMeshQSGRenderer renderer;
        renderer.setLayer(&layer);

        const quint64 before = renderer.contentRevision();
        QVERIFY(layer.applyMeshVertexZ(12, 99.0));
        QVERIFY(renderer.contentRevision() != before);
    }
};

QTEST_MAIN(TestRenderInvalidation)
#include "test_render_invalidation.moc"
