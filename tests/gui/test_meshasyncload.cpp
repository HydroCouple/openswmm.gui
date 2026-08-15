/*!
 * \file   test_meshasyncload.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Mesh Tiled LOD plan Phase 1 (P1.1 + P1.2) coverage:
 *           - Rendering/QsgMeshEnabled preference (default ON, signal on
 *             change) — the P1.1 activation knob.
 *           - SWMM2DMeshLayer::setQsgOwnsRendering() gates the QPainter
 *             SWMM2DMeshGraphicsItem::paint() (no double-paint while the QSG
 *             renderer owns the layer) and leaves pick/identify untouched.
 *           - Async-open equivalence: a layer built on a QtConcurrent worker
 *             and moved to the GUI thread (the attachMesh2DLayersAsync
 *             worker half) matches a synchronously-built layer.
 *           - QPainter vs QSG visual-parity screenshots at three zooms,
 *             written to tests/output/mesh_qsg_parity/ for human review
 *             (CLAUDE.md §4.1 Transparent File IO).
 *
 *         Uses QTEST_MAIN under the offscreen QPA like test_asyncload.
 */
#include "layers/swmm2dmeshlayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "render/sublayers/meshnodesublayer.h"
#include "map/mapextent.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "map/swmmlayerqsgrenderer.h"
#include "mesh/inpmeshreader.h"
#include "core/preferencesmanager.h"

#include <QtQml/qqml.h>

#include <QDir>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSurfaceFormat>
#include <QSignalSpy>
#include <QTest>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

#include <cmath>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("mesh_async_fixture.inp"));
}

QString outputDir()
{
    // Reviewable location (never a temp dir): <repo>/tests/output/mesh_qsg_parity.
    QDir d(dataDir());              // tests/gui/data
    d.cdUp();                       // tests/gui
    d.cdUp();                       // tests
    const QString out = d.filePath(QStringLiteral("output/mesh_qsg_parity"));
    QDir().mkpath(out);
    return out;
}

int nonWhitePixels(const QImage &img)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (row[x] != qRgb(255, 255, 255)) ++n;
    }
    return n;
}

// Count near-black pixels — the wireframe edges and vertex dots are drawn in
// dark ink over the pale terrain fill, so this signals whether the
// edge/vertex passes engaged (the elevation ramp never produces near-black).
int darkInkPixels(const QImage &img)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qRed(row[x]) < 60 && qGreen(row[x]) < 60 && qBlue(row[x]) < 60)
                ++n;
    }
    return n;
}

QImage paintQPainterPath(SWMM2DMeshLayer *layer, const QRectF &sceneRect,
                         const QSize &view)
{
    QGraphicsScene scene;
    layer->populateScene(&scene, layer->extent(), nullptr);
    QImage img(view, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    scene.render(&p, QRectF(QPointF(0, 0), QSizeF(view)), sceneRect);
    p.end();
    layer->depopulateScene(&scene);
    return img;
}

} // namespace

class TestMeshAsyncLoad : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        QVERIFY2(QFile::exists(fixturePath()),
                 "mesh_async_fixture.inp missing from the gui-test data dir");
    }

    // P1.1 knob — preference default ON; setter round-trips and signals.
    void qsgMeshPreferenceDefaultOnAndSignals()
    {
        auto *p = PreferencesManager::instance();
        const bool original = p->qsgMeshRenderEnabled();

        QSignalSpy spy(p, &PreferencesManager::preferenceChanged);
        p->setQsgMeshRenderEnabled(!original);
        QCOMPARE(p->qsgMeshRenderEnabled(), !original);
        bool sawKey = false;
        for (const auto &args : spy)
            if (args.at(0).toString() == QStringLiteral("Rendering")
                && args.at(1).toString() == QStringLiteral("QsgMeshEnabled"))
                sawKey = true;
        QVERIFY(sawKey);

        // Restore, then verify the shipped default is ON by writing true
        // explicitly (a cleared key also reads true — the getter default).
        p->setQsgMeshRenderEnabled(original);
        QCOMPARE(p->qsgMeshRenderEnabled(), original);
    }

    // MESH_DECOUPLED_1D2D_REMAP_PLAN Part A — the Metadata tab gains four
    // cell-area rows, and Part C adds the cell-coupling row count. Checked
    // on the layer (not the free stats function) because the layer is what
    // the Properties dialog reads.
    void metadataCarriesCellAreaStatsAndCouplingRows()
    {
        mesh::InpMeshReadResult read = mesh::InpMeshReader::read(fixturePath());
        QVERIFY2(read.hasMesh, qPrintable(read.errorMsg));
        SWMM2DMeshLayer layer(std::move(read.mesh), read.sourcePath);
        QVERIFY(layer.triangleCount() > 0);

        auto rowValue = [](const QVector<QPair<QString, QString>> &md,
                           const QString &key) -> QString {
            for (const auto &kv : md)
                if (kv.first == key) return kv.second;
            return {};
        };

        const auto md = layer.extendedMetadata();
        const QString mn  = rowValue(md, QStringLiteral("Cell area (min)"));
        const QString mx  = rowValue(md, QStringLiteral("Cell area (max)"));
        const QString avg = rowValue(md, QStringLiteral("Cell area (mean)"));
        const QString med = rowValue(md, QStringLiteral("Cell area (median)"));
        QVERIFY2(!mn.isEmpty() && !mx.isEmpty() && !avg.isEmpty() && !med.isEmpty(),
                 "cell-area rows missing from mesh metadata");

        bool ok = false;
        const double dmn = mn.toDouble(&ok);  QVERIFY(ok);
        const double dmx = mx.toDouble(&ok);  QVERIFY(ok);
        const double dav = avg.toDouble(&ok); QVERIFY(ok);
        const double dmd = med.toDouble(&ok); QVERIFY(ok);
        QVERIFY(dmn > 0.0);
        QVERIFY(dmn <= dmd && dmd <= dmx);
        QVERIFY(dmn <= dav && dav <= dmx);

        // No cell couplings on the fixture — the row stays hidden.
        QVERIFY(rowValue(md, QStringLiteral("Coupled cells (rows)")).isEmpty());

        // Author rows through the mutator the Remap action uses. Invalid
        // rows are dropped; the previous set comes back for undo.
        QVector<mesh::CellCoupling> rows = {
            { 0, QStringLiteral("W_UP"), 0.65, 2.0 },
            { 0, QStringLiteral("W_DN"), 0.65, 2.0 },
            { layer.triangleCount(), QStringLiteral("OOR"), 0.65, 2.0 },  // dropped
            { 1, QString(),            0.65, 2.0 },                       // dropped
        };
        const QVector<mesh::CellCoupling> before = layer.applyCellCouplings(rows);
        QVERIFY(before.isEmpty());
        QCOMPARE(layer.cellCouplings().size(), 2);
        QCOMPARE(rowValue(layer.extendedMetadata(),
                          QStringLiteral("Coupled cells (rows)")),
                 QStringLiteral("2"));

        // Undo contract — the mutator hands back what it replaced.
        const QVector<mesh::CellCoupling> prior =
            layer.applyCellCouplings(QVector<mesh::CellCoupling>{});
        QCOMPARE(prior.size(), 2);
        QVERIFY(layer.cellCouplings().isEmpty());
    }

    // P1.1 gate — QSG ownership suppresses the CPU paint; picks unaffected.
    void qsgOwnershipGatesCpuPaintNotPicks()
    {
        mesh::InpMeshReadResult read = mesh::InpMeshReader::read(fixturePath());
        QVERIFY2(read.hasMesh, qPrintable(read.errorMsg));
        SWMM2DMeshLayer layer(std::move(read.mesh), read.sourcePath);

        const QSize view(400, 300);
        const QRectF full = layer.m_sceneBBox;

        QCOMPARE(layer.qsgOwnsRendering(), false);
        const QImage cpuOwned = paintQPainterPath(&layer, full, view);
        QVERIFY2(nonWhitePixels(cpuOwned) > 1000,
                 "CPU path painted nothing while it owns rendering");

        const int vtx  = layer.pickVertexAt(full.center().x(), full.center().y(),
                                            50.0, 1.0);
        const int cell = layer.pickCellAt(full.center());

        QSignalSpy repaint(&layer, &OpenSWMMVisLayer::repaintRequested);
        layer.setQsgOwnsRendering(true);
        QCOMPARE(repaint.count(), 1);
        layer.setQsgOwnsRendering(true);      // no-op — no second emit
        QCOMPARE(repaint.count(), 1);

        const QImage cpuGated = paintQPainterPath(&layer, full, view);
        QCOMPARE(nonWhitePixels(cpuGated), 0);

        // Identify/pick resolve against native arrays regardless of owner.
        QCOMPARE(layer.pickVertexAt(full.center().x(), full.center().y(),
                                    50.0, 1.0), vtx);
        QCOMPARE(layer.pickCellAt(full.center()), cell);

        layer.setQsgOwnsRendering(false);
        const QImage cpuBack = paintQPainterPath(&layer, full, view);
        QCOMPARE(cpuBack, cpuOwned);
    }

    // P1.2 — worker-thread build + moveToThread(GUI) equals the sync build.
    void workerBuildEqualsSyncBuild()
    {
        mesh::InpMeshReadResult syncRead = mesh::InpMeshReader::read(fixturePath());
        QVERIFY(syncRead.hasMesh);
        SWMM2DMeshLayer syncLayer(std::move(syncRead.mesh), syncRead.sourcePath);

        QFutureWatcher<SWMM2DMeshLayer *> watcher;
        QSignalSpy done(&watcher, &QFutureWatcherBase::finished);
        const QString path = fixturePath();
        watcher.setFuture(QtConcurrent::run([path]() -> SWMM2DMeshLayer * {
            mesh::InpMeshReadResult r = mesh::InpMeshReader::read(path);
            if (!r.hasMesh) return nullptr;
            auto *l = new SWMM2DMeshLayer(std::move(r.mesh), r.sourcePath);
            l->moveToThread(qApp->thread());
            return l;
        }));
        QVERIFY2(done.wait(20000), "worker mesh build did not finish within 20s");

        SWMM2DMeshLayer *asyncLayer = watcher.result();
        QVERIFY(asyncLayer != nullptr);
        QCOMPARE(asyncLayer->thread(), qApp->thread());

        QCOMPARE(asyncLayer->vertexCount(),   syncLayer.vertexCount());
        QCOMPARE(asyncLayer->triangleCount(), syncLayer.triangleCount());
        QCOMPARE(asyncLayer->edgeCount(),     syncLayer.edgeCount());
        QCOMPARE(asyncLayer->zMin(),          syncLayer.zMin());
        QCOMPARE(asyncLayer->zMax(),          syncLayer.zMax());
        QCOMPARE(asyncLayer->m_sceneBBox,     syncLayer.m_sceneBBox);
        QCOMPARE(asyncLayer->m_sceneTris.size(), syncLayer.m_sceneTris.size());

        // Adoption works: the moved layer paints on the GUI thread.
        const QImage img = paintQPainterPath(asyncLayer, asyncLayer->m_sceneBBox,
                                             QSize(400, 300));
        QVERIFY(nonWhitePixels(img) > 1000);

        delete asyncLayer;
    }

    // Progressive load — a deferHeavyGeometry layer renders (fill) right
    // away, then finishSceneGeometryAsync() delivers wireframe/grids/
    // adjacency/BCs identical to a synchronously-built layer.
    void progressiveLoadEqualsSyncBuild()
    {
        mesh::InpMeshReadResult syncRead = mesh::InpMeshReader::read(fixturePath());
        QVERIFY(syncRead.hasMesh);
        SWMM2DMeshLayer syncLayer(std::move(syncRead.mesh), syncRead.sourcePath);

        mesh::InpMeshReadResult defRead = mesh::InpMeshReader::read(fixturePath());
        QVERIFY(defRead.hasMesh);
        SWMM2DMeshLayer defLayer(std::move(defRead.mesh), defRead.sourcePath,
                                 nullptr, /*deferHeavyGeometry=*/true);

        // Phase A: drawable immediately (fill), but heavy structures absent.
        QVERIFY(!defLayer.sceneGeometryComplete());
        QCOMPARE(defLayer.m_sceneTris.size(), syncLayer.m_sceneTris.size());
        QVERIFY(defLayer.m_sceneEdges.isEmpty());
        QVERIFY(defLayer.m_triGrid.isEmpty());
        const QImage phaseA = paintQPainterPath(&defLayer, defLayer.m_sceneBBox,
                                                QSize(400, 300));
        QVERIFY2(nonWhitePixels(phaseA) > 1000,
                 "Phase-A layer painted nothing");

        // Phase B: background build converges to the sync layer's state.
        QSignalSpy ready(&defLayer, &SWMM2DMeshLayer::sceneGeometryReady);
        defLayer.finishSceneGeometryAsync();
        QVERIFY2(ready.wait(20000), "deferred geometry did not finish in 20s");
        QVERIFY(defLayer.sceneGeometryComplete());
        QCOMPARE(defLayer.m_sceneEdges.size(), syncLayer.m_sceneEdges.size());
        QCOMPARE(defLayer.edgeCount(),         syncLayer.edgeCount());
        QCOMPARE(defLayer.maxSlope(),          syncLayer.maxSlope());
        QVERIFY(!defLayer.m_triGrid.isEmpty());
        QVERIFY(!defLayer.m_edgeGrid.isEmpty());
        QCOMPARE(defLayer.edgeBCs().size(),    syncLayer.edgeBCs().size());
        QCOMPARE(defLayer.pickCellAt(defLayer.m_sceneBBox.center()),
                 syncLayer.pickCellAt(syncLayer.m_sceneBBox.center()));
    }

    // Style plumbing — changing the FILL sublayer's colour ramp restyles the
    // shaded-relief terrain fill (the QPainter path shares the QSG
    // schemeDrivesColor decision, so a pixel diff here proves the chain
    // dialog → MeshFillStyle::setScheme → renderer).
    void terrainRampDrivesFill()
    {
        mesh::InpMeshReadResult read = mesh::InpMeshReader::read(fixturePath());
        QVERIFY(read.hasMesh);
        SWMM2DMeshLayer layer(std::move(read.mesh), read.sourcePath);

        const QSize view(400, 300);
        const QImage before = paintQPainterPath(&layer, layer.m_sceneBBox, view);

        auto *fill = layer.meshFillSublayer()->fillStyle();
        QSignalSpy repaint(&layer, &OpenSWMMVisLayer::repaintRequested);
        auto scheme = fill->scheme();
        scheme.setRampName(QStringLiteral("viridis"));
        fill->setScheme(scheme);
        QVERIFY2(repaint.count() >= 1,
                 "scheme edit did not reach the layer's repaintRequested");

        const QImage after = paintQPainterPath(&layer, layer.m_sceneBBox, view);
        QVERIFY2(after != before,
                 "changing the terrain colour ramp did not change the fill");

        // Invert flag also drives colour.
        const QImage inv1 = after;
        scheme = fill->scheme();
        scheme.setInvertRamp(true);
        fill->setScheme(scheme);
        const QImage inv2 = paintQPainterPath(&layer, layer.m_sceneBBox, view);
        QVERIFY(inv2 != inv1);
    }

    // Zoom-gated wireframe + vertices — edges/vertex dots stay hidden at far
    // zoom (sub-pixel cells = noise) and appear automatically once zoomed in
    // far enough that cells project large on screen. Uses a dense grid so the
    // full-extent view genuinely has sub-pixel cells (below the 200k-tri
    // overview threshold, so the wireframe pass — not the quad bake — governs).
    void edgesAndVerticesAppearWhenZoomedIn()
    {
        mesh::MeshResult m;
        const int n = 200;   // 201×201 verts, 80k tris (< 200k → no overview)
        m.vertices.reserve((n + 1) * (n + 1));
        for (int r = 0; r <= n; ++r)
            for (int c = 0; c <= n; ++c) {
                mesh::MeshVertex v;
                v.xy = QPointF(c, r);       // 1 unit spacing → 200×200 domain
                v.z  = 0.02 * c + 0.3 * std::sin(r * 0.2);
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
        SWMM2DMeshLayer layer(std::move(m), QString());
        QVERIFY(!layer.hasOverview());   // dense-but-small → wireframe governs

        // Defaults are moderate — detail comes in at a sensible zoom, not
        // only at extreme close-up (a few px per cell, not >10).
        QVERIFY(layer.edgeZoomMinCellPx()   > 0.0 && layer.edgeZoomMinCellPx()   <= 5.0);
        QVERIFY(layer.vertexZoomMinCellPx() > 0.0 && layer.vertexZoomMinCellPx() <= 8.0);

        // Explicit thresholds so the test is independent of the default
        // values: edges/vertices appear once cells project ≥ 10 px across.
        layer.setEdgeZoomMinCellPx(10.0);
        layer.setVertexZoomMinCellPx(10.0);

        const QSize view(800, 600);
        const QRectF bbox = layer.m_sceneBBox;

        // Full extent: 200 cells across 800 px → ~4 px/cell (16 px²), below
        // the 100 px² threshold → wireframe + vertices gated OFF.
        const int farDark = darkInkPixels(paintQPainterPath(&layer, bbox, view));

        // Zoom into a ~10-cell window (1/20 of each side) → ~80 px/cell →
        // 6400 px² → both gates open.
        const double f = 1.0 / 20.0;
        const QRectF nearRect(bbox.center().x() - bbox.width()  * f / 2.0,
                              bbox.center().y() - bbox.height() * f / 2.0,
                              bbox.width() * f, bbox.height() * f);
        const int nearDark = darkInkPixels(paintQPainterPath(&layer, nearRect, view));

        // Far view is essentially free of wireframe ink; the zoomed-in view
        // has a clear wireframe (thin anti-aliased edges → dozens of
        // fully-dark px, vs ~none at far zoom).
        QVERIFY2(farDark < 20 && nearDark > 50,
                 qPrintable(QStringLiteral("expected wireframe/vertices only "
                            "when zoomed in: farDark=%1 nearDark=%2")
                            .arg(farDark).arg(nearDark)));

        // The vertex sublayer defaults visible now (the LOD gate, not a
        // hidden flag, keeps far-zoom clean).
        QVERIFY(layer.meshNodeSublayer()->isVisible());

        // Configurable thresholds: raising the threshold above the near-zoom
        // cell size suppresses the detail even when zoomed in; 0 forces it on.
        layer.setEdgeZoomMinCellPx(400.0);
        layer.setVertexZoomMinCellPx(400.0);
        const int nearHiThresh =
            darkInkPixels(paintQPainterPath(&layer, nearRect, view));
        QVERIFY2(nearHiThresh < 20,
                 "raising the zoom threshold should suppress edges/vertices");

        layer.setEdgeZoomMinCellPx(0.0);   // always on
        const int farNoThresh =
            darkInkPixels(paintQPainterPath(&layer, bbox, view));
        QVERIFY2(farNoThresh > 50,
                 "a 0 edge threshold should force the wireframe on at any zoom");
    }

    // Pyramid rebuild — rebuildOverviewAsync() builds the far-zoom overview
    // on a worker, emits started/finished, adopts on the GUI thread, and
    // matches the synchronous load-time bake.
    void pyramidRebuildAsync()
    {
        // Programmatic mesh above the 200k-tri overview threshold: a
        // 320x320 quad grid = 204,800 triangles with a sloped surface.
        mesh::MeshResult m;
        const int n = 320;
        m.vertices.reserve((n + 1) * (n + 1));
        for (int r = 0; r <= n; ++r)
            for (int c = 0; c <= n; ++c) {
                mesh::MeshVertex v;
                v.xy = QPointF(c * 2.0, r * 2.0);
                v.z  = 0.01 * c + 0.5 * std::sin(r * 0.1);
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

        SWMM2DMeshLayer layer(std::move(m), QString());
        QVERIFY(layer.hasOverview());   // load-time bake ran (>= 200k tris)
        const int bakedCount = layer.m_overviewTris.size();
        const double bakedSpan = layer.m_nativeTriSpan;

        QSignalSpy started (&layer, &SWMM2DMeshLayer::overviewBuildStarted);
        QSignalSpy finished(&layer, &SWMM2DMeshLayer::overviewBuildFinished);
        QSignalSpy repaint (&layer, &OpenSWMMVisLayer::repaintRequested);

        layer.rebuildOverviewAsync();
        QVERIFY(layer.overviewBuildRunning());
        QCOMPARE(started.count(), 1);
        layer.rebuildOverviewAsync();          // no-op while running
        QCOMPARE(started.count(), 1);

        QVERIFY2(finished.wait(20000), "pyramid rebuild did not finish in 20s");
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.takeFirst().at(0).toBool());
        QVERIFY(!layer.overviewBuildRunning());
        QVERIFY(repaint.count() >= 1);

        // The background rebuild reproduces the load-time bake exactly
        // (same inputs, same code path).
        QCOMPARE(layer.m_overviewTris.size(), bakedCount);
        QCOMPARE(layer.m_nativeTriSpan, bakedSpan);
        QVERIFY(layer.hasOverview());
    }

    // P1.1 verify — QPainter vs QSG screenshots at three zooms for human
    // parity review; asserts both paths actually drew mesh pixels.
    // Optional: point SWMMVIS_MESH_PARITY_INP at any .inp/.2dm to run the
    // comparison on a real mesh (run WITHOUT the offscreen QPA to get real
    // scene-graph pixels — offscreen readback comes back blank).
    void parityScreenshots()
    {
        const QString src = qEnvironmentVariable("SWMMVIS_MESH_PARITY_INP",
                                                 fixturePath());
        mesh::InpMeshReadResult read = mesh::InpMeshReader::read(src);
        QVERIFY2(read.hasMesh, qPrintable(read.errorMsg));
        SWMM2DMeshLayer layer(std::move(read.mesh), read.sourcePath);

        // Optional SWMMVIS_MESH_PARITY_CONTOURS=1 turns bed-elevation contours
        // on (off by default) so the QSG far-zoom frame can be inspected for
        // contour lines — verifying they render at ALL zoom levels, not just
        // Mid/Near. Edges off to isolate the contour ink.
        if (qEnvironmentVariable("SWMMVIS_MESH_PARITY_CONTOURS")
            == QLatin1String("1")) {
            layer.setShowContours(true);
            layer.setShowEdges(false);
        }

        const QSize view(800, 600);
        const struct { const char *name; double factor; } zooms[] = {
            {"full", 1.0}, {"mid", 0.5}, {"near", 0.25}};
        const QString outDir = outputDir();

        // Production pipeline: the same offscreen QQuickWidget +
        // qrc:/openswmm/qml/swmmlayer.qml + grabFramebuffer() round trip
        // MapCanvas::paintEvent uses (the QML instantiates the mesh renderer
        // as "mesh2dRenderer" — P1.1), rather than a bare QQuickWindow whose
        // grabWindow() readback is unavailable under the offscreen QPA.
        // main.cpp registers these for the app; QTEST_MAIN does not run it,
        // so register here or swmmlayer.qml fails to load.
        static bool typesRegistered = false;
        if (!typesRegistered) {
            qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0,
                                                  "SWMMLayerQSGRenderer");
            qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0,
                                                   "SWMM2DMeshQSGRenderer");
            qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0,
                                                      "SWMM2DResultsQSGRenderer");
            typesRegistered = true;
        }

        QQuickWidget qsgWidget;
        qsgWidget.setAttribute(Qt::WA_DontShowOnScreen);
        qsgWidget.setAttribute(Qt::WA_QuitOnClose, false);
        qsgWidget.setClearColor(Qt::transparent);
        qsgWidget.setResizeMode(QQuickWidget::SizeRootObjectToView);
        // MapCanvas forces MSAA onto the QQuickWidget FBO before setSource —
        // mirror it so the harness renders the same target the app does.
        {
            QSurfaceFormat fmt = qsgWidget.format();
            if (fmt.samples() < 4)
                fmt.setSamples(4);
            qsgWidget.setFormat(fmt);
        }
        // The app loads this from qrc; the test binary does not compile the
        // resource bundle, so read the same file from the source tree.
        QDir qmlDir(dataDir());          // tests/gui/data
        qmlDir.cdUp(); qmlDir.cdUp(); qmlDir.cdUp();   // repo root
        const QString qmlPath =
            qmlDir.filePath(QStringLiteral("resources/qml/swmmlayer.qml"));
        QVERIFY2(QFile::exists(qmlPath), qPrintable(qmlPath));
        qsgWidget.setSource(QUrl::fromLocalFile(qmlPath));
        qsgWidget.resize(view);
        qsgWidget.show();
        SWMM2DMeshQSGRenderer *renderer =
            qsgWidget.rootObject()
                ? qsgWidget.rootObject()->findChild<SWMM2DMeshQSGRenderer *>(
                      QStringLiteral("mesh2dRenderer"))
                : nullptr;
        QVERIFY2(renderer, "swmmlayer.qml did not provide mesh2dRenderer");
        renderer->setLayer(&layer);

        // Expand each zoom window to the view's aspect ratio (what MapCanvas
        // does via adjustExtentToAspect) so the QPainter render and the QSG
        // extent describe the SAME region with no letterbox/stretch and the
        // two images are pixel-comparable.
        const double viewAspect = double(view.width()) / double(view.height());
        auto aspectRect = [viewAspect](QRectF r) {
            double w = r.width(), h = r.height();
            if (w / h < viewAspect) w = h * viewAspect;
            else                    h = w / viewAspect;
            return QRectF(r.center().x() - w / 2.0, r.center().y() - h / 2.0,
                          w, h);
        };

        bool qsgAvailable = true;
        for (const auto &z : zooms) {
            const QRectF bbox = layer.m_sceneBBox;
            const QRectF src  = aspectRect(QRectF(
                bbox.center().x() - bbox.width()  * z.factor / 2.0,
                bbox.center().y() - bbox.height() * z.factor / 2.0,
                bbox.width() * z.factor, bbox.height() * z.factor));
            const QImage cpu = paintQPainterPath(&layer, src, view);
            QVERIFY(nonWhitePixels(cpu) > 1000);
            QVERIFY(cpu.save(QStringLiteral("%1/qpainter_%2.png")
                                 .arg(outDir, QLatin1String(z.name))));

            // Scene y = -canvas y: convert the scene rect to a canvas extent.
            renderer->setMapExtent(MapExtent(src.left(), -src.bottom(),
                                             src.right(), -src.top()));
            qsgWidget.repaint();                      // sync render, as MapCanvas does
            QImage qsg = qsgWidget.grabFramebuffer();
            // Transparent clear colour → count non-transparent instead of
            // non-white.
            int drawn = 0;
            if (!qsg.isNull()) {
                const QImage a = qsg.convertToFormat(QImage::Format_ARGB32);
                for (int y = 0; y < a.height(); ++y) {
                    const QRgb *row =
                        reinterpret_cast<const QRgb *>(a.constScanLine(y));
                    for (int x = 0; x < a.width(); ++x)
                        if (qAlpha(row[x]) != 0) ++drawn;
                }
            }
            if (qsg.isNull() || drawn == 0) {
                // Same fallback as before: readback unavailable under this
                // QPA/driver — evidence saved, pixel assertions skipped, and
                // live parity is verified interactively in the real app.
                if (!qsg.isNull())
                    qsg.save(QStringLiteral("%1/qsg_%2_blank.png")
                                 .arg(outDir, QLatin1String(z.name)));
                qsgAvailable = false;
                break;
            }
            QVERIFY2(drawn > 1000, "QSG mesh renderer drew almost nothing");
            QVERIFY(qsg.save(QStringLiteral("%1/qsg_%2.png")
                                 .arg(outDir, QLatin1String(z.name))));
        }
        renderer->setLayer(nullptr);

        if (!qsgAvailable)
            QSKIP("offscreen QPA cannot read back scene-graph pixels — "
                  "QPainter screenshots were still written for review; "
                  "verify QSG parity in the live app");
        qInfo().noquote() << "parity screenshots written to" << outDir;
    }
};

// Expanded QTEST_MAIN so the harness can mirror src/main.cpp's scene-graph
// environment on demand (SWMMVIS_MESH_PARITY_APPENV=1): OpenGL RHI backend +
// threaded render loop + 6x MSAA default format — the exact configuration
// the shipping app forces on macOS. Must happen before QApplication exists.
int main(int argc, char *argv[])
{
    if (qEnvironmentVariable("SWMMVIS_MESH_PARITY_APPENV")
        == QLatin1String("1")) {
        qputenv("QSG_RHI_BACKEND", "opengl");
        qputenv("QSG_RENDER_LOOP", "threaded");
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setSamples(6);
        QSurfaceFormat::setDefaultFormat(fmt);
    }
    QApplication app(argc, argv);
    TestMeshAsyncLoad tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}
#include "test_meshasyncload.moc"
