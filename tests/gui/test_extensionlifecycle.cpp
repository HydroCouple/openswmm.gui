/**
 * @file test_extensionlifecycle.cpp
 * @brief Native-window acceptance tests for authoritative extension lifecycle and persistence events.
 * @details Exercises real engines, windows, project serializers and SDK model facades. Each
 * case writes independent retained files beneath the configured verification directory;
 * no user model, temporary directory or network service is used.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionmanager.h"
#include "plugins/extensionmodelaccess.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "sdk/IEventSourceV2.hpp"
#include "sdk/IProject.hpp"
#include "sdk/IUiHost.hpp"
#include "swmmvisprojectwindow.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMdiArea>
#include <QPointer>
#include <QQmlEngine>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTest>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>

using namespace OpenSWMM::Sdk;
using namespace openswmmvis;

namespace
{
/** @brief Own a native window before releasing the workspace that owns its model layer. */
struct NativeWindow {
    std::unique_ptr<OpenSWMMVisWorkspace> workspace; ///< Reviewable model's native workspace owner.
    QPointer<SWMMVisProjectWindow>
        window; ///< Guarded window, possibly deleted by a tested callback.
    /** @brief Construct an unloaded native session without reading or writing its model.
     * @param[in] path Intended input model path; may deliberately be absent.
     * @param[in] parent Optional MDI owner.
     * @param[in] instanceId Saved UUID to seed before registration.
     * @param[in] projectPath Logical project path to seed before registration. */
    explicit NativeWindow(const QString &path, QWidget *parent = nullptr,
                          const QUuid &instanceId = {}, const QString &projectPath = {})
        : workspace(OpenSWMMVisWorkspace::newInstance(QString(), nullptr)),
          window(new SWMMVisProjectWindow(workspace.get(), path, parent, instanceId, projectPath))
    {
    }
    /** @brief Destroy the window before its model/workspace and invalidate pending native jobs. */
    ~NativeWindow() { delete window.data(); }
};

/** @brief Keep an accepted native async open queued until its owner has been closed. */
class QueuedOpenBarrier
{
public:
    /** @brief Reserve the only global-pool worker, after draining previous work. */
    QueuedOpenBarrier() : mPreviousMaximum(QThreadPool::globalInstance()->maxThreadCount())
    {
        QThreadPool::globalInstance()->waitForDone();
        QThreadPool::globalInstance()->setMaxThreadCount(1);
        mBlocker = QtConcurrent::run([this] {
            mEntered.release();
            mResume.acquire();
        });
        mEntered.acquire();
    }
    /** @brief Release queued work and restore global-pool configuration before workspace teardown. */
    ~QueuedOpenBarrier()
    {
        release();
        QThreadPool::globalInstance()->waitForDone();
        QThreadPool::globalInstance()->setMaxThreadCount(mPreviousMaximum);
    }
    /** @brief Allow the queued open to run; repeated calls have no effect. */
    void release()
    {
        if (!mReleased) {
            mReleased = true;
            mResume.release();
        }
    }

private:
    int mPreviousMaximum;         ///< Previous process-global concurrency limit.
    bool mReleased = false;       ///< The occupying worker was unblocked.
    QSemaphore mEntered, mResume; ///< Explicit worker/UI synchronization without sleeps.
    QFuture<void> mBlocker;       ///< Owning future for the deliberately occupying worker.
};
}

/** @brief Exercise externally observable native lifecycle and save outcomes. */
class TestExtensionLifecycle : public QObject
{
    Q_OBJECT
    ExtensionStateRegistry *mRegistry = nullptr; ///< Application-owned authoritative registry.
    QMetaObject::Connection mConnection;         ///< Per-case native observation collector.
    QList<StateEvent> mEvents; ///< Owning copied records received during this case.
    QString mCaseDirectory;    ///< Unique retained directory for the current test case.

    /** @brief Construct an absolute retained file path and its parent directory.
     * @param[in] name Case-relative file/directory name.
     * @return Absolute path; fixture infrastructure failures abort with its reviewable location. */
    QString output(const QString &name) const
    {
        const auto path = QDir(mCaseDirectory).filePath(name);
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            qFatal("Cannot create native lifecycle evidence directory: %s", qPrintable(path));
        return path;
    }
    /** @brief Copy the small SDK model into this case, preserving the source fixture.
     * @param[in] name Case-relative input name.
     * @return Absolute copied model path. */
    QString model(const QString &name = QStringLiteral("model.inp")) const
    {
        const auto path = output(name);
        if (!QFile::copy(QStringLiteral(SDK_NATIVE_STATE_FIXTURE_PATH), path))
            qFatal("Cannot copy native lifecycle model fixture to %s", qPrintable(path));
        return path;
    }
    /** @brief Read one retained artifact without modifying it.
     * @param[in] path Existing artifact path.
     * @return File contents, or empty on failure. */
    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
    /** @brief Copy records of one kind for exactly one open lifetime.
     * @param[in] sessionId Registry-assigned open identity.
     * @param[in] kind Required transition reason.
     * @return Owning matching observations. */
    QList<StateEvent> events(const QUuid &sessionId, StateEventKind kind) const
    {
        QList<StateEvent> result;
        for (const auto &event : mEvents)
            if (event.session.sessionId == sessionId && event.kind == kind)
                result.append(event);
        return result;
    }

private slots:
    /** @brief Isolate settings and register production canvas renderer types before constructing windows. */
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString directory = QStringLiteral(SDK_NATIVE_STATE_VERIFICATION_DIRECTORY);
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(QFile::exists(QStringLiteral(SDK_NATIVE_STATE_FIXTURE_PATH)));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory + "/qt-settings");
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                           directory + "/qt-settings");
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setSamples(6);
        QSurfaceFormat::setDefaultFormat(format);
        mRegistry = ExtensionStateRegistry::instance();
        QVERIFY(mRegistry);
    }
    /** @brief Start a fresh collector and retained fixture directory without reusing previous results. */
    void init()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        mEvents.clear();
        mCaseDirectory = QDir(QStringLiteral(SDK_NATIVE_STATE_VERIFICATION_DIRECTORY))
                             .filePath(QString::fromLatin1(QTest::currentTestFunction()) + '/' +
                                       QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(QDir().mkpath(mCaseDirectory));
        mConnection = connect(mRegistry, &ExtensionStateRegistry::stateEventReady, this,
                              [this](const StateEvent &event) { mEvents.append(event); });
    }
    /** @brief Disconnect the case collector and complete deferred QObject cleanup. */
    void cleanup()
    {
        QObject::disconnect(mConnection);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    /** @brief Register stable saved IDs eagerly while independent open lifetimes remain distinct. */
    void constructorIdentityAndSnapshotsAreStable()
    {
        const QUuid persistent = QUuid::createUuid();
        const auto chosenProject = output("chosen/project.oswp");
        NativeWindow first(model("first/model.inp"), nullptr, persistent, chosenProject);
        const auto firstId = first.window->extensionSessionId();
        QVERIFY(!firstId.isNull());
        QCOMPARE(mRegistry->session(firstId).status, SessionStatus::Created);
        QCOMPARE(mRegistry->session(firstId).instanceId, persistent);
        QCOMPARE(mRegistry->session(firstId).projectPath, chosenProject);
        const auto watermark = mRegistry->snapshot().sequence;
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(first.window->extensionSessionId(), firstId);
            QCOMPARE(first.window->property("extensionInstanceId").toUuid(), persistent);
            QCOMPARE(mRegistry->snapshot().sequence, watermark);
        }
        delete first.window.data();
        QVERIFY(mRegistry->session(firstId).sessionId.isNull());
        NativeWindow reopened(model("reopened/model.inp"), nullptr, persistent, chosenProject);
        QVERIFY(reopened.window->extensionSessionId() != firstId);
        QCOMPARE(mRegistry->session(reopened.window->extensionSessionId()).instanceId, persistent);
        QTRY_VERIFY(!events(firstId, StateEventKind::SessionClosed).isEmpty());
    }

    /** @brief Settle sync loads without emitting async completions, and hold controller restoration explicitly. */
    void synchronousReadinessAndControllerBarrier()
    {
        NativeWindow direct(model("direct/model.inp"));
        QSignalSpy syncSpy(direct.window.data(), &SWMMVisProjectWindow::modelLoadFinished);
        QList<QString> warnings, errors;
        QVERIFY2(direct.window->loadModel(warnings, errors), qPrintable(errors.join(';')));
        QCOMPARE(syncSpy.count(), 0);
        const auto directId = direct.window->extensionSessionId();
        QCOMPARE(mRegistry->session(directId).status, SessionStatus::Ready);
        QCOMPARE(mRegistry->session(directId).loadGeneration, quint64{1});
        QTRY_COMPARE(events(directId, StateEventKind::ModelReady).size(), 1);

        NativeWindow held(model("held/model.inp"));
        const auto heldId = held.window->extensionSessionId();
        held.window->beginExtensionOpen();
        warnings.clear();
        errors.clear();
        QVERIFY2(held.window->loadModel(warnings, errors), qPrintable(errors.join(';')));
        QVERIFY(held.window->modelLayer()->engine());
        QCOMPARE(mRegistry->session(heldId).status, SessionStatus::Loading);
        QCoreApplication::processEvents();
        QVERIFY(events(heldId, StateEventKind::ModelReady).isEmpty());
        held.window->completeExtensionOpen();
        QCOMPARE(mRegistry->session(heldId).status, SessionStatus::Ready);
        QTRY_COMPARE(events(heldId, StateEventKind::ModelReady).size(), 1);
    }

    /** @brief Give newly initialized in-memory models the same settled lifecycle as file loads. */
    void blankInitializationPublishesReadiness()
    {
        NativeWindow fixture(QString{});
        fixture.window->markUntitled();
        QSignalSpy asyncSpy(fixture.window.data(), &SWMMVisProjectWindow::modelLoadFinished);
        SWMMModelLayer::NewProjectSpec spec;
        spec.name = "Lifecycle blank";
        spec.startDateTime = QDateTime(QDate(2026, 1, 1), QTime(0, 0));
        spec.endDateTime = spec.startDateTime.addSecs(3600);
        QList<QString> warnings, errors;
        QVERIFY2(fixture.window->initializeBlankModel(spec, warnings, errors),
                 qPrintable(errors.join(';')));
        const auto id = fixture.window->extensionSessionId();
        QCOMPARE(mRegistry->session(id).status, SessionStatus::Ready);
        QVERIFY(mRegistry->session(id).inputPath.isEmpty());
        QVERIFY(mRegistry->session(id).projectPath.isEmpty());
        QCOMPARE(asyncSpy.count(), 0);
        QTRY_COMPARE(events(id, StateEventKind::ModelReady).size(), 1);
    }

    /** @brief Preserve failed-attempt history and accept a repaired asynchronous retry as a new generation. */
    void asynchronousFailureThenRetry()
    {
        const auto path = output("repaired/model.inp");
        NativeWindow fixture(path);
        const auto id = fixture.window->extensionSessionId();
        QSignalSpy completed(fixture.window.data(), &SWMMVisProjectWindow::modelLoadFinished);
        fixture.window->loadModelAsync();
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 20000);
        QVERIFY(!completed.at(0).at(0).toBool());
        QCOMPARE(mRegistry->session(id).status, SessionStatus::Failed);
        QCOMPARE(mRegistry->session(id).loadGeneration, quint64{1});
        QVERIFY(!mRegistry->session(id).error.isEmpty());
        QVERIFY(QFile::copy(QStringLiteral(SDK_NATIVE_STATE_FIXTURE_PATH), path));
        fixture.window->loadModelAsync();
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 2, 20000);
        QVERIFY(completed.at(1).at(0).toBool());
        QCOMPARE(mRegistry->session(id).status, SessionStatus::Ready);
        QCOMPARE(mRegistry->session(id).loadGeneration, quint64{2});
        QTRY_COMPARE(events(id, StateEventKind::LoadFailed).size(), 1);
        QTRY_COMPARE(events(id, StateEventKind::ModelReady).size(), 1);
        QCOMPARE(events(id, StateEventKind::LoadFailed).first().session.loadGeneration, quint64{1});
        QCOMPARE(events(id, StateEventKind::ModelReady).first().session.loadGeneration, quint64{2});
    }

    /** @brief Retire an owner while its open remains queued and suppress readiness from the late worker. */
    void closeWhileAsyncOpenIsQueued()
    {
        NativeWindow fixture(model());
        const auto id = fixture.window->extensionSessionId();
        QSignalSpy completed(fixture.window.data(), &SWMMVisProjectWindow::modelLoadFinished);
        QueuedOpenBarrier barrier;
        fixture.window->loadModelAsync();
        QCOMPARE(mRegistry->session(id).status, SessionStatus::Loading);
        delete fixture.window.data();
        QVERIFY(mRegistry->session(id).sessionId.isNull());
        barrier.release();
        QThreadPool::globalInstance()->waitForDone();
        QTRY_COMPARE(events(id, StateEventKind::SessionClosed).size(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 0);
        QVERIFY(events(id, StateEventKind::ModelReady).isEmpty());
    }

    /** @brief Apply the selected modern project session before releasing the Ready barrier. */
    void selectedSidecarSessionSettlesBeforeReady()
    {
        const auto input = model();
        const auto project = output("chosen.oswp");
        const QUuid first = QUuid::createUuid(), selected = QUuid::createUuid();
        const QJsonObject root{
            {"schemaVersion", 5},
            {"sessions",
             QJsonArray{
                 QJsonObject{{"inpPath", "unused.inp"}, {"instanceId", first.toString()}},
                 QJsonObject{{"inpPath", "model.inp"},
                             {"instanceId", selected.toString()},
                             {"extensions",
                              QJsonObject{{"native-test", QJsonObject{{"selected", true}}}}}}}}};
        QFile file(project);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QJsonDocument(root).toJson();
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        NativeWindow fixture(input, nullptr, selected, project);
        const auto id = fixture.window->extensionSessionId();
        fixture.window->beginExtensionOpen();
        QList<QString> warnings, errors;
        QVERIFY2(fixture.window->loadModel(warnings, errors), qPrintable(errors.join(';')));
        QString error;
        QStringList restoreWarnings;
        QVERIFY2(
            ProjectSerializer::applyFromFile(project, fixture.window, &error, &restoreWarnings, 1),
            qPrintable(error));
        QCOMPARE(mRegistry->session(id).status, SessionStatus::Loading);
        fixture.window->completeExtensionOpen();
        QTRY_COMPARE(events(id, StateEventKind::ModelReady).size(), 1);
        const auto ready = events(id, StateEventKind::ModelReady).first().session;
        QCOMPARE(ready.instanceId, selected);
        QVERIFY(ready.instanceId != first);
        QCOMPARE(ready.projectPath, project);
        QVERIFY(fixture.window->property("extensionData")
                    .toJsonObject()["native-test"]
                    .toObject()["selected"]
                    .toBool());
    }

    /** @brief Publish Project success only after final files and clean state, and retain identity on failure. */
    void normalSaveAndFailedDestination()
    {
        NativeWindow fixture(model());
        QList<QString> warnings, errors;
        QVERIFY2(fixture.window->loadModel(warnings, errors), qPrintable(errors.join(';')));
        const auto id = fixture.window->extensionSessionId();
        const auto target = output("saved/final.inp");
        const auto sidecar = ProjectSerializer::sidecarPathFor(target);
        fixture.window->setHasChanges(true);
        QObject observer;
        bool finalFilesObserved = false;
        connect(mRegistry, &ExtensionStateRegistry::stateEventReady, &observer,
                [&](const StateEvent &event) {
                    if (event.session.sessionId == id &&
                        event.kind == StateEventKind::SaveFinished &&
                        event.save.outcome == SaveOutcome::Succeeded)
                        finalFilesObserved = QFile::exists(target) && QFile::exists(sidecar) &&
                                             !fixture.window->hasChanges() && !event.session.dirty;
                });
        QString error;
        QVERIFY2(fixture.window->saveAs(target, &error), qPrintable(error));
        QTRY_VERIFY(finalFilesObserved);
        QCOMPARE(events(id, StateEventKind::SaveFinished).size(), 1);
        const auto successful = events(id, StateEventKind::SaveFinished).first();
        QCOMPARE(successful.save.kind, SaveKind::Project);
        QCOMPARE(successful.save.targetPath, target);
        QCOMPARE(successful.session.projectPath, sidecar);
        const auto savedBytes = read(target);
        QVERIFY(!savedBytes.isEmpty());
        const auto blocked = output("blocked-destination");
        QVERIFY(QDir().mkpath(blocked));
        fixture.window->setHasChanges(true);
        error.clear();
        QVERIFY(!fixture.window->saveAs(blocked, &error));
        QVERIFY(!error.isEmpty());
        QTRY_COMPARE(events(id, StateEventKind::SaveFinished).size(), 2);
        const auto failed = events(id, StateEventKind::SaveFinished).last();
        QCOMPARE(failed.save.outcome, SaveOutcome::Failed);
        QCOMPARE(fixture.window->property("extensionProjectPath").toString(), sidecar);
        QCOMPARE(fixture.window->modelFilePath(), target);
        QVERIFY(fixture.window->hasChanges());
        QCOMPARE(read(target), savedBytes);
    }

    /** @brief Distinguish native view-state collection from a committed standalone Sidecar write. */
    void serializationDoesNotPretendToSave()
    {
        QMainWindow main;
        auto *mdi = new QMdiArea(&main);
        main.setCentralWidget(mdi);
        QSettings settings(output("host-settings.ini"), QSettings::IniFormat);
        GuiExtensionHost host("org.openswmm.native-state", ExtensionManager::buildManifest(), &main,
                              mdi, ExtensionUiBridge{}, &settings);
        QPointer<SWMMVisProjectWindow> active;
        installExtensionModelAccess(
            &host,
            [&] {
                return active ? QList<SWMMVisProjectWindow *>{active.data()}
                              : QList<SWMMVisProjectWindow *>{};
            },
            [&] { return active.data(); });
        NativeWindow fixture(model(), mdi);
        active = fixture.window;
        mdi->addSubWindow(fixture.window);
        QList<QString> warnings, errors;
        QVERIFY2(fixture.window->loadModel(warnings, errors), qPrintable(errors.join(';')));
        QCoreApplication::processEvents();
        const auto id = fixture.window->extensionSessionId();
        const auto original = fixture.window->property("extensionProjectPath").toString();
        const auto hypothetical = output("collection-only.oswp");
        // The real host collector replaces its own stale view entry even when it has
        // no registered document, proving the signal collected state without saving.
        fixture.window->setProperty(
            "extensionViews",
            QJsonArray{QJsonObject{{"kind", "ext.org.openswmm.native-state.stale-view"}}});
        const auto watermark = mRegistry->snapshot().sequence;
        fixture.window->extensionStateSaving();
        QVERIFY(fixture.window->property("extensionViews").toJsonArray().isEmpty());
        QCOMPARE(fixture.window->property("extensionProjectPath").toString(), original);
        QCOMPARE(mRegistry->snapshot().sequence, watermark);
        QVERIFY(!QFile::exists(hypothetical));
        QVERIFY(events(id, StateEventKind::SaveStarted).isEmpty());
        const auto actual = output("settings/gui.oswp");
        fixture.window->setHasChanges(true);
        QString error;
        QVERIFY2(ProjectSerializer::saveToFile(actual, fixture.window, &error), qPrintable(error));
        QTRY_COMPARE(events(id, StateEventKind::SaveFinished).size(), 1);
        const auto saved = events(id, StateEventKind::SaveFinished).first();
        QCOMPARE(saved.save.kind, SaveKind::Sidecar);
        QCOMPARE(saved.save.outcome, SaveOutcome::Succeeded);
        QCOMPARE(saved.save.targetPath, actual);
        QCOMPARE(fixture.window->property("extensionProjectPath").toString(), actual);
        QVERIFY(fixture.window->hasChanges());
        QVERIFY(QFile::exists(actual));
    }

    /** @brief Keep recovery independent, collect own extension state, and retain inert closed facades. */
    void recoveryPreservesLiveStateAndReportsLockedFailure()
    {
        QMainWindow main;
        auto *mdi = new QMdiArea(&main);
        main.setCentralWidget(mdi);
        QSettings settings(output("host-settings.ini"), QSettings::IniFormat);
        GuiExtensionHost host("org.openswmm.native-state", ExtensionManager::buildManifest(), &main,
                              mdi, ExtensionUiBridge{}, &settings);
        QPointer<SWMMVisProjectWindow> active;
        installExtensionModelAccess(
            &host,
            [&] {
                return active ? QList<SWMMVisProjectWindow *>{active.data()}
                              : QList<SWMMVisProjectWindow *>{};
            },
            [&] { return active.data(); });
        NativeWindow fixture(model(), mdi);
        active = fixture.window;
        mdi->addSubWindow(fixture.window);
        const auto id = fixture.window->extensionSessionId();
        auto *source = qobject_cast<IEventSourceV2 *>(host.lifetime());
        QVERIFY(source);
        QVERIFY(!source->resolveSession(id));
        QList<QString> warnings, errors;
        QVERIFY2(fixture.window->loadModel(warnings, errors), qPrintable(errors.join(';')));
        auto *instance = source->resolveSession(id);
        QVERIFY(instance);
        QCOMPARE(instance->id(), fixture.window->property("extensionInstanceId").toUuid());
        const auto facadeIdentity = instance->id();
        const auto originalProject = instance->project()->path();
        const auto originalInput = fixture.window->modelFilePath();
        QVERIFY(!fixture.window->hasChanges());
        QString error;
        const auto generation = mRegistry->session(id).loadGeneration;
        const auto engine = fixture.window->modelLayer()->engine();
        auto readLease = instance->acquireLease(LeaseKind::Read, &error);
        QVERIFY2(readLease, qPrintable(error));
        warnings.clear();
        errors.clear();
        QVERIFY(!fixture.window->loadModel(warnings, errors));
        QVERIFY(!errors.isEmpty());
        QCOMPARE(mRegistry->session(id).loadGeneration, generation);
        QCOMPARE(fixture.window->modelLayer()->engine(), engine);
        readLease.reset();
        QObject collector;
        QVERIFY(source->observeBeforeSave(&collector, [&](const SaveRequest &request) {
            if (request.sessionId == id && request.kind == SaveKind::Recovery)
                instance->project()->setExtensionData({{"collected", true}});
        }));
        const auto recovery = instance->project()->autosave(&error);
        QVERIFY2(!recovery.isEmpty(), qPrintable(error));
        QVERIFY(QFile::exists(recovery));
        QCOMPARE(instance->project()->path(), originalProject);
        QCOMPARE(fixture.window->modelFilePath(), originalInput);
        QVERIFY(!fixture.window->hasChanges());
        const auto saved = QJsonDocument::fromJson(read(recovery)).object();
        const auto session = saved["sessions"].toArray().first().toObject();
        const auto referenced =
            QFileInfo(recovery).absoluteDir().filePath(session["inpPath"].toString());
        QVERIFY(QFile::exists(referenced));
        QVERIFY(QFileInfo(referenced).absoluteFilePath() != originalInput);
        QVERIFY(session["extensions"]
                    .toObject()["org.openswmm.native-state"]
                    .toObject()["collected"]
                    .toBool());
        QTRY_COMPARE(events(id, StateEventKind::SaveFinished).size(), 1);
        const auto successful = events(id, StateEventKind::SaveFinished).first();
        QCOMPARE(successful.save.kind, SaveKind::Recovery);
        QCOMPARE(successful.save.outcome, SaveOutcome::Succeeded);
        QCOMPARE(successful.save.targetPath, recovery);
        QCOMPARE(successful.session.projectPath, originalProject);
        QVERIFY(!successful.session.dirty);
        const auto recoveryBytes = read(recovery);
        auto lease = instance->acquireLease(LeaseKind::Write, &error);
        QVERIFY2(lease, qPrintable(error));
        warnings.clear();
        errors.clear();
        QVERIFY(!fixture.window->loadModel(warnings, errors));
        QCOMPARE(mRegistry->session(id).loadGeneration, generation);
        QCOMPARE(fixture.window->modelLayer()->engine(), engine);
        error.clear();
        QVERIFY(instance->project()->autosave(&error).isEmpty());
        QVERIFY(!error.isEmpty());
        QTRY_COMPARE(events(id, StateEventKind::SaveFinished).size(), 2);
        const auto failed = events(id, StateEventKind::SaveFinished).last();
        QCOMPARE(failed.save.kind, SaveKind::Recovery);
        QCOMPARE(failed.save.outcome, SaveOutcome::Failed);
        QVERIFY(failed.save.operationId != successful.save.operationId);
        QCOMPARE(read(recovery), recoveryBytes);
        QCOMPARE(instance->project()->path(), originalProject);
        QVERIFY(!fixture.window->hasChanges());
        lease.reset();
        delete fixture.window.data();
        QVERIFY(!source->resolveSession(id));
        QVERIFY(!instance->isOpen());
        QCOMPARE(instance->id(), facadeIdentity);
    }

    /** @brief Stop native Save As safely when state collection deletes its original owner. */
    void beforeSaveDeletionCannotContinueNativeWrite()
    {
        NativeWindow fixture(model());
        QList<QString> warnings, errors;
        QVERIFY2(fixture.window->loadModel(warnings, errors), qPrintable(errors.join(';')));
        const auto id = fixture.window->extensionSessionId();
        const auto target = output("never-written.inp");
        QObject collector;
        connect(mRegistry, &ExtensionStateRegistry::beforeSave, &collector,
                [&](const SaveRequest &request) {
                    if (request.sessionId == id)
                        delete fixture.window.data();
                });
        QString error;
        auto *owner = fixture.window.data();
        QVERIFY(!owner->saveAs(target, &error));
        QVERIFY(!fixture.window);
        QVERIFY(!QFile::exists(target));
        QVERIFY(!error.isEmpty());
        QTRY_COMPARE(events(id, StateEventKind::SaveFinished).size(), 1);
        QCOMPARE(events(id, StateEventKind::SaveFinished).first().save.outcome,
                 SaveOutcome::Failed);
        QVERIFY(mRegistry->session(id).sessionId.isNull());
    }

    /** @brief Abort recovery if collecting plugin data synchronously deletes the model through dirty-state UI. */
    void recoveryCollectionDirtySignalCanDeleteOwner()
    {
        QMainWindow main;
        auto *mdi = new QMdiArea(&main);
        main.setCentralWidget(mdi);
        QSettings settings(output("host-settings.ini"), QSettings::IniFormat);
        GuiExtensionHost host("org.openswmm.native-state", ExtensionManager::buildManifest(), &main,
                              mdi, ExtensionUiBridge{}, &settings);
        QPointer<SWMMVisProjectWindow> active;
        installExtensionModelAccess(
            &host,
            [&] {
                return active ? QList<SWMMVisProjectWindow *>{active.data()}
                              : QList<SWMMVisProjectWindow *>{};
            },
            [&] { return active.data(); });
        const auto input = model();
        const auto originalBytes = read(input);
        NativeWindow fixture(input, mdi);
        active = fixture.window;
        mdi->addSubWindow(fixture.window);
        const auto id = fixture.window->extensionSessionId();
        auto *source = qobject_cast<IEventSourceV2 *>(host.lifetime());
        QVERIFY(source);
        QList<QString> warnings, errors;
        QVERIFY2(fixture.window->loadModel(warnings, errors), qPrintable(errors.join(';')));
        auto *instance = source->resolveSession(id);
        QVERIFY(instance);
        QVERIFY(!fixture.window->hasChanges());
        QObject collector;
        connect(fixture.window, &SWMMVisProjectWindow::hasChangesChanged, &collector,
                [&](bool dirty) {
                    if (dirty)
                        delete fixture.window.data();
                });
        QVERIFY(source->observeBeforeSave(&collector, [&](const SaveRequest &request) {
            if (request.sessionId == id && request.kind == SaveKind::Recovery)
                instance->project()->setExtensionData({{"collected-before-close", true}});
        }));
        QString error;
        QVERIFY(instance->project()->autosave(&error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!fixture.window);
        QCOMPARE(read(input), originalBytes);
        QVERIFY(!QFileInfo::exists(QFileInfo(input).absoluteDir().filePath(".swmmvis/edits")));
        QTRY_COMPARE(events(id, StateEventKind::SaveFinished).size(), 1);
        const auto finished = events(id, StateEventKind::SaveFinished).first();
        QCOMPARE(finished.save.kind, SaveKind::Recovery);
        QCOMPARE(finished.save.outcome, SaveOutcome::Failed);
        QVERIFY(mRegistry->session(id).sessionId.isNull());
        QVERIFY(!source->resolveSession(id));
        QVERIFY(!instance->isOpen());
    }

    /** @brief Restore guarded model state and abort publication if a legacy view collector deletes its owner. */
    void legacyCollectionDeletionCannotContinueNativeWrite()
    {
        const auto input = model();
        const auto originalBytes = read(input);
        NativeWindow fixture(input);
        QList<QString> warnings, errors;
        QVERIFY2(fixture.window->loadModel(warnings, errors), qPrintable(errors.join(';')));
        const auto id = fixture.window->extensionSessionId();
        const auto target = output("never-published.inp");
        QObject collector;
        connect(fixture.window, &SWMMVisProjectWindow::extensionStateSaving, &collector,
                [&] { delete fixture.window.data(); });
        QString error;
        auto *owner = fixture.window.data();
        QVERIFY(!owner->saveAs(target, &error));
        QVERIFY(!fixture.window);
        QVERIFY(!QFile::exists(target));
        QVERIFY(!QFile::exists(ProjectSerializer::sidecarPathFor(target)));
        QCOMPARE(read(input), originalBytes);
        QVERIFY(!error.isEmpty());
        QTRY_COMPARE(events(id, StateEventKind::SaveFinished).size(), 1);
        QCOMPARE(events(id, StateEventKind::SaveFinished).first().save.outcome,
                 SaveOutcome::Failed);
        QVERIFY(mRegistry->session(id).sessionId.isNull());
    }

    /** @brief Abort native publication if a registered document factory deletes its model during view collection. */
    void documentViewCollectionDeletionCannotContinueNativeWrite()
    {
        /** @brief Close the real native owner from inside the SDK's legacy persistence callback. */
        struct ClosingViewFactory final : IDocumentViewFactory {
            QPointer<SWMMVisProjectWindow> owner; ///< Guarded native owner supplied after loading.
            mutable int saves = 0;                ///< Number of view-state collection callbacks.
            /** @brief Identify this case's document type. @return Stable local kind. */
            QString kind() const override { return "close-on-save"; }
            /** @brief Accept only live model documents. @param[in] instance Candidate native facade.
             * @return True for an open model. */
            bool accepts(IModelInstance *instance) const override
            {
                return instance && instance->isOpen();
            }
            /** @brief Supply one host-owned view. @param[in] instance Accepted model, unused here.
             * @param[in] host Owning extension host, unused here. @return New MDI content widget. */
            QWidget *create(IModelInstance *instance, ExtensionHost *host) override
            {
                Q_UNUSED(instance);
                Q_UNUSED(host);
                return new QWidget;
            }
            /** @brief Delete the owner during synchronous state collection.
             * @param[in] view Borrowed document, destroyed with its model and never reused.
             * @return Copied state, even though the native save must now fail. */
            QJsonObject saveState(QWidget *view) const override
            {
                Q_UNUSED(view);
                ++saves;
                delete owner.data();
                return {{"collected-before-close", true}};
            }
            /** @brief Accept an empty restoration operation. @param[in] view Existing document, unused.
             * @param[in] state Saved state, unused. */
            void restoreState(QWidget *view, const QJsonObject &state) override
            {
                Q_UNUSED(view);
                Q_UNUSED(state);
            }
        } factory;
        QMainWindow main;
        auto *mdi = new QMdiArea(&main);
        main.setCentralWidget(mdi);
        QSettings settings(output("host-settings.ini"), QSettings::IniFormat);
        GuiExtensionHost host("org.openswmm.native-state", ExtensionManager::buildManifest(), &main,
                              mdi, ExtensionUiBridge{}, &settings);
        QPointer<SWMMVisProjectWindow> active;
        installExtensionModelAccess(
            &host,
            [&] {
                return active ? QList<SWMMVisProjectWindow *>{active.data()}
                              : QList<SWMMVisProjectWindow *>{};
            },
            [&] { return active.data(); });
        const auto input = model();
        const auto originalBytes = read(input);
        NativeWindow fixture(input, mdi);
        active = fixture.window;
        mdi->addSubWindow(fixture.window);
        const auto id = fixture.window->extensionSessionId();
        QList<QString> warnings, errors;
        QVERIFY2(fixture.window->loadModel(warnings, errors), qPrintable(errors.join(';')));
        auto *source = qobject_cast<IEventSourceV2 *>(host.lifetime());
        QVERIFY(source);
        auto *instance = source->resolveSession(id);
        QVERIFY(instance);
        factory.owner = fixture.window;
        QVERIFY(host.registerDocumentViewFactory(&factory));
        const QPointer<QWidget> view(host.openDocumentView(factory.kind(), instance));
        QVERIFY(view);
        const auto target = output("never-published.inp");
        QString error;
        auto *owner = fixture.window.data();
        QVERIFY(!owner->saveAs(target, &error));
        QCOMPARE(factory.saves, 1);
        QVERIFY(!fixture.window);
        QVERIFY(!view);
        QVERIFY(!QFile::exists(target));
        QVERIFY(!QFile::exists(ProjectSerializer::sidecarPathFor(target)));
        QCOMPARE(read(input), originalBytes);
        QVERIFY(!error.isEmpty());
        QTRY_COMPARE(events(id, StateEventKind::SaveFinished).size(), 1);
        const auto finished = events(id, StateEventKind::SaveFinished).first();
        QCOMPARE(finished.save.kind, SaveKind::Project);
        QCOMPARE(finished.save.outcome, SaveOutcome::Failed);
        QVERIFY(mRegistry->session(id).sessionId.isNull());
        host.unregisterDocumentViewFactory(factory.kind());
    }
};

QTEST_MAIN(TestExtensionLifecycle)
#include "test_extensionlifecycle.moc"
