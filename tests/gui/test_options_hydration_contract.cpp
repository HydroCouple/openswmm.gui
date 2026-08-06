/*!
 * \file   test_options_hydration_contract.cpp
 * \brief  Slice CX (Phase CX.1) QtTest: verifies the §M.1 hydration contract
 *         for status-bar widgets whose source-of-truth is a SWMM [OPTIONS]
 *         key.
 *
 * Contract (see docs/GUI_IMPLEMENTATION_PLAN.md §M.1): every in-scope control
 * must hydrate from the active project's engine at all three of:
 *   1. Project open      — modelled here as syncFromEngine() on a fresh engine.
 *   2. Tab activation    — modelled here as setActiveProject(&perProject).
 *   3. External mutation — modelled here as setFlowUnits(units, engine), which
 *                          writes through to the engine AND emits unitsChanged
 *                          through the facade.
 *
 * Self-contained: pulls in only UnitSystem + the swmm_options_* engine ABI
 * (link to openswmm_engine via the CMake helper). No MDI / MainWindow.
 *
 * Coverage today: FLOW_UNITS (Flow Units combo).  The LINK_OFFSETS leg lives
 * on SWMMVisProjectWindow and is exercised through that class once the
 * `swmmvis_core` extraction unblocks instantiating-the-app tests; the audit
 * entry is kept in swmmvis_hydration_audit.h so it isn't lost.
 */

#include "core/unitsystem.h"
#include "ui/swmmvis_hydration_audit.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <cstring>

namespace {

/*! \brief Build a synthetic engine in BUILDING state and stamp a FLOW_UNITS
 *         value on it. No node / link / outfall topology is needed for an
 *         OPTIONS-only round-trip. */
SWMM_Engine makeEngineWithFlowUnits(const char *flowUnitsToken)
{
    SWMM_Engine e = swmm_engine_new();
    Q_ASSERT(e != nullptr);
    const int rc = swmm_options_set(e, "FLOW_UNITS", flowUnitsToken);
    Q_ASSERT(rc == 0);
    Q_UNUSED(rc);
    return e;
}

/*! \brief Read FLOW_UNITS straight from the engine — independent of UnitSystem
 *         so trigger #3 can assert the value reached the engine, not just
 *         that the in-process cache flipped. */
QString readFlowUnitsFromEngine(SWMM_Engine e)
{
    char buf[32] = {};
    const int rc = swmm_options_get(e, "FLOW_UNITS", buf, sizeof(buf));
    Q_ASSERT(rc == 0);
    Q_UNUSED(rc);
    return QString::fromLatin1(buf).trimmed().toUpper();
}

} // anonymous namespace

class TestOptionsHydrationContract : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();   // unbind the facade between cases so leaks can't bleed.

    // §M.1 triggers, exercised against the engine ABI.
    void trigger1_projectOpen_syncsFlowUnitsFromEngine();
    void trigger2_tabActivation_flipsFacadeBetweenProjects();
    void trigger3_externalMutation_writesEngineAndFiresFacade();

    // LINK_OFFSETS ABI round-trip — covers the second audit entry at the
    // engine layer. The engine's standard OPTIONS switch did not recognise
    // LINK_OFFSETS until Slice CX Phase CX.4 added it (2026-05-22); this
    // case is the regression guard for that engine patch. The full GUI-side
    // leg lives on SWMMVisProjectWindow and is parked under the swmmvis_core
    // extraction blocker (see KNOWN GAPS).
    void linkOffsets_engineRoundTripsValue();

    // Simulation Options persistence fixes (2026-08-06): keys whose broken
    // C-API round-trip made dialog edits silently revert on reload.
    void minimumStep_engineRoundTripsValue();
    void flowTolerances_percentIdempotent();
    void ruleStep_over24hRoundTrips();
    void threads_engineRoundTripsValue();

    // §M.3 sanity — controls explicitly out of scope must not appear in the
    // audit list (canary against future drift).
    void auditList_excludesOutOfScopeWidgets();

    // Phase CX.3 — every audit entry has a non-empty key + name.
    void auditList_isWellFormed();
};

// ===========================================================================
// KNOWN GAPS — tests that require instantiating SWMMVis / SWMMVisProjectWindow
// and are parked behind the swmmvis_core static-lib extraction (see the
// parked-tests comment in tests/gui/CMakeLists.txt). Each gap is enumerated
// here so it cannot be silently lost; pick them up in the same change set
// that re-enables test_simulationoptionsdialog.
//
// CX.4-regression — Slice CX Phase CX.4 fix (2026-05-21): SWMMVis::openSingleINP
//   nulls mActiveProjectWindow before its explicit onActiveSubWindowChanged
//   re-call so the same-project guard does not block the post-loadModel
//   hydration. End-to-end multi-MDI test: open project A, assert status-bar
//   Flow Units combo reflects A's FLOW_UNITS; open project B in a second MDI
//   subwindow, assert the combo flipped to B's value; tab back to A, assert
//   the combo flipped back. The cases below already exercise this contract
//   at the UnitSystem facade layer — the parked test would extend the same
//   walk through the real status-bar widget pointers in SWMMVis.
//
// CX-dialog-tabs — Phase CX.2 dialog-tab read-helper audit: open
//   SimulationOptionsDialog twice against two engines with differing OPTIONS
//   values and confirm each of the 8 tabs' controls flipped. Today's parked
//   test_simulationoptionsdialog only covers pure helpers; this would
//   re-enable real-engine round-trip coverage for Tabs 0, 3, 4, 5, 6.
//
// CX-extended-keys — the standard swmm_options_get/set surface has grown to
//   cover the full Simulation Options dialog (LINK_OFFSETS 2026-05-22 as part
//   of Slice CX.4; MINIMUM_STEP + percent LAT/SYS_FLOW_TOL 2026-08-06 with
//   the dialog persistence fixes, round-trip-tested below). Extend
//   kStatusBarHydrationAudit as further keys come online.
// ===========================================================================

void TestOptionsHydrationContract::cleanup()
{
    UnitSystem::setActiveProject(nullptr);
}

// ---------------------------------------------------------------------------
// §M.1 — Trigger 1: Project open hydrates from engine
// ---------------------------------------------------------------------------

void TestOptionsHydrationContract::trigger1_projectOpen_syncsFlowUnitsFromEngine()
{
    SWMM_Engine eCfs = makeEngineWithFlowUnits("CFS");
    SWMM_Engine eCms = makeEngineWithFlowUnits("CMS");

    UnitSystem a;
    UnitSystem b;

    // Models the project-open path: SWMMVisProjectWindow::loadModel() →
    // mUnits->syncFromEngine(mModelLayer->engine()).
    a.syncFromEngine(eCfs);
    b.syncFromEngine(eCms);

    QCOMPARE(a.flowUnits(), swmm_CFS);
    QCOMPARE(b.flowUnits(), swmm_CMS);
    QVERIFY(!a.isSI());
    QVERIFY( b.isSI());

    swmm_engine_destroy(eCfs);
    swmm_engine_destroy(eCms);
}

// ---------------------------------------------------------------------------
// §M.1 — Trigger 2: Tab activation flips the facade
// ---------------------------------------------------------------------------

void TestOptionsHydrationContract::trigger2_tabActivation_flipsFacadeBetweenProjects()
{
    SWMM_Engine eA = makeEngineWithFlowUnits("CFS");
    SWMM_Engine eB = makeEngineWithFlowUnits("LPS");

    UnitSystem a; a.syncFromEngine(eA);
    UnitSystem b; b.syncFromEngine(eB);

    // Activate A — facade must report A's units.
    UnitSystem::setActiveProject(&a);
    QCOMPARE(UnitSystem::instance()->flowUnits(),     swmm_CFS);
    QCOMPARE(UnitSystem::instance()->flowUnitLabel(), QStringLiteral("CFS"));
    QVERIFY(!UnitSystem::instance()->isSI());

    // Switch tab to B — facade must flip without any per-callsite work.
    UnitSystem::setActiveProject(&b);
    QCOMPARE(UnitSystem::instance()->flowUnits(),     swmm_LPS);
    QCOMPARE(UnitSystem::instance()->flowUnitLabel(), QStringLiteral("LPS"));
    QVERIFY( UnitSystem::instance()->isSI());

    // Switch back to A — facade must flip back; no stale-cache survival.
    UnitSystem::setActiveProject(&a);
    QCOMPARE(UnitSystem::instance()->flowUnits(),     swmm_CFS);

    swmm_engine_destroy(eA);
    swmm_engine_destroy(eB);
}

// ---------------------------------------------------------------------------
// §M.1 — Trigger 3: External mutation propagates engine + facade
// ---------------------------------------------------------------------------

void TestOptionsHydrationContract::trigger3_externalMutation_writesEngineAndFiresFacade()
{
    SWMM_Engine eA = makeEngineWithFlowUnits("CFS");

    UnitSystem a;
    a.syncFromEngine(eA);
    UnitSystem::setActiveProject(&a);

    // Spy on the facade — what a status-bar combo would listen to.
    QSignalSpy facadeSpy(UnitSystem::instance(), &UnitSystem::unitsChanged);

    // Drain the rebind-fired unitsChanged so the spy count reflects mutation only.
    facadeSpy.clear();

    // External mutation through the facade: simulates a Simulation Options
    // dialog Apply or a scripted edit. Must write to the engine AND notify
    // listeners.
    UnitSystem::instance()->setFlowUnits(swmm_MGD, eA);

    // Engine round-trip — the source-of-truth must have flipped.
    QCOMPARE(readFlowUnitsFromEngine(eA), QStringLiteral("MGD"));

    // In-process — facade reflects the new value immediately.
    QCOMPARE(UnitSystem::instance()->flowUnits(), swmm_MGD);

    // Notification — exactly one fan-out through the facade.
    QCOMPARE(facadeSpy.count(), 1);
    QCOMPARE(facadeSpy.takeFirst().at(0).toInt(), static_cast<int>(swmm_MGD));

    swmm_engine_destroy(eA);
}

// ---------------------------------------------------------------------------
// LINK_OFFSETS — engine ABI round-trip (second audit entry)
// ---------------------------------------------------------------------------

void TestOptionsHydrationContract::linkOffsets_engineRoundTripsValue()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // Both legal tokens round-trip exactly. The GUI's Offset Mode checkbox
    // does a case-insensitive compare against "ELEVATION" (see
    // SWMMVisProjectWindow::reloadElevationOffsetModeFromEngine), so this
    // case verifies the ABI surface that GUI binding relies on.
    QCOMPARE(swmm_options_set(e, "LINK_OFFSETS", "ELEVATION"), 0);
    char buf[32] = {};
    QCOMPARE(swmm_options_get(e, "LINK_OFFSETS", buf, sizeof(buf)), 0);
    QCOMPARE(QString::fromLatin1(buf).trimmed().toUpper(), QStringLiteral("ELEVATION"));

    QCOMPARE(swmm_options_set(e, "LINK_OFFSETS", "DEPTH"), 0);
    std::memset(buf, 0, sizeof(buf));
    QCOMPARE(swmm_options_get(e, "LINK_OFFSETS", buf, sizeof(buf)), 0);
    QCOMPARE(QString::fromLatin1(buf).trimmed().toUpper(), QStringLiteral("DEPTH"));

    swmm_engine_destroy(e);
}

// ---------------------------------------------------------------------------
// Simulation Options persistence fixes (2026-08-06) — engine ABI round-trips
// for the keys whose broken get/set made dialog edits revert on reload.
// ---------------------------------------------------------------------------

namespace {

QString getOptionString(SWMM_Engine e, const char *key)
{
    char buf[64] = {};
    const int rc = swmm_options_get(e, key, buf, sizeof(buf));
    Q_ASSERT(rc == 0);
    Q_UNUSED(rc);
    return QString::fromLatin1(buf).trimmed();
}

double getOptionDouble(SWMM_Engine e, const char *key)
{
    bool ok = false;
    const double v = getOptionString(e, key).toDouble(&ok);
    Q_ASSERT(ok);
    Q_UNUSED(ok);
    return v;
}

} // anonymous namespace

void TestOptionsHydrationContract::minimumStep_engineRoundTripsValue()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // Historically swmm_options_set rejected MINIMUM_STEP entirely, so the
    // dialog's minimum-routing-step spin always showed the preferences
    // default and edits were dropped (and the "Apply fast preset" button was
    // half a no-op).
    QCOMPARE(getOptionDouble(e, "MINIMUM_STEP"), 0.5);   // engine default

    QCOMPARE(swmm_options_set(e, "MINIMUM_STEP", "1.0"), 0);
    QCOMPARE(getOptionDouble(e, "MINIMUM_STEP"), 1.0);

    // Clock form, same grammar as the [OPTIONS] parser.
    QCOMPARE(swmm_options_set(e, "MINIMUM_STEP", "0:00:02"), 0);
    QCOMPARE(getOptionDouble(e, "MINIMUM_STEP"), 2.0);

    swmm_engine_destroy(e);
}

void TestOptionsHydrationContract::flowTolerances_percentIdempotent()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // Percent on both sides. The getter used to return the stored fraction
    // while the setter divided by 100, so every dialog OK shrank the
    // tolerance 100× — a 5% tolerance came back as 0.05% on reload.
    QCOMPARE(swmm_options_set(e, "LAT_FLOW_TOL", "5"), 0);
    QCOMPARE(swmm_options_set(e, "SYS_FLOW_TOL", "5"), 0);
    QCOMPARE(getOptionDouble(e, "LAT_FLOW_TOL"), 5.0);
    QCOMPARE(getOptionDouble(e, "SYS_FLOW_TOL"), 5.0);

    // get → set(returned) → get twice must be a fixed point.
    for (int i = 0; i < 2; ++i) {
        const QByteArray echoed = getOptionString(e, "LAT_FLOW_TOL").toLatin1();
        QCOMPARE(swmm_options_set(e, "LAT_FLOW_TOL", echoed.constData()), 0);
    }
    QCOMPARE(getOptionDouble(e, "LAT_FLOW_TOL"), 5.0);

    swmm_engine_destroy(e);
}

void TestOptionsHydrationContract::ruleStep_over24hRoundTrips()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // The dialog's old QTimeEdit clamped RULE_STEP to 23:59:59 on read and
    // wrote the clamped value back; the engine itself has always accepted
    // > 24 h. The widget is now a QCustomTimespanEdit (days + HH:mm:ss).
    QCOMPARE(swmm_options_set(e, "RULE_STEP", "48:00:00"), 0);
    QCOMPARE(getOptionDouble(e, "RULE_STEP"), 172800.0);

    swmm_engine_destroy(e);
}

void TestOptionsHydrationContract::threads_engineRoundTripsValue()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // THREADS: plain integer; 0 is the "auto" sentinel the engine maps to
    // omp_get_max_threads() at start, matching the GUI spinbox's 0 = "auto".
    QCOMPARE(getOptionString(e, "THREADS"), QStringLiteral("1"));  // default

    QCOMPARE(swmm_options_set(e, "THREADS", "8"), 0);
    QCOMPARE(getOptionString(e, "THREADS"), QStringLiteral("8"));

    QCOMPARE(swmm_options_set(e, "THREADS", "0"), 0);
    QCOMPARE(getOptionString(e, "THREADS"), QStringLiteral("0"));

    swmm_engine_destroy(e);
}

// ---------------------------------------------------------------------------
// §M.3 — Out-of-scope controls must stay out of the audit
// ---------------------------------------------------------------------------

void TestOptionsHydrationContract::auditList_excludesOutOfScopeWidgets()
{
    using openswmmvis::kStatusBarHydrationAudit;

    auto contains = [&](const char *needle) {
        for (const auto &row : kStatusBarHydrationAudit)
            if (std::strstr(row.widgetName, needle) != nullptr)
                return true;
        return false;
    };

    QVERIFY2(!contains("Auto-Length"),    "Auto-Length is a GUI preference (§M.3) and must not appear in the OPTIONS audit.");
    QVERIFY2(!contains("Engine Version"), "Engine Version is project metadata (§M.3) and must not appear in the OPTIONS audit.");
    QVERIFY2(!contains("Coordinates"),    "Coordinates is a live readout (§M.3) and must not appear in the OPTIONS audit.");
    QVERIFY2(!contains("Progress"),       "Progress bar is live state (§M.3) and must not appear in the OPTIONS audit.");
}

// ---------------------------------------------------------------------------
// Phase CX.3 — every audit entry well-formed
// ---------------------------------------------------------------------------

void TestOptionsHydrationContract::auditList_isWellFormed()
{
    using openswmmvis::kStatusBarHydrationAudit;

    QVERIFY(kStatusBarHydrationAudit.size() > 0);
    for (const auto &row : kStatusBarHydrationAudit) {
        QVERIFY(row.widgetName != nullptr && row.widgetName[0] != '\0');
        QVERIFY(row.optionsKey != nullptr && row.optionsKey[0] != '\0');
    }
}

QTEST_MAIN(TestOptionsHydrationContract)
#include "test_options_hydration_contract.moc"
