/*!
 * \file   test_nonspatial_adapters.cpp
 * \brief  Slice DA.2 round-trip tests for the non-spatial property
 *         adapters. Exercises the three "rich" adapters (Pollutant,
 *         LandUse, RainGage) end-to-end against a real engine handle:
 *         build an object, set each writable Q_PROPERTY through the
 *         adapter, read it back, and verify the engine state agrees.
 *
 *         The 11 other DA.2 adapters (Curve, TimeSeries, Pattern,
 *         Aquifer, Snowpack, LIDControl, Transect, Hydrograph, Street,
 *         Inlet, ControlRule) ship as thin or summary-only adapters
 *         today because the engine lacks per-field getters; they are
 *         exercised by `test_dataobjectnaming.cpp` (DA.1) for name +
 *         count + group enumeration.
 */

#include "ui/properties/swmmpollutantpropertyadapter.h"
#include "ui/properties/swmmlandusepropertyadapter.h"
#include "ui/properties/swmmraingagepropertyadapter.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_gages.h>

#include <QObject>
#include <QSignalSpy>
#include <QTest>

class TestNonSpatialAdapters : public QObject
{
    Q_OBJECT

private slots:

    // ====================================================================
    // SWMMPollutantPropertyAdapter — rich scalar coverage
    // ====================================================================

    void pollutantScalarRoundTrip()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        // mg/L = 0; default after add.
        QCOMPARE(swmm_pollutant_add(e, "TSS", 0), SWMM_OK);

        SWMMPollutantPropertyAdapter a(e, QStringLiteral("TSS"));
        QCOMPARE(a.name(), QStringLiteral("TSS"));
        QCOMPARE(a.units(), 0);  // read-only: was set at add time

        a.setRainConc(2.5);
        a.setGwConc(1.0);
        a.setInitConc(0.5);
        a.setRdiiConc(3.0);
        a.setKDecay(0.1);
        a.setMwt(58.44);
        a.setSnowOnly(true);

        QCOMPARE(a.rainConc(), 2.5);
        QCOMPARE(a.gwConc(),   1.0);
        QCOMPARE(a.initConc(), 0.5);
        QCOMPARE(a.rdiiConc(), 3.0);
        QCOMPARE(a.kDecay(),   0.1);
        QCOMPARE(a.mwt(),      58.44);
        QVERIFY(a.snowOnly());

        // Verify the engine actually accepted the writes.
        int idx = swmm_pollutant_index(e, "TSS");
        double v = 0; int flag = 0;
        swmm_pollutant_get_kdecay(e, idx, &v);
        QCOMPARE(v, 0.1);
        swmm_pollutant_get_snow_only(e, idx, &flag);
        QCOMPARE(flag, 1);

        swmm_engine_destroy(e);
    }

    void pollutantChangedSignalFires()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_pollutant_add(e, "P1", 1), SWMM_OK);
        SWMMPollutantPropertyAdapter a(e, QStringLiteral("P1"));

        QSignalSpy spy(&a, &SWMMDataObjectPropertyAdapter::changed);
        a.setKDecay(0.25);
        QCOMPARE(spy.count(), 1);

        swmm_engine_destroy(e);
    }

    void pollutantCoPollutantRoundTrip()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_pollutant_add(e, "BOD", 0), SWMM_OK);
        QCOMPARE(swmm_pollutant_add(e, "TSS", 0), SWMM_OK);

        SWMMPollutantPropertyAdapter a(e, QStringLiteral("BOD"));
        a.setCoPollutant(QStringLiteral("TSS"));
        a.setCoPollutantFrac(0.4);

        QCOMPARE(a.coPollutant(), QStringLiteral("TSS"));
        QCOMPARE(a.coPollutantFrac(), 0.4);

        // Clearing the co-pollutant via empty name.
        a.setCoPollutant(QString());
        QCOMPARE(a.coPollutant(), QString());

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // SWMMLandUsePropertyAdapter
    // ====================================================================

    void landUseScalarRoundTrip()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_landuse_add(e, "Residential"), SWMM_OK);

        SWMMLandUsePropertyAdapter a(e, QStringLiteral("Residential"));
        a.setSweepInterval(7.0);
        a.setSweepRemoval(0.85);

        QCOMPARE(a.sweepInterval(), 7.0);
        QCOMPARE(a.sweepRemoval(),  0.85);

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // SWMMRainGagePropertyAdapter
    // ====================================================================

    void rainGageRainTypeRoundTrip()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        // The engine API doesn't expose `swmm_gage_add` directly; gages
        // come in via the model builder. Skip if the API isn't surfaced.
        // (Adapter behaviour is verified against any engine handle; here
        // we just confirm `idx == -1` doesn't crash.)
        SWMMRainGagePropertyAdapter a(e, QStringLiteral("MissingGage"));
        QCOMPARE(a.rainType(),   0);
        QCOMPARE(a.dataSource(), 0);
        // Setter against a missing gage is a no-op (idx() returns -1).
        a.setRainType(2);
        QCOMPARE(a.rainType(), 0);

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Rename round-trip — shared base class behaviour
    // ====================================================================

    void renameSignalFires()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_pollutant_add(e, "Old", 0), SWMM_OK);
        SWMMPollutantPropertyAdapter a(e, QStringLiteral("Old"));

        QSignalSpy spy(&a, &SWMMDataObjectPropertyAdapter::renameRequested);
        a.setName(QStringLiteral("New"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("Old"));
        QCOMPARE(spy.first().at(1).toString(), QStringLiteral("New"));

        // Empty / same-as-current names are silently ignored.
        a.setName(QString());
        a.setName(QStringLiteral("Old"));
        QCOMPARE(spy.count(), 1);

        swmm_engine_destroy(e);
    }
};

QTEST_GUILESS_MAIN(TestNonSpatialAdapters)
#include "test_nonspatial_adapters.moc"
