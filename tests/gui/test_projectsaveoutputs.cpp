// SPDX-License-Identifier: GPL-3.0-or-later
#include "project/projectsaveoutputs.h"
#include "project/generatedmeshartifacts.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTest>
#include <cstdlib>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "projectsaveoutputstestaccess.h"

static bool put(const QString &p, const QByteArray &bytes) {
    QFile f(p); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
static QByteArray read(const QString &p) {
    QFile f(p); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
static QString fixture(const QString &name) {
    const QString root = qEnvironmentVariable("SWMMVIS_SAVE_RECOVERY_OUTPUT",
        QStringLiteral(SAVE_RECOVERY_DATA_DIR));
    const QString dir = root + '/' + name + '-' + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(dir);
    return dir;
}
static QStringList paths(const QString &dir) {
    return {dir + "/model.inp", dir + "/mesh.2dm", dir + "/config.rxn", dir + "/new/settings.oswp"};
}
static bool prepare(ProjectSaveOutputs &outputs, const QString &dir, bool newModel = false) {
    auto list = paths(dir);
    // Use a component for the nested new file: settings must be an adjacent sibling.
    const ProjectSaveOutputs::Role roles[] = {ProjectSaveOutputs::Model, ProjectSaveOutputs::Mesh,
        ProjectSaveOutputs::Component, ProjectSaveOutputs::Component};
    for (int i = 0; i < list.size(); ++i) {
        if (i < 3 && !(newModel && i == 0) && !put(list[i], QByteArray("old-") + QByteArray::number(i))) return false;
        const QString stage = outputs.stage(list[i], roles[i]);
        if (stage.isEmpty() || !put(stage, QByteArray("new-") + QByteArray::number(i))) return false;
    }
    return true;
}
static bool crash(const QString &dir, int step, bool newModel = false) {
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {newModel ? "--interrupt-new-save" : "--interrupt-save", dir, QString::number(step)});
    return child.waitForFinished(15000) && child.exitCode() == 86;
}
static QStringList auxiliaryPaths(const QString &dir) {
    return {dir + "/model.inp", dir + "/burn.tif", dir + "/new/burn_report.csv"};
}
static bool prepareAuxiliary(ProjectSaveOutputs &outputs, const QString &dir) {
    const auto list = auxiliaryPaths(dir);
    for (int i = 0; i < list.size(); ++i) {
        if (i < 2 && !put(list[i], QByteArray("old-") + QByteArray::number(i))) return false;
        const auto role = i == 0 ? ProjectSaveOutputs::Model : ProjectSaveOutputs::Auxiliary;
        const QString stage = outputs.stage(list[i], role);
        if (stage.isEmpty() || !put(stage, QByteArray("new-") + QByteArray::number(i))) return false;
    }
    return true;
}

class TestProjectSaveOutputs : public QObject {
    Q_OBJECT
private slots:
    void inheritedOutputsSkipAnAlreadyPublishedParent() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString model=directory.filePath("model.inp");
        const QString mesh=directory.filePath("mesh.2dm");
        const QString terrain=directory.filePath("terrain.tif");
        const QString report=directory.filePath("new-report.csv");
        QString why;
        auto parent=GeneratedMeshArtifacts::create(model,&why);QVERIFY2(parent,qPrintable(why));
        QVERIFY(put(parent->reserve(mesh,"mesh.2dm",&why,ProjectSaveOutputs::Mesh),"original mesh"));
        QVERIFY(put(parent->reserve(terrain,"terrain.tif",&why),"original terrain"));
        QVERIFY(parent->requireAbsent(terrain+".aux.xml",&why));
        QVERIFY2(parent->seal(&why),qPrintable(why));
        auto child=GeneratedMeshArtifacts::create(model,&why);QVERIFY(child);
        QVERIFY2(child->inheritPending(parent,&why),qPrintable(why));
        QVERIFY(put(child->reserve(report,"new-report.csv",&why),"child report"));
        QVERIFY2(child->seal(&why),qPrintable(why));
        { ProjectSaveOutputs outputs;QVERIFY(put(outputs.stage(model,ProjectSaveOutputs::Model),"model"));
          QVERIFY2(parent->prepareSave(outputs,&why),qPrintable(why));
          QVERIFY2(outputs.publish(),qPrintable(outputs.error()));parent->markPublished(mesh); }
        QCOMPARE(child->publishedMeshPath(),mesh);QVERIFY(!child->isPublished());
        // A published auxiliary is independently editable; the still-pending
        // child must neither replay it nor recheck its old companion guard.
        QVERIFY(put(terrain,"edited published terrain"));QVERIFY(put(terrain+".aux.xml","published metadata"));
        ProjectSaveOutputs outputs;QVERIFY(put(outputs.stage(model,ProjectSaveOutputs::Model),"model"));
        QVERIFY2(child->prepareSave(outputs,&why),qPrintable(why));
        const auto pending=outputs.preparedOutputs();QCOMPARE(pending.size(),2);QCOMPARE(pending.last().finalPath,report);
        QVERIFY2(outputs.publish(),qPrintable(outputs.error()));child->markPublished(mesh);
        QCOMPARE(read(terrain),QByteArray("edited published terrain"));QCOMPARE(read(report),QByteArray("child report"));
        QCOMPARE(parent->publishedMeshPath(),mesh);
    }
    void inheritedPublicationDoesNotConsumeUnpublishedAuxiliaries() {
        QTemporaryDir directory;QVERIFY(directory.isValid());QString why;
        const QString model=directory.filePath("model.inp"),mesh=directory.filePath("mesh.2dm"),report=directory.filePath("report.csv");
        auto parent=GeneratedMeshArtifacts::create(model,&why);QVERIFY(parent);
        QVERIFY(put(parent->reserve(mesh,"mesh.2dm",&why,ProjectSaveOutputs::Mesh),"mesh"));QVERIFY(parent->seal(&why));
        auto child=GeneratedMeshArtifacts::create(model,&why);QVERIFY(child);QVERIFY(child->inheritPending(parent,&why));
        QVERIFY(put(child->reserve(report,"report.csv",&why),"report"));QVERIFY(child->seal(&why));
        ProjectSaveOutputs outputs;QVERIFY(put(outputs.stage(model,ProjectSaveOutputs::Model),"model"));
        QVERIFY2(child->prepareSave(outputs,&why),qPrintable(why));QCOMPARE(outputs.preparedOutputs().size(),3);
        QVERIFY(!parent->isPublished());QVERIFY(!child->isPublished());
        QVERIFY2(outputs.publish(),qPrintable(outputs.error()));child->markPublished(mesh);
        QVERIFY(parent->isPublished());QVERIFY(child->isPublished());QCOMPARE(read(mesh),QByteArray("mesh"));QCOMPARE(read(report),QByteArray("report"));
    }
    void protectedDirectory_data() {
        QTest::addColumn<QString>("scenario");
        QTest::newRow("nested-output") << QString("nested");
        QTest::newRow("directory-itself") << QString("equal");
        QTest::newRow("symlink-ancestor") << QString("alias");
        QTest::newRow("registered-after-preparation") << QString("late");
        QTest::newRow("symlink-changed-after-preparation") << QString("retarget");
        QTest::newRow("sibling-prefix-allowed") << QString("sibling");
    }
    void protectedDirectory() {
        QFETCH(QString, scenario);
        const QString dir = fixture("protected-directory");
        const QString owned = dir + "/pending";
        QVERIFY(QDir().mkpath(owned));
        QString output = owned + "/nested/component.cfg";
        QString alias;
        if (scenario == "equal") output = owned;
        if (scenario == "sibling") output = dir + "/pending-sibling/component.cfg";
        if (scenario == "alias" || scenario == "retarget") {
            alias = dir + "/alias";
            const QString initial = scenario == "alias" ? owned : dir + "/initial";
            QVERIFY(QDir().mkpath(initial));
            std::error_code ec;
            std::filesystem::create_directory_symlink(initial.toStdString(), alias.toStdString(), ec);
            if (ec) QSKIP("Directory symlinks not available");
            output = alias + "/nested/component.cfg";
        }
        ProjectSaveOutputs outputs;
        if (scenario != "late") outputs.protectDirectory(owned);
        const QString model = dir + "/model.inp";
        QVERIFY(put(model, "old model"));
        const QString modelStage = outputs.stage(model, ProjectSaveOutputs::Model);
        QVERIFY(!modelStage.isEmpty());
        QVERIFY(put(modelStage, "new model"));
        const QString componentStage = outputs.stage(output, ProjectSaveOutputs::Component);
        const bool stageAllowed = scenario == "late" || scenario == "retarget" || scenario == "sibling";
        QCOMPARE(!componentStage.isEmpty(), stageAllowed);
        if (!stageAllowed) {
            QVERIFY2(outputs.error().contains("protected directory"), qPrintable(outputs.error()));
            QVERIFY(!QFileInfo::exists(owned + "/nested"));
            QCOMPARE(read(model), QByteArray("old model"));
            return;
        }
        QVERIFY(put(componentStage, "prepared component"));
        if (scenario == "late") outputs.protectDirectory(owned);
        if (scenario == "retarget") {
            QVERIFY(QFile::remove(alias));
            std::error_code ec;
            std::filesystem::create_directory_symlink(owned.toStdString(), alias.toStdString(), ec);
            QVERIFY(!ec);
        }
        QCOMPARE(outputs.publish(), scenario == "sibling");
        if (scenario == "sibling") {
            QCOMPARE(read(output), QByteArray("prepared component"));
            QCOMPARE(read(model), QByteArray("new model"));
        } else {
            QVERIFY2(outputs.error().contains("protected directory"), qPrintable(outputs.error()));
            QVERIFY(!outputs.publicationStarted());
            QVERIFY(!QFileInfo::exists(output));
            QCOMPARE(read(model), QByteArray("old model"));
        }
    }
    void generationBaseline_data() {
        QTest::addColumn<QString>("change");
        QTest::newRow("existing-unchanged") << QString("existing");
        QTest::newRow("missing-unchanged") << QString("missing");
        QTest::newRow("existing-edited") << QString("edit");
        QTest::newRow("existing-removed") << QString("remove");
        QTest::newRow("missing-created") << QString("create");
        QTest::newRow("alias-retargeted-same-bytes") << QString("alias");
        QTest::newRow("reuse-edited") << QString("reuse");
    }
    void generationBaseline() {
        QFETCH(QString, change);
        const QString dir = fixture("generation-baseline");
        const QString output = dir + "/burn.tif";
        const bool absent = change == "missing" || change == "create";
        QString oldTarget, newTarget;
        if (change == "alias") {
            oldTarget = dir + "/old-target.tif";
            newTarget = dir + "/new-target.tif";
            QVERIFY(put(oldTarget, "old"));
            QVERIFY(put(newTarget, "old"));
            std::error_code ec;
            std::filesystem::create_symlink(oldTarget.toStdString(), output.toStdString(), ec);
            if (ec) QSKIP("Symlinks not available");
        } else if (!absent) QVERIFY(put(output, "old"));
        ProjectSaveOutputs::DestinationState expected;
        QString error;
        QVERIFY2(ProjectSaveOutputs::captureDestination(output, &expected, &error), qPrintable(error));
        QVERIFY(!expected.resolvedPath.isEmpty());
        QVERIFY(!expected.fingerprint.isEmpty());
        ProjectSaveOutputs outputs;
        if (change == "reuse") QVERIFY(!outputs.stage(output, ProjectSaveOutputs::Auxiliary).isEmpty());
        if (change == "edit" || change == "create" || change == "reuse") QVERIFY(put(output, "external edit"));
        else if (change == "remove") QVERIFY(QFile::remove(output));
        else if (change == "alias") {
            QVERIFY(QFile::remove(output));
            std::error_code ec;
            std::filesystem::create_symlink(newTarget.toStdString(), output.toStdString(), ec);
            QVERIFY(!ec);
        }
        const bool unchanged = change == "existing" || change == "missing";
        const QString stage = outputs.stage(output, ProjectSaveOutputs::Auxiliary, change == "reuse", &expected);
        QCOMPARE(!stage.isEmpty(), unchanged);
        if (!unchanged) QVERIFY2(outputs.error().contains("changed since generation"), qPrintable(outputs.error()));
        if (change == "edit" || change == "create" || change == "reuse") QCOMPARE(read(output), QByteArray("external edit"));
        else if (change == "remove" || change == "missing") QVERIFY(!QFileInfo::exists(output));
        else QCOMPARE(read(output), QByteArray("old"));
        if (change == "alias") {
            QCOMPARE(read(oldTarget), QByteArray("old"));
            QCOMPARE(read(newTarget), QByteArray("old"));
            QVERIFY(QFileInfo(output).isSymLink());
        }
    }
    void destinationCaptureDoesNotCreateDirectories() {
        const QString dir = fixture("capture-missing");
        const QString output = dir + "/terrain/nested/burn.tif";
        ProjectSaveOutputs::DestinationState state;
        QString error;
        QVERIFY2(ProjectSaveOutputs::captureDestination(output, &state, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(dir + "/terrain"));
        ProjectSaveOutputs outputs;
        QVERIFY(!outputs.stage(output, ProjectSaveOutputs::Auxiliary, false, &state).isEmpty());
        QVERIFY(!QFileInfo::exists(dir + "/terrain"));
        QVERIFY(!ProjectSaveOutputs::captureDestination(dir, &state, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(state.resolvedPath.isEmpty());
        QVERIFY(!ProjectSaveOutputs::captureDestination({}, &state, &error));
    }
    // The engine writer publishes by renaming its own temporary over the staged
    // path. A staging handle left open (QTemporaryFile::close() keeps it) made
    // that rename fail on Windows with "Access is denied".
    void stagedPathIsReplaceableByRename() {
        const QString dir = fixture("staged-rename");
        ProjectSaveOutputs outputs;
        QVERIFY(put(dir + "/model.inp", "old"));
        const QString staged = outputs.stage(dir + "/model.inp", ProjectSaveOutputs::Model);
        QVERIFY2(!staged.isEmpty(), qPrintable(outputs.error()));
#ifdef Q_OS_UNIX
        struct stat target {};
        QVERIFY(::stat(QFile::encodeName(staged).constData(), &target) == 0);
        for (int fd = 0; fd < ::getdtablesize(); ++fd) {
            struct stat open {};
            if (::fstat(fd, &open) == 0)
                QVERIFY2(!(open.st_dev == target.st_dev && open.st_ino == target.st_ino),
                         "a descriptor still holds the staged file");
        }
#endif
        const QString writer = dir + "/.writer-temp";
        QVERIFY(put(writer, "new"));
        std::error_code ec;
        std::filesystem::rename(writer.toStdWString(), staged.toStdWString(), ec);
        QVERIFY2(!ec, ec.message().c_str());
        QVERIFY2(outputs.publish(), qPrintable(outputs.error()));
        QCOMPARE(read(dir + "/model.inp"), QByteArray("new"));
        QVERIFY(!QFileInfo::exists(staged));
    }
    void auxiliaryPublication() {
        const QString dir = fixture("auxiliary-publish");
        ProjectSaveOutputs outputs;
        QVERIFY2(prepareAuxiliary(outputs, dir), qPrintable(outputs.error()));
        QVERIFY(!QFileInfo::exists(dir + "/new"));
        bool publishedBeforeModel = false;
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [&](int n) {
            if (n == 2) publishedBeforeModel = read(auxiliaryPaths(dir)[0]) == "old-0"
                && read(auxiliaryPaths(dir)[1]) == "new-1"
                && read(auxiliaryPaths(dir)[2]) == "new-2";
            return true;
        });
        QVERIFY2(outputs.publish(), qPrintable(outputs.error()));
        QVERIFY(publishedBeforeModel);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(read(auxiliaryPaths(dir)[i]), QByteArray("new-") + QByteArray::number(i));
    }
    void auxiliaryRollback() {
        const QString dir = fixture("auxiliary-rollback");
        ProjectSaveOutputs outputs;
        QVERIFY(prepareAuxiliary(outputs, dir));
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [](int n) { return n != 2; });
        QVERIFY(!outputs.publish());
        QVERIFY2(outputs.error().contains("restored"), qPrintable(outputs.error()));
        QCOMPARE(read(auxiliaryPaths(dir)[0]), QByteArray("old-0"));
        QCOMPARE(read(auxiliaryPaths(dir)[1]), QByteArray("old-1"));
        QVERIFY(!QFileInfo::exists(auxiliaryPaths(dir)[2]));
    }
    void auxiliaryRecovery_data() {
        QTest::addColumn<int>("step");
        for (int i = 0; i <= 4; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void auxiliaryRecovery() {
        QFETCH(int, step);
        const QString dir = fixture("auxiliary-recovery");
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--interrupt-aux-save", dir, QString::number(step)});
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitCode(), 86);
        QString error;
        QVERIFY2(ProjectSaveOutputs::recover(auxiliaryPaths(dir)[0], &error), qPrintable(error));
        for (int i = 0; i < 3; ++i) {
            if (step < 4 && i == 2) QVERIFY(!QFileInfo::exists(auxiliaryPaths(dir)[i]));
            else QCOMPARE(read(auxiliaryPaths(dir)[i]), QByteArray(step == 4 ? "new-" : "old-") + QByteArray::number(i));
        }
    }
    void lateFailureRollsBack_data() {
        QTest::addColumn<int>("step");
        for (int i = 0; i <= 4; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void lateFailureRollsBack() {
        QFETCH(int, step);
        const QString dir = fixture("rollback");
        ProjectSaveOutputs outputs;
        QVERIFY2(prepare(outputs, dir), qPrintable(outputs.error()));
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [step](int n) { return n != step; });
        QVERIFY(!outputs.publish());
        QVERIFY2(outputs.error().contains("restored"), qPrintable(outputs.error()));
        QVERIFY(!outputs.recoveryRequired());
        for (int i = 0; i < 3; ++i) QCOMPARE(read(paths(dir)[i]), QByteArray("old-") + QByteArray::number(i));
        QVERIFY(!QFileInfo::exists(paths(dir)[3]));
        QVERIFY(!QFileInfo::exists(paths(dir)[0] + ".openswmm-save.json"));
    }
    void interruptedSaveRecovers_data() {
        QTest::addColumn<int>("step");
        for (int i = 0; i <= 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void interruptedSaveRecovers() {
        QFETCH(int, step);
        const QString dir = fixture("interrupted");
        QVERIFY(crash(dir, step));
        QString error, notice;
        QVERIFY2(ProjectSaveOutputs::recover(paths(dir)[0], &error, &notice), qPrintable(error));
        QVERIFY(!notice.isEmpty());
        for (int i = 0; i < 4; ++i) {
            if (step < 5 && i == 3) QVERIFY(!QFileInfo::exists(paths(dir)[i]));
            else QCOMPARE(read(paths(dir)[i]), QByteArray(step == 5 ? "new-" : "old-") + QByteArray::number(i));
        }
        QVERIFY2(ProjectSaveOutputs::recover(paths(dir)[0], &error), qPrintable(error));
        QVERIFY(QDir(dir).entryList({"*.openswmm-save*", ".openswmm-stage-*"}, QDir::Files | QDir::Dirs | QDir::Hidden).isEmpty());
    }
    void interruptedNewSaveAsRecovers_data() { interruptedSaveRecovers_data(); }
    void interruptedNewSaveAsRecovers() {
        QFETCH(int, step);
        const QString dir = fixture("new-save-as");
        QVERIFY(crash(dir, step, true));
        QString error;
        QVERIFY2(ProjectSaveOutputs::recover(paths(dir)[0], &error), qPrintable(error));
        QCOMPARE(QFileInfo::exists(paths(dir)[0]), step == 5);
        QCOMPARE(QFileInfo::exists(paths(dir)[3]), step == 5);
        for (int i : {1, 2}) QCOMPARE(read(paths(dir)[i]), QByteArray(step == 5 ? "new-" : "old-") + QByteArray::number(i));
    }
    void rollbackConflictRetainsRecoveryFiles() {
        const QString dir = fixture("rollback-conflict");
        {
            ProjectSaveOutputs outputs;
            QVERIFY(prepare(outputs, dir));
            ProjectSaveOutputsTestAccess::checkpoint(outputs, [dir](int n) {
                if (n != 2) return true;
                put(paths(dir)[2], "external edit");
                return false;
            });
            QVERIFY(!outputs.publish());
            QVERIFY(outputs.recoveryRequired());
            QVERIFY(outputs.error().contains("Recovery files retained"));
        }
        QCOMPARE(read(paths(dir)[2]), QByteArray("external edit"));
        QVERIFY(put(paths(dir)[2], "new-2"));
        QString error;
        QVERIFY2(ProjectSaveOutputs::recover(paths(dir)[0], &error), qPrintable(error));
    }
    void changedDestinationIsPreserved() {
        const QString dir = fixture("conflict");
        QVERIFY(crash(dir, 2));
        QVERIFY(put(paths(dir)[2], "external edit"));
        QString error;
        QVERIFY(!ProjectSaveOutputs::recover(paths(dir)[0], &error));
        QVERIFY(error.contains("changed destination"));
        QCOMPARE(read(paths(dir)[2]), QByteArray("external edit"));
        QCOMPARE(read(paths(dir)[0]), QByteArray("old-0"));
        QVERIFY(QFileInfo::exists(paths(dir)[3])); // no partial repair before conflict detection
        QVERIFY(QFileInfo::exists(paths(dir)[0] + ".openswmm-save.json"));
        QVERIFY(put(paths(dir)[2], "new-2"));
        QVERIFY2(ProjectSaveOutputs::recover(paths(dir)[0], &error), qPrintable(error));
    }
    void damagedBackupIsPreserved() {
        const QString dir = fixture("damaged");
        QVERIFY(crash(dir, 2));
        const QString journal = paths(dir)[0] + ".openswmm-save.json";
        const auto obj = QJsonDocument::fromJson(read(journal)).object();
        const QString backup = dir + '/' + obj.value("data").toString() + "/0.old";
        QVERIFY(put(backup, "damaged"));
        QString error;
        QVERIFY(!ProjectSaveOutputs::recover(paths(dir)[0], &error));
        QVERIFY(error.contains("damaged"));
        QVERIFY(QFileInfo::exists(journal));
        QCOMPARE(read(paths(dir)[2]), QByteArray("new-2"));
    }
    void malformedJournalIsPreserved() {
        const QString dir = fixture("invalid");
        const QString model = paths(dir)[0];
        QVERIFY(put(model, "old"));
        QVERIFY(put(model + ".openswmm-save.json", "{ invalid"));
        QString error;
        QVERIFY(!ProjectSaveOutputs::recover(model, &error));
        QVERIFY(error.contains("Invalid"));
        QCOMPARE(read(model), QByteArray("old"));
        ProjectSaveOutputs outputs;
        QVERIFY(outputs.stage(model, ProjectSaveOutputs::Model).isEmpty());
        QVERIFY(outputs.error().contains("Reopen"));
    }
    void stagingDetectsExternalEdit() {
        const QString dir = fixture("stale");
        ProjectSaveOutputs outputs;
        QVERIFY(prepare(outputs, dir));
        QVERIFY(put(paths(dir)[0], "external edit"));
        QVERIFY(!outputs.publish());
        QVERIFY(outputs.error().contains("changed while"));
        QVERIFY(!outputs.publicationStarted());
        QCOMPARE(read(paths(dir)[0]), QByteArray("external edit"));
        QCOMPARE(read(paths(dir)[2]), QByteArray("old-2"));
    }
    void competingSaveIsRefused() {
        const QString dir = fixture("locked");
        ProjectSaveOutputs first, second;
        QVERIFY(prepare(first, dir));
        QVERIFY(second.stage(paths(dir)[0], ProjectSaveOutputs::Model).isEmpty());
        QVERIFY(second.error().contains("Another operation"));
    }
    void damagedPreparedCopyNeverReachesDestination() {
        const QString dir = fixture("damaged-new");
        ProjectSaveOutputs outputs;
        QVERIFY(prepare(outputs, dir));
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [dir](int n) {
            if (n != 0) return true;
            const auto journal = QJsonDocument::fromJson(read(paths(dir)[0] + ".openswmm-save.json")).object();
            return put(dir + '/' + journal.value("data").toString() + "/0.new", "damaged new bytes");
        });
        QVERIFY(!outputs.publish());
        QVERIFY(outputs.error().contains("source changed"));
        QVERIFY(outputs.error().contains("restored"));
        for (int i = 0; i < 3; ++i) QCOMPARE(read(paths(dir)[i]), QByteArray("old-") + QByteArray::number(i));
        QVERIFY(!QFileInfo::exists(paths(dir)[3]));
    }
    void realPublicationFailureRollsBack() {
        const QString dir = fixture("io-failure");
        {
            ProjectSaveOutputs outputs;
            QVERIFY(prepare(outputs, dir));
            ProjectSaveOutputsTestAccess::checkpoint(outputs, [dir](int n) {
                if (n == 1) return put(dir + "/new", "directory blocker");
                return true;
            });
            QVERIFY(!outputs.publish());
            QVERIFY2(outputs.error().contains("restored"), qPrintable(outputs.error()));
            for (int i = 0; i < 3; ++i) QCOMPARE(read(paths(dir)[i]), QByteArray("old-") + QByteArray::number(i));
            QCOMPARE(read(dir + "/new"), QByteArray("directory blocker"));
        }
        QVERIFY(QFile::remove(dir + "/new"));
        ProjectSaveOutputs retry;
        QVERIFY(prepare(retry, dir));
        QVERIFY2(retry.publish(), qPrintable(retry.error()));
        for (int i = 0; i < 4; ++i) QCOMPARE(read(paths(dir)[i]), QByteArray("new-") + QByteArray::number(i));
    }
    void ordinaryReadOnlyOpenNeedsNoLock() {
        const QString dir = fixture("readonly");
        QVERIFY(put(paths(dir)[0], "old"));
        QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::ExeOwner));
        QString error;
        const bool ok = ProjectSaveOutputs::recover(paths(dir)[0], &error);
        QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY2(ok, qPrintable(error));
    }
    void journalCannotRedirectBackupDirectory() {
        const QString dir = fixture("journal-path");
        QVERIFY(crash(dir, 1));
        const QString journal = paths(dir)[0] + ".openswmm-save.json";
        auto obj = QJsonDocument::fromJson(read(journal)).object();
        obj.insert("data", "../unowned");
        QVERIFY(put(journal, QJsonDocument(obj).toJson()));
        QString error;
        QVERIFY(!ProjectSaveOutputs::recover(paths(dir)[0], &error));
        QVERIFY(error.contains("Invalid"));
        QCOMPARE(read(paths(dir)[2]), QByteArray("new-2"));
    }
    void symlinkSavePreservesLink() {
        const QString dir = fixture("symlink");
        const QString actual = dir + "/actual.inp", link = paths(dir)[0];
        QVERIFY(put(actual, "old"));
        std::error_code ec;
        std::filesystem::create_symlink(actual.toStdString(), link.toStdString(), ec);
        if (ec) QSKIP("Symlinks not available");
        ProjectSaveOutputs outputs;
        const QString stage = outputs.stage(link, ProjectSaveOutputs::Model);
        QVERIFY(!stage.isEmpty()); QVERIFY(put(stage, "new"));
        QVERIFY2(outputs.publish(), qPrintable(outputs.error()));
        QVERIFY(QFileInfo(link).isSymLink()); QCOMPARE(read(actual), QByteArray("new"));
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString mode = app.arguments().value(1);
    if (mode == "--interrupt-aux-save") {
        ProjectSaveOutputs outputs;
        if (!prepareAuxiliary(outputs, app.arguments().value(2))) return 70;
        const int step = app.arguments().value(3).toInt();
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [step](int n) {
            if (n == step) std::_Exit(86);
            return true;
        });
        return outputs.publish() ? 0 : 71;
    }
    if (mode == "--interrupt-save" || mode == "--interrupt-new-save") {
        ProjectSaveOutputs outputs;
        if (!prepare(outputs, app.arguments().value(2), mode == "--interrupt-new-save")) return 70;
        const int step = app.arguments().value(3).toInt();
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [step](int n) {
            if (n == step) std::_Exit(86); // no destructors: exercise actual restart recovery
            return true;
        });
        return outputs.publish() ? 0 : 71;
    }
    if (mode == "--make-manual-fixture") {
        const QString model = QFileInfo(app.arguments().value(2)).absoluteFilePath();
        if (!QFileInfo::exists(model)) return 72;
        ProjectSaveOutputs outputs;
        const QString staged = outputs.stage(model, ProjectSaveOutputs::Model);
        if (staged.isEmpty() || !put(staged, read(model) + "\n; interrupted Phase 19 fixture\n")) return 73;
        const QString sidecar = QFileInfo(model).absolutePath() + '/' + QFileInfo(model).completeBaseName() + ".oswp";
        if (QFileInfo::exists(sidecar)) {
            const QString settings = outputs.stage(sidecar, ProjectSaveOutputs::Settings);
            if (settings.isEmpty() || !put(settings, read(sidecar) + "\n")) return 74;
        }
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [](int n) {
            if (n == 1) std::_Exit(86);
            return true;
        });
        return outputs.publish() ? 0 : 75;
    }
    TestProjectSaveOutputs tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_projectsaveoutputs.moc"
