/**
 * @file      test_2dstyle_render_gaps.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Style controls of the 2D mesh and 2D results sublayers reach what is drawn.
 *
 * Each case is a control that once changed nothing on screen (owner report 2026-11-13: "the
 * mesh edges color is not reflecting in the rendering"), checked on the scene-graph renderer
 * (vertices per colour) and, where the CPU path differed, on a QPainter render:
 *   - the Mesh Edges colour with Boundary Conditions shown (walls recolour boundary edges only);
 *   - edge and results stroke dashes, vertex outlines, stepped contour bands, the no-data
 *     colour, the edge/vertex zoom thresholds saved with the sublayers;
 *   - the Layer Styling dock rules writing only the edited field to the drawn pass;
 *   - results: the hidden terrain row, per-frame depth ranges, vector controls a mode ignores.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "io/mesh2dderivedvariables.h"
#include "io/mesh2dresultvector.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "map/mapextent.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "mesh/meshresult.h"
#include "render/isublayerhost.h"
#include "render/rule.h"
#include "render/rulelist.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshbcsublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "render/sublayers/meshnodesublayer.h"
#include "render/sublayers/resultvectorsublayer.h"
#include "render/sublayers/scalarfillsublayer.h"
#include "render/symbolstyleadapter.h"
#include "ui/dialogs/swmm2dresultsstylepanel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QtTest>

#include <memory>

using namespace OpenSWMM::Render;
using namespace openswmmvis::io;
using V = Mesh2DResultVariable;
using S = Mesh2DValueStatus;

namespace
{
class MeshRenderer : public SWMM2DMeshQSGRenderer
{
public:
    QSGNode *sync(QSGNode *old = nullptr) { return updatePaintNode(old, nullptr); }
};
class ResultsRenderer : public SWMM2DResultsQSGRenderer
{
public:
    QSGNode *sync(QSGNode *old = nullptr) { return updatePaintNode(old, nullptr); }
};

/** @brief Visit every geometry node under root. */
template <typename F> void forEachGeometry(QSGNode *root, F &&visit)
{
    QVector<QSGNode *> pending{root};
    for (qsizetype n = 0; n < pending.size(); ++n) {
        for (auto *c = pending[n]->firstChild(); c; c = c->nextSibling())
            pending.append(c);
        if (pending[n]->type() == QSGNode::GeometryNodeType)
            if (auto *g = static_cast<QSGGeometryNode *>(pending[n]); g->geometry())
                visit(g);
    }
}
/** @return Vertices drawn with a flat material of exactly this colour. */
int flatVertices(QSGNode *root, const QColor &color)
{
    int count = 0;
    forEachGeometry(root, [&](QSGGeometryNode *g) {
        if (auto *m = dynamic_cast<QSGFlatColorMaterial *>(g->material()); m && m->color() == color)
            count += g->geometry()->vertexCount();
    });
    return count;
}
/** @return Per-vertex-coloured vertices of this opaque colour (or of any colour if invalid). */
int coloredVertices(QSGNode *root, const QColor &color = QColor())
{
    int count = 0;
    forEachGeometry(root, [&](QSGGeometryNode *g) {
        if (g->geometry()->attributeCount() != 2)
            return;
        const auto *v = g->geometry()->vertexDataAsColoredPoint2D();
        for (int i = 0; i < g->geometry()->vertexCount(); ++i)
            if (!color.isValid() || (v[i].r == color.red() && v[i].g == color.green() &&
                                     v[i].b == color.blue() && v[i].a == color.alpha()))
                ++count;
    });
    return count;
}
/** @return Per-vertex-coloured vertices that satisfy pred. */
template <typename P> int coloredWhere(QSGNode *root, P &&pred)
{
    int count = 0;
    forEachGeometry(root, [&](QSGGeometryNode *g) {
        if (g->geometry()->attributeCount() != 2)
            return;
        const auto *v = g->geometry()->vertexDataAsColoredPoint2D();
        for (int i = 0; i < g->geometry()->vertexCount(); ++i)
            if (pred(v[i]))
                ++count;
    });
    return count;
}
/** @return Pixels of exactly this colour. */
int pixels(const QImage &image, const QColor &color)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y) == color)
                ++count;
    return count;
}

/** @brief Two triangles on a unit square with bed z = x + y (diagonal (0,0)–(1,1) interior). */
mesh::MeshResult squareMesh()
{
    mesh::MeshResult mesh;
    for (const QPointF &p : {QPointF(0, 0), QPointF(1, 0), QPointF(0, 1), QPointF(1, 1)}) {
        mesh::MeshVertex v;
        v.xy = p;
        v.z = p.x() + p.y();
        mesh.vertices.append(v);
    }
    mesh::MeshTriangle a, b;
    a.v0 = 0, a.v1 = 1, a.v2 = 3;
    b.v0 = 0, b.v1 = 3, b.v2 = 2;
    mesh.triangles = {a, b};
    mesh.ok = true;
    return mesh;
}
/** @brief Show only the given mesh sublayers, every pass drawn at any zoom. */
void showOnly(SWMM2DMeshLayer &layer, std::initializer_list<ISublayer *> shown)
{
    layer.setActiveMesh(true);
    layer.setEdgeZoomMinCellPx(0);
    layer.setVertexZoomMinCellPx(0);
    for (auto *sub : layer.sublayers())
        sub->setVisible(false);
    for (auto *sub : shown)
        sub->setVisible(true);
    layer.meshEdgeSublayer()->edgeStyle()->setUseSlopeDrivenWidth(false);
    layer.meshFillSublayer()->fillStyle()->setHillshadeStrength(0);
}
/** @brief Attach a 360 px renderer to a view of the unit square. */
template <typename R, typename L> void attach(R &renderer, L &layer)
{
    renderer.setWidth(360);
    renderer.setHeight(360);
    renderer.setMapExtent(MapExtent(-.1, -.1, 1.1, 1.1));
    renderer.setLayer(&layer);
}
/** @brief QPainter render of a populated layer over the same view (scene y is flipped). */
template <typename L> QImage paint(L &layer)
{
    QGraphicsScene scene;
    layer.populateScene(&scene, MapExtent(-.1, -.1, 1.1, 1.1), nullptr);
    QImage image(360, 360, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        scene.render(&painter, QRectF(0, 0, 360, 360), QRectF(-.1, -1.1, 1.2, 1.2));
    }
    layer.depopulateScene(&scene);
    return image;
}

V variable(const QString &dataset, const QString &label, const QString &units)
{
    V v;
    v.dataset = dataset;
    v.domain = V::Domain::Surface;
    v.label = label;
    v.units = units;
    v.unitsKnown = true;
    v.temporal = V::Temporal::Reported;
    v.frameCount = 2;
    return v;
}
/** Unit square of two cells: depth 1 and 2 at report time 0, times (t + 1) after; velocity rows. */
class FieldSource : public IMesh2DSource
{
public:
    QVector<V> catalog{variable("Mesh2_face_depth", "Surface — overland flow depth", "m"),
                       variable("Mesh2_face_vx", "Surface — cell velocity X", "m s-1"),
                       variable("Mesh2_face_vy", "Surface — cell velocity Y", "m s-1")};
    QHash<QString, std::vector<float>> values{{"Mesh2_face_depth", {1, 2}},
                                              {"Mesh2_face_vx", {1, 3}},
                                              {"Mesh2_face_vy", {0, 4}}};
    int vertexCount() const override { return 4; }
    int triangleCount() const override { return 2; }
    int timeCount() const override { return 2; }
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &tris) override
    {
        x = {0, 1, 1, 0};
        y = {0, 0, 1, 1};
        z = {0, 0, 0, 0};
        tris = {{0, 1, 2}, {0, 2, 3}};
        return true;
    }
    bool readDepthsAt(int t, std::vector<float> &d) override
    {
        d = values.value("Mesh2_face_depth");
        for (auto &x : d)
            x *= float(t + 1);
        return true;
    }
    QVector<V> faceVariables(QStringList *warnings = nullptr) const override
    {
        if (warnings)
            warnings->clear();
        return catalog + derivedResultVariables(catalog);
    }
    bool readFaceVariableAt(const V &v, int t, std::vector<float> &out,
                            std::vector<S> &status) override
    {
        if (v.derived)
            return readDerivedVariableAt(
                v, t,
                [this](const V &input, int time, std::vector<float> &o, std::vector<S> &s) {
                    return readFaceVariableAt(input, time, o, s);
                },
                out, status);
        if (!values.contains(v.dataset))
            return false;
        out = values.value(v.dataset);
        for (auto &x : out)
            x *= float(t + 1);
        status.assign(out.size(), S::Valid);
        return true;
    }
    QDateTime simTimeAt(int t) const override
    {
        return QDateTime(QDate(2026, 1, 1), QTime(), QTimeZone::UTC).addSecs(t * 60);
    }
    CoordinateReference coordinateReference() const override
    {
        CoordinateReference ref;
        ref.metresPerModelUnit = 1;
        ref.storedUnits = "m";
        ref.declared = true;
        return ref;
    }
};
/** @return The layer's rule by name. */
Rule *ruleNamed(RuleList *rules, const QString &name)
{
    for (int i = 0; rules && i < rules->count(); ++i)
        if (rules->at(i)->name() == name)
            return rules->at(i);
    return nullptr;
}
} // namespace

class Test2DStyleRenderGaps : public QObject
{
    Q_OBJECT

private slots:
    // ── Mesh layer ──────────────────────────────────────────────────────────
    /** @brief The owner's report: the edge colour shows while BC walls are drawn; walls
     *  recolour the boundary edges only (QSG and QPainter). */
    void edgeColourShowsBesideBoundaryWalls()
    {
        SWMM2DMeshLayer layer(squareMesh(), "edge-colour.2dm");
        showOnly(layer, {layer.meshEdgeSublayer(), layer.meshBcSublayer()});
        const QColor red(220, 20, 30), blue(20, 60, 220);
        layer.meshEdgeSublayer()->edgeStyle()->setColor(red);
        layer.meshEdgeSublayer()->edgeStyle()->setLineWidthPx(3);
        auto *bc = layer.meshBcSublayer()->bcStyle();
        bc->setWallVisible(true);
        bc->setWallColor(blue);
        MeshRenderer renderer;
        attach(renderer, layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QCOMPARE(flatVertices(root.get(), red), 6);   // the interior diagonal
        QCOMPARE(flatVertices(root.get(), blue), 24); // four boundary edges
        // The CPU render: the diagonal's midpoint (0.5, 0.5) is pixel (180, 180).
        const QImage image = paint(layer);
        QVERIFY(image.pixelColor(180, 180) == red);
        QVERIFY(pixels(image, blue) > 0);
    }
    /** @brief A dashed edge stroke draws as dashes on QSG (more, shorter segments). */
    void edgeDashesDrawOnQsg()
    {
        SWMM2DMeshLayer layer(squareMesh(), "edge-dash.2dm");
        showOnly(layer, {layer.meshEdgeSublayer()});
        const QColor red(220, 20, 30);
        auto *st = layer.meshEdgeSublayer()->edgeStyle();
        st->setColor(red);
        st->setLineWidthPx(2);
        MeshRenderer renderer;
        attach(renderer, layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        const int solid = flatVertices(root.get(), red);
        QCOMPARE(solid, 30); // five edges, one quad each
        st->setDashPattern(Qt::DashLine);
        root.reset(renderer.sync(root.release()));
        QVERIFY2(flatVertices(root.get(), red) > 4 * solid,
                 qPrintable(QString::number(flatVertices(root.get(), red))));
        st->setDashPattern(Qt::NoPen);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(flatVertices(root.get(), red), 0);
    }
    /** @brief Vertex outlines draw on QSG and on the CPU path. */
    void vertexOutlinesDraw()
    {
        SWMM2DMeshLayer layer(squareMesh(), "vertex-outline.2dm");
        showOnly(layer, {layer.meshNodeSublayer()});
        const QColor green(20, 160, 40), magenta(200, 0, 200);
        auto *st = layer.meshNodeSublayer()->nodeStyle();
        st->setColor(green);
        st->setMarkerSizePx(14);
        st->setOutlineColor(magenta);
        st->setOutlineWidthPx(3);
        MeshRenderer renderer;
        attach(renderer, layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QVERIFY(coloredVertices(root.get(), green) > 0);
        QVERIFY(coloredVertices(root.get(), magenta) > 0);
        QVERIFY(pixels(paint(layer), magenta) > 0);
        st->setOutlineWidthPx(0);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(coloredVertices(root.get(), magenta), 0);
    }
    /** @brief "Interpolate band boundaries" off draws one band polygon per cell. */
    void contourBandsStepWhenNotSmooth()
    {
        SWMM2DMeshLayer layer(squareMesh(), "bands.2dm");
        showOnly(layer, {layer.contourBandSublayer()});
        auto *st = layer.contourBandSublayer()->bandStyle();
        st->setBandCount(4);
        st->setSmoothBands(true);
        MeshRenderer renderer;
        attach(renderer, layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        const int smooth = coloredVertices(root.get());
        st->setSmoothBands(false);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(coloredVertices(root.get()), 6); // two cells, one triangle each
        QVERIFY2(smooth > 6, qPrintable(QString::number(smooth)));
    }
    /** @brief A new no-data colour repaints cells without a value (fill cache key). The fill
     *  shades each cell by its slope, so the test reads the hue, not the exact colour. */
    void noDataColourRepaints()
    {
        SWMM2DMeshLayer layer(squareMesh(), "nodata.2dm"); // Manning's n unset: no data
        showOnly(layer, {layer.meshFillSublayer()});
        auto *st = layer.meshFillSublayer()->fillStyle();
        st->setColorByAttribute(MeshFillStyle::CellAttribute::Mannings);
        const QColor first(10, 200, 10), second(200, 10, 10);
        st->setNoDataColor(first);
        MeshRenderer renderer;
        attach(renderer, layer);
        const auto greenish = [](const QSGGeometry::ColoredPoint2D &v) { return v.g > 2 * v.r; };
        const auto reddish = [](const QSGGeometry::ColoredPoint2D &v) { return v.r > 2 * v.g; };
        std::unique_ptr<QSGNode> root(renderer.sync());
        QCOMPARE(coloredVertices(root.get()), 6);
        QCOMPARE(coloredWhere(root.get(), greenish), 6);
        st->setNoDataColor(second);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(coloredWhere(root.get(), reddish), 6);
    }
    /** @brief The edge / vertex zoom thresholds and the hillshade sun travel with the
     *  sublayers (project and style file). */
    void displaySettingsSaveWithSublayers()
    {
        SWMM2DMeshLayer saved(squareMesh(), "saved.2dm");
        saved.setEdgeZoomMinCellPx(7);
        saved.setVertexZoomMinCellPx(9);
        saved.setHillshadeAzimuth(120);
        const QJsonObject json = ISublayerHost::saveSublayersToJson(saved);
        SWMM2DMeshLayer loaded(squareMesh(), "loaded.2dm");
        ISublayerHost::loadSublayersFromJson(loaded, json);
        QCOMPARE(loaded.edgeZoomMinCellPx(), 7.);
        QCOMPARE(loaded.vertexZoomMinCellPx(), 9.);
        QCOMPARE(loaded.hillshadeAzimuth(), 120.);
    }
    /** @brief A Layer Styling dock edit writes only the edited field, even with the dock
     *  opened before the style changed elsewhere. */
    void dockRuleEditWritesOnlyTheEditedField()
    {
        SWMM2DMeshLayer layer(squareMesh(), "dock.2dm");
        auto *st = layer.meshEdgeSublayer()->edgeStyle();
        st->setColor(QColor(220, 20, 30));
        st->setLineWidthPx(3);
        st->setDashPattern(Qt::DotLine);
        Rule *rule = ruleNamed(layer.ruleList(), "Mesh edges"); // the dock mounts here
        QVERIFY(rule);
        std::unique_ptr<QObject> adapter(SymbolStyleAdapter::createFor(rule));
        auto *edges = qobject_cast<MeshEdgeSymbolStyleAdapter *>(adapter.get());
        QVERIFY(edges);
        QCOMPARE(edges->color(), QColor(220, 20, 30)); // seeded from the live style
        QCOMPARE(edges->lineWidthPx(), 3.);
        edges->setLineWidthPx(5);
        QCOMPARE(st->lineWidthPx(), 5.);
        QCOMPARE(st->color(), QColor(220, 20, 30));
        QCOMPARE(st->dashPattern(), Qt::DotLine);
        // The panel changes the colour while the dock still shows the old one.
        st->setColor(QColor(20, 60, 220));
        edges->setLineWidthPx(6);
        QCOMPARE(st->lineWidthPx(), 6.);
        QCOMPARE(st->color(), QColor(20, 60, 220));
        // Shape and outline reach the vertex style; opacity the sublayer.
        Rule *nodes = ruleNamed(layer.ruleList(), "Mesh nodes");
        std::unique_ptr<QObject> nodeAdapter(SymbolStyleAdapter::createFor(nodes));
        auto *marker = qobject_cast<MeshNodeSymbolStyleAdapter *>(nodeAdapter.get());
        QVERIFY(marker);
        const QColor fill = layer.meshNodeSublayer()->nodeStyle()->color();
        marker->setShape(MarkerShape::Diamond);
        marker->setOpacity(.4);
        QCOMPARE(layer.meshNodeSublayer()->nodeStyle()->shape(), MeshNodeStyle::MarkerShape::Diamond);
        QCOMPARE(layer.meshNodeSublayer()->nodeStyle()->color(), fill);
        QCOMPARE(layer.meshNodeSublayer()->opacity(), .4);
        // Picked band colours replace the named ramp.
        Rule *bands = ruleNamed(layer.ruleList(), "Contour bands");
        std::unique_ptr<QObject> bandAdapter(SymbolStyleAdapter::createFor(bands));
        auto *band = qobject_cast<ContourBandSymbolStyleAdapter *>(bandAdapter.get());
        QVERIFY(band);
        layer.contourBandSublayer()->bandStyle()->setColorRampName("viridis");
        band->setLowColor(QColor(255, 255, 0));
        QCOMPARE(layer.contourBandSublayer()->bandStyle()->lowColor(), QColor(255, 255, 0));
        QVERIFY(layer.contourBandSublayer()->bandStyle()->colorRampName().isEmpty());
    }

    // ── Results layer ───────────────────────────────────────────────────────
    /** @brief The results terrain row draws nothing, so it is not listed. */
    void resultsTerrainRowNotListed()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        QVERIFY(layer.meshFillSublayer());
        QVERIFY(!layer.sublayerListed(layer.meshFillSublayer()));
        QVERIFY(layer.sublayerListed(layer.meshEdgeSublayer()));
    }
    /** @brief Results edges draw dashed and vertex markers draw their outline on QSG. */
    void resultsDashesAndOutlinesDraw()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        layer.setCurrentTimeIndex(0);
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        layer.meshEdgeSublayer()->setVisible(true);
        const QColor red(220, 20, 30);
        auto *edges = layer.meshEdgeSublayer()->edgeStyle();
        edges->setColor(red);
        edges->setLineWidthPx(2);
        ResultsRenderer renderer;
        attach(renderer, layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        const int solid = flatVertices(root.get(), red);
        QVERIFY(solid > 0);
        edges->setDashPattern(Qt::DashLine);
        root.reset(renderer.sync(root.release()));
        QVERIFY2(flatVertices(root.get(), red) > 4 * solid,
                 qPrintable(QString::number(flatVertices(root.get(), red))));

        layer.meshEdgeSublayer()->setVisible(false);
        layer.meshNodeSublayer()->setVisible(true);
        const QColor magenta(200, 0, 200);
        auto *nodes = layer.meshNodeSublayer()->nodeStyle();
        nodes->setMarkerSizePx(14);
        nodes->setOutlineColor(magenta);
        nodes->setOutlineWidthPx(3);
        root.reset(renderer.sync(root.release()));
        QVERIFY(coloredVertices(root.get(), magenta) > 0);
    }
    /** @brief Per-frame auto-stretch classifies over the shown frame, not the run. */
    void perFrameStretchUsesTheShownFrame()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>()); // max depth 2 at t0, 4 at t1
        layer.setCurrentTimeIndex(0);
        ScalarFillStyle style;
        auto scheme = style.scheme();
        scheme.setRangeMode(RangeMode::PerFrameAutoStretch);
        style.setScheme(scheme);
        const auto [lo, hi] = layer.depthFillRange(&style, 0.0);
        QCOMPARE(lo, 0.);
        QCOMPARE(hi, 2.);
        layer.setCurrentTimeIndex(1);
        QCOMPARE(layer.depthFillRange(&style, 0.0).second, 4.);
        // The whole run otherwise; a custom range as set.
        scheme.setRangeMode(RangeMode::FixedOverRun);
        style.setScheme(scheme);
        layer.setCurrentTimeIndex(0);
        QCOMPARE(layer.depthFillRange(&style, 0.0).second, 4.);
        style.setUseCustomRange(true);
        style.setRangeMin(.5);
        style.setRangeMax(1.5);
        QVERIFY(layer.depthFillRange(&style, 0.0) == std::make_pair(.5, 1.5));
    }
    /** @brief The results dock rules write the drawn passes: the velocity row's arrows and
     *  the depth fill being drawn. */
    void resultsDockRulesWriteTheDrawnPasses()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *velocity = layer.velocityResultSublayer();
        QVERIFY(velocity);
        auto *vs = velocity->vectorStyle();
        vs->setMaxLengthPx(33);
        Rule *rule = ruleNamed(layer.ruleList(), "Velocity vectors");
        QVERIFY(rule);
        std::unique_ptr<QObject> adapter(SymbolStyleAdapter::createFor(rule));
        auto *arrows = qobject_cast<VelocityVectorSymbolStyleAdapter *>(adapter.get());
        QVERIFY(arrows);
        QCOMPARE(arrows->glyphLengthMaxPx(), 33.); // seeded from the row
        arrows->setColor(QColor(10, 20, 30));
        QCOMPARE(vs->color(), QColor(10, 20, 30));
        QCOMPARE(vs->colorMode(), ResultVectorStyle::ColorMode::Constant);
        QCOMPARE(vs->maxLengthPx(), 33.);
        arrows->setHeadSizePx(9);
        QCOMPARE(vs->headSizePx(), 9.);

        Rule *ramp = ruleNamed(layer.ruleList(), "Depth color ramp");
        std::unique_ptr<QObject> rampAdapter(SymbolStyleAdapter::createFor(ramp));
        auto *depth = qobject_cast<RasterColorRampSymbolStyleAdapter *>(rampAdapter.get());
        QVERIFY(depth);
        depth->setHighColor(QColor(0, 0, 128));
        auto *fill = layer.cellDepthFillSublayer()->fillStyle();
        QCOMPARE(fill->highColor(), QColor(0, 0, 128));
        QVERIFY(fill->colorRampName().isEmpty());
    }
    /** @brief The vector page greys the controls the current mode ignores. */
    void vectorPanelGreysIgnoredControls()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *velocity = layer.velocityResultSublayer();
        QVERIFY(velocity);
        auto *vs = velocity->vectorStyle();
        openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer);
        QVERIFY(panel.focusResult(velocity->id()));
        const auto find = [&panel](const char *name) { return panel.findChild<QWidget *>(name); };
        auto *constant = find("additionalResultConstantColor");
        auto *customRange = find("additionalResultSizeCustomRange");
        auto *rangeMin = find("additionalResultSizeRangeMin");
        auto *shortest = find("additionalResultShortestArrow");
        QVERIFY(constant && customRange && rangeMin && shortest);

        vs->setColorMode(ResultVectorStyle::ColorMode::Magnitude);
        QVERIFY(!constant->isEnabled());
        vs->setColorMode(ResultVectorStyle::ColorMode::Constant);
        QVERIFY(constant->isEnabled());

        vs->setSizeMode(ResultVectorStyle::SizeMode::FixedScale);
        QVERIFY(!customRange->isEnabled() && !rangeMin->isEnabled() && shortest->isEnabled());
        vs->setSizeMode(ResultVectorStyle::SizeMode::Magnitude);
        vs->setSizeUseCustomRange(false);
        QVERIFY(customRange->isEnabled() && !rangeMin->isEnabled());
        vs->setSizeUseCustomRange(true);
        QVERIFY(rangeMin->isEnabled());
        vs->setSizeMode(ResultVectorStyle::SizeMode::Constant);
        QVERIFY(!shortest->isEnabled() && !rangeMin->isEnabled());
    }
};

QTEST_MAIN(Test2DStyleRenderGaps)
#include "test_2dstyle_render_gaps.moc"
