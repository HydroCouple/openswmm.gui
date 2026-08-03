/*!
 * \file   test_twod_defaults_prefs.cpp
 * \brief  Pins PreferencesManager::TwoDDefaults — the 2D Defaults
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
 */

#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>

#include "core/preferencesmanager.h"

class TestTwoDDefaultsPrefs : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void compiledDefaultsPinTheContract();
    void roundTripPersistsAllFields();
    void setterEmitsPreferenceChanged();
};

void TestTwoDDefaultsPrefs::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("twoddefaults-test"));
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

    // Mesh-generation seeds — the historical seedDefaults() values.
    QCOMPARE(d.meshMinAngleDeg, 33.0);
    QCOMPARE(d.meshMaxArea, 0.0);
    QCOMPARE(d.meshMaxSteiner, -1);
    QCOMPARE(d.meshIdwPower, 2.0);
    QCOMPARE(d.meshSimplifyEpsM, 0.1);
    QCOMPARE(d.meshSnapEpsM, 0.01);
    QCOMPARE(d.meshNodeFlattenRadM, 5.0);
    QVERIFY(d.meshMinNodeSepOn);
    QCOMPARE(d.meshMinNodeSepM, 2.0);
    QVERIFY(d.meshThinningOn);
    QCOMPARE(d.meshThinningTol, 0.6);
    QCOMPARE(d.meshThinningPasses, 3);
    QCOMPARE(d.meshBoundaryBufferM, 0.0);
    QVERIFY(!d.meshMaxBoundaryEdgeOn);
    QCOMPARE(d.meshMaxBoundaryEdgeM, 20.0);
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
    d.meshMinAngleDeg = 26.0;
    d.meshMaxArea = 50.0;
    d.meshMaxSteiner = 100000;
    d.meshIdwPower = 3.0;
    d.meshSimplifyEpsM = 0.2;
    d.meshSnapEpsM = 0.02;
    d.meshNodeFlattenRadM = 8.0;
    d.meshMinNodeSepOn = false;
    d.meshMinNodeSepM = 3.0;
    d.meshThinningOn = false;
    d.meshThinningTol = 0.5;
    d.meshThinningPasses = 5;
    d.meshBoundaryBufferM = 4.0;
    d.meshMaxBoundaryEdgeOn = true;
    d.meshMaxBoundaryEdgeM = 30.0;
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
    QCOMPARE(r.meshMinAngleDeg, d.meshMinAngleDeg);
    QCOMPARE(r.meshMaxArea, d.meshMaxArea);
    QCOMPARE(r.meshMaxSteiner, d.meshMaxSteiner);
    QCOMPARE(r.meshIdwPower, d.meshIdwPower);
    QCOMPARE(r.meshSimplifyEpsM, d.meshSimplifyEpsM);
    QCOMPARE(r.meshSnapEpsM, d.meshSnapEpsM);
    QCOMPARE(r.meshNodeFlattenRadM, d.meshNodeFlattenRadM);
    QCOMPARE(r.meshMinNodeSepOn, d.meshMinNodeSepOn);
    QCOMPARE(r.meshMinNodeSepM, d.meshMinNodeSepM);
    QCOMPARE(r.meshThinningOn, d.meshThinningOn);
    QCOMPARE(r.meshThinningTol, d.meshThinningTol);
    QCOMPARE(r.meshThinningPasses, d.meshThinningPasses);
    QCOMPARE(r.meshBoundaryBufferM, d.meshBoundaryBufferM);
    QCOMPARE(r.meshMaxBoundaryEdgeOn, d.meshMaxBoundaryEdgeOn);
    QCOMPARE(r.meshMaxBoundaryEdgeM, d.meshMaxBoundaryEdgeM);
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

QTEST_MAIN(TestTwoDDefaultsPrefs)
#include "test_twod_defaults_prefs.moc"
