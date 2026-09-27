// SPDX-License-Identifier: GPL-3.0-or-later
#include "project/generatedmeshartifacts.h"
#include <QTest>

namespace {
QString fixture(const QString &name) {
    const QString root = qEnvironmentVariable("SWMMVIS_MESH_IMPORT_ARTIFACT_OUTPUT",
        QStringLiteral(MESH_IMPORT_ARTIFACT_DATA_DIR));
    const QString dir = root + '/' + name + '-' + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(dir);
    return dir;
}
bool put(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool stageModel(ProjectSaveOutputs &outputs, const QString &dir) {
    const QString path = outputs.stage(dir + "/model.inp", ProjectSaveOutputs::Model);
    return !path.isEmpty() && put(path, "[TITLE]\nImported mesh project\n");
}
}

class TestMeshImportArtifacts : public QObject {
    Q_OBJECT
private slots:
    void meshSnapshotSurvivesSourceEdits() {
        const QString dir = fixture("source-snapshot");
        const QString source = dir + "/source.2dm", destination = dir + "/imported.2dm";
        const QByteArray original("[2D_VERTICES]\n0 0 0\n; original source snapshot\n");
        QVERIFY(put(source, original));
        QString error;
        auto artifacts = GeneratedMeshArtifacts::create(dir + "/model.inp", &error);
        QVERIFY2(artifacts, qPrintable(error));
        const QString snapshot = artifacts->reserve(destination, "snapshot.2dm", &error, ProjectSaveOutputs::Mesh);
        QVERIFY2(!snapshot.isEmpty(), qPrintable(error));
        artifacts->protectInput(source);
        QVERIFY2(artifacts->copySource(source, snapshot, &error), qPrintable(error));
        QVERIFY2(artifacts->seal(&error), qPrintable(error));
        QVERIFY(put(source, "external source edit"));
        ProjectSaveOutputs outputs;
        artifacts->protectInputs(outputs);
        QVERIFY(stageModel(outputs, dir));
        QVERIFY2(artifacts->prepareSave(outputs, &error), qPrintable(error));
        QCOMPARE(read(snapshot), original);
        QVERIFY(!QFileInfo::exists(destination));
        int meshEntries = 0;
        for (const auto &entry : outputs.preparedOutputs()) {
            if (entry.role != ProjectSaveOutputs::Mesh) continue;
            ++meshEntries;
            QCOMPARE(entry.finalPath, destination);
            QCOMPARE(read(entry.stagedPath), original);
        }
        QCOMPARE(meshEntries, 1);
        QVERIFY2(outputs.publish(), qPrintable(outputs.error()));
        QCOMPARE(read(destination), original);
        QCOMPARE(read(source), QByteArray("external source edit"));
    }

    void changedSourceDuringCopyIsRejected() {
        const QString dir = fixture("source-changed-during-copy");
        const QString source = dir + "/source.2dm";
        QVERIFY(put(source, QByteArray(2 * 1024 * 1024, 'a')));
        QString error;
        auto artifacts = GeneratedMeshArtifacts::create(dir + "/model.inp", &error);
        QVERIFY(artifacts);
        const QString snapshot = artifacts->reserve(dir + "/final.2dm", "snapshot.2dm", &error, ProjectSaveOutputs::Mesh);
        QVERIFY(!snapshot.isEmpty());
        int checks = 0;
        bool changed = false;
        QVERIFY(!artifacts->copySource(source, snapshot, &error, [&] {
            if (++checks == 2) changed = put(source, QByteArray(2 * 1024 * 1024, 'b'));
            return false;
        }));
        QVERIFY(changed);
        QVERIFY2(error.contains("changed during import"), qPrintable(error));
        QVERIFY(!QFileInfo::exists(snapshot));
        QVERIFY(!QFileInfo::exists(dir + "/final.2dm"));
    }

    void cancellation_data() {
        QTest::addColumn<int>("cancelAt");
        QTest::newRow("before-copy") << 1;
        QTest::newRow("after-first-buffer") << 3;
    }
    void cancellation() {
        QFETCH(int, cancelAt);
        const QString dir = fixture("cancel");
        const QString source = dir + "/source.2dm";
        const QByteArray original(2 * 1024 * 1024, 'x');
        QVERIFY(put(source, original));
        QString error;
        auto artifacts = GeneratedMeshArtifacts::create(dir + "/model.inp", &error);
        QVERIFY(artifacts);
        const QString ownedDirectory = artifacts->directoryPath();
        const QString snapshot = artifacts->reserve(dir + "/final.2dm", "snapshot.2dm", &error, ProjectSaveOutputs::Mesh);
        QVERIFY(!snapshot.isEmpty());
        int checks = 0;
        QVERIFY(!artifacts->copySource(source, snapshot, &error, [&] { return ++checks >= cancelAt; }));
        QVERIFY2(error.contains("cancelled"), qPrintable(error));
        QVERIFY(!QFileInfo::exists(snapshot));
        QVERIFY(QDir(ownedDirectory).entryList(QDir::Files | QDir::Hidden).isEmpty());
        artifacts.reset();
        QVERIFY(!QFileInfo::exists(ownedDirectory));
        QCOMPARE(read(source), original);
        QVERIFY(!QFileInfo::exists(dir + "/final.2dm"));
    }

    void lastOwnerCleansUnpublishedSnapshot() {
        const QString dir = fixture("last-owner");
        QString error;
        auto artifacts = GeneratedMeshArtifacts::create(dir + "/model.inp", &error);
        QVERIFY(artifacts);
        const QString ownedDirectory = artifacts->directoryPath();
        const QString snapshot = artifacts->reserve(dir + "/final.2dm", "snapshot.2dm", &error, ProjectSaveOutputs::Mesh);
        QVERIFY(put(snapshot, "pending mesh"));
        QVERIFY(artifacts->seal(&error));
        auto futureOwner = artifacts;
        artifacts.reset();
        QVERIFY(QFileInfo::exists(snapshot));
        futureOwner.reset();
        QVERIFY(!QFileInfo::exists(ownedDirectory));
        QVERIFY(!QFileInfo::exists(dir + "/final.2dm"));
    }

    void pendingPayloadTamper_data() {
        QTest::addColumn<bool>("reseal");
        QTest::newRow("modified-after-seal") << false;
        QTest::newRow("reseal-cannot-certify-tamper") << true;
    }
    void pendingPayloadTamper() {
        QFETCH(bool, reseal);
        const QString dir = fixture("tamper");
        QString error;
        auto artifacts = GeneratedMeshArtifacts::create(dir + "/model.inp", &error);
        QVERIFY(artifacts);
        const QString snapshot = artifacts->reserve(dir + "/final.2dm", "snapshot.2dm", &error, ProjectSaveOutputs::Mesh);
        QVERIFY(put(snapshot, "captured source"));
        QVERIFY(artifacts->seal(&error));
        QVERIFY(put(snapshot, "tampered snapshot"));
        if (reseal) (void)artifacts->seal(&error);
        ProjectSaveOutputs outputs;
        QVERIFY(stageModel(outputs, dir));
        QVERIFY(!artifacts->prepareSave(outputs, &error));
        QVERIFY2(error.contains("changed"), qPrintable(error));
        QVERIFY(!QFileInfo::exists(dir + "/final.2dm"));
        QVERIFY(!QFileInfo::exists(dir + "/model.inp"));
        QVERIFY(QFileInfo::exists(snapshot)); // retry/discard still owns its data
    }

    void sealRejectsUnownedFiles() {
        const QString dir = fixture("unowned-file");
        QString error;
        auto artifacts = GeneratedMeshArtifacts::create(dir + "/model.inp", &error);
        QVERIFY(artifacts);
        const QString snapshot = artifacts->reserve(dir + "/final.2dm", "snapshot.2dm", &error, ProjectSaveOutputs::Mesh);
        QVERIFY(put(snapshot, "mesh"));
        QVERIFY(put(QDir(artifacts->directoryPath()).filePath("unexpected.bin"), "unregistered"));
        QVERIFY(!artifacts->seal(&error));
        QVERIFY2(error.contains("unsupported companion"), qPrintable(error));
        ProjectSaveOutputs outputs;
        QVERIFY(!artifacts->prepareSave(outputs, &error));
    }

    void changedDestination_data() {
        QTest::addColumn<bool>("existing");
        QTest::newRow("existing-file-edited") << true;
        QTest::newRow("new-file-created") << false;
    }
    void changedDestination() {
        QFETCH(bool, existing);
        const QString dir = fixture("destination-change");
        const QString destination = dir + "/final.2dm";
        if (existing) QVERIFY(put(destination, "old destination"));
        QString error;
        auto artifacts = GeneratedMeshArtifacts::create(dir + "/model.inp", &error);
        QVERIFY(artifacts);
        const QString snapshot = artifacts->reserve(destination, "snapshot.2dm", &error, ProjectSaveOutputs::Mesh);
        QVERIFY(put(snapshot, "pending import"));
        QVERIFY(artifacts->seal(&error));
        QVERIFY(put(destination, "external change"));
        ProjectSaveOutputs outputs;
        QVERIFY(stageModel(outputs, dir));
        QVERIFY(!artifacts->prepareSave(outputs, &error));
        QVERIFY2(error.contains("changed since generation"), qPrintable(error));
        QCOMPARE(read(destination), QByteArray("external change"));
        QVERIFY(QFileInfo::exists(snapshot));
    }

    void rebaseDestination_data() {
        QTest::addColumn<QString>("change");
        QTest::newRow("absent-destination-publishes") << QString("none");
        QTest::newRow("new-collision-refused") << QString("collision");
        QTest::newRow("source-overwrite-refused") << QString("source");
    }
    void rebaseDestination() {
        QFETCH(QString, change);
        const QString dir = fixture("rebase");
        const QString source = dir + "/original.2dm";
        const QByteArray original("native source with unowned sections\n");
        QVERIFY(put(source, original));
        QString error;
        auto artifacts = GeneratedMeshArtifacts::create(dir + "/model.inp", &error);
        QVERIFY(artifacts);
        artifacts->protectInput(source);
        const QString oldDestination = dir + "/untitled.2dm";
        const QString snapshot = artifacts->reserve(oldDestination, "snapshot.2dm", &error, ProjectSaveOutputs::Mesh);
        QVERIFY(artifacts->copySource(source, snapshot, &error));
        QVERIFY(artifacts->seal(&error));
        GeneratedMeshArtifacts::Entry destination;
        destination.finalPath = change == "source" ? source : dir + "/saved/imported.2dm";
        QVERIFY(ProjectSaveOutputs::captureDestination(destination.finalPath, &destination.destination, &error));
        // Overrides control only destination; neither payload nor role can
        // be replaced through this path.
        destination.stagedPath = dir + "/untrusted.bin";
        destination.payloadHash = "not the source hash";
        destination.role = ProjectSaveOutputs::Auxiliary;
        if (change == "collision") {
            QVERIFY(QDir().mkpath(dir + "/saved"));
            QVERIFY(put(destination.finalPath, "later file"));
        }
        ProjectSaveOutputs outputs;
        artifacts->protectInputs(outputs);
        QVERIFY(stageModel(outputs, dir));
        const bool prepared = artifacts->prepareSave(outputs, &error, &destination);
        QCOMPARE(prepared, change == "none");
        if (prepared) {
            bool mesh = false;
            for (const auto &entry : outputs.preparedOutputs())
                mesh |= entry.role == ProjectSaveOutputs::Mesh && entry.finalPath == destination.finalPath;
            QVERIFY(mesh);
            QVERIFY(!QFileInfo::exists(dir + "/saved"));
            QVERIFY2(outputs.publish(), qPrintable(outputs.error()));
            QCOMPARE(read(destination.finalPath), original);
        } else {
            QVERIFY(!error.isEmpty());
            if (change == "collision") QCOMPARE(read(destination.finalPath), QByteArray("later file"));
        }
        QCOMPARE(read(source), original);
        QVERIFY(!QFileInfo::exists(oldDestination));
        QVERIFY(QFileInfo::exists(snapshot));
    }
};

QTEST_GUILESS_MAIN(TestMeshImportArtifacts)
#include "test_meshimportartifacts.moc"
