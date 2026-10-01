/*!
 * \file   test_layertree_attributetable.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  What the layer tree's "Open Attribute Table" and "Export…" entries
 *         act on, for every kind of row
 *         (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §7.2,
 *         §8.2; R7).
 *
 * Headless: the two predicates take the row's layer-type ordinal, so no layer
 * object is built (the pointer is a tag the targets must carry through and
 * never dereference). The panel side — showModelCategory / showMeshTable
 * switching the dock — needs a loaded model and lives in
 * test_featurelayer_editors.
 */

#include "ui/panels/layertreecategories.h"
#include "ui/panels/layertreetargets.h"

#include <QTest>

using openswmmvis::ui::AttributeTableTarget;
using openswmmvis::ui::LayerExportTarget;
using openswmmvis::ui::LayerTreeRow;
using openswmmvis::ui::LayerTypeOrdinal;
using openswmmvis::ui::MeshTable;

namespace {

/*! A distinctive non-null tag; never dereferenced. */
OpenSWMMVisLayer *tag() { return reinterpret_cast<OpenSWMMVisLayer *>(quintptr(0x1000)); }

LayerTreeRow layerRow(LayerTypeOrdinal t)
{
    LayerTreeRow r;
    r.kind = LayerTreeRow::Kind::Layer;
    r.layer = tag();
    r.layerType = int(t);
    return r;
}

LayerTreeRow kindRow(LayerTypeOrdinal parent, int category)
{
    LayerTreeRow r = layerRow(parent);
    r.kind = LayerTreeRow::Kind::ObjectType;
    r.category = category;
    return r;
}

LayerTreeRow sublayerRow(LayerTypeOrdinal parent, const char *id)
{
    LayerTreeRow r = layerRow(parent);
    r.kind = LayerTreeRow::Kind::Sublayer;
    r.sublayerId = QLatin1String(id);
    return r;
}

// SWMMModelLayer::Category values used below (OpenSWMMVis::SwmmCategory).
constexpr int kJunctions = 0;
constexpr int kConduits  = 4;

}   // namespace

class TestLayerTreeAttributeTable : public QObject
{
    Q_OBJECT

private slots:
    void layerRowsOpenTheirTable_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<int>("kind");
        QTest::newRow("feature layer") << int(LayerTypeOrdinal::SWMMFeatureLayer)
                                       << int(AttributeTableTarget::Kind::Layer);
        QTest::newRow("vector layer")  << int(LayerTypeOrdinal::SWMMVectorLayer)
                                       << int(AttributeTableTarget::Kind::Layer);
        QTest::newRow("GIS layer")     << int(LayerTypeOrdinal::SWMMGISLayer)
                                       << int(AttributeTableTarget::Kind::Layer);
        QTest::newRow("tabular")       << int(LayerTypeOrdinal::SWMMTabularDataLayer)
                                       << int(AttributeTableTarget::Kind::Layer);
        QTest::newRow("SWMM model")    << int(LayerTypeOrdinal::SWMMModelLayer)
                                       << int(AttributeTableTarget::Kind::Layer);
        QTest::newRow("1D results")    << int(LayerTypeOrdinal::SWMMResultsLayer)
                                       << int(AttributeTableTarget::Kind::Layer);
        QTest::newRow("2D mesh")       << int(LayerTypeOrdinal::SWMM2DMeshLayer)
                                       << int(AttributeTableTarget::Kind::MeshTable);
        QTest::newRow("raster")        << int(LayerTypeOrdinal::SWMMRasterLayer)
                                       << int(AttributeTableTarget::Kind::None);
        QTest::newRow("imagery")       << int(LayerTypeOrdinal::SWMMImageryLayer)
                                       << int(AttributeTableTarget::Kind::None);
        QTest::newRow("WMS")           << int(LayerTypeOrdinal::SWMMWMSLayer)
                                       << int(AttributeTableTarget::Kind::None);
        QTest::newRow("WMTS")          << int(LayerTypeOrdinal::SWMMWMTSLayer)
                                       << int(AttributeTableTarget::Kind::None);
        QTest::newRow("2D results")    << int(LayerTypeOrdinal::SWMM2DResultsLayer)
                                       << int(AttributeTableTarget::Kind::None);
        QTest::newRow("annotations")   << int(LayerTypeOrdinal::SWMMAnnotationLayer)
                                       << int(AttributeTableTarget::Kind::None);
    }
    void layerRowsOpenTheirTable()
    {
        QFETCH(int, type);
        QFETCH(int, kind);
        const AttributeTableTarget t =
            openswmmvis::ui::attributeTableTarget(layerRow(LayerTypeOrdinal(type)));
        QCOMPARE(int(t.kind), kind);
        QCOMPARE(t.isValid(), kind != int(AttributeTableTarget::Kind::None));
        QCOMPARE(t.layer, t.isValid() ? tag() : nullptr);
    }

    void aMeshOpensItsCells()
    {
        const AttributeTableTarget t = openswmmvis::ui::attributeTableTarget(
            layerRow(LayerTypeOrdinal::SWMM2DMeshLayer));
        QCOMPARE(t.meshTable, MeshTable::Cells);   // plan Q7
    }

    void objectTypeRowsOpenThatType()
    {
        // Kind rows exist under the model today; the results-layer case is
        // kept so a kind row there would still resolve.
        for (const auto parent : {LayerTypeOrdinal::SWMMModelLayer,
                                  LayerTypeOrdinal::SWMMResultsLayer}) {
            const AttributeTableTarget t =
                openswmmvis::ui::attributeTableTarget(kindRow(parent, kConduits));
            QCOMPARE(t.kind, AttributeTableTarget::Kind::ModelCategory);
            QCOMPARE(t.category, kConduits);
            QCOMPARE(t.layer, tag());   // the model, or the run whose statistics show
        }
        // A kind row under anything else has no table.
        QVERIFY(!openswmmvis::ui::attributeTableTarget(
                     kindRow(LayerTypeOrdinal::SWMMVectorLayer, kJunctions)).isValid());
        QVERIFY(!openswmmvis::ui::attributeTableTarget(
                     kindRow(LayerTypeOrdinal::SWMMModelLayer, -1)).isValid());
    }

    void meshSublayersOpenTheirTable_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<int>("table");
        QTest::newRow("vertices")      << "mesh.vertices"     << true  << int(MeshTable::Vertices);
        QTest::newRow("edges")         << "mesh.edges"        << true  << int(MeshTable::Edges);
        QTest::newRow("bc")            << "mesh.bc"           << true  << int(MeshTable::Edges);
        QTest::newRow("fill")          << "mesh.fill"         << true  << int(MeshTable::Cells);
        QTest::newRow("contour bands") << "mesh.contourBands" << false << -1;
        QTest::newRow("isolines")      << "mesh.isolines"     << false << -1;
        QTest::newRow("coupled nodes") << "mesh.coupledNodes" << false << -1;
    }
    void meshSublayersOpenTheirTable()
    {
        QFETCH(QString, id);
        QFETCH(bool, valid);
        QFETCH(int, table);
        const AttributeTableTarget t = openswmmvis::ui::attributeTableTarget(
            sublayerRow(LayerTypeOrdinal::SWMM2DMeshLayer, id.toLatin1().constData()));
        QCOMPARE(t.isValid(), valid);
        if (valid) {
            QCOMPARE(t.kind, AttributeTableTarget::Kind::MeshTable);
            QCOMPARE(int(t.meshTable), table);
        }
        // The same id under a non-mesh parent has no table.
        QVERIFY(!openswmmvis::ui::attributeTableTarget(
                     sublayerRow(LayerTypeOrdinal::SWMMModelLayer,
                                 id.toLatin1().constData())).isValid());
    }

    void aRunsObjectTypeSublayersOpenThatType()
    {
        // A 1D results layer shows its object types as "results.<kind>"
        // sublayer rows (no kind rows); the panel fills in the category.
        LayerTreeRow r = sublayerRow(LayerTypeOrdinal::SWMMResultsLayer, "results.conduits");
        r.category = kConduits;
        const AttributeTableTarget t = openswmmvis::ui::attributeTableTarget(r);
        QCOMPARE(t.kind, AttributeTableTarget::Kind::ModelCategory);
        QCOMPARE(t.category, kConduits);
        QCOMPARE(t.layer, tag());   // the run, whose statistics show

        const LayerExportTarget e = openswmmvis::ui::layerExportTarget(r);
        QCOMPARE(e.kind, LayerExportTarget::Kind::SwmmObjects);
        QCOMPARE(e.category, kConduits);
        QVERIFY(e.withResults);

        // Without a category (not an object-type sublayer) there is nothing.
        const LayerTreeRow bare = sublayerRow(LayerTypeOrdinal::SWMMResultsLayer, "results.conduits");
        QVERIFY(!openswmmvis::ui::attributeTableTarget(bare).isValid());
        QVERIFY(!openswmmvis::ui::layerExportTarget(bare).isValid());
        // A category on a mesh sublayer changes nothing.
        LayerTreeRow mesh = sublayerRow(LayerTypeOrdinal::SWMM2DMeshLayer, "mesh.edges");
        mesh.category = kConduits;
        QCOMPARE(openswmmvis::ui::attributeTableTarget(mesh).kind,
                 AttributeTableTarget::Kind::MeshTable);
    }

    // ----- Part C: the Export entries (§8.2) ------------------------------

    void exportTargetsFollowTheRow()
    {
        using K = LayerExportTarget::Kind;
        const auto ex = [](const LayerTreeRow &r) { return openswmmvis::ui::layerExportTarget(r); };

        LayerExportTarget t = ex(layerRow(LayerTypeOrdinal::SWMMModelLayer));
        QCOMPARE(t.kind, K::SwmmObjects);
        QCOMPARE(t.category, -1);
        QVERIFY(!t.withResults);

        t = ex(layerRow(LayerTypeOrdinal::SWMMResultsLayer));
        QCOMPARE(t.kind, K::SwmmObjects);
        QVERIFY(t.withResults);

        t = ex(kindRow(LayerTypeOrdinal::SWMMModelLayer, kConduits));
        QCOMPARE(t.kind, K::SwmmObjects);
        QCOMPARE(t.category, kConduits);
        QVERIFY(!t.withResults);

        t = ex(kindRow(LayerTypeOrdinal::SWMMResultsLayer, kJunctions));
        QCOMPARE(t.category, kJunctions);
        QVERIFY(t.withResults);

        QCOMPARE(ex(layerRow(LayerTypeOrdinal::SWMMFeatureLayer)).kind, K::VectorLayer);
        QCOMPARE(ex(layerRow(LayerTypeOrdinal::SWMMVectorLayer)).kind, K::VectorLayer);
        QCOMPARE(ex(layerRow(LayerTypeOrdinal::SWMM2DMeshLayer)).kind, K::Mesh);
        QCOMPARE(ex(layerRow(LayerTypeOrdinal::SWMM2DMeshLayer)).meshTable, -1);
        QCOMPARE(ex(layerRow(LayerTypeOrdinal::SWMM2DResultsLayer)).kind, K::Results2D);
        QCOMPARE(ex(layerRow(LayerTypeOrdinal::SWMMRasterLayer)).kind, K::Raster);
        QCOMPARE(ex(layerRow(LayerTypeOrdinal::SWMMImageryLayer)).kind, K::None);
        QCOMPARE(ex(layerRow(LayerTypeOrdinal::SWMMTabularDataLayer)).kind, K::None);
        QCOMPARE(ex(layerRow(LayerTypeOrdinal::SWMMAnnotationLayer)).kind, K::None);

        t = ex(sublayerRow(LayerTypeOrdinal::SWMM2DMeshLayer, "mesh.edges"));
        QCOMPARE(t.kind, K::Mesh);
        QCOMPARE(t.meshTable, int(MeshTable::Edges));
        t = ex(sublayerRow(LayerTypeOrdinal::SWMM2DMeshLayer, "mesh.isolines"));
        QCOMPARE(t.kind, K::Mesh);
        QCOMPARE(t.meshTable, -1);   // no table of its own: offer all three
        QVERIFY(!ex(layerRow(LayerTypeOrdinal::SWMMRasterLayer)).withResults);
        QCOMPARE(ex(layerRow(LayerTypeOrdinal::SWMMWMSLayer)).layer, nullptr);
    }
};

QTEST_MAIN(TestLayerTreeAttributeTable)
#include "test_layertree_attributetable.moc"
