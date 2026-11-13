/**
 * @file      test_unified2dresults.cpp
 * @brief     Unified 2D results rendering (2026-11-01): Surface water / Groundwater rows grouped
 *            into scalar and vector headings, independent scalar displays, arrows and streaks
 *            for every vector result, redundant rows removed.
 *
 * Gates (workplans/UNIFIED_2D_RESULTS_RENDERING_PLAN_2026-10-31.md, rev. 2026-11-01):
 * x/y components pair into vectors and get no scalar rows; the derived magnitude and the
 * unsaturated column average compute correctly; scalar toggles round-trip (old "presentation"
 * JSON still loads); defaults show depth filled contours, velocity streaks, labelled water-table
 * isolines and the column average; the depth and velocity rows drive the optimized passes, which
 * no longer appear as rows; older projects migrate; vector geometry points the right way in
 * scene coordinates and follows its colour binding; the scene graph draws cell fill, filled
 * contours and isolines of one scalar at once. Fixtures land in
 * tests/output/unified_2d_results_2026-11-01/.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/mesh2dderivedvariables.h"
#include "io/mesh2dresultvector.h"
#include "layers/swmm2dresultslayer.h"
#include "map/mapcanvas.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "mesh/meshcellgeom.h"
#include "render/isublayerhost.h"
#include "render/resultscalargeometry.h"
#include "render/sublayers/resultscalarsublayer.h"
#include "render/sublayers/resultvectorsublayer.h"
#include "ui/dialogs/layerstyledialog.h"
#include "ui/dialogs/swmm2dresultsstylepanel.h"
#include "ui/panels/layertreepanel.h"
#include "ui/widgets/classificationeditor.h"

#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMenu>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableView>
#include <QTreeView>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QSGGeometryNode>
#include <QSGOpacityNode>

using namespace openswmmvis::io;
using namespace OpenSWMM::Render;
using V = Mesh2DResultVariable;
using S = Mesh2DValueStatus;

namespace
{
QString outputDir()
{
    const QString dir = QStringLiteral(SWMMVIS_UNIFIED_2D_RESULTS_OUTPUT);
    QDir().mkpath(dir);
    return dir;
}

V variable(const QString &dataset, bool groundwater, const QString &label, const QString &units,
           V::Temporal temporal = V::Temporal::Reported)
{
    V v;
    v.dataset = dataset;
    v.domain = groundwater ? V::Domain::Groundwater : V::Domain::Surface;
    v.label = label;
    v.units = units;
    v.unitsKnown = !units.isEmpty();
    v.temporal = temporal;
    v.frameCount = temporal == V::Temporal::Static ? 0 : 2;
    return v;
}

// Unit square of two triangles with a scripted catalog; every variable has
// the same values at both report times. Derived variables are read like the
// saved and live sources read them.
class FieldSource : public IMesh2DSource
{
public:
    QVector<V> catalog{
        variable("Mesh2_face_depth", false, "Surface — overland flow depth", "m"),
        variable("Mesh2_face_vx", false, "Surface — cell velocity X", "m s-1"),
        variable("Mesh2_face_vy", false, "Surface — cell velocity Y", "m s-1"),
        variable("Mesh2_face_grad_hx", false, "Surface — head gradient dh/dx (unlimited)", "1"),
        variable("Mesh2_face_grad_hy", false, "Surface — head gradient dh/dy (unlimited)", "1"),
        variable("Mesh2_face_z", false, "Surface — bed", "m", V::Temporal::Static),
        variable("Mesh2_face_gw_table_elev", true, "Groundwater — water table elevation", "m"),
        variable("Mesh2_face_gw_hu", true, "Groundwater — unsaturated storage", "m"),
        variable("Mesh2_face_gw_closure", true, "Groundwater — closure", "",
                 V::Temporal::Static),
        variable("Mesh2_face_gw_qx", true, "Groundwater — Darcy velocity x", "m s-1",
                 V::Temporal::Held),
        variable("Mesh2_face_gw_qy", true, "Groundwater — Darcy velocity y", "m s-1",
                 V::Temporal::Held)};
    QHash<QString, std::vector<float>> values{
        {"Mesh2_face_depth", {0, 0}},          {"Mesh2_face_vx", {1, 1}},
        {"Mesh2_face_vy", {0, 0}},             {"Mesh2_face_grad_hx", {0, 0}},
        {"Mesh2_face_grad_hy", {1, 1}},        {"Mesh2_face_z", {10, 10}},
        {"Mesh2_face_gw_table_elev", {7, 9}},  {"Mesh2_face_gw_hu", {.6, .2}},
        {"Mesh2_face_gw_closure", {0, 0}},     {"Mesh2_face_gw_qx", {2e-7, 2e-7}},
        {"Mesh2_face_gw_qy", {0, 0}}};
    bool wholeRun = true; // false: like a live run, no whole-run history
    bool supportsWholeRunRange() const override { return wholeRun; }
    bool edgeFlux = false;    // true: the run carries edge fluxes (all zero)
    bool scaleByTime = false; // true: values at report time t are multiplied by t + 1
    QHash<QString, int> reads; // frame reads per stored dataset
    std::vector<double> bed{0, 0, 0, 0}; // vertex bed elevations
    QString missingDataset; // with missingTime: a frame that cannot be read
    int missingTime = -1;
    bool readEdgeFluxAt(int, std::vector<float> &flux) override
    {
        if (!edgeFlux)
            return false;
        flux.assign(size_t(mesh::edgeSlotCount(triangleCount())), 0.f);
        return true;
    }
    bool storedVelocity = false; // true: serve the datasets as stored face fields (vx/vy)
    bool hasFaceField(const char *dataset) const override
    {
        return storedVelocity && values.contains(QString::fromLatin1(dataset));
    }
    bool readFaceFieldAt(const char *dataset, int, std::vector<float> &out) override
    {
        if (!hasFaceField(dataset))
            return false;
        out = values.value(QString::fromLatin1(dataset));
        return true;
    }
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
    bool readDepthsAt(int, std::vector<float> &d) override
    {
        d = values.value("Mesh2_face_depth");
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
        if (!values.contains(v.dataset) || (v.dataset == missingDataset && t == missingTime))
            return false;
        ++reads[v.dataset];
        out = values.value(v.dataset);
        if (scaleByTime)
            for (auto &x : out)
                x *= float(t + 1);
        status.assign(out.size(), S::Valid);
        return true;
    }
};

// FieldSource's catalog over a strip of 250,001 unit squares (500,002 cells), just above the
// large-mesh default threshold. Every cell carries the first cell's value.
class LargeFieldSource : public FieldSource
{
public:
    static constexpr int kSquares = 250001;
    LargeFieldSource()
    {
        for (auto it = values.begin(); it != values.end(); ++it)
            it->assign(size_t(triangleCount()), it->front());
    }
    int vertexCount() const override { return 2 * (kSquares + 1); }
    int triangleCount() const override { return 2 * kSquares; }
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &tris) override
    {
        x.clear();
        y.clear();
        for (int i = 0; i <= kSquares; ++i) {
            x.insert(x.end(), {double(i), double(i)});
            y.insert(y.end(), {0, 1});
        }
        z.assign(x.size(), 0);
        tris.clear();
        for (int i = 0; i < kSquares; ++i) {
            const int a = 2 * i, b = a + 2, c = a + 3, d = a + 1;
            tris.push_back({a, b, c});
            tris.push_back({a, c, d});
        }
        return true;
    }
};

class Renderer : public SWMM2DResultsQSGRenderer
{
public:
    QSGNode *sync(QSGNode *old = nullptr) { return updatePaintNode(old, nullptr); }
};

/** @brief Count opaque colored vertices (attributes 2) or flat vertices (attributes 1). */
int vertices(QSGNode *root, int attributes)
{
    int count = 0;
    QVector<QSGNode *> pending{root};
    for (qsizetype n = 0; n < pending.size(); ++n) {
        for (auto *c = pending[n]->firstChild(); c; c = c->nextSibling())
            pending.append(c);
        if (pending[n]->type() != QSGNode::GeometryNodeType)
            continue;
        auto *g = static_cast<QSGGeometryNode *>(pending[n])->geometry();
        if (!g || g->attributeCount() != attributes)
            continue;
        if (attributes == 2)
            for (int i = 0; i < g->vertexCount(); ++i)
                count += g->vertexDataAsColoredPoint2D()[i].a > 0;
        else
            count += g->vertexCount();
    }
    return count;
}

/** @return The layer's CPU scene over the unit square, painted into a 100 px image. */
QImage paintCpu(SWMM2DResultsLayer &layer)
{
    QGraphicsScene scene;
    layer.populateScene(&scene, MapExtent(0, 0, 1, 1), nullptr);
    QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        scene.render(&painter, QRectF(0, 0, 100, 100), QRectF(0, -1, 1, 1));
    }
    layer.depopulateScene(&scene);
    return image;
}

/** @return The highest pixel alpha of the image. */
int highestAlpha(const QImage &image)
{
    int alpha = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            alpha = std::max(alpha, qAlpha(image.pixel(x, y)));
    return alpha;
}

/** @return Opacities of every opacity node under the root. */
QList<qreal> nodeOpacities(QSGNode *root)
{
    QList<qreal> out;
    QVector<QSGNode *> pending{root};
    for (qsizetype n = 0; n < pending.size(); ++n) {
        for (auto *c = pending[n]->firstChild(); c; c = c->nextSibling())
            pending.append(c);
        if (pending[n]->type() == QSGNode::OpacityNodeType)
            out << static_cast<QSGOpacityNode *>(pending[n])->opacity();
    }
    return out;
}

ResultScalarSublayer *scalarRow(const SWMM2DResultsLayer &layer, const QString &key)
{
    for (auto *s : layer.sublayers())
        if (auto *r = qobject_cast<ResultScalarSublayer *>(s); r && r->variableKey() == key)
            return r;
    return nullptr;
}

ResultVectorSublayer *vectorRow(const SWMM2DResultsLayer &layer, const QString &key)
{
    for (auto *s : layer.sublayers())
        if (auto *r = qobject_cast<ResultVectorSublayer *>(s); r && r->vectorKey() == key)
            return r;
    return nullptr;
}

const QString kDepth = QStringLiteral("surface:Mesh2_face_depth");
const QString kTable = QStringLiteral("groundwater:Mesh2_face_gw_table_elev");
const QString kMoisture = QStringLiteral("derived:gw_theta_unsat_mean");
const QString kVelocity = QStringLiteral("vector:surface:Mesh2_face_vx|surface:Mesh2_face_vy");
const QString kGradient =
    QStringLiteral("vector:surface:Mesh2_face_grad_hx|surface:Mesh2_face_grad_hy");
const QString kDarcy =
    QStringLiteral("vector:groundwater:Mesh2_face_gw_qx|groundwater:Mesh2_face_gw_qy");

using Panel = openswmmvis::ui::Swmm2DResultsStylePanel;
/** @return Page names of the panel's display tabs, in order. */
QStringList tabPages(const Panel &panel)
{
    QStringList pages;
    if (auto *tabs = panel.findChild<QTabWidget *>("resultDisplayTabs"))
        for (int i = 0; i < tabs->count(); ++i)
            pages << tabs->widget(i)->objectName();
    return pages;
}
/** @return Page name of the shown display tab, or empty. */
QString currentTab(const Panel &panel)
{
    auto *tabs = panel.findChild<QTabWidget *>("resultDisplayTabs");
    return tabs && tabs->currentWidget() ? tabs->currentWidget()->objectName() : QString();
}
/** @return The field tree's model. */
QAbstractItemModel *fieldModel(const Panel &panel)
{
    return panel.findChild<QTreeView *>("additionalResultList")->model();
}
/** @return Id of the selected field-tree item. */
QString currentId(const Panel &panel)
{
    return panel.findChild<QTreeView *>("additionalResultList")
        ->currentIndex()
        .data(Qt::UserRole)
        .toString();
}
/** @return The field-tree item with this id, or an invalid index. */
QModelIndex fieldItem(const Panel &panel, const QString &id)
{
    auto *model = fieldModel(panel);
    const auto hits = model->match(model->index(0, 0), Qt::UserRole, id, 1,
                                   Qt::MatchExactly | Qt::MatchRecursive);
    return hits.isEmpty() ? QModelIndex() : hits.first();
}
} // namespace

class TestUnified2DResults : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Component pairs become vectors; static, unmatched and unit-mismatched never do. */
    void componentsPairIntoVectors()
    {
        auto vx = variable("Mesh2_face_vx", false, "Surface — cell velocity X", "m s-1");
        auto vy = variable("Mesh2_face_vy", false, "Surface — cell velocity Y", "m s-1");
        auto lx = variable("Mesh2_face_grad_hx_lim", false, "lim x", "1");
        auto ly = variable("Mesh2_face_grad_hy_lim", false, "lim y", "1");
        auto cx = variable("Mesh2_face_x", false, "centroid x", "m", V::Temporal::Static);
        auto cy = variable("Mesh2_face_y", false, "centroid y", "m", V::Temporal::Static);
        auto max = variable("Mesh2_face_max_velocity", false, "max", "m s-1");
        auto qx = variable("Mesh2_face_gw_qx", true, "qx", "m s-1");
        auto qy = variable("Mesh2_face_gw_qy", true, "qy", "m d-1"); // units disagree
        const auto vectors = pairResultVectors({vx, vy, lx, ly, cx, cy, max, qx, qy});
        QCOMPARE(vectors.size(), 2);
        QCOMPARE(vectors[0].key(), kVelocity);
        QCOMPARE(vectors[0].label, QStringLiteral("Surface — velocity"));
        QCOMPARE(vectors[1].label, QStringLiteral("Surface — head gradient (slope limited)"));
        QVERIFY(isVectorComponentKey(vectors, vy.key()));
        QVERIFY(!isVectorComponentKey(vectors, cx.key()));
        QCOMPARE(plainVariableFromKey(qx.key()).dataset, qx.dataset);
        QVERIFY(plainVariableFromKey(QStringLiteral("derived:x")).dataset.isEmpty());
    }

    /** @brief |v| and the unsaturated column average (sigma layer mean, hu / thickness,
     * NotApplicable when saturated to the ground). */
    void derivedMagnitudeAndColumnAverage()
    {
        auto layer0 = variable("Mesh2_face_gw_theta_sigma", true, "theta 0", "1");
        layer0.zone = V::Zone::Sigma;
        layer0.layer = 0;
        auto layer1 = layer0;
        layer1.layer = 1;
        const QVector<V> catalog{
            variable("Mesh2_face_vx", false, "vx", "m s-1"),
            variable("Mesh2_face_vy", false, "vy", "m s-1"),
            variable("Mesh2_face_z", false, "bed", "m", V::Temporal::Static),
            variable("Mesh2_face_gw_table_elev", true, "table", "m"),
            variable("Mesh2_face_gw_hu", true, "hu", "m"),
            variable("Mesh2_face_gw_closure", true, "closure", "", V::Temporal::Static),
            layer0,
            layer1};
        const auto derived = derivedResultVariables(catalog);
        V magnitude, moisture;
        for (const auto &d : derived) {
            if (d.dataset.startsWith("magnitude|"))
                magnitude = d;
            if (d.key() == kMoisture)
                moisture = d;
        }
        QVERIFY(magnitude.derived && moisture.derived);
        QCOMPARE(moisture.derivedFrom.size(), 6);
        QCOMPARE(magnitude.units, QStringLiteral("m s-1"));
        QCOMPARE(magnitude.key(), pairResultVectors(catalog).value(0).magnitudeKey());
        const QHash<QString, std::vector<float>> data{
            {"surface:Mesh2_face_vx", {3, 0, 1}},
            {"surface:Mesh2_face_vy", {4, 0, 1}},
            {"surface:Mesh2_face_z", {10, 10, 10}},
            {"groundwater:Mesh2_face_gw_table_elev", {8, 8, 10}},
            {"groundwater:Mesh2_face_gw_hu", {0, .5, 0}},
            {"groundwater:Mesh2_face_gw_closure", {2, 0, 0}},
            {"groundwater:Mesh2_face_gw_theta_sigma:layer:0", {.2f, 0, 0}},
            {"groundwater:Mesh2_face_gw_theta_sigma:layer:1", {.4f, 0, 0}}};
        const auto read = [&](const V &v, int, std::vector<float> &out, std::vector<S> &status) {
            if (!data.contains(v.key()))
                return false;
            out = data.value(v.key());
            status.assign(out.size(), S::Valid);
            if (v.key() == "surface:Mesh2_face_vy")
                status[2] = S::Waterless;
            return true;
        };
        std::vector<float> values;
        std::vector<S> status;
        QVERIFY(readDerivedVariableAt(magnitude, 0, read, values, status));
        QCOMPARE(values[0], 5.f);
        QCOMPARE(status[2], S::Waterless);
        QVERIFY(readDerivedVariableAt(moisture, 0, read, values, status));
        QCOMPARE(status[0], S::Valid);
        QVERIFY(std::abs(values[0] - .3f) < 1e-6f); // sigma layer mean
        QCOMPARE(status[1], S::Valid);
        QVERIFY(std::abs(values[1] - .25f) < 1e-6f); // 0.5 m of water over 2 m
        QCOMPARE(status[2], S::NotApplicable);       // saturated to the ground
    }

    /** @brief Display toggles round-trip; a style saved with one presentation still loads. */
    void scalarTogglesRoundTrip()
    {
        ResultScalarStyle style;
        QVERIFY(style.cellFill() && !style.filledContours() && !style.isolines());
        style.setFilledContours(true);
        style.setIsolines(true);
        style.setSmoothFill(true);
        ResultScalarStyle restored;
        restored.fromJson(style.toJson());
        QVERIFY(restored.cellFill() && restored.smoothFill() && restored.filledContours() &&
                restored.isolines() && !restored.cellLabels());
        QCOMPARE(restored.presentation(), ResultScalarStyle::Presentation::Fill);
        auto legacy = ResultScalarStyle().toJson();
        for (const char *key : {"cellFill", "smoothFill", "filledContours", "isolines", "cellLabels"})
            legacy.remove(QLatin1String(key));
        legacy.insert("presentation", int(ResultScalarStyle::Presentation::Contours));
        restored.fromJson(legacy);
        QVERIFY(!restored.cellFill() && restored.isolines() && !restored.filledContours());
        QCOMPARE(restored.presentation(), ResultScalarStyle::Presentation::Contours);
    }

    /** @brief Only the default fields get rows (no component, magnitude or other vector rows),
     * each shown with its default display; the internal passes follow the depth and velocity
     * rows and are not listed. */
    void rowsDefaultsAndEngines()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        for (const char *component : {"surface:Mesh2_face_vx", "surface:Mesh2_face_vy",
                                      "groundwater:Mesh2_face_gw_qx", "surface:Mesh2_face_grad_hy"})
            QVERIFY2(!scalarRow(layer, component), component);
        auto *depth = scalarRow(layer, kDepth);
        auto *table = scalarRow(layer, kTable);
        auto *moisture = scalarRow(layer, kMoisture);
        auto *velocity = vectorRow(layer, kVelocity);
        auto *darcy = vectorRow(layer, kDarcy);
        QVERIFY(depth && table && moisture && velocity && darcy);
        QVERIFY(!vectorRow(layer, kGradient)); // added by hand when wanted
        QVERIFY(!scalarRow(layer, pairResultVectors(layer.resultVariables())[0].magnitudeKey()));
        QCOMPARE(layer.depthResultSublayer(), depth);
        QCOMPARE(layer.velocityResultSublayer(), velocity);

        QVERIFY(depth->isVisible() && depth->fillStyle()->filledContours() &&
                !depth->fillStyle()->cellFill());
        QVERIFY(velocity->isVisible() && velocity->vectorStyle()->streaks() &&
                !velocity->vectorStyle()->arrows());
        QVERIFY(table->isVisible() && table->fillStyle()->isolines() &&
                table->fillStyle()->contourStyle()->labels() && !table->fillStyle()->cellFill());
        QVERIFY(moisture->isVisible() && moisture->fillStyle()->smoothFill());
        QVERIFY(darcy->isVisible() && darcy->vectorStyle()->arrows() &&
                !darcy->vectorStyle()->streaks());
        QVERIFY(!scalarRow(layer, "groundwater:Mesh2_face_gw_hu"));

        QVERIFY(layer.contourBandSublayer()->isVisible());
        QVERIFY(!layer.isolineSublayer()->isVisible());
        QVERIFY(!layer.cellDepthFillSublayer()->isVisible());
        QVERIFY(!layer.smoothDepthFillSublayer()->isVisible());
        QVERIFY(layer.flowStreakSublayer()->isVisible());
        QVERIFY(!layer.velocityVectorSublayer()->isVisible());
        for (ISublayer *engine :
             std::initializer_list<ISublayer *>{layer.contourBandSublayer(), layer.isolineSublayer(),
                                                layer.cellDepthFillSublayer(),
                                                layer.smoothDepthFillSublayer(),
                                                layer.flowStreakSublayer(),
                                                layer.velocityVectorSublayer()})
            QVERIFY2(!layer.sublayerListed(engine), qPrintable(engine->id()));
        QVERIFY(layer.sublayerListed(depth));

        QCOMPARE(layer.sublayerGroups().size(), 4);
        QCOMPARE(layer.sublayerGroup(velocity), 0); // Surface water — Vector
        QCOMPARE(layer.sublayerGroup(depth), 1);    // Surface water — Scalar
        QCOMPARE(layer.sublayerGroup(darcy), 2);    // Groundwater — Vector
        QCOMPARE(layer.sublayerGroup(moisture), 3); // Groundwater — Scalar (derived)

        // The depth row's toggles and visibility drive the depth passes.
        depth->fillStyle()->setIsolines(true);
        QVERIFY(layer.isolineSublayer()->isVisible());
        depth->fillStyle()->setCellFill(true);
        depth->fillStyle()->setSmoothFill(true);
        QVERIFY(layer.smoothDepthFillSublayer()->isVisible());
        QVERIFY(!layer.cellDepthFillSublayer()->isVisible());
        depth->setOpacity(.3);
        QCOMPARE(layer.contourBandSublayer()->opacity(), .3);
        depth->setVisible(false);
        QVERIFY(!layer.contourBandSublayer()->isVisible() && !layer.isolineSublayer()->isVisible() &&
                !layer.smoothDepthFillSublayer()->isVisible());
        layer.setIsolines(true); // the legacy facade goes through the row
        QVERIFY(depth->isVisible() && layer.isolineSublayer()->isVisible());
        velocity->vectorStyle()->setStreaks(false);
        QVERIFY(!layer.flowStreakSublayer()->isVisible());

        QFile out(QDir(outputDir()).filePath("unified_rows.json"));
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(ISublayerHost::saveSublayersToJson(layer)).toJson());
    }

    /** @brief Above 500,000 cells only the depth row starts shown; the other default rows exist
     * with their default styles, hidden, and their passes stay off until switched on. */
    void largeMeshesShowOnlyDepthByDefault()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<LargeFieldSource>());
        auto *depth = scalarRow(layer, kDepth);
        auto *table = scalarRow(layer, kTable);
        auto *moisture = scalarRow(layer, kMoisture);
        auto *velocity = vectorRow(layer, kVelocity);
        auto *darcy = vectorRow(layer, kDarcy);
        QVERIFY(depth && table && moisture && velocity && darcy);
        QVERIFY(depth->isVisible() && depth->fillStyle()->filledContours());
        QVERIFY(layer.contourBandSublayer()->isVisible());
        QVERIFY(!velocity->isVisible() && velocity->vectorStyle()->streaks());
        QVERIFY(!layer.flowStreakSublayer()->isVisible());
        QVERIFY(!table->isVisible() && table->fillStyle()->isolines());
        QVERIFY(!moisture->isVisible() && moisture->fillStyle()->smoothFill());
        QVERIFY(!darcy->isVisible() && darcy->vectorStyle()->arrows());
        velocity->setVisible(true);
        QVERIFY(layer.flowStreakSublayer()->isVisible());
    }

    /** @brief A project saved before the rows drove the passes translates the passes' state into
     * the depth and velocity rows. */
    void oldProjectMigratesEngineState()
    {
        SWMM2DResultsLayer source;
        source.setSource(std::make_unique<FieldSource>());
        source.contourBandSublayer()->setVisible(false);
        source.smoothDepthFillSublayer()->setVisible(true);
        source.isolineSublayer()->setVisible(true);
        source.velocityVectorSublayer()->setVisible(true);
        source.flowStreakSublayer()->setVisible(false);
        source.smoothDepthFillSublayer()->setOpacity(.4);
        source.isolineSublayer()->setOpacity(.7);
        source.velocityVectorSublayer()->setOpacity(.5);
        auto *arrows = source.velocityVectorSublayer()->vectorStyle();
        arrows->setGlyphLengthScalePxPerMps(15);
        arrows->setDryDepthCutoff(.05);
        arrows->setSpeedMinMps(.1);
        arrows->setSpeedMaxMps(2);
        // An old project: builtin rows only, no catalog record, no marker.
        QJsonArray rows;
        for (const auto &row :
             ISublayerHost::saveSublayersToJson(source).value("sublayers").toArray()) {
            const QString id = row.toObject().value("id").toString();
            if (!id.startsWith("results2d.variable.") && !id.startsWith("results2d.vector."))
                rows.append(row);
        }
        const QJsonObject legacy{{"sublayers", rows}};
        {
            QFile out(QDir(outputDir()).filePath("legacy_engine_rows.json"));
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(QJsonDocument(legacy).toJson());
        }
        SWMM2DResultsLayer old;
        old.setSource(std::make_unique<FieldSource>());
        ISublayerHost::loadSublayersFromJson(old, legacy);
        auto *depth = old.depthResultSublayer();
        auto *velocity = old.velocityResultSublayer();
        QVERIFY(depth && velocity);
        QVERIFY(depth->isVisible());
        QVERIFY(depth->fillStyle()->cellFill() && depth->fillStyle()->smoothFill());
        QVERIFY(depth->fillStyle()->isolines() && !depth->fillStyle()->filledContours());
        QVERIFY(velocity->isVisible() && velocity->vectorStyle()->arrows() &&
                !velocity->vectorStyle()->streaks());
        // The former arrows' meaning carries over: px per m/s, cutoff, speed range.
        const auto *migrated = velocity->vectorStyle();
        QCOMPARE(migrated->sizeMode(), ResultVectorStyle::SizeMode::FixedScale);
        QCOMPARE(migrated->scalePxPerUnit(), 15.);
        QCOMPARE(migrated->dryDepthCutoff(), .05);
        QVERIFY(migrated->colorScheme().useCustomRange());
        QCOMPARE(migrated->colorScheme().rangeMin(), .1);
        QCOMPARE(migrated->colorScheme().rangeMax(), 2.);
        QVERIFY(old.smoothDepthFillSublayer()->isVisible() && old.isolineSublayer()->isVisible());
        QVERIFY(!old.contourBandSublayer()->isVisible());
        QVERIFY(!old.velocityVectorSublayer()->isVisible()); // the row draws the arrows
        // The rows show the opacity of what they now present; the passes keep theirs.
        QCOMPARE(depth->opacity(), .7);
        QCOMPARE(old.smoothDepthFillSublayer()->opacity(), .4);
        QCOMPARE(old.isolineSublayer()->opacity(), .7);
        QCOMPARE(velocity->opacity(), .5);
        QCOMPARE(old.flowStreakSublayer()->opacity(), source.flowStreakSublayer()->opacity());
        QVERIFY(ISublayerHost::saveSublayersToJson(old).contains("unifiedResults"));
    }

    /** @brief A project saved with hidden catalog rows (2026-10-31 builds, no marker) keeps the
     * passes' look: the saved depth row, restored after the passes, must not switch them off
     * before they are translated into its toggles. */
    void catalogRowProjectMigratesEngineState()
    {
        SWMM2DResultsLayer source;
        source.setSource(std::make_unique<FieldSource>());
        source.contourBandSublayer()->setVisible(false);
        source.smoothDepthFillSublayer()->setVisible(true);
        source.isolineSublayer()->setVisible(true);
        // That build also offered the vector components as automatic rows.
        QVERIFY(source.addResultSublayer("surface:Mesh2_face_vx",
                                         "results2d.variable.catalog.surface:Mesh2_face_vx"));
        auto saved = ISublayerHost::saveSublayersToJson(source);
        saved.remove("unifiedResults");
        // That build saved every catalog row hidden, with one presentation, no vector rows,
        // and the rows painting above every pass.
        QJsonArray rows, catalogRows;
        for (const auto &value : saved.value("sublayers").toArray()) {
            auto row = value.toObject();
            const QString id = row.value("id").toString();
            if (id.startsWith("results2d.vector."))
                continue;
            if (!id.startsWith("results2d.variable.")) {
                rows.append(row);
                continue;
            }
            row.insert("visible", false);
            auto style = row.value("style").toObject();
            for (const char *key :
                 {"cellFill", "smoothFill", "filledContours", "isolines", "cellLabels"})
                style.remove(QLatin1String(key));
            style.insert("presentation", 0);
            row.insert("style", style);
            catalogRows.append(row);
        }
        int enginesAt = -1, depthAt = -1;
        for (const auto &row : catalogRows)
            rows.append(row);
        for (int i = 0; i < rows.size(); ++i) {
            const QString id = rows[i].toObject().value("id").toString();
            if (id == "results2d.isolines")
                enginesAt = i;
            if (id.startsWith("results2d.variable.") && id.endsWith(kDepth))
                depthAt = i;
        }
        QVERIFY(enginesAt >= 0 && depthAt > enginesAt); // the depth row loads after the passes
        saved.insert("sublayers", rows);
        QJsonArray offered;
        for (const auto &key : saved.value("resultCatalogKeys").toArray())
            if (!key.toString().startsWith("vector:"))
                offered.append(key);
        saved.insert("resultCatalogKeys", offered);
        {
            QFile out(QDir(outputDir()).filePath("catalog_row_engine_rows.json"));
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(QJsonDocument(saved).toJson());
        }
        SWMM2DResultsLayer old;
        old.setSource(std::make_unique<FieldSource>());
        ISublayerHost::loadSublayersFromJson(old, saved);
        auto *depth = old.depthResultSublayer();
        QVERIFY(depth);
        QVERIFY(old.smoothDepthFillSublayer()->isVisible() && old.isolineSublayer()->isVisible());
        QVERIFY(!old.contourBandSublayer()->isVisible());
        QVERIFY(depth->isVisible());
        QVERIFY(depth->fillStyle()->cellFill() && depth->fillStyle()->smoothFill() &&
                depth->fillStyle()->isolines() && !depth->fillStyle()->filledContours());
        QVERIFY(!scalarRow(old, "surface:Mesh2_face_vx")); // the hidden component row retires
    }

    /** @brief Arrows follow the field (scene Y runs opposite to model Y) and their colour
     * binding; static streaks advance along the field; the velocity row leaves streaks to the
     * animated pass. */
    void vectorGeometryArrowsAndStreaks()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->values["Mesh2_face_gw_qy"] = {3e-7f, 3e-7f};
        owned->values["Mesh2_face_depth"] = {1, 1}; // wet: above the surface dry-depth cutoff
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        auto *darcy = vectorRow(layer, kDarcy);
        QVERIFY(darcy);
        darcy->setVisible(true);
        darcy->vectorStyle()->setStreaks(true);
        darcy->vectorStyle()->setColorMode(ResultVectorStyle::ColorMode::Scalar);
        darcy->vectorStyle()->setColorKey(kTable);
        const QRectF everywhere(-10, -10, 20, 20);
        double lo = 0, hi = 0;
        QString units;
        const auto geometry = layer.resultVectorGeometry(darcy, everywhere, 100, &lo, &hi, &units);
        QVERIFY2(geometry.error.isEmpty(), qPrintable(geometry.error));
        QVERIFY(!geometry.arrows.empty());
        QCOMPARE(lo, 7.);
        QCOMPARE(hi, 9.);
        QCOMPARE(units, QStringLiteral("m"));
        QSet<QRgb> colors;
        for (const auto &arrow : geometry.arrows) {
            QVERIFY(arrow.tip.x() > arrow.tail.x()); // +x
            QVERIFY(arrow.tip.y() < arrow.tail.y()); // +y north is up the screen
            colors.insert(arrow.color.rgba());
        }
        QCOMPARE(colors.size(), 2); // one colour per cell's water table
        QVERIFY(!geometry.streaks.empty());
        for (const auto &streak : geometry.streaks)
            QVERIFY(streak.points.back().x() > streak.points.front().x());

        auto *velocity = layer.velocityResultSublayer();
        velocity->vectorStyle()->setArrows(true);
        const auto surface = layer.resultVectorGeometry(velocity, everywhere, 100);
        QVERIFY(!surface.arrows.empty());
        QVERIFY(surface.streaks.empty()); // animated by the flow-streak pass
    }

    /** @brief One scalar row draws its cell fill, filled contours and isolines together. */
    void sceneGraphDrawsAllScalarDisplaysAtOnce()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        layer.setVisible(true);
        for (auto *s : layer.sublayers())
            s->setVisible(false);
        auto *table = scalarRow(layer, kTable);
        QVERIFY(table);
        table->setVisible(true);
        table->fillStyle()->setCellFill(true);
        table->fillStyle()->setIsolines(false);
        Renderer renderer;
        renderer.setWidth(100);
        renderer.setHeight(100);
        renderer.setMapExtent(MapExtent(0, 0, 1, 1));
        renderer.setLayer(&layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        const int fillOnly = vertices(root.get(), 2);
        QCOMPARE(fillOnly, 6);
        table->fillStyle()->setFilledContours(true);
        table->fillStyle()->setIsolines(true);
        root.reset(renderer.sync(root.release()));
        QVERIFY(vertices(root.get(), 2) > fillOnly); // + filled contour bands
        QVERIFY(vertices(root.get(), 1) > 0);        // + isoline strokes
    }

    /** @brief A display's Show box is the row's toggle: it follows edits made elsewhere, shows a
     * hidden row when turned on, and the hidden passes follow (one model). */
    void displayTabsShareRowToggles()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        Panel panel(&layer);
        auto *depth = layer.depthResultSublayer();
        QVERIFY(panel.focusResult(QStringLiteral("results2d.isolines")));
        QCOMPARE(currentId(panel), depth->id());
        QCOMPARE(currentTab(panel), QStringLiteral("resultContourLinesTab"));
        auto *isolines = panel.findChild<QCheckBox *>("additionalResultIsolines");
        QVERIFY(isolines && !isolines->isChecked());
        isolines->setChecked(true);
        QVERIFY(depth->fillStyle()->isolines());
        QVERIFY(layer.isolineSublayer()->isVisible());
        depth->setVisible(false);
        QVERIFY(!panel.findChild<QCheckBox *>("additionalResultVisible")->isChecked());
        QVERIFY(!layer.isolineSublayer()->isVisible());
        depth->fillStyle()->setIsolines(false); // set elsewhere: the box follows
        QVERIFY(!isolines->isChecked());
        isolines->setChecked(true); // a display turned on shows its row
        QVERIFY(depth->isVisible() && layer.isolineSublayer()->isVisible());

        auto *velocity = layer.velocityResultSublayer();
        QVERIFY(panel.focusResult(QStringLiteral("results2d.flowStreaks")));
        QCOMPARE(currentId(panel), velocity->id());
        QCOMPARE(currentTab(panel), QStringLiteral("resultStreaksTab"));
        QVERIFY(panel.findChild<QWidget *>("flowStreakTab")); // the animated streak editor
        auto *streaks = panel.findChild<QCheckBox *>("additionalResultStreaks");
        QVERIFY(streaks && streaks->isChecked());
        streaks->setChecked(false);
        QVERIFY(!velocity->vectorStyle()->streaks());
        QVERIFY(!layer.flowStreakSublayer()->isVisible());
    }

    /** @brief The field groups, headings and row order are the layer tree's (a second row of one
     * field included); the Layer group comes last. */
    void resultFieldTreeMirrorsLayerTree()
    {
        SWMM2DResultsLayer layer(QStringLiteral("2D Results"));
        layer.setSource(std::make_unique<FieldSource>());
        QVERIFY(layer.addResultSublayer(kDepth));
        MapCanvas canvas;
        canvas.addLayer(&layer, false);
        LayerTreePanel tree;
        tree.setCanvas(&canvas);
        LayerTreeModel *m = tree.model();
        const QModelIndex layerIdx = m->indexForLayer(&layer);
        QStringList expected;
        for (int r = 0; r < m->rowCount(layerIdx); ++r) {
            const QModelIndex heading = m->index(r, 0, layerIdx);
            if (!m->isSublayerGroupIndex(heading))
                continue;
            expected << heading.data().toString();
            for (int c = 0; c < m->rowCount(heading); ++c)
                expected << m->sublayerForIndex(m->index(c, 0, heading))->id();
        }
        QVERIFY(expected.size() > 8);
        Panel panel(&layer);
        auto *model = fieldModel(panel);
        QStringList fields, layerItems;
        for (int r = 0; r < model->rowCount(); ++r) {
            const QModelIndex heading = model->index(r, 0);
            const bool last = r + 1 == model->rowCount();
            if (!last)
                fields << heading.data().toString();
            for (int c = 0; c < model->rowCount(heading); ++c)
                (last ? layerItems : fields) << model->index(c, 0, heading).data(Qt::UserRole).toString();
        }
        QCOMPARE(fields, expected);
        QCOMPARE(model->index(model->rowCount() - 1, 0).data().toString(), QStringLiteral("Layer"));
        QCOMPARE(layerItems, (QStringList{"results2d.waterVisibility", "results2d.meshVertices",
                                          "results2d.meshEdges"}));
        // A heading edits nothing: arrow keys step over it, and a click on it selects the
        // group's first field.
        auto *view = panel.findChild<QTreeView *>("additionalResultList");
        const QModelIndex first = model->index(0, 0), second = model->index(1, 0);
        const QString lastOfFirst =
            model->index(model->rowCount(first) - 1, 0, first).data(Qt::UserRole).toString();
        const QString firstOfSecond = model->index(0, 0, second).data(Qt::UserRole).toString();
        QVERIFY(panel.focusResult(lastOfFirst));
        QTest::keyClick(view, Qt::Key_Down);
        QTRY_COMPARE(currentId(panel), firstOfSecond);
        QTest::keyClick(view, Qt::Key_Up);
        QTRY_COMPARE(currentId(panel), lastOfFirst);
        view->setCurrentIndex(second);
        QTRY_COMPARE(currentId(panel), firstOfSecond);
    }

    /** @brief Tree checks follow visibility both ways without flipping a row back, and a row
     * re-keyed to another domain moves under its heading. */
    void resultFieldTreeChecksFollowVisibility()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *bed = layer.addResultSublayer(QStringLiteral("surface:Mesh2_face_z")); // by hand
        QVERIFY(bed);
        bed->setVisible(false);
        Panel panel(&layer);
        const auto checked = [&panel](const QString &id) {
            return fieldItem(panel, id).data(Qt::CheckStateRole).toInt() == Qt::Checked;
        };
        QVERIFY(!checked(bed->id()));
        QVERIFY(panel.focusResult(bed->id()));
        panel.findChild<QCheckBox *>("additionalResultIsolines")->setChecked(true);
        QVERIFY(bed->isVisible()); // shown, and not flipped back by the tree
        QVERIFY(checked(bed->id()));
        fieldModel(panel)->setData(fieldItem(panel, bed->id()), Qt::Unchecked, Qt::CheckStateRole);
        QVERIFY(!bed->isVisible());
        auto *edges = layer.meshEdgeSublayer();
        edges->setVisible(!edges->isVisible());
        QCOMPARE(checked(edges->id()), edges->isVisible());
        bed->setVariableKey(kTable);
        QCOMPARE(fieldItem(panel, bed->id()).parent().data().toString(),
                 QStringLiteral("Groundwater — Scalar"));
        QCOMPARE(currentId(panel), bed->id());
    }

    /** @brief Tabs follow the field kind, and the depth/velocity tabs go to the rows that present
     * the passes (a second row of the field gets the generic tabs until it presents them). */
    void resultTabsFollowFieldKind()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *depth = layer.depthResultSublayer();
        auto *velocity = layer.velocityResultSublayer();
        auto *depth2 = layer.addResultSublayer(kDepth);
        auto *velocity2 = layer.addResultVectorSublayer(kVelocity);
        QVERIFY(depth && velocity && depth2 && velocity2);
        QVERIFY(layer.depthResultSublayer() == depth && layer.velocityResultSublayer() == velocity);
        Panel panel(&layer);
        const QStringList scalar{"resultFillTab", "resultFilledContoursTab",
                                 "resultContourLinesTab", "resultLabelsTab", "resultDataTab"};
        const QStringList vector{"resultArrowsTab", "resultStreaksTab", "resultColourTab",
                                 "resultDataTab"};
        using openswmmvis::ui::ClassificationEditor;
        QVERIFY(panel.focusResult(depth->id()));
        QCOMPARE(tabPages(panel), scalar);
        QVERIFY(panel.findChild<QStackedWidget *>("depthFillPasses"));
        QVERIFY(!panel.findChild<ClassificationEditor *>("additionalResultClassification"));
        QVERIFY(panel.focusResult(depth2->id()));
        QCOMPARE(tabPages(panel), scalar);
        QVERIFY(!panel.findChild<QStackedWidget *>("depthFillPasses"));
        QVERIFY(panel.findChild<ClassificationEditor *>("additionalResultClassification"));
        QVERIFY(panel.focusResult(velocity->id()));
        QCOMPARE(tabPages(panel), vector);
        QVERIFY(panel.findChild<QWidget *>("flowStreakTab"));
        QVERIFY(panel.focusResult(velocity2->id()));
        QCOMPARE(tabPages(panel), vector);
        QVERIFY(!panel.findChild<QWidget *>("flowStreakTab"));
        // The shown second depth row presents the passes once the first goes.
        QVERIFY(panel.focusResult(depth2->id()));
        QVERIFY(layer.removeResultSublayer(depth->id()));
        QCOMPARE(layer.depthResultSublayer(), depth2);
        QCOMPARE(currentId(panel), depth2->id());
        QVERIFY(panel.findChild<QStackedWidget *>("depthFillPasses"));
    }

    /** @brief The depth Fill tab edits the pass being drawn; the other pass's page is disabled,
     * and a draft left on it is dropped. */
    void depthFillTabFollowsSmoothChoice()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *depth = layer.depthResultSublayer();
        ClassificationScheme manual;
        manual.setMode(ClassificationScheme::ClassMode::Classified);
        manual.setUseCustomRange(true);
        manual.setRangeMin(0);
        manual.setRangeMax(10);
        manual.setMethod(BinMethod::Manual);
        manual.setManualBreaks({2, 5});
        layer.cellDepthFillSublayer()->fillStyle()->setScheme(manual);
        depth->fillStyle()->setSmoothFill(false);
        Panel panel(&layer);
        QVERIFY(panel.focusResult(QStringLiteral("results2d.cellDepthFill")));
        QCOMPARE(currentTab(panel), QStringLiteral("resultFillTab"));
        auto *passes = panel.findChild<QStackedWidget *>("depthFillPasses");
        QVERIFY(passes);
        QCOMPARE(passes->currentIndex(), 0);
        QVERIFY(!passes->widget(1)->isEnabled());
        // The fill is off by default, yet Smooth still picks the pass to edit.
        QVERIFY(!depth->fillStyle()->cellFill());
        QVERIFY(panel.findChild<QCheckBox *>("additionalResultSmoothFill")->isEnabled());
        auto *cell = passes->widget(0)->findChild<openswmmvis::ui::ClassificationEditor *>();
        auto *table = cell ? cell->findChild<QTableView *>() : nullptr;
        QVERIFY(table && table->model()->rowCount() > 1);
        const QString original = table->model()->index(0, 1).data().toString();
        QVERIFY(table->model()->setData(table->model()->index(0, 1), QStringLiteral("oops")));
        QVERIFY(!cell->hasValidDraft());
        depth->fillStyle()->setCellFill(true);
        depth->fillStyle()->setSmoothFill(true);
        QCOMPARE(passes->currentIndex(), 1);
        QVERIFY(!passes->widget(0)->isEnabled() && cell->hasValidDraft());
        depth->fillStyle()->setSmoothFill(false);
        QCOMPARE(passes->currentIndex(), 0);
        QVERIFY(cell->isEnabled() && cell->hasValidDraft()); // the left draft was dropped
        QCOMPARE(table->model()->index(0, 1).data().toString(), original);
    }

    /** @brief A pass's own opacity survives hiding and showing its row; the row's opacity, set in
     * the header, then resets it, and the pass's control follows. */
    void depthPassOpacityFollowsRowOpacity()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *depth = layer.depthResultSublayer();
        Panel panel(&layer);
        QVERIFY(panel.focusResult(QStringLiteral("results2d.bands")));
        auto *page = panel.findChild<QTabWidget *>("resultDisplayTabs")->currentWidget();
        QDoubleSpinBox *pass = nullptr;
        for (auto *spin : page->findChildren<QDoubleSpinBox *>())
            if (spin->maximum() == 1.0) {
                pass = spin;
                break;
            }
        QVERIFY(pass);
        pass->setValue(.5);
        QCOMPARE(layer.contourBandSublayer()->opacity(), .5);
        depth->setVisible(false);
        depth->setVisible(true);
        QCOMPARE(layer.contourBandSublayer()->opacity(), .5);
        QCOMPARE(pass->value(), .5);
        panel.findChild<QDoubleSpinBox *>("additionalResultOpacity")->setValue(80);
        QCOMPARE(layer.contourBandSublayer()->opacity(), .8);
        QCOMPARE(pass->value(), .8);
    }

    /** @brief Pass ids open the presenting row on the tab that edits the pass; rows and layer
     * items open themselves; unknown ids leave the selection; the dialog routes the same way. */
    void focusResultRoutesRowsPassesAndLayerItems()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        Panel panel(&layer);
        const QString depth = layer.depthResultSublayer()->id();
        const QString velocity = layer.velocityResultSublayer()->id();
        const QList<QStringList> routes{
            {"results2d.cellDepthFill", depth, "resultFillTab"},
            {"results2d.smoothDepthFill", depth, "resultFillTab"},
            {"results2d.bands", depth, "resultFilledContoursTab"},
            {"results2d.isolines", depth, "resultContourLinesTab"},
            {"results2d.flowStreaks", velocity, "resultStreaksTab"},
            {"results2d.velocity", velocity, "resultArrowsTab"},
            {"results2d.meshEdges", "results2d.meshEdges", ""},
            {"results2d.waterVisibility", "results2d.waterVisibility", ""}};
        for (const auto &route : routes) {
            // Start each route from the Data tab, so the route itself must pick the tab.
            if (auto *tabs = panel.findChild<QTabWidget *>("resultDisplayTabs"))
                tabs->setCurrentIndex(tabs->count() - 1);
            QVERIFY2(panel.focusResult(route[0]), qPrintable(route[0]));
            QCOMPARE(currentId(panel), route[1]);
            if (!route[2].isEmpty())
                QCOMPARE(currentTab(panel), route[2]);
        }
        QVERIFY(!panel.focusResult(QStringLiteral("results2d.mesh")));
        QVERIFY(!panel.focusResult(QStringLiteral("nope")));
        QCOMPARE(currentId(panel), QStringLiteral("results2d.waterVisibility"));
        openswmmvis::ui::LayerStyleDialog dialog(&layer, QStringLiteral("results2d.bands"));
        auto *opened = dialog.findChild<Panel *>();
        QVERIFY(opened);
        QCOMPARE(currentId(*opened), depth);
        QCOMPARE(currentTab(*opened), QStringLiteral("resultFilledContoursTab"));
    }

    /** @brief Add field offers every scalar and vector field of the run, adds a new row each time
     * (two rows of one field allowed) and opens it. */
    void addFieldMenuOffersEveryCatalogField()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        Panel panel(&layer);
        auto *menu = panel.findChild<QMenu *>("additionalResultCatalog");
        QVERIFY(menu);
        QStringList offered, expected;
        QAction *gradient = nullptr;
        for (auto *action : menu->actions())
            if (const QString key = action->data().toString(); !key.isEmpty()) {
                offered << key;
                if (key == kGradient)
                    gradient = action;
            }
        for (const auto &v : layer.resultVariables())
            expected << v.key();
        for (const auto &v : layer.resultVectors())
            expected << v.key();
        QCOMPARE(offered, expected);
        QVERIFY(gradient);
        const auto before = layer.sublayers().size();
        gradient->trigger();
        gradient->trigger();
        QCOMPARE(layer.sublayers().size(), before + 2);
        auto *added =
            qobject_cast<ResultVectorSublayer *>(ISublayerHost::findSublayer(layer, currentId(panel)));
        QVERIFY(added && added->vectorKey() == kGradient);
        QCOMPARE(currentTab(panel), QStringLiteral("resultArrowsTab"));
    }

    /** @brief The streak colour classes, hidden while colouring by speed is off, are disabled and
     * drop a draft, so an edit the user can no longer see never blocks OK/Apply. */
    void hiddenFlowClassificationNeverBlocksAccept()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *style = layer.flowStreakSublayer()->streakStyle();
        style->setColorByMagnitude(true);
        auto scheme = style->scheme();
        scheme.setUseCustomRange(true);
        scheme.setRangeMin(0);
        scheme.setRangeMax(1);
        style->setScheme(scheme);
        Panel panel(&layer);
        QVERIFY(panel.focusResult(QStringLiteral("results2d.flowStreaks")));
        auto *editor = panel.findChild<openswmmvis::ui::ClassificationEditor *>("flowClassification");
        QVERIFY(editor && editor->isEnabled());
        QDoubleSpinBox *minimum = nullptr;
        for (auto *spin : editor->findChildren<QDoubleSpinBox *>())
            if (spin->accessibleName() == QStringLiteral("Minimum classification value"))
                minimum = spin;
        QVERIFY(minimum);
        minimum->setValue(5); // above the maximum
        QVERIFY(!editor->hasValidDraft());
        style->setColorByMagnitude(false);
        QTRY_VERIFY(editor->isHidden());
        QVERIFY(editor->hasValidDraft());
        style->setColorByMagnitude(true);
        QTRY_VERIFY(!editor->isHidden());
        QVERIFY(editor->hasValidDraft()); // the hidden draft was dropped
    }

    /** @brief Connections from the layer and its rows to a page end with the page: a deleted water
     * page, and a page whose row was removed, see no further notifications. */
    void pageFollowersEndWithTheirPage()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        Panel panel(&layer);
        QVERIFY(panel.focusResult(QStringLiteral("results2d.waterVisibility")));
        auto *depth = layer.depthResultSublayer();
        QVERIFY(panel.focusResult(depth->id()));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        layer.setThinFilmDepth(.25);
        QVERIFY(panel.focusResult(QStringLiteral("results2d.bands")));
        const QString removed = depth->id();
        QVERIFY(layer.removeResultSublayer(removed));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(currentId(panel) != removed); // the page moved to another item
        layer.contourBandSublayer()->setOpacity(.3);
        layer.isolineSublayer()->isolineStyle()->setLabels(true);
        layer.dispatchAnimationTick(0);
        QVERIFY(panel.isEnabled());
    }

    /** @brief Without a whole-run history (live runs) the default water-table isolines still draw,
     * and a whole-run isoline range stops only the isolines, not the row's fill. */
    void liveRowsDrawWithoutWholeRunHistory()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->wholeRun = false;
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        layer.setVisible(true);
        auto *table = scalarRow(layer, kTable);
        QVERIFY(table && table->isVisible() && table->fillStyle()->isolines());
        QVERIFY(!table->fillStyle()->isolinesNeedWholeRunRange());
        for (auto *s : layer.sublayers())
            s->setVisible(false);
        Renderer renderer;
        renderer.setWidth(100);
        renderer.setHeight(100);
        renderer.setMapExtent(MapExtent(0, 0, 1, 1));
        renderer.setLayer(&layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        const int noLines = vertices(root.get(), 1);
        table->setVisible(true);
        root.reset(renderer.sync(root.release()));
        QVERIFY(vertices(root.get(), 1) > noLines); // the default isolines draw
        auto scheme = table->fillStyle()->contourStyle()->scheme();
        scheme.setRangeMode(RangeMode::FixedOverRun);
        table->fillStyle()->contourStyle()->setScheme(scheme);
        table->fillStyle()->setCellFill(true);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(vertices(root.get(), 2), 6);       // the fill still draws
        QCOMPARE(vertices(root.get(), 1), noLines); // isolines need the whole run
    }

    /** @brief Restoring a snapshot (Cancel, Undo, reopen) keeps each pass's saved opacity; the
     * depth row's restored opacity is not pushed over them. */
    void restoreKeepsPassOpacities()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *depth = layer.depthResultSublayer();
        QVERIFY(depth);
        const qreal rowOpacity = depth->opacity(); // the bands' opacity (see below)
        layer.contourBandSublayer()->setOpacity(.8); // set on the Depth Contours tab
        const auto snapshot = ISublayerHost::saveSublayersToJson(layer);
        depth->setOpacity(.5);
        QCOMPARE(layer.contourBandSublayer()->opacity(), .5);
        ISublayerHost::loadSublayersFromJson(layer, snapshot);
        QCOMPARE(depth->opacity(), rowOpacity);
        QCOMPARE(layer.contourBandSublayer()->opacity(), .8);
        QVERIFY(layer.contourBandSublayer()->isVisible()); // visibility still follows the row
    }

    /** @brief The default rows paint below the animated streaks, so the canvas may cache the
     * water; a visible row above the streaks prevents it. */
    void defaultRowsKeepTheWaterCache()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        QVERIFY(layer.depthResultSublayer()->isVisible() &&
                layer.velocityResultSublayer()->isVisible());
        QVERIFY(layer.staticWaterCacheable());
        // A surface vector row paints above every pass, the streaks included.
        auto *above = layer.addResultVectorSublayer(kGradient);
        QVERIFY(above && above->isVisible());
        QVERIFY(!layer.staticWaterCacheable());
        above->setVisible(false);
        QVERIFY(layer.staticWaterCacheable());
    }

    /** @brief A vector row without a colour range (streaks only, hidden, nothing moving) leaves
     * its colour editor disabled, so it never blocks OK/Apply of the style dialog. */
    void vectorColourEditorNeverBlocksAccept()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer);
        const auto editor = [&panel] {
            return panel.findChild<openswmmvis::ui::ClassificationEditor *>(
                "additionalResultVectorClassification");
        };
        auto *velocity = layer.velocityResultSublayer();
        QVERIFY(panel.focusResult(velocity->id())); // streaks only: no colour range
        QVERIFY(editor() && editor()->hasValidDraft());
        auto *darcy = vectorRow(layer, kDarcy);
        darcy->setVisible(false); // shown by default
        QVERIFY(panel.focusResult(darcy->id()));
        QVERIFY(editor() && !editor()->isEnabled() && editor()->hasValidDraft());
        darcy->setVisible(true); // arrows coloured by magnitude: a real range
        QVERIFY(editor()->isEnabled() && editor()->hasValidDraft());
        darcy->setVisible(false);
        QVERIFY(!editor()->isEnabled());
    }

    /** @brief The velocity row legends its own arrows; the hidden arrow engine adds no block. */
    void velocityLegendIsTheRowsOnly()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->storedVelocity = true;
        owned->values["Mesh2_face_depth"] = {1, 1}; // wet cells carry velocity
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        layer.setCurrentTimeIndex(1);
        auto *velocity = layer.velocityResultSublayer();
        velocity->vectorStyle()->setArrows(true);
        QVERIFY(layer.velocityVectorsVisible());
        QVERIFY(layer.hasVelocityData());
        QVERIFY(!layer.velocityVectorSublayer()->isVisible());
        int engine = 0, row = 0;
        for (const auto &item : layer.sublayerLegendItems()) {
            engine += item.sublayerId == layer.velocityVectorSublayer()->id();
            row += item.sublayerId == velocity->id();
        }
        QCOMPARE(engine, 0);
        QVERIFY(row > 0);
    }

    /** @brief A frame without velocity data shows zero velocity every time, including after
     * velocities were written again: the wipe is skipped only when nothing wrote them since. */
    void framesWithoutVelocityAlwaysClearIt()
    {
        auto owned = std::make_unique<FieldSource>();
        auto *source = owned.get();
        source->values["Mesh2_face_depth"] = {1, 1}; // wet cells carry velocity
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        layer.setCurrentTimeIndex(1);
        auto speed = [&] {
            float sum = 0;
            for (const auto &t : layer.m_sceneTris)
                sum += std::abs(t.vx) + std::abs(t.vy) + std::abs(t.vmag);
            return sum;
        };
        for (int round = 0; round < 2; ++round) {
            source->storedVelocity = true;
            layer.refreshCurrentFrame();
            QVERIFY(layer.hasVelocityData());
            QVERIFY(speed() > 0);
            source->storedVelocity = false;
            layer.refreshCurrentFrame();
            QVERIFY(!layer.hasVelocityData());
            QCOMPARE(speed(), 0.f);
            layer.refreshCurrentFrame(); // nothing written since: the wipe is skipped
            QCOMPARE(speed(), 0.f);
        }
    }

    /** @brief Fixed-scale arrows (px per m/s, as the former velocity arrows), the dry-depth
     * cutoff and 1 px spacing; the surface velocity row draws fixed-scale arrows by default. */
    void arrowsKeepTheFormerScaleAndCutoff()
    {
        using Style = ResultVectorStyle;
        Style style;
        style.setSizeMode(Style::SizeMode::FixedScale);
        QCOMPARE(style.lengthPxFor(1.0, 0, 0), 20.); // 20 px per m/s
        QCOMPARE(style.lengthPxFor(0.1, 0, 0), 6.);  // clamped to the shortest arrow
        QCOMPARE(style.lengthPxFor(5.0, 0, 0), 36.); // and to the longest
        QCOMPARE(style.lengthPxFor(0.0, 0, 0), 0.);
        style.setLengthScaling(Style::LengthScaling::SquareRoot);
        QVERIFY(std::abs(style.lengthPxFor(1.21, 0, 0) - 22.) < 1e-9);
        style.setSpacingPx(1);
        QCOMPARE(style.spacingPx(), 1.);
        style.setDryDepthCutoff(.2);
        Style restored;
        restored.fromJson(style.toJson());
        QCOMPARE(restored.sizeMode(), Style::SizeMode::FixedScale);
        QCOMPARE(restored.scalePxPerUnit(), 20.);
        QCOMPARE(restored.dryDepthCutoff(), .2);

        auto owned = std::make_unique<FieldSource>();
        owned->values["Mesh2_face_depth"] = {.005f, .5f}; // the first cell is under 1 cm deep
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        auto *velocity = layer.velocityResultSublayer();
        QCOMPARE(velocity->vectorStyle()->sizeMode(), Style::SizeMode::FixedScale);
        velocity->vectorStyle()->setArrows(true);
        const auto geometry = layer.resultVectorGeometry(velocity, QRectF(-10, -10, 20, 20), 100);
        QVERIFY(!geometry.arrows.empty());
        for (const auto &arrow : geometry.arrows) {
            const QPointF mid = (arrow.tail + arrow.tip) / 2;
            QCOMPARE(layer.pickCellAt(mid), 1); // only the wet cell has arrows
            QVERIFY(std::abs(QLineF(arrow.tail, arrow.tip).length() * 100 - 20.) < 1e-6);
        }
    }

    /** @brief Scalar rows paint below the mesh wireframe and the surface vectors above every
     * pass, also when an older snapshot saved the rows above the wireframe. */
    void wireframePaintsAboveScalarRows()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        const auto check = [](const SWMM2DResultsLayer &l) {
            const auto order = l.sublayers();
            const auto at = [&](ISublayer *s) { return order.indexOf(s); };
            const int edges = at(l.meshEdgeSublayer()), nodes = at(l.meshNodeSublayer());
            return at(l.depthResultSublayer()) < edges &&
                   at(scalarRow(l, kTable)) < edges && at(scalarRow(l, kMoisture)) < edges &&
                   at(l.velocityResultSublayer()) > nodes &&
                   at(l.velocityResultSublayer()) > at(l.flowStreakSublayer());
        };
        QVERIFY(check(layer));
        // Saved by an older build, every row above the passes: reopening corrects it.
        auto saved = ISublayerHost::saveSublayersToJson(layer);
        saved.remove("unifiedResults");
        QJsonArray passes, rows;
        for (const auto &value : saved.value("sublayers").toArray()) {
            const QString id = value.toObject().value("id").toString();
            const bool row = id.startsWith("results2d.variable.") ||
                             id.startsWith("results2d.vector.");
            (row ? rows : passes).append(value);
        }
        for (const auto &row : rows)
            passes.append(row);
        saved.insert("sublayers", passes);
        SWMM2DResultsLayer reopened;
        reopened.setSource(std::make_unique<FieldSource>());
        ISublayerHost::loadSublayersFromJson(reopened, saved);
        QVERIFY(check(reopened));
    }

    /** @brief Removing or re-keying the depth or velocity row hides the passes it drove; a new
     * depth row drives them again. */
    void removingThePresentingRowHidesItsPasses()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        QVERIFY(layer.contourBandSublayer()->isVisible() && layer.flowStreakSublayer()->isVisible());
        QVERIFY(layer.removeResultSublayer(layer.depthResultSublayer()->id()));
        QVERIFY(!layer.depthResultSublayer());
        QVERIFY(!layer.contourBandSublayer()->isVisible());
        auto *depth = layer.addResultSublayer(kDepth);
        QCOMPARE(layer.depthResultSublayer(), depth);
        QVERIFY(layer.cellDepthFillSublayer()->isVisible()); // its display: a cell fill
        depth->fillStyle()->setAttribute(kTable);           // re-keyed away from depth
        QVERIFY(!layer.depthResultSublayer());
        QVERIFY(!layer.cellDepthFillSublayer()->isVisible());
        QVERIFY(layer.removeResultSublayer(layer.velocityResultSublayer()->id()));
        QVERIFY(!layer.flowStreakSublayer()->isVisible());
    }

    /** @brief A glyph grid larger than its cap coarsens instead of stopping partway down. */
    void glyphsCoverTheWholeView()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->values["Mesh2_face_depth"] = {1, 1};
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        auto *velocity = layer.velocityResultSublayer();
        velocity->vectorStyle()->setArrows(true);
        velocity->vectorStyle()->setSpacingPx(1);
        // 1 px is 1e-4 scene units: 1e8 grid points over the unit square.
        const auto geometry = layer.resultVectorGeometry(velocity, QRectF(-10, -10, 20, 20), 1e4);
        QVERIFY(!geometry.arrows.empty() && geometry.arrows.size() <= 60000);
        double lo = 1e9, hi = -1e9;
        for (const auto &arrow : geometry.arrows) {
            lo = std::min(lo, arrow.tail.y());
            hi = std::max(hi, arrow.tail.y());
        }
        QVERIFY2(hi - lo > .8, qPrintable(QString::number(hi - lo)));
    }

    /** @brief Filled contours march only the triangles in view; band classes of a value. */
    void filledContoursMarchOnlyTheView()
    {
        // A strip of four triangles over x in [0, 2]; the view keeps x >= 1.5.
        const QVector<QPointF> points{{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}};
        const std::vector<std::array<int, 4>> cells{
            {0, 1, 4, -1}, {0, 4, 3, -1}, {1, 2, 5, -1}, {1, 5, 4, -1}};
        const std::vector<std::array<int, 3>> tris{{0, 1, 4}, {0, 4, 3}, {1, 2, 5}, {1, 5, 4}};
        const std::vector<int> owners{0, 1, 2, 3};
        Mesh2DScalarFrame frame;
        frame.values = {1, 2, 3, 4};
        frame.status.assign(4, S::Valid);
        const std::vector<double> levels{0, 2.5, 5};
        const auto all = buildResultScalarGeometry(points, cells, tris, owners, frame, {}, false,
                                                   levels);
        const auto view = buildResultScalarGeometry(points, cells, tris, owners, frame, {}, false,
                                                    levels, false, QRectF(1.5, -1, 2, 3));
        QVERIFY2(all.error.isEmpty() && view.error.isEmpty(), qPrintable(all.error + view.error));
        QVERIFY(!view.bands.empty() && view.bands.size() < all.bands.size());
        for (const auto &band : view.bands)
            for (const auto &point : band.verts)
                QVERIFY(point.x() >= 1 - 1e-9);
        QCOMPARE(resultBandIndex(levels, -.1), -1);
        QCOMPARE(resultBandIndex(levels, 0), 0);
        QCOMPARE(resultBandIndex(levels, 2.5), 1);
        QCOMPARE(resultBandIndex(levels, 5), 1); // the top edge belongs to the last band
        QCOMPARE(resultBandIndex(levels, 5.1), -1);
        QCOMPARE(resultBandIndex(levels, std::nan("")), -1);
    }

    /** @brief Vector rows can draw the displayed frame's time, not only the current one. */
    void vectorRowsFollowTheDisplayedTime()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->scaleByTime = true;
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        layer.setCurrentTimeIndex(1);
        auto *darcy = vectorRow(layer, kDarcy);
        darcy->setVisible(true);
        double lo0 = 0, hi0 = 0, lo1 = 0, hi1 = 0;
        const QRectF everywhere(-10, -10, 20, 20);
        (void)layer.resultVectorGeometry(darcy, everywhere, 100, &lo0, &hi0, nullptr, 0);
        (void)layer.resultVectorGeometry(darcy, everywhere, 100, &lo1, &hi1);
        QVERIFY(hi0 > 0);
        QVERIFY(std::abs(hi1 - 2 * hi0) < 1e-12 * hi0 + 1e-18);
    }

    /** @brief With cell labels on, the depth row legends only its labels (the passes legend
     * its fills and contours). */
    void depthLabelsLegendOnlyTheLabels()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *depth = layer.depthResultSublayer();
        depth->fillStyle()->setCellLabels(true);
        QStringList labels;
        for (const auto &item : layer.sublayerLegendItems())
            if (item.sublayerId == depth->id())
                labels << item.label;
        QCOMPARE(labels.size(), 1);
        QVERIFY(labels.first().contains("cell values"));
    }

    /** @brief A depth row the user showed (2026-10-31 builds) keeps its display through the
     * migration even when every depth pass was off. */
    void shownDepthRowSurvivesMigration()
    {
        SWMM2DResultsLayer source;
        source.setSource(std::make_unique<FieldSource>());
        for (ISublayer *pass : std::initializer_list<ISublayer *>{
                 source.contourBandSublayer(), source.isolineSublayer(),
                 source.cellDepthFillSublayer(), source.smoothDepthFillSublayer()})
            pass->setVisible(false);
        auto saved = ISublayerHost::saveSublayersToJson(source);
        saved.remove("unifiedResults");
        QJsonArray rows;
        for (const auto &value : saved.value("sublayers").toArray()) {
            auto row = value.toObject();
            const QString id = row.value("id").toString();
            if (id.startsWith("results2d.vector."))
                continue;
            if (id.startsWith("results2d.variable.")) {
                row.insert("visible", id.endsWith(kDepth)); // the user showed depth as a fill
                auto style = row.value("style").toObject();
                for (const char *key :
                     {"cellFill", "smoothFill", "filledContours", "isolines", "cellLabels"})
                    style.remove(QLatin1String(key));
                style.insert("presentation", 0);
                row.insert("style", style);
            }
            rows.append(row);
        }
        saved.insert("sublayers", rows);
        SWMM2DResultsLayer old;
        old.setSource(std::make_unique<FieldSource>());
        ISublayerHost::loadSublayersFromJson(old, saved);
        auto *depth = old.depthResultSublayer();
        QVERIFY(depth && depth->isVisible() && depth->fillStyle()->cellFill());
        QVERIFY(old.cellDepthFillSublayer()->isVisible());
    }

    /** @brief A second surface velocity row draws static streaks; only the presenting row
     * leaves them to the animated pass. */
    void secondVelocityRowDrawsStaticStreaks()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->values["Mesh2_face_depth"] = {1, 1};
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        auto *second = layer.addResultVectorSublayer(kVelocity);
        second->vectorStyle()->setArrows(false);
        second->vectorStyle()->setStreaks(true);
        QVERIFY(layer.velocityResultSublayer() != second);
        const QRectF everywhere(-10, -10, 20, 20);
        QVERIFY(!layer.resultVectorGeometry(second, everywhere, 100).streaks.empty());
        QVERIFY(
            layer.resultVectorGeometry(layer.velocityResultSublayer(), everywhere, 100).streaks.empty());
    }

    /** @brief Edge fluxes without cell velocities: no velocity row, so the animated streak
     * pass is a tree row of its own; without fluxes there is nothing to animate. */
    void fluxOnlyRunListsTheStreakPass()
    {
        const auto source = [](bool flux) {
            auto owned = std::make_unique<FieldSource>();
            owned->catalog.removeIf([](const V &v) {
                return v.dataset == "Mesh2_face_vx" || v.dataset == "Mesh2_face_vy";
            });
            owned->edgeFlux = flux;
            return owned;
        };
        SWMM2DResultsLayer flux;
        flux.setSource(source(true));
        QVERIFY(!flux.velocityResultSublayer() && flux.hasEdgeFluxData());
        QVERIFY(flux.sublayerListed(flux.flowStreakSublayer()));
        QVERIFY(!flux.sublayerListed(flux.velocityVectorSublayer()));
        SWMM2DResultsLayer none;
        none.setSource(source(false));
        QVERIFY(!none.sublayerListed(none.flowStreakSublayer()));
    }

    /** @brief A hidden automatic component row of a current project survives later syncs
     * (only older snapshots retire them). */
    void componentRowsRetireOnlyFromOlderSnapshots()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *vx = layer.addResultSublayer("surface:Mesh2_face_vx",
                                           "results2d.variable.catalog.surface:Mesh2_face_vx");
        QVERIFY(vx);
        vx->setVisible(false);
        ISublayerHost::loadSublayersFromJson(layer, ISublayerHost::saveSublayersToJson(layer));
        QVERIFY(scalarRow(layer, "surface:Mesh2_face_vx"));
    }

    /** @brief A removed default row does not come back on later syncs or after a save and
     * reopen; adding the field by hand brings it back. */
    void removedDefaultRowStaysRemoved()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        QVERIFY(layer.removeResultSublayer(vectorRow(layer, kDarcy)->id()));
        const auto saved = ISublayerHost::saveSublayersToJson(layer);
        ISublayerHost::loadSublayersFromJson(layer, saved); // syncs again
        QVERIFY(!vectorRow(layer, kDarcy));
        SWMM2DResultsLayer reopened;
        reopened.setSource(std::make_unique<FieldSource>());
        QVERIFY(vectorRow(reopened, kDarcy));
        ISublayerHost::loadSublayersFromJson(reopened, saved);
        QVERIFY(!vectorRow(reopened, kDarcy));
        auto *added = reopened.addResultVectorSublayer(kDarcy);
        QVERIFY(added && vectorRow(reopened, kDarcy) == added);
    }

    /** @brief A snapshot saved before the default rows (marker 1, or none) retires its hidden
     * automatic rows of other fields, and keeps visible ones, hidden rows added by hand and the
     * default fields' rows. A current snapshot keeps a hidden automatic row. */
    void olderSnapshotsRetireHiddenAutomaticRows()
    {
        SWMM2DResultsLayer source;
        source.setSource(std::make_unique<FieldSource>());
        const QString hiddenAuto = "results2d.variable.catalog.surface:Mesh2_face_z";
        const QString hiddenVector = "results2d.vector.catalog." + kGradient;
        const QString shownAuto = "results2d.variable.catalog.groundwater:Mesh2_face_gw_hu";
        QVERIFY(source.addResultSublayer("surface:Mesh2_face_z", hiddenAuto));
        QVERIFY(source.addResultVectorSublayer(kGradient, hiddenVector));
        QVERIFY(source.addResultSublayer("groundwater:Mesh2_face_gw_hu", shownAuto));
        auto *byHand = source.addResultSublayer("groundwater:Mesh2_face_gw_closure");
        QVERIFY(byHand);
        const QString byHandId = byHand->id();
        for (const QString &id : {hiddenAuto, hiddenVector, byHandId})
            ISublayerHost::findSublayer(source, id)->setVisible(false);
        scalarRow(source, kTable)->setVisible(false); // a hidden default
        const auto current = ISublayerHost::saveSublayersToJson(source);
        QCOMPARE(current.value("unifiedResults").toInt(), 2);
        {
            QFile out(QDir(outputDir()).filePath("default_rows_snapshot.json"));
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(QJsonDocument(current).toJson());
        }
        const auto load = [](const QJsonObject &json) {
            auto layer = std::make_unique<SWMM2DResultsLayer>();
            layer->setSource(std::make_unique<FieldSource>());
            ISublayerHost::loadSublayersFromJson(*layer, json);
            return layer;
        };
        for (const bool markerless : {false, true}) {
            auto older = current;
            if (markerless)
                older.remove("unifiedResults");
            else
                older.insert("unifiedResults", 1);
            const auto old = load(older);
            const QByteArray context = markerless ? "no marker" : "marker 1";
            QVERIFY2(!ISublayerHost::findSublayer(*old, hiddenAuto), context);
            QVERIFY2(!ISublayerHost::findSublayer(*old, hiddenVector), context);
            QVERIFY2(ISublayerHost::findSublayer(*old, shownAuto) &&
                         ISublayerHost::findSublayer(*old, shownAuto)->isVisible(),
                     context);
            QVERIFY2(ISublayerHost::findSublayer(*old, byHandId), context);
            QVERIFY2(scalarRow(*old, kTable), context);
            QVERIFY2(scalarRow(*old, kDepth) && vectorRow(*old, kDarcy), context);
        }
        const auto reopened = load(current);
        QVERIFY(ISublayerHost::findSublayer(*reopened, hiddenAuto));
        QVERIFY(ISublayerHost::findSublayer(*reopened, hiddenVector));
        QVERIFY(!scalarRow(*reopened, kTable)->isVisible());
    }

    /** @brief An unavailable |v| row of a surface vector stays under the surface heading. */
    void unavailableSurfaceMagnitudeStaysSurface()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        const QString key =
            "derived:" + QString::fromLatin1(QUrl::toPercentEncoding(
                             "magnitude|surface:Mesh2_face_ux|surface:Mesh2_face_uy"));
        auto *row = layer.addResultSublayer(key);
        QVERIFY(row);
        QCOMPARE(layer.sublayerGroup(row), 1); // Surface water — Scalar
    }

    /** @brief Column average: saturated to the ground is NotApplicable under every closure;
     * a sigma column without its layers (live runs) uses hu / (ground - table). */
    void columnAverageSaturatedAndLiveSigma()
    {
        const QVector<V> live{
            variable("Mesh2_face_z", false, "bed", "m", V::Temporal::Static),
            variable("Mesh2_face_gw_table_elev", true, "table", "m"),
            variable("Mesh2_face_gw_hu", true, "hu", "m"),
            variable("Mesh2_face_gw_closure", true, "closure", "", V::Temporal::Static)};
        V moisture;
        for (const auto &d : derivedResultVariables(live))
            if (d.key() == kMoisture)
                moisture = d;
        QVERIFY(moisture.derived);
        const QHash<QString, std::vector<float>> data{
            {"surface:Mesh2_face_z", {10, 10, 10}},
            {"groundwater:Mesh2_face_gw_table_elev", {8, 10, 9}},
            {"groundwater:Mesh2_face_gw_hu", {.5f, 0, .2f}},
            {"groundwater:Mesh2_face_gw_closure", {2, 2, 0}}};
        const auto read = [&](const V &v, int, std::vector<float> &out, std::vector<S> &status) {
            if (!data.contains(v.key()))
                return false;
            out = data.value(v.key());
            status.assign(out.size(), S::Valid);
            return true;
        };
        std::vector<float> values;
        std::vector<S> status;
        QVERIFY(readDerivedVariableAt(moisture, 0, read, values, status));
        QCOMPARE(status[0], S::Valid);
        QVERIFY(std::abs(values[0] - .25f) < 1e-6f); // sigma column, no layers: hu / 2 m
        QCOMPARE(status[1], S::NotApplicable);       // sigma column saturated to the ground
        QCOMPARE(status[2], S::Valid);
        QVERIFY(std::abs(values[2] - .2f) < 1e-6f);
    }

    /** @brief A user move of a builtin pass stays put: new rows go to their group's place,
     * and a current project's saved order is not rearranged. */
    void builtinMovesSurviveNewRowsAndReopening()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        const auto edges = [&] { return layer.sublayers().indexOf(layer.meshEdgeSublayer()); };
        QVERIFY(layer.moveSublayer(edges(), 0)); // the wireframe to the bottom
        QCOMPARE(edges(), 0);
        QVERIFY(layer.addResultSublayer(kTable));
        QCOMPARE(edges(), 0);
        const auto saved = ISublayerHost::saveSublayersToJson(layer);
        ISublayerHost::loadSublayersFromJson(layer, saved);
        QCOMPARE(edges(), 0);
    }

    /** @brief An older project whose depth row was removed while its bands stayed on gets a
     * depth row back to present them; a row it added by hand stays above new rows. */
    void olderProjectRowsPresentAndStayOnTop()
    {
        SWMM2DResultsLayer source;
        source.setSource(std::make_unique<FieldSource>());
        auto *mine = source.addResultSublayer(kTable, QStringLiteral("results2d.variable.mine"));
        QVERIFY(mine);
        auto saved = ISublayerHost::saveSublayersToJson(source);
        saved.remove("unifiedResults");
        QJsonArray rows;
        for (const auto &value : saved.value("sublayers").toArray()) {
            const QString id = value.toObject().value("id").toString();
            // The old project: no depth or vector rows; the bands pass is on.
            if (id.startsWith("results2d.vector.") || id.endsWith(kDepth))
                continue;
            rows.append(value);
        }
        saved.insert("sublayers", rows);
        SWMM2DResultsLayer old;
        old.setSource(std::make_unique<FieldSource>());
        ISublayerHost::loadSublayersFromJson(old, saved);
        auto *depth = old.depthResultSublayer();
        QVERIFY(depth && depth->isVisible() && depth->fillStyle()->filledContours());
        QVERIFY(old.contourBandSublayer()->isVisible());
        const auto order = old.sublayers();
        auto *user = ISublayerHost::findSublayer(old, QStringLiteral("results2d.variable.mine"));
        QVERIFY(user && order.indexOf(user) > order.indexOf(depth));
    }

    /** @brief A vector row re-keyed from a surface to a groundwater vector moves below the
     * wireframe with the other groundwater rows. */
    void rekeyedVectorRowMovesRegion()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *row = layer.addResultVectorSublayer(kGradient);
        const auto at = [&](ISublayer *s) { return layer.sublayers().indexOf(s); };
        QVERIFY(at(row) > at(layer.meshNodeSublayer()));
        row->vectorStyle()->setVectorKey(kDarcy);
        QCOMPARE(layer.sublayerGroup(row), 2);
        QVERIFY(at(row) < at(layer.meshEdgeSublayer()));
    }

    /** @brief The colour range of surface arrows covers only the cells they are drawn on. */
    void arrowColourRangeSkipsDryCells()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->values["Mesh2_face_vx"] = {3, 1};
        owned->values["Mesh2_face_depth"] = {.005f, .5f}; // the faster cell is dry
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        auto *velocity = layer.velocityResultSublayer();
        velocity->vectorStyle()->setArrows(true);
        double lo = 0, hi = 0;
        (void)layer.resultVectorGeometry(velocity, QRectF(-10, -10, 20, 20), 100, &lo, &hi);
        QCOMPARE(lo, 1.);
        QCOMPARE(hi, 1.);
    }

    /** @brief A coarsened glyph grid does not shift when the view pans across the mesh edge. */
    void coarsenedGridStaysPutWhilePanning()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->values["Mesh2_face_depth"] = {1, 1};
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        auto *velocity = layer.velocityResultSublayer();
        velocity->vectorStyle()->setArrows(true);
        // The mesh's scene extent, from a coarse grid over everything.
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        for (const auto &arrow :
             layer.resultVectorGeometry(velocity, QRectF(-10, -10, 20, 20), 300).arrows) {
            const QPointF mid = (arrow.tail + arrow.tip) / 2;
            x0 = std::min(x0, mid.x());
            x1 = std::max(x1, mid.x());
            y0 = std::min(y0, mid.y());
            y1 = std::max(y1, mid.y());
        }
        const double w = x1 - x0;
        QVERIFY(w > 0);
        velocity->vectorStyle()->setSpacingPx(1); // coarsened: 1e8 points per view
        const auto centres = [&](double left) {
            QSet<QPair<qint64, qint64>> out;
            const QRectF view(left, y0 - 1, w, y1 - y0 + 2);
            for (const auto &arrow : layer.resultVectorGeometry(velocity, view, 1e4).arrows) {
                const QPointF mid = (arrow.tail + arrow.tip) / 2;
                if (mid.x() >= x0 + .65 * w && mid.x() <= x0 + .95 * w) // in both views
                    out.insert({qRound64(mid.x() * 1e9), qRound64(mid.y() * 1e9)});
            }
            return out;
        };
        const auto a = centres(x0 + .5 * w), b = centres(x0 + .6 * w);
        QVERIFY(!a.isEmpty());
        QCOMPARE(a, b);
    }

    /** @brief Without a whole-run history (live runs) a row's classification editors preview the
     * current frame: they stay usable and never block OK/Apply, and the Data tab says why a
     * whole-run display draws nothing. */
    void liveRowEditorsStayUsable()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->wholeRun = false;
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        auto *row = layer.addResultSublayer(QStringLiteral("surface:Mesh2_face_vx"));
        QVERIFY(row);
        QCOMPARE(row->fillStyle()->contourStyle()->scheme().rangeMode(), RangeMode::FixedOverRun);
        Panel panel(&layer);
        QVERIFY(panel.focusResult(row->id()));
        for (const char *name :
             {"additionalResultClassification", "additionalResultContourClassification"}) {
            auto *editor = panel.findChild<openswmmvis::ui::ClassificationEditor *>(name);
            QVERIFY2(editor && editor->isEnabled() && editor->hasValidDraft(), name);
        }
        row->fillStyle()->setIsolines(true);
        QVERIFY(panel.findChild<QLabel *>("additionalResultStatus")
                    ->text()
                    .contains(QStringLiteral("Whole-run range requires")));
    }

    /** @brief Sizing or colouring by a scalar variable binds the first listed variable when none
     * is bound, so the arrows do not vanish. */
    void scalarBindingPicksTheFirstVariable()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *row = layer.addResultVectorSublayer(kGradient);
        QVERIFY(row && row->vectorStyle()->sizeKey().isEmpty() &&
                row->vectorStyle()->colorKey().isEmpty());
        Panel panel(&layer);
        QVERIFY(panel.focusResult(row->id()));
        for (const char *name : {"additionalResultSizeMode", "additionalResultColorMode"}) {
            auto *mode = panel.findChild<QComboBox *>(name);
            QVERIFY(mode);
            mode->setCurrentIndex(mode->findText(QStringLiteral("Scalar variable")));
        }
        const QString first =
            panel.findChild<QComboBox *>("additionalResultSizeVariable")->itemData(0).toString();
        QVERIFY(!first.isEmpty());
        QCOMPARE(row->vectorStyle()->sizeKey(), first);
        QCOMPARE(row->vectorStyle()->colorKey(), first);
    }

    /** @brief When another row stops presenting the depth passes (re-keyed elsewhere), the shown
     * second depth row takes them over and its page follows. */
    void pageFollowsAnotherRowsRekey()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *depth = layer.depthResultSublayer();
        auto *second = layer.addResultSublayer(kDepth);
        Panel panel(&layer);
        QVERIFY(panel.focusResult(second->id()));
        QVERIFY(!panel.findChild<QStackedWidget *>("depthFillPasses"));
        depth->setVariableKey(QStringLiteral("surface:Mesh2_face_z")); // same group
        QCOMPARE(layer.depthResultSublayer(), second);
        QVERIFY(panel.findChild<QStackedWidget *>("depthFillPasses"));
    }

    /** @brief While a picker opened from the page (a modal colour or ramp dialog) is open, the
     * page is neither replaced nor refreshed under it; it catches up once the picker closes. */
    void pageWaitsForAnOpenPicker()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *row = layer.addResultSublayer(QStringLiteral("surface:Mesh2_face_vx"));
        QVERIFY(row);
        Panel panel(&layer);
        QVERIFY(panel.focusResult(row->id()));
        auto *page = panel.findChild<QTabWidget *>("resultDisplayTabs");
        QDialog picker;
        picker.setModal(true);
        picker.show();
        QCOMPARE(QApplication::activeModalWidget(), &picker);
        row->setVariableKey(QStringLiteral("surface:Mesh2_face_vy")); // would rebuild the page
        QCOMPARE(panel.findChild<QTabWidget *>("resultDisplayTabs"), page);
        picker.hide();
        QTRY_VERIFY(panel.findChild<QTabWidget *>("resultDisplayTabs") != page);
        QCOMPARE(panel.findChild<QComboBox *>("additionalResultVariable")->currentData().toString(),
                 QStringLiteral("surface:Mesh2_face_vy"));
    }

    /** @brief Without the catalog depth row, the first depth row in paint order drives the
     * passes: a reorder that changes it hands them over, and the open page follows. */
    void reorderHandsThePassesToTheNewFirstRow()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        QVERIFY(layer.removeResultSublayer(layer.depthResultSublayer()->id()));
        auto *first = layer.addResultSublayer(kDepth);  // a cell fill
        auto *second = layer.addResultSublayer(kDepth); // above it, drawn as bands
        second->fillStyle()->setCellFill(false);
        second->fillStyle()->setFilledContours(true);
        QCOMPARE(layer.depthResultSublayer(), first);
        QVERIFY(layer.cellDepthFillSublayer()->isVisible());
        QVERIFY(!layer.contourBandSublayer()->isVisible());
        Panel panel(&layer);
        QVERIFY(panel.focusResult(second->id()));
        QVERIFY(!panel.findChild<QStackedWidget *>("depthFillPasses"));
        QSignalSpy changed(&layer, &SWMM2DResultsLayer::resultSublayersChanged);
        const auto subs = layer.sublayers();
        QVERIFY(layer.moveSublayer(subs.indexOf(second), subs.indexOf(first)));
        QCOMPARE(layer.depthResultSublayer(), second);
        QCOMPARE(changed.count(), 1);
        QVERIFY(!layer.cellDepthFillSublayer()->isVisible());
        QVERIFY(layer.contourBandSublayer()->isVisible());
        QCOMPARE(currentId(panel), second->id());
        QVERIFY(panel.findChild<QStackedWidget *>("depthFillPasses"));
    }

    /** @brief A row that becomes the depth or velocity row shows the opacity of the passes it
     * presents (new run, removal, re-key), without changing them; a row edit still sets every
     * pass. */
    void presentingRowShowsItsPassOpacity()
    {
        SWMM2DResultsLayer defaults; // passes at their own defaults
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *depth = layer.depthResultSublayer();
        auto *bands = layer.contourBandSublayer();
        QVERIFY(bands->opacity() < 1);
        QCOMPARE(depth->opacity(), bands->opacity()); // it presents the filled contours
        QCOMPARE(bands->opacity(), defaults.contourBandSublayer()->opacity());
        QCOMPARE(layer.isolineSublayer()->opacity(), defaults.isolineSublayer()->opacity());
        QCOMPARE(layer.cellDepthFillSublayer()->opacity(),
                 defaults.cellDepthFillSublayer()->opacity());
        auto *velocity = layer.velocityResultSublayer();
        QCOMPARE(velocity->opacity(), layer.flowStreakSublayer()->opacity());
        depth->setOpacity(.5);
        QCOMPARE(bands->opacity(), .5);
        QCOMPARE(layer.isolineSublayer()->opacity(), .5);
        // The presenting row is removed: a 30 % hand-added row takes over and shows the bands.
        bands->setOpacity(.7); // the Filled contours tab
        auto *second = layer.addResultSublayer(kDepth);
        second->fillStyle()->setCellFill(false);
        second->fillStyle()->setFilledContours(true);
        second->setOpacity(.3);
        QCOMPARE(bands->opacity(), .7); // not the presenting row yet
        QVERIFY(layer.removeResultSublayer(depth->id()));
        QCOMPARE(layer.depthResultSublayer(), second);
        QCOMPARE(second->opacity(), .7);
        QCOMPARE(bands->opacity(), .7);
        // Re-keyed into depth (no depth row left): the smooth fill it presents.
        QVERIFY(layer.removeResultSublayer(second->id()));
        layer.smoothDepthFillSublayer()->setOpacity(.45);
        auto *moisture = scalarRow(layer, kMoisture);
        moisture->setOpacity(.2);
        moisture->setVariableKey(kDepth);
        QCOMPARE(layer.depthResultSublayer(), moisture);
        QCOMPARE(moisture->opacity(), .45);
        QCOMPARE(layer.smoothDepthFillSublayer()->opacity(), .45);
        // Reopened, a saved presenting row keeps the opacity it was saved with.
        SWMM2DResultsLayer saved;
        saved.setSource(std::make_unique<FieldSource>());
        QVERIFY(saved.removeResultSublayer(saved.depthResultSublayer()->id()));
        auto *own = saved.addResultSublayer(kDepth);
        own->fillStyle()->setCellFill(false);
        own->fillStyle()->setFilledContours(true);
        own->setOpacity(.3);
        saved.contourBandSublayer()->setOpacity(.8); // the Filled contours tab
        const auto snapshot = ISublayerHost::saveSublayersToJson(saved);
        SWMM2DResultsLayer reopened;
        reopened.setSource(std::make_unique<FieldSource>());
        ISublayerHost::loadSublayersFromJson(reopened, snapshot);
        auto *row = reopened.depthResultSublayer();
        QVERIFY(row && row->id() == own->id());
        QCOMPARE(row->opacity(), .3);
        QCOMPARE(reopened.contourBandSublayer()->opacity(), .8);
    }

    /** @brief The depth contour-line pass draws its lines (CPU) and labels (scene graph) at its
     * own opacity. */
    void isolinePassOpacityReachesLinesAndLabels()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->bed = {0, .5, 1, .5}; // depth contours cross the sloping bed
        owned->values["Mesh2_face_depth"] = {1.f, 1.f};
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        layer.setCurrentTimeIndex(0);
        auto *depth = layer.depthResultSublayer();
        depth->fillStyle()->setFilledContours(false);
        depth->fillStyle()->setIsolines(true);
        auto *lines = layer.isolineSublayer();
        // Only the contour lines paint: no other rows, no mesh overlays.
        for (auto *sub : layer.sublayers())
            if ((qobject_cast<ResultScalarSublayer *>(sub) ||
                 qobject_cast<ResultVectorSublayer *>(sub)) &&
                sub != depth)
                sub->setVisible(false);
        layer.meshEdgeSublayer()->setVisible(false);
        layer.meshNodeSublayer()->setVisible(false);
        QVERIFY(lines->isVisible() && !layer.contourBandSublayer()->isVisible());
        lines->setOpacity(1);
        const QImage opaqueImage = paintCpu(layer);
        opaqueImage.save(QDir(outputDir()).filePath("isoline_pass_opaque.png"));
        const int opaque = highestAlpha(opaqueImage);
        QVERIFY(opaque > 0);
        lines->setOpacity(.25);
        const QImage faintImage = paintCpu(layer);
        faintImage.save(QDir(outputDir()).filePath("isoline_pass_faint.png"));
        const int faint = highestAlpha(faintImage);
        QVERIFY2(faint > 0 && faint < opaque / 2,
                 qPrintable(QStringLiteral("alpha %1 at 25 %, %2 opaque").arg(faint).arg(opaque)));
        lines->isolineStyle()->setLabels(true);
        Renderer renderer;
        renderer.setWidth(100);
        renderer.setHeight(100);
        renderer.setMapExtent(MapExtent(0, 0, 1, 1));
        renderer.setLayer(&layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        root.reset(renderer.sync(root.release()));
        const auto opacities = nodeOpacities(root.get());
        QVERIFY2(std::any_of(opacities.cbegin(), opacities.cend(),
                             [](qreal o) { return qFuzzyCompare(o, qreal(.25)); }),
                 "the label node does not carry the pass opacity");
    }

    /** @brief The vector legend and the style panel's colour range follow the time the scene
     * graph drew, not a newer current time. */
    void vectorLegendAndPanelFollowTheDrawnTime()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->scaleByTime = true;
        owned->values["Mesh2_face_gw_qx"] = {1e-7f, 3e-7f};
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        layer.setQsgOwnsRendering(true);
        layer.setCurrentTimeIndex(0);
        auto *darcy = vectorRow(layer, kDarcy);
        QVERIFY(darcy && darcy->isVisible() && darcy->vectorStyle()->arrows());
        auto scheme = darcy->vectorStyle()->colorScheme();
        scheme.setMode(OpenSWMM::Render::ClassificationScheme::ClassMode::Classified);
        scheme.setClassCount(2);
        darcy->vectorStyle()->setColorScheme(scheme);
        Renderer renderer;
        renderer.setWidth(100);
        renderer.setHeight(100);
        renderer.setMapExtent(MapExtent(0, 0, 1, 1));
        renderer.setLayer(&layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QCOMPARE(darcy->presentedTime(), 0);
        double lo0 = 0, hi0 = 0, lo1 = 0, hi1 = 0;
        QString units;
        (void)layer.resultVectorGeometry(darcy, QRectF(), 1, &lo0, &hi0, &units, 0);
        (void)layer.resultVectorGeometry(darcy, QRectF(), 1, &lo1, &hi1, nullptr, 1);
        QVERIFY(hi0 > lo0 && hi1 > hi0);
        layer.setCurrentTimeIndex(1); // not drawn yet
        QCOMPARE(layer.displayedVectorTime(darcy), 0);
        QStringList legend, expected;
        for (const auto &item : layer.sublayerLegendItems())
            if (item.sublayerId == darcy->id())
                legend << item.label;
        for (const auto &item : darcy->legendSymbolItems(lo0, hi0, units))
            expected << item.label;
        QCOMPARE(legend, expected);
        Panel panel(&layer);
        QVERIFY(panel.focusResult(darcy->id()));
        auto *editor = panel.findChild<openswmmvis::ui::ClassificationEditor *>(
            "additionalResultVectorClassification");
        QVERIFY(editor);
        auto *table = editor->findChild<QTableView *>();
        QVERIFY(table && table->model()->rowCount() == 2);
        QStringList cells;
        for (int r = 0; r < table->model()->rowCount(); ++r)
            for (int c = 0; c < table->model()->columnCount(); ++c)
                cells << table->model()->index(r, c).data().toString();
        const auto &shown = darcy->vectorStyle()->colorScheme();
        QVERIFY2(cells.contains(shown.formatValue(hi0)) && !cells.contains(shown.formatValue(hi1)),
                 qPrintable(cells.join(QLatin1Char('|'))));
        // The colour editor stays usable while the drawn frame has a range, although the newer
        // current frame cannot be read.
        auto lagging = std::make_unique<FieldSource>();
        lagging->values["Mesh2_face_gw_qx"] = {1e-7f, 3e-7f};
        lagging->missingDataset = QStringLiteral("Mesh2_face_gw_qx");
        lagging->missingTime = 1;
        SWMM2DResultsLayer late;
        late.setSource(std::move(lagging));
        late.setQsgOwnsRendering(true);
        late.setCurrentTimeIndex(0);
        Renderer lateRenderer;
        lateRenderer.setWidth(100);
        lateRenderer.setHeight(100);
        lateRenderer.setMapExtent(MapExtent(0, 0, 1, 1));
        lateRenderer.setLayer(&late);
        std::unique_ptr<QSGNode> lateRoot(lateRenderer.sync());
        late.setCurrentTimeIndex(1);
        auto *lateRow = vectorRow(late, kDarcy);
        QCOMPARE(lateRow->presentedTime(), 0);
        Panel latePanel(&late);
        QVERIFY(latePanel.focusResult(lateRow->id()));
        auto *lateEditor = latePanel.findChild<openswmmvis::ui::ClassificationEditor *>(
            "additionalResultVectorClassification");
        QVERIFY(lateEditor && lateEditor->isEnabled());
    }

    /** @brief Bands marched for a new view replace that request's bands for the old view, and the
     * least recently used geometry goes first: panning keeps the isolines cached. */
    void viewBoundBandsKeepOtherGeometryCached()
    {
        auto owned = std::make_unique<FieldSource>();
        owned->values["Mesh2_face_depth"] = {0.f, 1.f};
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        const auto frame = layer.resultFrame(kDepth, 0);
        QVERIFY(frame && frame->error.isEmpty());
        const std::vector<double> levels{.5}, bandLevels{0, .5, 1};
        const auto isolines = layer.resultScalarGeometry(frame, levels, false);
        for (int i = 0; i < 12; ++i) // a panning view
            (void)layer.resultScalarGeometry(frame, {}, false, bandLevels, false,
                                             QRectF(.01 * i, 0, 1, 1));
        QCOMPARE(layer.resultScalarGeometry(frame, levels, false).get(), isolines.get());
        // Other requests fill the cache; the isolines, still in use, stay.
        for (int i = 1; i <= 10; ++i) {
            (void)layer.resultScalarGeometry(frame, {.05 * i}, false);
            QCOMPARE(layer.resultScalarGeometry(frame, levels, false).get(), isolines.get());
        }
    }

    /** @brief Without cell labels the CPU painter reads no depth frame for the depth row (its
     * passes draw the fills and contours); with labels it does. */
    void cpuDepthRowReadsNoFrameWithoutLabels()
    {
        auto owned = std::make_unique<FieldSource>();
        auto *source = owned.get();
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        QVERIFY(layer.depthResultSublayer()->isVisible());
        source->reads.clear();
        (void)paintCpu(layer);
        QCOMPARE(source->reads.value("Mesh2_face_depth"), 0);
        layer.depthResultSublayer()->fillStyle()->setCellLabels(true);
        layer.setCurrentTimeIndex(1); // a frame not read yet
        (void)paintCpu(layer);
        QVERIFY(source->reads.value("Mesh2_face_depth") > 0);
    }

    /** @brief A reorder that leaves the depth and velocity rows alone still signals once, and the
     * style panel's field tree follows the new order. */
    void plainReorderSignalsAndReordersTheTree()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FieldSource>());
        auto *table = scalarRow(layer, kTable);
        auto *moisture = scalarRow(layer, kMoisture);
        QVERIFY(table && moisture);
        auto *depth = layer.depthResultSublayer();
        auto *velocity = layer.velocityResultSublayer();
        Panel panel(&layer);
        const auto tableAbove = [&] {
            return fieldItem(panel, table->id()).row() < fieldItem(panel, moisture->id()).row();
        };
        const bool before = tableAbove();
        QSignalSpy changed(&layer, &SWMM2DResultsLayer::resultSublayersChanged);
        const auto subs = layer.sublayers();
        QVERIFY(layer.moveSublayer(subs.indexOf(table), subs.indexOf(moisture)));
        QCOMPARE(layer.depthResultSublayer(), depth);
        QCOMPARE(layer.velocityResultSublayer(), velocity);
        QCOMPARE(changed.count(), 1);
        QTRY_COMPARE(tableAbove(), !before);
    }

    /** @brief The tree lists result rows under the four domain/kind headings and no row for
     * the internal passes. */
    void treeGroupsByDomainAndKind()
    {
        SWMM2DResultsLayer layer(QStringLiteral("2D Results"));
        layer.setSource(std::make_unique<FieldSource>());
        MapCanvas canvas;
        canvas.addLayer(&layer, false);
        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();
        const QModelIndex layerIdx = m->indexForLayer(&layer);
        QStringList headings, rows;
        for (int r = 0; r < m->rowCount(layerIdx); ++r) {
            const QModelIndex index = m->index(r, 0, layerIdx);
            if (m->isSublayerGroupIndex(index))
                headings << index.data().toString();
            else
                rows << m->sublayerForIndex(index)->id();
        }
        QCOMPARE(headings, (QStringList{"Surface water — Vector", "Surface water — Scalar",
                                        "Groundwater — Vector", "Groundwater — Scalar"}));
        // The terrain draws nothing under results and is not listed.
        QCOMPARE(rows, (QStringList{"results2d.meshVertices", "results2d.meshEdges"}));
    }
};

QTEST_MAIN(TestUnified2DResults)
#include "test_unified2dresults.moc"
