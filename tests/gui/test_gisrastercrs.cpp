/*!
 * \file   test_gisrastercrs.cpp
 * \brief  QtTest: a GIS raster layer adopts the CRS its file declares and
 *         reprojects onto a canvas in a different CRS.
 *
 * Companion to the vector-side cases in test_gisvectorpopulate.cpp. The vector
 * layer had a real defect here — rebuildTransform() was reachable only from
 * onCanvasCRSChanged(), so a layer added to a canvas whose CRS never changed
 * afterwards drew raw file coordinates. The raster layer takes a different
 * route (GDALCreateGenImgProjTransformer with explicit source/destination
 * projections in warpToCanvas(), plus an extent transform in
 * buildWindowedSource()), so it was NOT affected — these cases exist to pin
 * that down, because nothing covered raster CRS at all.
 */

#include "layers/gisrasterlayer.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

namespace {

QString outDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_OUT", QStringLiteral("."));
}

QString tifPath() { return outDir() + QStringLiteral("/crs_utm33n.tif"); }

/*! A 100x100 GeoTIFF in EPSG:32633 (UTM 33N) whose origin is 500000 E /
 *  4601000 N at 10 m pixels — a 1 km square around 15 E, 41.5 N. Every pixel
 *  is 200 so any rendered coverage is unambiguous against a 0 background. */
bool buildTif()
{
    GDALAllRegister();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) return false;

    const QByteArray p = tifPath().toUtf8();
    if (QFile::exists(tifPath())) QFile::remove(tifPath());

    GDALDataset *ds = drv->Create(p.constData(), 100, 100, 1, GDT_Byte, nullptr);
    if (!ds) return false;

    double gt[6] = { 500000.0, 10.0, 0.0, 4601000.0, 0.0, -10.0 };
    ds->SetGeoTransform(gt);

    OGRSpatialReference srs;
    if (srs.importFromEPSG(32633) != OGRERR_NONE) { GDALClose(ds); return false; }
    char *wkt = nullptr;
    srs.exportToWkt(&wkt);
    if (wkt) { ds->SetProjection(wkt); CPLFree(wkt); }

    std::vector<GByte> row(100, 200);
    for (int y = 0; y < 100; ++y) {
        if (ds->GetRasterBand(1)->RasterIO(GF_Write, 0, y, 100, 1, row.data(),
                                           100, 1, GDT_Byte, 0, 0) != CE_None) {
            GDALClose(ds);
            return false;
        }
    }
    GDALClose(ds);
    return true;
}

} // namespace

class TestGisRasterCRS : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(buildTif()); }

    /*! The layer adopts the CRS declared by the file. */
    void rasterLayerAdoptsFileCRS()
    {
        GISRasterLayer layer(tifPath());
        SpatialReferenceSystem *s = layer.srs();
        QVERIFY2(s != nullptr,
                 "raster layer has no SRS after opening a file declaring EPSG:32633");
        QCOMPARE(s->code(), 32633);
        QVERIFY(s->isProjected());
    }

    /*! Rendering onto a WGS84 canvas puts the data at ~15 E / 41.5 N.
     *
     *  Two extents are rendered: the correct lon/lat footprint (must show
     *  coverage) and an unrelated part of the world (must not). Together they
     *  distinguish "reprojected" from "painting everywhere", which a single
     *  positive assertion cannot.
     */
    void rasterReprojectsOntoDifferentCanvasCRS()
    {
        GISRasterLayer layer(tifPath());
        SpatialReferenceSystem canvas(QStringLiteral("EPSG"), 4326);

        // Tiles are produced asynchronously (fetchCache enqueues; workers warp;
        // repaintRequested fires when one lands), so a single render right
        // after fetchCache paints nothing. Pump until the layer stops asking
        // for repaints, then draw.
        const auto coverage = [&](const MapExtent &e) {
            QImage img(64, 64, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            layer.setViewportSize(img.width(), img.height());

            for (int round = 0; round < 40; ++round) {
                layer.fetchCache(e, img.size(), &canvas);
                QSignalSpy repaint(&layer, &GISRasterLayer::repaintRequested);
                if (!repaint.wait(250) && round > 2)
                    break;   // settled: no more tiles arriving
            }

            QPainter pr(&img);
            layer.render(&pr, e, img.size(), &canvas);
            pr.end();

            int painted = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) != 0) ++painted;
            return painted;
        };

        // ~15 E, 41.5 N with a margin — where UTM 33N 500000/4601000 lands.
        const int atLonLat = coverage(MapExtent(14.5, 41.0, 15.5, 42.0));

        // A genuinely different part of the world, in valid degrees. (The raw
        // UTM numbers are NOT a usable control: 499000 degrees wraps to a real
        // longitude, so coverage there would prove nothing.)
        const int atElsewhere = coverage(MapExtent(-100.0, 10.0, -99.0, 11.0));

        QVERIFY2(atLonLat > 0,
                 qPrintable(QStringLiteral("no raster coverage at the reprojected "
                                           "lon/lat footprint (painted=%1)")
                                .arg(atLonLat)));
        QVERIFY2(atElsewhere == 0,
                 qPrintable(QStringLiteral("raster painted at 100 W / 10 N, nowhere "
                                           "near its UTM 33N footprint (painted=%1)")
                                .arg(atElsewhere)));
    }
};

QTEST_MAIN(TestGisRasterCRS)
#include "test_gisrastercrs.moc"
