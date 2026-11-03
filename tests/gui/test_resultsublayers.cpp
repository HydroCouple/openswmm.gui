/**
 * @file      test_resultsublayers.cpp
 * @brief     2D result variables as sublayers, grouped under Surface / Groundwater
 *            headings in the Layers panel (2026-10-31). Since 2026-11-03 only the
 *            default fields get rows of their own; the others are added by hand
 *            (the style panel's Add field).
 *
 * Gates (workplans/BELLINGE_GW_QUALITY_RESULT_SUBLAYERS_BRIEF_2026-10-31.md,
 * workplans/RESULTS_DEFAULT_ROWS_PLAN_2026-11-03.md): default fields -> rows in
 * group order with labels/units, other fields none; hidden rows read no frames
 * until checked; tree headings with tri-state checks, collapse and drag within a
 * group only; project JSON keeps visibility, order and a user removal and offers
 * new default fields; the live -> saved switch keeps a checked variable checked.
 * Fixtures are synthetic and written under tests/output/result_sublayers_2026-10-31/.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/mesh2dlivevariables.h"
#include "layers/swmm2dresultslayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "render/isublayerhost.h"
#include "render/stylefileio.h"
#include "render/sublayers/resultscalarsublayer.h"
#include "ui/panels/layertreepanel.h"

#include <QtTest>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QPainter>
#include <QSGGeometryNode>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <hdf5.h>

using namespace openswmmvis::io;
using namespace OpenSWMM::Render;

namespace
{
/** @brief Reviewable output folder for fixtures and evidence (created on demand). */
QString outputDir()
{
    const QString dir = QStringLiteral(SWMMVIS_RESULT_SUBLAYERS_OUTPUT);
    QDir().mkpath(dir);
    return dir;
}

/** @brief Build one catalog descriptor with known units and two reported frames. */
Mesh2DResultVariable variable(const QString &dataset, const QString &species, bool groundwater,
                              const QString &label, const QString &units, bool derived = false)
{
    Mesh2DResultVariable v;
    v.dataset = dataset;
    v.species = species;
    v.domain = groundwater ? Mesh2DResultVariable::Domain::Groundwater
                           : Mesh2DResultVariable::Domain::Surface;
    v.label = label;
    v.units = units;
    v.unitsKnown = true;
    v.frameCount = 2;
    v.derived = derived;
    return v;
}

/** @brief Interleaved catalog: 2 surface, 2 groundwater, 1 derived (groundwater group). */
QVector<Mesh2DResultVariable> fiveVariables()
{
    return {variable("Mesh2_face_gw_table_elev", {}, true, "Groundwater table", "m"),
            variable("Mesh2_face_depth", {}, false, "Depth", "m"),
            variable("depth_to_water", {}, true, "Depth to water", "m", true),
            variable("Mesh2_face_species_conc", "TSS", false, "Surface TSS", "mg/L"),
            variable("Mesh2_face_gw_sat_conc", "TSS", true, "Saturated TSS", "mg/L")};
}

// Unit square of two cells whose catalog is supplied by the test. Every
// readFaceVariableAt call is counted: hidden rows must cause none.
class CatalogSource : public IMesh2DSource
{
public:
    QVector<Mesh2DResultVariable> catalog = fiveVariables();
    int reads = 0;
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
    bool readDepthsAt(int, std::vector<float> &d) override
    {
        d = {0, 0};
        return true;
    }
    QVector<Mesh2DResultVariable> faceVariables(QStringList *warnings = nullptr) const override
    {
        if (warnings)
            warnings->clear();
        return catalog;
    }
    bool readFaceVariableAt(const Mesh2DResultVariable &v, int t, std::vector<float> &values,
                            std::vector<Mesh2DValueStatus> &status) override
    {
        ++reads;
        for (const auto &known : catalog)
            if (known.key() == v.key() && t >= 0 && t < 2) {
                values = {float(t), 5.f};
                status = {Mesh2DValueStatus::Valid, Mesh2DValueStatus::Valid};
                return true;
            }
        return false;
    }
};

/** @brief Result-variable rows of a layer in paint order (bottom first). */
QList<ResultScalarSublayer *> resultRows(const SWMM2DResultsLayer &layer)
{
    QList<ResultScalarSublayer *> rows;
    for (auto *s : layer.sublayers())
        if (auto *r = qobject_cast<ResultScalarSublayer *>(s))
            rows.append(r);
    return rows;
}

/** @brief First result row bound to a variable key, or null. */
ResultScalarSublayer *rowFor(const SWMM2DResultsLayer &layer, const QString &key)
{
    for (auto *r : resultRows(layer))
        if (r->variableKey() == key)
            return r;
    return nullptr;
}

/** @brief Add rows by hand (the style panel's Add field), shown or hidden. */
void addByHand(SWMM2DResultsLayer &layer, std::initializer_list<Mesh2DResultVariable> vars,
               bool visible)
{
    for (const auto &v : vars)
        if (auto *row = layer.addResultSublayer(v.key()))
            row->setVisible(visible);
}

/** @brief Display labels of a parent's children, top row first. */
QStringList childLabels(const QAbstractItemModel &m, const QModelIndex &parent)
{
    QStringList out;
    for (int r = 0; r < m.rowCount(parent); ++r)
        out << m.index(r, 0, parent).data(Qt::DisplayRole).toString();
    return out;
}

/** @brief Drag one row and drop it at (row, parent) exactly as QTreeView would. */
bool dragDrop(LayerTreeModel &m, const QModelIndex &src, int row, const QModelIndex &parent)
{
    std::unique_ptr<QMimeData> mime(m.mimeData({src}));
    return mime && m.canDropMimeData(mime.get(), Qt::MoveAction, row, 0, parent) &&
           m.dropMimeData(mime.get(), Qt::MoveAction, row, 0, parent);
}

/** @brief Whether a dragged row may land at (row, parent). */
bool canDrop(LayerTreeModel &m, const QModelIndex &src, int row, const QModelIndex &parent)
{
    std::unique_ptr<QMimeData> mime(m.mimeData({src}));
    return mime && m.canDropMimeData(mime.get(), Qt::MoveAction, row, 0, parent);
}

class Renderer : public SWMM2DResultsQSGRenderer
{
public:
    /** @brief Synchronize through the protected paint-node hook. */
    QSGNode *sync(QSGNode *old = nullptr) { return updatePaintNode(old, nullptr); }
};

/** @brief Count opaque colored vertices in a scene-graph tree. */
int coloredVertices(QSGNode *root)
{
    int count = 0;
    QVector<QSGNode *> pending{root};
    for (qsizetype n = 0; n < pending.size(); ++n) {
        for (auto *c = pending[n]->firstChild(); c; c = c->nextSibling())
            pending.append(c);
        if (pending[n]->type() != QSGNode::GeometryNodeType)
            continue;
        auto *g = static_cast<QSGGeometryNode *>(pending[n])->geometry();
        if (g && g->attributeCount() == 2)
            for (int i = 0; i < g->vertexCount(); ++i)
                count += g->vertexDataAsColoredPoint2D()[i].a > 0;
    }
    return count;
}

/** @brief Render the layer's CPU scene over the unit square and count painted pixels. */
int paintedPixels(SWMM2DResultsLayer &layer)
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
    int painted = 0;
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x)
            painted += image.pixelColor(x, y).alpha() > 0;
    return painted;
}

/** @brief Write a small saved-results file whose catalog holds groundwater TSS but not Other. */
bool writeSavedResults(const QString &path)
{
    struct File {
        hid_t id;
        ~File()
        {
            if (id >= 0)
                H5Fclose(id);
        }
    } file{H5Fcreate(path.toUtf8().constData(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)};
    if (file.id < 0)
        return false;
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
    auto dataset = [&](const char *name, const std::vector<hsize_t> &dims,
                       const std::vector<double> &values, const char *units = nullptr,
                       const char *species = nullptr) {
        const hid_t space = H5Screate_simple(int(dims.size()), dims.data(), nullptr);
        const hid_t ds = space >= 0 ? H5Dcreate2(file.id, name, H5T_NATIVE_DOUBLE, space,
                                                 H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)
                                    : -1;
        bool ok = ds >= 0 &&
                  H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data()) >= 0;
        if (ok && units)
            ok = attr(ds, "units", units);
        if (ok && species)
            ok = attr(ds, "species_names", species) && attr(ds, "species_units", "MG/L");
        if (ds >= 0)
            H5Dclose(ds);
        if (space >= 0)
            H5Sclose(space);
        return ok;
    };
    return dataset("Mesh2_node_x", {4}, {0, 1, 1, 0}, "m") &&
           dataset("Mesh2_node_y", {4}, {0, 0, 1, 1}, "m") &&
           dataset("Mesh2_node_z", {4}, {10, 10, 10, 10}, "m") &&
           dataset("Mesh2_face_nodes", {2, 3}, {0, 1, 2, 0, 2, 3}) &&
           dataset("time", {2}, {46000, 46000.5}, "days since 1899-12-30 00:00:00") &&
           dataset("Mesh2_face_depth", {2, 2}, {0, 0, 0, 0}, "m") &&
           dataset("Mesh2_face_gw_hg", {2, 2}, {1, 1, 1, 1}, "m") &&
           dataset("Mesh2_face_gw_sat_conc", {2, 1, 2}, {5, 0, 6, 0}, "1", "TSS");
}

/** @brief Live descriptor of a saturated groundwater species in MG/L. */
Mesh2DResultVariable liveSpecies(const QString &species)
{
    Mesh2DResultVariable d;
    d.dataset = "Mesh2_face_gw_sat_conc";
    d.species = species;
    d.domain = Mesh2DResultVariable::Domain::Groundwater;
    d.zone = Mesh2DResultVariable::Zone::Saturated;
    d.label = "Saturated " + species;
    d.units = "MG/L";
    d.unitsKnown = true;
    return d;
}

/** @brief Live two-cell chemistry payload for TSS and Other. */
Mesh2DLiveVariablesPtr liveFrame()
{
    auto f = std::make_shared<Mesh2DLiveVariables>();
    f->cellCount = 2;
    for (const QString &name : {QStringLiteral("TSS"), QStringLiteral("Other")}) {
        Mesh2DLiveVariable v;
        v.descriptor = liveSpecies(name);
        v.values = {5, 0};
        v.status = {Mesh2DValueStatus::Valid, Mesh2DValueStatus::Valid};
        f->variables.append(v);
    }
    return f;
}

std::unique_ptr<EngineMesh2DSource> liveSource()
{
    return std::make_unique<EngineMesh2DSource>(
        std::vector<double>{0, 1, 1, 0}, std::vector<double>{0, 0, 1, 1},
        std::vector<double>{10, 10, 10, 10}, std::vector<std::array<int, 3>>{{0, 1, 2}, {0, 2, 3}});
}

// Counts frame reads of a real saved-results file.
class CountingH5Source : public HDF5Mesh2DSource
{
public:
    int reads = 0;
    bool readFaceVariableAt(const Mesh2DResultVariable &v, int t, std::vector<float> &values,
                            std::vector<Mesh2DValueStatus> &status) override
    {
        ++reads;
        return HDF5Mesh2DSource::readFaceVariableAt(v, t, values, status);
    }
};
} // namespace

class TestResultSublayers : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Only the default fields get rows (here the water table and depth): Surface above
     * Groundwater, labelled "label [units]" plus their displays, shown with their defaults
     * (unified rendering, 2026-11-01): depth as filled contours and the water table as labelled
     * isolines. The other fields get rows only when added by hand. */
    void defaultFieldsBecomeGroupedSublayers()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<CatalogSource>());
        const auto rows = resultRows(layer);
        QCOMPARE(rows.size(), 2);
        QStringList topFirst;
        for (auto it = rows.crbegin(); it != rows.crend(); ++it) {
            QVERIFY2((*it)->isVisible(), qPrintable((*it)->displayName()));
            QVERIFY((*it)->id().startsWith("results2d.variable."));
            topFirst << (*it)->displayName();
        }
        QCOMPARE(topFirst,
                 (QStringList{"Depth [m] — filled contours", "Groundwater table [m] — contours"}));
        for (int i : {2, 3, 4})
            QVERIFY2(!rowFor(layer, fiveVariables()[i].key()), qPrintable(fiveVariables()[i].label));
        // The variable rows paint above the depth passes and below the mesh wireframe
        // (unified rendering follow-up, 2026-11-01).
        QVERIFY(layer.sublayers().indexOf(rows.last()) <
                layer.sublayers().indexOf(layer.meshEdgeSublayer()));
        QVERIFY(layer.sublayers().indexOf(rows.first()) >
                layer.sublayers().indexOf(layer.contourBandSublayer()));
        // Re-announcing the catalog adds nothing; rows added by hand stay, a second row of a
        // field included.
        layer.refreshTimeRange();
        QCOMPARE(resultRows(layer).size(), 2);
        const auto vars = fiveVariables();
        addByHand(layer, {vars[3], vars[2], vars[4]}, true);
        auto *byHand = layer.addResultSublayer(vars[1].key());
        QVERIFY(byHand && byHand->isVisible());
        layer.refreshTimeRange();
        QCOMPARE(resultRows(layer).size(), 6);
        QCOMPARE(rowFor(layer, vars[3].key())->displayName(), QStringLiteral("Surface TSS [mg/L]"));
    }

    /** @brief Hidden rows read no frames through CPU paint, scene graph or legend; checking one
     * reads and paints it. */
    void hiddenSublayersReadNoFramesUntilChecked()
    {
        SWMM2DResultsLayer layer;
        auto owned = std::make_unique<CatalogSource>();
        auto *source = owned.get();
        layer.setSource(std::move(owned));
        const auto vars = fiveVariables();
        addByHand(layer, {vars[2], vars[3], vars[4]}, false);
        layer.setVisible(true);
        for (auto *s : layer.sublayers())
            if (!qobject_cast<ResultScalarSublayer *>(s))
                s->setVisible(false); // isolate the variable rows
        for (auto *r : resultRows(layer))
            r->setVisible(false); // including the rows shown by default
        layer.setCurrentTimeIndex(1);
        QCOMPARE(resultRows(layer).size(), 5);
        QCOMPARE(paintedPixels(layer), 0);
        Renderer renderer;
        renderer.setWidth(100);
        renderer.setHeight(100);
        renderer.setMapExtent(MapExtent(0, 0, 1, 1));
        renderer.setLayer(&layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QCOMPARE(coloredVertices(root.get()), 0);
        QVERIFY(layer.sublayerLegendItems().isEmpty());
        QVERIFY(ISublayerHost::aggregatedLegendSymbolItems(layer).isEmpty());
        QCOMPARE(source->reads, 0);

        // Animation ticks leave hidden rows alone; a shown row is invalidated.
        auto *saturated = rowFor(layer, vars[4].key());
        QVERIFY(saturated);
        QSignalSpy hiddenTicks(saturated, &ISublayer::invalidated);
        layer.dispatchAnimationTick(0);
        QCOMPARE(hiddenTicks.count(), 0);
        saturated->setVisible(true);
        hiddenTicks.clear();
        layer.dispatchAnimationTick(1);
        QCOMPARE(hiddenTicks.count(), 1);
        QVERIFY(paintedPixels(layer) > 0);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(coloredVertices(root.get()), 6);
        QVERIFY(source->reads > 0);
    }

    /** @brief The tree nests variable rows under two headings after the builtin rows; heading
     * checks are tri-state and toggle their rows; collapse survives resets; drags stay inside a
     * group and are undoable. Besides the defaults, three fields were added by hand (hidden). */
    void treeHeadingsToggleCollapseAndDragWithinGroup()
    {
        SWMM2DResultsLayer layer(QStringLiteral("2D Results"));
        layer.setSource(std::make_unique<CatalogSource>());
        const auto vars = fiveVariables();
        addByHand(layer, {vars[3], vars[2], vars[4]}, false); // each on top of its group
        MapCanvas canvas;
        canvas.addLayer(&layer, false);
        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();
        const QModelIndex layerIdx = m->indexForLayer(&layer);
        QVERIFY(layerIdx.isValid());
        // Listed builtins: terrain, edges and vertices. The depth and velocity
        // passes are displays of their rows, not rows of their own.
        int builtins = 0;
        for (auto *s : layer.sublayers())
            builtins += !qobject_cast<ResultScalarSublayer *>(s) && layer.sublayerListed(s);
        QCOMPARE(builtins, 3);
        QCOMPARE(m->rowCount(layerIdx), builtins + 2);
        for (int r = 0; r < builtins; ++r) {
            auto *s = m->sublayerForIndex(m->index(r, 0, layerIdx));
            QVERIFY(s && !qobject_cast<ResultScalarSublayer *>(s));
        }
        const QModelIndex surface = m->index(builtins, 0, layerIdx);
        const QModelIndex groundwater = m->index(builtins + 1, 0, layerIdx);
        QVERIFY(m->isSublayerGroupIndex(surface) && m->isSublayerGroupIndex(groundwater));
        QCOMPARE(surface.data().toString(), QStringLiteral("Surface water — Scalar"));
        QCOMPARE(groundwater.data().toString(), QStringLiteral("Groundwater — Scalar"));
        QCOMPARE(childLabels(*m, surface),
                 (QStringList{"Surface TSS [mg/L]", "Depth [m] — filled contours"}));
        QCOMPARE(childLabels(*m, groundwater),
                 (QStringList{"Saturated TSS [mg/L]", "Depth to water [m]",
                              "Groundwater table [m] — contours"}));
        QCOMPARE(m->parent(m->index(0, 0, groundwater)), groundwater);
        QCOMPARE(m->parent(groundwater), layerIdx);
        QCOMPARE(m->layerForIndex(groundwater), static_cast<OpenSWMMVisLayer *>(&layer));
        QVERIFY(m->flags(groundwater) & Qt::ItemIsUserCheckable);
        QVERIFY(!(m->flags(groundwater) & Qt::ItemIsDragEnabled));

        // Tri-state heading check (the water table shows by default).
        QCOMPARE(groundwater.data(Qt::CheckStateRole).toInt(), int(Qt::PartiallyChecked));
        QVERIFY(m->setData(groundwater, Qt::Checked, Qt::CheckStateRole));
        for (int r = 0; r < 3; ++r)
            QVERIFY(m->sublayerForIndex(m->index(r, 0, groundwater))->isVisible());
        QCOMPARE(groundwater.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
        QCOMPARE(surface.data(Qt::CheckStateRole).toInt(), int(Qt::PartiallyChecked));
        QVERIFY(m->setData(m->index(1, 0, groundwater), Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(groundwater.data(Qt::CheckStateRole).toInt(), int(Qt::PartiallyChecked));
        QVERIFY(m->setData(groundwater, Qt::Unchecked, Qt::CheckStateRole));
        for (int r = 0; r < 3; ++r)
            QVERIFY(!m->sublayerForIndex(m->index(r, 0, groundwater))->isVisible());

        // Collapsing a heading survives the model reset every reorder causes.
        auto *view = panel.findChild<QTreeView *>();
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
        QVERIFY(view && proxy);
        QVERIFY(view->isExpanded(proxy->mapFromSource(groundwater)));
        view->doItemsLayout(); // laid out, as when the user clicks the arrow
        QSignalSpy collapsed(view, &QTreeView::collapsed);
        view->collapse(proxy->mapFromSource(groundwater));
        QCOMPARE(collapsed.count(), 1);
        m->notifyHostSubOrderChanged();
        const auto headingAt = [&](int offset) {
            return m->index(builtins + offset, 0, m->indexForLayer(&layer));
        };
        QVERIFY(!view->isExpanded(proxy->mapFromSource(headingAt(1))));
        QVERIFY(view->isExpanded(proxy->mapFromSource(headingAt(0))));

        // Drag within a group: the top groundwater row dropped between rows 1 and 2.
        QVERIFY(dragDrop(*m, m->index(0, 0, headingAt(1)), 2, headingAt(1)));
        QCOMPARE(childLabels(*m, headingAt(1)),
                 (QStringList{"Depth to water [m]", "Saturated TSS [mg/L]",
                              "Groundwater table [m] — contours"}));
        // ... and onto a sibling row: the bottom row takes the top slot.
        QVERIFY(dragDrop(*m, m->index(2, 0, headingAt(1)), -1, m->index(0, 0, headingAt(1))));
        QCOMPARE(childLabels(*m, headingAt(1)),
                 (QStringList{"Groundwater table [m] — contours", "Depth to water [m]",
                              "Saturated TSS [mg/L]"}));
        QCOMPARE(childLabels(*m, headingAt(0)),
                 (QStringList{"Surface TSS [mg/L]", "Depth [m] — filled contours"}));
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        QCOMPARE(childLabels(*m, headingAt(1)),
                 (QStringList{"Saturated TSS [mg/L]", "Depth to water [m]",
                              "Groundwater table [m] — contours"}));

        // Across groups, or between a group and the builtin rows: refused.
        const QModelIndex surfaceRow = m->index(0, 0, headingAt(0));
        const QModelIndex lay = m->indexForLayer(&layer);
        QVERIFY(canDrop(*m, surfaceRow, 2, headingAt(0)));
        QVERIFY(!canDrop(*m, surfaceRow, 0, headingAt(1)));
        QVERIFY(!canDrop(*m, surfaceRow, -1, m->index(0, 0, headingAt(1))));
        QVERIFY(!canDrop(*m, surfaceRow, 1, lay));
        QVERIFY(!canDrop(*m, surfaceRow, -1, m->index(0, 0, lay)));
        const QModelIndex builtin = m->index(0, 0, lay);
        QVERIFY(canDrop(*m, builtin, builtins, lay));
        QVERIFY(!canDrop(*m, builtin, builtins + 1, lay));
        QVERIFY(!canDrop(*m, builtin, 0, headingAt(0)));
        QVERIFY(!canDrop(*m, builtin, -1, m->index(0, 0, headingAt(1))));
        QVERIFY(!dragDrop(*m, surfaceRow, 0, headingAt(1)));
        QCOMPARE(m->sublayerSiblings(m->index(1, 0, headingAt(1))).size(), 3);
    }

    /** @brief Project JSON keeps visibility, opacity, order, rows added by hand and the removal
     * of a default; offers default fields new since the save (and no others); survives a style
     * round trip; an older project gets the defaults. */
    void projectRoundTripKeepsChoicesAndOffersNewDefaults()
    {
        const auto vars = fiveVariables();
        SWMM2DResultsLayer saved;
        saved.setSource(std::make_unique<CatalogSource>());
        addByHand(saved, {vars[2], vars[4]}, false);
        auto *gwTss = rowFor(saved, vars[4].key());
        QVERIFY(gwTss);
        gwTss->setVisible(true);
        gwTss->setOpacity(.4);
        QVERIFY(saved.removeResultSublayer(rowFor(saved, vars[1].key())->id())); // a default
        rowFor(saved, vars[0].key())->setVisible(false); // the user hides a default
        const auto order = saved.sublayers();
        QVERIFY(saved.moveSublayer(order.indexOf(rowFor(saved, vars[2].key())),
                                   order.indexOf(rowFor(saved, vars[0].key()))));
        QStringList ids;
        for (auto *r : resultRows(saved))
            ids << r->id();

        // Through a file, the way the .oswp carries it.
        const QString path = QDir(outputDir()).filePath("project_sublayers.json");
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(QJsonDocument(ISublayerHost::saveSublayersToJson(saved)).toJson());
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(json.value("resultCatalogKeys").toArray().size(), 2); // the default fields

        SWMM2DResultsLayer reopened;
        reopened.setSource(std::make_unique<CatalogSource>());
        QCOMPARE(resultRows(reopened).size(), 2);
        ISublayerHost::loadSublayersFromJson(reopened, json);
        QStringList reopenedIds;
        for (auto *r : resultRows(reopened))
            reopenedIds << r->id();
        QCOMPARE(reopenedIds, ids);
        QVERIFY(!rowFor(reopened, vars[1].key()));
        QVERIFY(rowFor(reopened, vars[4].key())->isVisible());
        QCOMPARE(rowFor(reopened, vars[4].key())->opacity(), .4);
        QVERIFY(!rowFor(reopened, vars[0].key())->isVisible());

        // A default field new since the save arrives with its display, another field does not
        // arrive at all; the removal still holds.
        auto grown = std::make_unique<CatalogSource>();
        grown->catalog.append(
            variable("gw_theta_unsat_mean", {}, true, "Soil moisture", "1", true));
        grown->catalog.append(
            variable("Mesh2_face_gw_hu", {}, true, "Unsaturated thickness", "m"));
        const QString moistureKey = grown->catalog[5].key(), huKey = grown->catalog[6].key();
        QCOMPARE(moistureKey, QStringLiteral("derived:gw_theta_unsat_mean"));
        SWMM2DResultsLayer rerun;
        rerun.setSource(std::move(grown));
        ISublayerHost::loadSublayersFromJson(rerun, json);
        QCOMPARE(resultRows(rerun).size(), ids.size() + 1);
        QVERIFY(!rowFor(rerun, vars[1].key()));
        QVERIFY(rowFor(rerun, moistureKey) && rowFor(rerun, moistureKey)->isVisible());
        QVERIFY(!rowFor(rerun, huKey));

        // Style snapshots (dialog undo/redo, style files) replay the rows and the removal too.
        SWMM2DResultsLayer styled;
        styled.setSource(std::make_unique<CatalogSource>());
        QVERIFY(rowFor(styled, vars[1].key()) && !rowFor(styled, vars[4].key()));
        StyleFileIO::applyStyleJson(&styled, StyleFileIO::styleToJson(&saved));
        QVERIFY(!rowFor(styled, vars[1].key()));
        QVERIFY(rowFor(styled, vars[4].key())->isVisible());

        // An older project: builtin rows plus one row added by hand, no record.
        QJsonArray legacyRows;
        for (const auto &row : json.value("sublayers").toArray())
            if (!row.toObject().value("id").toString().startsWith("results2d.variable."))
                legacyRows.append(row);
        ResultScalarStyle depthStyle;
        depthStyle.setAttribute(vars[1].key());
        legacyRows.append(QJsonObject{{"id", "results2d.variable.legacy-depth"},
                                      {"visible", true},
                                      {"opacity", 1.0},
                                      {"style", depthStyle.toJson()}});
        const QJsonObject legacy{{"sublayers", legacyRows}};
        {
            QFile out(QDir(outputDir()).filePath("legacy_project_sublayers.json"));
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(QJsonDocument(legacy).toJson());
        }
        SWMM2DResultsLayer old;
        old.setSource(std::make_unique<CatalogSource>());
        ISublayerHost::loadSublayersFromJson(old, legacy);
        QCOMPARE(resultRows(old).size(), 2); // no duplicate of the hand-added depth row
        QCOMPARE(rowFor(old, vars[1].key())->id(), QStringLiteral("results2d.variable.legacy-depth"));
        QVERIFY(rowFor(old, vars[1].key())->isVisible());   // as saved
        QVERIFY(rowFor(old, vars[0].key())->isVisible());   // the water table by default
        for (int i : {2, 3, 4})
            QVERIFY(!rowFor(old, vars[i].key()));
    }

    /** @brief A live catalog that grows gives the default field a row and no other; live ->
     * saved keeps a checked variable checked with its style and keeps rows added by hand; a
     * rerun's live phase keeps rows its catalog lacks. */
    void liveToSavedKeepsCheckedVariables()
    {
        const QString h5 = QDir(outputDir()).filePath("saved_results.2d.h5");
        QVERIFY(writeSavedResults(h5));
        SWMM2DResultsLayer layer;
        auto owned = liveSource();
        auto *live = owned.get();
        layer.setSource(std::move(owned));
        QCOMPARE(resultRows(layer).size(), 0); // nothing reported yet
        const QDateTime start = QDateTime::fromSecsSinceEpoch(100);
        live->pushDepths({0, 0}, start, 0);
        QVERIFY(live->pushFaceVariables(liveFrame(), start, 0));
        layer.refreshTimeRange();
        QTRY_COMPARE(resultRows(layer).size(), 1); // live depth; the species are added by hand
        const QString tss = liveSpecies("TSS").key(), other = liveSpecies("Other").key();
        QVERIFY(!rowFor(layer, tss) && !rowFor(layer, other));
        auto *checked = layer.addResultSublayer(tss);
        auto *unchecked = layer.addResultSublayer(other);
        QVERIFY(checked && unchecked);
        unchecked->setVisible(false);
        checked->fillStyle()->setLowColor(Qt::red);
        const QString checkedId = checked->id(), uncheckedId = unchecked->id();

        live->markFinished();
        auto saved = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(saved->open(h5));
        layer.setSource(std::move(saved));
        auto *kept = rowFor(layer, tss);
        QVERIFY(kept);
        QCOMPARE(kept->id(), checkedId);
        QVERIFY(kept->isVisible());
        QCOMPARE(kept->fillStyle()->lowColor(), QColor(Qt::red));
        // Added by hand: kept though the saved catalog lacks it.
        QVERIFY(rowFor(layer, other) && rowFor(layer, other)->id() == uncheckedId);
        QVERIFY(!rowFor(layer, QStringLiteral("groundwater:Mesh2_face_gw_hg"))); // not a default
        auto *depth = rowFor(layer, QStringLiteral("surface:Mesh2_face_depth"));
        QVERIFY(depth && depth->isVisible()); // shown by default
        QCOMPARE(resultRows(layer).size(), 3);

        // Rerun into the same layer: the live phase does not drop saved-only rows.
        auto rerun = liveSource();
        auto *rerunLive = rerun.get();
        layer.setSource(std::move(rerun));
        rerunLive->pushDepths({0, 0}, start, 0);
        QVERIFY(rerunLive->pushFaceVariables(liveFrame(), start, 0));
        layer.refreshTimeRange();
        QTRY_VERIFY(rowFor(layer, other));
        QCOMPARE(rowFor(layer, QStringLiteral("surface:Mesh2_face_depth")), depth);
        QVERIFY(depth->isVisible());
        QCOMPARE(resultRows(layer).size(), 3);
    }

    /** @brief Manual check against a real results file (set SWMMVIS_RESULT_SUBLAYERS_H5): rows
     * for the default fields it holds, grouped, no frame read until one is checked. */
    void realResultsFileListsTheDefaults()
    {
        const QString path = qEnvironmentVariable("SWMMVIS_RESULT_SUBLAYERS_H5");
        if (path.isEmpty())
            QSKIP("Set SWMMVIS_RESULT_SUBLAYERS_H5 to a saved .2d.h5 to run this manual check.");
        auto owned = std::make_unique<CountingH5Source>();
        auto *source = owned.get();
        QVERIFY(owned->open(path));
        SWMM2DResultsLayer layer(QStringLiteral("2D Results"));
        QElapsedTimer timer;
        timer.start();
        layer.setSource(std::move(owned));
        const qint64 loadMs = timer.elapsed();
        const auto catalog = layer.resultVariables();
        const QSet<QString> defaults{QStringLiteral("surface:Mesh2_face_depth"),
                                     QStringLiteral("groundwater:Mesh2_face_gw_table_elev"),
                                     QStringLiteral("derived:gw_theta_unsat_mean")};
        QSet<QString> keys; // the default scalar fields this file holds
        for (const auto &v : catalog)
            if (defaults.contains(v.key()))
                keys.insert(v.key());
        const auto rows = resultRows(layer);
        QCOMPARE(rows.size(), keys.size());
        // Rows shown by default (unified rendering) are hidden to count reads from zero.
        for (auto *r : rows)
            r->setVisible(false);
        for (auto *s : layer.sublayers())
            if (auto *v = qobject_cast<ResultVectorSublayer *>(s))
                v->setVisible(false);
        layer.setVisible(true);
        Renderer renderer;
        renderer.setWidth(400);
        renderer.setHeight(400);
        renderer.setMapExtent(layer.extent());
        renderer.setLayer(&layer);
        std::unique_ptr<QSGNode> root(renderer.sync());
        (void)layer.sublayerLegendItems();
        QCOMPARE(source->reads, 0);
        QStringList report{QStringLiteral("file: %1").arg(path),
                           QStringLiteral("setSource ms: %1").arg(loadMs),
                           QStringLiteral("catalog keys: %1, rows: %2").arg(keys.size()).arg(rows.size())};
        for (auto it = rows.crbegin(); it != rows.crend(); ++it) {
            QVERIFY(!(*it)->isVisible());
            report << QStringLiteral("%1\t%2\t%3")
                          .arg(layer.sublayerGroups().value(layer.sublayerGroup(*it)),
                               (*it)->displayName(), (*it)->variableKey());
        }
        auto *table = rowFor(layer, QStringLiteral("groundwater:Mesh2_face_gw_table_elev"));
        if (table) {
            table->setVisible(true);
            root.reset(renderer.sync(root.release()));
            QVERIFY(source->reads > 0);
            report << QStringLiteral("reads after checking %1: %2")
                          .arg(table->displayName())
                          .arg(source->reads);
        }
        QFile out(QDir(outputDir()).filePath(QFileInfo(path).completeBaseName() + "_rows.txt"));
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(report.join('\n').toUtf8() + '\n');
    }
};

QTEST_MAIN(TestResultSublayers)
#include "test_resultsublayers.moc"
