// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_modelcheck.cpp
 * @brief     Check Model (review G-09): message parsing, object links, the engine check and the
 *            results window.
 * @details   Fixtures: tests/gui/data/model_check/. Copies and check folders are written under
 *            tests/output/model_check_g09/ so they can be reviewed.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "simulation/modelcheck.h"
#include "ui/dialogs/modelcheckdialog.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QUuid>

#include <openswmm/engine/openswmm_engine.h>

using openswmmvis::simulation::ModelCheckIssue;
using openswmmvis::ui::ModelCheckDialog;
using openswmmvis::ui::ModelCheckTableModel;
using Severity = ModelCheckIssue::Severity;
using Candidates = QList<QPair<SWMMObjectRef::ObjectType, QString>>;

namespace
{
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/** Fresh output folder tests/output/model_check_g09/<name>. */
QString outputDir(const QString &name)
{
    const QString dir = QDir(dataDir()).absoluteFilePath(
        QStringLiteral("../../output/model_check_g09/") + name);
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    return dir;
}

/** Copy a model_check fixture to @p path. */
bool copyFixture(const QString &fixture, const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile::remove(path);
    return QFile::copy(QDir(dataDir()).filePath(QStringLiteral("model_check/") + fixture), path);
}

/** A live model engine as the GUI holds it (lenient open). */
struct LiveEngine {
    SWMM_Engine engine = nullptr;
    explicit LiveEngine(const QString &inp)
    {
        engine = swmm_engine_create();
        swmm_engine_set_lenient_open(engine, 1);
        const QString stem = inp.left(inp.size() - 4);
        swmm_engine_open(engine, inp.toUtf8().constData(), (stem + ".live.rpt").toUtf8().constData(),
                         (stem + ".live.out").toUtf8().constData(), nullptr);
    }
    ~LiveEngine()
    {
        swmm_engine_close(engine);
        swmm_engine_destroy(engine);
    }
};

ModelCheckIssue issue(const QString &message, Severity severity = Severity::Error)
{
    return openswmmvis::simulation::parseModelCheckMessage(message, severity);
}
} // namespace

class TestModelCheck : public QObject
{
    Q_OBJECT

private slots:
    void parseReadsSeverityAndCode();
    void objectsNamedInReadsTypeWords_data();
    void objectsNamedInReadsTypeWords();
    void resolveLinksOnlyObjectsOfTheModel();
    void checkModelFileReportsWhatARunWould_data();
    void checkModelFileReportsWhatARunWould();
    void windowChecksLinksAndGoesToObjects();
    void windowReportsACopyThatCouldNotBeWritten();
    void removeSnapshotTouchesOnlyCheckFolders();
};

void TestModelCheck::parseReadsSeverityAndCode()
{
    auto parsed = issue(QStringLiteral("  ERROR 115: adverse slope for Conduit C2."), Severity::Warning);
    QCOMPARE(parsed.severity, Severity::Error);
    QCOMPARE(parsed.code, 115);
    QCOMPARE(parsed.message, QStringLiteral("ERROR 115: adverse slope for Conduit C2."));

    parsed = issue(QStringLiteral("  WARNING 04: minimum elevation drop used for Conduit C1"));
    QCOMPARE(parsed.severity, Severity::Warning);
    QCOMPARE(parsed.code, 4);

    // No prefix: the list the message came from decides.
    parsed = issue(QStringLiteral("USE HOTSTART: Cannot open hotstart file 'x.hsf'"), Severity::Error);
    QCOMPARE(parsed.severity, Severity::Error);
    QCOMPARE(parsed.code, 0);
    parsed = issue(QStringLiteral("[2D_OPTIONS] value ignored"), Severity::Warning);
    QCOMPARE(parsed.severity, Severity::Warning);
}

void TestModelCheck::objectsNamedInReadsTypeWords_data()
{
    QTest::addColumn<QString>("message");
    QTest::addColumn<Candidates>("expected");
    QTest::newRow("conduit") << QStringLiteral("ERROR 115: adverse slope for Conduit C2.")
                             << Candidates{{SWMMObjectRef::Link, QStringLiteral("C2.")}};
    QTest::newRow("time series")
        << QStringLiteral("ERROR 361: could not open external file used for Time Series TS1.")
        << Candidates{{SWMMObjectRef::TimeSeries, QStringLiteral("TS1.")}};
    QTest::newRow("virtual junction first")
        << QStringLiteral("too many items for Virtual Junction VJ1")
        << Candidates{{SWMMObjectRef::Node, QStringLiteral("VJ1")}};
    QTest::newRow("quoted, lower case") << QStringLiteral("Hot start: node 'J 1' not found in current model")
                                        << Candidates{{SWMMObjectRef::Node, QStringLiteral("J 1")}};
    QTest::newRow("no type word") << QStringLiteral("ERROR 209: undefined object J9.") << Candidates{};
    QTest::newRow("several")
        << QStringLiteral("time series interval greater than recording interval for Rain Gage G1")
        << Candidates{{SWMMObjectRef::TimeSeries, QStringLiteral("interval")},
                      {SWMMObjectRef::RainGage, QStringLiteral("G1")}};
    QTest::newRow("snow pack") << QStringLiteral("invalid parameters for Snow Pack SP1")
                               << Candidates{{SWMMObjectRef::Snowpack, QStringLiteral("SP1")}};
    QTest::newRow("unit hydrograph not linked")
        << QStringLiteral("parameters for same month provided more than once for Unit Hydrograph UH1")
        << Candidates{};
}

void TestModelCheck::objectsNamedInReadsTypeWords()
{
    QFETCH(QString, message);
    QFETCH(Candidates, expected);
    QCOMPARE(openswmmvis::simulation::objectsNamedIn(message), expected);
}

void TestModelCheck::resolveLinksOnlyObjectsOfTheModel()
{
    const QString inp = outputDir(QStringLiteral("resolve")) + QStringLiteral("/model.inp");
    QVERIFY(copyFixture(QStringLiteral("starts_with_warning.inp"), inp));
    LiveEngine live(inp);
    QList<ModelCheckIssue> issues{
        issue(QStringLiteral("ERROR 115: adverse slope for Conduit C2.")),
        issue(QStringLiteral("time series interval greater than recording interval for Rain Gage G1")),
        issue(QStringLiteral("Node C1 is not a node")),
        issue(QStringLiteral("Junction J9 is missing")),
        issue(QStringLiteral("ERROR 138: Node J1 has initial depth greater than maximum depth.")),
        issue(QStringLiteral("invalid length for Conduit C1 detail.")),
        issue(QStringLiteral("ERROR 209: undefined object J1.")),
    };
    auto unresolved = issues;
    openswmmvis::simulation::resolveModelCheckObjects(nullptr, unresolved);
    for (const auto &item : unresolved)
        QVERIFY(item.objectName.isEmpty());

    openswmmvis::simulation::resolveModelCheckObjects(live.engine, issues);
    const QList<QPair<SWMMObjectRef::ObjectType, QString>> expected{
        {SWMMObjectRef::Link, QStringLiteral("C2")},   // trailing period dropped
        {SWMMObjectRef::Unknown, QString()},          // no gage G1 in the model
        {SWMMObjectRef::Unknown, QString()},          // C1 is a link, not a node
        {SWMMObjectRef::Unknown, QString()},          // no J9
        {SWMMObjectRef::Node, QStringLiteral("J1")},
        {SWMMObjectRef::Link, QStringLiteral("C1")},
        {SWMMObjectRef::Unknown, QString()},          // a name without a type word is not guessed
    };
    QCOMPARE(issues.size(), expected.size());
    for (int i = 0; i < issues.size(); ++i) {
        QCOMPARE(issues.at(i).objectType, expected.at(i).first);
        QCOMPARE(issues.at(i).objectName, expected.at(i).second);
    }
}

void TestModelCheck::checkModelFileReportsWhatARunWould_data()
{
    QTest::addColumn<QString>("fixture");
    QTest::addColumn<bool>("opened");
    QTest::addColumn<bool>("initialized");
    QTest::addColumn<int>("firstCode");
    QTest::addColumn<int>("errors");
    QTest::addColumn<int>("warnings");
    QTest::newRow("starts") << QStringLiteral("starts_with_warning.inp") << true << true << 4 << 0 << 1;
    QTest::newRow("fails to start") << QStringLiteral("fails_to_start.inp") << true << false << 115
                                    << 1 << 1;
    QTest::newRow("fails to open") << QStringLiteral("fails_to_open.inp") << false << false << 361
                                   << 1 << 1;
}

void TestModelCheck::checkModelFileReportsWhatARunWould()
{
    QFETCH(QString, fixture);
    QFETCH(bool, opened);
    QFETCH(bool, initialized);
    QFETCH(int, firstCode);
    QFETCH(int, errors);
    QFETCH(int, warnings);
    const QString inp =
        outputDir(QStringLiteral("check_") + QFileInfo(fixture).completeBaseName()) +
        QStringLiteral("/model.inp");
    QVERIFY(copyFixture(fixture, inp));
    const auto result = openswmmvis::simulation::checkModelFile(inp);
    QCOMPARE(result.opened, opened);
    QCOMPARE(result.initialized, initialized);
    QVERIFY(!result.issues.isEmpty());
    QCOMPARE(result.issues.first().code, firstCode);
    int errorCount = 0;
    for (const auto &item : result.issues)
        errorCount += item.severity == Severity::Error;
    QCOMPARE(errorCount, errors);
    QCOMPARE(int(result.issues.size()) - errorCount, warnings);
    // Errors come first.
    QCOMPARE(result.issues.first().severity, errors ? Severity::Error : Severity::Warning);
}

void TestModelCheck::windowChecksLinksAndGoesToObjects()
{
    const QString modelDir = outputDir(QStringLiteral("window"));
    const QString inp = modelDir + QStringLiteral("/model.inp");
    QVERIFY(copyFixture(QStringLiteral("fails_to_start.inp"), inp));
    LiveEngine live(inp);
    int writes = 0;
    ModelCheckDialog dialog(
        [&](QString *) {
            ++writes;
            const QString copy = modelDir + QStringLiteral("/.swmmvis/check/") +
                                 QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                 QStringLiteral("/model.inp");
            return copyFixture(QStringLiteral("fails_to_start.inp"), copy) ? copy : QString();
        },
        [&] { return live.engine; });
    QAbstractItemModelTester tester(dialog.issueModel(),
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy finished(&dialog, &ModelCheckDialog::checkFinished);
    QSignalSpy activated(&dialog, &ModelCheckDialog::objectActivated);

    dialog.startCheck();
    QVERIFY(dialog.isChecking());
    dialog.startCheck(); // ignored while a check runs
    QVERIFY(finished.wait(30000));
    QCOMPARE(writes, 1);
    QVERIFY(!dialog.isChecking());
    QCOMPARE(finished.first().at(0).toString(),
             QStringLiteral("The model would not start: 1 error, 1 warning."));
    QCOMPARE(finished.first().at(1).toBool(), false);
    QCOMPARE(dialog.summaryText(), finished.first().at(0).toString());
    // The copy's folder (and the empty check folders above it) is gone; the model is untouched.
    QVERIFY(!QDir(modelDir + QStringLiteral("/.swmmvis")).exists());
    QVERIFY(QFile::exists(inp));

    auto *model = dialog.issueModel();
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->index(0, ModelCheckTableModel::SeverityColumn).data().toString(),
             QStringLiteral("Error"));
    QCOMPARE(model->index(0, ModelCheckTableModel::CodeColumn).data().toInt(), 115);
    QCOMPARE(model->index(0, ModelCheckTableModel::ObjectColumn).data().toString(),
             QStringLiteral("Link C2"));
    QCOMPARE(model->index(1, ModelCheckTableModel::SeverityColumn).data().toString(),
             QStringLiteral("Warning"));
    QCOMPARE(model->index(1, ModelCheckTableModel::ObjectColumn).data().toString(),
             QStringLiteral("Link C1"));

    auto *view = dialog.findChild<QTableView *>(QStringLiteral("modelCheckTable"));
    QVERIFY(view);
    QVERIFY(!view->accessibleName().isEmpty());
    view->setCurrentIndex(model->index(1, ModelCheckTableModel::MessageColumn));
    QTest::keyClick(view, Qt::Key_Return);
    QCOMPARE(activated.size(), 1);
    const auto ref = activated.first().at(0).value<SWMMObjectRef>();
    QCOMPARE(ref.objectType, SWMMObjectRef::Link);
    QCOMPARE(ref.name, QStringLiteral("C1"));

    // A row that names no object does nothing.
    model->setIssues({issue(QStringLiteral("ERROR 209: undefined object J9."))});
    view->setCurrentIndex(model->index(0, 0));
    QTest::keyClick(view, Qt::Key_Return);
    QCOMPARE(activated.size(), 1);

    // Check Again runs a new copy.
    auto *again = dialog.findChild<QPushButton *>(QStringLiteral("modelCheckAgain"));
    QVERIFY(again && again->isEnabled() && !again->autoDefault());
    again->click();
    QVERIFY(finished.wait(30000));
    QCOMPARE(writes, 2);
    QCOMPARE(model->rowCount(), 2);
}

void TestModelCheck::windowReportsACopyThatCouldNotBeWritten()
{
    ModelCheckDialog dialog(
        [](QString *error) {
            *error = QStringLiteral("the disk is full");
            return QString();
        },
        [] { return SWMM_Engine(nullptr); });
    QSignalSpy finished(&dialog, &ModelCheckDialog::checkFinished);
    dialog.startCheck();
    QCOMPARE(finished.size(), 1);
    QVERIFY(!dialog.isChecking());
    QCOMPARE(dialog.summaryText(),
             QStringLiteral("The model could not be copied for checking: the disk is full"));
    QCOMPARE(finished.first().at(1).toBool(), false);
    QCOMPARE(dialog.issueModel()->rowCount(), 0);
}

void TestModelCheck::removeSnapshotTouchesOnlyCheckFolders()
{
    const QString root = outputDir(QStringLiteral("remove"));
    // Not a check layout: left alone.
    const QString other = root + QStringLiteral("/a/b/model.inp");
    QVERIFY(copyFixture(QStringLiteral("starts_with_warning.inp"), other));
    QVERIFY(!openswmmvis::ui::removeModelCheckSnapshot(other));
    QVERIFY(QFile::exists(other));
    const QString editsOnly = root + QStringLiteral("/.swmmvis/edits/x/model.inp");
    QVERIFY(copyFixture(QStringLiteral("starts_with_warning.inp"), editsOnly));
    QVERIFY(!openswmmvis::ui::removeModelCheckSnapshot(editsOnly));
    QVERIFY(QFile::exists(editsOnly));

    // A check copy goes; .swmmvis stays because an extension snapshot is still in it.
    const QString copy = root + QStringLiteral("/.swmmvis/check/y/model.inp");
    QVERIFY(copyFixture(QStringLiteral("starts_with_warning.inp"), copy));
    QVERIFY(QFile::copy(copy, root + QStringLiteral("/.swmmvis/check/y/model.check.rpt")));
    QVERIFY(openswmmvis::ui::removeModelCheckSnapshot(copy));
    QVERIFY(!QDir(root + QStringLiteral("/.swmmvis/check")).exists());
    QVERIFY(QFile::exists(editsOnly));
}

QTEST_MAIN(TestModelCheck)
#include "test_modelcheck.moc"
