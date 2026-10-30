/**
 * @file      test_groundwaterassignment.cpp
 * @brief     Tests for groundwaterassignment.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "assignment/groundwaterassignment.h"
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <QtTest>
#include <limits>
using namespace openswmmvis::assignment;
/**
 *
 * @brief Scoped raw engine owner used without copying by assignment cases.
 *
 */
struct Engine {
    SWMM_Engine e = swmm_engine_new();
    /**
     *
     * @brief Destroy the fixture-owned engine handle at scope exit.
     * Handle must be null or still owned/live; copying this simple fixture would duplicate
     * raw ownership and is not used. No separate close/finalize cycle is performed.
     *
     */
    ~Engine() { swmm_engine_destroy(e); }
};
/**
 *
 * @brief App-less Qt fixture for candidate validation, conflict detection and aquifer undo.
 *
 */
class TestGroundwaterAssignment : public QObject
{
    Q_OBJECT
private:
    /**
     *
     * @brief Append the base aquifer row and copy its authored model snapshot.
     * @param[in,out] e Fixture-owned editable BUILDING engine. Adds default-scope row
     * with KS 0.1234567890123 in project rain-depth/hour, thickness five project length,
     * porosity 0.45, residual 0.1, alpha two per project length, then optional L=-0.75.
     * @return Owned snapshot, or empty when snapshot read fails. Initial mutator status
     * is ignored, so empty/failure is not a rollback guarantee; cases assert prerequisites
     * before dependent indexing. No engine ownership is transferred.
     *
     */
    AquiferSnapshot seed(Engine &e)
    {
        swmm_gw2d_row_add(e.e, 0, nullptr, -1, .1234567890123, 5, .45, .1, 2);
        swmm_gw2d_row_set_property(e.e, 0, "L", -.75);
        AquiferSnapshot s;
        QString error;
        if (!readAquiferSnapshot(e.e, &s, &error))
            return {};
        return s;
    }
    /**
     *
     * @brief Build a two-cell conductivity-assignment candidate from a copied base snapshot.
     * @param s Borrowed authored aquifer snapshot, copied as the expected before state.
     * @return Owned request for cell IDs 0/1 tagged A/B, target KS and values 2/3 in
     * project rain-depth/hour units. Uses ordinary non-skipping NoData policy. No engine
     * access, physical validation or assignment publication occurs in this helper.
     *
     */
    AquiferRequest request(const AquiferSnapshot &s)
    {
        AquiferRequest r;
        r.before = s;
        r.cells = {0, 1};
        r.cellTags = {"A", "B"};
        r.target = "KS";
        r.values = {2, 3};
        return r;
    }
private slots:
    /**
     *
     * @brief Verify process preview leaves the model unchanged, apply/undo restores authored
     * defaults and stale options refuse writes.
     * Also checks invalid zero step is rejected and unrelated GW_ET remains at its prior value.
     *
     */
    void processReviewIsAtomicAndRestoresDefaults()
    {
        Engine e;
        auto before = seed(e);
        auto p = previewAquiferProcesses(e.e, "NONE", "ONE_WAY", "30");
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QVERIFY(p.changed);
        QVERIFY(!p.forcing.isEmpty());
        QVERIFY(p.beforeEffective.contains("BOTH"));
        QVERIFY(p.afterEffective.contains("NONE"));
        QString error;
        AquiferSnapshot now;
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now, before);
        QVERIFY(applyAquiferProcesses(e.e, p.before, p.after, p.groundwater, &error));
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now, p.after);
        QVERIFY(applyAquiferProcesses(e.e, p.after, p.before, p.groundwater, &error));
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now, before);
        QVERIFY(!previewAquiferProcesses(e.e, "BOTH", "TWO_WAY", "0").error.isEmpty());
        QCOMPARE(swmm_gw2d_option_set(e.e, "SOIL_CHAR", "GARDNER"), SWMM_OK);
        QVERIFY(!applyAquiferProcesses(e.e, p.before, p.after, p.groundwater, &error));
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now.options.value("GW_ET"), before.options.value("GW_ET"));
    }
    /**
     *
     * @brief Change project flow-unit system after process preview and require stale preview
     * refusal without optional-default corruption.
     *
     */
    void processReviewRefusesAProjectUnitChange()
    {
        Engine e;
        seed(e);
        auto p = previewAquiferProcesses(e.e, "BOTH", "TWO_WAY", "30");
        QVERIFY(p.error.isEmpty());
        const auto other = p.before.options.value("PROJECT_FLOW_UNITS") == "CMS" ? "CFS" : "CMS";
        QCOMPARE(swmm_options_set(e.e, "FLOW_UNITS", other), SWMM_OK);
        QString error;
        QVERIFY(!applyAquiferProcesses(e.e, p.before, p.after, p.groundwater, &error));
        AquiferSnapshot now;
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now.options.value("WILTING_SUCTION"), QString("AUTO"));
    }
    /**
     *
     * @brief Cancel at the third poll and require all actionable assignment rows/values cleared
     * and model unchanged.
     * A noncancelling callback must be polled during successful two-cell preparation.
     *
     */
    void cancellationLeavesNoActionablePreview()
    {
        Engine e;
        auto s = seed(e);
        auto r = request(s);
        QString error;
        int polls = 0;
        auto p = previewAquiferAssignment(r, [&] { return ++polls == 3; });
        QCOMPARE(p.error, QString("Cancelled."));
        QVERIFY(p.appended.isEmpty());
        QVERIFY(p.cells.isEmpty());
        QVERIFY(p.oldValues.isEmpty());
        QVERIFY(p.newValues.isEmpty());
        AquiferSnapshot now;
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now, s);
        polls = 0;
        p = previewAquiferAssignment(r, [&] {
            ++polls;
            return false;
        });
        QVERIFY(p.error.isEmpty());
        QVERIFY(polls >= 3);
        QCOMPARE(p.appended.size(), 2);
    }
    /**
     *
     * @brief Require candidate KS replacement to inherit all independent authored material fields
     * and preserve exact before snapshot.
     *
     */
    void snapshotAndExactIndependentFields()
    {
        Engine e;
        auto s = seed(e);
        QCOMPARE(s.rows.size(), 1);
        auto p = previewAquiferAssignment(request(s));
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QCOMPARE(p.appended.size(), 2);
        QCOMPARE(p.appended[0].ks, 2.);
        QCOMPARE(p.appended[0].optional.value("L"), -.75);
        QCOMPARE(p.appended[0].thetaS, s.rows[0].thetaS);
        QCOMPARE(p.before, s);
    }
    /**
     *
     * @brief Reject an invalid per-cell porosity batch before apply and require the full model
     * snapshot unchanged.
     *
     */
    void completeValidationBeforeAnyWrite()
    {
        Engine e;
        auto s = seed(e);
        QVERIFY(!s.rows.isEmpty());
        auto r = request(s);
        r.target = "THETA_S";
        r.values = {.5, .05};
        auto p = previewAquiferAssignment(r);
        QVERIFY(!p.error.isEmpty());
        QVERIFY(p.appended.isEmpty());
        QString error;
        QVERIFY(!applyAquiferPreview(e.e, p, &error));
        AquiferSnapshot now;
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now, s);
    }
    /**
     *
     * @brief Require NaN samples to fail by default, then skip explicitly and avoid appending a
     * no-op unchanged cell.
     *
     */
    void noDataExplicitAndNoOp()
    {
        Engine e;
        auto s = seed(e);
        QVERIFY(!s.rows.isEmpty());
        auto r = request(s);
        r.values = {s.rows[0].ks, std::numeric_limits<double>::quiet_NaN()};
        QVERIFY(!previewAquiferAssignment(r).error.isEmpty());
        r.skipNoData = true;
        auto p = previewAquiferAssignment(r);
        QVERIFY(p.error.isEmpty());
        QVERIFY(p.appended.isEmpty());
        QCOMPARE(p.skippedCells, QVector<int>({1}));
    }
    /**
     *
     * @brief Apply/undo/reapply appended per-cell overrides and require stale-base mutation to
     * prevent undo without removing rows.
     *
     */
    void applyUndoRedoAndConflict()
    {
        Engine e;
        auto s = seed(e);
        QVERIFY(!s.rows.isEmpty());
        auto p = previewAquiferAssignment(request(s));
        QString error;
        QVERIFY2(applyAquiferPreview(e.e, p, &error), qPrintable(error));
        AquiferSnapshot now;
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now.rows.size(), 3);
        QCOMPARE(now.rows[0], s.rows[0]);
        QVERIFY(undoAquiferPreview(e.e, p, &error));
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now, s);
        QVERIFY(applyAquiferPreview(e.e, p, &error));
        swmm_gw2d_row_set_property(e.e, 0, "L", -.5);
        QVERIFY(!undoAquiferPreview(e.e, p, &error));
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now.rows.size(), 3);
    }
    /**
     *
     * @brief Inject failure before the fourth numbered write and require appended-row rollback to
     * the exact original snapshot.
     *
     */
    void failureRollsBackAppendedRows()
    {
        Engine e;
        auto s = seed(e);
        QVERIFY(!s.rows.isEmpty());
        auto p = previewAquiferAssignment(request(s));
        QString error;
        QVERIFY(!applyAquiferPreview(e.e, p, &error, [](int write) { return write != 4; }));
        AquiferSnapshot now;
        QVERIFY(readAquiferSnapshot(e.e, &now, &error));
        QCOMPARE(now, s);
    }
    /**
     *
     * @brief Check explicit-cell override takes precedence over tag/default inheritance while
     * other cells inherit the base row.
     * Switching soil-characteristic policy must still permit this candidate.
     *
     */
    void scopesOrderAndAmbiguousInheritance()
    {
        Engine e;
        auto s = seed(e);
        QVERIFY(!s.rows.isEmpty());
        swmm_gw2d_row_add(e.e, 1, "A", -1, 4, 6, .5, .2, 3);
        swmm_gw2d_row_add(e.e, 2, nullptr, 0, 7, 8, .6, .3, 4);
        QString error;
        QVERIFY(readAquiferSnapshot(e.e, &s, &error));
        auto r = request(s);
        r.target = "ZS";
        r.values = {9, 10};
        auto p = previewAquiferAssignment(r);
        QVERIFY(p.error.isEmpty());
        QCOMPARE(p.appended[0].ks, 7.);
        QCOMPARE(p.appended[1].ks, s.rows[0].ks);
        swmm_gw2d_option_set(e.e, "SOIL_CHAR", "VAN_GENUCHTEN");
        QVERIFY(readAquiferSnapshot(e.e, &r.before, &error));
        QVERIFY(previewAquiferAssignment(r).error.isEmpty());
    }
    /**
     *
     * @brief Require authored optional-default presence flags to survive preview/apply/undo.
     * A separately inherited default must retain its unset flag despite global policy changes.
     *
     */
    void explicitDefaultsAndInheritanceRemainDistinct()
    {
        Engine e;
        auto s = seed(e);
        QVERIFY(!s.rows.isEmpty());
        QCOMPARE(swmm_gw2d_row_set_property(e.e, 0, "SOIL_CHAR", 0), SWMM_OK);
        QCOMPARE(swmm_gw2d_row_set_property(e.e, 0, "CLOSURE", -1), SWMM_OK);
        QCOMPARE(swmm_gw2d_option_set(e.e, "SOIL_CHAR", "VAN_GENUCHTEN"), SWMM_OK);
        QString error;
        QVERIFY(readAquiferSnapshot(e.e, &s, &error));
        QCOMPARE(s.rows[0].optional.value("SOIL_CHAR_SET"), 1.);
        QCOMPARE(s.rows[0].optional.value("CLOSURE_SET"), 1.);
        auto p = previewAquiferAssignment(request(s));
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QVERIFY2(applyAquiferPreview(e.e, p, &error), qPrintable(error));
        double flag = 0;
        QCOMPARE(swmm_gw2d_row_get_property(e.e, 1, "SOIL_CHAR_SET", &flag), SWMM_OK);
        QCOMPARE(flag, 1.);
        QCOMPARE(swmm_gw2d_row_get_property(e.e, 1, "CLOSURE_SET", &flag), SWMM_OK);
        QCOMPARE(flag, 1.);
        QVERIFY2(undoAquiferPreview(e.e, p, &error), qPrintable(error));
        Engine inherited;
        auto before = seed(inherited);
        QCOMPARE(swmm_gw2d_option_set(inherited.e, "SOIL_CHAR", "VAN_GENUCHTEN"), SWMM_OK);
        QVERIFY(readAquiferSnapshot(inherited.e, &before, &error));
        QCOMPARE(before.rows[0].optional.value("SOIL_CHAR_SET"), 0.);
        p = previewAquiferAssignment(request(before));
        QVERIFY(p.error.isEmpty());
        QVERIFY(applyAquiferPreview(inherited.e, p, &error));
        QCOMPARE(swmm_gw2d_row_get_property(inherited.e, 1, "SOIL_CHAR_SET", &flag), SWMM_OK);
        QCOMPARE(flag, 0.);
    }
    /**
     *
     * @brief Build/timestamp one large explicit-cell preview and verify all expected rows/last
     * ID.
     * Cell count comes from SWMMVIS_ASSIGNMENT_BENCHMARK_CELLS, default 100000, bounded
     * to [1,1000000]. The body logs milliseconds but does not assert scaling or a speed
     * threshold.
     *
     */
    void explicitCellPreviewScalesLinearly()
    {
        const int count =
            qBound(1,
                   qEnvironmentVariableIntValue("SWMMVIS_ASSIGNMENT_BENCHMARK_CELLS")
                       ? qEnvironmentVariableIntValue("SWMMVIS_ASSIGNMENT_BENCHMARK_CELLS")
                       : 100000,
                   1000000);
        AquiferRequest r;
        r.target = "KS";
        r.before.options = {{"SOIL_CHAR", "RUSSO"}, {"CLOSURE", "AUTO"}};
        AquiferRow row;
        row.scope = 2;
        row.ks = 1;
        row.zs = 5;
        row.thetaS = .45;
        row.thetaR = .1;
        row.alpha = 2;
        row.optional = {{"PSI_B", .2},   {"LAMBDA", .4},       {"N", 1.6},        {"L", .5},
                        {"C_LOSS", 0},   {"HG0", -1},          {"M_LAYERS", -1},  {"SOIL_CHAR", 0},
                        {"CLOSURE", -1}, {"SOIL_CHAR_SET", 0}, {"CLOSURE_SET", 0}};
        r.before.rows.reserve(count);
        r.cells.reserve(count);
        r.values.reserve(count);
        r.cellTags.reserve(count);
        for (int i = 0; i < count; ++i) {
            row.cell = i;
            r.before.rows.append(row);
            r.cells.append(i);
            r.values.append(2);
            r.cellTags.append(QString());
        }
        QElapsedTimer timer;
        timer.start();
        const auto p = previewAquiferAssignment(r);
        qInfo() << "Explicit-cell preview" << count << "cells in" << timer.elapsed() << "ms";
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QCOMPARE(p.appended.size(), count);
        QCOMPARE(p.appended.last().cell, count - 1);
    }
    /**
     *
     * @brief Require FLOW target metadata to advertise signed range while remaining unsupported
     * with a nonempty explanation.
     *
     */
    void signedMetadataAndUnsupportedForcingGate()
    {
        auto targets = aquiferTargets();
        QVERIFY(!targets.isEmpty());
        bool found = false;
        for (const auto &t : targets)
            if (t.key == "FLOW") {
                found = true;
                QVERIFY(t.minimum < 0);
                QVERIFY(!t.supported);
                QVERIFY(!t.unavailableReason.isEmpty());
            }
        QVERIFY(found);
    }
};
QTEST_APPLESS_MAIN(TestGroundwaterAssignment)
#include "test_groundwaterassignment.moc"
