/*!
 * \file   test_ioportabilitynormalizer.cpp
 * \brief  Slice IO-10 — pre-save normalizer pre-flight checks.
 *
 * \details Stands up a real engine (via the public C-API), plants a few
 *          scalar external-file slots, and asserts the normalizer's
 *          preview / warning output is correct.
 *
 *          Cases:
 *            - Empty engine → empty result.
 *            - INP preview: slot under destination → relative form;
 *              slot on different root → absolute + cross_volume warning.
 *            - GPKG pre-flight: USE-direction slot with missing file
 *              produces a warning; SAVE-direction slot with missing
 *              file does not.
 */

#include "project/ioportabilitynormalizer.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

using openswmmvis::project::IoPortabilityNormalizer;
using openswmmvis::project::PreflightResult;

class TestIoPortabilityNormalizer : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void emptyEngineProducesEmptyResult();
    void inpPreviewProducesRelativePathForSlotUnderDestination();
    void inpPreviewFlagsCrossVolumeAsWarning();
    void gpkgPreflightWarnsOnMissingUseFile();
    void gpkgPreflightSilentOnMissingSaveFile();

private:
    SWMM_Engine engine_ = nullptr;
};

void TestIoPortabilityNormalizer::init()
{
    engine_ = swmm_engine_new();
    QVERIFY(engine_ != nullptr);
}

void TestIoPortabilityNormalizer::cleanup()
{
    if (engine_) swmm_engine_destroy(engine_);
    engine_ = nullptr;
}

void TestIoPortabilityNormalizer::emptyEngineProducesEmptyResult()
{
    auto r = IoPortabilityNormalizer::preflightInpSave(engine_, "/tmp/x.inp");
    QCOMPARE(r.slotPreviews.size(),    0);
    QCOMPARE(r.warnings.size(), 0);
}

void TestIoPortabilityNormalizer::inpPreviewProducesRelativePathForSlotUnderDestination()
{
    // Plant rainfall slot under /tmp/proj/data/rain.dat; destination
    // is /tmp/proj/model.inp → preview should be "data/rain.dat".
    QCOMPARE(swmm_file_path_set(engine_, SWMM_FILE_RAINFALL, nullptr,
                                  "/tmp/proj/data/rain.dat"), SWMM_OK);

    auto r = IoPortabilityNormalizer::preflightInpSave(
        engine_, "/tmp/proj/model.inp");
    QCOMPARE(r.slotPreviews.size(), 1);
    QCOMPARE(r.slotPreviews[0].role_label,       QStringLiteral("RAINFALL"));
    QCOMPARE(r.slotPreviews[0].original,         QStringLiteral("/tmp/proj/data/rain.dat"));
    QCOMPARE(r.slotPreviews[0].preview_relative, QStringLiteral("data/rain.dat"));
    QVERIFY(!r.slotPreviews[0].crosses_volume);
    QVERIFY(r.warnings.isEmpty());
}

void TestIoPortabilityNormalizer::inpPreviewFlagsCrossVolumeAsWarning()
{
    // Windows drive-letter style. On POSIX QDir::relativeFilePath happily
    // produces a long "../..." for any pair of POSIX-absolute paths, so
    // the cross-volume warning only triggers with paths the platform
    // treats as different roots. We use a drive-letter input to force it.
    QCOMPARE(swmm_file_path_set(engine_, SWMM_FILE_RAINFALL, nullptr,
                                  "D:/data/rain.dat"), SWMM_OK);

    auto r = IoPortabilityNormalizer::preflightInpSave(
        engine_, "/tmp/proj/model.inp");
#ifdef Q_OS_WIN
    QCOMPARE(r.slotPreviews.size(), 1);
    QVERIFY(r.slotPreviews[0].crosses_volume);
    QVERIFY(!r.warnings.isEmpty());
#else
    // On POSIX the drive-letter token is just a relative path that
    // happens to start with "D:". QDir treats it as relative. We accept
    // either outcome — the platform-specific cross-volume case is
    // verified on Windows CI.
    QCOMPARE(r.slotPreviews.size(), 1);
#endif
}

void TestIoPortabilityNormalizer::gpkgPreflightWarnsOnMissingUseFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // USE-direction INFLOWS slot pointing at a path that does not exist.
    const QString ghost = tmp.path() + "/ghost.txt";
    QCOMPARE(swmm_file_path_set(engine_, SWMM_FILE_INFLOWS, nullptr,
                                  ghost.toUtf8().constData()), SWMM_OK);

    auto r = IoPortabilityNormalizer::preflightGpkgSave(
        engine_, tmp.path() + "/proj.gpkg");
    QCOMPARE(r.slotPreviews.size(), 1);
    QVERIFY(r.slotPreviews[0].file_missing);
    QVERIFY(!r.warnings.isEmpty());
    QVERIFY(r.warnings.first().contains("INFLOWS"));
}

void TestIoPortabilityNormalizer::gpkgPreflightSilentOnMissingSaveFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // OUTFLOWS is SAVE-direction; a missing file at save-time is the
    // expected state (the engine populates it after the run).
    const QString ghost = tmp.path() + "/ghost_out.txt";
    QCOMPARE(swmm_file_path_set(engine_, SWMM_FILE_OUTFLOWS, nullptr,
                                  ghost.toUtf8().constData()), SWMM_OK);

    auto r = IoPortabilityNormalizer::preflightGpkgSave(
        engine_, tmp.path() + "/proj.gpkg");
    QCOMPARE(r.slotPreviews.size(), 1);
    QVERIFY(!r.slotPreviews[0].file_missing);
    QVERIFY(r.warnings.isEmpty());
}

QTEST_MAIN(TestIoPortabilityNormalizer)
#include "test_ioportabilitynormalizer.moc"
