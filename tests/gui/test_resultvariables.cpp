/**
 * @file      test_resultvariables.cpp
 * @brief     Tests for resultvariables.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmm2dresultslayer.h"
#include "render/sublayers/resultscalarsublayer.h"
#include <QTest>
#include <QSignalSpy>
#include <QGraphicsScene>
#include <QPainter>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include "map/swmm2dresultsqsgrenderer.h"
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <hdf5.h>
#include "io/mesh2dvariableexport.h"
#include <cmath>
using namespace openswmmvis::io;
using namespace OpenSWMM::Render;

class VariableSource : public IMesh2DSource
{
public:
    int reads = 0;
    float scale = 1;
    int generation = 0;
    /** @brief Expose the synthetic square's vertex count.
     * @return Four vertices. */
    int vertexCount() const override { return 4; }
    /** @brief Expose the synthetic square's cell count.
     * @return Two triangles. */
    int triangleCount() const override { return 2; }
    /** @brief Expose the synthetic variable history length.
     * @return Two frames. */
    int timeCount() const override { return 2; }
    /** @brief Expose the manually controlled history revision used by cache tests.
     * @return The current generation field; tests increment it to invalidate cached history. */
    int historyGeneration() const override { return generation; }
    /** @brief Replace geometry outputs with a flat two-triangle unit square.
     * @param[out] x Four x coordinates: 0, 1, 1, 0.
     * @param[out] y Four y coordinates: 0, 0, 1, 1.
     * @param[out] z Four zero bed elevations.
     * @param[out] tris Two zero-based triangles (0,1,2) and (0,2,3).
     * @return Always true. This synthetic source does not override coordinate-reference metadata. */
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &tris) override
    {
        x = {0, 1, 1, 0};
        y = {0, 0, 1, 1};
        z = {0, 0, 0, 0};
        tris = {{0, 1, 2}, {0, 2, 3}};
        return true;
    }
    /** @brief Return two dry surface cells regardless of the requested frame.
     * The unnamed frame index is intentionally ignored and not validated.
     * @param[out] d Replaced with two zero depths.
     * @return Always true. */
    bool readDepthsAt(int, std::vector<float> &d) override
    {
        d = {0, 0};
        return true;
    }
    /** @brief Describe one saturated-groundwater Nitrate variable in mg/L.
     * The optional unnamed diagnostic-list pointer is ignored and left untouched.
     * @return A single descriptor with a stable dataset/species/domain/zone key, known units and two frames. */
    QVector<Mesh2DResultVariable> faceVariables(QStringList * = nullptr) const override
    {
        Mesh2DResultVariable v;
        v.dataset = "Mesh2_face_gw_sat_species_conc";
        v.species = "Nitrate";
        v.domain = Mesh2DResultVariable::Domain::Groundwater;
        v.zone = Mesh2DResultVariable::Zone::Saturated;
        v.label = "Saturated Nitrate";
        v.units = "mg/L";
        v.unitsKnown = true;
        v.frameCount = 2;
        return {v};
    }
    /** @brief Read the synthetic variable while counting every read attempt.
     * @param[in] v Descriptor whose key must equal this source's sole advertised variable key.
     * @param[in] t Zero-based frame index, restricted to 0 or 1.
     * @param[out] values Replaced on success with (10*t*scale, 2*scale) in the advertised mg/L units; unchanged on failure.
     * @param[out] status Replaced on success with Valid for cell zero and Waterless for cell one at frame zero, otherwise Valid; unchanged on failure.
     * @return True for an accepted key/frame, false otherwise. The reads counter increments even on rejected requests. */
    bool readFaceVariableAt(const Mesh2DResultVariable &v, int t, std::vector<float> &values,
                            std::vector<Mesh2DValueStatus> &status) override
    {
        ++reads;
        if (v.key() != faceVariables()[0].key() || t < 0 || t > 1)
            return false;
        values = {float(t * 10) * scale, 2 * scale};
        status = {Mesh2DValueStatus::Valid,
                  t == 0 ? Mesh2DValueStatus::Waterless : Mesh2DValueStatus::Valid};
        return true;
    }
};
class TestResultVariables : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Verify native scalar contours cache geometry and render nonempty CPU pixels and scene-graph vertices.
     * Checks half-opacity propagation and presented-frame availability, switches to CPU labels and checks nonempty text rendering, then depopulates the scene. Scene-graph geometry is inspected without requiring GPU readback. */
    void nativeScalarContoursAndLabelsUseBothRenderPaths()
    {
        class Renderer : public SWMM2DResultsQSGRenderer
        {
        public:
            /** @brief Build a fresh test renderer scene graph by invoking the protected paint-node hook.
             * Returns: Newly returned scene-tree root, owned by the test's unique pointer; may be nullptr when no content is produced. */
            QSGNode *sync() { return updatePaintNode(nullptr, nullptr); }
        };
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<VariableSource>());
        layer.setVisible(true);
        layer.setCurrentTimeIndex(1);
        for (auto *s : layer.sublayers())
            s->setVisible(false);
        auto *sub = layer.addResultSublayer(layer.resultVariables()[0].key());
        sub->fillStyle()->setPresentation(ResultScalarStyle::Presentation::Contours);
        auto *style = sub->fillStyle()->contourStyle();
        style->setColor(Qt::red);
        style->setLineWidthPx(3);
        style->setLevelMode(IsolineStyle::LevelMode::FixedInterval);
        style->setLevelInterval(2);
        const auto frame = layer.resultFrame(sub->variableKey(), 1);
        const auto levels =
            sub->fillStyle()->contourLevels(frame->minimum, frame->maximum, frame->samples);
        const auto g = layer.resultScalarGeometry(frame, levels, false);
        QVERIFY(g->error.isEmpty());
        QVERIFY(!g->contours.empty());
        QCOMPARE(layer.resultScalarGeometry(frame, levels, false).get(), g.get());
        QGraphicsScene scene;
        layer.populateScene(&scene, MapExtent(0, 0, 1, 1), nullptr);
        QImage image(400, 400, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            scene.render(&painter, QRectF(0, 0, 400, 400), QRectF(0, -1, 1, 1));
        }
        int painted = 0;
        for (int y = 0; y < 400; ++y)
            for (int x = 0; x < 400; ++x)
                painted += image.pixelColor(x, y).alpha() > 0;
        QVERIFY(painted > 0);
        QVERIFY(painted < 80000);
        sub->setOpacity(.5);
        Renderer renderer;
        renderer.setWidth(400);
        renderer.setHeight(400);
        renderer.setMapExtent(MapExtent(0, 0, 1, 1));
        renderer.setLayer(&layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QVERIFY(root);
        int vertices = 0;
        bool correctOpacity = false;
        QVector<QSGNode *> nodes{root.get()};
        for (qsizetype i = 0; i < nodes.size(); ++i) {
            auto *n = nodes[i];
            for (auto *c = n->firstChild(); c; c = c->nextSibling())
                nodes.append(c);
            if (n->type() == QSGNode::OpacityNodeType)
                correctOpacity =
                    correctOpacity || qAbs(static_cast<QSGOpacityNode *>(n)->opacity() - .5) < 1e-6;
            if (n->type() == QSGNode::GeometryNodeType) {
                auto *geometry = static_cast<QSGGeometryNode *>(n)->geometry();
                if (geometry && geometry->attributeCount() == 1)
                    vertices += geometry->vertexCount();
            }
        }
        QVERIFY(vertices > 0);
        QVERIFY(correctOpacity);
        QVERIFY(sub->presentedFrame());
        sub->fillStyle()->setPresentation(ResultScalarStyle::Presentation::Labels);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            scene.render(&painter, QRectF(0, 0, 400, 400), QRectF(0, -1, 1, 1));
        }
        painted = 0;
        for (int y = 0; y < 400; ++y)
            for (int x = 0; x < 400; ++x)
                painted += image.pixelColor(x, y).alpha() > 0;
        QVERIFY(painted > 0);
        layer.depopulateScene(&scene);
    }
    /** @brief Verify an authored two-cell HDF5 species fixture preserves native units, wet-zero and waterless statuses through reader, style restore and CSV export.
     * Distinguishes explicitly declared surface mg/L from unknown groundwater units and wet groundwater under dry surface; retains the HDF5 and CSV under SWMMVIS_RESULTS_TEST_OUTPUT or the source-relative artifact directory. */
    void hdf5SpeciesRoundTripRendersNativeValuesAndExportsCsv()
    {
        const QString root = qEnvironmentVariable(
            "SWMMVIS_RESULTS_TEST_OUTPUT",
            QFileInfo(QString::fromUtf8(__FILE__))
                .absoluteDir()
                .absoluteFilePath(
                    "../../workplans/artifacts/phase_34_themes_results/reader-integration"));
        QVERIFY(QDir().mkpath(root));
        const QString path = QDir(root).filePath("two_cell_species.h5");
        struct FileGuard {
            hid_t id;
            /** @brief Close the owned HDF5 file when its identifier remains nonnegative.
             * The test sets id to -1 after an explicit successful close; destructor close errors are ignored. */
            ~FileGuard()
            {
                if (id >= 0)
                    H5Fclose(id);
            }
        } file{H5Fcreate(path.toUtf8().constData(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)};
        QVERIFY(file.id >= 0);
        /** @brief Create and write one fixed-size null-terminated HDF5 string attribute.
         * Argument `ds`: Borrowed valid dataset identifier; ownership remains with the caller.
         * Argument `name`: Required attribute name accepted by HDF5.
         * Argument `value`: Attribute bytes, stored in a type sized to value.size()+1.
         * Returns: True when type/space/attribute creation and writing succeed, false otherwise; closes all locally created handles but does not check close errors. */
        auto attr = [](hid_t ds, const char *name, const QByteArray &value) {
            const hid_t type = H5Tcopy(H5T_C_S1), space = H5Screate(H5S_SCALAR);
            bool ok = type >= 0 && space >= 0 && H5Tset_size(type, size_t(value.size() + 1)) >= 0;
            const hid_t a = ok ? H5Acreate2(ds, name, type, space, H5P_DEFAULT, H5P_DEFAULT) : -1;
            ok = a >= 0 && H5Awrite(a, type, value.constData()) >= 0;
            if (a >= 0)
                H5Aclose(a);
            if (space >= 0)
                H5Sclose(space);
            if (type >= 0)
                H5Tclose(type);
            return ok;
        };
        /** @brief Create a numeric fixture dataset in the captured owned HDF5 file.
         * Argument `name`: Required dataset name.
         * Argument `dims`: Dimensions whose product must match the supplied values; not checked locally.
         * Argument `values`: Contiguous double payload of the required size.
         * Argument `units`: Optional units attribute; nullptr omits it.
         * Argument `species`: Add a Nitrate species_names attribute when true.
         * Argument `declaredSpeciesUnits`: Add a species_units attribute of mg/L when true.
         * Returns: True when dataset creation, payload and requested attributes succeed; closes local handles on every path and leaves any partially written dataset in the fixture file on failure. */
        auto dataset = [&](const char *name, const std::vector<hsize_t> &dims,
                           const std::vector<double> &values, const char *units = nullptr,
                           bool species = false, bool declaredSpeciesUnits = false) {
            const hid_t space = H5Screate_simple(int(dims.size()), dims.data(), nullptr);
            const hid_t ds = space >= 0 ? H5Dcreate2(file.id, name, H5T_NATIVE_DOUBLE, space,
                                                     H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)
                                        : -1;
            bool ok = ds >= 0 && H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                                          values.data()) >= 0;
            if (ok && units)
                ok = attr(ds, "units", units);
            if (ok && species)
                ok = attr(ds, "species_names", "Nitrate");
            if (ok && declaredSpeciesUnits)
                ok = attr(ds, "species_units", "mg/L");
            if (ds >= 0)
                H5Dclose(ds);
            if (space >= 0)
                H5Sclose(space);
            return ok;
        };
        QVERIFY(dataset("Mesh2_node_x", {4}, {0, 1, 1, 0}, "m"));
        QVERIFY(dataset("Mesh2_node_y", {4}, {0, 0, 1, 1}, "m"));
        QVERIFY(dataset("Mesh2_node_z", {4}, {0, 0, 0, 0}, "m"));
        QVERIFY(dataset("Mesh2_face_nodes", {2, 3}, {0, 1, 2, 0, 2, 3}));
        QVERIFY(dataset("time", {2}, {46000, 46000.5}, "days since 1899-12-30 00:00:00"));
        QVERIFY(dataset("Mesh2_face_depth", {2, 2}, {1, 0, 1, 0}, "m"));
        QVERIFY(dataset("Mesh2_face_species_conc", {2, 1, 2}, {0, 7, 12.5, 4}, "1", true, true));
        QVERIFY(dataset("Mesh2_face_gw_hg", {2, 2}, {1, 1, 1, 1}, "m"));
        QVERIFY(dataset("Mesh2_face_gw_sat_conc", {2, 1, 2}, {0, 0, 0, 0}, "1", true));
        QCOMPARE(H5Fclose(file.id), herr_t(0));
        file.id = -1;

        SWMM2DResultsLayer layer;
        auto source = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(source->open(path));
        layer.setSource(std::move(source));
        Mesh2DResultVariable surface, groundwater;
        for (const auto &v : layer.resultVariables()) {
            if (v.dataset == "Mesh2_face_species_conc")
                surface = v;
            if (v.dataset == "Mesh2_face_gw_sat_conc")
                groundwater = v;
        }
        QCOMPARE(surface.species, QString("Nitrate"));
        QVERIFY(surface.unitsKnown);
        QCOMPARE(surface.units, QString("mg/L"));
        QVERIFY(!groundwater.key().isEmpty());
        QVERIFY(!groundwater.unitsKnown);
        QVERIFY(surface.key() != groundwater.key());
        const auto wetZero = layer.resultFrame(surface.key(), 0);
        QVERIFY2(wetZero->error.isEmpty(), qPrintable(wetZero->error));
        QCOMPARE(wetZero->values[0], 0.f);
        QCOMPARE(wetZero->status[0], Mesh2DValueStatus::Valid);
        QCOMPARE(wetZero->status[1], Mesh2DValueStatus::Waterless);
        const auto gw = layer.resultFrame(groundwater.key(), 0);
        QVERIFY(gw->error.isEmpty());
        QCOMPARE(gw->values[1], 0.f);
        QCOMPARE(gw->status[1], Mesh2DValueStatus::Valid); // Wet GW survives a dry surface.
        auto *sub = layer.addResultSublayer(surface.key());
        QVERIFY(sub);
        sub->fillStyle()->setColorRampName("");
        sub->fillStyle()->setLowColor(Qt::red);
        sub->fillStyle()->setHighColor(Qt::red);
        sub->fillStyle()->setWaterlessColor(Qt::blue);
        const auto snapshot = ISublayerHost::saveSublayersToJson(layer);
        const QString id = sub->id();
        SWMM2DResultsLayer reopened;
        auto reopenedSource = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(reopenedSource->open(path));
        reopened.setSource(std::move(reopenedSource));
        ISublayerHost::loadSublayersFromJson(reopened, snapshot);
        auto *restored =
            qobject_cast<ResultScalarSublayer *>(ISublayerHost::findSublayer(reopened, id));
        QVERIFY(restored);
        QCOMPARE(restored->variableKey(), surface.key());
        const auto frame = reopened.resultFrame(restored->variableKey(), 1);
        QVERIFY2(frame->error.isEmpty(), qPrintable(frame->error));
        QCOMPARE(frame->values[0], 12.5f);
        const auto colors = restored->cellColors(*frame);
        QCOMPARE(colors.size(), 2);
        QCOMPARE(colors[0], QColor(Qt::red));
        QCOMPARE(colors[1], QColor(Qt::blue));
        QString error;
        const QString csv = QDir(root).filePath("nitrate_frame_1.csv");
        QVERIFY2(
            exportMesh2DVariableCsv(*reopened.source(), restored->variableKey(), 1, csv, &error),
            qPrintable(error));
        QFile exported(csv);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        const QByteArray bytes = exported.readAll();
        QVERIFY(bytes.contains("\"Mesh2_face_species_conc\",\"Nitrate\",surface,none"));
        QVERIFY(bytes.contains(",0,12.5,valid,\"mg/L\"\n"));
        QVERIFY(bytes.contains(",1,4,waterless,\"mg/L\"\n"));
    }
    /** @brief Verify valid zero is distinct from waterless cells and cached frames invalidate on history revision or source replacement.
     * Checks read counts, scaled ranges and immutability of an already published frame. */
    void framesKeepZeroSeparateAndCacheBySourceHistory()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<VariableSource>();
        auto *raw = source.get();
        layer.setSource(std::move(source));
        const auto key = layer.resultVariables()[0].key();
        auto f = layer.resultFrame(key, 0);
        QVERIFY(f->error.isEmpty());
        QCOMPARE(f->minimum, 0.);
        QCOMPARE(f->maximum, 0.);
        QCOMPARE(f->values[0], 0.f);
        QCOMPARE(f->status[1], Mesh2DValueStatus::Waterless);
        QCOMPARE(layer.resultFrame(key, 0).get(), f.get());
        QCOMPARE(raw->reads, 1);
        raw->scale = 3;
        ++raw->generation;
        auto changed = layer.resultFrame(key, 1);
        QCOMPARE(changed->maximum, 30.);
        auto replacement = std::make_unique<VariableSource>();
        replacement->scale = 4;
        layer.setSource(std::move(replacement));
        QCOMPARE(layer.resultFrame(key, 1)->maximum, 40.);
        QCOMPARE(f->values[0], 0.f); // published frame remains immutable
    }
    /** @brief Verify whole-run range spans all valid samples and is reused for the next frame.
     * Checks only one extra read for that frame, an unavailable variable error and empty values for an out-of-range frame. */
    void wholeRunIncludesEveryFrameAndCachesRange()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<VariableSource>();
        auto *raw = source.get();
        layer.setSource(std::move(source));
        const auto key = layer.resultVariables()[0].key();
        auto f = layer.resultFrame(key, 0, true);
        QCOMPARE(f->minimum, 0.);
        QCOMPARE(f->maximum, 10.);
        QCOMPARE(f->samples.size(), 3);
        const int reads = raw->reads;
        auto next = layer.resultFrame(key, 1, true);
        QCOMPARE(next->maximum, 10.);
        QCOMPARE(raw->reads, reads + 1);
        QVERIFY(!layer.resultFrame("missing", 0)->error.isEmpty());
        QVERIFY(layer.resultFrame(key, 8)->values.empty());
    }
    /** @brief Verify duplicate variable sublayers keep distinct IDs and styles across JSON restore.
     * Malformed structures leave both live sublayers intact; restoring the original base snapshot removes the added sublayers. */
    void independentlyStyledVariablesRestoreAndRollbackStructure()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<VariableSource>());
        const auto base = ISublayerHost::saveSublayersToJson(layer);
        const auto key = layer.resultVariables()[0].key();
        auto *a = layer.addResultSublayer(key);
        auto *b = layer.addResultSublayer(key);
        QVERIFY(a);
        QVERIFY(b);
        QVERIFY(a->id() != b->id());
        a->setOpacity(.35);
        b->setOpacity(.7);
        a->fillStyle()->setLowColor(Qt::red);
        b->fillStyle()->setLowColor(Qt::green);
        const QString aid = a->id(), bid = b->id();
        for (const QJsonValue bad :
             {QJsonValue(QJsonObject{}), QJsonValue(QJsonValue::Null), QJsonValue("invalid")}) {
            ISublayerHost::loadSublayersFromJson(layer, QJsonObject{{"sublayers", bad}});
            QVERIFY(ISublayerHost::findSublayer(layer, aid));
            QVERIFY(ISublayerHost::findSublayer(layer, bid));
        }
        for (const QJsonArray bad :
             {QJsonArray{QJsonObject{}}, QJsonArray{QJsonObject{{"id", aid}, {"style", 42}}},
              QJsonArray{QJsonObject{{"id", aid}}, QJsonObject{{"id", aid}}}}) {
            ISublayerHost::loadSublayersFromJson(layer, QJsonObject{{"sublayers", bad}});
            QVERIFY(ISublayerHost::findSublayer(layer, aid));
            QVERIFY(ISublayerHost::findSublayer(layer, bid));
        }
        auto snapshot = ISublayerHost::saveSublayersToJson(layer);
        SWMM2DResultsLayer reopened;
        reopened.setSource(std::make_unique<VariableSource>());
        ISublayerHost::loadSublayersFromJson(reopened, snapshot);
        auto *ra = qobject_cast<ResultScalarSublayer *>(ISublayerHost::findSublayer(reopened, aid));
        auto *rb = qobject_cast<ResultScalarSublayer *>(ISublayerHost::findSublayer(reopened, bid));
        QVERIFY(ra);
        QVERIFY(rb);
        QCOMPARE(ra->opacity(), .35);
        QCOMPARE(rb->opacity(), .7);
        QCOMPARE(ra->fillStyle()->attribute(), key);
        QCOMPARE(rb->fillStyle()->lowColor(), QColor(Qt::green));
        ISublayerHost::loadSublayersFromJson(layer, base);
        QVERIFY(!ISublayerHost::findSublayer(layer, aid));
        QVERIFY(!ISublayerHost::findSublayer(layer, bid));
    }
    /** @brief Verify saturated-groundwater scalar colors render over a dry surface in CPU pixels and scene-graph geometry.
     * Requires six colored vertices, removes the scalar sublayer and verifies its colored geometry disappears. */
    void cpuAndQsgRenderGroundwaterOnDrySurfaceAndRemoveIt()
    {
        class Renderer : public SWMM2DResultsQSGRenderer
        {
        public:
            /** @brief Synchronize a test scene tree through the protected renderer hook.
             * Argument `old`: Nullable existing tree whose ownership is passed into updatePaintNode; the caller must release any owning pointer before supplying it.
             * Returns: Updated/replacement root to adopt into the caller's owning pointer; the hook may reuse or replace the supplied tree. */
            QSGNode *sync(QSGNode *old = nullptr) { return updatePaintNode(old, nullptr); }
        };
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<VariableSource>());
        layer.setVisible(true);
        for (auto *s : layer.sublayers())
            s->setVisible(false);
        layer.setCurrentTimeIndex(1);
        auto *sub = layer.addResultSublayer(layer.resultVariables()[0].key());
        const QString id = sub->id();
        sub->fillStyle()->setColorRampName("");
        sub->fillStyle()->setLowColor(Qt::red);
        sub->fillStyle()->setHighColor(Qt::red);
        QGraphicsScene scene;
        layer.populateScene(&scene, MapExtent(0, 0, 1, 1), nullptr);
        QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            scene.render(&painter, QRectF(0, 0, 100, 100), QRectF(0, -1, 1, 1));
        }
        QCOMPARE(image.pixelColor(75, 75), QColor(Qt::red));
        QCOMPARE(image.pixelColor(25, 25), QColor(Qt::red));
        layer.depopulateScene(&scene);
        Renderer renderer;
        renderer.setWidth(100);
        renderer.setHeight(100);
        renderer.setMapExtent(MapExtent(0, 0, 1, 1));
        renderer.setLayer(&layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QVERIFY(root);
        bool colorsMatch = true;
        /** @brief Count opaque colored-point vertices in the captured scene tree and accumulate the red-color match flag.
         * Returns: count of vertices with alpha greater than zero in two-attribute geometry; updates captured colorsMatch by requiring red=255 and green=0 for each counted vertex. The current root must exist. */
        auto coloredVertices = [&] {
            int count = 0;
            QVector<QSGNode *> pending{root.get()};
            for (qsizetype n = 0; n < pending.size(); ++n) {
                auto *child = pending[n];
                for (auto *nested = child->firstChild(); nested; nested = nested->nextSibling())
                    pending.append(nested);
                if (child->type() != QSGNode::GeometryNodeType)
                    continue;
                auto *g = static_cast<QSGGeometryNode *>(child)->geometry();
                if (!g || g->attributeCount() != 2)
                    continue;
                for (int i = 0; i < g->vertexCount(); ++i) {
                    const auto &v = g->vertexDataAsColoredPoint2D()[i];
                    if (v.a > 0) {
                        colorsMatch = colorsMatch && v.r == 255 && v.g == 0;
                        ++count;
                    }
                }
            }
            return count;
        };
        QCOMPARE(coloredVertices(), 6);
        QVERIFY(colorsMatch);
        QVERIFY(layer.removeResultSublayer(id));
        root.reset(renderer.sync(root.release()));
        QCOMPARE(coloredVertices(), 0);
    }
    /** @brief Verify additional scalar fields retain the complete old frame until asynchronous contour publication completes.
     * Runs only with OPENSWMM_QSG_ASYNC_CONTOURS=1, waits up to five seconds for contentReady, and checks color plus presented-frame index switch together from frame zero to one. */
    void additionalFieldsFollowDisplayedAsyncFrame()
    {
        if (qEnvironmentVariableIntValue("OPENSWMM_QSG_ASYNC_CONTOURS") != 1)
            QSKIP(
                "Run with OPENSWMM_QSG_ASYNC_CONTOURS=1 to exercise asynchronous frame publication.");
        class Renderer : public SWMM2DResultsQSGRenderer
        {
        public:
            /** @brief Synchronize a test scene tree through the protected renderer hook.
             * Argument `old`: Nullable existing tree whose ownership is passed into updatePaintNode; the caller must release any owning pointer before supplying it.
             * Returns: Updated/replacement root to adopt into the caller's owning pointer; the hook may reuse or replace the supplied tree. */
            QSGNode *sync(QSGNode *old = nullptr) { return updatePaintNode(old, nullptr); }
        };
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<VariableSource>());
        layer.setVisible(true);
        for (auto *s : layer.sublayers())
            s->setVisible(false);
        layer.setMaxDepth(1);
        layer.setCurrentTimeIndex(0);
        layer.contourBandSublayer()->setVisible(true);
        layer.contourBandSublayer()->bandStyle()->setSmoothBands(true);
        auto *sub = layer.addResultSublayer(layer.resultVariables()[0].key());
        auto scheme = sub->fillStyle()->scheme();
        scheme.setRampName("");
        scheme.setLowColor(Qt::red);
        scheme.setHighColor(Qt::blue);
        scheme.setUseCustomRange(true);
        scheme.setRangeMin(0);
        scheme.setRangeMax(10);
        sub->fillStyle()->setScheme(scheme);
        Renderer renderer;
        renderer.setWidth(100);
        renderer.setHeight(100);
        renderer.setMapExtent(MapExtent(0, 0, 1, 1));
        renderer.setLayer(&layer);
        QSignalSpy ready(&renderer, &SWMM2DResultsQSGRenderer::contentReady);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QVERIFY(root);
        /** @brief Find the first colored-point vertex in breadth-first scene-tree order.
         * Returns: its RGBA color, or an invalid QColor if none exists. The captured root must exist, and two-attribute geometry is assumed to use ColoredPoint2D. */
        auto firstColor = [&]() {
            QVector<QSGNode *> pending{root.get()};
            for (qsizetype n = 0; n < pending.size(); ++n) {
                auto *child = pending[n];
                for (auto *nested = child->firstChild(); nested; nested = nested->nextSibling())
                    pending.append(nested);
                if (child->type() != QSGNode::GeometryNodeType)
                    continue;
                auto *g = static_cast<QSGGeometryNode *>(child)->geometry();
                if (!g || g->attributeCount() != 2 || !g->vertexCount())
                    continue;
                const auto v = g->vertexDataAsColoredPoint2D()[0];
                return QColor(v.r, v.g, v.b, v.a);
            }
            return QColor();
        };
        QCOMPARE(firstColor(), QColor(Qt::red));
        QVERIFY(sub->presentedFrame());
        QCOMPARE(sub->presentedFrame()->time, 0);
        layer.setCurrentTimeIndex(1);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(firstColor(), QColor(Qt::red)); // Pending job retains the complete old frame.
        QVERIFY(sub->presentedFrame());
        QCOMPARE(sub->presentedFrame()->time, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 5000);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(firstColor(), QColor(Qt::blue));
        QVERIFY(sub->presentedFrame());
        QCOMPARE(sub->presentedFrame()->time, 1);
    }
    /** @brief Verify restoring an unavailable saved variable preserves its key and reports an error instead of substituting the available species. */
    void unavailableSavedVariableNeverSubstitutesFirstSpecies()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<VariableSource>());
        auto *sub = layer.addResultSublayer("groundwater:missing:species:Absent");
        QVERIFY(sub);
        const auto json = ISublayerHost::saveSublayersToJson(layer);
        SWMM2DResultsLayer reopened;
        reopened.setSource(std::make_unique<VariableSource>());
        ISublayerHost::loadSublayersFromJson(reopened, json);
        auto *restored =
            qobject_cast<ResultScalarSublayer *>(ISublayerHost::findSublayer(reopened, sub->id()));
        QVERIFY(restored);
        QCOMPARE(restored->fillStyle()->attribute(), sub->fillStyle()->attribute());
        QVERIFY(!reopened.resultFrame(restored->fillStyle()->attribute(), 0)->error.isEmpty());
    }
};
QTEST_MAIN(TestResultVariables)
#include "test_resultvariables.moc"
