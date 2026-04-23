/*!
 * \file   test_unitsystemfacade.cpp
 * \brief  Slice B QtTest: verifies UnitSystem delegating-facade semantics.
 *
 * Phase 0.5 / 0.6 contract: each project owns a per-instance UnitSystem;
 * UnitSystem::instance() is a facade whose accessors forward to the
 * currently-active per-project instance, set by setActiveProject(). On
 * tab switch the facade emits unitsChanged so dialogs refresh.
 *
 * The test compiles unitsystem.cpp directly (no engine linkage required —
 * the stub typedefs in unitsystem.h take over when HAVE_OPENSWMMCORE is
 * undefined, which is the test build configuration).
 */

#include "core/unitsystem.h"

#include <QObject>
#include <QSignalSpy>
#include <QTest>

class TestUnitSystemFacade : public QObject
{
    Q_OBJECT
private slots:
    void perProjectInstancesAreIndependent();
    void facadeReflectsActiveProject();
    void rebindEmitsUnitsChangedThroughFacade();
    void facadeSetterRoutesToActiveProject();
    void unboundFacadeIsSafe();
    void labelsFollowActiveProjectSI();
};

void TestUnitSystemFacade::perProjectInstancesAreIndependent()
{
    UnitSystem a;
    UnitSystem b;
    a.setFlowUnits(swmm_CFS);
    b.setFlowUnits(swmm_CMS);
    QCOMPARE(a.flowUnits(), swmm_CFS);
    QCOMPARE(b.flowUnits(), swmm_CMS);
    QVERIFY(!a.isSI());
    QVERIFY( b.isSI());
}

void TestUnitSystemFacade::facadeReflectsActiveProject()
{
    UnitSystem a; a.setFlowUnits(swmm_GPM);
    UnitSystem b; b.setFlowUnits(swmm_LPS);

    UnitSystem::setActiveProject(&a);
    QCOMPARE(UnitSystem::instance()->flowUnits(),       swmm_GPM);
    QCOMPARE(UnitSystem::instance()->flowUnitLabel(),   QStringLiteral("GPM"));
    QVERIFY(!UnitSystem::instance()->isSI());

    UnitSystem::setActiveProject(&b);
    QCOMPARE(UnitSystem::instance()->flowUnits(),       swmm_LPS);
    QCOMPARE(UnitSystem::instance()->flowUnitLabel(),   QStringLiteral("LPS"));
    QVERIFY( UnitSystem::instance()->isSI());

    UnitSystem::setActiveProject(nullptr);
}

void TestUnitSystemFacade::rebindEmitsUnitsChangedThroughFacade()
{
    UnitSystem a; a.setFlowUnits(swmm_CFS);
    UnitSystem b; b.setFlowUnits(swmm_CMS);

    QSignalSpy spy(UnitSystem::instance(), &UnitSystem::unitsChanged);

    UnitSystem::setActiveProject(&a);
    UnitSystem::setActiveProject(&b);   // rebind → facade re-emits

    QVERIFY(spy.count() >= 2);
    QCOMPARE(spy.takeLast().at(0).toInt(), static_cast<int>(swmm_CMS));

    UnitSystem::setActiveProject(nullptr);
}

void TestUnitSystemFacade::facadeSetterRoutesToActiveProject()
{
    UnitSystem a; a.setFlowUnits(swmm_CFS);
    UnitSystem::setActiveProject(&a);

    QSignalSpy aSpy(&a, &UnitSystem::unitsChanged);

    // Setter on the facade — should mutate `a`, fire its signal, and bubble
    // through the facade.
    UnitSystem::instance()->setFlowUnits(swmm_MGD);

    QCOMPARE(a.flowUnits(), swmm_MGD);
    QCOMPARE(aSpy.count(), 1);
    QCOMPARE(aSpy.takeFirst().at(0).toInt(), static_cast<int>(swmm_MGD));

    UnitSystem::setActiveProject(nullptr);
}

void TestUnitSystemFacade::unboundFacadeIsSafe()
{
    UnitSystem::setActiveProject(nullptr);
    // No project bound → accessors return defaults, setters are no-ops.
    QCOMPARE(UnitSystem::instance()->flowUnits(),     swmm_CFS);
    QCOMPARE(UnitSystem::instance()->flowUnitLabel(), QStringLiteral("CFS"));
    UnitSystem::instance()->setFlowUnits(swmm_LPS);   // must not crash
    QCOMPARE(UnitSystem::instance()->flowUnits(),     swmm_CFS);
}

void TestUnitSystemFacade::labelsFollowActiveProjectSI()
{
    UnitSystem us; us.setFlowUnits(swmm_CFS);    // US units
    UnitSystem si; si.setFlowUnits(swmm_CMS);    // SI units

    UnitSystem::setActiveProject(&us);
    QCOMPARE(UnitSystem::instance()->lengthLabel(),   QStringLiteral("ft"));
    QCOMPARE(UnitSystem::instance()->velocityLabel(), QStringLiteral("ft/s"));
    QCOMPARE(UnitSystem::instance()->areaLabel(),     QStringLiteral("ac"));

    UnitSystem::setActiveProject(&si);
    QCOMPARE(UnitSystem::instance()->lengthLabel(),   QStringLiteral("m"));
    QCOMPARE(UnitSystem::instance()->velocityLabel(), QStringLiteral("m/s"));
    QCOMPARE(UnitSystem::instance()->areaLabel(),     QStringLiteral("ha"));

    UnitSystem::setActiveProject(nullptr);
}

QTEST_MAIN(TestUnitSystemFacade)
#include "test_unitsystemfacade.moc"
