/**
 * @file test_extensionactivity.cpp
 * @brief Native acceptance of unique run attempts, preparation refusal and stale callback fencing.
 * @details Drives the real main-window run controller. All inputs, outputs and Qt
 * settings are retained beneath the configured repository verification directory.
 * No user model, temporary directory or remote service is used.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "plugins/extensionactivityui.h"
#include "plugins/extensionstateregistry.h"
#include "simulation/simulationrunner.h"
#include "simulation/simulationstatusmodel.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dresultslayer.h"
#include "map/mapcanvas.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include <openswmm/engine/openswmm_nodes.h>
#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMdiArea>
#include <QMessageBox>
#include <QQmlEngine>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStandardItemModel>
#include <QSurfaceFormat>
#include <QTest>
#include <QTimer>
#include <array>
#include <memory>

using namespace OpenSWMM::Sdk;
using namespace openswmmvis;

/** @brief Exercise native run behavior through a narrowly scoped private acceptance seam. */
class TestExtensionActivity final : public QObject
{
    Q_OBJECT
    std::unique_ptr<SWMMVis>
        mApp; ///< Real native controller, destroyed after accepted work settles.
    QPointer<SWMMVisProjectWindow> mProject; ///< Native model lifetime used by the current case.
    ExtensionStateRegistry *mRegistry = nullptr; ///< Shared application-owned process metadata.
    QList<ActivityEvent> mEvents;                ///< Copied managed native process observations.
    QMetaObject::Connection mConnection;         ///< Per-case registry collector.
    QString mDirectory; ///< Independent retained current-case artifact directory.

    /** @brief Resolve a case-relative retained path and create its parent directory.
     * @param[in] name Relative fixture/output filename.
     * @return Absolute reviewable path. */
    QString path(const QString &name) const
    {
        const auto result = QDir(mDirectory).filePath(name);
        if (!QDir().mkpath(QFileInfo(result).absolutePath()))
            qFatal("Cannot create native activity evidence directory");
        return result;
    }
    /** @brief Open the copied fixture through initialized native project/window wiring.
     * @return True when the model is Ready and the intended MDI document is active. */
    bool openModel()
    {
        const auto input = path("model.inp");
        if (!QFile::copy(QStringLiteral(SDK_NATIVE_ACTIVITY_FIXTURE_PATH), input))
            return false;
        mApp = std::make_unique<SWMMVis>();
        mProject = mApp->createProjectWindow(input);
        QList<QString> warnings, errors;
        if (!mProject || !mProject->loadModel(warnings, errors))
            return false;
        mProject->setHasChanges(false);
        auto *mdi = mApp->findChild<QMdiArea *>("mdiAreaCentral");
        if (!mdi)
            return false;
        mProject->show();
        mdi->setActiveSubWindow(mProject);
        QCoreApplication::processEvents();
        return mApp->activeProjectWindow() == mProject &&
               mRegistry->session(mProject->extensionSessionId()).status == SessionStatus::Ready;
    }
    /** @brief Start the current exact generation through the SDK's native command route.
     * @param[out] error Optional refusal diagnostic.
     * @return Accepted UUID, including synchronous preparation failure. */
    QUuid start(QString *error = nullptr)
    {
        const auto id = mProject->extensionSessionId();
        return mApp->startRunAttemptForSession(id, mRegistry->session(id).loadGeneration, error);
    }
    /** @brief Count one immutable native process transition.
     * @param[in] operation Accepted unique UUID.
     * @param[in] kind Transition reason.
     * @return Number of copied matching observations. */
    int count(const QUuid &operation, ActivityEventKind kind) const
    {
        int result = 0;
        for (const auto &event : mEvents)
            if (event.process.operationId == operation && event.kind == kind)
                ++result;
        return result;
    }
    /** @brief Accept only the normal overwrite confirmation in controlled rerun fixtures.
     * @param[in,out] timer GUI timer owned by the current test call stack. */
    void acceptOverwrite(QTimer &timer)
    {
        timer.setInterval(1);
        connect(&timer, &QTimer::timeout, &timer, [] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                    if (auto *yes = box->button(QMessageBox::Yes))
                        yes->click();
        });
        timer.start();
    }

private slots:
    /** @brief Isolate settings and register production canvas types before constructing windows. */
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("QT_QUICK_BACKEND", "software");
        qunsetenv("SWMMVIS_OPEN_ON_STARTUP");
        qunsetenv("SWMMVIS_GUI_EXTENSION_PATH");
        QCoreApplication::setOrganizationName("OpenSWMMActivityTest");
        QCoreApplication::setApplicationName("NativeActivity");
        const QString directory = QStringLiteral(SDK_NATIVE_ACTIVITY_VERIFICATION_DIRECTORY);
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(QFile::exists(QStringLiteral(SDK_NATIVE_ACTIVITY_FIXTURE_PATH)));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory + "/qt-settings");
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                           directory + "/qt-settings");
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
        mRegistry = ExtensionStateRegistry::instance();
        QVERIFY(mRegistry);
    }
    /** @brief Start an independent retained collector without reusing preceding reports. */
    void init()
    {
        mDirectory = QDir(QStringLiteral(SDK_NATIVE_ACTIVITY_VERIFICATION_DIRECTORY))
                         .filePath(QString::fromLatin1(QTest::currentTestFunction()) + '/' +
                                   QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(QDir().mkpath(mDirectory));
        mEvents.clear();
        mConnection = connect(mRegistry, &ExtensionStateRegistry::activityEventReady, this,
                              [this](const ActivityEvent &event) {
                                  if (!event.process.operationId.isNull() &&
                                      event.process.kind == ProcessKind::Run)
                                      mEvents.append(event);
                              });
    }
    /** @brief Drain accepted workers before destroying native controllers and their borrowed state. */
    void cleanup()
    {
        if (mApp) {
            const auto runners = mApp->mActiveRunners;
            for (auto *runner : runners)
                mApp->requestRunCancellation(runner->property("extensionRunAttemptId").toUuid());
            QTRY_VERIFY_WITH_TIMEOUT(mApp->mActiveRunners.isEmpty(), 20000);
            if (mProject) {
                mProject->setHasChanges(false);
                delete mProject.data();
            }
            mApp.reset();
        }
        QObject::disconnect(mConnection);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        qunsetenv("SWMMVIS_TEST_FAULT");
    }
    /** @brief Status lookup never inserts an Idle row or publishes a misleading run. */
    void statusLookupHasNoDiscoverySideEffects()
    {
        QVERIFY(openModel());
        const auto rows = mApp->mSimStatusModel->rowCount();
        const auto watermark = mRegistry->snapshot().sequence;
        for (int i = 0; i < 10; ++i)
            QCOMPARE(mApp->mSimStatusModel->existingJobForModel(nullptr, "unknown"), -1);
        QCOMPARE(mApp->mSimStatusModel->rowCount(), rows);
        QCOMPARE(mRegistry->snapshot().sequence, watermark);
    }
    /** @brief Native row publication tolerates a synchronous observer clearing its model rows. */
    void statusRowsCanBeClearedDuringPublication()
    {
        QVERIFY(openModel());
        SimulationStatusModel status;
        auto clearOnInsert = connect(&status, &QAbstractItemModel::rowsInserted, this,
                                     [&] { status.clearJobsForModel(mProject); });
        QCOMPARE(status.ensureJobForModel(mProject, "Fixture", path("model.inp"), "6.0.0"), -1);
        QCOMPARE(status.rowCount(), 0);
        QObject::disconnect(clearOnInsert);
        const int row = status.ensureJobForModel(mProject, "Fixture", path("model.inp"), "6.0.0");
        QVERIFY(row >= 0);
        status.addWarning(row, 1, "Old warning");
        connect(&status, &QAbstractItemModel::dataChanged, this,
                [&] { status.clearJobsForModel(mProject); });
        QCOMPARE(status.addOrReuseJobForModel(mProject, "Fixture", path("model.inp"), "6.0.0"), -1);
        QCOMPARE(status.rowCount(), 0);
    }
    /** @brief Reusing a presentation row creates a new UUID and immutable independent outcomes. */
    void nativeSuccessAndReusedStatusRows()
    {
        QVERIFY(openModel());
        QString error;
        const auto first = start(&error);
        QVERIFY2(!first.isNull(), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(mRegistry->operation(first).isTerminal(), 20000);
        QCOMPARE(mRegistry->operation(first).phase, ProcessPhase::Succeeded);
        const int row = mRegistry->operation(first).rowId;
        QCOMPARE(mRegistry->operation(first).progress, 1.);
        QTRY_VERIFY(mApp->mActiveRunners.isEmpty());
        QCOMPARE(mProject->property("extensionRunCount").toInt(), 0);
        QTimer overwrite;
        acceptOverwrite(overwrite);
        const auto second = start(&error);
        overwrite.stop();
        QVERIFY2(!second.isNull(), qPrintable(error));
        QVERIFY(second != first);
        QCOMPARE(mRegistry->operation(second).rowId, row);
        QTRY_VERIFY_WITH_TIMEOUT(mRegistry->operation(second).isTerminal(), 20000);
        QCOMPARE(mRegistry->operation(second).phase, ProcessPhase::Succeeded);
        QTRY_COMPARE(count(first, ActivityEventKind::OperationFinished), 1);
        QTRY_COMPARE(count(second, ActivityEventKind::OperationFinished), 1);
        QVERIFY(mRegistry->operation(first).operationId.isNull());
    }
    /** @brief Reentrant starts are refused and accepted pre-worker failure still has one terminal UUID. */
    void acceptedPreparationFailureAndReentrantStart()
    {
        QVERIFY(openModel());
        QObject observer;
        bool changed = false, refused = false;
        connect(mApp->mSimStatusModel, &QAbstractItemModel::dataChanged, &observer, [&] {
            if (changed)
                return;
            const auto row =
                mApp->mSimStatusModel->existingJobForModel(mProject, mProject->engineVersion());
            const auto *record = mApp->mSimStatusModel->jobRecord(row);
            if (!record || record->status != SimulationJobStatus::Running)
                return;
            changed = true;
            QString error;
            refused = start(&error).isNull() && !error.isEmpty();
            const int node = swmm_node_index(mProject->modelLayer()->engine(), "J1");
            QVERIFY(node >= 0);
            QVERIFY(mProject->modelLayer()->applyNodeMove(node, -75.99, 40.02));
        });
        QString error;
        const auto operation = start(&error);
        QVERIFY2(!operation.isNull(), qPrintable(error));
        QVERIFY(changed);
        QVERIFY(refused);
        QCOMPARE(mRegistry->operation(operation).phase, ProcessPhase::Failed);
        QVERIFY(!mRegistry->operation(operation).startedAt.isValid());
        QVERIFY(!mRegistry->operation(operation).error.isEmpty());
        QVERIFY(mApp->mActiveRunners.isEmpty());
        QCOMPARE(mProject->property("extensionRunCount").toInt(), 0);
        QTRY_COMPARE(count(operation, ActivityEventKind::OperationAccepted), 1);
        QTRY_COMPARE(count(operation, ActivityEventKind::OperationFinished), 1);
    }
    /** @brief Cancellation from an accepted preparation log stops before trace/output side effects. */
    void cancellationDuringPreparationLogDoesNotDispatch()
    {
        QVERIFY(openModel());
        QObject observer;
        const auto session = mProject->extensionSessionId();
        QUuid cancelled;
        connect(mApp->mLogMessagesModel, &QAbstractItemModel::rowsInserted, &observer, [&] {
            const auto operation = mApp->mLastAcceptedRunAttempts.value(session);
            if (cancelled.isNull() && !operation.isNull() &&
                mRegistry->operation(operation).phase == ProcessPhase::Queued) {
                cancelled = operation;
                QVERIFY(mApp->requestRunCancellation(operation));
            }
        });
        QString error;
        const auto operation = start(&error);
        QVERIFY2(!operation.isNull(), qPrintable(error));
        QCOMPARE(operation, cancelled);
        QCOMPARE(mRegistry->operation(operation).phase, ProcessPhase::Cancelled);
        QVERIFY(!mRegistry->operation(operation).startedAt.isValid());
        QVERIFY(mApp->mActiveRunners.isEmpty());
        QVERIFY(!QFileInfo::exists(path("model.out")));
        QCOMPARE(mProject->property("extensionRunCount").toInt(), 0);
    }
    /** @brief An SDK generation cannot silently change during native overwrite confirmation. */
    void requestedGenerationSurvivesPreflightDialogs()
    {
        QVERIFY(openModel());
        QString error;
        const auto first = start(&error);
        QVERIFY2(!first.isNull(), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(mRegistry->operation(first).isTerminal(), 20000);
        QTRY_VERIFY(mApp->mActiveRunners.isEmpty());
        const auto session = mProject->extensionSessionId();
        const auto requested = mRegistry->session(session).loadGeneration;
        bool changed = false;
        QTimer overwrite;
        overwrite.setInterval(1);
        connect(&overwrite, &QTimer::timeout, this, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                    if (auto *yes = box->button(QMessageBox::Yes); yes && !changed) {
                        changed = true;
                        const auto next = mRegistry->beginLoad(session);
                        QVERIFY(next > requested);
                        mRegistry->finishLoad(session, next, true);
                        yes->click();
                    }
        });
        overwrite.start();
        const auto refused = mApp->startRunAttemptForSession(session, requested, &error);
        overwrite.stop();
        QVERIFY(changed);
        QVERIFY(refused.isNull());
        QVERIFY(!error.isEmpty());
        QCOMPARE(mApp->mLastAcceptedRunAttempts.value(session), first);
        QVERIFY(mApp->mActiveRunners.isEmpty());
        QCOMPARE(mProject->property("extensionRunCount").toInt(), 0);
    }
    /** @brief A cancellation request and a successful race outcome remain separate facts. */
    void nativeCancellationAndSettledRefusal()
    {
        QVERIFY(openModel());
        QString error;
        const auto operation = start(&error);
        QVERIFY2(!operation.isNull(), qPrintable(error));
        QVERIFY2(mApp->requestRunCancellation(operation, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(mRegistry->operation(operation).isTerminal(), 20000);
        const auto final = mRegistry->operation(operation);
        QVERIFY(final.cancelRequested);
        QVERIFY(final.phase == ProcessPhase::Cancelled || final.phase == ProcessPhase::Succeeded);
        if (final.phase == ProcessPhase::Cancelled)
            QVERIFY(final.progress < 1.);
        QVERIFY(!mApp->requestRunCancellation(operation, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(mRegistry->operation(operation).phase, final.phase);
        QTRY_COMPARE(count(operation, ActivityEventKind::CancellationRequested), 1);
        QTRY_COMPARE(count(operation, ActivityEventKind::OperationFinished), 1);
    }
    /** @brief Worker faults are terminal failures rather than successful/cancelled rows. */
    void workerFailureHasAcceptedIdentity()
    {
        QVERIFY(openModel());
        qputenv("SWMMVIS_TEST_FAULT", "step:bad_alloc");
        QString error;
        const auto operation = start(&error);
        qunsetenv("SWMMVIS_TEST_FAULT");
        QVERIFY2(!operation.isNull(), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(mRegistry->operation(operation).isTerminal(), 20000);
        const auto final = mRegistry->operation(operation);
        QCOMPARE(final.phase, ProcessPhase::Failed);
        QVERIFY(final.errorCode != 0);
        QVERIFY(!final.error.isEmpty());
        QVERIFY(final.startedAt.isValid());
        QVERIFY(final.progress < 1.);
        QTRY_COMPARE(count(operation, ActivityEventKind::OperationFinished), 1);
    }
    /** @brief Model retirement cancels its worker but preserves captured completion history. */
    void modelClosePreservesTerminalHistory()
    {
        QVERIFY(openModel());
        const auto session = mProject->extensionSessionId();
        QString error;
        const auto operation = start(&error);
        QVERIFY2(!operation.isNull(), qPrintable(error));
        delete mProject.data();
        QTRY_COMPARE_WITH_TIMEOUT(count(operation, ActivityEventKind::OperationFinished), 1, 20000);
        bool matched = false;
        for (const auto &event : mEvents)
            if (event.kind == ActivityEventKind::OperationFinished &&
                event.process.operationId == operation)
                matched = event.process.sessionId == session && event.process.cancelRequested &&
                          event.process.isTerminal();
        QVERIFY(matched);
        QVERIFY(mRegistry->session(session).sessionId.isNull());
        QTRY_VERIFY(mApp->mActiveRunners.isEmpty());
    }
    /** @brief Destroying the native main window drains and records the real runner outcome. */
    void nativeWindowShutdownPublishesTerminalHistory()
    {
        QVERIFY(openModel());
        QString error;
        const auto operation = start(&error);
        QVERIFY2(!operation.isNull(), qPrintable(error));
        const auto session = mProject->extensionSessionId();
        const auto generation = mRegistry->session(session).loadGeneration;
        const QPointer<SWMMVis> controller(mApp.get());
        auto *runner = mApp->mActiveRunners.value(mRegistry->operation(operation).rowId);
        QVERIFY(runner);
        bool observed = false, success = false, restartRefused = false;
        int code = 0;
        connect(runner, &SimulationRunner::finished, this,
                [&](int, bool nativeSuccess, int nativeCode) {
                    observed = true;
                    success = nativeSuccess;
                    code = nativeCode;
                    restartRefused =
                        controller &&
                        controller->startRunAttemptForSession(session, generation, &error).isNull();
                });
        mProject->setHasChanges(false);
        mApp.reset();
        QVERIFY(observed);
        QVERIFY(restartRefused);
        QVERIFY(controller.isNull());
        QTRY_COMPARE(count(operation, ActivityEventKind::OperationFinished), 1);
        ProcessState terminal;
        for (const auto &event : mEvents)
            if (event.kind == ActivityEventKind::OperationFinished &&
                event.process.operationId == operation)
                terminal = event.process;
        const auto expected = success ? ProcessPhase::Succeeded
                              : code  ? ProcessPhase::Failed
                                      : ProcessPhase::Cancelled;
        QCOMPARE(terminal.phase, expected);
        QVERIFY(terminal.cancelRequested);
        QCOMPARE(terminal.sessionId, session);
        QVERIFY(mRegistry->operation(operation).operationId.isNull());
        QVERIFY(mRegistry->session(session).sessionId.isNull());
    }
    /** @brief Old completion/progress cannot overwrite a reentrant new attempt using the same row. */
    void oldCallbacksCannotMutateReentrantAttempt()
    {
        QVERIFY(openModel());
        QObject observer;
        QString error;
        const auto first = start(&error);
        QVERIFY2(!first.isNull(), qPrintable(error));
        const int row = mRegistry->operation(first).rowId;
        const QPointer<SimulationRunner> firstRunner(mApp->mActiveRunners.value(row));
        QVERIFY(firstRunner);
        QUuid second;
        QPointer<SWMM2DResultsLayer> newLayer;
        bool restarted = false, staleProgressBlocked = false, oldFinishBlocked = false;
        QTimer overwrite;
        acceptOverwrite(overwrite);
        const auto restart = [&] {
            if (restarted || !mRegistry->operation(first).isTerminal())
                return;
            restarted = true;
            second = start(&error);
            if (second.isNull())
                return;
            const auto *record = mApp->mSimStatusModel->jobRecord(row);
            const double before = record ? record->progress : -1.;
            emit firstRunner->progressChanged(row, .987654, {}, 0., 0., 0., qQNaN());
            emit firstRunner->warningReceived(row, 999, "STALE ATTEMPT");
            record = mApp->mSimStatusModel->jobRecord(row);
            staleProgressBlocked = record && record->progress == before &&
                                   !record->warnings.join('\n').contains("STALE ATTEMPT");
            newLayer = new SWMM2DResultsLayer("New attempt sentinel", nullptr);
            newLayer->setParent(mProject.data());
            newLayer->setSource(std::make_unique<EngineMesh2DSource>(
                std::vector<double>{}, std::vector<double>{}, std::vector<double>{},
                std::vector<std::array<int, 3>>{}));
            mApp->mActive2DResultsLayers.insert(row, newLayer);
        };
        connect(mProject->canvas(), &MapCanvas::layerAdded, &observer, [&](OpenSWMMVisLayer *base) {
            if (auto *layer = qobject_cast<SWMMResultsLayer *>(base)) {
                connect(layer, &SWMMResultsLayer::sourceStateChanged, &observer, restart);
                connect(layer, &SWMMResultsLayer::resultsFinalized, &observer, restart);
            }
        });
        connect(firstRunner, &SimulationRunner::finished, &observer, [&] {
            oldFinishBlocked = !second.isNull() && newLayer &&
                               newLayer->name() == "New attempt sentinel" && newLayer->source() &&
                               newLayer->source()->isLive() &&
                               mApp->mActive2DResultsLayers.value(row) == newLayer;
        });
        QTRY_VERIFY_WITH_TIMEOUT(restarted, 20000);
        overwrite.stop();
        QVERIFY2(!second.isNull(), qPrintable(error));
        QVERIFY(staleProgressBlocked);
        QVERIFY(oldFinishBlocked);
        QTRY_VERIFY_WITH_TIMEOUT(mRegistry->operation(second).isTerminal(), 20000);
        QTRY_COMPARE(count(first, ActivityEventKind::OperationFinished), 1);
        QTRY_COMPARE(count(second, ActivityEventKind::OperationFinished), 1);
    }
};

/** @brief Start QtTest with native extension discovery disabled for this isolated controller fixture.
 * @param[in] argc Original QtTest argument count.
 * @param[in] argv Original QtTest arguments.
 * @return QtTest exit status. */
int main(int argc, char **argv)
{
    QList<QByteArray> storage;
    for (int i = 0; i < argc; ++i)
        storage.append(argv[i]);
    storage.append("--no-extensions");
    QList<char *> applicationArguments;
    for (auto &value : storage)
        applicationArguments.append(value.data());
    applicationArguments.append(nullptr);
    int applicationCount = storage.size();
    QApplication application(applicationCount, applicationArguments.data());
    TestExtensionActivity test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_extensionactivity.moc"
