/*!
 * \file test_dtmthinner_precision.cpp
 * \brief Projected-coordinate translation must not alter terrain thinning.
 */
#include <QtTest>
#include <QDir>

#include "mesh/dtmthinner.h"

#include <gdal_priv.h>

#include <cmath>
#include <vector>

namespace {

constexpr int kSize = 64;

QString fixtureRoot()
{
    const QString configured = qEnvironmentVariable("SWMMVIS_TERRAIN_PRECISION_OUTPUT");
    return configured.isEmpty()
        ? QDir::current().filePath(QStringLiteral("terrain_precision_output"))
        : configured;
}

bool writeTerrain(const QString &path, double x, double y, double step)
{
    GDALAllRegister();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) return false;
    GDALDataset *ds = driver->Create(path.toUtf8().constData(), kSize, kSize,
                                    1, GDT_Float32, nullptr);
    if (!ds) return false;
    double transform[6] = {x, step, 0.0, y + kSize * step, 0.0, -step};
    bool ok = ds->SetGeoTransform(transform) == CE_None;
    std::vector<float> values(kSize * kSize);
    for (int r = 0; r < kSize; ++r)
        for (int c = 0; c < kSize; ++c)
            values[r * kSize + c] = float(step * (0.05 * c + 0.03 * r
                + 2.0 * std::sin(c * 0.35) * std::cos(r * 0.25)));
    ok = ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, kSize, kSize,
             values.data(), kSize, kSize, GDT_Float32, 0, 0) == CE_None && ok;
    GDALClose(ds);
    return ok;
}

} // namespace

class TestDTMThinnerPrecision : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(QDir().mkpath(fixtureRoot()));
    }

    void translatedTerrain_data()
    {
        QTest::addColumn<double>("step");
        QTest::addColumn<double>("x");
        QTest::addColumn<double>("y");
        QTest::addColumn<bool>("average");
        QTest::addColumn<bool>("banded");
        for (double step : {0.125, 0.25, 0.5, 1.0})
            for (bool statePlane : {false, true})
                for (bool average : {false, true})
                    for (bool banded : {false, true}) {
                        const QByteArray name = QStringLiteral("%1-%2-%3-%4")
                            .arg(step).arg(statePlane ? "stateplane" : "utm")
                            .arg(average ? "average" : "minimum")
                            .arg(banded ? "banded" : "single").toLatin1();
                        QTest::newRow(name.constData()) << step
                            << (statePlane ? 10000000.0 : 500000.0)
                            << (statePlane ? 12000000.0 : 4500000.0)
                            << average << banded;
                    }
    }

    void translatedTerrain()
    {
        QFETCH(double, step);
        QFETCH(double, x);
        QFETCH(double, y);
        QFETCH(bool, average);
        QFETCH(bool, banded);
        const QDir dir(fixtureRoot());
        const QString name = QString::fromLatin1(QTest::currentDataTag());
        const QString localPath = dir.filePath(name + "-local.tif");
        const QString shiftedPath = dir.filePath(name + "-projected.tif");
        QVERIFY(writeTerrain(localPath, 0.0, 0.0, step));
        QVERIFY(writeTerrain(shiftedPath, x, y, step));
        mesh::DTMThinner local, shifted;
        QVERIFY(local.open(localPath));
        QVERIFY(shifted.open(shiftedPath));
        mesh::DTMThinnerOptions opts;
        opts.gridSpacing = step;
        opts.normalDotThreshold = 0.97;
        opts.useAverageDot = average;
        opts.maxIterations = 3;
        mesh::DTMThinnerLimits limits;
        if (banded) limits.maxGridBytes = 46ll * kSize * (4 + 2 * opts.maxIterations);
        QVector<double> localZ, shiftedZ;
        const auto a = local.generatePoints(MapExtent(0.0, 0.0, kSize * step, kSize * step),
                                            opts, &localZ, {}, limits);
        const auto b = shifted.generatePoints(MapExtent(x, y, x + kSize * step, y + kSize * step),
                                              opts, &shiftedZ, {}, limits);
        QVERIFY2(local.errorMsg().isEmpty(), qPrintable(local.errorMsg()));
        QVERIFY2(shifted.errorMsg().isEmpty(), qPrintable(shifted.errorMsg()));
        QVERIFY(!a.isEmpty());
        QVERIFY(a.size() < kSize * kSize);
        QCOMPARE(b.size(), a.size());
        QCOMPARE(shiftedZ, localZ);
        for (qsizetype i = 0; i < a.size(); ++i) {
            QCOMPARE(b[i].x() - x, a[i].x());
            QCOMPARE(b[i].y() - y, a[i].y());
        }
    }

    void unthinnedCoordinatesRetainSubmetreLattice()
    {
        const double x = 500000.0, y = 4500000.0, step = 0.125;
        const QString path = QDir(fixtureRoot()).filePath("unthinned-utm.tif");
        QVERIFY(writeTerrain(path, x, y, step));
        mesh::DTMThinner thinner;
        QVERIFY(thinner.open(path));
        mesh::DTMThinnerOptions opts;
        opts.gridSpacing = step;
        opts.normalDotThreshold = 2.0;
        opts.maxIterations = 3;
        mesh::DTMThinnerLimits limits;
        limits.maxGridBytes = 46ll * kSize * 10;
        const auto points = thinner.generatePoints(
            MapExtent(x, y, x + kSize * step, y + kSize * step), opts, nullptr, {}, limits);
        QVERIFY2(thinner.errorMsg().isEmpty(), qPrintable(thinner.errorMsg()));
        QCOMPARE(points.size(), kSize * kSize);
        for (int r = 0; r < kSize; ++r)
            for (int c = 0; c < kSize; ++c)
                QCOMPARE(points[r * kSize + c], QPointF(x + (c + 0.5) * step,
                                                       y + (r + 0.5) * step));
    }
};

QTEST_APPLESS_MAIN(TestDTMThinnerPrecision)
#include "test_dtmthinner_precision.moc"
