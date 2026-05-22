/*!
 * \file   test_profilenetworkadapter_model.cpp
 * \brief  Regression guard for the link-offset normalisation contract that
 *         `profilenetworkadapter_model.cpp` relies on.
 *
 * What is being pinned
 * --------------------
 * After Slice BC Phase 8.7.2, the GUI's profile pipeline trusts that
 * `swmm_link_get_offset_up/dn()` ALWAYS returns the conduit-end offset as
 * **depth above the connecting node invert**, regardless of whether the
 * source .inp declared `LINK_OFFSETS DEPTH` or `LINK_OFFSETS ELEVATION`.
 * `PostParseResolver` in the engine normalises ELEVATION-mode input to
 * DEPTH at parse time (see openswmm.engine
 * src/engine/input/PostParseResolver.cpp lines 1013–1022 for conduits and
 * 463–470 for orifices), so the GUI no longer applies any second
 * conversion.  Before this fix the GUI double-converted, producing
 * wildly-wrong (typically large-negative) offsets whenever the source
 * .inp used ELEVATION mode.
 *
 * Test strategy
 * -------------
 * Two minimal .inp models describe the same physical geometry — a single
 * 100-ft conduit between J1 (invert 100) and J2 (invert 95), with an
 * upstream offset of 0.5 ft and a downstream offset of 1.5 ft.  One
 * model declares the offsets in DEPTH mode (raw values 0.5 / 1.5), the
 * other in ELEVATION mode (absolute end inverts 100.5 / 96.5).  Loaded
 * into the engine and queried via `swmm_link_get_offset_up/dn`, both
 * MUST return identical depths (0.5 / 1.5).  Any future regression in
 * the engine that re-introduces mode-leakage at the ABI surface fails
 * this test.
 *
 * The .inp fixtures are emitted to `SWMMVIS_GUI_TEST_DATA/profile_offset/`
 * (under tests/gui/data/profile_offset/ at the source tree) so a reader
 * can inspect them after a run — per the workspace's CLAUDE.md
 * "Transparent File IO" rule.
 */

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QTest>
#include <QTextStream>

namespace {

// Fixture content — both models describe the same physical conduit.
//
// J1 invert = 100 ft, J2 invert = 95 ft, length = 100 ft.
// Conduit C1: upstream end at elev 100.5 (depth 0.5 above J1),
//             downstream end at elev 96.5 (depth 1.5 above J2).
// Depth mode raw input  : 0.5 / 1.5
// Elev mode raw input   : 100.5 / 96.5
//
// Everything else is reduced to the minimum a SWMM .inp parser accepts.
constexpr const char *kDepthInp =
    "[TITLE]\n"
    "Link-offset contract - DEPTH mode\n\n"
    "[OPTIONS]\n"
    "FLOW_UNITS       CFS\n"
    "INFILTRATION     HORTON\n"
    "FLOW_ROUTING     DYNWAVE\n"
    "LINK_OFFSETS     DEPTH\n"
    "MIN_SLOPE        0\n"
    "START_DATE       01/01/2026\n"
    "START_TIME       00:00:00\n"
    "REPORT_START_DATE 01/01/2026\n"
    "REPORT_START_TIME 00:00:00\n"
    "END_DATE         01/01/2026\n"
    "END_TIME         00:30:00\n"
    "REPORT_STEP      00:01:00\n"
    "ROUTING_STEP     00:00:30\n\n"
    "[JUNCTIONS]\n"
    ";;Name    Elev    MaxDepth    InitDepth    SurDepth    Aponded\n"
    "J1        100.0   10.0        0            0           0\n\n"
    "[OUTFALLS]\n"
    ";;Name    Elev    Type    StageData    Gated\n"
    "J2        95.0    FREE                  NO\n\n"
    "[CONDUITS]\n"
    ";;Name    From    To    Length    Roughness    InOffset    OutOffset    InitFlow    MaxFlow\n"
    "C1        J1      J2    100.0     0.013        0.5         1.5          0           0\n\n"
    "[XSECTIONS]\n"
    ";;Link    Shape       Geom1    Geom2    Geom3    Geom4    Barrels\n"
    "C1        CIRCULAR    1.0      0        0        0        1\n";

constexpr const char *kElevInp =
    "[TITLE]\n"
    "Link-offset contract - ELEVATION mode\n\n"
    "[OPTIONS]\n"
    "FLOW_UNITS       CFS\n"
    "INFILTRATION     HORTON\n"
    "FLOW_ROUTING     DYNWAVE\n"
    "LINK_OFFSETS     ELEVATION\n"
    "MIN_SLOPE        0\n"
    "START_DATE       01/01/2026\n"
    "START_TIME       00:00:00\n"
    "REPORT_START_DATE 01/01/2026\n"
    "REPORT_START_TIME 00:00:00\n"
    "END_DATE         01/01/2026\n"
    "END_TIME         00:30:00\n"
    "REPORT_STEP      00:01:00\n"
    "ROUTING_STEP     00:00:30\n\n"
    "[JUNCTIONS]\n"
    ";;Name    Elev    MaxDepth    InitDepth    SurDepth    Aponded\n"
    "J1        100.0   10.0        0            0           0\n\n"
    "[OUTFALLS]\n"
    ";;Name    Elev    Type    StageData    Gated\n"
    "J2        95.0    FREE                  NO\n\n"
    "[CONDUITS]\n"
    ";;Name    From    To    Length    Roughness    InOffset    OutOffset    InitFlow    MaxFlow\n"
    "C1        J1      J2    100.0     0.013        100.5       96.5         0           0\n\n"
    "[XSECTIONS]\n"
    ";;Link    Shape       Geom1    Geom2    Geom3    Geom4    Barrels\n"
    "C1        CIRCULAR    1.0      0        0        0        1\n";

QString writeFixture(const QString &fixtureDir, const QString &name, const char *contents)
{
    QDir().mkpath(fixtureDir);
    const QString path = fixtureDir + QStringLiteral("/") + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return {};
    QTextStream s(&f);
    s << contents;
    return path;
}

struct LoadedOffsets {
    double offsetUp = 0.0;
    double offsetDn = 0.0;
    int    linkType = -1;
};

bool loadAndQuery(const QString &inpPath, LoadedOffsets *out)
{
    // `swmm_engine_create` returns a CREATED-state engine suitable for
    // `swmm_engine_open` against a file (vs. `swmm_engine_new` which yields
    // BUILDING state for programmatic construction).
    SWMM_Engine e = swmm_engine_create();
    if (e == nullptr) return false;

    // .rpt / .out land next to the .inp so a reader can inspect them after
    // the run; binary output is not required for the contract assertion but
    // the engine demands a path.
    const QFileInfo inpInfo(inpPath);
    const QString stem = inpInfo.absolutePath() + QStringLiteral("/")
                         + inpInfo.completeBaseName();
    const QByteArray inpUtf8 = inpPath.toUtf8();
    const QByteArray rptUtf8 = (stem + QStringLiteral(".rpt")).toUtf8();
    const QByteArray outUtf8 = (stem + QStringLiteral(".out")).toUtf8();

    const int rc = swmm_engine_open(e,
                                    inpUtf8.constData(),
                                    rptUtf8.constData(),
                                    outUtf8.constData(),
                                    nullptr);
    if (rc != 0) {
        const int lastErr = swmm_get_last_error(e);
        qWarning("swmm_engine_open(%s) failed: rc=%d last_error=%d",
                 qUtf8Printable(inpPath), rc, lastErr);
        swmm_engine_destroy(e);
        return false;
    }

    // Single conduit, zero-indexed.  Validate it before reading offsets so a
    // failure here points at fixture corruption, not at the contract.
    const int nLinks = swmm_link_count(e);
    if (nLinks != 1) {
        swmm_engine_destroy(e);
        return false;
    }

    swmm_link_get_type(e, 0, &out->linkType);
    swmm_link_get_offset_up(e, 0, &out->offsetUp);
    swmm_link_get_offset_dn(e, 0, &out->offsetDn);

    swmm_engine_destroy(e);
    return true;
}

} // namespace


class TestProfileNetworkAdapterModel : public QObject
{
    Q_OBJECT

private:
    QString fixtureDir() const
    {
        // Prefer the env-injected SWMMVIS_GUI_TEST_DATA (set by add_swmmvis_gui_test);
        // fall back to the cwd so a manual run still produces visible artifacts.
        const QByteArray env = qgetenv("SWMMVIS_GUI_TEST_DATA");
        const QString base = env.isEmpty()
                                 ? QDir::currentPath()
                                 : QString::fromLocal8Bit(env);
        return base + QStringLiteral("/profile_offset");
    }

private slots:
    /*! Both modes must round-trip through the engine to the **same DEPTH**
     *  numbers on `swmm_link_get_offset_up/dn`.  This pins the contract
     *  that profilenetworkadapter_model.cpp relies on after the fix that
     *  removed `linkOffsetsInElevationMode()`. */
    void offsets_are_normalised_to_depth_in_both_modes()
    {
        const QString dir = fixtureDir();
        const QString depthInp = writeFixture(dir, QStringLiteral("depth_mode.inp"), kDepthInp);
        const QString elevInp  = writeFixture(dir, QStringLiteral("elevation_mode.inp"), kElevInp);
        QVERIFY2(!depthInp.isEmpty(), "Could not write depth_mode.inp fixture");
        QVERIFY2(!elevInp.isEmpty(),  "Could not write elevation_mode.inp fixture");

        LoadedOffsets depthVals;
        LoadedOffsets elevVals;
        QVERIFY2(loadAndQuery(depthInp, &depthVals),
                 "Engine failed to open the DEPTH-mode fixture");
        QVERIFY2(loadAndQuery(elevInp,  &elevVals),
                 "Engine failed to open the ELEVATION-mode fixture");

        // Engine reports both links as type 0 (conduit).
        QCOMPARE(depthVals.linkType, 0);
        QCOMPARE(elevVals.linkType,  0);

        // Both fixtures describe the same physical conduit; after engine
        // normalisation the API surface must return the same depths.
        QCOMPARE(depthVals.offsetUp, 0.5);
        QCOMPARE(depthVals.offsetDn, 1.5);
        QCOMPARE(elevVals.offsetUp,  0.5);
        QCOMPARE(elevVals.offsetDn,  1.5);
    }

    /*! Sanity guard: the fixtures must actually exercise the offset path,
     *  i.e. non-zero offsets that differ at the two ends.  If a future
     *  refactor accidentally writes zeros to the fixtures this case fails
     *  fast and makes the diagnosis obvious. */
    void fixtures_exercise_nontrivial_offsets()
    {
        const QString dir = fixtureDir();
        const QString depthInp = writeFixture(dir, QStringLiteral("depth_mode.inp"), kDepthInp);
        LoadedOffsets v;
        QVERIFY(loadAndQuery(depthInp, &v));
        QVERIFY(v.offsetUp > 0.0);
        QVERIFY(v.offsetDn > 0.0);
        QVERIFY(v.offsetUp != v.offsetDn);
    }
};

QTEST_MAIN(TestProfileNetworkAdapterModel)
#include "test_profilenetworkadapter_model.moc"
