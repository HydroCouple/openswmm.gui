/*!
 * \file   test_featurestore.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Unit tests for the GeoPackage read/write backing store
 *         (MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07 §3.1;
 *         HANDOFF_FEATURE_LAYERS_VALIDATE_2026-09-07 §4).
 *
 * This is the repository's FIRST OGR write path, so the tests are deliberately
 * behavioural rather than structural: they exercise the driver, not the code
 * shape. Two of them settle open questions the handoff could not answer without
 * a toolchain:
 *
 *   - W9  — does GeoPackage honour an explicit FID on insert? The stable-id
 *           contract of AddFeatureCommand (undo then redo restores the SAME id)
 *           depends on it.
 *   - W11 — can a mesh worker open the same .gpkg read-only, by path, and see
 *           writes made through a still-open update handle? PLAN §6 (mesh
 *           generation consuming drawn layers) is built on that.
 *
 * Artifacts are written to tests/unit/data/featurestore_artifacts/ so a failure
 * can be opened in QGIS (CLAUDE.md §4.1 — never a temp directory), and are
 * removed at the start of each run so the suite is repeatable.
 */
#include <gtest/gtest.h>

#include "feature/featurestore.h"
#include "feature/featuretypes.h"

#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogrsf_frmts.h>

#include <QDir>
#include <QFile>
#include <QPointF>
#include <QString>
#include <QVariant>

#ifndef FEATURESTORE_OUT_DIR
#define FEATURESTORE_OUT_DIR "featurestore_artifacts"
#endif

using namespace openswmmvis::feature;

namespace {

QString outDir()
{
    QDir d(QStringLiteral(FEATURESTORE_OUT_DIR));
    if (!d.exists()) QDir().mkpath(d.absolutePath());
    return d.absolutePath();
}

//! A fresh path under the artifacts directory; any previous run is deleted so
//! the test starts from a known-empty container.
QString freshGpkg(const QString &stem)
{
    const QString path = QDir(outDir()).filePath(stem + QStringLiteral(".gpkg"));
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
    return path;
}

Ring squareRing(double x, double y, double w)
{
    Ring r;
    r.pts << QPointF(x, y) << QPointF(x + w, y)
          << QPointF(x + w, y + w) << QPointF(x, y + w);
    return r;
}

FeatureGeometry squarePolygon(double x, double y, double w)
{
    Part p;
    p.exterior = squareRing(x, y, w);
    FeatureGeometry g(GeometryType::Polygon);
    g.addPart(p);
    return g;
}

FeatureGeometry line3D()
{
    Ring r;
    r.pts << QPointF(0.0, 0.0) << QPointF(10.0, 0.0) << QPointF(10.0, 10.0);
    r.z = {1.5, -2.25, 300.125};
    Part p;
    p.exterior = r;
    FeatureGeometry g(GeometryType::LineString);
    g.addPart(p);
    return g;
}

Schema demoSchema()
{
    Schema s;
    FieldDef name;
    name.name = QStringLiteral("name");
    name.type = FieldType::Text;
    FieldDef count;
    count.name = QStringLiteral("count");
    count.type = FieldType::Integer;
    FieldDef roughness;
    roughness.name = QStringLiteral("roughness");
    roughness.type = FieldType::Real;
    FieldDef active;
    active.name = QStringLiteral("active");
    active.type = FieldType::Boolean;
    EXPECT_TRUE(s.append(name));
    EXPECT_TRUE(s.append(count));
    EXPECT_TRUE(s.append(roughness));
    EXPECT_TRUE(s.append(active));
    return s;
}

//! Open a store on a freshly created table, failing the calling test on error.
bool makeStore(FeatureStore &store, const QString &gpkg, const QString &table,
               GeometryType type, bool hasZ, const Schema &schema)
{
    QString err;
    if (!FeatureStore::ensureGeoPackage(gpkg, &err)) {
        ADD_FAILURE() << "ensureGeoPackage: " << err.toStdString();
        return false;
    }
    if (!FeatureStore::createTable(gpkg, table, type, hasZ, schema, QString(), &err)) {
        ADD_FAILURE() << "createTable: " << err.toStdString();
        return false;
    }
    if (!store.open(gpkg, table, &err)) {
        ADD_FAILURE() << "open: " << err.toStdString();
        return false;
    }
    return true;
}

//! Register GDAL drivers once for the whole binary.
class GdalEnvironment : public ::testing::Environment
{
public:
    void SetUp() override { GDALAllRegister(); }
};

const auto *kGdalEnv =
    ::testing::AddGlobalTestEnvironment(new GdalEnvironment);

} // namespace

// ---------------------------------------------------------------------------
// Container and schema
// ---------------------------------------------------------------------------

TEST(FeatureStoreContainer, EnsureGeoPackageCreatesAndReopens)
{
    const QString gpkg = freshGpkg(QStringLiteral("ensure"));
    ASSERT_FALSE(QFile::exists(gpkg));

    QString err;
    ASSERT_TRUE(FeatureStore::ensureGeoPackage(gpkg, &err)) << err.toStdString();
    EXPECT_TRUE(QFile::exists(gpkg));
    EXPECT_TRUE(err.isEmpty());

    // "ensure" — a second call on an existing container succeeds silently.
    EXPECT_TRUE(FeatureStore::ensureGeoPackage(gpkg, &err)) << err.toStdString();
    EXPECT_TRUE(FeatureStore::tables(gpkg).isEmpty());
}

TEST(FeatureStoreContainer, CreateTableThenSchemaRoundTrips)
{
    const QString gpkg = freshGpkg(QStringLiteral("schema"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("regions"),
                          GeometryType::Polygon, false, demoSchema()));

    EXPECT_TRUE(FeatureStore::tableExists(gpkg, QStringLiteral("regions")));
    EXPECT_EQ(store.geometryType(), GeometryType::Polygon);
    EXPECT_FALSE(store.hasZ());
    EXPECT_EQ(store.count(), 0);

    // Field names, types AND order survive; Boolean comes back as Boolean
    // rather than the Integer it is stored as (HANDOFF W6: OFSTBoolean).
    const Schema back = store.schema();
    ASSERT_EQ(back.count(), 4);
    EXPECT_EQ(back.at(0).name, QStringLiteral("name"));
    EXPECT_EQ(back.at(0).type, FieldType::Text);
    EXPECT_EQ(back.at(1).name, QStringLiteral("count"));
    EXPECT_EQ(back.at(1).type, FieldType::Integer);
    EXPECT_EQ(back.at(2).name, QStringLiteral("roughness"));
    EXPECT_EQ(back.at(2).type, FieldType::Real);
    EXPECT_EQ(back.at(3).name, QStringLiteral("active"));
    EXPECT_EQ(back.at(3).type, FieldType::Boolean);

    // A name already in use is refused, not silently renamed.
    QString err;
    EXPECT_FALSE(FeatureStore::createTable(gpkg, QStringLiteral("regions"),
                                           GeometryType::Polygon, false,
                                           Schema(), QString(), &err));
    EXPECT_FALSE(err.isEmpty());
    EXPECT_EQ(FeatureStore::uniqueTableName(gpkg, QStringLiteral("regions")),
              QStringLiteral("regions_1"));
}

TEST(FeatureStoreContainer, AddFieldAppearsInSchema)
{
    const QString gpkg = freshGpkg(QStringLiteral("addfield"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("t"),
                          GeometryType::Point, false, Schema()));
    ASSERT_EQ(store.schema().count(), 0);

    FieldDef f;
    f.name = QStringLiteral("depth");
    f.type = FieldType::Real;
    QString err;
    ASSERT_TRUE(store.addField(f, &err)) << err.toStdString();

    const Schema s = store.schema();
    ASSERT_EQ(s.count(), 1);
    EXPECT_EQ(s.at(0).name, QStringLiteral("depth"));
    EXPECT_EQ(s.at(0).type, FieldType::Real);

    // A new feature written after the change carries the new column.
    Feature feat;
    feat.geometry = FeatureGeometry(GeometryType::Point);
    Part p;
    p.exterior.pts << QPointF(5.0, 5.0);
    feat.geometry.addPart(p);
    feat.attributes.insert(QStringLiteral("depth"), 2.5);
    const FeatureId id = store.addFeature(feat, &err);
    ASSERT_NE(id, kInvalidFeatureId) << err.toStdString();

    Feature back;
    ASSERT_TRUE(store.feature(id, back));
    EXPECT_DOUBLE_EQ(back.attributes.value(QStringLiteral("depth")).toDouble(), 2.5);
}

/*! HANDOFF W7 — GeoPackage column deletion depends on the GDAL build. Either
 *  the column is gone, or the call reported why it could not be. A silent
 *  no-op is the only failure. */
TEST(FeatureStoreContainer, RemoveFieldOrReportsUnsupported)
{
    const QString gpkg = freshGpkg(QStringLiteral("removefield"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("t"),
                          GeometryType::Point, false, demoSchema()));
    ASSERT_EQ(store.schema().count(), 4);

    QString err;
    const bool ok = store.removeField(QStringLiteral("count"), &err);
    if (ok) {
        const Schema s = store.schema();
        EXPECT_EQ(s.count(), 3);
        EXPECT_FALSE(s.contains(QStringLiteral("count")));
        // The surviving columns keep their order.
        EXPECT_EQ(s.at(0).name, QStringLiteral("name"));
        EXPECT_EQ(s.at(1).name, QStringLiteral("roughness"));
        EXPECT_EQ(s.at(2).name, QStringLiteral("active"));
    } else {
        EXPECT_FALSE(err.isEmpty()) << "removeField failed without saying why";
        EXPECT_EQ(store.schema().count(), 4) << "failed but mutated anyway";
    }

    // Removing a column that is not there SUCCEEDS — removeField is documented
    // idempotent so a QUndoStack replay of RemoveFieldCommand cannot fail on
    // the second pass. The command itself gates on m_field.isValid(), so
    // nothing depends on telling "removed" apart from "was never there".
    const int before = store.schema().count();
    EXPECT_TRUE(store.removeField(QStringLiteral("no_such_column"), &err));
    EXPECT_EQ(store.schema().count(), before) << "idempotent path mutated the schema";
}

// ---------------------------------------------------------------------------
// Feature CRUD
// ---------------------------------------------------------------------------

TEST(FeatureStoreCrud, AddReadUpdateDeleteFeature)
{
    const QString gpkg = freshGpkg(QStringLiteral("crud"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("regions"),
                          GeometryType::Polygon, false, demoSchema()));

    Feature f;
    f.geometry = squarePolygon(0.0, 0.0, 100.0);
    f.attributes.insert(QStringLiteral("name"), QStringLiteral("basin A"));
    f.attributes.insert(QStringLiteral("count"), 3);
    f.attributes.insert(QStringLiteral("roughness"), 0.013);
    f.attributes.insert(QStringLiteral("active"), true);

    QString err;
    const FeatureId id = store.addFeature(f, &err);
    ASSERT_NE(id, kInvalidFeatureId) << err.toStdString();
    EXPECT_EQ(store.count(), 1);
    EXPECT_EQ(store.featureIds(), QVector<FeatureId>{id});

    Feature back;
    ASSERT_TRUE(store.feature(id, back));
    EXPECT_EQ(back.id, id);
    EXPECT_EQ(back.geometry, f.geometry);
    EXPECT_EQ(back.attributes.value(QStringLiteral("name")).toString(),
              QStringLiteral("basin A"));
    EXPECT_EQ(back.attributes.value(QStringLiteral("count")).toInt(), 3);
    EXPECT_DOUBLE_EQ(back.attributes.value(QStringLiteral("roughness")).toDouble(),
                     0.013);
    EXPECT_TRUE(back.attributes.value(QStringLiteral("active")).toBool());

    // Geometry update.
    const FeatureGeometry moved = squarePolygon(500.0, 500.0, 50.0);
    ASSERT_TRUE(store.setGeometry(id, moved, &err)) << err.toStdString();
    ASSERT_TRUE(store.feature(id, back));
    EXPECT_EQ(back.geometry, moved);

    // Attribute update.
    QVariantMap attrs = back.attributes;
    attrs.insert(QStringLiteral("name"), QStringLiteral("basin B"));
    attrs.insert(QStringLiteral("active"), false);
    ASSERT_TRUE(store.setAttributes(id, attrs, &err)) << err.toStdString();
    ASSERT_TRUE(store.feature(id, back));
    EXPECT_EQ(back.attributes.value(QStringLiteral("name")).toString(),
              QStringLiteral("basin B"));
    EXPECT_FALSE(back.attributes.value(QStringLiteral("active")).toBool());
    EXPECT_EQ(back.geometry, moved) << "attribute write disturbed the geometry";

    // Delete.
    ASSERT_TRUE(store.removeFeature(id, &err)) << err.toStdString();
    EXPECT_EQ(store.count(), 0);
    EXPECT_FALSE(store.feature(id, back));

    // Editing a feature that is gone fails rather than resurrecting it.
    EXPECT_FALSE(store.setGeometry(id, moved, &err));
    EXPECT_FALSE(err.isEmpty());
    EXPECT_FALSE(store.removeFeature(id, &err));
}

/*! HANDOFF W9 — the stable-id contract. AddFeatureCommand's redo must restore
 *  the id its undo removed, which requires GeoPackage to honour an explicit
 *  FID on insert. */
TEST(FeatureStoreCrud, AddFeatureReusesFreeFid)
{
    const QString gpkg = freshGpkg(QStringLiteral("fid"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("t"),
                          GeometryType::Polygon, false, Schema()));

    Feature f;
    f.id       = 42;
    f.geometry = squarePolygon(0.0, 0.0, 10.0);

    QString err;
    const FeatureId id = store.addFeature(f, &err);
    ASSERT_NE(id, kInvalidFeatureId) << err.toStdString();
    EXPECT_EQ(id, 42) << "GeoPackage did not honour the requested FID — "
                         "AddFeatureCommand must store the RETURNED id on redo";

    // The undo/redo shape: remove it, then re-add with the same id.
    ASSERT_TRUE(store.removeFeature(id, &err)) << err.toStdString();
    f.id = id;
    const FeatureId again = store.addFeature(f, &err);
    ASSERT_NE(again, kInvalidFeatureId) << err.toStdString();
    EXPECT_EQ(again, id) << "redo did not restore the id undo removed";

    // A TAKEN id is not an error: OGR assigns a fresh one and it is returned.
    Feature other;
    other.id       = id;
    other.geometry = squarePolygon(100.0, 100.0, 10.0);
    const FeatureId fresh = store.addFeature(other, &err);
    ASSERT_NE(fresh, kInvalidFeatureId) << err.toStdString();
    EXPECT_NE(fresh, id);
    EXPECT_EQ(store.count(), 2);
}

TEST(FeatureStoreCrud, ThreeDTableStoresZ)
{
    const QString gpkg = freshGpkg(QStringLiteral("threed"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("breaklines"),
                          GeometryType::LineString, true, Schema()));
    EXPECT_TRUE(store.hasZ());

    Feature f;
    f.geometry = line3D();
    ASSERT_TRUE(f.geometry.hasZ());

    QString err;
    const FeatureId id = store.addFeature(f, &err);
    ASSERT_NE(id, kInvalidFeatureId) << err.toStdString();

    Feature back;
    ASSERT_TRUE(store.feature(id, back));
    ASSERT_TRUE(back.geometry.hasZ()) << "Z was lost through the GeoPackage";
    ASSERT_EQ(back.geometry.partCount(), 1);
    const Ring &r = back.geometry.parts().at(0).exterior;
    ASSERT_EQ(r.z.size(), 3);
    EXPECT_DOUBLE_EQ(r.z.at(0), 1.5);
    EXPECT_DOUBLE_EQ(r.z.at(1), -2.25);
    EXPECT_DOUBLE_EQ(r.z.at(2), 300.125);
    EXPECT_EQ(back.geometry, f.geometry);
}

/*! HANDOFF W11 — mesh generation and attribute assignment re-open every source
 *  READ-ONLY by path on a worker thread. That only works if writes made through
 *  a still-open update handle are visible to a separate read-only open. */
TEST(FeatureStoreCrud, ConcurrentReadOnlyOpenSeesWrites)
{
    const QString gpkg = freshGpkg(QStringLiteral("concurrent"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("regions"),
                          GeometryType::Polygon, false, Schema()));

    Feature f;
    f.geometry = squarePolygon(0.0, 0.0, 100.0);
    QString err;
    const FeatureId id = store.addFeature(f, &err);
    ASSERT_NE(id, kInvalidFeatureId) << err.toStdString();
    ASSERT_TRUE(store.isOpen()) << "the update handle must stay open";

    // A second, independent handle — exactly what a mesh worker does.
    auto *ro = static_cast<GDALDataset *>(
        GDALOpenEx(gpkg.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    ASSERT_NE(ro, nullptr) << "read-only open failed while an update handle is live";

    OGRLayer *lyr = ro->GetLayerByName("regions");
    ASSERT_NE(lyr, nullptr);
    EXPECT_EQ(lyr->GetFeatureCount(1), 1)
        << "a concurrent read-only open did not see the flushed write";
    GDALClose(ro);

    // And a write made AFTER the reader opened is visible to a new reader.
    Feature g;
    g.geometry = squarePolygon(200.0, 200.0, 10.0);
    ASSERT_NE(store.addFeature(g, &err), kInvalidFeatureId) << err.toStdString();

    auto *ro2 = static_cast<GDALDataset *>(
        GDALOpenEx(gpkg.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    ASSERT_NE(ro2, nullptr);
    OGRLayer *lyr2 = ro2->GetLayerByName("regions");
    ASSERT_NE(lyr2, nullptr);
    EXPECT_EQ(lyr2->GetFeatureCount(1), 2);
    GDALClose(ro2);
}

// ---------------------------------------------------------------------------
// Guards
// ---------------------------------------------------------------------------

TEST(FeatureStoreGuards, ClosedStoreRefusesEveryWrite)
{
    FeatureStore store;
    EXPECT_FALSE(store.isOpen());

    Feature f;
    f.geometry = squarePolygon(0.0, 0.0, 10.0);
    QString err;
    EXPECT_EQ(store.addFeature(f, &err), kInvalidFeatureId);
    EXPECT_FALSE(err.isEmpty());
    EXPECT_FALSE(store.setGeometry(1, f.geometry, &err));
    EXPECT_FALSE(store.setAttributes(1, {}, &err));
    EXPECT_FALSE(store.removeFeature(1, &err));
    EXPECT_EQ(store.count(), 0);
    EXPECT_TRUE(store.featureIds().isEmpty());
}
