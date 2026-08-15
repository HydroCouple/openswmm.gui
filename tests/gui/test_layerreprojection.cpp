/*!
 * \file   test_layerreprojection.cpp
 * \brief  Smoke test: on-the-fly coordinate reprojection primitive.
 *
 * Every layer that renders data in a CRS different from the canvas CRS
 * relies on SpatialReferenceSystem::createTransformationTo() to build an
 * OGR coordinate transformation. If that primitive drifts, every layer's
 * "project on the fly" behaviour breaks silently.
 *
 * This test verifies the round trip for a known point and confirms the
 * axis ordering used across the codebase (traditional GIS: lon/lat,
 * x/y — not GDAL 3's default of lat/lon).
 */

#include "map/spatialreferencesystem.h"

#include <ogr_spatialref.h>

#include <QTest>
#include <QCoreApplication>

#include <memory>

class TestLayerReprojection : public QObject
{
    Q_OBJECT
private slots:
    void wgs84ToWebMercatorMatchesReference();
    void webMercatorToWgs84RoundTripWithinTolerance();
    void selfTransformIsIdentity();
};

namespace {

// Deleter so unique_ptr can own an OGRCoordinateTransformation.
struct OgrCtDeleter
{
    void operator()(OGRCoordinateTransformation *ct) const
    {
        if (ct) OGRCoordinateTransformation::DestroyCT(ct);
    }
};
using OgrCtPtr = std::unique_ptr<OGRCoordinateTransformation, OgrCtDeleter>;

} // namespace

// NYC City Hall in WGS84 (lon, lat) → approx (-8238310.24, 4970071.58) m
// in Web Mercator. Reference values computed from the standard Mercator
// formula; tolerance accounts for PROJ grid-shift refinements.
void TestLayerReprojection::wgs84ToWebMercatorMatchesReference()
{
    SpatialReferenceSystem wgs84 (QStringLiteral("EPSG"), 4326);
    SpatialReferenceSystem web   (QStringLiteral("EPSG"), 3857);

    OgrCtPtr ct(wgs84.createTransformationTo(web));
    QVERIFY2(ct, "createTransformationTo returned nullptr");

    double x = -74.006, y = 40.7128;
    QVERIFY(ct->Transform(1, &x, &y));

    QVERIFY2(qAbs(x - (-8238310.24)) < 1.0, qPrintable(QStringLiteral("x=%1").arg(x)));
    QVERIFY2(qAbs(y - ( 4970071.58)) < 1.0, qPrintable(QStringLiteral("y=%1").arg(y)));
}

void TestLayerReprojection::webMercatorToWgs84RoundTripWithinTolerance()
{
    SpatialReferenceSystem wgs84 (QStringLiteral("EPSG"), 4326);
    SpatialReferenceSystem web   (QStringLiteral("EPSG"), 3857);

    OgrCtPtr fwd(wgs84.createTransformationTo(web));
    OgrCtPtr rev(web.createTransformationTo(wgs84));
    QVERIFY(fwd && rev);

    const double lon0 = -122.4194, lat0 = 37.7749;  // SF
    double x = lon0, y = lat0;
    QVERIFY(fwd->Transform(1, &x, &y));
    QVERIFY(rev->Transform(1, &x, &y));

    QVERIFY2(qAbs(x - lon0) < 1e-6, qPrintable(QStringLiteral("lon=%1").arg(x)));
    QVERIFY2(qAbs(y - lat0) < 1e-6, qPrintable(QStringLiteral("lat=%1").arg(y)));
}

void TestLayerReprojection::selfTransformIsIdentity()
{
    SpatialReferenceSystem wgs84(QStringLiteral("EPSG"), 4326);

    OgrCtPtr ct(wgs84.createTransformationTo(wgs84));
    QVERIFY(ct);

    double x = 10.0, y = 20.0;
    QVERIFY(ct->Transform(1, &x, &y));
    QCOMPARE(x, 10.0);
    QCOMPARE(y, 20.0);
}

QTEST_MAIN(TestLayerReprojection)
#include "test_layerreprojection.moc"
