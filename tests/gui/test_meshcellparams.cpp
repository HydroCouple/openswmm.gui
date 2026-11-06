/**
 * @file      test_meshcellparams.cpp
 * @brief     Tests for meshcellparams.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * The per-cell 2D parameter registry: the table every editing surface reads
 * (toolbar selector, Cell Data dialog target, property adapter labels), the
 * NaN-unset value convention, and the "engine support pending" gate that keeps
 * the groundwater keys visible but inert.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QtTest>

#include "mesh/meshcellparams.h"
#include "mesh/meshresult.h"

#include <cmath>

using namespace mesh;

class TestMeshCellParams : public QObject
{
    Q_OBJECT

private:
    /*! Two triangles: [0] fully set, [1] left unset (the NaN sentinel the
     *  reader leaves when the INP column is absent). */
    /**
     * @brief Construct owned two-triangle cell-parameter fixture with explicit and unset
     * attributes.
     * @return ok mesh with first triangle n.025/depth.4 and second NaN defaults; three vertices
     * in arbitrary planar/project-height units. Allocates local vectors, no engine or shared
     * layer state.
     */
    static MeshResult sampleMesh()
    {
        MeshResult m;
        m.vertices.append({QPointF(0, 0), 1.0, 0, ""});
        m.vertices.append({QPointF(10, 0), 1.5, 0, ""});
        m.vertices.append({QPointF(10, 10), 2.0, 0, ""});
        MeshTriangle t0;
        t0.v0 = 0;
        t0.v1 = 1;
        t0.v2 = 2;
        t0.mannings = 0.025;
        t0.initDepth = 0.4;
        MeshTriangle t1;
        t1.v0 = 0;
        t1.v1 = 2;
        t1.v2 = 1; // mannings/depth NaN
        m.triangles.append(t0);
        m.triangles.append(t1);
        m.ok = true;
        return m;
    }

private slots:
    /*! The two file-backed parameters are live and come first, so the toolbar
     *  and dialog open on a usable entry. */
    void registry_liveParametersAreFirstAndEnabled()
    {
        const QVector<CellParamSpec> &specs = cellParamSpecs();
        QVERIFY(specs.size() >= 2);
        QCOMPARE(specs[0].key, QByteArray("mannings"));
        QCOMPARE(specs[1].key, QByteArray("initDepth"));
        QVERIFY(specs[0].enabled);
        QVERIFY(specs[1].enabled);
    }

    /*! Groundwater keys are advertised but refuse to be written until the
     *  engine carries [2D_AQUIFER]. */
    void registry_groundwaterKeysArePendingWithReason()
    {
        int pending = 0;
        for (const CellParamSpec &s : cellParamSpecs()) {
            if (s.enabled || !s.key.startsWith("gw."))
                continue;
            ++pending;
            QVERIFY2(s.key.startsWith("gw."), s.key.constData());
            QVERIFY2(!s.tooltip.isEmpty(), "a disabled entry must explain why it is disabled");
        }
        QVERIFY2(pending >= 5, "the draft [2D_AQUIFER] parameter set");
    }

    /**
     * @brief Verify all advertised snow cell parameters are pending with explanation and no
     * stored scalar value.
     * @note Checks pack/forest/drift/qg/aep/slope/aspect registry keys disabled, tooltip
     * nonempty and cellParamValue NaN on fixture. Tests advertised UI gating, not physical snow
     * execution.
     */
    void snowParametersAreVisibleButPending()
    {
        for (const char *key : {"snow.pack", "snow.forestCover", "snow.drift", "snow.qg",
                                "snow.aep", "snow.slope", "snow.aspect"}) {
            const auto *s = cellParamSpec(key);
            QVERIFY(s);
            QVERIFY(!s->enabled);
            QVERIFY(!s->tooltip.isEmpty());
            QVERIFY(std::isnan(cellParamValue(sampleMesh(), 0, key)));
        }
    }

    /**
     * @brief Verify known initial-depth key resolves length metadata and unknown key returns
     * null.
     * @note Exact QByteArray identity and lengthUnit flag checked; borrowed registry pointer
     * remains registry-owned, no value writes or engine/GUI lifecycle changes.
     */
    void registry_lookupByKey()
    {
        const CellParamSpec *s = cellParamSpec("initDepth");
        QVERIFY(s);
        QCOMPARE(s->key, QByteArray("initDepth"));
        QVERIFY(s->lengthUnit);
        QCOMPARE(cellParamSpec("nope"), nullptr);
    }

    /*! Length-valued parameters carry the project unit; dimensionless ones
     *  never do — this is what replaced the hardcoded "(m)". */
    void labels_lengthParametersCarryTheProjectUnit()
    {
        QCOMPARE(cellParamLabel("initDepth", QStringLiteral("ft")),
                 QStringLiteral("Initial Depth (ft)"));
        QCOMPARE(cellParamLabel("initDepth", QStringLiteral("m")),
                 QStringLiteral("Initial Depth (m)"));
        // Manning's n is dimensionless — no suffix, whatever the unit system.
        QCOMPARE(cellParamLabel("mannings", QStringLiteral("ft")),
                 cellParamLabel("mannings", QStringLiteral("m")));
        QVERIFY(!cellParamLabel("mannings", QStringLiteral("ft")).contains(QStringLiteral("ft")));
    }

    /*! Stored values come back as authored; an unset attribute reports NaN so
     *  callers can tell "unset" from "explicitly zero" and substitute the
     *  spec default for display. */
    void values_readStoredAndReportUnsetAsNaN()
    {
        const MeshResult m = sampleMesh();
        QCOMPARE(cellParamValue(m, 0, "mannings"), 0.025);
        QCOMPARE(cellParamValue(m, 0, "initDepth"), 0.4);
        QVERIFY(std::isnan(cellParamValue(m, 1, "mannings")));
        QVERIFY(std::isnan(cellParamValue(m, 1, "initDepth")));
    }

    /**
     * @brief Verify invalid cell indices, unknown keys and pending groundwater storage yield
     * NaN.
     * @note Uses-1,99 and nonexistent key against owned two-cell fixture; unset result must stay
     * distinguishable from explicit 0. No engine/model mutation, QtTest assertions only.
     */
    void values_outOfRangeAndUnknownKeysAreNaN()
    {
        const MeshResult m = sampleMesh();
        QVERIFY(std::isnan(cellParamValue(m, -1, "mannings")));
        QVERIFY(std::isnan(cellParamValue(m, 99, "mannings")));
        QVERIFY(std::isnan(cellParamValue(m, 0, "nope")));
        // A pending groundwater key has no storage yet.
        QVERIFY(std::isnan(cellParamValue(m, 0, "gw.Ks")));
    }

    /*! Editor configuration must be usable as-is: a default inside the range,
     *  a positive step, and enough decimals to express the step. */
    void specs_editorConfigurationIsSelfConsistent()
    {
        for (const CellParamSpec &s : cellParamSpecs()) {
            QVERIFY2(s.min < s.max, s.key.constData());
            QVERIFY2(s.defaultValue >= s.min && s.defaultValue <= s.max, s.key.constData());
            QVERIFY2(s.step > 0.0, s.key.constData());
            QVERIFY2(s.decimals >= 0 && s.decimals <= 10, s.key.constData());
            QVERIFY2(!s.label.isEmpty(), s.key.constData());
        }
    }
};

QTEST_MAIN(TestMeshCellParams)
#include "test_meshcellparams.moc"
