/**
 * @file      test_featurestore.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Unit tests for the GeoPackage read/write backing store
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
 * Artifacts use FEATURESTORE_OUT_DIR (default featurestore_artifacts relative
 * to process cwd) so failures can be inspected in QGIS. Individual fixture
 * containers and SQLite companions are removed before reuse; deletion is unchecked.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <gtest/gtest.h>

#include "feature/featureroles.h"
#include "feature/featurestore.h"
#include "feature/featuretypes.h"

#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogr_feature.h>
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

namespace
{

/**
 *
 * @brief Resolve/create the persistent GeoPackage fixture directory.
 * @return Absolute FEATURESTORE_OUT_DIR path, relative to process cwd if configured
 * that way. Directory creation errors are unchecked; later opens report failures.
 * Leaves artifacts available for inspection and does not clear existing files.
 *
 */
QString outDir()
{
    QDir d(QStringLiteral(FEATURESTORE_OUT_DIR));
    if (!d.exists())
        QDir().mkpath(d.absolutePath());
    return d.absolutePath();
}

/**
 *
 * @brief Remove prior test-owned container and SQLite companions before reuse.
 * @param stem Filename stem under outDir, without extension; callers use simple
 * unique names and must close any handles from prior use before deletion.
 * @return Owned .gpkg path after attempting removal of it, -wal and -shm files.
 * Removal and directory creation errors are unchecked, so emptiness is not guaranteed.
 * Destructive only to these fixture paths; does not open a database.
 *
 */
QString freshGpkg(const QString &stem)
{
    const QString path = QDir(outDir()).filePath(stem + QStringLiteral(".gpkg"));
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
    return path;
}

/**
 *
 * @brief Build an unclosed axis-aligned four-point fixture ring.
 * @param x Lower-left x in arbitrary common fixture coordinate units.
 * @param y Lower-left y in the same units.
 * @param w Intended positive side length; no finite/degeneracy validation.
 * @return Owned XY ring counterclockwise for positive w, with no Z values or repeated
 * closing vertex. No coordinate transformation or persistent mutation.
 *
 */
Ring squareRing(double x, double y, double w)
{
    Ring r;
    r.pts << QPointF(x, y) << QPointF(x + w, y) << QPointF(x + w, y + w) << QPointF(x, y + w);
    return r;
}

/**
 *
 * @brief Wrap one fixture square exterior in a polygon geometry.
 * @param x Lower-left x in fixture coordinate units.
 * @param y Lower-left y in the same units.
 * @param w Positive side length for intended nondegenerate input.
 * @return Owned single-part polygon with four unclosed XY vertices and no holes/Z.
 * Delegates ring construction without checking finite coordinates or width.
 *
 */
FeatureGeometry squarePolygon(double x, double y, double w)
{
    Part p;
    p.exterior = squareRing(x, y, w);
    FeatureGeometry g(GeometryType::Polygon);
    g.addPart(p);
    return g;
}

/**
 *
 * @brief Construct the three-vertex Z round-trip fixture.
 * @return Owned single-part line at XY (0,0), (10,0), (10,10), with Z 1.5, -2.25,
 * 300.125 in arbitrary fixture units; no CRS, attributes or database mutation.
 *
 */
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

/**
 *
 * @brief Build the ordered four-field schema used by storage CRUD tests.
 * @return Owned name/Text, count/Integer, roughness/Real and active/Boolean fields.
 * Records nonfatal GoogleTest failures if any append is rejected, then still returns
 * the partially constructed schema. No field defaults, CRS or persistent mutation.
 *
 */
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

/**
 *
 * @brief Ensure a fixture container, create one table and open its update store.
 * @param store Borrowed output handle opened on success and retained by the caller.
 * @param gpkg Borrowed test-owned GeoPackage path; may name an existing container.
 * @param table New table name; an existing table is not silently reused.
 * @param type Requested geometry family for the new table.
 * @param hasZ True requests Z-capable table geometry.
 * @param schema Borrowed ordered field definitions copied into the new table.
 * @return True only after all three operations succeed; false also records a
 * nonfatal test failure with the failing operation's diagnostic. Uses empty CRS.
 * Earlier file/table creation can survive later failure; no transaction rollback,
 * parent creation, fixture cleanup or ownership transfer of the store.
 *
 */
bool makeStore(FeatureStore &store, const QString &gpkg, const QString &table, GeometryType type,
               bool hasZ, const Schema &schema)
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
    /**
     *
     * @brief Register GDAL drivers before this binary's tests run.
     * Called by GoogleTest's owned global environment; changes process-global driver
     * state and does not unregister drivers or create any fixture dataset.
     *
     */
    void SetUp() override { GDALAllRegister(); }
};

const auto *kGdalEnv = ::testing::AddGlobalTestEnvironment(new GdalEnvironment);

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
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("regions"), GeometryType::Polygon, false,
                          demoSchema()));

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
    EXPECT_FALSE(FeatureStore::createTable(gpkg, QStringLiteral("regions"), GeometryType::Polygon,
                                           false, Schema(), QString(), &err));
    EXPECT_FALSE(err.isEmpty());
    EXPECT_EQ(FeatureStore::uniqueTableName(gpkg, QStringLiteral("regions")),
              QStringLiteral("regions_1"));
}

TEST(FeatureStoreContainer, AddFieldAppearsInSchema)
{
    const QString gpkg = freshGpkg(QStringLiteral("addfield"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("t"), GeometryType::Point, false, Schema()));
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
    ASSERT_TRUE(
        makeStore(store, gpkg, QStringLiteral("t"), GeometryType::Point, false, demoSchema()));
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
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("regions"), GeometryType::Polygon, false,
                          demoSchema()));

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
    EXPECT_EQ(back.attributes.value(QStringLiteral("name")).toString(), QStringLiteral("basin A"));
    EXPECT_EQ(back.attributes.value(QStringLiteral("count")).toInt(), 3);
    EXPECT_DOUBLE_EQ(back.attributes.value(QStringLiteral("roughness")).toDouble(), 0.013);
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
    EXPECT_EQ(back.attributes.value(QStringLiteral("name")).toString(), QStringLiteral("basin B"));
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
    ASSERT_TRUE(
        makeStore(store, gpkg, QStringLiteral("t"), GeometryType::Polygon, false, Schema()));

    Feature f;
    f.id = 42;
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
    other.id = id;
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
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("breaklines"), GeometryType::LineString, true,
                          Schema()));
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
    ASSERT_TRUE(
        makeStore(store, gpkg, QStringLiteral("regions"), GeometryType::Polygon, false, Schema()));

    Feature f;
    f.geometry = squarePolygon(0.0, 0.0, 100.0);
    QString err;
    const FeatureId id = store.addFeature(f, &err);
    ASSERT_NE(id, kInvalidFeatureId) << err.toStdString();
    ASSERT_TRUE(store.isOpen()) << "the update handle must stay open";

    // A second, independent handle — exactly what a mesh worker does.
    auto *ro = static_cast<GDALDataset *>(GDALOpenEx(
        gpkg.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
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

    auto *ro2 = static_cast<GDALDataset *>(GDALOpenEx(
        gpkg.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
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

// ---------------------------------------------------------------------------
// Column metadata: defaults, descriptions, value lists
// (FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md R2)
// ---------------------------------------------------------------------------

namespace
{

/**
 *
 * @brief Compare only the field metadata this GDAL build persists.
 * @param stored Borrowed field read back from a GeoPackage.
 * @param reg Borrowed registry definition used as the expected contract.
 * Records nonfatal expectations for name/type and coerced default. On builds that
 * store metadata, also checks description and unordered fixed choices, or absence
 * of choices for other sources. Deliberately omits editor units, required flags and
 * registry/model/suggested hints; no mutation or return status.
 *
 */
void expectSamePersisted(const FieldDef &stored, const FieldDef &reg)
{
    SCOPED_TRACE(reg.name.toStdString());
    EXPECT_EQ(stored.name, reg.name);
    EXPECT_EQ(stored.type, reg.type);
    const QVariant regDefault =
        reg.defaultValue.isValid() ? coerceToFieldType(reg.defaultValue, reg.type) : QVariant();
    EXPECT_EQ(stored.defaultValue, regDefault);
    if (FeatureStore::storesFieldMetadata()) {
        EXPECT_EQ(stored.description, reg.description);
        if (reg.choiceSource == ChoiceSource::Fixed) {
            EXPECT_EQ(stored.choiceSource, ChoiceSource::Fixed);
            // GeoPackage returns the list sorted by value.
            EXPECT_TRUE(sameChoiceSet(stored.choices, reg.choices));
        } else {
            EXPECT_TRUE(stored.choices.isEmpty());
        }
    }
}

/**
 *
 * @brief Construct an attribute-free default-field insertion fixture.
 * @return Owned single-part XY line from (0,0) to (10,0), no Z or assigned ID;
 * coordinates have arbitrary common fixture units. Does not insert into a store.
 *
 */
Feature lineFeature()
{
    Ring r;
    r.pts << QPointF(0.0, 0.0) << QPointF(10.0, 0.0);
    Part p;
    p.exterior = r;
    Feature f;
    f.geometry = FeatureGeometry(GeometryType::LineString);
    f.geometry.addPart(p);
    return f;
}

} // namespace

TEST(FeatureStoreMetadata, RoleSchemaRoundTripsThroughReopen)
{
    const QString gpkg = freshGpkg(QStringLiteral("role_bc"));
    const Schema tmpl = featureLayerRoleTemplate(FeatureLayerRole::BoundaryCondition);
    {
        FeatureStore store;
        ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("bc_lines"), GeometryType::LineString,
                              false, tmpl));
        const Schema back = store.schema();
        ASSERT_EQ(back.count(), tmpl.count());
        for (int i = 0; i < tmpl.count(); ++i)
            expectSamePersisted(back.at(i), tmpl.at(i));
    }
    // A fresh handle reads the same thing back from the file.
    FeatureStore reopened;
    QString err;
    ASSERT_TRUE(reopened.open(gpkg, QStringLiteral("bc_lines"), &err)) << err.toStdString();
    const Schema back = reopened.schema();
    ASSERT_EQ(back.count(), tmpl.count());
    for (int i = 0; i < tmpl.count(); ++i)
        expectSamePersisted(back.at(i), tmpl.at(i));
}

TEST(FeatureStoreMetadata, EditorSchemaRestoresTheRegistryOrder)
{
    const QString gpkg = freshGpkg(QStringLiteral("role_order"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("bc_lines"), GeometryType::LineString, false,
                          featureLayerRoleTemplate(FeatureLayerRole::BoundaryCondition)));
    const Schema e = editorSchema(FeatureLayerRole::BoundaryCondition, store.schema());
    const FieldDef *reg =
        featureRoleSpec(FeatureLayerRole::BoundaryCondition).field(QStringLiteral("bc_type"));
    EXPECT_EQ(e.field(QStringLiteral("bc_type"))->choices, reg->choices);
    // And a layer read back from its own file needs no update.
    EXPECT_TRUE(planFieldUpdate(FeatureLayerRole::BoundaryCondition, store.schema(), {}).isEmpty());
}

TEST(FeatureStoreMetadata, NewFeatureTakesItsDefaults)
{
    const QString gpkg = freshGpkg(QStringLiteral("role_defaults"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("bc_lines"), GeometryType::LineString, false,
                          featureLayerRoleTemplate(FeatureLayerRole::BoundaryCondition)));

    // The drawing tools start a feature from Schema::defaultAttributes().
    Feature drawn = lineFeature();
    drawn.attributes = store.schema().defaultAttributes();
    QString err;
    const FeatureId a = store.addFeature(drawn, &err);
    ASSERT_NE(a, kInvalidFeatureId) << err.toStdString();
    Feature back;
    ASSERT_TRUE(store.feature(a, back));
    EXPECT_DOUBLE_EQ(back.attributes.value(QStringLiteral("conveyance")).toDouble(), 1.0);
    EXPECT_EQ(back.attributes.value(QStringLiteral("bc_type")).toString(), QStringLiteral("WALL"));
    EXPECT_DOUBLE_EQ(back.attributes.value(QStringLiteral("slope")).toDouble(), 0.001);

    // A writer that sets no attributes at all gets the column defaults too.
    const FeatureId b = store.addFeature(lineFeature(), &err);
    ASSERT_NE(b, kInvalidFeatureId) << err.toStdString();
    ASSERT_TRUE(store.feature(b, back));
    EXPECT_DOUBLE_EQ(back.attributes.value(QStringLiteral("conveyance")).toDouble(), 1.0);
    EXPECT_EQ(back.attributes.value(QStringLiteral("bc_type")).toString(), QStringLiteral("WALL"));
}

TEST(FeatureStoreMetadata, AddFieldWithAValueListIsVisibleToAPlainOgrHandle)
{
    if (!FeatureStore::storesFieldMetadata())
        GTEST_SKIP() << "GDAL < 3.8 stores no value lists";
    const QString gpkg = freshGpkg(QStringLiteral("role_domain"));
    FeatureStore store;
    ASSERT_TRUE(
        makeStore(store, gpkg, QStringLiteral("zones"), GeometryType::Polygon, false, Schema()));

    const FieldDef hsg =
        *featureRoleSpec(FeatureLayerRole::ParameterZone).field(QStringLiteral("hsg"));
    QString err;
    ASSERT_TRUE(store.addField(hsg, &err)) << err.toStdString();
    expectSamePersisted(store.schema().at(0), hsg);
    store.flush();

    GDALDataset *ds = static_cast<GDALDataset *>(GDALOpenEx(
        gpkg.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_TRUE(ds);
    OGRLayer *layer = ds->GetLayerByName("zones");
    ASSERT_TRUE(layer);
    const OGRFieldDefn *fd = layer->GetLayerDefn()->GetFieldDefn(0);
    ASSERT_TRUE(fd);
    EXPECT_EQ(fd->GetDomainName(), std::string("zones__hsg"));
    const OGRFieldDomain *dom = ds->GetFieldDomain(fd->GetDomainName());
    ASSERT_TRUE(dom);
    ASSERT_EQ(dom->GetDomainType(), OFDT_CODED);
    int n = 0;
    for (const OGRCodedValue *cv = static_cast<const OGRCodedFieldDomain *>(dom)->GetEnumeration();
         cv && cv->pszCode; ++cv)
        ++n;
    EXPECT_EQ(n, 7);
    EXPECT_EQ(fd->GetComment(), hsg.description.toStdString());
    GDALClose(ds);
}

TEST(FeatureStoreMetadata, ARemovedColumnComesBackWithItsValueList)
{
    if (!FeatureStore::storesFieldMetadata())
        GTEST_SKIP() << "GDAL < 3.8 stores no value lists";
    const QString gpkg = freshGpkg(QStringLiteral("role_domain_drop"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("bc_lines"), GeometryType::LineString, false,
                          featureLayerRoleTemplate(FeatureLayerRole::BoundaryCondition)));
    QString err;
    if (!store.removeField(QStringLiteral("bc_type"), &err))
        GTEST_SKIP() << "this GDAL cannot delete a GeoPackage column: " << err.toStdString();
    EXPECT_FALSE(store.schema().contains(QStringLiteral("bc_type")));

    // Re-adding it (what an undo does) works whether or not the driver could
    // delete the old list.
    const FieldDef bcType =
        *featureRoleSpec(FeatureLayerRole::BoundaryCondition).field(QStringLiteral("bc_type"));
    ASSERT_TRUE(store.addField(bcType, &err)) << err.toStdString();
    expectSamePersisted(*store.schema().field(QStringLiteral("bc_type")), bcType);

    // A different list under the same column name never rewrites a list some
    // other column may hold: it is stored, and read back, as given.
    ASSERT_TRUE(store.removeField(QStringLiteral("bc_type"), &err)) << err.toStdString();
    FieldDef shorter = bcType;
    shorter.choices.removeLast();
    ASSERT_TRUE(store.addField(shorter, &err)) << err.toStdString();
    EXPECT_TRUE(
        sameChoiceSet(store.schema().field(QStringLiteral("bc_type"))->choices, shorter.choices));
}

TEST(FeatureStoreMetadata, RenameKeepsValuesAndMetadataCanBeAttached)
{
    // An old Region layer: the quad engine's columns, no metadata.
    Schema old;
    for (const char *n : {"quad_mode", "quad_spacing", "tag"}) {
        FieldDef f;
        f.name = QLatin1String(n);
        f.type =
            (QLatin1String(n) == QLatin1String("quad_spacing")) ? FieldType::Real : FieldType::Text;
        old.append(f);
    }
    const QString gpkg = freshGpkg(QStringLiteral("role_update"));
    FeatureStore store;
    ASSERT_TRUE(
        makeStore(store, gpkg, QStringLiteral("regions"), GeometryType::Polygon, false, old));
    Feature f;
    f.geometry = squarePolygon(0.0, 0.0, 10.0);
    f.attributes.insert(QStringLiteral("quad_spacing"), 4.5);
    f.attributes.insert(QStringLiteral("tag"), QStringLiteral("park"));
    QString err;
    const FeatureId id = store.addFeature(f, &err);
    ASSERT_NE(id, kInvalidFeatureId) << err.toStdString();

    ASSERT_TRUE(store.renameField(QStringLiteral("quad_spacing"), QStringLiteral("h"), &err))
        << err.toStdString();
    EXPECT_FALSE(store.schema().contains(QStringLiteral("quad_spacing")));
    Feature back;
    ASSERT_TRUE(store.feature(id, back));
    EXPECT_DOUBLE_EQ(back.attributes.value(QStringLiteral("h")).toDouble(), 4.5);

    // Renaming onto an existing column is refused.
    EXPECT_FALSE(store.renameField(QStringLiteral("h"), QStringLiteral("tag"), &err));

    const FeatureRoleSpec &region = featureRoleSpec(FeatureLayerRole::Region);
    ASSERT_TRUE(
        store.setFieldMetadata(QStringLiteral("h"), *region.field(QStringLiteral("h")), &err))
        << err.toStdString();
    ASSERT_TRUE(
        store.setFieldMetadata(QStringLiteral("tag"), *region.field(QStringLiteral("tag")), &err))
        << err.toStdString();
    // A Fixed list attached to an existing text column, then detached again.
    FieldDef cells = *region.field(QStringLiteral("cells"));
    ASSERT_TRUE(store.setFieldMetadata(QStringLiteral("quad_mode"), cells, &err))
        << err.toStdString();

    const Schema s = store.schema();
    expectSamePersisted(*s.field(QStringLiteral("h")), *region.field(QStringLiteral("h")));
    expectSamePersisted(*s.field(QStringLiteral("tag")), *region.field(QStringLiteral("tag")));
    if (FeatureStore::storesFieldMetadata()) {
        EXPECT_TRUE(sameChoiceSet(s.field(QStringLiteral("quad_mode"))->choices, cells.choices));
    }

    cells.choices.clear();
    cells.choiceSource = ChoiceSource::None;
    ASSERT_TRUE(store.setFieldMetadata(QStringLiteral("quad_mode"), cells, &err))
        << err.toStdString();
    EXPECT_TRUE(store.schema().field(QStringLiteral("quad_mode"))->choices.isEmpty());

    // Values survive the table rebuild AlterFieldDefn may do.
    ASSERT_TRUE(store.feature(id, back));
    EXPECT_DOUBLE_EQ(back.attributes.value(QStringLiteral("h")).toDouble(), 4.5);
    EXPECT_EQ(back.attributes.value(QStringLiteral("tag")).toString(), QStringLiteral("park"));
}

TEST(FeatureStoreMetadata, TwoTablesNeverShareAValueListByAccident)
{
    if (!FeatureStore::storesFieldMetadata())
        GTEST_SKIP() << "GDAL < 3.8 stores no value lists";
    // "a" + "b_c" and "a_b" + "c" must not share a value list: neither table
    // may rewrite or delete the other's (names are "a__b_c" and "a_b__c").
    // Store "a" stays open throughout, as a FeatureLayer's handle does, and
    // that handle never sees table "a_b", created after it opened.
    FieldDef first;
    first.name = QStringLiteral("b_c");
    first.type = FieldType::Text;
    first.choiceSource = ChoiceSource::Fixed;
    for (const char *v : {"x", "y", "z"})
        first.choices.append(FieldChoice{QLatin1String(v), QString()});
    FieldDef second = first; // the same values: a shared list would be reused
    second.name = QStringLiteral("c");

    const QString gpkg = freshGpkg(QStringLiteral("domain_collision"));
    QString err;
    Schema sa;
    sa.append(first);
    FeatureStore a;
    ASSERT_TRUE(makeStore(a, gpkg, QStringLiteral("a"), GeometryType::Point, false, sa));
    {
        Schema s;
        s.append(second);
        FeatureStore ab;
        ASSERT_TRUE(makeStore(ab, gpkg, QStringLiteral("a_b"), GeometryType::Point, false, s));
    }

    // Through the old handle: change a's list, then drop its column.
    FieldDef shorter = first;
    shorter.choices.removeLast();
    ASSERT_TRUE(a.setFieldMetadata(QStringLiteral("b_c"), shorter, &err)) << err.toStdString();
    const Schema aNow = a.schema();
    EXPECT_TRUE(sameChoiceSet(aNow.field(QStringLiteral("b_c"))->choices, shorter.choices));
    // Dropping the column, where this GDAL can, must not take a_b's list along.
    const bool dropped = a.removeField(QStringLiteral("b_c"), &err);
    Q_UNUSED(dropped);

    FeatureStore ab;
    ASSERT_TRUE(ab.open(gpkg, QStringLiteral("a_b"), &err)) << err.toStdString();
    const Schema abNow = ab.schema();
    ASSERT_TRUE(abNow.field(QStringLiteral("c")));
    EXPECT_TRUE(sameChoiceSet(abNow.field(QStringLiteral("c"))->choices, second.choices));
}

TEST(FeatureStoreMetadata, TextDefaultsWithQuotesRoundTrip)
{
    FieldDef f;
    f.name = QStringLiteral("label");
    f.type = FieldType::Text;
    f.defaultValue = QStringLiteral("O'Brien's");
    Schema s;
    s.append(f);
    const QString gpkg = freshGpkg(QStringLiteral("quoted_default"));
    FeatureStore store;
    ASSERT_TRUE(makeStore(store, gpkg, QStringLiteral("t"), GeometryType::Point, false, s));
    EXPECT_EQ(store.schema().at(0).defaultValue.toString(), QStringLiteral("O'Brien's"));
}
