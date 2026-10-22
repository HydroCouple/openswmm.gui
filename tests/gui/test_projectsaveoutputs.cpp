// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_projectsaveoutputs.cpp
 * @brief     Tests for projectsaveoutputs.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

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

/** @brief Overwrite one controlled fixture file with exact requested bytes.
 * @param[in] p Destination filename whose parent already exists.
 * @param[in] bytes Content borrowed for synchronous write.
 * @return True on open and exact write count; flush/close errors are unchecked.
 * Failures can leave truncated/partial output; no parent directory is created. */
static bool put(const QString &p, const QByteArray &bytes)
{
    QFile f(p);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
/** @brief Read current fixture bytes into owned storage.
 * @param[in] p Filename borrowed for synchronous read.
 * @return Bytes or empty on open refusal/empty file, with no separate partial-read status. */
static QByteArray read(const QString &p)
{
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
/** @brief Create/name a unique retained save-recovery case directory.
 * @param[in] name Controlled case prefix.
 * @return Owned configured-root/name-UUID directory path.
 * Root comes from SWMMVIS_SAVE_RECOVERY_OUTPUT or SAVE_RECOVERY_DATA_DIR; mkpath
 * is unchecked, the UUID does not atomically reserve a directory, and cwd is unchanged. */
static QString fixture(const QString &name)
{
    const QString root = qEnvironmentVariable("SWMMVIS_SAVE_RECOVERY_OUTPUT",
                                              QStringLiteral(SAVE_RECOVERY_DATA_DIR));
    const QString dir =
        root + '/' + name + '-' + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(dir);
    return dir;
}
/** @brief Build the ordered four-file recovery fixture destination list.
 * @param[in] dir Existing controlled case root.
 * @return Owned model.inp, mesh.2dm, config.rxn and new/settings.oswp paths in
 * that order. Does not create files or assign publication roles. */
static QStringList paths(const QString &dir)
{
    return {dir + "/model.inp", dir + "/mesh.2dm", dir + "/config.rxn", dir + "/new/settings.oswp"};
}
/** @brief Stage a four-output old/new sentinel save transaction.
 * @param[in,out] outputs Fresh staging transaction owning prepared private files.
 * @param[in] dir Existing controlled case root.
 * @param[in] newModel True leaves model destination initially absent; other old
 * mesh/config destinations are still written.
 * @return True when all sentinel writes/stages succeed, false after partial work.
 * Uses Model/Mesh/Component/Component roles; the nested settings-named artifact
 * is deliberately a component, since Settings must be adjacent to the model.
 * Does not publish or roll back before return; caller/transaction own cleanup. */
static bool prepare(ProjectSaveOutputs &outputs, const QString &dir, bool newModel = false)
{
    auto list = paths(dir);
    // Use a component for the nested new file: settings must be an adjacent sibling.
    const ProjectSaveOutputs::Role roles[] = {ProjectSaveOutputs::Model, ProjectSaveOutputs::Mesh,
                                              ProjectSaveOutputs::Component,
                                              ProjectSaveOutputs::Component};
    for (int i = 0; i < list.size(); ++i) {
        if (i < 3 && !(newModel && i == 0) &&
            !put(list[i], QByteArray("old-") + QByteArray::number(i)))
            return false;
        const QString stage = outputs.stage(list[i], roles[i]);
        if (stage.isEmpty() || !put(stage, QByteArray("new-") + QByteArray::number(i)))
            return false;
    }
    return true;
}
/** @brief Run a child test process that interrupts a controlled publication checkpoint.
 * @param[in] dir Existing expendable fixture directory passed to child preparation.
 * @param[in] step Checkpoint index intended to trigger immediate exit 86.
 * @param[in] newModel Select the initially absent model save-as mode.
 * @return True only when the child finishes within 15 s with exitCode 86.
 * Does not separately check exitStatus, validate the step, recover files or assert;
 * QProcess owns child cleanup and interrupted recovery artifacts remain. */
static bool crash(const QString &dir, int step, bool newModel = false)
{
    QProcess child;
    child.start(
        QCoreApplication::applicationFilePath(),
        {newModel ? "--interrupt-new-save" : "--interrupt-save", dir, QString::number(step)});
    return child.waitForFinished(15000) && child.exitCode() == 86;
}
/** @brief Build the ordered model/terrain/new-report auxiliary fixture paths.
 * @param[in] dir Controlled case root.
 * @return Owned model.inp, burn.tif and new/burn_report.csv paths; no I/O. */
static QStringList auxiliaryPaths(const QString &dir)
{
    return {dir + "/model.inp", dir + "/burn.tif", dir + "/new/burn_report.csv"};
}
/** @brief Stage one model and two generated auxiliary sentinel outputs.
 * @param[in,out] outputs Fresh transaction owning private stages.
 * @param[in] dir Existing controlled root.
 * @return True on every write/stage success, false after partial work.
 * Writes old model/terrain sentinels, leaves the nested report initially absent
 * and stages new bytes under Model/Auxiliary roles without publishing. */
static bool prepareAuxiliary(ProjectSaveOutputs &outputs, const QString &dir)
{
    const auto list = auxiliaryPaths(dir);
    for (int i = 0; i < list.size(); ++i) {
        if (i < 2 && !put(list[i], QByteArray("old-") + QByteArray::number(i)))
            return false;
        const auto role = i == 0 ? ProjectSaveOutputs::Model : ProjectSaveOutputs::Auxiliary;
        const QString stage = outputs.stage(list[i], role);
        if (stage.isEmpty() || !put(stage, QByteArray("new-") + QByteArray::number(i)))
            return false;
    }
    return true;
}

class TestProjectSaveOutputs : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Require a pending child generation to skip its parent's already published mesh/terrain and publish only its new report plus model.
     * After parent publication, externally edits terrain and creates its old guarded
     * companion; child must preserve both and retain the shared published mesh path.
     * Uses existing QTemporaryDir ownership for cleanup. */
    void inheritedOutputsSkipAnAlreadyPublishedParent()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString model = directory.filePath("model.inp");
        const QString mesh = directory.filePath("mesh.2dm");
        const QString terrain = directory.filePath("terrain.tif");
        const QString report = directory.filePath("new-report.csv");
        QString why;
        auto parent = GeneratedMeshArtifacts::create(model, &why);
        QVERIFY2(parent, qPrintable(why));
        QVERIFY(put(parent->reserve(mesh, "mesh.2dm", &why, ProjectSaveOutputs::Mesh),
                    "original mesh"));
        QVERIFY(put(parent->reserve(terrain, "terrain.tif", &why), "original terrain"));
        QVERIFY(parent->requireAbsent(terrain + ".aux.xml", &why));
        QVERIFY2(parent->seal(&why), qPrintable(why));
        auto child = GeneratedMeshArtifacts::create(model, &why);
        QVERIFY(child);
        QVERIFY2(child->inheritPending(parent, &why), qPrintable(why));
        QVERIFY(put(child->reserve(report, "new-report.csv", &why), "child report"));
        QVERIFY2(child->seal(&why), qPrintable(why));
        {
            ProjectSaveOutputs outputs;
            QVERIFY(put(outputs.stage(model, ProjectSaveOutputs::Model), "model"));
            QVERIFY2(parent->prepareSave(outputs, &why), qPrintable(why));
            QVERIFY2(outputs.publish(), qPrintable(outputs.error()));
            parent->markPublished(mesh);
        }
        QCOMPARE(child->publishedMeshPath(), mesh);
        QVERIFY(!child->isPublished());
        // A published auxiliary is independently editable; the still-pending
        // child must neither replay it nor recheck its old companion guard.
        QVERIFY(put(terrain, "edited published terrain"));
        QVERIFY(put(terrain + ".aux.xml", "published metadata"));
        ProjectSaveOutputs outputs;
        QVERIFY(put(outputs.stage(model, ProjectSaveOutputs::Model), "model"));
        QVERIFY2(child->prepareSave(outputs, &why), qPrintable(why));
        const auto pending = outputs.preparedOutputs();
        QCOMPARE(pending.size(), 2);
        QCOMPARE(pending.last().finalPath, report);
        QVERIFY2(outputs.publish(), qPrintable(outputs.error()));
        child->markPublished(mesh);
        QCOMPARE(read(terrain), QByteArray("edited published terrain"));
        QCOMPARE(read(report), QByteArray("child report"));
        QCOMPARE(parent->publishedMeshPath(), mesh);
    }
    /** @brief Require publishing an inherited pending child to publish its parent mesh and its own report together.
     * Checks three prepared outputs, both generation publication states and exact
     * mesh/report bytes; no model engine or simulation is involved. */
    void inheritedPublicationDoesNotConsumeUnpublishedAuxiliaries()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString why;
        const QString model = directory.filePath("model.inp"),
                      mesh = directory.filePath("mesh.2dm"),
                      report = directory.filePath("report.csv");
        auto parent = GeneratedMeshArtifacts::create(model, &why);
        QVERIFY(parent);
        QVERIFY(put(parent->reserve(mesh, "mesh.2dm", &why, ProjectSaveOutputs::Mesh), "mesh"));
        QVERIFY(parent->seal(&why));
        auto child = GeneratedMeshArtifacts::create(model, &why);
        QVERIFY(child);
        QVERIFY(child->inheritPending(parent, &why));
        QVERIFY(put(child->reserve(report, "report.csv", &why), "report"));
        QVERIFY(child->seal(&why));
        ProjectSaveOutputs outputs;
        QVERIFY(put(outputs.stage(model, ProjectSaveOutputs::Model), "model"));
        QVERIFY2(child->prepareSave(outputs, &why), qPrintable(why));
        QCOMPARE(outputs.preparedOutputs().size(), 3);
        QVERIFY(!parent->isPublished());
        QVERIFY(!child->isPublished());
        QVERIFY2(outputs.publish(), qPrintable(outputs.error()));
        child->markPublished(mesh);
        QVERIFY(parent->isPublished());
        QVERIFY(child->isPublished());
        QCOMPARE(read(mesh), QByteArray("mesh"));
        QCOMPARE(read(report), QByteArray("report"));
    }
    /** @brief Register nested/equal/symlink/late-registration/retargeted protected-directory refusals and a permitted sibling-prefix case. */
    void protectedDirectory_data()
    {
        QTest::addColumn<QString>("scenario");
        QTest::newRow("nested-output") << QString("nested");
        QTest::newRow("directory-itself") << QString("equal");
        QTest::newRow("symlink-ancestor") << QString("alias");
        QTest::newRow("registered-after-preparation") << QString("late");
        QTest::newRow("symlink-changed-after-preparation") << QString("retarget");
        QTest::newRow("sibling-prefix-allowed") << QString("sibling");
    }
    /** @brief Require staging/publication to honor protected directory identity before and after preparation.
     * Checks alias retargeting and late protection before any publication, preserving
     * old model bytes and absent protected outputs; only the sibling case publishes.
     * Creates real directory symlinks or skips when unsupported. */
    void protectedDirectory()
    {
        QFETCH(QString, scenario);
        const QString dir = fixture("protected-directory");
        const QString owned = dir + "/pending";
        QVERIFY(QDir().mkpath(owned));
        QString output = owned + "/nested/component.cfg";
        QString alias;
        if (scenario == "equal")
            output = owned;
        if (scenario == "sibling")
            output = dir + "/pending-sibling/component.cfg";
        if (scenario == "alias" || scenario == "retarget") {
            alias = dir + "/alias";
            const QString initial = scenario == "alias" ? owned : dir + "/initial";
            QVERIFY(QDir().mkpath(initial));
            std::error_code ec;
            std::filesystem::create_directory_symlink(initial.toStdString(), alias.toStdString(),
                                                      ec);
            if (ec)
                QSKIP("Directory symlinks not available");
            output = alias + "/nested/component.cfg";
        }
        ProjectSaveOutputs outputs;
        if (scenario != "late")
            outputs.protectDirectory(owned);
        const QString model = dir + "/model.inp";
        QVERIFY(put(model, "old model"));
        const QString modelStage = outputs.stage(model, ProjectSaveOutputs::Model);
        QVERIFY(!modelStage.isEmpty());
        QVERIFY(put(modelStage, "new model"));
        const QString componentStage = outputs.stage(output, ProjectSaveOutputs::Component);
        const bool stageAllowed =
            scenario == "late" || scenario == "retarget" || scenario == "sibling";
        QCOMPARE(!componentStage.isEmpty(), stageAllowed);
        if (!stageAllowed) {
            QVERIFY2(outputs.error().contains("protected directory"), qPrintable(outputs.error()));
            QVERIFY(!QFileInfo::exists(owned + "/nested"));
            QCOMPARE(read(model), QByteArray("old model"));
            return;
        }
        QVERIFY(put(componentStage, "prepared component"));
        if (scenario == "late")
            outputs.protectDirectory(owned);
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
    /** @brief Register existing/missing unchanged baselines, external edits/removal/creation, same-byte alias retargeting and stale reuse. */
    void generationBaseline_data()
    {
        QTest::addColumn<QString>("change");
        QTest::newRow("existing-unchanged") << QString("existing");
        QTest::newRow("missing-unchanged") << QString("missing");
        QTest::newRow("existing-edited") << QString("edit");
        QTest::newRow("existing-removed") << QString("remove");
        QTest::newRow("missing-created") << QString("create");
        QTest::newRow("alias-retargeted-same-bytes") << QString("alias");
        QTest::newRow("reuse-edited") << QString("reuse");
    }
    /** @brief Require generated auxiliary staging to match the captured destination identity/fingerprint.
     * Only unchanged existing/missing destinations may stage; conflicts preserve
     * external bytes and alias targets while reporting changed-since-generation.
     * Filesystem symlink unavailability skips its alias data case. */
    void generationBaseline()
    {
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
            if (ec)
                QSKIP("Symlinks not available");
        } else if (!absent)
            QVERIFY(put(output, "old"));
        ProjectSaveOutputs::DestinationState expected;
        QString error;
        QVERIFY2(ProjectSaveOutputs::captureDestination(output, &expected, &error),
                 qPrintable(error));
        QVERIFY(!expected.resolvedPath.isEmpty());
        QVERIFY(!expected.fingerprint.isEmpty());
        ProjectSaveOutputs outputs;
        if (change == "reuse")
            QVERIFY(!outputs.stage(output, ProjectSaveOutputs::Auxiliary).isEmpty());
        if (change == "edit" || change == "create" || change == "reuse")
            QVERIFY(put(output, "external edit"));
        else if (change == "remove")
            QVERIFY(QFile::remove(output));
        else if (change == "alias") {
            QVERIFY(QFile::remove(output));
            std::error_code ec;
            std::filesystem::create_symlink(newTarget.toStdString(), output.toStdString(), ec);
            QVERIFY(!ec);
        }
        const bool unchanged = change == "existing" || change == "missing";
        const QString stage =
            outputs.stage(output, ProjectSaveOutputs::Auxiliary, change == "reuse", &expected);
        QCOMPARE(!stage.isEmpty(), unchanged);
        if (!unchanged)
            QVERIFY2(outputs.error().contains("changed since generation"),
                     qPrintable(outputs.error()));
        if (change == "edit" || change == "create" || change == "reuse")
            QCOMPARE(read(output), QByteArray("external edit"));
        else if (change == "remove" || change == "missing")
            QVERIFY(!QFileInfo::exists(output));
        else
            QCOMPARE(read(output), QByteArray("old"));
        if (change == "alias") {
            QCOMPARE(read(oldTarget), QByteArray("old"));
            QCOMPARE(read(newTarget), QByteArray("old"));
            QVERIFY(QFileInfo(output).isSymLink());
        }
    }
    /** @brief Require destination-state capture and staging of an absent nested auxiliary to avoid creating its final parent tree.
     * Also checks directory/empty-path capture refusal and cleared resolved state. */
    void destinationCaptureDoesNotCreateDirectories()
    {
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
    /** @brief Require the staged model path to accept an engine-style replacement rename and publish new bytes.
     * On Unix scans open descriptors to ensure none holds the stage inode; checks
     * final bytes and stage removal after publication. Uses real filesystem rename. */
    void stagedPathIsReplaceableByRename()
    {
        const QString dir = fixture("staged-rename");
        ProjectSaveOutputs outputs;
        QVERIFY(put(dir + "/model.inp", "old"));
        const QString staged = outputs.stage(dir + "/model.inp", ProjectSaveOutputs::Model);
        QVERIFY2(!staged.isEmpty(), qPrintable(outputs.error()));
#ifdef Q_OS_UNIX
        struct stat target{};
        QVERIFY(::stat(QFile::encodeName(staged).constData(), &target) == 0);
        for (int fd = 0; fd < ::getdtablesize(); ++fd) {
            struct stat open{};
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
    /** @brief Require both auxiliary outputs to publish before the main model commit point.
     * Uses a test checkpoint callback at step two to inspect intermediate bytes,
     * then checks all three final new sentinels. */
    void auxiliaryPublication()
    {
        const QString dir = fixture("auxiliary-publish");
        ProjectSaveOutputs outputs;
        QVERIFY2(prepareAuxiliary(outputs, dir), qPrintable(outputs.error()));
        QVERIFY(!QFileInfo::exists(dir + "/new"));
        bool publishedBeforeModel = false;
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [&](int n) {
            if (n == 2)
                publishedBeforeModel = read(auxiliaryPaths(dir)[0]) == "old-0" &&
                                       read(auxiliaryPaths(dir)[1]) == "new-1" &&
                                       read(auxiliaryPaths(dir)[2]) == "new-2";
            return true;
        });
        QVERIFY2(outputs.publish(), qPrintable(outputs.error()));
        QVERIFY(publishedBeforeModel);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(read(auxiliaryPaths(dir)[i]), QByteArray("new-") + QByteArray::number(i));
    }
    /** @brief Require an injected step-two auxiliary publication refusal to restore old model/terrain and remove the newly created report. */
    void auxiliaryRollback()
    {
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
    /** @brief Register auxiliary interruption checkpoints zero through four. */
    void auxiliaryRecovery_data()
    {
        QTest::addColumn<int>("step");
        for (int i = 0; i <= 4; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    /** @brief Require restart recovery after real child exit 86 to restore old outputs before commit or retain all new outputs after commit.
     * Checks all three auxiliary fixture destinations for the selected checkpoint. */
    void auxiliaryRecovery()
    {
        QFETCH(int, step);
        const QString dir = fixture("auxiliary-recovery");
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),
                    {"--interrupt-aux-save", dir, QString::number(step)});
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitCode(), 86);
        QString error;
        QVERIFY2(ProjectSaveOutputs::recover(auxiliaryPaths(dir)[0], &error), qPrintable(error));
        for (int i = 0; i < 3; ++i) {
            if (step < 4 && i == 2)
                QVERIFY(!QFileInfo::exists(auxiliaryPaths(dir)[i]));
            else
                QCOMPARE(read(auxiliaryPaths(dir)[i]),
                         QByteArray(step == 4 ? "new-" : "old-") + QByteArray::number(i));
        }
    }
    /** @brief Register controlled four-output publication failure checkpoints zero through four. */
    void lateFailureRollsBack_data()
    {
        QTest::addColumn<int>("step");
        for (int i = 0; i <= 4; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    /** @brief Require each injected publication failure to restore three old files, remove the new nested artifact and clear the recovery journal.
     * Checks restored diagnostic and no retained recovery requirement. */
    void lateFailureRollsBack()
    {
        QFETCH(int, step);
        const QString dir = fixture("rollback");
        ProjectSaveOutputs outputs;
        QVERIFY2(prepare(outputs, dir), qPrintable(outputs.error()));
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [step](int n) { return n != step; });
        QVERIFY(!outputs.publish());
        QVERIFY2(outputs.error().contains("restored"), qPrintable(outputs.error()));
        QVERIFY(!outputs.recoveryRequired());
        for (int i = 0; i < 3; ++i)
            QCOMPARE(read(paths(dir)[i]), QByteArray("old-") + QByteArray::number(i));
        QVERIFY(!QFileInfo::exists(paths(dir)[3]));
        QVERIFY(!QFileInfo::exists(paths(dir)[0] + ".openswmm-save.json"));
    }
    /** @brief Register real four-output child interruption checkpoints zero through five. */
    void interruptedSaveRecovers_data()
    {
        QTest::addColumn<int>("step");
        for (int i = 0; i <= 5; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    /** @brief Require restart recovery to restore precommit state or retain committed new state after exit 86.
     * Checks notice, all four destinations, idempotent second recovery and removal of
     * journal/staging artifacts; retained fixture directory remains for review. */
    void interruptedSaveRecovers()
    {
        QFETCH(int, step);
        const QString dir = fixture("interrupted");
        QVERIFY(crash(dir, step));
        QString error, notice;
        QVERIFY2(ProjectSaveOutputs::recover(paths(dir)[0], &error, &notice), qPrintable(error));
        QVERIFY(!notice.isEmpty());
        for (int i = 0; i < 4; ++i) {
            if (step < 5 && i == 3)
                QVERIFY(!QFileInfo::exists(paths(dir)[i]));
            else
                QCOMPARE(read(paths(dir)[i]),
                         QByteArray(step == 5 ? "new-" : "old-") + QByteArray::number(i));
        }
        QVERIFY2(ProjectSaveOutputs::recover(paths(dir)[0], &error), qPrintable(error));
        QVERIFY(QDir(dir)
                    .entryList({"*.openswmm-save*", ".openswmm-stage-*"},
                               QDir::Files | QDir::Dirs | QDir::Hidden)
                    .isEmpty());
    }
    /** @brief Reuse the zero-through-five interruption dataset for an initially absent model save-as. */
    void interruptedNewSaveAsRecovers_data() { interruptedSaveRecovers_data(); }
    /** @brief Require interrupted first-save recovery to leave model/new artifact absent before commit and present after commit, preserving expected mesh/config bytes. */
    void interruptedNewSaveAsRecovers()
    {
        QFETCH(int, step);
        const QString dir = fixture("new-save-as");
        QVERIFY(crash(dir, step, true));
        QString error;
        QVERIFY2(ProjectSaveOutputs::recover(paths(dir)[0], &error), qPrintable(error));
        QCOMPARE(QFileInfo::exists(paths(dir)[0]), step == 5);
        QCOMPARE(QFileInfo::exists(paths(dir)[3]), step == 5);
        for (int i : {1, 2})
            QCOMPARE(read(paths(dir)[i]),
                     QByteArray(step == 5 ? "new-" : "old-") + QByteArray::number(i));
    }
    /** @brief Require rollback to retain recovery evidence rather than overwrite an external destination edit.
     * After proving external bytes survive destructor cleanup, restores the expected
     * new sentinel and checks explicit recovery can then complete. */
    void rollbackConflictRetainsRecoveryFiles()
    {
        const QString dir = fixture("rollback-conflict");
        {
            ProjectSaveOutputs outputs;
            QVERIFY(prepare(outputs, dir));
            ProjectSaveOutputsTestAccess::checkpoint(outputs, [dir](int n) {
                if (n != 2)
                    return true;
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
    /** @brief Require recovery preflight to refuse an externally changed destination without partially repairing other outputs.
     * Checks journal/new nested file retention, then restores the expected new bytes
     * and retries recovery successfully. */
    void changedDestinationIsPreserved()
    {
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
    /** @brief Require a damaged old-backup payload to stop recovery and retain journal/current new destination bytes. */
    void damagedBackupIsPreserved()
    {
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
    /** @brief Require malformed journal JSON to preserve original model bytes and block a new save with a reopen diagnostic. */
    void malformedJournalIsPreserved()
    {
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
    /** @brief Require publication to refuse a model changed after staging before publication starts, preserving external and other old bytes. */
    void stagingDetectsExternalEdit()
    {
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
    /** @brief Require a second transaction to refuse staging the same model while the first transaction owns its save lock. */
    void competingSaveIsRefused()
    {
        const QString dir = fixture("locked");
        ProjectSaveOutputs first, second;
        QVERIFY(prepare(first, dir));
        QVERIFY(second.stage(paths(dir)[0], ProjectSaveOutputs::Model).isEmpty());
        QVERIFY(second.error().contains("Another operation"));
    }
    /** @brief Require corruption of a journal-owned new payload to be detected and rolled back before damaged bytes reach a final destination. */
    void damagedPreparedCopyNeverReachesDestination()
    {
        const QString dir = fixture("damaged-new");
        ProjectSaveOutputs outputs;
        QVERIFY(prepare(outputs, dir));
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [dir](int n) {
            if (n != 0)
                return true;
            const auto journal =
                QJsonDocument::fromJson(read(paths(dir)[0] + ".openswmm-save.json")).object();
            return put(dir + '/' + journal.value("data").toString() + "/0.new",
                       "damaged new bytes");
        });
        QVERIFY(!outputs.publish());
        QVERIFY(outputs.error().contains("source changed"));
        QVERIFY(outputs.error().contains("restored"));
        for (int i = 0; i < 3; ++i)
            QCOMPARE(read(paths(dir)[i]), QByteArray("old-") + QByteArray::number(i));
        QVERIFY(!QFileInfo::exists(paths(dir)[3]));
    }
    /** @brief Require an actual final-parent file blocker to trigger rollback, preserving old destinations and blocker bytes.
     * Removes the blocker and retries a complete publication successfully afterward. */
    void realPublicationFailureRollsBack()
    {
        const QString dir = fixture("io-failure");
        {
            ProjectSaveOutputs outputs;
            QVERIFY(prepare(outputs, dir));
            ProjectSaveOutputsTestAccess::checkpoint(outputs, [dir](int n) {
                if (n == 1)
                    return put(dir + "/new", "directory blocker");
                return true;
            });
            QVERIFY(!outputs.publish());
            QVERIFY2(outputs.error().contains("restored"), qPrintable(outputs.error()));
            for (int i = 0; i < 3; ++i)
                QCOMPARE(read(paths(dir)[i]), QByteArray("old-") + QByteArray::number(i));
            QCOMPARE(read(dir + "/new"), QByteArray("directory blocker"));
        }
        QVERIFY(QFile::remove(dir + "/new"));
        ProjectSaveOutputs retry;
        QVERIFY(prepare(retry, dir));
        QVERIFY2(retry.publish(), qPrintable(retry.error()));
        for (int i = 0; i < 4; ++i)
            QCOMPARE(read(paths(dir)[i]), QByteArray("new-") + QByteArray::number(i));
    }
    /** @brief Require no-journal recovery/open to succeed in an owner-read/execute-only directory.
     * Restores owner-write permission before asserting the result; does not create a
     * journal or run an engine. */
    void ordinaryReadOnlyOpenNeedsNoLock()
    {
        const QString dir = fixture("readonly");
        QVERIFY(put(paths(dir)[0], "old"));
        QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::ExeOwner));
        QString error;
        const bool ok = ProjectSaveOutputs::recover(paths(dir)[0], &error);
        QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY2(ok, qPrintable(error));
    }
    /** @brief Require a journal data-directory traversal edit to fail validation without changing the already published component bytes. */
    void journalCannotRedirectBackupDirectory()
    {
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
    /** @brief Require saving through a model symlink to update its target while retaining the link object.
     * Uses actual filesystem symlink creation or skips when unavailable. */
    void symlinkSavePreservesLink()
    {
        const QString dir = fixture("symlink");
        const QString actual = dir + "/actual.inp", link = paths(dir)[0];
        QVERIFY(put(actual, "old"));
        std::error_code ec;
        std::filesystem::create_symlink(actual.toStdString(), link.toStdString(), ec);
        if (ec)
            QSKIP("Symlinks not available");
        ProjectSaveOutputs outputs;
        const QString stage = outputs.stage(link, ProjectSaveOutputs::Model);
        QVERIFY(!stage.isEmpty());
        QVERIFY(put(stage, "new"));
        QVERIFY2(outputs.publish(), qPrintable(outputs.error()));
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(read(actual), QByteArray("new"));
    }
};
/** @brief Dispatch save-recovery child fixture modes or execute the QtTest suite.
 * @param[in] argc Argument count supplied to QCoreApplication.
 * @param[in] argv Process-lifetime argument vector consumed by Qt and mode parsing.
 * @return QtTest status for normal mode; child modes return 70/71 for preparation/
 * publication failure, 72..75 for manual-fixture failure, zero for publication
 * success, or terminate immediately with code 86 at the selected checkpoint.
 * Interrupt modes deliberately bypass destructors through _Exit to retain real
 * restart evidence. Manual mode mutates the explicitly named existing model and
 * optional adjacent sidecar, stages an appended fixture comment and interrupts
 * after step one; missing/malformed numeric step text defaults through toInt.
 * All mode files are caller-selected expendable review artifacts. */
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString mode = app.arguments().value(1);
    if (mode == "--interrupt-aux-save") {
        ProjectSaveOutputs outputs;
        if (!prepareAuxiliary(outputs, app.arguments().value(2)))
            return 70;
        const int step = app.arguments().value(3).toInt();
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [step](int n) {
            if (n == step)
                std::_Exit(86);
            return true;
        });
        return outputs.publish() ? 0 : 71;
    }
    if (mode == "--interrupt-save" || mode == "--interrupt-new-save") {
        ProjectSaveOutputs outputs;
        if (!prepare(outputs, app.arguments().value(2), mode == "--interrupt-new-save"))
            return 70;
        const int step = app.arguments().value(3).toInt();
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [step](int n) {
            if (n == step)
                std::_Exit(86); // no destructors: exercise actual restart recovery
            return true;
        });
        return outputs.publish() ? 0 : 71;
    }
    if (mode == "--make-manual-fixture") {
        const QString model = QFileInfo(app.arguments().value(2)).absoluteFilePath();
        if (!QFileInfo::exists(model))
            return 72;
        ProjectSaveOutputs outputs;
        const QString staged = outputs.stage(model, ProjectSaveOutputs::Model);
        if (staged.isEmpty() || !put(staged, read(model) + "\n; interrupted Phase 19 fixture\n"))
            return 73;
        const QString sidecar =
            QFileInfo(model).absolutePath() + '/' + QFileInfo(model).completeBaseName() + ".oswp";
        if (QFileInfo::exists(sidecar)) {
            const QString settings = outputs.stage(sidecar, ProjectSaveOutputs::Settings);
            if (settings.isEmpty() || !put(settings, read(sidecar) + "\n"))
                return 74;
        }
        ProjectSaveOutputsTestAccess::checkpoint(outputs, [](int n) {
            if (n == 1)
                std::_Exit(86);
            return true;
        });
        return outputs.publish() ? 0 : 75;
    }
    TestProjectSaveOutputs tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_projectsaveoutputs.moc"
