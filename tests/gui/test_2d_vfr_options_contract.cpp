/*!
 * \file   test_2d_vfr_options_contract.cpp
 * \brief  Pins the engine [2D_OPTIONS] ABI that SimulationOptionsDialog's VFR
 *         controls (CELL_CLOSURE / FACE_RECONSTRUCTION / VFR_MIN_WET_FRAC) rely
 *         on.
 *
 * The dialog's read2DFromEngine()/write2DToEngine() reach the three VFR keys
 * only through the generic swmm_options_get_ext / swmm_options_set_ext surface
 * and persist them on Save via the built-in .inp writer (swmm_model_write).
 * This test exercises that exact contract end-to-end on an opened 2D model:
 *
 *   open (FLAT default) → get_ext reads defaults → set_ext to VFR → Save
 *   (swmm_model_write) → reopen → get_ext reads back VFR.
 *
 * If a future engine change drops one of these keys from is2DOptionKey /
 * format2DOptionValue / the writer, the new GUI combos would silently stop
 * persisting — this test fails first. Mirrors the lightweight engine-ABI
 * linkage of test_options_hydration_contract (no MainWindow / dialog).
 */

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTest>

#include <cstring>

namespace {

QString dataDir()
{
    // Provided by the CTest ENVIRONMENT in tests/gui/CMakeLists.txt.
    const QByteArray env = qgetenv("SWMMVIS_GUI_TEST_DATA");
    return QString::fromUtf8(env) + "/output_simstatus2derr";
}

// The exact call the dialog's write2DToEngine() makes for one 2D key.
QString getExt(SWMM_Engine e, const char *key)
{
    char buf[256] = {};
    if (swmm_options_get_ext(e, key, buf, sizeof(buf)) != 0) return {};
    return QString::fromUtf8(buf).trimmed();
}

} // namespace

class Test2DVfrOptionsContract : public QObject
{
    Q_OBJECT
private slots:
    void defaultsReadFlatMean();
    void setExtThenSaveReopenRoundTrips();
    void faceReconIndependentOfCellClosure();
    void vfrMinWetFracRejectsOutOfRange();
    void backendRoundTripsAndRejectsUnknown();
};

// read2DFromEngine() hydrates the combos from these — an opened model with no
// explicit keys must report the FLAT/MEAN legacy defaults (VFR is opt-in: it is
// ~3–8× slower, so the default stays FLAT and VFR is selected only when the
// shoreline-artifact fix is wanted).
void Test2DVfrOptionsContract::defaultsReadFlatMean()
{
    const QString inp = dataDir() + "/mini_2d.inp";
    QVERIFY2(QFile::exists(inp), qPrintable(inp));

    SWMM_Engine e = swmm_engine_create();
    QVERIFY(e != nullptr);
    const QString rpt = QDir::tempPath() + "/vfr_contract_defaults.rpt";
    QCOMPARE(swmm_engine_open(e, inp.toUtf8().constData(),
                             rpt.toUtf8().constData(), nullptr, nullptr), 0);

    QCOMPARE(getExt(e, "CELL_CLOSURE"),        QStringLiteral("FLAT"));
    QCOMPARE(getExt(e, "FACE_RECONSTRUCTION"), QStringLiteral("MEAN"));
    // Default floor is 0.01; format uses %g.
    QCOMPARE(getExt(e, "VFR_MIN_WET_FRAC").toDouble(), 0.01);

    swmm_engine_close(e);
    swmm_engine_destroy(e);
}

// The full dialog Save path: mutate via set_ext (write2DToEngine), serialize
// with the built-in writer (swmm_model_write, == write_with_plugin(NULL)),
// reopen, and confirm the VFR selection survived a round-trip to disk.
void Test2DVfrOptionsContract::setExtThenSaveReopenRoundTrips()
{
    const QString inp = dataDir() + "/mini_2d.inp";
    SWMM_Engine e = swmm_engine_create();
    QVERIFY(e != nullptr);
    const QString rpt = QDir::tempPath() + "/vfr_contract_rt.rpt";
    QCOMPARE(swmm_engine_open(e, inp.toUtf8().constData(),
                             rpt.toUtf8().constData(), nullptr, nullptr), 0);

    // Exactly what write2DToEngine() does when the user picks VFR + VFR_FACE.
    QCOMPARE(swmm_options_set_ext(e, "CELL_CLOSURE",        "VFR"),      0);
    QCOMPARE(swmm_options_set_ext(e, "FACE_RECONSTRUCTION", "VFR_FACE"), 0);
    QCOMPARE(swmm_options_set_ext(e, "VFR_MIN_WET_FRAC",    "0.03"),     0);

    // Written to the reviewable build/test-tmp path, not a hidden temp file.
    const QString outInp = QDir::tempPath() + "/vfr_contract_saved.inp";
    QCOMPARE(swmm_model_write(e, outInp.toUtf8().constData()), 0);
    swmm_engine_close(e);
    swmm_engine_destroy(e);

    // Reopen the saved model — the run path (SimulationRunner) does exactly
    // this: swmm_engine_open on the just-written .inp.
    SWMM_Engine e2 = swmm_engine_create();
    const QString rpt2 = QDir::tempPath() + "/vfr_contract_rt2.rpt";
    QCOMPARE(swmm_engine_open(e2, outInp.toUtf8().constData(),
                             rpt2.toUtf8().constData(), nullptr, nullptr), 0);

    QCOMPARE(getExt(e2, "CELL_CLOSURE"),        QStringLiteral("VFR"));
    QCOMPARE(getExt(e2, "FACE_RECONSTRUCTION"), QStringLiteral("VFR_FACE"));
    QCOMPARE(getExt(e2, "VFR_MIN_WET_FRAC").toDouble(), 0.03);

    swmm_engine_close(e2);
    swmm_engine_destroy(e2);
}

// The dialog exposes the two combos independently; the engine must accept the
// face gate with the legacy flat cell closure (and vice-versa) so the A/B
// matrix the tooltips promise is real.
void Test2DVfrOptionsContract::faceReconIndependentOfCellClosure()
{
    const QString inp = dataDir() + "/mini_2d.inp";
    SWMM_Engine e = swmm_engine_create();
    const QString rpt = QDir::tempPath() + "/vfr_contract_indep.rpt";
    QCOMPARE(swmm_engine_open(e, inp.toUtf8().constData(),
                             rpt.toUtf8().constData(), nullptr, nullptr), 0);

    QCOMPARE(swmm_options_set_ext(e, "CELL_CLOSURE",        "FLAT"),     0);
    QCOMPARE(swmm_options_set_ext(e, "FACE_RECONSTRUCTION", "VFR_FACE"), 0);
    QCOMPARE(getExt(e, "CELL_CLOSURE"),        QStringLiteral("FLAT"));
    QCOMPARE(getExt(e, "FACE_RECONSTRUCTION"), QStringLiteral("VFR_FACE"));

    swmm_engine_close(e);
    swmm_engine_destroy(e);
}

// The spinbox clamps to (0, 0.5]; the engine must reject anything outside it
// rather than silently clobbering the field, so a bad hand-edit is caught.
void Test2DVfrOptionsContract::vfrMinWetFracRejectsOutOfRange()
{
    const QString inp = dataDir() + "/mini_2d.inp";
    SWMM_Engine e = swmm_engine_create();
    const QString rpt = QDir::tempPath() + "/vfr_contract_reject.rpt";
    QCOMPARE(swmm_engine_open(e, inp.toUtf8().constData(),
                             rpt.toUtf8().constData(), nullptr, nullptr), 0);

    // Set a valid value first, then attempt invalid ones; the field must not
    // be clobbered by the rejected writes (the intentional validate-into-local
    // behavior of parse2DOptionsLine).
    QCOMPARE(swmm_options_set_ext(e, "VFR_MIN_WET_FRAC", "0.02"), 0);
    QVERIFY(swmm_options_set_ext(e, "VFR_MIN_WET_FRAC", "0.0")  != 0);  // <= 0
    QVERIFY(swmm_options_set_ext(e, "VFR_MIN_WET_FRAC", "0.9")  != 0);  // > 0.5
    QCOMPARE(getExt(e, "VFR_MIN_WET_FRAC").toDouble(), 0.02);

    swmm_engine_close(e);
    swmm_engine_destroy(e);
}

QTEST_MAIN(Test2DVfrOptionsContract)


// The 2D tab's Backend combo persists through the same generic surface as the
// VFR keys: an opened model with no BACKEND line reads the engine default
// (AUTO), a set_ext survives Save → reopen, and an accelerator the engine does
// not know is refused (so a typo fails the write instead of silently running
// on AUTO).
void Test2DVfrOptionsContract::backendRoundTripsAndRejectsUnknown()
{
    const QString inp = dataDir() + "/mini_2d.inp";
    SWMM_Engine e = swmm_engine_create();
    QVERIFY(e != nullptr);
    const QString rpt = QDir::tempPath() + "/backend_contract_rt.rpt";
    QCOMPARE(swmm_engine_open(e, inp.toUtf8().constData(),
                             rpt.toUtf8().constData(), nullptr, nullptr), 0);

    QCOMPARE(getExt(e, "BACKEND"), QStringLiteral("AUTO"));
    QVERIFY(swmm_options_set_ext(e, "BACKEND", "METAL") != 0);
    QCOMPARE(getExt(e, "BACKEND"), QStringLiteral("AUTO"));   // untouched

    // Exactly what write2DToEngine() does when the user pins the CPU marcher.
    QCOMPARE(swmm_options_set_ext(e, "BACKEND", "CPU"), 0);
    QCOMPARE(getExt(e, "BACKEND"), QStringLiteral("CPU"));

    const QString outInp = QDir::tempPath() + "/backend_contract_saved.inp";
    QCOMPARE(swmm_model_write(e, outInp.toUtf8().constData()), 0);
    swmm_engine_close(e);
    swmm_engine_destroy(e);

    SWMM_Engine e2 = swmm_engine_create();
    const QString rpt2 = QDir::tempPath() + "/backend_contract_rt2.rpt";
    QCOMPARE(swmm_engine_open(e2, outInp.toUtf8().constData(),
                             rpt2.toUtf8().constData(), nullptr, nullptr), 0);
    QCOMPARE(getExt(e2, "BACKEND"), QStringLiteral("CPU"));
    swmm_engine_close(e2);
    swmm_engine_destroy(e2);
}

#include "test_2d_vfr_options_contract.moc"
