/*!
 * \file   test_meshperf_baseline.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Mesh Tiled LOD plan Phase 0 — rendering-performance baseline
 *         harness (workplans/MESH_TILED_LOD_RENDERING_PLAN_2026-07-13.md §6).
 *
 *         Measures, per synthetic perf mesh (tests/perf-data/mesh/, generated
 *         by tests/tools/meshperfgen.cpp — NOT committed, so this test
 *         self-skips when the fixtures are absent, e.g. on CI):
 *           - InpMeshReader::read ms (file parse)
 *           - SWMM2DMeshLayer ctor ms (rebuildSceneGeometry + adjacency)
 *           - QPainter paint ms at full / mid (1/8) / near (1/64) zoom
 *             (the live render path — Layer 2 of MapCanvas::paintEvent)
 *           - QSG mesh renderer first + warm grab ms at the same zooms
 *             (offscreen QQuickWindow; skipped gracefully when the offscreen
 *             platform cannot grab a scene-graph frame)
 *           - scene-cache resident bytes (SceneTri/SceneEdge/SceneNode SoA)
 *
 *         Numbers land in the plan's §6 table. Uses QTEST_MAIN + offscreen
 *         QPA like test_asyncload.
 */
#include "layers/swmm2dmeshlayer.h"
#include "map/mapextent.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "mesh/inpmeshreader.h"

#include <QDir>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QImage>
#include <QLoggingCategory>
#include <QPainter>
#include <QQuickWindow>
#include <QTest>

namespace {

QString perfDir()
{
    return qEnvironmentVariable("SWMMVIS_MESH_PERF_DIR",
                                QStringLiteral("tests/perf-data/mesh"));
}

struct Zoom { const char *name; double factor; };
constexpr Zoom kZooms[] = { {"full", 1.0}, {"mid", 1.0 / 8.0}, {"near", 1.0 / 64.0} };

/*! Scene-space source rect for a zoom factor, centred on the mesh bbox. */
QRectF zoomedSceneRect(const QRectF &bbox, double factor)
{
    const double w = bbox.width() * factor, h = bbox.height() * factor;
    return { bbox.center().x() - w / 2.0, bbox.center().y() - h / 2.0, w, h };
}

} // namespace

class TestMeshPerfBaseline : public QObject
{
    Q_OBJECT

private slots:

    void baseline_data()
    {
        QTest::addColumn<QString>("inpPath");
        const QDir dir(perfDir());
        const QStringList inps = dir.entryList({QStringLiteral("mesh_*tri.inp")},
                                               QDir::Files, QDir::Name);
        for (const QString &f : inps)
            QTest::newRow(qPrintable(f)) << dir.absoluteFilePath(f);
        if (inps.isEmpty())
            QSKIP("no perf meshes present — run mesh_perf_generator first "
                  "(see tests/perf-data/mesh/README.md)");
    }

    void baseline()
    {
        QFETCH(QString, inpPath);

        // ── Parse ────────────────────────────────────────────────────────
        QElapsedTimer t;
        t.start();
        mesh::InpMeshReadResult read = mesh::InpMeshReader::read(inpPath);
        const qint64 readMs = t.elapsed();
        QVERIFY2(read.hasMesh, qPrintable(read.errorMsg));

        const int nTris = read.mesh.triangles.size();

        // ── Scene-geometry build (layer ctor) ───────────────────────────
        t.restart();
        auto *layer = new SWMM2DMeshLayer(std::move(read.mesh), read.sourcePath);
        const qint64 buildMs = t.elapsed();

        const qint64 residentBytes =
              qint64(layer->m_sceneTris.size())  * qint64(sizeof(SWMM2DMeshLayer::SceneTri))
            + qint64(layer->m_sceneEdges.size()) * qint64(sizeof(SWMM2DMeshLayer::SceneEdge))
            + qint64(layer->m_sceneNodes.size()) * qint64(sizeof(SWMM2DMeshLayer::SceneNode))
            + qint64(layer->m_triBBoxes.size() + layer->m_edgeBBoxes.size())
                  * qint64(sizeof(QRectF));

        qInfo().noquote()
            << QStringLiteral("BASELINE %1: tris=%2 read=%3ms build=%4ms "
                              "sceneCaches=%5MB overview=%6")
                   .arg(QFileInfo(inpPath).fileName()).arg(nTris)
                   .arg(readMs).arg(buildMs)
                   .arg(double(residentBytes) / (1024.0 * 1024.0), 0, 'f', 1)
                   .arg(layer->hasOverview() ? "yes" : "no");

        // ── QPainter live path (QGraphicsScene::render) ──────────────────
        const QSize kView(1200, 800);
        {
            QGraphicsScene scene;
            layer->populateScene(&scene, layer->extent(), nullptr);
            QImage img(kView, QImage::Format_ARGB32_Premultiplied);
            for (const Zoom &z : kZooms) {
                const QRectF src = zoomedSceneRect(layer->m_sceneBBox, z.factor);
                img.fill(Qt::white);
                QPainter p(&img);
                t.restart();
                scene.render(&p, QRectF(QPointF(0, 0), QSizeF(kView)), src);
                p.end();
                qInfo().noquote()
                    << QStringLiteral("BASELINE %1: qpainter %2 paint=%3ms")
                           .arg(QFileInfo(inpPath).fileName())
                           .arg(QLatin1String(z.name)).arg(t.elapsed());
            }
            layer->depopulateScene(&scene);
        }

        // ── QSG mesh renderer (offscreen scene-graph grab) ───────────────
        {
            QQuickWindow win;
            win.resize(kView);
            auto *renderer = new SWMM2DMeshQSGRenderer(win.contentItem());
            renderer->setSize(QSizeF(kView));
            renderer->setLayer(layer);

            const MapExtent full = layer->extent();
            bool grabOk = true;
            for (const Zoom &z : kZooms) {
                if (!grabOk) break;
                renderer->setMapExtent(full.scaled(z.factor));
                t.restart();
                QImage first = win.grabWindow();     // build + upload + render
                const qint64 firstMs = t.elapsed();
                if (first.isNull()) { grabOk = false; break; }
                t.restart();
                QImage warm = win.grabWindow();      // warm re-render
                const qint64 warmMs = t.elapsed();
                qInfo().noquote()
                    << QStringLiteral("BASELINE %1: qsg %2 first=%3ms warm=%4ms")
                           .arg(QFileInfo(inpPath).fileName())
                           .arg(QLatin1String(z.name)).arg(firstMs).arg(warmMs);
            }
            renderer->setLayer(nullptr);
            if (!grabOk)
                qInfo().noquote()
                    << QStringLiteral("BASELINE %1: qsg grab unavailable under "
                                      "this QPA — QSG numbers not recorded")
                           .arg(QFileInfo(inpPath).fileName());
        }

        delete layer;
    }
};

QTEST_MAIN(TestMeshPerfBaseline)
#include "test_meshperf_baseline.moc"
