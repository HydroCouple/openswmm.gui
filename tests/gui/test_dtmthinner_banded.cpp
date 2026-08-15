/*!
 * \file   test_dtmthinner_banded.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Banded (tiled) terrain thinning must be bit-identical to the untiled run:
 * forcing tiny per-band budgets via DTMThinnerLimits and comparing against
 * the default single-band path proves the halo/ghost-zone machinery, the
 * global lattice, NoData handling across seams, and the row-major output
 * ordering.  Also covers the progress/cancel callback and the retained-
 * points / too-wide-grid guard rails.
 */
#include <QtTest>
#include <QPointF>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include "mesh/dtmthinner.h"

#include <gdal_priv.h>

#include <cmath>
#include <limits>
#include <vector>

using mesh::DTMThinner;
using mesh::DTMThinnerLimits;
using mesh::DTMThinnerOptions;

namespace {

constexpr double kNoData = -9999.0;

// Per-grid-point working-set constant mirrored from dtmthinner.cpp — used
// only to size the forced-banding budgets in these tests.
constexpr qint64 kBytesPerGridPoint = 46;

/*! Build a w×h Float64 GTiff, north-up, origin (0, h), pixel size 1.
 *  Elevation = gentle plane + sinusoidal ridges so successive thinning
 *  passes each remove something (pass-1-only terrain would make the halo
 *  machinery untestable).  Optionally punch a NoData block spanning
 *  [ndRow0, ndRow1) x [20, 44) so band seams cross the hole. */
QString buildTerrain(QTemporaryDir &dir, const QString &name, int w, int h,
                     int ndRow0 = -1, int ndRow1 = -1)
{
    GDALAllRegister();

    const QString path = dir.filePath(name);
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) return {};
    GDALDataset *ds = drv->Create(path.toUtf8().constData(), w, h, 1,
                                  GDT_Float64, nullptr);
    if (!ds) return {};

    double geo[6] = {0.0, 1.0, 0.0, double(h), 0.0, -1.0};
    ds->SetGeoTransform(geo);

    std::vector<double> buf(size_t(w) * size_t(h), 0.0);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            buf[size_t(r) * w + c] = 0.05 * c + 0.03 * r
                                   + 2.0 * std::sin(c * 0.35) * std::cos(r * 0.25);
    if (ndRow0 >= 0)
        for (int r = ndRow0; r < ndRow1 && r < h; ++r)
            for (int c = 20; c < 44 && c < w; ++c)
                buf[size_t(r) * w + c] = kNoData;

    ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, w, h, buf.data(),
                                   w, h, GDT_Float64, 0, 0);
    ds->GetRasterBand(1)->SetNoDataValue(kNoData);
    GDALClose(ds);
    return path;
}

/*! Budget that forces multi-band processing with ~coreRows-row cores for a
 *  grid `cols` wide when the halo is `halo` rows. */
qint64 forcedBandBudget(int cols, int halo, int coreRows)
{
    return kBytesPerGridPoint * qint64(cols) * qint64(coreRows + 2 * halo);
}

} // namespace

class TestDTMThinnerBanded : public QObject
{
    Q_OBJECT

private:
    /*! Bitwise comparison of two thinning runs (points + elevations). */
    void compareRuns(const QVector<QPointF> &a, const QVector<double> &az,
                     const QVector<QPointF> &b, const QVector<double> &bz)
    {
        QCOMPARE(b.size(), a.size());
        QCOMPARE(bz.size(), az.size());
        QCOMPARE(az.size(), a.size());
        for (int i = 0; i < a.size(); ++i)
        {
            QVERIFY2(a[i].x() == b[i].x() && a[i].y() == b[i].y(),
                     qPrintable(QStringLiteral(
                         "point %1 differs: (%2, %3) vs (%4, %5)")
                         .arg(i).arg(a[i].x()).arg(a[i].y())
                         .arg(b[i].x()).arg(b[i].y())));
            QVERIFY2(az[i] == bz[i],
                     qPrintable(QStringLiteral("z %1 differs: %2 vs %3")
                         .arg(i).arg(az[i]).arg(bz[i])));
        }
    }

private slots:

    void multiBand_bitParity_iter3()
    {
        QTemporaryDir dir;
        DTMThinner t;
        QVERIFY(t.open(buildTerrain(dir, "t96.tif", 96, 96)));

        const MapExtent domain(0.0, 0.0, 96.0, 96.0);
        DTMThinnerOptions opts;
        opts.gridSpacing        = 1.0;
        opts.normalDotThreshold = 0.95;
        opts.maxIterations      = 3;

        QVector<double> zA, zB;
        const auto a = t.generatePoints(domain, opts, &zA);
        QVERIFY2(t.errorMsg().isEmpty(), qPrintable(t.errorMsg()));
        QVERIFY(!a.isEmpty());

        DTMThinnerLimits lim;
        lim.maxGridBytes = forcedBandBudget(96, /*halo*/ 3, /*coreRows*/ 4);
        const auto b = t.generatePoints(domain, opts, &zB, {}, lim);
        QVERIFY2(t.errorMsg().isEmpty(), qPrintable(t.errorMsg()));

        compareRuns(a, zA, b, zB);
    }

    void multiBand_bitParity_convergence()
    {
        // "(unlimited)" passes: multi-band caps at kMaxThinningHalo (64).
        // Parity with the untiled run holds when the untiled run converges in
        // <= 64 passes — this smooth terrain converges in far fewer.  The
        // halo-64 minimum band height (130 rows) needs a tall grid to still
        // force multiple bands.
        QTemporaryDir dir;
        DTMThinner t;
        QVERIFY(t.open(buildTerrain(dir, "t64x160.tif", 64, 160)));

        const MapExtent domain(0.0, 0.0, 64.0, 160.0);
        DTMThinnerOptions opts;
        opts.gridSpacing        = 1.0;
        opts.normalDotThreshold = 0.90;
        opts.maxIterations      = 0;   // unlimited

        QVector<double> zA, zB;
        const auto a = t.generatePoints(domain, opts, &zA);
        QVERIFY2(t.errorMsg().isEmpty(), qPrintable(t.errorMsg()));
        QVERIFY(!a.isEmpty());

        DTMThinnerLimits lim;
        lim.maxGridBytes =
            forcedBandBudget(64, DTMThinner::kMaxThinningHalo, /*coreRows*/ 4);
        const auto b = t.generatePoints(domain, opts, &zB, {}, lim);
        QVERIFY2(t.errorMsg().isEmpty(), qPrintable(t.errorMsg()));

        compareRuns(a, zA, b, zB);
    }

    void noDataSpansSeam()
    {
        // NoData rows 37-45 cross several 4-row band seams; parity must hold
        // and points must survive adjacent to the hole.
        QTemporaryDir dir;
        DTMThinner t;
        QVERIFY(t.open(buildTerrain(dir, "t96nd.tif", 96, 96, 37, 45)));

        const MapExtent domain(0.0, 0.0, 96.0, 96.0);
        DTMThinnerOptions opts;
        opts.gridSpacing        = 1.0;
        opts.normalDotThreshold = 0.95;
        opts.maxIterations      = 3;

        QVector<double> zA, zB;
        const auto a = t.generatePoints(domain, opts, &zA);
        QVERIFY(!a.isEmpty());

        DTMThinnerLimits lim;
        lim.maxGridBytes = forcedBandBudget(96, 3, 4);
        const auto b = t.generatePoints(domain, opts, &zB, {}, lim);
        QVERIFY2(t.errorMsg().isEmpty(), qPrintable(t.errorMsg()));

        compareRuns(a, zA, b, zB);
    }

    void latticeContinuity()
    {
        // Every banded point must sit bitwise on the GLOBAL half-step
        // lattice: float(x0 + c*step) — no per-band origin drift.
        QTemporaryDir dir;
        DTMThinner t;
        QVERIFY(t.open(buildTerrain(dir, "t96lat.tif", 96, 96)));

        const MapExtent domain(0.0, 0.0, 96.0, 96.0);
        DTMThinnerOptions opts;
        opts.gridSpacing        = 1.0;
        opts.normalDotThreshold = 0.95;
        opts.maxIterations      = 3;

        DTMThinnerLimits lim;
        lim.maxGridBytes = forcedBandBudget(96, 3, 4);
        QVector<double> z;
        const auto pts = t.generatePoints(domain, opts, &z, {}, lim);
        QVERIFY(!pts.isEmpty());

        const double x0 = domain.xMin() + 0.5, y0 = domain.yMin() + 0.5;
        for (const QPointF &p : pts)
        {
            const double c = std::round(p.x() - x0);
            const double r = std::round(p.y() - y0);
            QVERIFY(c >= 0.0 && r >= 0.0);
            QCOMPARE(p.x(), double(float(x0 + c)));
            QCOMPARE(p.y(), double(float(y0 + r)));
        }
    }

    void cancelViaCallback()
    {
        QTemporaryDir dir;
        DTMThinner t;
        QVERIFY(t.open(buildTerrain(dir, "t96c.tif", 96, 96)));

        const MapExtent domain(0.0, 0.0, 96.0, 96.0);
        DTMThinnerOptions opts;
        opts.gridSpacing   = 1.0;
        opts.maxIterations = 3;

        DTMThinnerLimits lim;
        lim.maxGridBytes = forcedBandBudget(96, 3, 4);

        int calls = 0;
        QVector<double> z{99.0};   // pre-seeded: must come back EMPTY
        const auto pts = t.generatePoints(
            domain, opts, &z,
            [&calls](double) { return ++calls < 5; }, lim);

        QVERIFY(pts.isEmpty());
        QVERIFY(z.isEmpty());
        QVERIFY2(t.errorMsg().contains(QStringLiteral("cancel"),
                                       Qt::CaseInsensitive),
                 qPrintable(t.errorMsg()));
    }

    void progressMonotoneBounded()
    {
        QTemporaryDir dir;
        DTMThinner t;
        QVERIFY(t.open(buildTerrain(dir, "t96p.tif", 96, 96)));

        const MapExtent domain(0.0, 0.0, 96.0, 96.0);
        DTMThinnerOptions opts;
        opts.gridSpacing   = 1.0;
        opts.maxIterations = 3;

        DTMThinnerLimits lim;
        lim.maxGridBytes = forcedBandBudget(96, 3, 4);

        QVector<double> fracs;
        QVector<double> z;
        const auto pts = t.generatePoints(
            domain, opts, &z,
            [&fracs](double f) { fracs.append(f); return true; }, lim);

        QVERIFY(!pts.isEmpty());
        QVERIFY(fracs.size() > 10);   // many bands x (strips + passes)
        for (int i = 0; i < fracs.size(); ++i)
        {
            QVERIFY(fracs[i] >= 0.0 && fracs[i] <= 1.0);
            if (i > 0) QVERIFY(fracs[i] >= fracs[i - 1]);
        }
        QCOMPARE(fracs.last(), 1.0);
    }

    void retainedCeiling()
    {
        QTemporaryDir dir;
        DTMThinner t;
        QVERIFY(t.open(buildTerrain(dir, "t96r.tif", 96, 96)));

        const MapExtent domain(0.0, 0.0, 96.0, 96.0);
        DTMThinnerOptions opts;
        opts.gridSpacing        = 1.0;
        opts.normalDotThreshold = 2.0;   // keep everything
        opts.maxIterations      = 3;

        DTMThinnerLimits lim;
        lim.maxGridBytes      = forcedBandBudget(96, 3, 4);
        lim.maxRetainedPoints = 100;     // first band alone exceeds this

        QVector<double> z{1.0};
        const auto pts = t.generatePoints(domain, opts, &z, {}, lim);
        QVERIFY(pts.isEmpty());
        QVERIFY(z.isEmpty());
        QVERIFY2(t.errorMsg().contains(QStringLiteral("retained")),
                 qPrintable(t.errorMsg()));
    }

    void maxPointsMultiBand_soft()
    {
        // maxPoints across bands is a SOFT proportional quota (checked
        // between passes, may over/undershoot) — assert only that it runs
        // clean and thins harder than the uncapped run.
        QTemporaryDir dir;
        DTMThinner t;
        QVERIFY(t.open(buildTerrain(dir, "t96m.tif", 96, 96)));

        const MapExtent domain(0.0, 0.0, 96.0, 96.0);
        DTMThinnerOptions opts;
        opts.gridSpacing        = 1.0;
        opts.normalDotThreshold = 0.95;
        opts.maxIterations      = 3;

        DTMThinnerLimits lim;
        lim.maxGridBytes = forcedBandBudget(96, 3, 4);

        QVector<double> zFree, zCapped;
        const auto uncapped = t.generatePoints(domain, opts, &zFree, {}, lim);
        QVERIFY(!uncapped.isEmpty());

        opts.maxPoints = 200;
        const auto capped = t.generatePoints(domain, opts, &zCapped, {}, lim);
        QVERIFY2(t.errorMsg().isEmpty(), qPrintable(t.errorMsg()));
        QVERIFY(!capped.isEmpty());
        QVERIFY(capped.size() <= uncapped.size());
    }

    void tooWideGrid_error()
    {
        QTemporaryDir dir;
        DTMThinner t;
        QVERIFY(t.open(buildTerrain(dir, "t96w.tif", 96, 96)));

        const MapExtent domain(0.0, 0.0, 96.0, 96.0);
        DTMThinnerOptions opts;
        opts.gridSpacing   = 1.0;
        opts.maxIterations = 3;   // halo 3 → minimum band height 8 rows

        DTMThinnerLimits lim;
        lim.maxGridBytes = forcedBandBudget(96, 3, /*coreRows*/ 1);  // 7 rows < 8

        QVector<double> z;
        const auto pts = t.generatePoints(domain, opts, &z, {}, lim);
        QVERIFY(pts.isEmpty());
        QVERIFY2(t.errorMsg().contains(QStringLiteral("wide")),
                 qPrintable(t.errorMsg()));
    }
};

QTEST_MAIN(TestDTMThinnerBanded)
#include "test_dtmthinner_banded.moc"
