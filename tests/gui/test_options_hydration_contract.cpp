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
 * Coverage today: FLOW_UNITS (Flow Units combo) at all three triggers, and
 * LINK_OFFSETS (Offset Mode checkbox) at the engine layer — both the bare-ABI
 * round-trip and the file → engine → file leg the toggle actually drives.
 * What still needs SWMMVisProjectWindow is the widget binding itself (does the
 * checkbox reflect the engine at open / tab switch / external mutation); that
 * is parked behind the `swmmvis_core` extraction, and the audit entry is kept
 * in swmmvis_hydration_audit.h so it isn't lost.
 */

#include "core/unitsystem.h"
#include "ui/swmmvis_hydration_audit.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
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
    void linkOffsets_fileRoundTripsThroughSave();

    // Simulation Options persistence fixes (2026-08-06): keys whose broken
    // C-API round-trip made dialog edits silently revert on reload.
    void minimumStep_engineRoundTripsValue();
    void flowTolerances_percentIdempotent();
    void ruleStep_over24hRoundTrips();
    void threads_engineRoundTripsValue();

    // FLOW_ROUTING FV + the FV_* option family (2026-08-06). The dialog's
    // FV groups hydrate and write through exactly these keys, and the
    // capability gate in applyEngineConstraints() probes FV_CFL.
    void flowRouting_fvRoundTripsValue();
    void fvOptions_engineRoundTripsValues();
    void fvOptions_rejectBadEnumTokens();
    // FV_NODE_COUPLING / FV_NODE_DT / FV_NODE_PICARD retired (2026-08-29):
    // widgets gone; the engine must still accept and freeze them so older
    // projects and scripts keep working.
    void fvOptions_retiredNodeKeysAcceptAndFreeze();

    // QUALITY_SOLVER + the transport option family (Y1 / G1g, 2026-08-23).
    // The Quality & Transport page hydrates and writes through exactly
    // these keys, and its capability gate probes QUALITY_SOLVER — which
    // only reached the C API in subplan Y0, AFTER the parser had them
    // (the prerequisite-in-the-wrong-layer trap that round records).
    void transportOptions_engineRoundTripsValues();
    void transportOptions_rejectBadEnumTokens();

    // Mixed-flow option surface (engine issue #156; GUI issue #10). The
    // Routing & Hydraulics tab hydrates and writes through exactly these
    // keys, and the capability gates in applyEngineConstraints() probe
    // TPA_CELERITY / FV_PRESSURE_CLOSURE / UNSTEADY_FRICTION /
    // REPORT_SIGNED_HEADS individually.
    void mixedFlowOptions_engineRoundTripsValues();
    void mixedFlowOptions_rejectBadEnumTokens();

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
// LINK_OFFSETS — file → engine → file, the leg the Offset Mode toggle drives
// ---------------------------------------------------------------------------
//
// linkOffsets_engineRoundTripsValue above proves the ABI on a BARE engine; it
// never parses or writes a deck. Two halves of the toggle's contract are only
// observable through a file:
//
//   Read  — a deck declaring ELEVATION must arrive at the engine as ELEVATION.
//           ELEVATION is the discriminating token here: DEPTH is also the
//           SimulationOptions default (link_offsets = 0), so asserting DEPTH
//           after a load (as test_asyncload does) passes even if the parser
//           dropped the key entirely. Only ELEVATION can fail that way.
//   Save  — flipping the option (what SWMMVisProjectWindow::
//           setElevationOffsetMode does via swmm_options_set) must reach the
//           written .inp, and survive re-parsing it.
//
void TestOptionsHydrationContract::linkOffsets_fileRoundTripsThroughSave()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Offsets are declared as elevations here (Z1/Z2 above the inverts), which
    // is what LINK_OFFSETS ELEVATION means; the option is what is under test,
    // not the values.
    const auto writeDeck = [&dir](const QString &name, const char *token) {
        const QString path = dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();
        f.write(QStringLiteral(
                    "[OPTIONS]\n"
                    "FLOW_UNITS           CFS\n"
                    "FLOW_ROUTING         DYNWAVE\n"
                    "LINK_OFFSETS         %1\n"
                    "START_DATE           01/01/2026\n"
                    "END_DATE             01/01/2026\n"
                    "END_TIME             00:30:00\n"
                    "ROUTING_STEP         1\n"
                    "\n[JUNCTIONS]\n J1  10.0  5.0\n"
                    "\n[OUTFALLS]\n O1  8.0  FREE  NO\n"
                    "\n[CONDUITS]\n C1  J1  O1  200.0  0.013  11.0  9.0\n"
                    "\n[XSECTIONS]\n C1  CIRCULAR  1.0  0  0  0  1\n"
                    "\n[COORDINATES]\n J1  0.0  0.0\n O1  200.0  0.0\n")
                    .arg(QString::fromLatin1(token)).toUtf8());
        f.close();
        return path;
    };

    const auto optionOf = [](SWMM_Engine e) {
        char buf[32] = {};
        if (swmm_options_get(e, "LINK_OFFSETS", buf, sizeof(buf)) != 0)
            return QString();
        return QString::fromLatin1(buf).trimmed().toUpper();
    };

    const QString src = writeDeck(QStringLiteral("offsets_elev.inp"), "ELEVATION");
    QVERIFY(!src.isEmpty());

    // ---- Read: the parser must honour ELEVATION, not fall back to default.
    SWMM_Engine e = swmm_engine_create();
    QVERIFY(e != nullptr);
    QCOMPARE(swmm_engine_open(e, src.toUtf8().constData(),
                              dir.filePath(QStringLiteral("r.rpt")).toUtf8().constData(),
                              dir.filePath(QStringLiteral("r.out")).toUtf8().constData(),
                              nullptr), 0);
    QCOMPARE(optionOf(e), QStringLiteral("ELEVATION"));

    // ---- Save: flip it the way the Offset Mode toggle does, then write.
    QCOMPARE(swmm_options_set(e, "LINK_OFFSETS", "DEPTH"), 0);
    const QString out = dir.filePath(QStringLiteral("offsets_saved.inp"));
    QCOMPARE(swmm_model_write(e, out.toUtf8().constData()), 0);
    swmm_engine_close(e);
    swmm_engine_destroy(e);

    // The written deck carries the NEW token — a save that dropped the edit
    // would still say ELEVATION here.
    QFile written(out);
    QVERIFY(written.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(written.readAll());
    written.close();
    QVERIFY2(text.contains(QStringLiteral("LINK_OFFSETS")),
             "written deck has no LINK_OFFSETS row");
    QVERIFY2(!text.contains(QStringLiteral("LINK_OFFSETS         ELEVATION")),
             "save did not apply the flip to DEPTH");

    // ---- And it survives a full re-parse of what was written.
    SWMM_Engine e2 = swmm_engine_create();
    QVERIFY(e2 != nullptr);
    QCOMPARE(swmm_engine_open(e2, out.toUtf8().constData(),
                              dir.filePath(QStringLiteral("r2.rpt")).toUtf8().constData(),
                              dir.filePath(QStringLiteral("r2.out")).toUtf8().constData(),
                              nullptr), 0);
    QCOMPARE(optionOf(e2), QStringLiteral("DEPTH"));
    swmm_engine_close(e2);
    swmm_engine_destroy(e2);
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
// FLOW_ROUTING FV + FV_* option family — engine ABI round-trips
// ---------------------------------------------------------------------------

void TestOptionsHydrationContract::flowRouting_fvRoundTripsValue()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // The canonical token is what the routing combo stores as item data and
    // what applyEngineConstraints() falls back from — it must echo exactly.
    QCOMPARE(swmm_options_set(e, "FLOW_ROUTING", "FV"), 0);
    QCOMPARE(getOptionString(e, "FLOW_ROUTING"), QStringLiteral("FV"));

    // "FINITE_VOLUME" is an [OPTIONS]-parser alias only; the C ABI takes
    // just the canonical token. Documented here so nobody wires the alias
    // into a combo's item data and loses the round-trip.
    QVERIFY(swmm_options_set(e, "FLOW_ROUTING", "FINITE_VOLUME") != 0);
    QCOMPARE(getOptionString(e, "FLOW_ROUTING"), QStringLiteral("FV"));

    swmm_engine_destroy(e);
}

void TestOptionsHydrationContract::fvOptions_engineRoundTripsValues()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // Defaults — these are the hard-coded fallbacks in readFromEngine();
    // if the engine defaults drift this case flags the dialog for a resync.
    QCOMPARE(getOptionDouble(e, "FV_CELL_LENGTH"), 0.0);
    QCOMPARE(getOptionString(e, "FV_MIN_CELLS"),   QStringLiteral("4"));
    QCOMPARE(getOptionDouble(e, "FV_CFL"),         0.5);
    QCOMPARE(getOptionString(e, "FV_RIEMANN"),     QStringLiteral("HLLC"));
    QCOMPARE(getOptionString(e, "FV_ORDER"),       QStringLiteral("1"));
    QCOMPARE(getOptionString(e, "FV_LIMITER"),     QStringLiteral("MINMOD"));
    QCOMPARE(getOptionString(e, "FV_TIME_INTEGRATION"), QStringLiteral("EULER"));
    QCOMPARE(getOptionDouble(e, "FV_SLOT_CELERITY"), 100.0);
    // No widget edits these two any more (FV_PRESSURIZED_IMPLICIT is hidden
    // until slot program R2b; FV_DISPERSION is inert on every path). The
    // pins stay: nothing else would notice an engine-side removal.
    QCOMPARE(getOptionString(e, "FV_PRESSURIZED_IMPLICIT"), QStringLiteral("NO"));
    QCOMPARE(getOptionDouble(e, "FV_DISPERSION"),  0.0);
    QCOMPARE(getOptionString(e, "FV_STRUCTURE_COUPLING"), QStringLiteral("SUBSTEP"));
    QCOMPARE(getOptionString(e, "FV_COMPACTION"),  QStringLiteral("YES"));
    QCOMPARE(getOptionString(e, "FV_BACKEND"),     QStringLiteral("AUTO"));
    QCOMPARE(getOptionString(e, "FV_MIN_PARALLEL_CELLS"), QStringLiteral("20000"));
    QCOMPARE(getOptionString(e, "FV_LTS"),         QStringLiteral("YES"));
    QCOMPARE(getOptionString(e, "FV_LTS_MAX_TIERS"), QStringLiteral("6"));
    QCOMPARE(getOptionString(e, "FV_CFL_CENSUS_INTERVAL"), QStringLiteral("1"));

    // Set → get for every key the dialog writes, using the exact string
    // forms writeToEngine() produces (spin formats and combo tokens).
    const struct { const char *key; const char *set; const char *expect; } rows[] = {
        { "FV_CELL_LENGTH",         "25.00",        "25"           },
        { "FV_MIN_CELLS",           "8",            "8"            },
        { "FV_CFL",                 "0.70",         "0.7"          },
        { "FV_RIEMANN",             "HLL",          "HLL"          },
        { "FV_ORDER",               "2",            "2"            },
        { "FV_LIMITER",             "VANLEER",      "VANLEER"      },
        { "FV_TIME_INTEGRATION",    "RK2",          "RK2"          },
        { "FV_SLOT_CELERITY",       "150.0",        "150"          },
        { "FV_PRESSURIZED_IMPLICIT","YES",          "YES"          },  // engine pin only
        { "FV_PRESSURIZED_IMPLICIT","NO",           "NO"           },
        { "FV_DISPERSION",          "1.500",        "1.5"          },  // engine pin only
        { "FV_STRUCTURE_COUPLING",  "ROUTING_STEP", "ROUTING_STEP" },
        { "FV_COMPACTION",          "NO",           "NO"           },
        { "FV_BACKEND",             "CPU",          "CPU"          },
        { "FV_MIN_PARALLEL_CELLS",  "5000",         "5000"         },
        { "FV_LTS",                 "NO",           "NO"           },
        { "FV_LTS_MAX_TIERS",       "3",            "3"            },
        { "FV_CFL_CENSUS_INTERVAL", "10",           "10"           },
    };
    for (const auto &r : rows) {
        QVERIFY2(swmm_options_set(e, r.key, r.set) == 0, r.key);
        // Numeric keys echo std::to_string forms ("25.000000"); compare as
        // numbers where a double parse succeeds, exactly otherwise.
        const QString got = getOptionString(e, r.key);
        bool gotNum = false, expNum = false;
        const double g = got.toDouble(&gotNum);
        const double x = QString::fromLatin1(r.expect).toDouble(&expNum);
        if (gotNum && expNum)
            QCOMPARE(g, x);
        else
            QCOMPARE(got, QString::fromLatin1(r.expect));
    }

    swmm_engine_destroy(e);
}

void TestOptionsHydrationContract::fvOptions_rejectBadEnumTokens()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // Enum keys must reject unknown tokens instead of silently keeping or
    // mangling state — the dialog relies on this to surface typos through
    // setOption() failure rather than losing edits.
    QVERIFY(swmm_options_set(e, "FV_RIEMANN",         "ROE")     != 0);
    QVERIFY(swmm_options_set(e, "FV_LIMITER",         "OSPRE")   != 0);
    QVERIFY(swmm_options_set(e, "FV_SCALAR_SCHEME",   "WENO")    != 0);
    QVERIFY(swmm_options_set(e, "FV_TIME_INTEGRATION","RK4")     != 0);
    QVERIFY(swmm_options_set(e, "FV_STRUCTURE_COUPLING", "NEVER") != 0);
    QVERIFY(swmm_options_set(e, "FV_BACKEND",         "METAL")   != 0);

    // ...and a rejected set must leave the previous value untouched.
    QCOMPARE(swmm_options_set(e, "FV_RIEMANN", "HLL"), 0);
    QVERIFY(swmm_options_set(e, "FV_RIEMANN", "ROE") != 0);
    QCOMPARE(getOptionString(e, "FV_RIEMANN"), QStringLiteral("HLL"));

    swmm_engine_destroy(e);
}

void TestOptionsHydrationContract::fvOptions_retiredNodeKeysAcceptAndFreeze()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // Retired engine-side and removed from the dialog in the same round.
    // Older projects and scripts still write them, so the C API must accept
    // ANY value (never fail the set) and keep reporting the built-in
    // behaviour (never echo the value) — exactly the FV_JUNCTION_MODEL
    // convention. Against the pre-retirement engine every row here fails,
    // because the values used to round-trip.
    const struct { const char *key; const char *frozen; const char *attempt; } rows[] = {
        { "FV_NODE_COUPLING", "SEMI_IMPLICIT", "EXPLICIT" },
        { "FV_NODE_DT",       "STABILITY",     "NONE"     },
        { "FV_NODE_PICARD",   "1",             "3"        },
    };
    for (const auto &r : rows) {
        QCOMPARE(getOptionString(e, r.key), QString::fromLatin1(r.frozen));
        QCOMPARE(swmm_options_set(e, r.key, r.attempt), 0);
        QCOMPARE(getOptionString(e, r.key), QString::fromLatin1(r.frozen));
        QCOMPARE(swmm_options_set(e, r.key, "GARBAGE"), 0);
        QCOMPARE(getOptionString(e, r.key), QString::fromLatin1(r.frozen));
    }

    swmm_engine_destroy(e);
}

// ---------------------------------------------------------------------------
// Quality & Transport (Y1 / G1g) — the page's seven keys
// ---------------------------------------------------------------------------

void TestOptionsHydrationContract::transportOptions_engineRoundTripsValues()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // Defaults — these are the fallbacks readFromEngine() passes to
    // getOption(); if the engine's defaults drift this case flags the
    // dialog for a resync (the fvOptions case's contract, same rule).
    QCOMPARE(getOptionString(e, "QUALITY_SOLVER"), QStringLiteral("LEGACY"));
    QCOMPARE(getOptionString(e, "WATER_AGE"),      QStringLiteral("NO"));
    QCOMPARE(getOptionString(e, "HEAT_TRANSPORT"), QStringLiteral("NO"));
    QCOMPARE(getOptionDouble(e, "QUALITY_STEP"),   0.0);
    QCOMPARE(getOptionString(e, "MAX_SEGMENTS_PER_LINK"), QStringLiteral("100"));
    QCOMPARE(getOptionString(e, "DISPERSION"),     QStringLiteral("OFF"));
    QCOMPARE(getOptionString(e, "RWPT_SEED"),      QStringLiteral("0"));
    QCOMPARE(getOptionString(e, "OUTFALL_BACKFLOW_QUALITY"),
             QStringLiteral("LAST"));
    // FV_SCALAR_SCHEME is edited on this page (ARD group) since the FV
    // simplification round — its only live consumer is the ARD engine.
    QCOMPARE(getOptionString(e, "FV_SCALAR_SCHEME"), QStringLiteral("MUSCL"));

    // Set → get in the exact string forms writeToEngine() produces:
    // combo tokens, QString::number(v,'f',2) for the step, plain ints.
    const struct { const char *key; const char *set; const char *expect; } rows[] = {
        { "QUALITY_SOLVER",        "LAGRANGIAN",   "LAGRANGIAN"   },
        { "FV_SCALAR_SCHEME",      "QUICKEST_ULTIMATE", "QUICKEST_ULTIMATE" },
        { "FV_SCALAR_SCHEME",      "MUSCL",        "MUSCL"        },
        { "QUALITY_STEP",          "5.00",         "5"            },
        { "MAX_SEGMENTS_PER_LINK", "50",           "50"           },
        { "DISPERSION",            "RWPT",         "RWPT"         },
        { "RWPT_SEED",             "7",            "7"            },
        { "WATER_AGE",             "YES",          "YES"          },
        { "HEAT_TRANSPORT",        "YES",          "YES"          },
        { "OUTFALL_BACKFLOW_QUALITY", "ZERO",      "ZERO"         },
        // The OFF/NO directions too — a one-way table would pass a setter
        // that can only ever turn things on.
        { "QUALITY_SOLVER",        "EULERIAN_ARD", "EULERIAN_ARD" },
        { "DISPERSION",            "OFF",          "OFF"          },
        { "WATER_AGE",             "NO",           "NO"           },
        { "HEAT_TRANSPORT",        "NO",           "NO"           },
        { "OUTFALL_BACKFLOW_QUALITY", "LAST",      "LAST"         },
        { "QUALITY_SOLVER",        "LEGACY",       "LEGACY"       },
    };
    for (const auto &r : rows) {
        QVERIFY2(swmm_options_set(e, r.key, r.set) == 0, r.key);
        const QString got = getOptionString(e, r.key);
        bool gotNum = false, expNum = false;
        const double g = got.toDouble(&gotNum);
        const double x = QString::fromLatin1(r.expect).toDouble(&expNum);
        if (gotNum && expNum)
            QCOMPARE(g, x);
        else
            QCOMPARE(got, QString::fromLatin1(r.expect));
    }

    // NOTE: the churn guard for QUALITY_STEP ("0.000000" from the engine vs
    // "0.00" from the dialog) is asserted in test_simulationoptionsdialog,
    // which links the helpers TU. This file stays engine-ABI-only on
    // purpose — including the dialog header here would pull AUTOMOC into a
    // target that cannot satisfy the dialog's link closure (the trap this
    // repo already records at tests/gui/CMakeLists.txt:1996).

    swmm_engine_destroy(e);
}

void TestOptionsHydrationContract::transportOptions_rejectBadEnumTokens()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // Enum keys must reject unknown tokens so a typo surfaces through a
    // failed setOption() rather than a silently lost edit.
    QVERIFY(swmm_options_set(e, "QUALITY_SOLVER", "MAGIC")   != 0);
    QVERIFY(swmm_options_set(e, "DISPERSION",     "FISCHER") != 0);

    // ...and a rejected set leaves the previous value untouched.
    QCOMPARE(swmm_options_set(e, "QUALITY_SOLVER", "LAGRANGIAN"), 0);
    QVERIFY(swmm_options_set(e, "QUALITY_SOLVER", "MAGIC") != 0);
    QCOMPARE(getOptionString(e, "QUALITY_SOLVER"), QStringLiteral("LAGRANGIAN"));

    // The combos' item data must be the CANONICAL tokens the getter emits:
    // the parser's aliases are accepted on the way in but never returned,
    // so a combo carrying "LARD" would never match on hydration.
    QCOMPARE(swmm_options_set(e, "QUALITY_SOLVER", "LARD"), 0);
    QCOMPARE(getOptionString(e, "QUALITY_SOLVER"), QStringLiteral("LAGRANGIAN"));
    QCOMPARE(swmm_options_set(e, "QUALITY_SOLVER", "ARD"), 0);
    QCOMPARE(getOptionString(e, "QUALITY_SOLVER"), QStringLiteral("EULERIAN_ARD"));

    swmm_engine_destroy(e);
}

// ---------------------------------------------------------------------------
// Mixed-flow options (engine issue #156; GUI issue #10) — the five new keys
// plus the SURCHARGE_METHOD=TPA enum value
// ---------------------------------------------------------------------------

void TestOptionsHydrationContract::mixedFlowOptions_engineRoundTripsValues()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // Defaults — these are the fallbacks readFromEngine() passes to
    // getOption(); if the engine's defaults drift this case flags the
    // dialog for a resync (the fvOptions case's contract, same rule).
    QCOMPARE(getOptionString(e, "SURCHARGE_METHOD"),    QStringLiteral("EXTRAN"));
    QCOMPARE(getOptionDouble(e, "TPA_CELERITY"),        100.0);
    QCOMPARE(getOptionString(e, "FV_PRESSURE_CLOSURE"), QStringLiteral("SLOT"));
    QCOMPARE(getOptionString(e, "UNSTEADY_FRICTION"),   QStringLiteral("NONE"));
    QCOMPARE(getOptionDouble(e, "UF_K3"),               0.015);
    QCOMPARE(getOptionString(e, "REPORT_SIGNED_HEADS"), QStringLiteral("NO"));

    // Set → get in the exact string forms writeToEngine() produces:
    // combo tokens, QString::number(v,'f',1) for the celerity,
    // QString::number(v,'f',3) for k3, YES/NO for the checkbox.
    const struct { const char *key; const char *set; const char *expect; } rows[] = {
        { "SURCHARGE_METHOD",    "TPA",       "TPA"       },
        { "TPA_CELERITY",        "250.0",     "250"       },
        { "FV_PRESSURE_CLOSURE", "TPA",       "TPA"       },
        { "UNSTEADY_FRICTION",   "VITKOVSKY", "VITKOVSKY" },
        { "UF_K3",               "0.010",     "0.01"      },
        { "REPORT_SIGNED_HEADS", "YES",       "YES"       },
        // The default-restoring directions too — a one-way table would pass
        // a setter that can only ever turn things on.
        { "SURCHARGE_METHOD",    "EXTRAN",    "EXTRAN"    },
        { "FV_PRESSURE_CLOSURE", "SLOT",      "SLOT"      },
        { "UNSTEADY_FRICTION",   "NONE",      "NONE"      },
        { "REPORT_SIGNED_HEADS", "NO",        "NO"        },
    };
    for (const auto &r : rows) {
        QVERIFY2(swmm_options_set(e, r.key, r.set) == 0, r.key);
        const QString got = getOptionString(e, r.key);
        bool gotNum = false, expNum = false;
        const double g = got.toDouble(&gotNum);
        const double x = QString::fromLatin1(r.expect).toDouble(&expNum);
        if (gotNum && expNum)
            QCOMPARE(g, x);
        else
            QCOMPARE(got, QString::fromLatin1(r.expect));
    }

    swmm_engine_destroy(e);
}

void TestOptionsHydrationContract::mixedFlowOptions_rejectBadEnumTokens()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e != nullptr);

    // Enum keys must reject unknown tokens (SWMM_ERR_BADPARAM) so a typo
    // surfaces through a failed setOption() rather than a silently lost
    // edit. REPORT_SIGNED_HEADS is deliberately absent: it takes the
    // engine's lenient bool grammar and never rejects.
    QVERIFY(swmm_options_set(e, "SURCHARGE_METHOD",    "PIPE")    != 0);
    QVERIFY(swmm_options_set(e, "UNSTEADY_FRICTION",   "BRUNONE") != 0);
    // EXTRAN is a valid SURCHARGE_METHOD token but not a closure — the
    // discriminating typo for this key.
    QVERIFY(swmm_options_set(e, "FV_PRESSURE_CLOSURE", "EXTRAN")  != 0);

    // ...and a rejected set leaves the previous value untouched.
    QCOMPARE(swmm_options_set(e, "SURCHARGE_METHOD", "TPA"), 0);
    QVERIFY(swmm_options_set(e, "SURCHARGE_METHOD", "PIPE") != 0);
    QCOMPARE(getOptionString(e, "SURCHARGE_METHOD"), QStringLiteral("TPA"));

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
