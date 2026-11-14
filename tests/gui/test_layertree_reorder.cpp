/**
 * @file      test_layertree_reorder.cpp
 * @brief     Layer-tree reordering (Slice LTR-2026-09-19): category headers,
 *         layers and sublayers reorder by drag-drop (exercised through the
 *         model's mimeData/dropMimeData contract) and by the context-menu
 *         paths, and the canvas paint order follows the tree.
 *
 * Plan: workplans/LAYER_TREE_REORDER_PLAN_2026-09-19.md. The repro that
 * motivated the slice is categoryMoveWithEmptyCategoriesPresent(): before
 * the fix the tree row among NON-EMPTY categories was used as a position
 * in the all-categories order, so with any category empty the wrong group
 * moved (usually an empty one — i.e. nothing happened).
 *
 * Runs offscreen against a real MapCanvas and real layers that need no
 * files: SWMM2DResultsLayer (category "SWMM 2D Outputs", an ISublayerHost
 * with 8 sublayers), OpenSWMMVisAnnotationLayer ("Feature Layers") and
 * TabularDataLayer ("Tables").
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMimeData>
#include <QSignalSpy>

#include <algorithm>
#include <memory>
#include <vector>

#include "layers/annotationlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshresult.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "ui/theme/iconfactory.h"
#include <QIcon>
#include <QSvgRenderer>
#include "layers/swmmmodellayer.h"
#include "layers/tabulardatalayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "render/isublayer.h"
#include "render/isublayerhost.h"
#include "ui/panels/layertreecategories.h"
#include "ui/panels/layertreepanel.h"

using openswmmvis::ui::CatFeatureLayers;
using openswmmvis::ui::CatSwmm2DOutputs;
using openswmmvis::ui::CatTables;
using openswmmvis::ui::CatCount;

namespace
{
/** Two cells with an edge-flux feed and no result variables: the 2D results layer lists its
 *  vertex, edge and flow-streak rows flat, its unlisted passes interleaved among them. */
class FluxOnlySource : public IMesh2DSource
{
public:
    int vertexCount() const override { return 4; }
    int triangleCount() const override { return 2; }
    int timeCount() const override { return 1; }
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &tris) override
    {
        x = {0, 1, 1, 0};
        y = {0, 0, 1, 1};
        z = {0, 0, 0, 0};
        tris = {{0, 1, 2}, {0, 2, 3}};
        return true;
    }
    bool readDepthsAt(int, std::vector<float> &depths) override
    {
        depths = {0.f, 0.f};
        return true;
    }
    bool readEdgeFluxAt(int, std::vector<float> &flux) override
    {
        flux.assign(size_t(mesh::edgeSlotCount(triangleCount())), 0.f);
        return true;
    }
};
} // namespace

class TestLayerTreeReorder : public QObject
{
    Q_OBJECT

    // Bottom→top pointer list of the canvas stack, for readable QCOMPAREs.
    /** @brief Copy the canvas paint stack in bottom-to-top order.
     * @param[in] c Borrowed canvas whose layers are inspected.
     * @return Independent list of borrowed layer pointers, with ownership unchanged. */
    static QList<OpenSWMMVisLayer *> stack(const MapCanvas &c) { return c.layers(); }

    // QCOMPARE needs identical pointer types: widen a concrete layer.
    /** @brief Widen a concrete layer reference for pointer comparisons.
     * @param[in] l Borrowed live layer.
     * @return Address of l; never transfers ownership. */
    static OpenSWMMVisLayer *L(OpenSWMMVisLayer &l) { return &l; }

    // A category row's DisplayRole is "<name> (<layer count>)"; the count is
    // asserted through the stack comparisons, so compare on the name alone.
    /** @brief Read a top-level category label without its trailing layer-count suffix.
     * @param[in] m Borrowed layer-tree model.
     * @param[in] row Top-level row requested; invalid rows generally produce empty display text.
     * @return Display label truncated before its last space/open-parenthesis suffix when present after position zero. */
    static QString categoryName(const LayerTreeModel &m, int row)
    {
        const QString label = m.data(m.index(row, 0), Qt::DisplayRole).toString();
        const int paren = label.lastIndexOf(QLatin1String(" ("));
        return paren > 0 ? label.left(paren) : label;
    }

    // Drag `src` and drop it at (row, parent) exactly as QTreeView would.
    /** @brief Drive the tree model MIME move contract without synthesizing mouse events.
     * @param[in,out] m Model supplying MIME data and accepting the move.
     * @param[in] src Source index to package.
     * @param[in] row Drop row, including Qt -1 onto/append semantics.
     * @param[in] parent Drop parent index.
     * @return True only when MIME creation, canDropMimeData and dropMimeData succeed; deletes the temporary MIME object on all non-null paths. */
    static bool dragDrop(LayerTreeModel &m, const QModelIndex &src, int row,
                         const QModelIndex &parent)
    {
        QMimeData *mime = m.mimeData({src});
        if (!mime)
            return false;
        const bool can = m.canDropMimeData(mime, Qt::MoveAction, row, 0, parent);
        const bool ok = can && m.dropMimeData(mime, Qt::MoveAction, row, 0, parent);
        delete mime;
        return ok;
    }

private slots:

    // Default group order puts 2D outputs above feature layers above tables,
    // regardless of the order the layers were added in.
    /** @brief Verify added layers remain grouped in canvas paint order and the tree reverses order within each category. */
    void addLayerKeepsStackGroupedByCategory()
    {
        OpenSWMMVisAnnotationLayer feat1(QStringLiteral("feat1"));
        TabularDataLayer tab1(QStringLiteral("tab1"));
        SWMM2DResultsLayer res1(QStringLiteral("res1"));
        OpenSWMMVisAnnotationLayer feat2(QStringLiteral("feat2"));
        SWMM2DResultsLayer res2(QStringLiteral("res2"));
        MapCanvas canvas;

        canvas.addLayer(&feat1, false); // first layer → index 0
        canvas.addLayer(&tab1, false);  // Tables sits BELOW Feature Layers
        canvas.addLayer(&res1, false);  // 2D outputs go on top
        canvas.addLayer(&feat2, false); // top of ITS group, under results
        canvas.addLayer(&res2, false);  // top of stack

        const QList<OpenSWMMVisLayer *> expected{&tab1, &feat1, &feat2, &res1, &res2};
        QCOMPARE(stack(canvas), expected);

        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();
        QCOMPARE(m->rowCount(), 3);
        QCOMPARE(categoryName(*m, 0), QStringLiteral("SWMM 2D Outputs"));
        QCOMPARE(categoryName(*m, 1), QStringLiteral("Feature Layers"));
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Tables"));
        // Top of tree = top of stack within each group.
        QCOMPARE(m->layerForIndex(m->index(0, 0, m->index(0, 0))), L(res2));
        QCOMPARE(m->layerForIndex(m->index(1, 0, m->index(0, 0))), L(res1));
        QCOMPARE(m->layerForIndex(m->index(0, 0, m->index(1, 0))), L(feat2));
    }

    // The bug: 5 of the 8 categories are empty here. Moving "Tables" up
    // must move the TABLES group, both in the tree and in the stack.
    /** @brief Verify moving a visible category uses visible positions despite empty categories, records full group order and supports undo/redo.
     * Checks repeated/out-of-range moves refuse without changes. */
    void categoryMoveWithEmptyCategoriesPresent()
    {
        SWMM2DResultsLayer res(QStringLiteral("res"));
        OpenSWMMVisAnnotationLayer feat(QStringLiteral("feat"));
        TabularDataLayer tab(QStringLiteral("tab"));
        MapCanvas canvas;
        canvas.addLayer(&res, false);
        canvas.addLayer(&feat, false);
        canvas.addLayer(&tab, false);
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &feat, &res}));

        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Tables"));

        QVERIFY(m->reorderCategory(CatTables, 1)); // "Move Category Up"
        QCOMPARE(categoryName(*m, 0), QStringLiteral("SWMM 2D Outputs"));
        QCOMPARE(categoryName(*m, 1), QStringLiteral("Tables"));
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Feature Layers"));
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&feat, &tab, &res}));

        // Per-project preference recorded on the canvas, Tables now just
        // before Feature Layers, with the empty groups keeping their slots.
        const QVector<int> g = canvas.layerGroupOrder();
        QCOMPARE(g.size(), int(CatCount));
        QCOMPARE(g.indexOf(CatTables) + 1, g.indexOf(CatFeatureLayers));

        // Undo restores the stack and — because the tree is derived from
        // the stack — the tree.
        canvas.undoStack()->undo();
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &feat, &res}));
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Tables"));
        canvas.undoStack()->redo();
        QCOMPARE(categoryName(*m, 1), QStringLiteral("Tables"));

        // Bounds: no-ops return false and change nothing.
        QVERIFY(!m->reorderCategory(CatTables, 1));
        QVERIFY(!m->reorderCategory(CatTables, 3));
        QVERIFY(!m->reorderCategory(CatSwmm2DOutputs, -1));
    }

    /** @brief Verify category MIME moves between rows, onto category rows and to the bottom, while rejecting a layer-row target. */
    void categoryDragDropBetweenAndOntoRows()
    {
        SWMM2DResultsLayer res(QStringLiteral("res"));
        OpenSWMMVisAnnotationLayer feat(QStringLiteral("feat"));
        TabularDataLayer tab(QStringLiteral("tab"));
        MapCanvas canvas;
        canvas.addLayer(&res, false);
        canvas.addLayer(&feat, false);
        canvas.addLayer(&tab, false);
        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();

        // Drag "Tables" (row 2) and drop it BETWEEN row 0 and row 1
        // (parent invalid, row = 1).
        QVERIFY(dragDrop(*m, m->index(2, 0), 1, QModelIndex()));
        QCOMPARE(categoryName(*m, 1), QStringLiteral("Tables"));
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&feat, &tab, &res}));

        // Drop "SWMM 2D Outputs" (row 0) ONTO the last category row
        // ("Feature Layers"). "Onto" means "just above that row": the group
        // lands directly above Feature Layers → Tables, 2D Outputs, Features.
        QVERIFY(dragDrop(*m, m->index(0, 0), -1, m->index(2, 0)));
        QCOMPARE(categoryName(*m, 0), QStringLiteral("Tables"));
        QCOMPARE(categoryName(*m, 1), QStringLiteral("SWMM 2D Outputs"));
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Feature Layers"));
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&feat, &res, &tab}));

        // Drop after the last row (parent invalid, row -1) moves to bottom.
        QVERIFY(dragDrop(*m, m->index(0, 0), -1, QModelIndex()));
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Tables"));

        // A category can never be dropped onto a layer row.
        QMimeData *mime = m->mimeData({m->index(0, 0)});
        QVERIFY(!m->canDropMimeData(mime, Qt::MoveAction, -1, 0, m->index(0, 0, m->index(1, 0))));
        delete mime;
    }

    /** @brief Verify layer MIME reordering stays within its category, rejects cross-category drops and undoes both accepted moves. */
    void layerDragDropStaysInsideItsCategory()
    {
        OpenSWMMVisAnnotationLayer a(QStringLiteral("a"));
        OpenSWMMVisAnnotationLayer b(QStringLiteral("b"));
        OpenSWMMVisAnnotationLayer c(QStringLiteral("c"));
        TabularDataLayer tab(QStringLiteral("tab"));
        MapCanvas canvas;
        canvas.addLayer(&a, false);
        canvas.addLayer(&b, false);
        canvas.addLayer(&c, false);
        canvas.addLayer(&tab, false);
        // stack: tab a b c → tree Feature Layers: c b a ; Tables: tab

        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();
        const QModelIndex feat = m->index(0, 0);
        const QModelIndex tabs = m->index(1, 0);
        QCOMPARE(m->layerForIndex(m->index(0, 0, feat)), L(c));

        // Drag "a" (row 2) and drop between row 0 and 1 → c a b.
        QVERIFY(dragDrop(*m, m->index(2, 0, feat), 1, feat));
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &b, &a, &c}));

        // Drop "c" (row 0) onto the category header → bottom of the group.
        QVERIFY(dragDrop(*m, m->index(0, 0, feat), -1, feat));
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &c, &b, &a}));

        // Cross-category is refused up front (forbidden cursor), never applied.
        QMimeData *mime = m->mimeData({m->index(0, 0, feat)});
        QVERIFY(!m->canDropMimeData(mime, Qt::MoveAction, 0, 0, tabs));
        QVERIFY(!m->dropMimeData(mime, Qt::MoveAction, 0, 0, tabs));
        delete mime;
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &c, &b, &a}));

        // Undo the two drops.
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &a, &b, &c}));
    }

    /** @brief Verify sublayer MIME moves track reversed display/paint order, support three undos and reject other hosts. */
    /*! Owner 2026-11-13: mesh-instance and 2D-output sublayer rows show an icon of what
     *  they draw (terrain, edges, vertices, boundary conditions, couplings, groundwater,
     *  contours of elevation or of depth, vectors, streaks), not one shared glyph per kind. */
    void sublayerRowsShowWhatTheyDraw()
    {
        // Every new icon is a valid SVG in the resources.
        const QStringList icons{
            "MeshTerrain", "MeshEdges", "MeshVertices", "MeshBoundary", "CoupledNodes", "CoupledCells",
            "GroundwaterWell", "ElevationBands", "ElevationIsolines", "DepthFill", "DepthBands",
            "DepthIsolines", "VelocityVectors", "FlowArrows", "FlowStreaks", "WaterSurface",
            "VelocityMagnitude", "Infiltration", "GroundwaterTable", "GroundwaterFlow", "SoilMoisture",
            "Concentration", "Roughness", "BalanceError", "Turbulence", "Exchange", "ResultRamp"};
        for (const QString &alias : icons)
            QVERIFY2(QSvgRenderer(QStringLiteral(":/swmmvis/") + alias).isValid(), qPrintable(alias));

        mesh::MeshResult triangle;
        for (const QPointF p : {QPointF(0, 0), QPointF(10, 0), QPointF(0, 10)}) {
            mesh::MeshVertex v;
            v.xy = p;
            triangle.vertices.append(v);
        }
        mesh::MeshTriangle t;
        t.v0 = 0;
        t.v1 = 1;
        t.v2 = 2;
        triangle.triangles.append(t);
        SWMM2DMeshLayer meshLayer(triangle);
        SWMM2DResultsLayer results(QStringLiteral("res"));
        MapCanvas canvas;
        canvas.addLayer(&meshLayer, false);
        canvas.addLayer(&results, false);
        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();

        const auto picture = [](const QIcon &icon) { return icon.pixmap(24, 24).toImage(); };
        const auto expected = [](const OpenSWMM::Render::ISublayer *s) -> QString {
            const QByteArray name = s->metaObject()->className();
            const auto elevation = [](const QString &a) {
                return a.compare(QLatin1String("elevation"), Qt::CaseInsensitive) == 0;
            };
            if (auto *bands = qobject_cast<const OpenSWMM::Render::ContourBandSublayer *>(s))
                return elevation(bands->bandStyle()->attribute()) ? "ElevationBands" : "DepthBands";
            if (auto *lines = qobject_cast<const OpenSWMM::Render::IsolineSublayer *>(s))
                return elevation(lines->isolineStyle()->attribute()) ? "ElevationIsolines"
                                                                     : "DepthIsolines";
            const QHash<QByteArray, QString> byClass{
                {"OpenSWMM::Render::MeshFillSublayer", "MeshTerrain"},
                {"OpenSWMM::Render::MeshEdgeSublayer", "MeshEdges"},
                {"OpenSWMM::Render::MeshNodeSublayer", "MeshVertices"},
                {"OpenSWMM::Render::MeshBcSublayer", "MeshBoundary"},
                {"OpenSWMM::Render::CoupledNodeSublayer", "CoupledNodes"},
                {"OpenSWMM::Render::CoupledCellSublayer", "CoupledCells"},
                {"OpenSWMM::Render::GroundwaterMarkerSublayer", "GroundwaterWell"},
                {"OpenSWMM::Render::FlowStreakSublayer", "FlowStreaks"},
                {"OpenSWMM::Render::VelocityVectorSublayer", "VelocityVectors"},
                {"OpenSWMM::Render::FlowArrowSublayer", "FlowArrows"},
                {"OpenSWMM::Render::DepthColorRampSublayer", "DepthFill"},
                {"OpenSWMM::Render::CellDepthFillSublayer", "DepthFill"},
                {"OpenSWMM::Render::SmoothDepthFillSublayer", "DepthFill"}};
            return byClass.value(name);
        };
        int checked = 0;
        QSet<QString> seen;
        const std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
            for (int r = 0; r < m->rowCount(parent); ++r) {
                const QModelIndex index = m->index(r, 0, parent);
                if (m->isSublayerIndex(index)) {
                    OpenSWMM::Render::ISublayer *s = m->sublayerForIndex(index);
                    const QString alias = expected(s);
                    QVERIFY2(!alias.isEmpty(), s->metaObject()->className());
                    QVERIFY2(picture(index.data(Qt::DecorationRole).value<QIcon>()) ==
                                 picture(openswmmvis::ui::IconFactory::icon(alias)),
                             qPrintable(s->displayName() + QStringLiteral(" should show ") + alias));
                    seen.insert(alias);
                    ++checked;
                }
                walk(index);
            }
        };
        walk(QModelIndex());
        // The mesh instance's own rows each have their own picture.
        for (const char *alias : {"MeshTerrain", "MeshEdges", "MeshBoundary"})
            QVERIFY2(seen.contains(QLatin1String(alias)), alias);
        QVERIFY2(checked >= 5, qPrintable(QString::number(checked)));
        panel.setCanvas(nullptr);
    }

    void sublayerDropBetweenRowsAndOntoRows()
    {
        SWMM2DResultsLayer res(QStringLiteral("res"));
        res.setSource(std::make_unique<FluxOnlySource>()); // three flat rows (see the source)
        SWMM2DResultsLayer other(QStringLiteral("other")); // used at the end
        MapCanvas canvas;
        canvas.addLayer(&res, false);
        auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(&res);
        QVERIFY(host);
        const QList<OpenSWMM::Render::ISublayer *> before = host->sublayers();
        // The tree shows only listed sublayers; the 2D results layer's depth and
        // velocity passes are unlisted (unified results, 2026-11-01) and stay
        // interleaved in the paint order.
        const auto listedOf = [host](const QList<OpenSWMM::Render::ISublayer *> &subs) {
            QList<OpenSWMM::Render::ISublayer *> out;
            for (auto *s : subs)
                if (host->sublayerListed(s))
                    out.append(s);
            return out;
        };
        const auto listed = listedOf(before);
        const int n = listed.size();
        QVERIFY(n >= 3);

        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();
        const QModelIndex layerIdx = m->index(0, 0, m->index(0, 0));
        QCOMPARE(m->rowCount(layerIdx), n);
        // Display row 0 is the TOP of the paint stack.
        QCOMPARE(m->sublayerForIndex(m->index(0, 0, layerIdx)), listed.last());

        // Drag the top sub-row and drop BETWEEN rows 1 and 2 (parent = the
        // host layer row, row = 2) → it becomes display row 1.
        QVERIFY(dragDrop(*m, m->index(0, 0, layerIdx), 2, layerIdx));
        QList<OpenSWMM::Render::ISublayer *> after = listedOf(host->sublayers());
        QCOMPARE(after[n - 1], listed[n - 2]);
        QCOMPARE(after[n - 2], listed[n - 1]);
        QCOMPARE(m->sublayerForIndex(m->index(1, 0, layerIdx)), listed.last());

        // Drop the bottom sub-row ONTO the top sub-row → takes the top slot.
        QVERIFY(dragDrop(*m, m->index(n - 1, 0, layerIdx), -1, m->index(0, 0, layerIdx)));
        after = listedOf(host->sublayers());
        QCOMPARE(after.last(), listed.first());

        // Drop after the last row (row -1, parent = host) → bottom.
        QVERIFY(dragDrop(*m, m->index(0, 0, layerIdx), -1, layerIdx));
        after = listedOf(host->sublayers());
        QCOMPARE(after.first(), listed.first());
        QCOMPARE(host->sublayers().size(), before.size()); // unlisted passes kept

        // Undoable: three undos restore the original paint order (unlisted
        // passes included) and the sub-rows re-derive from it.
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        QCOMPARE(host->sublayers(), before);
        QCOMPARE(m->sublayerForIndex(m->index(0, 0, layerIdx)), listed.last());

        // A sublayer can't be dropped under a different host.
        canvas.addLayer(&other, false);
        const QModelIndex otherIdx = m->indexForLayer(&other);
        const QModelIndex resIdx = m->indexForLayer(&res);
        QVERIFY(otherIdx.isValid() && resIdx.isValid());
        QMimeData *mime = m->mimeData({m->index(0, 0, resIdx)});
        QVERIFY(!m->canDropMimeData(mime, Qt::MoveAction, 0, 0, otherIdx));
        QVERIFY(!m->canDropMimeData(mime, Qt::MoveAction, -1, 0, m->index(0, 0, otherIdx)));
        delete mime;
    }

    /** @brief Verify canvas sublayer movement emits once per move/undo and rejects no-op, out-of-range and absent-host cases. */
    void canvasMoveSublayerEmitsAndUndoes()
    {
        SWMM2DResultsLayer res(QStringLiteral("res"));
        MapCanvas canvas;
        canvas.addLayer(&res, false);
        auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(&res);
        const auto before = host->sublayers();
        QSignalSpy spy(&canvas, &MapCanvas::sublayerOrderChanged);

        QVERIFY(canvas.moveSublayer(&res, 0, 1));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(host->sublayers()[1], before[0]);
        canvas.undoStack()->undo();
        QCOMPARE(spy.count(), 2);
        QCOMPARE(host->sublayers(), before);

        QVERIFY(!canvas.moveSublayer(&res, 0, 0));
        QVERIFY(!canvas.moveSublayer(&res, 0, before.size()));
        OpenSWMMVisAnnotationLayer notOnCanvas(QStringLiteral("x"));
        QVERIFY(!canvas.moveSublayer(&notOnCanvas, 0, 1));
    }

    // .oswp round-trip contract: the saved keys are applied as layers
    // arrive, in any order, and re-sort the layers already present.
    /** @brief Verify pending saved layer keys reorder existing and late-arriving layers while unknown layers retain grouped insertion. */
    void pendingLayerOrderRestoresSavedStack()
    {
        OpenSWMMVisAnnotationLayer feat(QStringLiteral("feat"));
        SWMM2DResultsLayer res(QStringLiteral("res"));
        TabularDataLayer tab(QStringLiteral("tab"));
        OpenSWMMVisAnnotationLayer feat2(QStringLiteral("feat2")); // late arrival
        MapCanvas canvas;

        // The user had saved the (non-default) order tab < res < feat.
        const QStringList saved{MapCanvas::layerOrderKey(&tab), MapCanvas::layerOrderKey(&res),
                                MapCanvas::layerOrderKey(&feat)};

        // Layers already present before the order is known get re-sorted.
        canvas.addLayer(&res, false);
        canvas.addLayer(&feat, false); // grouped default: feat below res
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&feat, &res}));
        canvas.setPendingLayerOrder(saved);
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&res, &feat}));

        // A late (async) arrival lands in its saved slot, not its group slot.
        canvas.addLayer(&tab, false);
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &res, &feat}));

        // An unknown layer still uses the grouped insert (top of its group).
        canvas.addLayer(&feat2, false);
        QCOMPARE(stack(canvas).last(), L(feat2));
    }

    // ---- Perf harness (hand-off §4) -------------------------------------
    //
    // The grouped insert made a mid-stack VECTOR insert repopulate every
    // vector layer above it (MapCanvas::insertLayer): scene items keep the
    // z they were populated with, so without the rebuild the new layer
    // would not actually draw beneath them. Feature Layers sit BELOW the
    // SWMM model, so every GIS layer added to a loaded project repopulates
    // the model scene once — the cost this measures.
    //
    // A = grouped insert (this slice): each GIS layer lands under the model.
    // B = the pre-slice behavior, append at the top, which repopulates
    //     nothing. B is the same layers on the same canvas, so the
    //     difference is the repopulate and nothing else.
    //
    //   SWMM_PROFILE_INP=<path.inp> [SWMM_PROFILE_GIS_N=8] \
    //     ./build/tests/gui/test_layertree_reorder layerAddRepopulateCost
    //
    // Skips without the variable, so it stays inert in CI — same idiom as
    // profileExternalModel() in test_asyncload.
    /** @brief Optionally compare grouped-below-model insertion with direct-top insertion for the same borrowed layers.
     * Requires SWMM_PROFILE_INP and positive SWMM_PROFILE_GIS_N (default eight), prints millisecond timings without a speed threshold and empties the canvas before layer destruction. */
    void layerAddRepopulateCost()
    {
        const QString inp = qEnvironmentVariable("SWMM_PROFILE_INP");
        if (inp.isEmpty())
            QSKIP("set SWMM_PROFILE_INP=<path.inp> to run the grouped-insert profile");
        QVERIFY2(QFile::exists(inp), qPrintable("SWMM_PROFILE_INP not found: " + inp));
        const int n = qEnvironmentVariable("SWMM_PROFILE_GIS_N", QStringLiteral("8")).toInt();
        QVERIFY(n > 0);

        // Declaration order is load-bearing: every layer must outlive the
        // canvas that holds a bare pointer to it, so the canvas is declared
        // LAST and destroyed FIRST (the same ordering every other case in
        // this file relies on).
        SWMMModelLayer model(inp, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(model.loadModel(warnings, errors), qPrintable(errors.join(QStringLiteral("; "))));

        std::vector<std::unique_ptr<OpenSWMMVisAnnotationLayer>> gis;
        gis.reserve(size_t(n));
        for (int i = 0; i < n; ++i)
            gis.push_back(
                std::make_unique<OpenSWMMVisAnnotationLayer>(QStringLiteral("gis%1").arg(i)));

        MapCanvas canvas;
        canvas.addLayer(&model, false);

        // A — grouped insert. Each lands below the model, which is
        //     depopulated and repopulated every time.
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < n; ++i)
            canvas.addLayer(gis[size_t(i)].get(), false);
        const double groupedMs = double(t.nsecsElapsed()) / 1e6;
        QCOMPARE(canvas.layers().last(), L(model)); // model stayed on top

        // Strip the GIS layers back off, leaving the loaded model alone.
        for (int i = 0; i < n; ++i)
            canvas.takeLayer(canvas.layers().indexOf(gis[size_t(i)].get()), false);
        QCOMPARE(canvas.layerCount(), 1);

        // B — pre-slice behavior: straight onto the top of the stack.
        t.restart();
        for (int i = 0; i < n; ++i)
            canvas.insertLayer(canvas.layerCount(), gis[size_t(i)].get(), false);
        const double topMs = double(t.nsecsElapsed()) / 1e6;

        // Leave the canvas empty so nothing in it dangles at scope exit.
        while (canvas.layerCount() > 0)
            canvas.takeLayer(canvas.layerCount() - 1, false);

        qInfo().noquote() << QStringLiteral("=== PROFILE grouped insert — %1").arg(inp);
        qInfo().noquote() << QStringLiteral("    model: nodes=%1 links=%2 subcatchments=%3")
                                 .arg(model.cachedNodeCount())
                                 .arg(model.cachedLinkCount())
                                 .arg(model.cachedSubcatchCount());
        qInfo().noquote() << QStringLiteral(
                                 "    A  %1 GIS layers BELOW the model (this slice): %2 ms")
                                 .arg(n)
                                 .arg(groupedMs, 0, 'f', 3);
        qInfo().noquote() << QStringLiteral(
                                 "    B  %1 GIS layers on TOP (pre-slice)          : %2 ms")
                                 .arg(n)
                                 .arg(topMs, 0, 'f', 3);
        qInfo().noquote() << QStringLiteral("    added cost: %1 ms total, %2 ms per layer added")
                                 .arg(groupedMs - topMs, 0, 'f', 3)
                                 .arg((groupedMs - topMs) / n, 0, 'f', 3);
    }

    /** @brief Verify wrong-size/duplicate group permutations are ignored and a valid reversed order is accepted. */
    void groupOrderValidation()
    {
        MapCanvas canvas;
        const QVector<int> def = canvas.layerGroupOrder();
        QCOMPARE(def.size(), int(CatCount));
        canvas.setLayerGroupOrder({0, 1, 2}); // wrong size
        QCOMPARE(canvas.layerGroupOrder(), def);
        QVector<int> dup = def;
        dup[0] = dup[1]; // not a permutation
        canvas.setLayerGroupOrder(dup);
        QCOMPARE(canvas.layerGroupOrder(), def);
        QVector<int> rev = def;
        std::reverse(rev.begin(), rev.end());
        canvas.setLayerGroupOrder(rev);
        QCOMPARE(canvas.layerGroupOrder(), rev);
    }

    // Review E-01 (2026-11-12): "Remove Layer" in the tree pushed an undoable
    // RemoveLayerCommand AND deleteLater()'d the layer, so Undo re-inserted a
    // freed object. The layer must survive while the command can restore it.
    /** @brief Verify tree removal keeps the layer alive for undo, and undo/redo restore and remove it. */
    void removeSelectedLayerSurvivesForUndo()
    {
        MapCanvas canvas;
        auto *feat = new OpenSWMMVisAnnotationLayer(QStringLiteral("feat"));
        const QPointer<OpenSWMMVisLayer> guard(feat);
        canvas.addLayer(feat, false);

        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        QVERIFY(panel.focusLayer(feat));
        QCOMPARE(panel.selectedLayer(), static_cast<OpenSWMMVisLayer *>(feat));
        QVERIFY(QMetaObject::invokeMethod(&panel, "onRemoveSelectedLayer"));
        QVERIFY(!canvas.layers().contains(feat));

        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY2(guard, "removed layer was deleted while its RemoveLayerCommand can still undo it");

        canvas.undoStack()->undo();
        QVERIFY(canvas.layers().contains(guard.data()));
        canvas.undoStack()->redo();
        QVERIFY(!canvas.layers().contains(guard.data()));

        // Deleting it here is safe: the command guards the pointer (E-02).
        delete guard.data();
    }

    // Review E-02, owner decision 2026-11-12: a removed layer was never freed.
    // RemoveLayerCommand now frees it once the removal can no longer be undone.
    /** @brief Verify a removed layer is freed when its command is cleared, falls off the undo limit or the canvas closes, and an undone removal is not. */
    void removedLayerIsFreedOnceUndoCannotRestoreIt()
    {
        const auto flush = [] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); };
        {
            MapCanvas canvas;
            QPointer<OpenSWMMVisLayer> cleared(new OpenSWMMVisAnnotationLayer(QStringLiteral("cleared")));
            QPointer<OpenSWMMVisLayer> undone(new OpenSWMMVisAnnotationLayer(QStringLiteral("undone")));
            canvas.addLayer(cleared, false);
            canvas.addLayer(undone, false);
            canvas.takeLayer(canvas.layers().indexOf(cleared.data()));
            canvas.takeLayer(canvas.layers().indexOf(undone.data()));
            canvas.undoStack()->undo(); // "undone" is back on the canvas
            canvas.undoStack()->clear();
            flush();
            QVERIFY2(!cleared, "a removed layer survived the clearing of the stack that could restore it");
            QVERIFY2(undone, "a layer back on the canvas was freed");
            delete canvas.takeLayer(canvas.layers().indexOf(undone.data()), false);
        }
        QPointer<OpenSWMMVisLayer> atClose;
        {
            MapCanvas canvas;
            canvas.undoStack()->setMaxUndoCount(1); // set while the stack is empty
            QPointer<OpenSWMMVisLayer> first(new OpenSWMMVisAnnotationLayer(QStringLiteral("first")));
            atClose = new OpenSWMMVisAnnotationLayer(QStringLiteral("atClose"));
            canvas.addLayer(first, false);
            canvas.addLayer(atClose, false);
            canvas.takeLayer(canvas.layers().indexOf(first.data()));
            canvas.takeLayer(canvas.layers().indexOf(atClose.data())); // pushes "first" off
            flush();
            QVERIFY2(!first, "a removal that fell off the undo limit kept its layer");
            QVERIFY(atClose);
        }
        flush();
        QVERIFY2(!atClose, "a layer still removed when its canvas closed was never freed");
    }

    // Owner decision 2026-11-12: the project's model layer cannot be removed
    // from the layer tree (the extension command already refused it).
    /** @brief Verify the tree's Remove Layer leaves the project's model layer and the undo stack untouched. */
    void modelLayerCannotBeRemoved()
    {
        const QString inp = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
                                .filePath(QStringLiteral("reference_rename_fixture.inp"));
        SWMMModelLayer model(inp, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(model.loadModel(warnings, errors), qPrintable(errors.join(QStringLiteral("; "))));

        MapCanvas canvas;
        canvas.addLayer(&model, false);
        const int steps = canvas.undoStack()->count();

        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        QVERIFY(panel.focusLayer(&model));
        QCOMPARE(panel.selectedLayer(), static_cast<OpenSWMMVisLayer *>(&model));
        QVERIFY(QMetaObject::invokeMethod(&panel, "onRemoveSelectedLayer"));
        QVERIFY2(canvas.layers().contains(&model), "the model layer was removed");
        QCOMPARE(canvas.undoStack()->count(), steps);

        panel.setCanvas(nullptr);
        canvas.takeLayer(canvas.layers().indexOf(&model), false);
    }

    // Review E-20 (2026-11-12): a zoom drag along one pixel row produced a
    // zero-height request that became the canvas extent (later divided by).
    /** @brief Verify a one-dimension-zero request is widened to the view aspect and a point request is refused. */
    void degenerateExtentIsWidenedOrRefused()
    {
        MapCanvas canvas;
        canvas.resize(400, 200); // view aspect 2:1
        canvas.setExtent(MapExtent(0, 0, 100, 50));
        QVERIFY(canvas.extent().isValid());

        canvas.setExtent(MapExtent(10, 20, 30, 20)); // zero height
        QVERIFY(canvas.extent().isValid());
        QCOMPARE(canvas.extent().width(), 20.0);
        QCOMPARE(canvas.extent().height(), 10.0);
        QCOMPARE(canvas.extent().centerY(), 20.0);

        const MapExtent widened = canvas.extent();
        const int steps = canvas.undoStack()->count();
        canvas.setExtent(MapExtent(5, 5, 5, 5)); // a point: nothing to fit
        QVERIFY(canvas.extent() == widened);
        QCOMPARE(canvas.undoStack()->count(), steps);
    }
};

QTEST_MAIN(TestLayerTreeReorder)
#include "test_layertree_reorder.moc"
