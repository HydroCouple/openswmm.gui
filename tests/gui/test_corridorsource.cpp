/**
 * @file      test_corridorsource.cpp
 * @brief     Tests for corridorsource.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest>
#include <QDir>
#include <QFile>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cmath>
#include <limits>
#include "mesh/corridorsource.h"

using namespace mesh;

class TestCorridorSource : public QObject
{
    Q_OBJECT
    QString m_dir, m_path, m_wkt;
    static constexpr qint64 largeFid = 9007199254740993LL;
    /**
     *
     * @brief Build a baseline selected-road source against this fixture's current GeoPackage and
     * CRS.
     * @return Owned value selecting FID seven, roads layer, width/across/along two and
     * matching source/mesh WKT. Does not read the dataset; initTestCase/init must prepare
     * members and file before use. Distances are projected EPSG:32618 metres.
     *
     */
    CorridorSource source() const
    {
        CorridorSource s;
        s.path = m_path;
        s.layerName = QStringLiteral("roads");
        s.sourceCRSWkt = s.meshCRSWkt = m_wkt;
        s.featureIds = {7};
        s.width = 2;
        s.across = 2;
        s.along = 2;
        return s;
    }
    /**
     *
     * @brief Build the baseline two-bank source with reversed feature-selection order.
     * @return Owned source() value with bankPair true, FIDs ten/seven and river-banks tag;
     * no file read or geometry validation occurs here.
     *
     */
    CorridorSource banks() const
    {
        auto s = source();
        s.bankPair = true;
        s.featureIds = {10, 7};
        s.tag = "river-banks";
        return s;
    }
    /**
     *
     * @brief Replace one existing fixture feature's geometry in place from WKT.
     * @param fid Exact signed 64-bit feature id, without floating-point conversion.
     * @param wkt Borrowed non-null valid C string consumed during parsing.
     * @return True only when dataset opening, feature lookup, parsing and SetFeature succeed.
     * Closes the dataset/destroys the feature on handled paths and transfers parsed geometry
     * to the feature. Requires the roads layer to exist; no null-layer guard or rollback.
     *
     */
    bool replaceLine(qint64 fid, const char *wkt)
    {
        auto *ds = static_cast<GDALDataset *>(GDALOpenEx(m_path.toUtf8().constData(),
                                                         GDAL_OF_VECTOR | GDAL_OF_UPDATE, nullptr,
                                                         nullptr, nullptr));
        if (!ds)
            return false;
        auto *layer = ds->GetLayerByName("roads");
        auto *feature = layer->GetFeature(fid);
        OGRGeometry *geometry = nullptr;
        bool ok =
            feature && OGRGeometryFactory::createFromWkt(wkt, nullptr, &geometry) == OGRERR_NONE;
        if (ok) {
            feature->SetGeometryDirectly(geometry);
            ok = layer->SetFeature(feature) == OGRERR_NONE;
        }
        if (feature)
            OGRFeature::DestroyFeature(feature);
        GDALClose(ds);
        return ok;
    }
    /**
     *
     * @brief Create a fixture feature with exact FID, width and road label from parsed WKT.
     * @param layer Borrowed non-null layer with width/label fields; retained during call.
     * @param fid Signed 64-bit id to request from the driver.
     * @param text Borrowed non-null WKT string consumed synchronously; geometry may be nonline.
     * @param width Numeric field value, including deliberately invalid test values.
     * @return True on CreateFeature success, false on parse/write refusal. Destroys the
     * feature and its transferred geometry before returning; mutates the open dataset,
     * without closing it, asserting assigned FID or validating corridor suitability.
     *
     */
    bool add(OGRLayer *layer, qint64 fid, const char *text, double width)
    {
        auto *f = OGRFeature::CreateFeature(layer->GetLayerDefn());
        f->SetFID(fid);
        f->SetField("width", width);
        f->SetField("label", "road");
        OGRGeometry *g = nullptr;
        if (OGRGeometryFactory::createFromWkt(text, nullptr, &g) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(f);
            return false;
        }
        f->SetGeometryDirectly(g);
        const bool ok = layer->CreateFeature(f) == OGRERR_NONE;
        OGRFeature::DestroyFeature(f);
        return ok;
    }
private slots:
    /**
     *
     * @brief Register GDAL and create the evidence directory and shared projected CRS metadata.
     * Uses SWMMVIS_CORRIDOR_TEST_OUTPUT or the current-directory workplans artifact path,
     * sets selected_roads.gpkg, and exports EPSG:32618 WKT. Writes directories, not the
     * GeoPackage yet; CPL-allocated WKT is copied then freed.
     *
     */
    void initTestCase()
    {
        GDALAllRegister();
        m_dir = qEnvironmentVariable("SWMMVIS_CORRIDOR_TEST_OUTPUT");
        if (m_dir.isEmpty())
            m_dir =
                QDir::current().absoluteFilePath("workplans/artifacts/phase_28_bank_pairs/helper");
        QVERIFY(QDir().mkpath(m_dir));
        m_path = QDir(m_dir).filePath("selected_roads.gpkg");
        OGRSpatialReference srs;
        QCOMPARE(srs.importFromEPSG(32618), OGRERR_NONE);
        char *wkt = nullptr;
        srs.exportToWkt(&wkt);
        m_wkt = QString::fromUtf8(wkt);
        CPLFree(wkt);
    }
    /**
     *
     * @brief Recreate the GeoPackage before each case with line, multiline, point, null-width and
     * bank fixtures.
     * Deletes any previous selected_roads.gpkg in the chosen evidence directory, creates
     * roads with width/label fields and FIDs seven through ten plus a 64-bit FID above 2^53.
     * Stores projected metre coordinates; the null-width feature is explicitly updated.
     * Files remain as evidence; no preservation of a pre-existing same-name file.
     *
     */
    void init()
    {
        auto *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        QVERIFY(driver);
        if (QFile::exists(m_path))
            QCOMPARE(driver->Delete(m_path.toUtf8().constData()), CE_None);
        auto *ds = driver->Create(m_path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
        QVERIFY(ds);
        OGRSpatialReference srs;
        srs.SetFromUserInput(m_wkt.toUtf8().constData());
        auto *layer = ds->CreateLayer("roads", &srs, wkbUnknown, nullptr);
        QVERIFY(layer);
        OGRFieldDefn width("width", OFTReal), label("label", OFTString);
        QCOMPARE(layer->CreateField(&width), OGRERR_NONE);
        QCOMPARE(layer->CreateField(&label), OGRERR_NONE);
        QVERIFY(add(layer, 7, "LINESTRING (500000 4500000,500010 4500000)", 4));
        QVERIFY(
            add(layer, largeFid,
                "MULTILINESTRING ((500000 4500020,500010 4500020),(500000 4500040,500010 4500040))",
                6));
        QVERIFY(add(layer, 8, "POINT (500000 4500060)", -1));
        QVERIFY(add(layer, 9, "LINESTRING (500000 4500080,500010 4500080)", -1));
        auto *f = layer->GetFeature(9);
        f->SetFieldNull(f->GetFieldIndex("width"));
        QCOMPARE(layer->SetFeature(f), OGRERR_NONE);
        OGRFeature::DestroyFeature(f);
        QVERIFY(add(layer, 10, "LINESTRING (500000 4500004,500010 4500004)", 4));
        GDALClose(ds);
    }
    /**
     *
     * @brief Require two banks to define one valid ten-quad 10-by-4-metre patch independently of
     * scalar width.
     * Checks tag, sorted selected FIDs, bank metadata, canonical source dependency, stamps
     * and repeatable geometry digest. Reversing selected id order preserves vertices/digest;
     * switching to swept mode changes digest. Does not compare mesh solver results.
     *
     */
    void bankPairDimensionsAndDigest()
    {
        auto s = banks();
        s.width = 1000; // Bank geometry, not this scalar, sets width.
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.patches.size(), 1);
        const auto &patch = r.patches.first();
        QCOMPARE(patch.tag, QString("river-banks"));
        QCOMPARE(patch.quads.size(), 10);
        QVERIFY2(validate(patch).isEmpty(), qPrintable(validate(patch)));
        QCOMPARE(QPolygonF(patch.xy).boundingRect(), QRectF(500000, 4500000, 10, 4));
        QCOMPARE(r.resolvedSources.first().featureIds, QVector<qint64>({7, 10}));
        QVERIFY(r.resolvedSources.first().bankPair);
        QVERIFY(corridorSourceFilesUnchanged(r.sourceStamps));
        QVERIFY(
            r.resolvedSources.first().sourceFiles.contains(QFileInfo(m_path).canonicalFilePath()));
        const auto again = readCorridorSources(r.resolvedSources, m_wkt);
        QVERIFY2(again.ok(), qPrintable(again.error));
        QCOMPARE(again.resolvedSources.first().geometryDigest,
                 r.resolvedSources.first().geometryDigest);
        s.featureIds = {7, 10};
        s.width = 2;
        const auto ordered = readCorridorSources({s}, m_wkt);
        QVERIFY2(ordered.ok(), qPrintable(ordered.error));
        QCOMPARE(ordered.patches.first().xy, patch.xy);
        QCOMPARE(ordered.resolvedSources.first().geometryDigest,
                 r.resolvedSources.first().geometryDigest);
        s.bankPair = false;
        const auto swept = readCorridorSources({s}, m_wkt);
        QVERIFY2(swept.ok(), qPrintable(swept.error));
        QVERIFY(swept.resolvedSources.first().geometryDigest !=
                r.resolvedSources.first().geometryDigest);
    }
    /**
     *
     * @brief Register all four combinations of forward/reversed first and second bank
     * orientation.
     * Supplies two boolean columns consumed by bankPairAsymmetricAndReversed().
     *
     */
    void bankPairAsymmetricAndReversed_data()
    {
        QTest::addColumn<bool>("reverseA");
        QTest::addColumn<bool>("reverseB");
        QTest::newRow("forward") << false << false;
        QTest::newRow("reverse-first") << true << false;
        QTest::newRow("reverse-second") << false << true;
        QTest::newRow("reverse-both") << true << true;
    }
    /**
     *
     * @brief Require asymmetric banks in every orientation to yield a valid patch retaining the
     * middle bank vertex.
     * Fetches reversal flags, rewrites both lines and checks one patch, explicit point
     * 500003/4500005 and 10-by-6-metre bounds; no cross-row byte-identical patch comparison.
     *
     */
    void bankPairAsymmetricAndReversed()
    {
        QFETCH(bool, reverseA);
        QFETCH(bool, reverseB);
        QVERIFY(replaceLine(7, reverseA ? "LINESTRING (500010 4500000,500000 4500000)"
                                        : "LINESTRING (500000 4500000,500010 4500000)"));
        QVERIFY(replaceLine(10, reverseB
                                    ? "LINESTRING (500010 4500006,500003 4500005,500000 4500004)"
                                    : "LINESTRING (500000 4500004,500003 4500005,500010 4500006)"));
        const auto r = readCorridorSources({banks()}, m_wkt);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.patches.size(), 1);
        const auto &patch = r.patches.first();
        QVERIFY2(validate(patch).isEmpty(), qPrintable(validate(patch)));
        QVERIFY(patch.xy.contains(QPointF(500003, 4500005)));
        QCOMPARE(QPolygonF(patch.xy).boundingRect(), QRectF(500000, 4500000, 10, 6));
    }
    /**
     *
     * @brief Register thirteen invalid bank selections, geometry types, metadata and spacing
     * scenarios.
     * Includes missing/duplicate/wrong-count FIDs, point/multipart/closed banks, width-field
     * use, negative/NaN width, negative along spacing and zero across count.
     *
     */
    void invalidBankPair_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const char *s : {"one", "three", "duplicate", "duplicate-three", "missing", "point",
                              "multipart", "closed", "width-field", "negative-width-metadata",
                              "nan-width-metadata", "negative-along", "zero-across"})
            QTest::newRow(s) << QString::fromLatin1(s);
    }
    /**
     *
     * @brief Require every invalid bank scenario to discard an earlier valid source's partial
     * results.
     * Mutates selection/metadata or fixture geometry for the fetched scenario, then checks
     * failure, nonempty error and empty patches/resolved sources/stamps. No input mutation
     * rollback is expected; each case receives a freshly recreated dataset.
     *
     */
    void invalidBankPair()
    {
        QFETCH(QString, scenario);
        auto s = banks();
        if (scenario == "one")
            s.featureIds = {7};
        if (scenario == "three")
            s.featureIds = {7, 9, 10};
        if (scenario == "duplicate")
            s.featureIds = {7, 7};
        if (scenario == "duplicate-three")
            s.featureIds = {7, 10, 10};
        if (scenario == "missing")
            s.featureIds = {7, 12345};
        if (scenario == "point")
            s.featureIds = {7, 8};
        if (scenario == "multipart")
            s.featureIds = {7, largeFid};
        if (scenario == "closed")
            QVERIFY(replaceLine(
                10, "LINESTRING (500000 4500004,500010 4500004,500005 4500006,500000 4500004)"));
        if (scenario == "width-field")
            s.widthField = "width";
        if (scenario == "negative-width-metadata")
            s.width = -1;
        if (scenario == "nan-width-metadata")
            s.width = std::numeric_limits<double>::quiet_NaN();
        if (scenario == "negative-along")
            s.along = -1;
        if (scenario == "zero-across")
            s.across = 0;
        // A previously successful source must not escape on later failure.
        const auto r = readCorridorSources({source(), s}, m_wkt);
        QVERIFY2(!r.ok(), qPrintable(scenario));
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.patches.isEmpty());
        QVERIFY(r.resolvedSources.isEmpty());
        QVERIFY(r.sourceStamps.isEmpty());
    }
    /**
     *
     * @brief Register exact first/second bank FIDs to exercise either dependency changing.
     * Supplies qint64 seven and ten to changedBankGeometry().
     *
     */
    void changedBankGeometry_data()
    {
        QTest::addColumn<qint64>("fid");
        QTest::newRow("first-bank") << qint64(7);
        QTest::newRow("second-bank") << qint64(10);
    }
    /**
     *
     * @brief Require rereading resolved bank sources after either bank geometry changes to fail
     * atomically.
     * Checks a changed diagnostic and empty patches/resolved sources; does not assert the
     * stamp vector or restore the edited dataset within this case.
     *
     */
    void changedBankGeometry()
    {
        QFETCH(qint64, fid);
        const auto first = readCorridorSources({banks()}, m_wkt);
        QVERIFY2(first.ok(), qPrintable(first.error));
        QVERIFY(replaceLine(fid, fid == 7 ? "LINESTRING (500000 4499999,500010 4500000)"
                                          : "LINESTRING (500000 4500004,500010 4500005)"));
        const auto r = readCorridorSources(first.resolvedSources, m_wkt);
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("changed", Qt::CaseInsensitive));
        QVERIFY(r.patches.isEmpty());
        QVERIFY(r.resolvedSources.isEmpty());
    }
    /**
     *
     * @brief Require ninth-poll cancellation to clear all results and a later file timestamp
     * change to fail stamp checks.
     * The synchronous callback increments borrowed local state. After a successful bank
     * read, advances modification time by ten seconds and requires a changed diagnostic;
     * does not depend on wall-clock waiting or rerun meshing.
     *
     */
    void bankPairCancellationAndStamps()
    {
        int checks = 0;
        const auto cancelled =
            readCorridorSources({source(), banks()}, m_wkt, [&checks] { return ++checks >= 9; });
        QVERIFY(!cancelled.ok());
        QVERIFY(cancelled.error.contains("cancel", Qt::CaseInsensitive));
        QVERIFY(cancelled.patches.isEmpty());
        QVERIFY(cancelled.resolvedSources.isEmpty());
        QVERIFY(cancelled.sourceStamps.isEmpty());
        const auto r = readCorridorSources({banks()}, m_wkt);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QFile file(m_path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(file.fileTime(QFileDevice::FileModificationTime).addSecs(10),
                                 QFileDevice::FileModificationTime));
        file.close();
        QString error;
        QVERIFY(!corridorSourceFilesUnchanged(r.sourceStamps, &error));
        QVERIFY(error.contains("changed"));
    }
    /**
     *
     * @brief Require selected line/multipart features to yield three valid width-field patches
     * without narrowing a large FID.
     * Checks sorted IDs including 2^53+1, resolved-source count, nonempty/repeatable digest,
     * canonical source dependency and fresh stamps. Pins the first patch's signed four-metre
     * width; does not compare every generated vertex or perform a solver run.
     *
     */
    void selectedLineAndMultipart()
    {
        auto s = source();
        s.featureIds = {largeFid, 7};
        s.widthField = "width";
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.patches.size(), 3);
        QCOMPARE(r.resolvedSources.size(), 1);
        QCOMPARE(r.resolvedSources[0].featureIds, QVector<qint64>({7, largeFid}));
        QVERIFY(!r.resolvedSources[0].geometryDigest.isEmpty());
        QVERIFY(r.resolvedSources[0].sourceFiles.contains(QFileInfo(m_path).canonicalFilePath()));
        QCOMPARE(r.patches[0].xy[0].y() - r.patches[0].xy[2].y(), -4.0);
        for (const auto &p : r.patches)
            QVERIFY2(validate(p).isEmpty(), qPrintable(validate(p)));
        const auto again = readCorridorSources(r.resolvedSources, m_wkt);
        QVERIFY2(again.ok(), qPrintable(again.error));
        QCOMPARE(again.resolvedSources[0].geometryDigest, r.resolvedSources[0].geometryDigest);
        QVERIFY(corridorSourceFilesUnchanged(r.sourceStamps));
    }
    /**
     *
     * @brief Require a scalar-width road to produce one patch tagged river with two-metre
     * transverse width.
     * Checks absolute first-edge separation only, without digest/stamp or full topology
     * comparison beyond successful read.
     *
     */
    void constantWidthAndExplicitTag()
    {
        auto s = source();
        s.tag = "river";
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.patches.size(), 1);
        QCOMPARE(r.patches[0].tag, QString("river"));
        QCOMPARE(std::abs(r.patches[0].xy[0].y() - r.patches[0].xy[2].y()), 2.0);
    }
    /**
     *
     * @brief Require an empty selected-id list to fail with a selection diagnostic.
     * Does not separately inspect returned patch or source vectors.
     *
     */
    void emptySelection()
    {
        auto s = source();
        s.featureIds.clear();
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("select", Qt::CaseInsensitive));
    }
    /**
     *
     * @brief Require a missing FID after a valid selection to fail without patches or resolved
     * sources.
     * Checks diagnostic mentions 12345; no separate stamp-vector assertion.
     *
     */
    void missingFeatureAtomic()
    {
        auto s = source();
        s.featureIds = {7, 12345};
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY(!r.ok());
        QVERIFY(r.patches.isEmpty());
        QVERIFY(r.resolvedSources.isEmpty());
        QVERIFY(r.error.contains("12345"));
    }
    /**
     *
     * @brief Require selected point FID eight to fail and identify that feature in its error.
     * Does not inspect every result vector or other unsupported geometry families.
     *
     */
    void nonLine()
    {
        auto s = source();
        s.featureIds = {8};
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("8"));
    }
    /**
     *
     * @brief Register missing, text-valued and explicitly null width-field scenarios.
     * Supplies field name and exact feature id to badWidth(); numeric negative widths are
     * covered separately rather than by these rows.
     *
     */
    void badWidth_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<qint64>("fid");
        QTest::newRow("missing") << QString("absent") << qint64(7);
        QTest::newRow("text") << QString("label") << qint64(7);
        QTest::newRow("null") << QString("width") << qint64(9);
    }
    /**
     *
     * @brief Require each fetched missing/text/null width field to fail with a width diagnostic.
     * Sets the selected feature and field override; does not assert all result vectors or
     * compare an exact error string.
     *
     */
    void badWidth()
    {
        QFETCH(QString, field);
        QFETCH(qint64, fid);
        auto s = source();
        s.widthField = field;
        s.featureIds = {fid};
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("width", Qt::CaseInsensitive));
    }
    /**
     *
     * @brief Require absent source CRS metadata to fail with a CRS diagnostic.
     * Leaves projected fixture data unchanged and does not inspect output vectors.
     *
     */
    void missingCRS()
    {
        auto s = source();
        s.sourceCRSWkt.clear();
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("CRS"));
    }
    /**
     *
     * @brief Require a requested target CRS differing from stored source mesh CRS to fail.
     * Passes EPSG:32617 against stored EPSG:32618 and checks a CRS diagnostic; no reprojection
     * success or exact error-text assertion.
     *
     */
    void changedMeshCRS()
    {
        auto s = source();
        const auto r = readCorridorSources({s}, "EPSG:32617");
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("CRS"));
    }
    /**
     *
     * @brief Require a geographic EPSG:4326 target to fail with a planar-coordinate diagnostic.
     * Changes declared mesh target only, without performing geodesic corridor construction.
     *
     */
    void geographicTarget()
    {
        auto s = source();
        s.meshCRSWkt = "EPSG:4326";
        const auto r = readCorridorSources({s}, s.meshCRSWkt);
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("planar", Qt::CaseInsensitive));
    }
    /**
     *
     * @brief Require a selected width-field edit to invalidate a previously resolved geometry
     * digest.
     * Reads successfully first, writes width eight to FID seven in place, then checks a
     * changed diagnostic and empty patches. Does not separately compare source stamps.
     *
     */
    void changedSelectedData()
    {
        auto s = source();
        s.widthField = "width";
        const auto first = readCorridorSources({s}, m_wkt);
        QVERIFY2(first.ok(), qPrintable(first.error));
        auto *ds = static_cast<GDALDataset *>(GDALOpenEx(m_path.toUtf8().constData(),
                                                         GDAL_OF_VECTOR | GDAL_OF_UPDATE, nullptr,
                                                         nullptr, nullptr));
        QVERIFY(ds);
        auto *layer = ds->GetLayerByName("roads");
        auto *f = layer->GetFeature(7);
        QVERIFY(f);
        f->SetField("width", 8.0);
        QCOMPARE(layer->SetFeature(f), OGRERR_NONE);
        OGRFeature::DestroyFeature(f);
        GDALClose(ds);
        const auto r = readCorridorSources(first.resolvedSources, m_wkt);
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("changed", Qt::CaseInsensitive));
        QVERIFY(r.patches.isEmpty());
    }
    /**
     *
     * @brief Require an always-cancel callback to fail with a cancel diagnostic and no patches.
     * No timing/thread cancellation or callback-count assertion is included.
     *
     */
    void cancelled()
    {
        const auto r = readCorridorSources({source()}, m_wkt, [] { return true; });
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("cancel", Qt::CaseInsensitive));
        QVERIFY(r.patches.isEmpty());
    }
    /**
     *
     * @brief Require no requested sources to succeed with no patches even with an empty target
     * CRS.
     * Does not assert diagnostic or other empty vectors separately.
     *
     */
    void emptySources()
    {
        const auto r = readCorridorSources({}, {});
        QVERIFY(r.ok());
        QVERIFY(r.patches.isEmpty());
    }
    /**
     *
     * @brief Require eighth-poll cancellation while processing selected line/multipart features
     * to discard results.
     * Callback borrows a live counter synchronously; checks cancel diagnostic and empty
     * patches/resolved sources without asserting the stamp vector.
     *
     */
    void lateCancellationIsAtomic()
    {
        auto s = source();
        s.featureIds = {7, largeFid};
        int checks = 0;
        const auto r = readCorridorSources({s}, m_wkt, [&checks] { return ++checks >= 8; });
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("cancel", Qt::CaseInsensitive));
        QVERIFY(r.patches.isEmpty());
        QVERIFY(r.resolvedSources.isEmpty());
    }
    /**
     *
     * @brief Require advancing a successfully read source file's timestamp by ten seconds to
     * invalidate stamps.
     * Checks a nonempty initial stamp set and changed diagnostic; mutates the evidence file
     * metadata without altering its geometry or rerunning corridor construction.
     *
     */
    void changedDependencyAfterRead()
    {
        const auto first = readCorridorSources({source()}, m_wkt);
        QVERIFY2(first.ok(), qPrintable(first.error));
        QVERIFY(!first.sourceStamps.isEmpty());
        QFile file(m_path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(file.fileTime(QFileDevice::FileModificationTime).addSecs(10),
                                 QFileDevice::FileModificationTime));
        file.close();
        QString error;
        QVERIFY(!corridorSourceFilesUnchanged(first.sourceStamps, &error));
        QVERIFY(error.contains("changed"));
    }
    /**
     *
     * @brief Register zero/negative/NaN scalar widths and negative along-spacing input pairs.
     * Supplies doubles to invalidSpacingAndWidth(); across count remains the baseline two.
     *
     */
    void invalidSpacingAndWidth_data()
    {
        QTest::addColumn<double>("width");
        QTest::addColumn<double>("along");
        QTest::newRow("zero-width") << 0.0 << 2.0;
        QTest::newRow("negative-width") << -1.0 << 2.0;
        QTest::newRow("nan-width") << std::numeric_limits<double>::quiet_NaN() << 2.0;
        QTest::newRow("negative-spacing") << 2.0 << -1.0;
    }
    /**
     *
     * @brief Require each fetched invalid scalar width/along-spacing pair to fail without
     * patches.
     * Does not inspect error wording or all other output vectors.
     *
     */
    void invalidSpacingAndWidth()
    {
        QFETCH(double, width);
        QFETCH(double, along);
        auto s = source();
        s.width = width;
        s.along = along;
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY(!r.ok());
        QVERIFY(r.patches.isEmpty());
    }
    /**
     *
     * @brief Require an unprojectable second vertex to fail the whole road instead of silently
     * joining its neighbours.
     * Rewrites a feature with declared geographic longitude/latitude including latitude
     * 100, then checks vertex-two diagnostic and no patches. Dataset layer metadata remains
     * its fixture CRS; declared sourceCRSWkt drives this test's transformation.
     *
     */
    void failedProjectionRejectsWholeFeature()
    {
        // The second latitude cannot project. It must not be omitted, leaving
        // the first and third vertices connected into a plausible wrong road.
        auto s = source();
        s.sourceCRSWkt = "EPSG:4326";
        auto *ds = static_cast<GDALDataset *>(GDALOpenEx(m_path.toUtf8().constData(),
                                                         GDAL_OF_VECTOR | GDAL_OF_UPDATE, nullptr,
                                                         nullptr, nullptr));
        QVERIFY(ds);
        auto *layer = ds->GetLayerByName("roads");
        auto *f = layer->GetFeature(7);
        OGRLineString line;
        line.addPoint(-75, 40);
        line.addPoint(-75, 100);
        line.addPoint(-74.999, 40);
        f->SetGeometry(&line);
        QCOMPARE(layer->SetFeature(f), OGRERR_NONE);
        OGRFeature::DestroyFeature(f);
        GDALClose(ds);
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("vertex 2"));
        QVERIFY(r.patches.isEmpty());
    }
    /**
     *
     * @brief Require geographic longitude/latitude to transform in traditional x/y order into the
     * projected target.
     * Rewrites a short road near longitude -75/latitude 40 and checks first patch x within
     * two metres of 500000 and y between 4.4/4.5 million. Does not check exact transformed
     * vertices or update the dataset's layer CRS metadata.
     *
     */
    void reprojectsTraditionalXY()
    {
        auto s = source();
        s.sourceCRSWkt = "EPSG:4326";
        auto *ds = static_cast<GDALDataset *>(GDALOpenEx(m_path.toUtf8().constData(),
                                                         GDAL_OF_VECTOR | GDAL_OF_UPDATE, nullptr,
                                                         nullptr, nullptr));
        QVERIFY(ds);
        auto *layer = ds->GetLayerByName("roads");
        auto *f = layer->GetFeature(7);
        OGRLineString line;
        line.addPoint(-75, 40);
        line.addPoint(-74.999, 40);
        f->SetGeometry(&line);
        QCOMPARE(layer->SetFeature(f), OGRERR_NONE);
        OGRFeature::DestroyFeature(f);
        GDALClose(ds);
        const auto r = readCorridorSources({s}, m_wkt);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY(std::abs(r.patches[0].xy[0].x() - 500000) < 2);
        QVERIFY(r.patches[0].xy[0].y() > 4400000);
        QVERIFY(r.patches[0].xy[0].y() < 4500000);
    }
};
QTEST_APPLESS_MAIN(TestCorridorSource)
#include "test_corridorsource.moc"
