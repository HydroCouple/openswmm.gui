/**
 * @file      test_mesh2dvariableexport.cpp
 * @brief     Tests for mesh2dvariableexport.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/mesh2dvariableexport.h"
#include "layers/swmm2dresultslayer.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>
#include <filesystem>
#include <limits>

using namespace openswmmvis::io;

namespace
{
class VariableSource final : public IMesh2DSource
{
public:
    Mesh2DResultVariable variable;
    QString file;
    bool fail = false, corrupt = false, stale = false;
    int generation = 0;
    /**
     * @brief Initialize an in-memory two-frame concentration descriptor with CSV-hostile species
     * text.
     * @note Dataset Mesh2_face_species_conc, label Surface concentration, known ug/L units;
     * species includes comma, quotes and newline. Defaults retain no source file, no read
     * failure/corruption/staleness; constructor allocates text only, no file IO or native
     * engine.
     */
    VariableSource()
    {
        variable.dataset = "Mesh2_face_species_conc";
        variable.species = "N,\"special\"\nline";
        variable.label = "Surface concentration";
        variable.units = "ug/L";
        variable.unitsKnown = true;
        variable.frameCount = 2;
    }
    /**
     * @brief Report that this scalar-only fixture supplies no geometry vertices.
     * @return Zero, geometry reading is intentionally unsupported.
     */
    int vertexCount() const override { return 0; }
    /**
     * @brief Report four scalar cell records for each fixture frame.
     * @return Four, no state changes or allocation.
     */
    int triangleCount() const override { return 4; }
    /**
     * @brief Report two available dynamic fixture frames.
     * @return Two, descriptor may independently mark a variable static for temporal tests.
     */
    int timeCount() const override { return 2; }
    /**
     * @brief Expose mutable fixture history identity for stale-read detection.
     * @return Current generation integer, incremented by readFaceVariableAt only when stale flag
     * set; read-only accessor, not synchronized for concurrent use.
     */
    int historyGeneration() const override { return generation; }
    /**
     * @brief Return the configured source identity used for overwrite protection and CSV
     * metadata.
     * @return Owned QString copy of file, possibly empty; no file existence check or open.
     */
    QString sourcePath() const override { return file; }
    /**
     * @brief Map an available frame to its one-minute UTC fixture report time.
     * @param[in] frame Zero-based index 0 or 1; other values are unavailable.
     * @return 2026-09-30 T 00:00 Z plus 60*frame seconds, invalid QDateTime for other indices.
     * No source mutation or IO.
     */
    QDateTime simTimeAt(int frame) const override
    {
        return frame >= 0 && frame < 2
                   ? QDateTime(QDate(2026, 9, 30), QTime(0, 0), QTimeZone::UTC).addSecs(frame * 60)
                   : QDateTime();
    }
    /**
     * @brief Refuse geometry reads in this scalar-only fixture.
     * @return Always false, all four unnamed borrowed output vectors remain unchanged.
     * @note Positional outputs are x, y, z and triangle connectivity; none are inspected or
     * retained.
     */
    bool readMeshGeometry(std::vector<double> &, std::vector<double> &, std::vector<double> &,
                          std::vector<std::array<int, 3>> &) override
    {
        return false;
    }
    /**
     * @brief Refuse depth reads because only catalog scalar variables are supplied.
     * @return Always false, unnamed frame ignored and unnamed borrowed depth vector unchanged.
     * No IO, allocation or source mutation.
     */
    bool readDepthsAt(int, std::vector<float> &) override { return false; }
    /**
     * @brief Return the fixture's single configurable catalog descriptor.
     * @return Owning one-entry vector copied from variable; unnamed optional warnings output is
     * ignored and left unchanged. No catalog validation, file IO or source mutation.
     */
    QVector<Mesh2DResultVariable> faceVariables(QStringList * = nullptr) const override
    {
        return {variable};
    }
    /**
     * @brief Populate a scalar frame and optionally simulate malformed shape or stale history.
     * @param[in] value Stable requested descriptor, exact key must match fixture variable.
     * @param[in] frame Zero-based 0 or 1; outside range refuses.
     * @param[out] out Borrowed vector replaced on success with four values: zero, NaN, zero,
     * zero.
     * @param[out] status Borrowed vector replaced with Valid/Missing/Waterless/NotApplicable,
     * last removed when corrupt flag set; failure before assignment leaves both outputs
     * unchanged.
     * @return False for fail flag/key/frame mismatch, true otherwise even when
     * corruption/staleness injected. When stale flag set, increments generation after
     * assignment. Allocations may partially update outputs; mutable fixture is not
     * concurrent-safe, no IO or retained references.
     */
    bool readFaceVariableAt(const Mesh2DResultVariable &value, int frame, std::vector<float> &out,
                            std::vector<Mesh2DValueStatus> &status) override
    {
        if (fail || value.key() != variable.key() || frame < 0 || frame >= 2)
            return false;
        out = {0.f, std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f};
        status = {Mesh2DValueStatus::Valid, Mesh2DValueStatus::Missing,
                  Mesh2DValueStatus::Waterless, Mesh2DValueStatus::NotApplicable};
        if (corrupt)
            status.pop_back();
        if (stale)
            ++generation;
        return true;
    }
};
/**
 * @brief Create the persistent scalar-export review root and resolve one output basename.
 * @param[in] name Safe test-owned filename, preserved; caller controls overwrite behavior.
 * @return Root/name path, root from SWMMVIS_RESULTS_TEST_OUTPUT or source-relative phase 34
 * review folder. Attempts mkpath without reporting failure, no output file creation or broad
 * directory deletion.
 */
QString outputPath(const QString &name)
{
    const QString root = qEnvironmentVariable(
        "SWMMVIS_RESULTS_TEST_OUTPUT",
        QFileInfo(QString::fromUtf8(__FILE__))
            .absoluteDir()
            .absoluteFilePath("../../workplans/artifacts/phase_34_themes_results/export"));
    QDir().mkpath(root);
    return QDir(root).filePath(name);
}
/**
 * @brief Read a fixture/export file's raw bytes.
 * @param[in] path Read-only path, borrowed synchronously.
 * @return Owned available bytes, empty for open failure or empty file; read errors not
 * distinguished. Closes handle locally, does not modify destination.
 */
QByteArray contents(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
/**
 * @brief Replace a test-owned path with the preserved original sentinel payload.
 * @param[in] path Writable source/destination fixture path with existing parent.
 * @return True when open succeeds and all 19 bytes written; false can leave partial/truncated
 * file. No flush check, handle closes locally, no parent creation.
 */
bool sentinel(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("preserved original\n") == 19;
}
}

class TestMesh2DVariableExport : public QObject
{
    Q_OBJECT
private slots:
    /**
     * @brief Verify scalar CSV preserves escaped identity, UTC time, native units and every
     * value-status distinction.
     * @note Writes current,frame.csv with quoted multiline species, explicit valid zero/missing
     * blank/ waterless zero/not_applicable zero and requested frame 1 time; exact source
     * sentinel retained. Outputs under persistent review root, no real results source opened.
     */
    void csvCarriesIdentityTimeUnitsAndStatuses()
    {
        VariableSource source;
        source.file = outputPath("source,fixture.h5");
        QVERIFY(sentinel(source.file));
        const QString path = outputPath("current,frame.csv");
        QString error;
        QVERIFY2(exportMesh2DVariableCsv(source, source.variable.key(), 1, path, &error),
                 qPrintable(error));
        QVERIFY(error.isEmpty());
        const QByteArray csv = contents(path);
        QVERIFY(csv.startsWith(
            "source,key,dataset,species,domain,zone,layer_index_0based,temporal,requested_frame,frame,time,cell_index_0based,value,status,units\n"));
        QVERIFY(csv.contains("\"N,\"\"special\"\"\nline\""));
        QVERIFY(csv.contains("2026-09-30T00:01:00.000Z"));
        QVERIFY(csv.contains(",0,0,valid,\"ug/L\""));
        QVERIFY(csv.contains(",1,,missing,\"ug/L\""));
        QVERIFY(csv.contains(",2,0,waterless,\"ug/L\""));
        QVERIFY(csv.contains(",3,0,not_applicable,\"ug/L\""));
        QCOMPARE(contents(source.file), QByteArray("preserved original\n"));
    }
    /**
     * @brief Register unknown units/key/frame, read failure, shape mismatch and stale-history
     * export refusals.
     * @note Six cases start with an existing destination sentinel.
     */
    void refusalsPreserveExistingDestination_data()
    {
        QTest::addColumn<QString>("reason");
        for (const QString reason : {"unknown-units", "missing-key", "missing-frame", "read-error",
                                     "bad-shape", "stale-frame"})
            QTest::newRow(qPrintable(reason)) << reason;
    }
    /**
     * @brief Verify every scalar export refusal provides an error and preserves prior
     * destination bytes.
     * @note Configurable fake source injects each data-driven failure, no successful writer
     * publication; scenario CSV sentinel created/replaced only by test setup.
     */
    void refusalsPreserveExistingDestination()
    {
        QFETCH(QString, reason);
        VariableSource source;
        const QString path = outputPath(reason + ".csv");
        QVERIFY(sentinel(path));
        QString key = source.variable.key();
        int frame = 1;
        if (reason == "unknown-units")
            source.variable.unitsKnown = false;
        if (reason == "missing-key")
            key += ":missing";
        if (reason == "missing-frame")
            frame = 8;
        source.fail = reason == "read-error";
        source.corrupt = reason == "bad-shape";
        source.stale = reason == "stale-frame";
        QString error;
        QVERIFY(!exportMesh2DVariableCsv(source, key, frame, path, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(contents(path), QByteArray("preserved original\n"));
    }
    /**
     * @brief Register exact source path, symlink and hardlink overwrite-protection cases.
     * @note Aliases identify the same physical source despite different output names.
     */
    void sourceAliasesCannotBeOverwritten_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("same-path") << QString("same");
        QTest::newRow("symlink") << QString("symlink");
        QTest::newRow("hardlink") << QString("hardlink");
    }
    /**
     * @brief Verify CSV export refuses every filesystem alias of the protected source.
     * @note Creates review source/symlink/hardlink fixtures, requires nonempty refusal and
     * original 19-byte source sentinel exact. Alias basename replaced by setup only, source
     * payload never exported over.
     */
    void sourceAliasesCannotBeOverwritten()
    {
        QFETCH(QString, kind);
        VariableSource source;
        source.file = outputPath("protected-" + kind + ".h5");
        QVERIFY(sentinel(source.file));
        QString path = source.file;
        if (kind != "same") {
            path = outputPath("alias-" + kind + ".csv");
            QFile::remove(path);
            if (kind == "symlink")
                QVERIFY(QFile::link(source.file, path));
            else {
                std::error_code ec;
                std::filesystem::create_hard_link(
                    std::filesystem::u8path(source.file.toUtf8().constData()),
                    std::filesystem::u8path(path.toUtf8().constData()), ec);
                QVERIFY2(!ec, ec.message().c_str());
            }
        }
        QString error;
        QVERIFY(!exportMesh2DVariableCsv(source, source.variable.key(), 1, path, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(contents(source.file), QByteArray("preserved original\n"));
    }
    /**
     * @brief Verify static-variable CSV records requested frame but omits effective report
     * frame/time.
     * @note Marks descriptor Static/frameCount 0, requested 1; expected static row metadata and
     * no date text. Writes static.csv under review root, does not alter temporal source clock.
     */
    void staticExportDoesNotClaimAnInstantaneousReport()
    {
        VariableSource source;
        source.variable.temporal = Mesh2DResultVariable::Temporal::Static;
        source.variable.frameCount = 0;
        const QString path = outputPath("static.csv");
        QString error;
        QVERIFY2(exportMesh2DVariableCsv(source, source.variable.key(), 1, path, &error),
                 qPrintable(error));
        const QByteArray csv = contents(path);
        QVERIFY(csv.contains(",static,1,,,0,0,valid,"));
        QVERIFY(!csv.contains("2026-09-30"));
    }
    /**
     * @brief Verify unresolved scalar units fail before creating any new output file.
     * @note Removes only owned unresolved-must-not-exist.csv, requires error and absent target;
     * no source/result files accessed.
     */
    void unknownUnitsDoNotCreateDestination()
    {
        VariableSource source;
        source.variable.unitsKnown = false;
        const QString path = outputPath("unresolved-must-not-exist.csv");
        QFile::remove(path);
        QString error;
        QVERIFY(!exportMesh2DVariableCsv(source, source.variable.key(), 1, path, &error));
        QVERIFY(!QFile::exists(path));
        QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestMesh2DVariableExport)
#include "test_mesh2dvariableexport.moc"
