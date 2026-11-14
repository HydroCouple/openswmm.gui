/**
 * @file      test_2dstyle_render_gaps.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Style controls of the 2D mesh and 2D results sublayers reach what is drawn.
 *
 * Each case is a control that once changed nothing on screen (owner report 2026-11-13: "the
 * mesh edges color is not reflecting in the rendering"), checked on the scene-graph renderer
 * (vertices per color) and, where the CPU path differed, on a QPainter render:
 *   - the Mesh Edges color with Boundary Conditions shown (walls recolor boundary edges only);
 *   - edge and results stroke dashes, vertex outlines, stepped contour bands, the no-data
 *     color, the edge/vertex zoom thresholds saved with the sublayers;
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
#include "render/rulelistio.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshbcsublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "render/sublayers/meshnodesublayer.h"
#include "render/sublayers/resultvectorsublayer.h"
#include "render/sublayers/scalarfillsublayer.h"
#include "render/classificationcolorsampler.h"
#include "render/classificationscheme.h"
#include "render/sublayers/couplednodesublayer.h"
#include "render/symbolstyle.h"
#include "render/symbolstyleadapter.h"
#include "ui/dialogs/istyleeditorwidget.h"
#include "ui/dialogs/swmm2dresultsstylepanel.h"
#include "ui/widgets/markershapecombo.h"

#include <QCheckBox>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QDir>
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
using openswmmvis::ui::IStyleEditorWidget;
using openswmmvis::ui::StyleEditorRegistry;
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
/** @return Vertices drawn with a flat material of exactly this color. */
int flatVertices(QSGNode *root, const QColor &color)
{
    int count = 0;
    forEachGeometry(root, [&](QSGGeometryNode *g) {
        if (auto *m = dynamic_cast<QSGFlatColorMaterial *>(g->material()); m && m->color() == color)
            count += g->geometry()->vertexCount();
    });
    return count;
}
/** @return Per-vertex-colored vertices of this opaque color (or of any color if invalid). */
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
/** @return Per-vertex-colored vertices that satisfy pred. */
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
/** @return Pixels of exactly this color. */
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
    std::vector<double> bed{0, 0, 0, 0}; // vertex bed elevations
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
        z = bed;
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
/** @return A path in the suite's reviewable output folder (tests/output/...). */
QString outputPath(const QString &name)
{
    const QString dir = QStringLiteral(SWMMVIS_STYLE_GAPS_OUTPUT);
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + name;
}
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
    /** @brief The owner's report: the edge color shows while BC walls are drawn; walls
     *  recolor the boundary edges only (QSG and QPainter). */
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
    /** @brief A new no-data color repaints cells without a value (fill cache key). The fill
     *  shades each cell by its slope, so the test reads the hue, not the exact color. */
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
        // The panel changes the color while the dock still shows the old one.
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
        // Picked band colors replace the named ramp.
        Rule *bands = ruleNamed(layer.ruleList(), "Contour bands");
        std::unique_ptr<QObject> bandAdapter(SymbolStyleAdapter::createFor(bands));
        auto *band = qobject_cast<ContourBandSymbolStyleAdapter *>(bandAdapter.get());
        QVERIFY(band);
        layer.contourBandSublayer()->bandStyle()->setColorRampName("viridis");
        band->setLowColor(QColor(255, 255, 0));
        QCOMPARE(layer.contourBandSublayer()->bandStyle()->lowColor(), QColor(255, 255, 0));
        QVERIFY(layer.contourBandSublayer()->bandStyle()->colorRampName().isEmpty());
    }

    /** @brief Style Manager → Apply (a saved rule list loaded into the layer's list) restores the
     *  saved styles, and the dock still edits the layer afterwards. */
    void styleManagerApplyReachesTheStyles()
    {
        SWMM2DMeshLayer layer(squareMesh(), "style-apply.2dm");
        auto *st = layer.meshEdgeSublayer()->edgeStyle();
        st->setColor(QColor(220, 20, 30));
        st->setLineWidthPx(3);
        const QString path = outputPath("mesh_edges.swmm-rule.json");
        QVERIFY(RuleListIO::save(layer.ruleList(), path).ok);
        st->setColor(QColor(20, 60, 220));
        st->setLineWidthPx(1);
        QVERIFY(RuleListIO::load(path, layer.ruleList()).ok); // what Style Manager → Apply does
        QCOMPARE(st->color(), QColor(220, 20, 30));
        QCOMPARE(st->lineWidthPx(), 3.);
        Rule *rule = ruleNamed(layer.ruleList(), "Mesh edges");
        QVERIFY(rule);
        std::unique_ptr<QObject> adapter(SymbolStyleAdapter::createFor(rule));
        auto *edges = qobject_cast<MeshEdgeSymbolStyleAdapter *>(adapter.get());
        QVERIFY(edges);
        edges->setLineWidthPx(5);
        QCOMPARE(st->lineWidthPx(), 5.);
        QCOMPARE(st->color(), QColor(220, 20, 30));
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
    /** @brief The vector page grays the controls the current mode ignores. */
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

    // ── Round 2 (owner decisions D1–D6) ─────────────────────────────────────
    /** @brief The color scheme's log scale, outside-the-range colors and their JSON. */
    void schemeLogScaleAndOutsideColours()
    {
        ClassificationScheme sc;
        sc.setMode(ClassificationScheme::ClassMode::Continuous);
        sc.setRampName(QString());
        sc.setLowColor(Qt::black);
        sc.setHighColor(Qt::white);
        QCOMPARE(ClassificationScheme::logFloor(0.0, 10.0), 0.01); // three decades
        QCOMPARE(ClassificationScheme::logFloor(2.0, 10.0), 2.0);
        QVERIFY(std::isnan(ClassificationScheme::logFloor(-1.0, 0.0)));
        // Linear: 10 of [0, 100] is 10 % up the ramp; log from 0.1: a third.
        QVERIFY(qAbs(sc.rampPosition(10, 0, 100) - 0.1) < 1e-12);
        sc.setLogScale(true);
        QVERIFY(qAbs(sc.rampPosition(10, 0, 100) - 2.0 / 3.0) < 1e-12);
        QVERIFY(sc.rampPosition(0.05, 0, 100) < 0); // below the floor
        QCOMPARE(sc.colorForValue(0.05, 0, 100).alpha(), 0); // not drawn
        sc.setBelowColor(QColor(0, 200, 0));
        sc.setAboveColor(QColor(200, 0, 0));
        QCOMPARE(sc.colorForValue(0.05, 0, 100), QColor(0, 200, 0));
        sc.setUseCustomRange(true);
        sc.setRangeMin(1);
        sc.setRangeMax(50);
        QCOMPARE(sc.colorForValue(80, 0, 100), QColor(200, 0, 0));
        // Equal-interval classes are log-spaced.
        sc.setMode(ClassificationScheme::ClassMode::Classified);
        sc.setClassCount(2);
        sc.setRangeMin(1);
        sc.setRangeMax(100);
        const auto edges = sc.levelEdges(0, 1000, {});
        QCOMPARE(edges.size(), 3);
        QVERIFY(qAbs(edges[1] - 10.0) < 1e-9);
        QCOMPARE(sc.colorForClassified(0.5, edges), QColor(0, 200, 0));
        QCOMPARE(sc.colorForClassified(500, edges), QColor(200, 0, 0));
        // Saved and read back; files without them read as before.
        const auto back = ClassificationScheme::fromJson(sc.toJson());
        QVERIFY(back == sc);
        QVERIFY(back.logScale());
        QCOMPARE(back.belowColor(), QColor(0, 200, 0));
        QVERIFY(!ClassificationScheme().toJson().contains("logScale"));
        QVERIFY(!ClassificationScheme().toJson().contains("belowColor"));
    }
    /** @brief The mesh fill draws cells below its range in the below color. */
    void meshFillDrawsBelowTheRange()
    {
        auto mesh = squareMesh();
        mesh.triangles[0].mannings = .02; // below the range
        mesh.triangles[1].mannings = .08;
        SWMM2DMeshLayer layer(mesh, "fill-below.2dm");
        showOnly(layer, {layer.meshFillSublayer()});
        auto *st = layer.meshFillSublayer()->fillStyle();
        st->setColorByAttribute(MeshFillStyle::CellAttribute::Mannings);
        auto sc = st->scheme();
        sc.setMode(ClassificationScheme::ClassMode::Continuous);
        sc.setUseCustomRange(true);
        sc.setRangeMin(.05);
        sc.setRangeMax(.1);
        st->setScheme(sc);
        MeshRenderer renderer;
        attach(renderer, layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QCOMPARE(coloredVertices(root.get()), 3); // the cell below the range is not drawn
        sc.setBelowColor(QColor(0, 220, 0));
        st->setScheme(sc);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(coloredVertices(root.get()), 6);
        QCOMPARE(coloredWhere(root.get(), [](const auto &v) { return v.g > 2 * v.r && v.g > 2 * v.b; }), 3);
    }
    /** @brief The results depth fill draws below its range (below color) and log-scales. */
    void resultsDepthFillOutsideAndLog()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>()); // depths 1 and 2 at t0
        layer.setCurrentTimeIndex(0);
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        auto *cell = layer.cellDepthFillSublayer();
        cell->setVisible(true);
        auto *st = cell->fillStyle();
        st->setClassified(false);
        st->setUseCustomRange(true);
        st->setRangeMin(1.5);
        st->setRangeMax(3);
        ResultsRenderer renderer;
        attach(renderer, layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        const int inRange = coloredVertices(root.get());
        QVERIFY(inRange > 0);
        auto sc = st->scheme();
        sc.setBelowColor(QColor(0, 220, 0));
        st->setScheme(sc);
        root.reset(renderer.sync(root.release()));
        QVERIFY(coloredVertices(root.get()) > inRange);
        QVERIFY(coloredWhere(root.get(), [](const auto &v) { return v.g > 2 * v.r && v.g > 2 * v.b; }) > 0);
        // The sampler the fills use follows the log scale over the custom range [1.5, 3].
        sc.setLogScale(true);
        const ClassificationColorSampler sampler(sc, 0, 100);
        QVERIFY(sampler.logScale());
        QVERIFY(qAbs(sampler.position(1.5)) < 1e-12);
        QVERIFY(qAbs(sampler.position(3.0) - 1.0) < 1e-12);
        QVERIFY(qAbs(sampler.position(std::sqrt(1.5 * 3.0)) - 0.5) < 1e-12); // geometric middle
        QVERIFY(qAbs(sampler.valueAt(0.5) - std::sqrt(1.5 * 3.0)) < 1e-9);
    }
    /** @brief Band below / above colors fill the regions outside the band range. */
    void bandsDrawOutsideTheirRange()
    {
        SWMM2DMeshLayer layer(squareMesh(), "bands-outside.2dm"); // bed 0 … 2
        showOnly(layer, {layer.contourBandSublayer()});
        auto *st = layer.contourBandSublayer()->bandStyle();
        st->setSmoothBands(false);
        auto sc = st->scheme();
        sc.setUseCustomRange(true);
        sc.setRangeMin(1.5); // both cells (mean bed 1) lie below
        sc.setRangeMax(2);
        st->setScheme(sc);
        MeshRenderer renderer;
        attach(renderer, layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QCOMPARE(coloredVertices(root.get()), 0);
        st->setBelowMinColor(QColor(0, 220, 0)); // what the dock band rule writes
        QVERIFY(st->scheme().belowColor().isValid());
        root.reset(renderer.sync(root.release()));
        QCOMPARE(coloredWhere(root.get(), [](const auto &v) { return v.g > 2 * v.r && v.g > 2 * v.b; }), 6);
        // The old default above color (never drawn) does not come back from a file.
        QJsonObject old = st->toJson();
        old.insert("aboveMaxColor", "#c8ffffff");
        st->fromJson(old);
        QVERIFY(!st->scheme().aboveColor().isValid());
    }
    /** @brief Results edges widen steep edges (slope emphasis) on QSG and the CPU path, and the
     *  results Edges tab edits it. */
    void resultsSlopeEmphasisDraws()
    {
        auto source = std::make_unique<FieldSource>();
        source->bed = {0, 0, 1, 1}; // edges (1,0)-(1,1) and (0,1)-(0,0) are the steepest
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(source));
        layer.setCurrentTimeIndex(0);
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        layer.meshEdgeSublayer()->setVisible(true);
        auto *es = layer.meshEdgeSublayer()->edgeStyle();
        const QColor red(220, 20, 30), blue(20, 60, 220);
        es->setColor(red);
        es->setLineWidthPx(2);
        es->setUseSlopeDrivenWidth(true);
        es->setSlopeBreak(.9);
        es->setWideColor(blue);
        es->setWideWidthPx(6);
        ResultsRenderer renderer;
        attach(renderer, layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QCOMPARE(flatVertices(root.get(), blue), 12); // two steep edges
        QCOMPARE(flatVertices(root.get(), red), 18);  // three others
        QVERIFY(pixels(paint(layer), blue) > 0);
        es->setUseSlopeDrivenWidth(false);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(flatVertices(root.get(), blue), 0);

        openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer);
        QVERIFY(panel.focusResult(layer.meshEdgeSublayer()->id()));
        auto *check = panel.findChild<QCheckBox *>("resultEdgeSlopeDriven");
        QVERIFY(check);
        check->setChecked(true);
        QVERIFY(es->useSlopeDrivenWidth());
    }
    /** @brief The results dock lists no Hillshade rule (the results draw no terrain). */
    void resultsRulesHaveNoHillshade()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        QCOMPARE(layer.ruleList()->count(), 6);
        QVERIFY(!ruleNamed(layer.ruleList(), "Hillshade"));
        QVERIFY(ruleNamed(layer.ruleList(), "Depth color ramp"));
    }
    /** @brief The color-ramp rules write the log scale, the colors outside the range and (mesh)
     *  the fill's attribute; controls a layer does not draw are grayed in the dock's editor. */
    void dockRampRulesWriteTheNewFields()
    {
        SWMM2DMeshLayer mesh(squareMesh(), "dock-ramp.2dm");
        std::unique_ptr<QObject> adapter(
            SymbolStyleAdapter::createFor(ruleNamed(mesh.ruleList(), "Mesh fill")));
        auto *ramp = qobject_cast<RasterColorRampSymbolStyleAdapter *>(adapter.get());
        QVERIFY(ramp);
        ramp->setUseLogScale(true);
        ramp->setBelowMinColor(QColor(0, 220, 0));
        ramp->setAttribute("mannings");
        auto *fill = mesh.meshFillSublayer()->fillStyle();
        QVERIFY(fill->scheme().logScale());
        QCOMPARE(fill->scheme().belowColor(), QColor(0, 220, 0));
        QCOMPARE(fill->colorByAttribute(), MeshFillStyle::CellAttribute::Mannings);
        ramp->setBelowMinColor(QColor(0, 0, 0, 0)); // transparent: none
        QVERIFY(!fill->scheme().belowColor().isValid());

        SWMM2DResultsLayer results;
        results.setSource(std::make_unique<FieldSource>());
        std::unique_ptr<QObject> depthAdapter(
            SymbolStyleAdapter::createFor(ruleNamed(results.ruleList(), "Depth color ramp")));
        auto *depth = qobject_cast<RasterColorRampSymbolStyleAdapter *>(depthAdapter.get());
        QVERIFY(depth);
        depth->setAboveMaxColor(QColor(200, 0, 0));
        QCOMPARE(results.cellDepthFillSublayer()->fillStyle()->scheme().aboveColor(),
                 QColor(200, 0, 0));
        // Grayed with the reason: the results Attribute; enabled on the mesh layer.
        const auto attributeEdit = [](QWidget *editor) -> QLineEdit * {
            for (auto *edit : editor->findChildren<QLineEdit *>())
                if (!qobject_cast<QAbstractSpinBox *>(edit->parentWidget()))
                    return edit; // the Attribute field, not a spin box's text
            return nullptr;
        };
        std::unique_ptr<IStyleEditorWidget> editor(
            StyleEditorRegistry::instance().createEditorFor(depthAdapter.get(), nullptr));
        QVERIFY(editor);
        auto *attr = attributeEdit(editor.get());
        QVERIFY(attr && !attr->isEnabled() && !attr->toolTip().isEmpty());
        std::unique_ptr<IStyleEditorWidget> meshEditor(
            StyleEditorRegistry::instance().createEditorFor(adapter.get(), nullptr));
        QVERIFY(meshEditor && attributeEdit(meshEditor.get())->isEnabled());
    }
    /** @brief The vertex rule's tagged settings drive the mesh layer's Coupled Vertices row;
     *  on results they are grayed. */
    void dockTaggedSettingsDriveCoupledVertices()
    {
        SWMM2DMeshLayer mesh(squareMesh(), "dock-tagged.2dm");
        mesh.coupledNodeSublayer()->setVisible(true);
        std::unique_ptr<QObject> adapter(
            SymbolStyleAdapter::createFor(ruleNamed(mesh.ruleList(), "Mesh nodes")));
        auto *nodes = qobject_cast<MeshNodeSymbolStyleAdapter *>(adapter.get());
        QVERIFY(nodes);
        QVERIFY(nodes->highlightTagged()); // seeded from the row
        nodes->setTaggedColor(QColor(10, 200, 220));
        nodes->setTaggedSizePx(11);
        nodes->setHighlightTagged(false);
        QCOMPARE(mesh.coupledNodeSublayer()->coupledStyle()->color(), QColor(10, 200, 220));
        QCOMPARE(mesh.coupledNodeSublayer()->coupledStyle()->markerSizePx(), 11.);
        QVERIFY(!mesh.coupledNodeSublayer()->isVisible());

        SWMM2DResultsLayer results;
        results.setSource(std::make_unique<FieldSource>());
        std::unique_ptr<QObject> resultsAdapter(
            SymbolStyleAdapter::createFor(ruleNamed(results.ruleList(), "Mesh nodes")));
        QVERIFY(resultsAdapter->property("ignoredKeys").toMap().contains("taggedColor"));
    }
    /** @brief The dock's vertex Shape list offers only the shapes the 2D renderers draw. */
    void dockVertexShapesAreTheDrawnOnes()
    {
        SWMM2DMeshLayer mesh(squareMesh(), "dock-shapes.2dm");
        std::unique_ptr<QObject> adapter(
            SymbolStyleAdapter::createFor(ruleNamed(mesh.ruleList(), "Mesh nodes")));
        std::unique_ptr<IStyleEditorWidget> editor(
            StyleEditorRegistry::instance().createEditorFor(adapter.get(), nullptr));
        QVERIFY(editor);
        auto *shapes = editor->findChild<openswmmvis::ui::MarkerShapeCombo *>();
        QVERIFY(shapes);
        QCOMPARE(shapes->count(), 4);
        for (int i = 0; i < shapes->count(); ++i)
            QCOMPARE(shapes->itemData(i).toInt(), i); // Circle, Square, Triangle, Diamond
        shapes->setShapeValue(int(MarkerShape::Diamond));
        QCOMPARE(mesh.meshNodeSublayer()->nodeStyle()->shape(), MeshNodeStyle::MarkerShape::Diamond);
    }
    /** @brief Legends list the below/above-range colors that are set, around the class rows. */
    void legendShowsOutsideRangeColours()
    {
        const auto colourOf = [](const LegendSymbolItem &row) {
            return SymbolProps::readColor(row.symbol.layers.value(0).props, "color");
        };
        ClassificationScheme sc;
        sc.setMode(ClassificationScheme::ClassMode::Classified);
        sc.setUseCustomRange(true);
        sc.setRangeMin(1);
        sc.setRangeMax(3);
        const int classRows = int(sc.legendItems(0, 4).size());
        sc.setBelowColor(QColor(0, 200, 0));
        sc.setAboveColor(QColor(200, 0, 0));
        const auto rows = sc.legendItems(0, 4);
        QCOMPARE(int(rows.size()), classRows + 2);
        QCOMPARE(rows.first().label, "< " + sc.formatValue(1));
        QCOMPARE(colourOf(rows.first()), QColor(0, 200, 0));
        QVERIFY(rows.first().classKey.isEmpty()); // not an editable class
        QCOMPARE(rows.last().label, "> " + sc.formatValue(3));
        QCOMPARE(colourOf(rows.last()), QColor(200, 0, 0));

        // Mesh fill (automatic range): a summary row plus the below row.
        SWMM2DMeshLayer mesh(squareMesh(), "legend-outside.2dm");
        auto *fill = mesh.meshFillSublayer()->fillStyle();
        auto fillScheme = fill->scheme();
        fillScheme.setBelowColor(QColor(0, 200, 0));
        fill->setScheme(fillScheme);
        const auto fillRows = mesh.meshFillSublayer()->legendSymbolItems();
        QCOMPARE(int(fillRows.size()), 2);
        QCOMPARE(fillRows.first().label, QStringLiteral("Below range"));
        QCOMPARE(fillRows.first().sublayerId, mesh.meshFillSublayer()->id());

        // Mesh bands: the above row last.
        auto *bands = mesh.contourBandSublayer();
        const int bandRows = int(bands->legendSymbolItems().size());
        bands->bandStyle()->setAboveMaxColor(QColor(200, 0, 0));
        const auto bandLegend = bands->legendSymbolItems();
        QCOMPARE(int(bandLegend.size()), bandRows + 1);
        QCOMPARE(colourOf(bandLegend.last()), QColor(200, 0, 0));
        QCOMPARE(bandLegend.last().sublayerId, bands->id());

        // Results depth fill (custom range): "> max" last, stamped like the class rows.
        SWMM2DResultsLayer results;
        results.setSource(std::make_unique<FieldSource>());
        auto *depth = results.cellDepthFillSublayer();
        auto ds = depth->fillStyle()->scheme();
        ds.setUseCustomRange(true);
        ds.setRangeMin(0.5);
        ds.setRangeMax(2);
        ds.setAboveColor(QColor(200, 0, 0));
        depth->fillStyle()->setScheme(ds);
        const auto depthRows = depth->legendSymbolItems();
        QCOMPARE(depthRows.last().label, "> " + ds.formatValue(2));
        QCOMPARE(depthRows.last().sublayerId, depth->id());
        QCOMPARE(depthRows.last().symbol.opacity, depthRows.first().symbol.opacity);
    }
};

QTEST_MAIN(Test2DStyleRenderGaps)
#include "test_2dstyle_render_gaps.moc"
