/*!
 * \file   test_simoptions_persistence_contract.cpp
 * \brief  Pins the full disk round-trip for the [OPTIONS] keys whose broken
 *         C-API surface made Simulation Options dialog edits revert on
 *         reload (2026-08-06 persistence fixes).
 *
 * The dialog persists nothing itself: writeToEngine() mutates the in-memory
 * engine via swmm_options_set and File→Save serializes with the built-in
 * writer (swmm_model_write == write_with_plugin(NULL)). This test walks that
 * exact path on an opened model:
 *
 *   open → set (MINIMUM_STEP / LAT+SYS_FLOW_TOL / RULE_STEP / REPORT_STEP /
 *   THREADS) → swmm_model_write → reopen → get reads the same values.
 *
 * Engine-ABI only (no dialog header — keeps the leaf test clear of the
 * AUTOMOC → OGR/GDAL cascade). Mirrors test_2d_vfr_options_contract.
 */

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTest>

namespace {

QString dataDir()
{
    // Provided by the CTest ENVIRONMENT in tests/gui/CMakeLists.txt.
    //
    // This used to append "/output_simstatus2derr" and read mini_2d.inp from
    // there. That directory is test_simstatus_2derr's scratch output dir: it
    // is gitignored, and the .inp only exists once that test (which runs
    // later — #158 vs this one's #117) has written it inline. So the fixture
    // was present only on machines that had already run the full suite, and
    // absent on every fresh clone, i.e. always in CI. Same trap, and same
    // resolution, as tests/unit/test_rptparser.cpp's kTwoDFixture: read a
    // tracked fixture from the test data dir instead.
    const QByteArray env = qgetenv("SWMMVIS_GUI_TEST_DATA");
    return QString::fromUtf8(env);
}

// Reviewable artifact directory under the test's working dir (the build
// tree), not a hidden system temp folder.
QString artifactDir()
{
    const QString d = QDir::currentPath() + "/test_simoptions_persistence_artifacts";
    QDir().mkpath(d);
    return d;
}

// The exact call the dialog's getOption() makes for one standard key.
QString getOpt(SWMM_Engine e, const char *key)
{
    char buf[256] = {};
    if (swmm_options_get(e, key, buf, sizeof(buf)) != 0) return {};
    return QString::fromUtf8(buf).trimmed();
}

} // namespace

class TestSimOptionsPersistenceContract : public QObject
{
    Q_OBJECT
private slots:
    void setThenSaveReopenRoundTrips();
};

void TestSimOptionsPersistenceContract::setThenSaveReopenRoundTrips()
{
    const QString inp = dataDir() + "/mini_2d.inp";
    QVERIFY2(QFile::exists(inp), qPrintable(inp));

    SWMM_Engine e = swmm_engine_create();
    QVERIFY(e != nullptr);
    const QString rpt = artifactDir() + "/simoptions_contract.rpt";
    QCOMPARE(swmm_engine_open(e, inp.toUtf8().constData(),
                              rpt.toUtf8().constData(), nullptr, nullptr), 0);

    // Exactly what writeToEngine() sends for each key.
    QCOMPARE(swmm_options_set(e, "MINIMUM_STEP", "2.000"),    0);
    QCOMPARE(swmm_options_set(e, "LAT_FLOW_TOL", "3.00"),     0);
    QCOMPARE(swmm_options_set(e, "SYS_FLOW_TOL", "3.00"),     0);
    QCOMPARE(swmm_options_set(e, "RULE_STEP",    "172800"),   0);
    QCOMPARE(swmm_options_set(e, "REPORT_STEP",  "600"),      0);
    QCOMPARE(swmm_options_set(e, "THREADS",      "8"),        0);

    const QString outInp = artifactDir() + "/simoptions_contract_saved.inp";
    QCOMPARE(swmm_model_write(e, outInp.toUtf8().constData()), 0);
    swmm_engine_close(e);
    swmm_engine_destroy(e);

    // The .inp surface must carry the keys (percent tolerances, seconds or
    // clock-form steps).
    QFile f(outInp);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(f.readAll());
    QVERIFY2(text.contains(QStringLiteral("MINIMUM_STEP")), "MINIMUM_STEP missing from saved .inp");
    QVERIFY2(text.contains(QStringLiteral("LAT_FLOW_TOL")), "LAT_FLOW_TOL missing from saved .inp");
    QVERIFY2(text.contains(QStringLiteral("RULE_STEP")),    "RULE_STEP missing from saved .inp");

    // Reopen the saved model — the same path SimulationRunner and a project
    // reload take — and confirm every value survived the disk round-trip.
    SWMM_Engine e2 = swmm_engine_create();
    QVERIFY(e2 != nullptr);
    const QString rpt2 = artifactDir() + "/simoptions_contract_reopen.rpt";
    QCOMPARE(swmm_engine_open(e2, outInp.toUtf8().constData(),
                              rpt2.toUtf8().constData(), nullptr, nullptr), 0);

    QCOMPARE(getOpt(e2, "MINIMUM_STEP").toDouble(), 2.0);
    QCOMPARE(getOpt(e2, "LAT_FLOW_TOL").toDouble(), 3.0);   // percent
    QCOMPARE(getOpt(e2, "SYS_FLOW_TOL").toDouble(), 3.0);   // percent
    QCOMPARE(getOpt(e2, "RULE_STEP").toDouble(),    172800.0);
    QCOMPARE(getOpt(e2, "REPORT_STEP").toDouble(),  600.0);
    QCOMPARE(getOpt(e2, "THREADS"),                 QStringLiteral("8"));

    swmm_engine_close(e2);
    swmm_engine_destroy(e2);
}

QTEST_MAIN(TestSimOptionsPersistenceContract)
#include "test_simoptions_persistence_contract.moc"
