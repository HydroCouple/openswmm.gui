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

namespace {
class VariableSource final : public IMesh2DSource {
public:
    Mesh2DResultVariable variable;
    QString file;
    bool fail = false, corrupt = false, stale = false;
    int generation = 0;
    VariableSource() {
        variable.dataset = "Mesh2_face_species_conc";
        variable.species = "N,\"special\"\nline";
        variable.label = "Surface concentration";
        variable.units = "ug/L";
        variable.unitsKnown = true;
        variable.frameCount = 2;
    }
    int vertexCount() const override { return 0; }
    int triangleCount() const override { return 4; }
    int timeCount() const override { return 2; }
    int historyGeneration() const override { return generation; }
    QString sourcePath() const override { return file; }
    QDateTime simTimeAt(int frame) const override {
        return frame >= 0 && frame < 2
            ? QDateTime(QDate(2026,9,30), QTime(0,0), QTimeZone::UTC).addSecs(frame * 60) : QDateTime();
    }
    bool readMeshGeometry(std::vector<double>&, std::vector<double>&,
                           std::vector<double>&, std::vector<std::array<int,3>>&) override { return false; }
    bool readDepthsAt(int, std::vector<float>&) override { return false; }
    QVector<Mesh2DResultVariable> faceVariables(QStringList* = nullptr) const override { return {variable}; }
    bool readFaceVariableAt(const Mesh2DResultVariable& value, int frame,
                            std::vector<float>& out, std::vector<Mesh2DValueStatus>& status) override {
        if (fail || value.key() != variable.key() || frame < 0 || frame >= 2) return false;
        out = {0.f, std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f};
        status = {Mesh2DValueStatus::Valid, Mesh2DValueStatus::Missing,
                  Mesh2DValueStatus::Waterless, Mesh2DValueStatus::NotApplicable};
        if (corrupt) status.pop_back();
        if (stale) ++generation;
        return true;
    }
};
QString outputPath(const QString& name) {
    const QString root = qEnvironmentVariable("SWMMVIS_RESULTS_TEST_OUTPUT",
        QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath(
            "../../workplans/artifacts/phase_34_themes_results/export"));
    QDir().mkpath(root);
    return QDir(root).filePath(name);
}
QByteArray contents(const QString& path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
bool sentinel(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("preserved original\n") == 19;
}
}

class TestMesh2DVariableExport : public QObject {
    Q_OBJECT
private slots:
    void csvCarriesIdentityTimeUnitsAndStatuses() {
        VariableSource source;
        source.file = outputPath("source,fixture.h5");
        QVERIFY(sentinel(source.file));
        const QString path = outputPath("current,frame.csv");
        QString error;
        QVERIFY2(exportMesh2DVariableCsv(source, source.variable.key(), 1, path, &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        const QByteArray csv = contents(path);
        QVERIFY(csv.startsWith("source,key,dataset,species,domain,zone,layer_index_0based,temporal,requested_frame,frame,time,cell_index_0based,value,status,units\n"));
        QVERIFY(csv.contains("\"N,\"\"special\"\"\nline\""));
        QVERIFY(csv.contains("2026-09-30T00:01:00.000Z"));
        QVERIFY(csv.contains(",0,0,valid,\"ug/L\""));
        QVERIFY(csv.contains(",1,,missing,\"ug/L\""));
        QVERIFY(csv.contains(",2,0,waterless,\"ug/L\""));
        QVERIFY(csv.contains(",3,0,not_applicable,\"ug/L\""));
        QCOMPARE(contents(source.file), QByteArray("preserved original\n"));
    }
    void refusalsPreserveExistingDestination_data() {
        QTest::addColumn<QString>("reason");
        for (const QString reason : {"unknown-units", "missing-key", "missing-frame", "read-error", "bad-shape", "stale-frame"})
            QTest::newRow(qPrintable(reason)) << reason;
    }
    void refusalsPreserveExistingDestination() {
        QFETCH(QString, reason);
        VariableSource source;
        const QString path = outputPath(reason + ".csv");
        QVERIFY(sentinel(path));
        QString key = source.variable.key(); int frame = 1;
        if (reason == "unknown-units") source.variable.unitsKnown = false;
        if (reason == "missing-key") key += ":missing";
        if (reason == "missing-frame") frame = 8;
        source.fail = reason == "read-error";
        source.corrupt = reason == "bad-shape";
        source.stale = reason == "stale-frame";
        QString error;
        QVERIFY(!exportMesh2DVariableCsv(source, key, frame, path, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(contents(path), QByteArray("preserved original\n"));
    }
    void sourceAliasesCannotBeOverwritten_data() {
        QTest::addColumn<QString>("kind");
        QTest::newRow("same-path") << QString("same");
        QTest::newRow("symlink") << QString("symlink");
        QTest::newRow("hardlink") << QString("hardlink");
    }
    void sourceAliasesCannotBeOverwritten() {
        QFETCH(QString, kind);
        VariableSource source; source.file = outputPath("protected-" + kind + ".h5");
        QVERIFY(sentinel(source.file));
        QString path = source.file;
        if (kind != "same") {
            path = outputPath("alias-" + kind + ".csv"); QFile::remove(path);
            if (kind == "symlink") QVERIFY(QFile::link(source.file, path));
            else {
                std::error_code ec;
                std::filesystem::create_hard_link(std::filesystem::u8path(source.file.toUtf8().constData()),
                                                 std::filesystem::u8path(path.toUtf8().constData()), ec);
                QVERIFY2(!ec, ec.message().c_str());
            }
        }
        QString error;
        QVERIFY(!exportMesh2DVariableCsv(source, source.variable.key(), 1, path, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(contents(source.file), QByteArray("preserved original\n"));
    }
    void staticExportDoesNotClaimAnInstantaneousReport() {
        VariableSource source;
        source.variable.temporal = Mesh2DResultVariable::Temporal::Static;
        source.variable.frameCount = 0;
        const QString path = outputPath("static.csv");
        QString error;
        QVERIFY2(exportMesh2DVariableCsv(source, source.variable.key(), 1, path, &error), qPrintable(error));
        const QByteArray csv = contents(path);
        QVERIFY(csv.contains(",static,1,,,0,0,valid,"));
        QVERIFY(!csv.contains("2026-09-30"));
    }
    void unknownUnitsDoNotCreateDestination() {
        VariableSource source; source.variable.unitsKnown = false;
        const QString path = outputPath("unresolved-must-not-exist.csv"); QFile::remove(path);
        QString error;
        QVERIFY(!exportMesh2DVariableCsv(source, source.variable.key(), 1, path, &error));
        QVERIFY(!QFile::exists(path)); QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestMesh2DVariableExport)
#include "test_mesh2dvariableexport.moc"
