/**
 * @file      test_gageassignintegration.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 *
 * @brief     End-to-end check of spatial rain-gage assignment against a real
 *        engine-loaded model.
 *
 *        Two things here cannot be covered by the leaf-module tests:
 *
 *        1. cachedGageCoord() must report the ENGINE's coordinate. A gage with
 *           no [SYMBOLS] row is (0,0) in the engine, but buildGeometryCache()
 *           relocates it to the mean of every model vertex so it does not
 *           render at the origin. If spatial code read the cache instead, that
 *           invented position would become a real Thiessen site sitting in the
 *           middle of the network. This is the single most damaging failure
 *           mode of the feature, and it is invisible in the output.
 *
 *        2. applySubcatchSetGage() must write through to the engine AND raise
 *           the change signals, since a bulk assignment has no other way to
 *           refresh the views or mark the project dirty.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "core/gageassignment.h"
#include "core/gageblend.h"
#include "core/swmmdatetime.h"
#include "layers/swmmmodellayer.h"

#include <QDir>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <cmath>

namespace
{

/**
 *
 * @brief Resolve the rain-assignment fixture directory from SWMMVIS_GUI_TEST_DATA.
 * @return Environment value or current-directory dot, unvalidated and not created.
 *
 */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/**
 *
 * @brief Append gage_assignment.inp to dataDir().
 * @return Owned path without existence check or IO.
 *
 */
QString fixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("gage_assignment.inp"));
}

} // namespace

class TestGageAssignIntegration : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Reset the per-case owned layer pointer before fixture work.
     * Assumes previous cleanup ran; assigning null would not delete an old allocation.
     *
     */
    void init() { mLayer = nullptr; }
    /**
     *
     * @brief Delete the per-case model layer and clear its pointer after synchronous assertions.
     * Safe for null; releases its owned engine without writing the source fixture.
     *
     */
    void cleanup()
    {
        delete mLayer;
        mLayer = nullptr;
    }

    /**
     *
     * @brief Require the real fixture to expose three cached gages and four subcatchments.
     *
     */
    void fixtureLoads()
    {
        QVERIFY(load());
        QCOMPARE(mLayer->cachedGageCount(), 3);
        QCOMPARE(mLayer->cachedSubcatchCount(), 4);
    }

    // ── The trap ────────────────────────────────────────────────────────
    /**
     *
     * @brief Require an unlocated gage to report engine origin rather than an invented display
     * relocation.
     *
     */
    void unlocatedGageReadsAsOriginFromEngine()
    {
        QVERIFY(load());

        const int idx = gageIndex(QStringLiteral("GAGE_NOWHERE"));
        QVERIFY(idx >= 0);

        double x = 1.0, y = 1.0;
        QVERIFY(mLayer->cachedGageCoord(idx, &x, &y));

        // Exactly (0,0) — the engine's "no [SYMBOLS] row" sentinel. If this
        // ever returns the relocated display position instead, every spatial
        // rule silently gains a phantom gage.
        QCOMPARE(x, 0.0);
        QCOMPARE(y, 0.0);
    }

    /**
     *
     * @brief Pin the two located gages to their engine west/east coordinates.
     *
     */
    void locatedGagesReadTheirRealCoordinates()
    {
        QVERIFY(load());
        double x = 0.0, y = 0.0;

        QVERIFY(mLayer->cachedGageCoord(gageIndex(QStringLiteral("GAGE_W")), &x, &y));
        QCOMPARE(x, 0.0);
        QCOMPARE(y, 500.0);

        QVERIFY(mLayer->cachedGageCoord(gageIndex(QStringLiteral("GAGE_E")), &x, &y));
        QCOMPARE(x, 1000.0);
        QCOMPARE(y, 500.0);
    }

    /**
     *
     * @brief Require negative/excessive gage indices to fail coordinate lookup.
     *
     */
    void gageCoordRejectsOutOfRange()
    {
        QVERIFY(load());
        double x = 0.0, y = 0.0;
        QVERIFY(!mLayer->cachedGageCoord(-1, &x, &y));
        QVERIFY(!mLayer->cachedGageCoord(999, &x, &y));
    }

    // ── Thiessen assignment over the real polygons ──────────────────────
    /**
     *
     * @brief Require four real catchment polygons to choose the expected gage by strict area
     * majority using only located sites.
     *
     */
    void areaMajorityMatchesTheFixtureGeometry()
    {
        QVERIFY(load());

        // Only the two located gages participate.
        const QVector<QPointF> sites{{0.0, 500.0}, {1000.0, 500.0}};
        const QStringList expected{
            QStringLiteral("GAGE_W"), // SUB_W
            QStringLiteral("GAGE_E"), // SUB_E
            QStringLiteral("GAGE_W"), // SUB_MID
            QStringLiteral("GAGE_W"), // SUB_SKEW — centroid rule would say E
        };
        const QStringList names{QStringLiteral("SUB_W"), QStringLiteral("SUB_E"),
                                QStringLiteral("SUB_MID"), QStringLiteral("SUB_SKEW")};

        for (int i = 0; i < names.size(); ++i) {
            const int idx = subcatchIndex(names[i]);
            QVERIFY2(idx >= 0, qPrintable(names[i]));
            const QVector<QPointF> ring = mLayer->cachedSubcatchVertices(idx);
            QVERIFY2(ring.size() >= 3, qPrintable(names[i]));

            const QVector<double> shares = GageAssignment::thiessenAreaShares(ring, sites);
            double frac = 0.0;
            const int winner = GageAssignment::areaMajorityGage(shares, &frac);
            QVERIFY2(winner >= 0, qPrintable(names[i]));

            const QString got = (winner == 0) ? QStringLiteral("GAGE_W") : QStringLiteral("GAGE_E");
            QCOMPARE(got, expected[i]);
            QVERIFY(frac > 0.5); // a majority, by definition
        }
    }

    // SUB_SKEW is the discriminator: most of its area is west of the bisector
    // while its centroid lies east. Pinning both halves keeps a future
    // "simplify to centroid-nearest" change honest.
    /**
     *
     * @brief Pin a fixture where western Thiessen area wins although the polygon area centroid
     * lies east of the bisector.
     *
     */
    void skewedCatchmentSeparatesAreaFromCentroid()
    {
        QVERIFY(load());
        const QVector<QPointF> ring =
            mLayer->cachedSubcatchVertices(subcatchIndex(QStringLiteral("SUB_SKEW")));
        QVERIFY(ring.size() >= 3);

        const QVector<QPointF> sites{{0.0, 500.0}, {1000.0, 500.0}};
        const QVector<double> shares = GageAssignment::thiessenAreaShares(ring, sites);
        QVERIFY(shares[0] > shares[1]); // more area west

        double cx = 0.0, cy = 0.0, a = 0.0;
        for (int i = 0; i < ring.size(); ++i) {
            const QPointF &p = ring[i];
            const QPointF &q = ring[(i + 1) % ring.size()];
            const double cr = p.x() * q.y() - q.x() * p.y();
            a += cr;
            cx += (p.x() + q.x()) * cr;
            cy += (p.y() + q.y()) * cr;
        }
        a *= 0.5;
        cx /= (6.0 * a);
        cy /= (6.0 * a);
        QVERIFY2(cx > 500.0, "fixture invariant: the centroid must fall EAST of the bisector");
    }

    // ── The mediated write ──────────────────────────────────────────────
    /**
     *
     * @brief Require mediated reassignment to update the engine and emit one named attribute
     * change plus at least one model-edited signal.
     *
     */
    void applySubcatchSetGageWritesAndSignals()
    {
        QVERIFY(load());
        const int idx = subcatchIndex(QStringLiteral("SUB_W"));
        QVERIFY(idx >= 0);
        QCOMPARE(assignedGage(idx), QStringLiteral("GAGE_E")); // fixture starts wrong

        QSignalSpy attrSpy(mLayer, &SWMMModelLayer::attributeChanged);
        QSignalSpy editSpy(mLayer, &SWMMModelLayer::modelEdited);

        QVERIFY(mLayer->applySubcatchSetGage(idx, QStringLiteral("GAGE_W")));

        QCOMPARE(assignedGage(idx), QStringLiteral("GAGE_W"));
        QCOMPARE(attrSpy.count(), 1);
        QCOMPARE(attrSpy.first().first().toString(), QStringLiteral("SUB_W"));
        QVERIFY2(editSpy.count() >= 1,
                 "a gage reassignment must mark the project dirty, or the run "
                 "silently uses the last saved state");
    }

    /**
     *
     * @brief Require unknown/empty gage IDs and invalid catchment indices to fail without
     * changing the original assignment.
     *
     */
    void applySubcatchSetGageRejectsBadInput()
    {
        QVERIFY(load());
        const int idx = subcatchIndex(QStringLiteral("SUB_W"));

        QVERIFY(!mLayer->applySubcatchSetGage(idx, QStringLiteral("NO_SUCH_GAGE")));
        QVERIFY(!mLayer->applySubcatchSetGage(idx, QString()));
        QVERIFY(!mLayer->applySubcatchSetGage(-1, QStringLiteral("GAGE_W")));
        QVERIFY(!mLayer->applySubcatchSetGage(999, QStringLiteral("GAGE_W")));

        // Nothing above may have changed the assignment.
        QCOMPARE(assignedGage(idx), QStringLiteral("GAGE_E"));
    }

    // ── External rain-file gages ────────────────────────────────────────
    //
    // Interpolation used to refuse any FILE-source gage outright. It now reads
    // every gage through swmm_gage_get_rainfall_series, which resolves the
    // rainfall the engine will actually apply — so a time series, a standard
    // rain file, and a multi-column CSV all arrive in one domain.
    /**
     *
     * @brief Require time-series, standard rain-file and CSV gages in the external-files fixture
     * each to expose nonempty resolved rainfall.
     *
     */
    void allThreeDataSourcesResolveRainfall()
    {
        QVERIFY(loadFiles());
        QCOMPARE(mLayer->cachedGageCount(), 3);

        for (const char *id : {"TS_NORTH", "STD_EAST", "CSV_SOUTH"}) {
            const int idx = gageIndex(QString::fromLatin1(id));
            QVERIFY2(idx >= 0, id);
            int n = 0;
            QCOMPARE(swmm_gage_get_rainfall_series_count(mLayer->engine(), idx, &n), SWMM_OK);
            QVERIFY2(n > 0, id); // a FILE gage used to be unreadable here
        }
    }

    // The fixture gives all three gages identical rainfall, so any difference
    // between them is the API mis-resolving one of the sources.
    /**
     *
     * @brief Require four resolved intensities to agree across equivalent
     * time-series/standard/CSV sources.
     * Pins 0.8 in/hour from 0.2-in/15-minute volume input; standard-file float storage
     * uses 1e-6 tolerance while CSV double data uses 1e-12.
     *
     */
    void resolvedSeriesAgreeAcrossDataSources()
    {
        QVERIFY(loadFiles());

        const QVector<double> ts = resolved(gageIndex(QStringLiteral("TS_NORTH")));
        const QVector<double> std = resolved(gageIndex(QStringLiteral("STD_EAST")));
        const QVector<double> csv = resolved(gageIndex(QStringLiteral("CSV_SOUTH")));

        QCOMPARE(ts.size(), 4);
        QCOMPARE(std.size(), 4);
        QCOMPARE(csv.size(), 4);

        // 0.20 in per 15-min interval, declared VOLUME -> 0.8 in/hr.
        QVERIFY(std::abs(ts[1] - 0.8) < 1e-9);

        // The STANDARD rain-file loader deliberately stores float-quantized
        // inches, to stay bit-identical to legacy's binary rain interface file.
        // So it agrees with an equivalent double-precision time series only to
        // float precision (0.20f is 0.20000000298..., which lands ~1.2e-8 off
        // after the VOLUME conversion). That is correct behaviour, not drift —
        // hence a float-sized tolerance rather than a double-sized one.
        constexpr double kFloatTol = 1e-6;
        for (int k = 0; k < 4; ++k)
            QVERIFY2(std::abs(std[k] - ts[k]) < kFloatTol,
                     "standard rain file disagrees with the equivalent time series");

        // USER_CSV has no such legacy constraint: it stores project units in
        // double, so it must match exactly.
        for (int k = 0; k < 4; ++k)
            QVERIFY2(std::abs(csv[k] - ts[k]) < 1e-12,
                     "CSV rain file disagrees with the equivalent time series");
    }

    // The end the whole feature serves: a blend across all three sources has to
    // conserve depth, which it cannot do if any source resolved wrongly.
    /**
     *
     * @brief Require an equal-weight blend of three resolved source encodings to conserve depth
     * and match the first source within float tolerance.
     *
     */
    void blendAcrossDataSourcesConservesVolume()
    {
        QVERIFY(loadFiles());

        QVector<GageBlend::SourceGage> sources;
        for (const char *id : {"TS_NORTH", "STD_EAST", "CSV_SOUTH"}) {
            const int idx = gageIndex(QString::fromLatin1(id));
            int n = 0;
            QCOMPARE(swmm_gage_get_rainfall_series_count(mLayer->engine(), idx, &n), SWMM_OK);
            QVERIFY(n > 0);
            QVector<double> t(n), v(n);
            QCOMPARE(swmm_gage_get_rainfall_series(mLayer->engine(), idx, t.data(), v.data(), n),
                     SWMM_OK);

            GageBlend::SourceGage s;
            s.name = QString::fromLatin1(id);
            s.rainType = GageBlend::RainType::Intensity; // already resolved
            s.scaleFactor = 1.0;                         // already applied
            double interval = 900.0;
            swmm_gage_get_rain_interval(mLayer->engine(), idx, &interval);
            s.intervalSec = static_cast<qint64>(interval);
            for (int k = 0; k < n; ++k)
                s.points.append(
                    {openswmmvis::core::swmmDateTimeToQDateTime(t[k]).toSecsSinceEpoch(), v[k]});
            sources.append(s);
        }

        const GageBlend::BlendResult r =
            GageBlend::blend(sources, {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0});
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY2(r.volumeOk(),
                 qPrintable(QStringLiteral("relative error %1").arg(r.relativeError)));

        // Equal-weighted blend of three equivalent gages is that same gage.
        // Float-sized tolerance: one of the three is a standard rain file,
        // whose values are float-quantized by design (see the note above).
        QVERIFY(std::abs(r.blendedDepth - GageBlend::boxDepth(GageBlend::toBoxes(sources[0]))) <
                1e-6);
    }

private:
    /**
     *
     * @brief Load and own the primary spatial assignment fixture for this case.
     * @return True after load, false after deleting/clearing a failed layer. Requires
     * mLayer null on entry; repeated successful calls would overwrite/leak it. Keeps
     * diagnostics local and never adds the model to a canvas or writes input.
     *
     */
    bool load()
    {
        mLayer = new SWMMModelLayer(fixturePath(), nullptr);
        QList<QString> warnings, errors;
        if (!mLayer->loadModel(warnings, errors)) {
            delete mLayer;
            mLayer = nullptr;
            return false;
        }
        return true;
    }

    /**
     *
     * @brief Load and own gage_assignment_files.inp for external-source comparison.
     * @return True on success; failure deletes/clears mLayer. Requires mLayer null and
     * readable referenced rain files under dataDir(); diagnostics remain local.
     *
     */
    bool loadFiles()
    {
        mLayer = new SWMMModelLayer(
            QDir(dataDir()).filePath(QStringLiteral("gage_assignment_files.inp")), nullptr);
        QList<QString> warnings, errors;
        if (!mLayer->loadModel(warnings, errors)) {
            delete mLayer;
            mLayer = nullptr;
            return false;
        }
        return true;
    }

    /*! \brief A gage's resolved rainfall intensities. */
    /**
     *
     * @brief Read all resolved engine rainfall intensities for one gage.
     * @param idx Valid zero-based gage index in the loaded mLayer engine; API rejects
     * invalid indices. Requires successful fixture load.
     * @return Owned project-depth/hour intensity vector, empty on count/read error or
     * no samples. Discards read timestamps; allocates count-sized scratch/output arrays
     * and retains no engine pointer. Underlying file resolution can report errors.
     *
     */
    QVector<double> resolved(int idx) const
    {
        int n = 0;
        if (swmm_gage_get_rainfall_series_count(mLayer->engine(), idx, &n) != SWMM_OK || n <= 0)
            return {};
        QVector<double> t(n), v(n);
        if (swmm_gage_get_rainfall_series(mLayer->engine(), idx, t.data(), v.data(), n) != SWMM_OK)
            return {};
        return v;
    }

    /**
     *
     * @brief Look up a rain-gage fixture ID.
     * @param name Borrowed UTF-8-convertible exact ID in the loaded engine.
     * @return Zero-based index or API missing sentinel; requires nonnull mLayer.
     *
     */
    int gageIndex(const QString &name) const
    {
        return swmm_gage_index(mLayer->engine(), name.toUtf8().constData());
    }

    /**
     *
     * @brief Look up a subcatchment fixture ID.
     * @param name Borrowed UTF-8-convertible exact ID in the loaded engine.
     * @return Zero-based index or API missing sentinel; requires nonnull mLayer.
     *
     */
    int subcatchIndex(const QString &name) const
    {
        return swmm_subcatch_index(mLayer->engine(), name.toUtf8().constData());
    }

    /**
     *
     * @brief Copy the gage ID currently assigned to a fixture subcatchment.
     * @param subcatchIdx Zero-based loaded-engine index; invalid input is delegated
     * to the checked engine getter. Requires nonnull mLayer.
     * @return Owned UTF-8-decoded ID, or empty on error/no assignment/missing ID. No write.
     *
     */
    QString assignedGage(int subcatchIdx) const
    {
        int g = -1;
        if (swmm_subcatch_get_gage(mLayer->engine(), subcatchIdx, &g) != SWMM_OK || g < 0)
            return {};
        const char *id = swmm_gage_id(mLayer->engine(), g);
        return id ? QString::fromUtf8(id) : QString();
    }

    SWMMModelLayer *mLayer = nullptr;
};

QTEST_MAIN(TestGageAssignIntegration)
#include "test_gageassignintegration.moc"
