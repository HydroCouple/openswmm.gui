/**
 * @file      test_twod_defaults_prefs.cpp
 * @brief     Pins PreferencesManager::TwoDDefaults — the 2D Defaults
 *         preference bundle (iteration 4).
 *
 * Three contracts:
 *  1. The compiled-in struct defaults match the engine's [2D_OPTIONS]
 *     defaults and the historical mesh-generation seeds. These values are
 *     the missing-key fallbacks SimulationOptionsDialog::read2DFromEngine
 *     shows and what File→New synthesizes — drift here silently changes
 *     both surfaces.
 *  2. setTwoDDefaults → twoDDefaults round-trips every field through
 *     QSettings.
 *  3. setTwoDDefaults emits preferenceChanged("Defaults", "TwoDDefaults")
 *     so live-bound consumers refresh.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QSignalSpy>

#include "core/preferencesmanager.h"

class TestTwoDDefaultsPrefs : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Select dedicated Qt identity and INI user-scope path under persistent mesh-controls
     * verification output, then clear TwoDDefaults.
     * Global format/path/app identity changes are not restored. Directory creation is
     * asserted; settings sync errors unchecked and tests require isolated serialized use.
     *
     */
    void initTestCase();
    /**
     *
     * @brief Pin listed 2D solver/coupling/rainfall/output and mesh-generation defaults,
     * including unset optional resolution sources.
     * Lengths use the struct documented metres, steps seconds and angles degrees;
     * reads local defaults without running a solver or mesh generator.
     *
     */
    void compiledDefaultsPinTheContract();
    /**
     *
     * @brief Persist/read the explicitly listed solver and mesh default fields, then restore a
     * compiled-default bundle on success.
     * Early assertions can skip that reset; this is same-process QSettings verification,
     * not a restart test or proof that future added fields are covered.
     *
     */
    void roundTripPersistsAllFields();
    /**
     *
     * @brief Seed legacy mesh spacing/coarsening keys, require metre-based migration and persist
     * an explicit optional resolution bundle.
     * Checks JSON equality including terrain Off, then resets defaults on success.
     *
     */
    void explicitResolutionAndLegacyPreferences();
    /**
     *
     * @brief Require setting compiled TwoDDefaults to emit one Defaults/TwoDDefaults
     * notification.
     *
     */
    void setterEmitsPreferenceChanged();
};

void TestTwoDDefaultsPrefs::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("twoddefaults-test"));
    const QString output = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                               .filePath("../../verification/mesh_controls_2026-10-13/preferences");
    QVERIFY(QDir().mkpath(output));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, output);
    QSettings settings;
    settings.remove(QStringLiteral("SWMMVis/Preferences/TwoDDefaults"));
    settings.sync();
}

void TestTwoDDefaultsPrefs::compiledDefaultsPinTheContract()
{
    const PreferencesManager::TwoDDefaults d;

    // [2D_OPTIONS] solver keys — must match the engine defaults that
    // read2DFromEngine's fallbacks used to hardcode.
    QCOMPARE(d.maxTimestepSec, 10.0);
    QCOMPARE(d.theta, 0.8);
    QCOMPARE(d.cflNumber, 0.7);
    QCOMPARE(d.ltsTiers, 4);
    QCOMPARE(d.hMove, 0.003);
    QCOMPARE(d.froudeMax, 1.5);
    QVERIFY(!d.advection);
    QCOMPARE(d.dryDepth, 0.001);
    QCOMPARE(d.limiterEpsilon, 1e-6);
    QCOMPARE(d.fluxDhEps, 0.004);
    QCOMPARE(d.cellClosure, QStringLiteral("FLAT"));
    QCOMPARE(d.faceReconstruction, QStringLiteral("MEAN"));
    QCOMPARE(d.vfrMinWetFrac, 0.01);
    QCOMPARE(d.couplingCd, 0.65);
    QCOMPARE(d.couplingSync, 0.0);
    QVERIFY(!d.couplingAreaAuto);
    QCOMPARE(d.rainfallMode, QStringLiteral("NATURAL_NEIGHBOUR"));
    QVERIFY(!d.report2D);

    // Mesh-generation seeds (MESH_OVERHAUL_PLAN_2026-09-29.md §3,
    // MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §5; user defaults 2026-10-03).
    QCOMPARE(d.meshCellSizeM, 0.0);
    QCOMPARE(d.meshCoarsenFactor, 0.0);
    QCOMPARE(d.meshSizeRatio, 2.0);
    QCOMPARE(d.meshMinCellSizeM, 6.096);
    QCOMPARE(d.meshTerrainToleranceM, 0.0);
    QVERIFY(!d.resolutionSettings().maximumM);
    QVERIFY(!d.resolutionSettings().featureM);
    QVERIFY(!d.resolutionSettings().toleranceM);
    QCOMPARE(d.meshMinAngleDeg, 33.0);
    QVERIFY(d.meshQuadsBetweenBreaklines);
    QCOMPARE(d.meshTrimTurnDeg, 5.0);
    QCOMPARE(d.meshTrimDeviationM, 0.0);
    QCOMPARE(d.meshIdwPower, 2.0);
    QCOMPARE(d.meshNodeFlattenRadM, 5.0);
    // 2026-09-11 decision: nodes pinned at rim elevation with the minimum
    // separation on.
    QVERIFY(d.meshNodesAsVertices);
    QVERIFY(d.meshNodesUseRim);
    QVERIFY(d.meshMinNodeSepOn);
    QCOMPARE(d.meshMinNodeSepM, 2.0);
    QCOMPARE(d.meshInitDepth, 0.0);
    QCOMPARE(d.meshManningsN, 0.035);
    QVERIFY(d.meshOutputExternal);
}

void TestTwoDDefaultsPrefs::roundTripPersistsAllFields()
{
    auto *p = PreferencesManager::instance();

    PreferencesManager::TwoDDefaults d;
    d.maxTimestepSec = 5.5;
    d.theta = 0.9;
    d.cflNumber = 0.5;
    d.ltsTiers = 2;
    d.hMove = 0.01;
    d.froudeMax = 2.0;
    d.advection = true;
    d.dryDepth = 0.002;
    d.limiterEpsilon = 1e-7;
    d.fluxDhEps = 0.008;
    d.cellClosure = QStringLiteral("VFR");
    d.faceReconstruction = QStringLiteral("VFR_FACE");
    d.vfrMinWetFrac = 0.05;
    d.couplingCd = 0.7;
    d.couplingSync = 1.5;
    d.couplingAreaAuto = true;
    d.rainfallMode = QStringLiteral("SYSTEM");
    d.report2D = true;
    d.meshCellSizeM = 3.5;
    d.meshCoarsenFactor = 12.0;
    d.meshSizeRatio = 1.3;
    d.meshMinCellSizeM = 0.8;
    d.meshTerrainToleranceM = 0.1;
    d.meshMinAngleDeg = 26.0;
    d.meshQuadsBetweenBreaklines = false;
    d.meshTrimTurnDeg = 3.0;
    d.meshTrimDeviationM = 0.4;
    d.meshIdwPower = 3.0;
    d.meshNodeFlattenRadM = 8.0;
    d.meshNodesAsVertices = false;
    d.meshNodesUseRim = false;
    d.meshMinNodeSepOn = false;
    d.meshMinNodeSepM = 3.0;
    d.meshInitDepth = 0.05;
    d.meshManningsN = 0.05;
    d.meshOutputExternal = false;

    p->setTwoDDefaults(d);
    const auto r = p->twoDDefaults();

    QCOMPARE(r.maxTimestepSec, d.maxTimestepSec);
    QCOMPARE(r.theta, d.theta);
    QCOMPARE(r.cflNumber, d.cflNumber);
    QCOMPARE(r.ltsTiers, d.ltsTiers);
    QCOMPARE(r.hMove, d.hMove);
    QCOMPARE(r.froudeMax, d.froudeMax);
    QCOMPARE(r.advection, d.advection);
    QCOMPARE(r.dryDepth, d.dryDepth);
    QCOMPARE(r.limiterEpsilon, d.limiterEpsilon);
    QCOMPARE(r.fluxDhEps, d.fluxDhEps);
    QCOMPARE(r.cellClosure, d.cellClosure);
    QCOMPARE(r.faceReconstruction, d.faceReconstruction);
    QCOMPARE(r.vfrMinWetFrac, d.vfrMinWetFrac);
    QCOMPARE(r.couplingCd, d.couplingCd);
    QCOMPARE(r.couplingSync, d.couplingSync);
    QCOMPARE(r.couplingAreaAuto, d.couplingAreaAuto);
    QCOMPARE(r.rainfallMode, d.rainfallMode);
    QCOMPARE(r.report2D, d.report2D);
    QCOMPARE(r.meshCellSizeM, d.meshCellSizeM);
    QCOMPARE(r.meshCoarsenFactor, d.meshCoarsenFactor);
    QCOMPARE(r.meshSizeRatio, d.meshSizeRatio);
    QCOMPARE(r.meshMinCellSizeM, d.meshMinCellSizeM);
    QCOMPARE(r.meshTerrainToleranceM, d.meshTerrainToleranceM);
    QCOMPARE(r.meshMinAngleDeg, d.meshMinAngleDeg);
    QCOMPARE(r.meshQuadsBetweenBreaklines, d.meshQuadsBetweenBreaklines);
    QCOMPARE(r.meshTrimTurnDeg, d.meshTrimTurnDeg);
    QCOMPARE(r.meshTrimDeviationM, d.meshTrimDeviationM);
    QCOMPARE(r.meshIdwPower, d.meshIdwPower);
    QCOMPARE(r.meshNodeFlattenRadM, d.meshNodeFlattenRadM);
    QCOMPARE(r.meshNodesAsVertices, d.meshNodesAsVertices);
    QCOMPARE(r.meshNodesUseRim, d.meshNodesUseRim);
    QCOMPARE(r.meshMinNodeSepOn, d.meshMinNodeSepOn);
    QCOMPARE(r.meshMinNodeSepM, d.meshMinNodeSepM);
    QCOMPARE(r.meshInitDepth, d.meshInitDepth);
    QCOMPARE(r.meshManningsN, d.meshManningsN);
    QCOMPARE(r.meshOutputExternal, d.meshOutputExternal);

    // Restore compiled-in defaults so later tests see a clean slate.
    p->setTwoDDefaults(PreferencesManager::TwoDDefaults{});
}

void TestTwoDDefaultsPrefs::setterEmitsPreferenceChanged()
{
    auto *p = PreferencesManager::instance();
    QSignalSpy spy(p, &PreferencesManager::preferenceChanged);
    p->setTwoDDefaults(PreferencesManager::TwoDDefaults{});
    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("Defaults"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("TwoDDefaults"));
}

void TestTwoDDefaultsPrefs::explicitResolutionAndLegacyPreferences()
{
    auto *p = PreferencesManager::instance();
    QSettings settings;
    const QString prefix = "SWMMVis/Preferences/TwoDDefaults/";
    settings.remove(prefix + "MeshResolutionJson");
    settings.setValue(prefix + "MeshCellSizeM", 6.096);
    settings.setValue(prefix + "MeshMinCellSizeM", 6.096);
    settings.setValue(prefix + "MeshCoarsenFactor", 1000.0);
    settings.setValue(prefix + "MeshTerrainToleranceM", 6.096);
    settings.sync();
    const auto migrated = p->twoDDefaults().resolutionSettings();
    QCOMPARE(migrated.maximumM.value(), 6096.0);
    QCOMPARE(migrated.toleranceM.value(), 6.096);
    QCOMPARE(migrated.breaklineToleranceM.value(), 6.096);

    auto d = p->twoDDefaults();
    auto r = migrated;
    r.maximumM.reset();
    r.featureM.reset();
    r.toleranceM = .1524;
    r.breaklineToleranceM = .6096;
    r.averagingM = 3.048;
    r.terrain = mesh::ResolutionSettings::Terrain::Off;
    d.meshResolution = r;
    p->setTwoDDefaults(d);
    QCOMPARE(p->twoDDefaults().resolutionSettings().toJson(), r.toJson());
    p->setTwoDDefaults(PreferencesManager::TwoDDefaults{});
}

QTEST_MAIN(TestTwoDDefaultsPrefs)
#include "test_twod_defaults_prefs.moc"
