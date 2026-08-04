/*!
 * \file   test_object_defaults_prefs.cpp
 * \brief  Pins PreferencesManager::ObjectDefaults — the per-object-type
 *         creation-defaults bundle.
 *         Plan: workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md
 *
 * Contracts:
 *  1. The compiled-in seeds match the signed-off defaults table (US via
 *     member initializers / usSeed(), SI via siSeed()'s overrides). These
 *     are what every drawn/imported object receives — drift here silently
 *     changes new models.
 *  2. The US and SI sets persist independently: writing one leaves the
 *     other untouched.
 *  3. setObjectDefaults → objectDefaults round-trips fields through
 *     QSettings for both sets.
 *  4. setObjectDefaults emits preferenceChanged("Defaults", "ObjectDefaults").
 */

#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>

#include "core/preferencesmanager.h"

class TestObjectDefaultsPrefs : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void compiledSeedsPinTheTable();
    void roundTripPersistsFields();
    void usAndSiSetsAreIndependent();
    void setterEmitsPreferenceChanged();
};

void TestObjectDefaultsPrefs::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("objectdefaults-test"));
    QSettings settings;
    settings.remove(QStringLiteral("SWMMVis/Preferences/ObjectDefaults"));
    settings.sync();
}

void TestObjectDefaultsPrefs::compiledSeedsPinTheTable()
{
    using OD = PreferencesManager::ObjectDefaults;
    const OD us = OD::usSeed();
    const OD si = OD::siSeed();

    // Junction: 0 max depth IS the safe value (engine auto from crown).
    QCOMPARE(us.junctionMaxDepth, 0.0);

    // Nonzero storage geometry — a 0-depth storage node is always wrong.
    QCOMPARE(us.storageMaxDepth, 15.0);
    QCOMPARE(us.storageFuncConstant, 1000.0);
    QCOMPARE(si.storageMaxDepth, 4.5);
    QCOMPARE(si.storageFuncConstant, 100.0);

    QCOMPARE(us.outfallType, QStringLiteral("FREE"));
    QCOMPARE(us.dividerType, QStringLiteral("OVERFLOW"));

    // Conduit: 1 ft / 0.3 m circular, concrete roughness, nonzero length.
    QCOMPARE(us.conduitShape, QStringLiteral("CIRCULAR"));
    QCOMPARE(us.conduitGeom1, 1.0);
    QCOMPARE(us.conduitRoughness, 0.013);
    QCOMPARE(us.conduitLength, 400.0);
    QCOMPARE(us.conduitBarrels, 1);
    QCOMPARE(si.conduitGeom1, 0.3);
    QCOMPARE(si.conduitLength, 120.0);
    QCOMPARE(si.conduitRoughness, 0.013);   // dimensionless — no override

    QCOMPARE(us.pumpInitStateOn, true);

    QCOMPARE(us.orificeType, QStringLiteral("SIDE"));
    QCOMPARE(us.orificeGeom1, 1.0);
    QCOMPARE(us.orificeCd, 0.65);
    QCOMPARE(si.orificeGeom1, 0.3);

    // Weir Cd and outlet C are unit-semantic — the whole reason for
    // parallel sets.
    QCOMPARE(us.weirCd, 3.33);
    QCOMPARE(si.weirCd, 1.84);
    QCOMPARE(us.weirGeom1, 3.0);
    QCOMPARE(si.weirGeom1, 1.0);
    QCOMPARE(us.outletCoeff, 10.0);
    QCOMPARE(si.outletCoeff, 0.5);
    QCOMPARE(us.outletExponent, 0.5);

    // Subcatchment.
    QCOMPARE(us.subcatchArea, 5.0);
    QCOMPARE(us.subcatchWidth, 500.0);
    QCOMPARE(us.subcatchSlopePct, 0.5);
    QCOMPARE(us.subcatchImpervPct, 25.0);
    QCOMPARE(us.subcatchNImperv, 0.012);
    QCOMPARE(us.subcatchNPerv, 0.15);
    QCOMPARE(us.subcatchDsImperv, 0.06);
    QCOMPARE(si.subcatchArea, 2.0);
    QCOMPARE(si.subcatchWidth, 150.0);
    QCOMPARE(si.subcatchDsImperv, 1.5);
    QCOMPARE(us.hortonMaxRate, 3.0);
    QCOMPARE(si.hortonMaxRate, 76.0);
    QCOMPARE(us.gaSuction, 3.5);
    QCOMPARE(si.gaSuction, 89.0);
    QCOMPARE(us.gaImd, 0.26);
    QCOMPARE(us.cnCurveNumber, 80.0);

    QCOMPARE(us.gageRainFormat, QStringLiteral("INTENSITY"));
    QCOMPARE(us.gageIntervalMin, 15);
    QCOMPARE(us.gageSnowCatch, 1.0);
}

void TestObjectDefaultsPrefs::roundTripPersistsFields()
{
    auto *p = PreferencesManager::instance();
    using OD = PreferencesManager::ObjectDefaults;

    OD d = OD::usSeed();
    d.junctionMaxDepth  = 12.5;
    d.conduitGeom1      = 1.5;
    d.conduitRoughness  = 0.024;
    d.subcatchWidth     = 321.0;
    d.weirCd            = 2.9;
    d.outfallType       = QStringLiteral("NORMAL");
    d.pumpInitStateOn   = false;
    d.gageIntervalMin   = 5;
    p->setObjectDefaults(d, /*si=*/false);

    const OD r = p->objectDefaults(/*si=*/false);
    QCOMPARE(r.junctionMaxDepth, 12.5);
    QCOMPARE(r.conduitGeom1, 1.5);
    QCOMPARE(r.conduitRoughness, 0.024);
    QCOMPARE(r.subcatchWidth, 321.0);
    QCOMPARE(r.weirCd, 2.9);
    QCOMPARE(r.outfallType, QStringLiteral("NORMAL"));
    QCOMPARE(r.pumpInitStateOn, false);
    QCOMPARE(r.gageIntervalMin, 5);
    // Untouched field still carries the seed.
    QCOMPARE(r.orificeCd, 0.65);
}

void TestObjectDefaultsPrefs::usAndSiSetsAreIndependent()
{
    auto *p = PreferencesManager::instance();
    using OD = PreferencesManager::ObjectDefaults;

    OD us = OD::usSeed();
    us.conduitGeom1 = 2.25;
    p->setObjectDefaults(us, /*si=*/false);

    // SI set must still read its own seed, not the US write.
    QCOMPARE(p->objectDefaults(/*si=*/true).conduitGeom1, 0.3);

    OD si = OD::siSeed();
    si.conduitGeom1 = 0.45;
    p->setObjectDefaults(si, /*si=*/true);

    QCOMPARE(p->objectDefaults(/*si=*/true).conduitGeom1, 0.45);
    QCOMPARE(p->objectDefaults(/*si=*/false).conduitGeom1, 2.25);
}

void TestObjectDefaultsPrefs::setterEmitsPreferenceChanged()
{
    auto *p = PreferencesManager::instance();
    QSignalSpy spy(p, &PreferencesManager::preferenceChanged);

    p->setObjectDefaults(PreferencesManager::ObjectDefaults::usSeed(), false);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("Defaults"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("ObjectDefaults"));
}

QTEST_MAIN(TestObjectDefaultsPrefs)
#include "test_object_defaults_prefs.moc"
