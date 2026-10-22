/* SPDX-License-Identifier: GPL-3.0-or-later */

/**
 * @file      test_checkpointmodel.cpp
 * @brief     Tests for checkpointmodel.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QTableView>
#include <QTextBrowser>
#include <QComboBox>
#include <QPushButton>
#include <QImage>
#include <QStandardItemModel>
#include <openswmm/engine/openswmm_checkpoint.h>
#include "simulation/checkpointmodel.h"
#include "simulation/runpathlease.h"
#include "simulation/simulationrunner.h"
#include "ui/dialogs/checkpointsdialog.h"
#include "core/swmmdatetime.h"
#include <filesystem>

namespace
{
QString root()
{
    const auto p = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
                       .filePath("output_checkpointmodel");
    QDir().mkpath(p);
    return p;
}
QString put(const QString &name, const QByteArray &bytes)
{
    const auto p = QDir(root()).filePath(name);
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        qFatal("cannot create test file");
    if (f.write(bytes) != bytes.size())
        qFatal("write test file");
    return p;
}
QByteArray bytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
QJsonObject metadata(const QString &companion)
{
    const double date = 46307.0;
    return {{"schema_version", 1},
            {"engine_seconds", 12.5},
            {"engine_date", date + 12.5 / 86400},
            {"start_date", date},
            {"completed_source_seconds", 12.0},
            {"checkpoint_bytes", "3"},
            {"source_frames", "9007199254740993"},
            {"source_prefix_bytes", "2"},
            {"next_report_seconds", 15.0},
            {"driver", QJsonValue::Null},
            {"file_outputs", true},
            {"relocatable_bundle", false},
            {"binary", QJsonObject{{"enabled", true},
                                   {"records", "0"},
                                   {"cadence_seconds", 2.0},
                                   {"first_date", QJsonValue::Null},
                                   {"last_date", QJsonValue::Null}}},
            {"mesh", QJsonObject{{"enabled", true},
                                 {"records", "2"},
                                 {"cadence_seconds", 3.0},
                                 {"first_date", date + 1.001 / 86400},
                                 {"last_date", date + 4.001 / 86400}}},
            {"companions", QJsonArray{QJsonObject{
                               {"role", "source"}, {"path", companion}, {"required_bytes", "2"}}}}};
}
QString catalog(const QString &name, const QStringList &paths, const QString &companion)
{
    QJsonArray entries;
    for (int i = 0; i < paths.size(); ++i)
        entries.append(QJsonObject{{"id", QString::number(i)},
                                   {"name", QString("Checkpoint %1").arg(i)},
                                   {"path", paths[i]},
                                   {"publication", QString::number(i + 1)},
                                   {"lineage", i == 4 ? "branch" : "run"},
                                   {"pinned", i == 0},
                                   {"metadata", metadata(companion)}});
    return put(name, QJsonDocument(QJsonObject{{"schema", 1}, {"entries", entries}}).toJson());
}
CheckpointRequest request(const QString &name)
{
    CheckpointRequest r;
    r.projectRevision = "snapshot-1";
    r.stagingRoot = root();
    r.modelPath = put(
        name + ".inp",
        "[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\nSTART_DATE 10/12/2026\nEND_DATE 10/12/2026\nEND_TIME 00:10:00\nROUTING_STEP .01\nVARIABLE_STEP 0\nREPORT_STEP 00:00:01\nIGNORE_QUALITY YES\nIGNORE_2D YES\nIGNORE_SNOWMELT YES\nIGNORE_GROUNDWATER YES\n[STORAGE]\nS 0 2 1 FUNCTIONAL 0 0 10 0 0\n[OUTFALLS]\nO -1 FREE NO\n[CONDUITS]\nC S O 10 .013 .1 0 0 0\n[XSECTIONS]\nC CIRCULAR .3 0 0 0\n");
    r.modelSha256 = QCryptographicHash::hash(bytes(r.modelPath), QCryptographicHash::Sha256);
    return r;
}
}
class TestCheckpointModel : public QObject
{
    Q_OBJECT
private slots:
    void millisecondWallClockCoverageAndSources()
    {
        using openswmmvis::core::swmmDateTimeToMilliseconds;
        const auto epoch = swmmDateTimeToMilliseconds(0);
        QCOMPARE(epoch.date(), QDate(1899, 12, 30));
        double d = 0;
        swmm_datetime_encode_date(2026, 12, 31, &d);
        QCOMPARE(
            swmmDateTimeToMilliseconds(d + 86399.9996 / 86400).toString("yyyy-MM-dd HH:mm:ss.zzz"),
            QString("2027-01-01 00:00:00.000"));
        QCOMPARE(swmmDateTimeToMilliseconds(d + 1.001 / 86400).time(), QTime(0, 0, 1, 1));
        QVERIFY(!swmmDateTimeToMilliseconds(qQNaN()).isValid());
        QCOMPARE(swmmDateTimeToMilliseconds(-1.5),
                 QDateTime(QDate(1899, 12, 28), QTime(12, 0), Qt::UTC));
        QCOMPARE(swmmDateTimeToMilliseconds(QDate(1899, 12, 30).daysTo(QDate(1, 1, 1))).date(),
                 QDate(1, 1, 1));
        const auto previous = qgetenv("TZ");
        qputenv("TZ", "America/New_York");
        double dst = 0;
        swmm_datetime_encode_date(2026, 3, 8, &dst);
        QCOMPARE(swmmDateTimeToMilliseconds(dst + 2.5 / 24).time(), QTime(2, 30));
        qputenv("TZ", previous);
        auto m = metadata("/unused");
        QVERIFY(CheckpointModel::coverage(m.value("binary").toObject()).contains("Enabled · 0"));
        QVERIFY(CheckpointModel::coverage(m.value("mesh").toObject()).contains("00:00:01.001"));
        QVERIFY(CheckpointModel::coverage(QJsonObject{{"enabled", false}}) == "Disabled");
        QVERIFY(CheckpointModel::clocks(m).contains("pending interval 0.5 s"));
        m["completed_source_seconds"] = QJsonValue::Null;
        QVERIFY(CheckpointModel::clocks(m).contains("Not applicable"));
        m["driver"] = QJsonObject{{"identity", "driver-test"},
                                  {"next_sequence", "9007199254740993"},
                                  {"next_update_seconds", 12.5}};
        QVERIFY(CheckpointModel::clocks(m).contains("Due now"));
        QVERIFY(CheckpointModel::clocks(m).contains("9007199254740993"));
        m["driver"] = QJsonObject{{"next_update_seconds", -1}};
        QVERIFY(CheckpointModel::clocks(m).contains("Exhausted"));
    }
    void retentionProtectsPinsBranchesUnknownMissingAndActiveGroups()
    {
        auto companion = put("retention.history", "later live tail");
        QStringList paths;
        for (int i = 0; i < 5; ++i)
            paths << put(QString("retention-%1.chk").arg(i), "chk");
        CheckpointModel model(catalog("retention.json", paths, companion));
        QCOMPARE(model.suggestions(1), QList<int>({2, 1}));
        QVERIFY(model.suggestions(0).isEmpty());
        QVERIFY(model.importFile(put("unknown-lineage.chk", "saved")));
        QCOMPARE(model.suggestions(1), QList<int>({2, 1}));
        model.setInventory({}, QStringList{companion});
        QVERIFY(model.suggestions(1).isEmpty());
        model.setInventory({}, {});
        QFile::remove(paths[3]);
        QCOMPARE(model.suggestions(1), QList<int>({1}));
        QVERIFY(model.setData(model.index(1, 4), Qt::Checked, Qt::CheckStateRole));
        QVERIFY(model.suggestions(1).isEmpty());
        const auto old = bytes(paths[2]);
        QVERIFY(model.forget(2));
        QCOMPARE(bytes(paths[2]), old);
    }
    void storageDeduplicatesHardlinksAndDistinguishesRequiredFromLiveBytes()
    {
        const auto source = put("shared-source.history", "0123456789"),
                   a = put("shared-a.chk", "chk");
        const auto b = QDir(root()).filePath("shared-b.chk");
        std::filesystem::create_hard_link(RunPathLease::nativePath(a), RunPathLease::nativePath(b));
        CheckpointModel model(catalog("storage.json", {a, b}, source));
        const auto storage = model.storage({0, 1});
        QCOMPARE(storage.bytes, quint64(13));
        QVERIFY(storage.complete);
        QCOMPARE(storage.paths.size(), 2);
        const auto files = model.files(0);
        QCOMPARE(files[1].requiredBytes, QString("2"));
        QCOMPARE(files[1].currentBytes, QString("10"));
        auto old = bytes(source);
        QVERIFY(model.suggestions(1).isEmpty());
        QCOMPARE(bytes(source), old);
        QFile::remove(source);
        QVERIFY(!model.storage({0, 1}).complete);
        QVERIFY(model.suggestions(1).isEmpty());
    }
    void sharedViewsStaySynchronizedAndNarrowLayoutIsReviewable()
    {
        const auto source = put("views.history", "history"), cp = put("views.chk", "chk");
        CheckpointModel model(catalog("views.json", {cp}, source));
        model.setCapabilities(SWMM_CHECKPOINT_SESSION_BUILDER | SWMM_CHECKPOINT_BINARY_PROFILE |
                              SWMM_CHECKPOINT_SURFACE_PROFILE | SWMM_CHECKPOINT_FILE_PROFILE |
                              SWMM_CHECKPOINT_SOURCE_STREAM | SWMM_CHECKPOINT_BUNDLE_EXPORT);
        CheckpointRequest r;
        r.outputProfile = 4;
        r.sourceHistory = true;
        model.setContext(r);
        CheckpointsDialog first(&model), second(&model);
        first.resize(560, 620);
        first.show();
        second.show();
        auto *list = first.findChild<QTableView *>("checkpointList");
        list->selectRow(0);
        QCoreApplication::processEvents();
        QVERIFY(first.findChild<QTextBrowser *>("checkpointCoverage")
                    ->toPlainText()
                    .contains("Enabled · 0"));
        QVERIFY(first.findChild<QTextBrowser *>("checkpointFiles")
                    ->toPlainText()
                    .contains("Current file bytes"));
        QVERIFY(first.findChild<QTextBrowser *>("checkpointStorage")
                    ->toPlainText()
                    .contains("No files are deleted"));
        auto *profile = first.findChild<QComboBox *>("checkpointProfile");
        profile->setCurrentIndex(3);
        QCOMPARE(second.findChild<QComboBox *>("checkpointProfile")->currentIndex(), 3);
        QVERIFY(model.setData(model.index(0, 4), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(second.findChild<QTableView *>("checkpointList")
                     ->model()
                     ->index(0, 4)
                     .data(Qt::CheckStateRole)
                     .toInt(),
                 int(Qt::Checked));
        for (auto *button : first.findChildren<QPushButton *>()) {
            QVERIFY(button->isVisible());
            QVERIFY(button->width() >= button->minimumSizeHint().width());
        }
        QVERIFY(first.grab().save(QDir(root()).filePath("checkpoints-narrow.png")));
        first.resize(960, 720);
        QCoreApplication::processEvents();
        QVERIFY(first.grab().save(QDir(root()).filePath("checkpoints-wide.png")));
        model.setCapabilities(SWMM_CHECKPOINT_SESSION_BUILDER | SWMM_CHECKPOINT_BINARY_PROFILE);
        auto *choices = qobject_cast<QStandardItemModel *>(profile->model());
        QVERIFY(!choices->item(2)->isEnabled());
        QVERIFY(!choices->item(4)->isEnabled());
    }
    void initialRunPublishesImmediatelyAndStaleResumeIsDisabled()
    {
        auto r = request("initial");
        const auto folder = QDir(root()).filePath("initial-run");
        CheckpointModel model(QDir(root()).filePath("initial-catalog.json"));
        model.setContext(r);
        QSignalSpy done(&model, &CheckpointModel::runFinished);
        QSignalSpy timeline(&model, &CheckpointModel::runTimeline);
        QVERIFY(model.startRun(-1, folder));
        CheckpointsDialog view(&model);
        view.show();
        view.close();
        QVERIFY(model.runBusy());
        const auto cp = QDir(folder).filePath("accepted.chk");
        QVERIFY(model.save(cp));
        QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 30000);
        QVERIFY(QFileInfo::exists(cp));
        QCOMPARE(model.entry(0)->publication, quint64(1));
        QVERIFY(!model.entry(0)->lineage.isEmpty());
        QVERIFY(!model.entry(0)->compatible);
        model.cancelRun();
        if (done.isEmpty())
            QVERIFY(done.wait(30000));
        QCOMPARE(timeline.size(), 1);
        auto result = qvariant_cast<CheckpointResult>(done.last()[0]);
        QVERIFY(result.cancelled);
        QVERIFY(!model.runBusy());
        CheckpointModel loaded(QDir(root()).filePath("initial-catalog.json"));
        QCOMPARE(loaded.rowCount(), 1);
        QVERIFY(!loaded.entry(0)->compatible);
        QVERIFY(model.inspectEntry(0));
        QTRY_VERIFY_WITH_TIMEOUT(!model.ioBusy(), 30000);
        QVERIFY(model.entry(0)->compatible);
        r.projectRevision = "edited";
        model.setContext(r);
        QVERIFY(!model.entry(0)->compatible);
        QVERIFY(!model.startRun(0, QDir(root()).filePath("stale-run")));
        QVERIFY(model.inspectEntry(0));
        r.projectRevision = "edited-again";
        model.setContext(r);
        QTRY_VERIFY_WITH_TIMEOUT(!model.ioBusy(), 30000);
        QVERIFY(!model.entry(0)->compatible);
    }
    void savedCheckpointSurvivesCatalogWriteFailureAndProjectClose()
    {
        auto r = request("list-failure");
        CheckpointModel model(QDir(root()).filePath("missing-parent/catalog.json"));
        model.setContext(r);
        QSignalSpy notices(&model, &CheckpointModel::notice),
            done(&model, &CheckpointModel::runFinished);
        const auto folder = QDir(root()).filePath("list-failure-run"),
                   cp = QDir(folder).filePath("published.chk");
        QVERIFY(model.startRun(-1, folder));
        QVERIFY(model.save(cp));
        QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 30000);
        bool reported = false;
        for (const auto &item : notices)
            if (item[0].toString().contains("Saved; list update failed") &&
                item[0].toString().contains(cp))
                reported = true;
        QVERIFY(reported);
        QVERIFY(QFileInfo::exists(cp));
        model.closeProject();
        if (done.isEmpty())
            QVERIFY(done.wait(30000));
        QVERIFY(QFileInfo::exists(cp));
        const auto shared = QDir(root()).filePath("concurrent-catalog.json");
        CheckpointModel first(shared), second(shared);
        QVERIFY(first.importFile(cp));
        const auto published = bytes(shared);
        QVERIFY(second.importFile(cp));
        QCOMPARE(bytes(shared), published);
        const auto invalid = put("invalid-catalog.json", "bad json");
        CheckpointModel corrupt(invalid);
        QVERIFY(corrupt.importFile(cp));
        QCOMPARE(bytes(invalid), QByteArray("bad json"));
    }
    void ordinaryRunnerAndCheckpointWorkersShareReservations()
    {
        auto r = request("ordinary-lease");
        RunPathLease lease(this);
        QString error;
        const auto folder = QDir(root()).filePath("leased-run");
        QVERIFY(lease.add(folder, true, &error));
        CheckpointWorker worker;
        r.operation = CheckpointRequest::Operation::Run;
        r.destination = folder;
        QSignalSpy done(&worker, &CheckpointWorker::finished);
        QVERIFY(worker.start(r));
        if (done.isEmpty())
            QVERIFY(done.wait(30000));
        auto result = qvariant_cast<CheckpointResult>(done.last()[0]);
        QVERIFY(!result.success);
        QVERIFY(result.error.contains("conflicts"));
        QVERIFY(!QFileInfo::exists(folder));
        const auto report = QDir(folder).filePath("ordinary.rpt"),
                   out = QDir(folder).filePath("ordinary.out");
        SimulationRunner runner(71, "lease test", r.modelPath, report, out, "6.0.0");
        QSignalSpy finished(&runner, &SimulationRunner::finished);
        runner.start();
        if (finished.isEmpty())
            QVERIFY(finished.wait(30000));
        QVERIFY(!finished.last()[1].toBool());
        QVERIFY(finished.last()[3].toString().contains("conflicts"));
        QVERIFY(!QFileInfo::exists(report));
    }
};
QTEST_MAIN(TestCheckpointModel)
#include "test_checkpointmodel.moc"
