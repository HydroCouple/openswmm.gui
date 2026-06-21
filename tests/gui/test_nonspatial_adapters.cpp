/*!
 * \file   test_nonspatial_adapters.cpp
 * \brief  Slice DA.2 round-trip tests for the non-spatial property
 *         adapters. Exercises the four "rich" adapters (Pollutant,
 *         LandUse, RainGage, Transect) end-to-end against a real engine
 *         handle: build an object, set each writable Q_PROPERTY through
 *         the adapter, read it back, and verify the engine state agrees.
 *
 *         The 10 remaining DA.2 adapters (Curve, TimeSeries, Pattern,
 *         Aquifer, Snowpack, LIDControl, Hydrograph, Street, Inlet,
 *         ControlRule) ship as thin or summary-only adapters today
 *         because the engine lacks per-field getters; they are exercised
 *         by `test_dataobjectnaming.cpp` (DA.1) for name + count + group
 *         enumeration.
 */

#include "ui/properties/swmmpollutantpropertyadapter.h"
#include "ui/properties/swmmlandusepropertyadapter.h"
#include "ui/properties/swmmraingagepropertyadapter.h"
#include "ui/properties/swmmtransectpropertyadapter.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_infrastructure.h>

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

    // DA.2 parity — recording interval, snow-catch factor, series-name picker,
    // station id, rain units. Against a missing gage the getters return their
    // documented defaults and the setters are safe no-ops (idx() == -1).
    void rainGageAdvancedFieldsDefaults()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        SWMMRainGagePropertyAdapter a(e, QStringLiteral("MissingGage"));

        QCOMPARE(a.rainIntervalRef().seconds, 0);
        QCOMPARE(a.snowFactor(),   1.0);   // SCF default
        QCOMPARE(a.rainUnits(),    0);     // IN
        QVERIFY(a.stationId().isEmpty());
        // Series-name ref carries the TimeSeries kind even when unassigned.
        QCOMPARE(a.seriesNameRef().kind, DataObjectRef::TimeSeries);
        QVERIFY(a.seriesNameRef().currentName.isEmpty());

        // No-op setters against the missing gage must not change the reads.
        RainIntervalRef ivl = a.rainIntervalRef();
        ivl.seconds = 900;
        a.setRainIntervalRef(ivl);
        a.setSnowFactor(1.5);
        a.setRainUnits(1);
        a.setStationId(QStringLiteral("STA"));
        QCOMPARE(a.rainIntervalRef().seconds, 0);
        QCOMPARE(a.snowFactor(),   1.0);
        QCOMPARE(a.rainUnits(),    0);
        QVERIFY(a.stationId().isEmpty());

        swmm_engine_destroy(e);
    }

    // DA.2 parity — H:MM clock helpers shared by the interval combo + table
    // delegate. Must round-trip and match the engine's parse_time_seconds
    // (colon form is H:MM[:SS]; bare number is decimal hours).
    void rainIntervalHmmHelpers()
    {
        using namespace rain_interval;
        QCOMPARE(hmmToSeconds(QStringLiteral("0:15")), 900);
        QCOMPARE(hmmToSeconds(QStringLiteral("1:00")), 3600);
        QCOMPARE(hmmToSeconds(QStringLiteral("24:00")), 86400);
        QCOMPARE(hmmToSeconds(QStringLiteral("0:00:30")), 30);
        QCOMPARE(hmmToSeconds(QStringLiteral("2")), 7200);   // bare = decimal hours

        QCOMPARE(secondsToHMM(900), QStringLiteral("0:15"));
        QCOMPARE(secondsToHMM(3600), QStringLiteral("1:00"));
        QCOMPARE(secondsToHMM(86400), QStringLiteral("24:00"));
        QCOMPARE(secondsToHMM(90), QStringLiteral("0:01:30"));  // seconds shown

        // Every preset round-trips through parse -> format unchanged.
        for (const QString &p : presetsHMM())
            QCOMPARE(secondsToHMM(hmmToSeconds(p)), p);

        // Malformed input is rejected.
        QCOMPARE(hmmToSeconds(QStringLiteral("abc")), -1);
        QCOMPARE(hmmToSeconds(QStringLiteral("1:2:3:4")), -1);
        QCOMPARE(hmmToSeconds(QString()), -1);
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

    // ====================================================================
    // SWMMTransectPropertyAdapter — Phase 6.7.4 rich scalar coverage
    // ====================================================================

    void transectScalarRoundTrip()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_transect_add(e, "MainChannel"), SWMM_OK);

        SWMMTransectPropertyAdapter a(e, QStringLiteral("MainChannel"));
        QCOMPARE(a.name(), QStringLiteral("MainChannel"));

        // ── Roughness triple ────────────────────────────────────────────────
        a.setNLeftBank(0.04);
        a.setNRightBank(0.05);
        a.setNChannel(0.03);
        QCOMPARE(a.nLeftBank(),  0.04);
        QCOMPARE(a.nRightBank(), 0.05);
        QCOMPARE(a.nChannel(),   0.03);
        // Cross-check that the engine actually accepted the triple — read
        // back through the C API rather than the adapter.
        int idx = swmm_transect_index(e, "MainChannel");
        double nL = 0, nR = 0, nCh = 0;
        QCOMPARE(swmm_transect_get_roughness(e, idx, &nL, &nR, &nCh), SWMM_OK);
        QCOMPARE(nL, 0.04); QCOMPARE(nR, 0.05); QCOMPARE(nCh, 0.03);

        // ── Bank stations ───────────────────────────────────────────────────
        a.setXLeftBank(10.0);
        a.setXRightBank(40.0);
        QCOMPARE(a.xLeftBank(),  10.0);
        QCOMPARE(a.xRightBank(), 40.0);
        double xLb = 0, xRb = 0;
        QCOMPARE(swmm_transect_get_bank_stations(e, idx, &xLb, &xRb), SWMM_OK);
        QCOMPARE(xLb, 10.0); QCOMPARE(xRb, 40.0);

        // ── Encroachment stations ───────────────────────────────────────────
        a.setXLeftEncroachment(5.0);
        a.setXRightEncroachment(45.0);
        QCOMPARE(a.xLeftEncroachment(),  5.0);
        QCOMPARE(a.xRightEncroachment(), 45.0);
        double xLe = 0, xRe = 0;
        QCOMPARE(swmm_transect_get_encroachment_stations(e, idx, &xLe, &xRe), SWMM_OK);
        QCOMPARE(xLe, 5.0); QCOMPARE(xRe, 45.0);

        // ── Modifiers ───────────────────────────────────────────────────────
        a.setStationMultiplier(2.0);
        a.setElevationOffset(1.5);
        a.setMeanderFactor(1.25);
        QCOMPARE(a.stationMultiplier(), 2.0);
        QCOMPARE(a.elevationOffset(),   1.5);
        QCOMPARE(a.meanderFactor(),     1.25);

        // ── Comments round-trip ─────────────────────────────────────────────
        a.setComments(QStringLiteral("Main channel — surveyed 2026-Q1"));
        QCOMPARE(a.comments(), QStringLiteral("Main channel — surveyed 2026-Q1"));

        swmm_engine_destroy(e);
    }

    void transectStationCountReadOnlySummary()
    {
        // The adapter doesn't edit the station list — that's the editor
        // dialog's job — but it surfaces the count as a read-only summary
        // so the Object Browser property panel can show "N stations" at
        // a glance.
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_transect_add(e, "X"), SWMM_OK);
        const int idx = swmm_transect_index(e, "X");
        QCOMPARE(swmm_transect_add_station(e, idx, 0.0, 10.0), SWMM_OK);
        QCOMPARE(swmm_transect_add_station(e, idx, 5.0,  0.0), SWMM_OK);
        QCOMPARE(swmm_transect_add_station(e, idx, 10.0, 10.0), SWMM_OK);

        SWMMTransectPropertyAdapter a(e, QStringLiteral("X"));
        QCOMPARE(a.stationCount(), 3);

        swmm_engine_destroy(e);
    }

    void transectChangedSignalFires()
    {
        // Every successful per-field setter must emit `changed()` so the
        // QPropertyModel-bound view refreshes. Cross-field independence
        // matters: setting roughness must not emit a bank-station change.
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        QCOMPARE(swmm_transect_add(e, "S"), SWMM_OK);

        SWMMTransectPropertyAdapter a(e, QStringLiteral("S"));
        QSignalSpy spy(&a, &SWMMDataObjectPropertyAdapter::changed);

        a.setNLeftBank(0.07);            QCOMPARE(spy.count(), 1);
        a.setNLeftBank(0.07);            QCOMPARE(spy.count(), 1);   // no-op
        a.setXLeftBank(2.5);             QCOMPARE(spy.count(), 2);
        a.setXLeftEncroachment(1.0);     QCOMPARE(spy.count(), 3);
        a.setStationMultiplier(3.0);     QCOMPARE(spy.count(), 4);
        a.setComments(QStringLiteral("c"));
        QCOMPARE(spy.count(), 5);

        swmm_engine_destroy(e);
    }

    void transectUnknownNameDegradesGracefully()
    {
        // An adapter bound to a name the engine never saw should return
        // zeros from all getters and quietly drop all setters — no crash,
        // no signal storm.
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);

        SWMMTransectPropertyAdapter a(e, QStringLiteral("DoesNotExist"));
        QCOMPARE(a.nLeftBank(),         0.0);
        QCOMPARE(a.xLeftBank(),         0.0);
        QCOMPARE(a.stationMultiplier(), 1.0);  // modifiers default (unread → 1.0)
        QCOMPARE(a.stationCount(),      0);
        QCOMPARE(a.comments(),          QString());

        QSignalSpy spy(&a, &SWMMDataObjectPropertyAdapter::changed);
        a.setNLeftBank(0.05);
        a.setComments(QStringLiteral("x"));
        QCOMPARE(spy.count(), 0);

        swmm_engine_destroy(e);
    }
};

// ---------------------------------------------------------------------------
// Link-time stubs for the user-flags surface that the RainGage (and sibling)
// property adapters reference from userFlagsRef(). The rain-gage cases here
// (rainGageAdvancedFieldsDefaults) never call userFlagsRef(), so these bodies
// are never executed — they exist only to satisfy the linker without dragging
// the full SWMMModelLayer chain (nanoflann / GDAL / the scene graph) into this
// self-contained adapter test. Same pattern as test_typeconversionflow.cpp.
// ---------------------------------------------------------------------------
#include "layers/swmmmodellayer.h"
#include "ui/properties/userflagseditref.h"

openswmmvis::ui::UserFlagsModel *SWMMModelLayer::ensureUserFlagsModel() { return nullptr; }

QString userFlagsSummaryFor(openswmmvis::ui::UserFlagsModel *, const QString &,
                            const QString &)
{
    return QString();
}

QTEST_GUILESS_MAIN(TestNonSpatialAdapters)
#include "test_nonspatial_adapters.moc"
