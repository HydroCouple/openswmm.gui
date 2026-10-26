/**
 * @file test_activityui.cpp
 * @brief Scoped UI activity acceptance with real Qt hosts, commands, docks and MDI documents.
 * @details Uses no native model/engine objects. Every settings file stays beneath
 * the repository verification directory; runtime identities are queried through
 * the public optional services and no public getter constructs native state.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionactivityui.h"
#include "plugins/extensionmanager.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QSettings>
#include <QTest>
#include <utility>

using namespace OpenSWMM::Sdk;
using namespace openswmmvis;

namespace
{
/** @brief Isolated generic native host state, owned in dependency order. */
struct UiFixture {
    ExtensionStateRegistry registry; ///< Shared authoritative registry substitute.
    QMainWindow window;              ///< Native widget owner, outliving its extension hosts.
    QMdiArea *mdi = nullptr;         ///< Window-owned document area.
    QSettings settings{QStringLiteral(SDK_UI_ACTIVITY_VERIFICATION_DIRECTORY "/host.ini"),
                       QSettings::IniFormat}; ///< Reviewable test settings.
    std::unique_ptr<GuiExtensionHost> host;   ///< Actual public SDK implementation.
    /** @brief Create a fresh host and empty MDI area with reviewable settings. */
    UiFixture()
    {
        settings.clear();
        mdi = new QMdiArea(&window);
        window.setCentralWidget(mdi);
        window.resize(900, 600);
        host = makeHost("org.openswmm.ui-first");
    }
    /** @brief Create another host sharing the native window but no private contributions.
     * @param[in] id Distinct configured plugin identity.
     * @param[in] bridge Optional approved native dialog catalogue callbacks.
     * @return Owning host. */
    std::unique_ptr<GuiExtensionHost> makeHost(const QString &id, ExtensionUiBridge bridge = {})
    {
        return std::make_unique<GuiExtensionHost>(id, ExtensionManager::buildManifest(), &window,
                                                  mdi, std::move(bridge), &settings, nullptr,
                                                  &registry);
    }
    /** @brief Query copied scoped state without inspecting private tracker objects.
     * @return Current public snapshot. */
    ActivitySnapshot snapshot() const { return host->activitySnapshot(); }
};

/** @brief Small dock factory with optional reentrant creation behavior. */
struct DockFactory : IDockPanelFactory {
    QString localId = "panel";                    ///< Registered local key.
    std::function<void()> creating;               ///< Optional tested reentrant callback.
    std::function<void(const QString &)> reading; ///< Optional metadata getter callback.
    std::function<QWidget *()> makeWidget;        ///< Optional content with native event hooks.
    QPointer<QWidget> created; ///< Detects correct ownership after creation refusal.
    QString id() const override
    {
        if (reading)
            reading("id");
        return localId;
    }
    QString title() const override
    {
        if (reading)
            reading("title");
        return "Test panel";
    }
    Qt::DockWidgetArea defaultArea() const override
    {
        if (reading)
            reading("area");
        return Qt::RightDockWidgetArea;
    }
    QString tabifyWith() const override
    {
        if (reading)
            reading("tabify");
        return {};
    }
    QWidget *create(ExtensionHost *) override
    {
        if (creating)
            creating();
        created = makeWidget ? makeWidget() : new QLabel("Dock contents");
        return created;
    }
};

/** @brief Independent/model-bound document factory with reentrant hooks. */
struct DocumentFactory : IDocumentViewFactory {
    std::function<void()> creating;        ///< Optional creation hook.
    std::function<void()> restoring;       ///< Optional persistence hook.
    std::function<void()> readingKind;     ///< Optional reentrant document-kind getter callback.
    std::function<QWidget *()> makeWidget; ///< Optional content with native event hooks.
    QPointer<QWidget> created; ///< Host-transferred widget, guarded after refusal/removal.
    QString kind() const override
    {
        if (readingKind)
            readingKind();
        return "document";
    }
    bool accepts(IModelInstance *) const override { return true; }
    QWidget *create(IModelInstance *, ExtensionHost *) override
    {
        if (creating)
            creating();
        created = makeWidget ? makeWidget() : new QLabel("Document contents");
        created->setWindowTitle("Extension document");
        return created;
    }
    QJsonObject saveState(QWidget *) const override { return {{"saved", true}}; }
    void restoreState(QWidget *, const QJsonObject &) override
    {
        if (restoring)
            restoring();
    }
};

/** @brief Invoke one-shot hooks from actual native widget attachment/show events. */
class LifecycleHookWidget final : public QLabel
{
public:
    std::function<void()> whenShown; ///< Called after the content receives its first show event.
    std::function<void()> whenDockAttached; ///< Called after reparenting into a native dock.

protected:
    /** @brief Run hooks after normal Qt processing, before returning to the host native route.
     * @param[in] event Borrowed native widget event.
     * @return The ordinary QLabel event result. */
    bool event(QEvent *event) override
    {
        const auto type = event->type();
        const bool result = QLabel::event(event);
        std::function<void()> hook;
        if (type == QEvent::Show)
            hook = std::exchange(whenShown, {});
        else if (type == QEvent::ParentChange && qobject_cast<QDockWidget *>(parentWidget()))
            hook = std::exchange(whenDockAttached, {});
        if (hook)
            hook();
        return result;
    }
};

/** @brief Lifetime-only SDK model substitute; no engine pointer is fabricated. */
struct ModelFacade : IModelInstance {
    QPointer<QObject> owner;                ///< Guarded native document lifetime.
    QUuid persistent = QUuid::createUuid(); ///< Persistent identity independent of document UUID.
    explicit ModelFacade(QObject *native) : owner(native) {}
    QUuid id() const override { return persistent; }
    QString title() const override { return "Model"; }
    QString inpPath() const override { return {}; }
    QString engineVersion() const override { return {}; }
    QObject *lifetime() const override { return owner; }
    IProject *project() const override { return nullptr; }
    bool isOpen() const override { return owner; }
    QList<QVariantMap> selection() const override { return {}; }
    void select(const QList<QVariantMap> &) override {}
    QString snapshot(QString *) const override { return {}; }
    QVariantMap attributes(const QString &, const QString &) const override { return {}; }
    IAttributeProvider *attributeProvider() const override { return nullptr; }
    QList<QVariantMap> results() const override { return {}; }
    std::unique_ptr<IEngineLease> acquireLease(LeaseKind, QString *) override { return {}; }
    std::unique_ptr<INativeEditScope> nativeEdit(const QString &, QString *) override { return {}; }
    std::unique_ptr<IEditScope> edit(const QString &, QString *) override { return {}; }
    bool registerAttributeProvider(const QString &, IComputedAttributeProvider *, QObject *,
                                   QString *) override
    {
        return false;
    }
    void unregisterAttributeProvider(const QString &) override {}
    void refreshAttributeProvider(const QString &) override {}
};

/** @brief Copy one public command registration by its local/approved ID.
 * @param[in] state Owning scoped metadata.
 * @param[in] id Local/approved string identity.
 * @param[in] builtin Distinguish approved native commands from own actions.
 * @return Copied registration, with null UUID when missing. */
CommandState commandState(const ActivitySnapshot &state, const QString &id, bool builtin = false)
{
    for (const auto &value : state.ui.commands)
        if (value.id == id && value.builtin == builtin)
            return value;
    return {};
}
/** @brief Copy one calling host's dock registration.
 * @param[in] state Owning metadata.
 * @param[in] id Local dock key.
 * @return Copied registration, with null UUID when missing. */
DockState dockState(const ActivitySnapshot &state, const QString &id)
{
    for (const auto &value : state.ui.docks)
        if (value.id == id)
            return value;
    return {};
}
}

/** @brief Exercise UI lifetimes through actual Qt ownership and optional SDK services. */
class ActivityUiTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Retain every settings artifact in the requested reviewable repository tree. */
    void initTestCase() { QVERIFY(QDir().mkpath(SDK_UI_ACTIVITY_VERIFICATION_DIRECTORY)); }

    /** @brief Query pure shared native document state while private contributions stay scoped. */
    void snapshotsArePureAndContributionsAreScoped()
    {
        UiFixture f;
        auto second = f.makeHost("org.openswmm.ui-second");
        QAction firstAction("First", f.host.get()), secondAction("Second", second.get());
        QVERIFY(f.host->registerAction("first", &firstAction));
        QVERIFY(second->registerAction("second", &secondAction));
        const auto initial = f.snapshot();
        QVERIFY(!initial.ui.windowId.isNull());
        QCOMPARE(initial.ui.windowId, second->activitySnapshot().ui.windowId);
        QVERIFY(!commandState(initial, "first").commandId.isNull());
        QVERIFY(commandState(initial, "second").commandId.isNull());
        QVERIFY(commandState(second->activitySnapshot(), "first").commandId.isNull());
        for (int i = 0; i < 20; ++i) {
            QCOMPARE(f.snapshot().sequence, initial.sequence);
            QCOMPARE(f.snapshot().ui.commands.size(), 1);
        }
        UiActivityCommand foreign{UiActivityCommandKind::TriggerCommand, initial.ui.windowId,
                                  commandState(initial, "first").commandId};
        QString error;
        QVERIFY(!second->executeActivityUiCommand(foreign, &error));
        QVERIFY(!error.isEmpty());
    }

    /** @brief Coalesce changed QAction state, preserve origin, and reject stale registrations. */
    void commandStateOriginNoopsAndRecreation()
    {
        UiFixture f;
        QAction action("Own command", f.host.get());
        action.setCheckable(true);
        QVERIFY(f.host->registerAction("command", &action));
        // Isolate the tagged command from the earlier untagged registration invalidation.
        QCoreApplication::processEvents();
        const auto original = commandState(f.snapshot(), "command");
        QObject receiver;
        QList<ActivityEvent> events;
        f.host->observeActivity(&receiver, {{ActivityEventKind::CommandChanged}},
                                [&](const ActivityEvent &event) { events.append(event); });
        UiActivityCommand request{UiActivityCommandKind::TriggerCommand, f.snapshot().ui.windowId,
                                  original.commandId, true, QUuid::createUuid()};
        QString error;
        QVERIFY2(f.host->executeActivityUiCommand(request, &error), qPrintable(error));
        QVERIFY(commandState(f.snapshot(), "command").checked);
        QTRY_COMPARE(events.size(), 1);
        QCOMPARE(events.first().origin, request.origin);
        events.clear();
        QVERIFY2(f.host->executeActivityUiCommand(request, &error), qPrintable(error));
        action.setText("Mixed command and native state");
        QTRY_COMPARE(events.size(), 1);
        QVERIFY(events.first().origin.isNull());
        events.clear();
        for (int i = 0; i < 1000; ++i)
            action.setText(QString::number(i));
        QCOMPARE(commandState(f.snapshot(), "command").title, "999");
        QTRY_COMPARE(events.size(), 1);
        const auto stable = f.snapshot().sequence;
        action.setText("999");
        QCOMPARE(f.snapshot().sequence, stable);
        action.setEnabled(false);
        QVERIFY(!f.host->executeActivityUiCommand(request, &error));
        f.host->unregisterAction("command");
        QVERIFY(commandState(f.snapshot(), "command").commandId.isNull());
        action.setEnabled(true);
        QVERIFY(f.host->registerAction("command", &action));
        QVERIFY(commandState(f.snapshot(), "command").commandId != original.commandId);
        QVERIFY(!f.host->executeActivityUiCommand(request, &error));
    }

    /** @brief Include only approved built-in dialogs and invalidate destroyed metadata. */
    void approvedBuiltinCommandsStayValidated()
    {
        UiFixture f;
        QAction builtin("Options...", &f.window), forbidden("Foreign", &f.window);
        int triggers = 0;
        connect(&builtin, &QAction::triggered, this, [&] { ++triggers; });
        ExtensionUiBridge bridge;
        bridge.dialogIds = [] { return QStringList{"native.options", "ext.foreign.action"}; };
        bridge.dialogAction = [&](const QString &id) {
            return id == "native.options" ? &builtin : &forbidden;
        };
        bridge.openDialog = [&](const QString &id) {
            if (id != "native.options" || !builtin.isEnabled())
                return false;
            builtin.trigger();
            return true;
        };
        auto host = f.makeHost("org.openswmm.ui-dialogs", bridge);
        const auto state = host->activitySnapshot();
        QCOMPARE(state.ui.commands.size(), 1);
        const auto approved = commandState(state, "native.options", true);
        QVERIFY(!approved.commandId.isNull());
        UiActivityCommand request{UiActivityCommandKind::TriggerCommand, state.ui.windowId,
                                  approved.commandId};
        QString error;
        QVERIFY(host->executeActivityUiCommand(request, &error));
        QCOMPARE(triggers, 1);
        builtin.setEnabled(false);
        QVERIFY(!host->executeActivityUiCommand(request, &error));
        QCOMPARE(triggers, 1);
    }

    /** @brief Tab exposure differs from logical openness, and recreation retires old dock IDs. */
    void dockVisibilityTabificationAndRecreation()
    {
        UiFixture f;
        DockFactory first, second;
        first.localId = "first";
        second.localId = "second";
        QVERIFY(f.host->registerDockPanelFactory(&first));
        QVERIFY(f.host->registerDockPanelFactory(&second));
        auto *a = f.window.findChild<QDockWidget *>("ext.org.openswmm.ui-first.first");
        auto *b = f.window.findChild<QDockWidget *>("ext.org.openswmm.ui-first.second");
        QVERIFY(a && b);
        f.window.show();
        f.window.tabifyDockWidget(a, b);
        b->show();
        b->raise();
        QTRY_VERIFY(dockState(f.snapshot(), "first").open);
        QTRY_VERIFY(dockState(f.snapshot(), "second").open);
        QTRY_VERIFY(dockState(f.snapshot(), "second").exposed);
        QTRY_VERIFY(!dockState(f.snapshot(), "first").exposed);
        const auto original = dockState(f.snapshot(), "first");
        UiActivityCommand request{UiActivityCommandKind::SetDockOpen, f.snapshot().ui.windowId,
                                  original.dockId, false, QUuid::createUuid()};
        QString error;
        QVERIFY(f.host->executeActivityUiCommand(request, &error));
        QVERIFY(!dockState(f.snapshot(), "first").open);
        const auto stable = f.snapshot().sequence;
        QVERIFY(f.host->executeActivityUiCommand(request, &error));
        QCOMPARE(f.snapshot().sequence, stable);
        f.host->unregisterDockPanelFactory("first");
        QVERIFY(first.created.isNull());
        QVERIFY(f.host->registerDockPanelFactory(&first));
        QVERIFY(dockState(f.snapshot(), "first").dockId != original.dockId);
        QVERIFY(!f.host->executeActivityUiCommand(request, &error));
    }

    /** @brief Actual utility focus stays separate from a cached active model facade. */
    void utilityFocusHasIndependentCanonicalDocumentIdentity()
    {
        UiFixture f;
        auto second = f.makeHost("org.openswmm.ui-second");
        auto *model = new QMdiSubWindow;
        model->setWidget(new QLabel("Model"));
        model->setWindowTitle("Model");
        ModelFacade facade(model);
        const auto session =
            f.registry.registerSession(model, {facade.id(), "Model", {}, {}, false, 0});
        f.registry.finishLoad(session, f.registry.beginLoad(session), true);
        f.host->setModelAccess({}, [&] { return &facade; });
        f.mdi->addSubWindow(model);
        auto *utility = f.mdi->addSubWindow(new QLabel("Utility"));
        utility->setWindowTitle("Utility");
        f.window.show();
        model->show();
        utility->show();
        f.mdi->setActiveSubWindow(model);
        QTRY_COMPARE(f.snapshot().ui.documents.size(), 2);
        QUuid modelId, utilityId;
        for (const auto &state : f.snapshot().ui.documents)
            if (state.title == "Model") {
                modelId = state.documentId;
                QCOMPARE(state.kind, DocumentKind::Model);
                QCOMPARE(state.sessionId, session);
            } else if (state.title == "Utility") {
                utilityId = state.documentId;
                QCOMPARE(state.kind, DocumentKind::Utility);
                QVERIFY(state.sessionId.isNull());
            }
        QVERIFY(!modelId.isNull() && !utilityId.isNull());
        f.mdi->setActiveSubWindow(utility);
        QTRY_COMPARE(f.snapshot().ui.activeDocumentId, utilityId);
        QCOMPARE(f.host->activeInstance(), static_cast<IModelInstance *>(&facade));
        QCOMPARE(second->activitySnapshot().ui.activeDocumentId, utilityId);
        UiActivityCommand request{UiActivityCommandKind::ActivateDocument, f.snapshot().ui.windowId,
                                  modelId};
        QString error;
        QVERIFY(f.host->executeActivityUiCommand(request, &error));
        QCOMPARE(f.snapshot().ui.activeDocumentId, modelId);
        const auto stable = f.snapshot().sequence;
        QVERIFY(f.host->executeActivityUiCommand(request, &error));
        QCOMPARE(f.snapshot().sequence, stable);
        f.registry.beginClosing(session);
        QVERIFY(!f.host->executeActivityUiCommand(request, &error));
    }

    /** @brief MDI removal/re-add of one native pointer creates a new runtime attachment UUID. */
    void documentDetachAndReattachRetiresIdentity()
    {
        UiFixture f;
        auto *sub = f.mdi->addSubWindow(new QLabel("Utility"));
        sub->setWindowTitle("Utility");
        sub->show();
        QTRY_COMPARE(f.snapshot().ui.documents.size(), 1);
        const auto original = f.snapshot().ui.documents.first().documentId;
        f.mdi->removeSubWindow(sub);
        QTRY_VERIFY(f.snapshot().ui.documents.isEmpty());
        UiActivityCommand stale{UiActivityCommandKind::ActivateDocument, f.snapshot().ui.windowId,
                                original};
        QString error;
        QVERIFY(!f.host->executeActivityUiCommand(stale, &error));
        f.mdi->addSubWindow(sub);
        sub->show();
        QTRY_COMPARE(f.snapshot().ui.documents.size(), 1);
        QVERIFY(f.snapshot().ui.documents.first().documentId != original);
        QVERIFY(!f.host->executeActivityUiCommand(stale, &error));
    }

    /** @brief Track mounted extension documents with explicit owner/model associations. */
    void extensionDocumentsShareCanonicalMetadata()
    {
        UiFixture f;
        auto second = f.makeHost("org.openswmm.ui-second");
        QObject native;
        ModelFacade facade(&native);
        const auto session =
            f.registry.registerSession(&native, {facade.id(), "Model", {}, {}, false, 0});
        f.registry.finishLoad(session, f.registry.beginLoad(session), true);
        DocumentFactory factory;
        QVERIFY(f.host->registerDocumentViewFactory(&factory));
        auto *widget = f.host->openDocumentView(factory.kind(), &facade);
        QVERIFY(widget);
        QTRY_COMPARE(f.snapshot().ui.documents.size(), 1);
        const auto original = f.snapshot().ui.documents.first();
        QCOMPARE(original.kind, DocumentKind::Extension);
        QCOMPARE(original.ownerPluginId, f.host->pluginId());
        QCOMPARE(original.sessionId, session);
        QCOMPARE(second->activitySnapshot().ui.documents.first().documentId, original.documentId);
        auto *sub = qobject_cast<QMdiSubWindow *>(widget->parentWidget());
        QVERIFY(sub);
        sub->setWindowTitle("Renamed document");
        QCOMPARE(f.snapshot().ui.documents.first().title, "Renamed document");
        f.host->unregisterDocumentViewFactory(factory.kind());
        QTRY_VERIFY(f.snapshot().ui.documents.isEmpty());
        QTRY_VERIFY(second->activitySnapshot().ui.documents.isEmpty());
        QVERIFY(factory.created.isNull());
    }

    /** @brief Mount extension metadata only for a surviving model/factory/host. */
    void reentrantCreationCannotLeaveOrphanViews()
    {
        UiFixture f;
        DocumentFactory factory;
        QVERIFY(f.host->registerDocumentViewFactory(&factory));
        auto native = std::make_unique<QObject>();
        ModelFacade facade(native.get());
        factory.creating = [&] { native.reset(); };
        QVERIFY(!f.host->openDocumentView(factory.kind(), &facade));
        QVERIFY(factory.created.isNull());
        QVERIFY(f.mdi->subWindowList().isEmpty());
        factory.creating = [&] { f.host->unregisterDocumentViewFactory(factory.kind()); };
        QVERIFY(!f.host->openDocumentView(factory.kind(), nullptr));
        QVERIFY(factory.created.isNull());
        QVERIFY(f.mdi->subWindowList().isEmpty());
        QVERIFY(f.host->registerDocumentViewFactory(&factory));
        factory.creating = [&] { f.host.reset(); };
        auto *host = f.host.get();
        QVERIFY(!host->openDocumentView(factory.kind(), nullptr));
        QVERIFY(factory.created.isNull());
        QVERIFY(f.mdi->subWindowList().isEmpty());
    }

    /** @brief Reserve dock IDs through reentrant callbacks and discard refused factory widgets. */
    void reentrantDockCreationIsGuarded()
    {
        UiFixture f;
        DockFactory factory;
        factory.creating = [&] { f.host->unregisterDockPanelFactory(factory.id()); };
        QVERIFY(!f.host->registerDockPanelFactory(&factory));
        QVERIFY(factory.created.isNull());
        QVERIFY(f.snapshot().ui.docks.isEmpty());
        factory.creating = {};
        QVERIFY(f.host->registerDockPanelFactory(&factory));
        f.host->unregisterDockPanelFactory(factory.id());
        QVERIFY(factory.created.isNull());
        factory.creating = [&] { f.host.reset(); };
        auto *host = f.host.get();
        QVERIFY(!host->registerDockPanelFactory(&factory));
        QVERIFY(factory.created.isNull());
    }

    /** @brief Exercise every metadata callback before any native content is constructed. */
    void metadataGetterShutdownIsGuarded_data()
    {
        QTest::addColumn<QString>("getter");
        QTest::newRow("dock-id") << QString("id");
        QTest::newRow("dock-title") << QString("title");
        QTest::newRow("dock-area") << QString("area");
        QTest::newRow("dock-tabify") << QString("tabify");
        QTest::newRow("document-kind") << QString("kind");
    }

    /** @brief Reentrant metadata shutdown refuses registration before invoking the factory. */
    void metadataGetterShutdownIsGuarded()
    {
        QFETCH(QString, getter);
        UiFixture f;
        bool read = false;
        bool created = false;
        if (getter == "kind") {
            DocumentFactory factory;
            factory.readingKind = [&] {
                read = true;
                f.host->prepareShutdown();
            };
            factory.creating = [&] { created = true; };
            QVERIFY(!f.host->registerDocumentViewFactory(&factory));
        } else {
            DockFactory factory;
            factory.reading = [&](const QString &field) {
                if (field == getter) {
                    read = true;
                    f.host->prepareShutdown();
                }
            };
            factory.creating = [&] { created = true; };
            QVERIFY(!f.host->registerDockPanelFactory(&factory));
        }
        QVERIFY(read);
        QVERIFY(!created);
        QVERIFY(f.window.findChildren<QDockWidget *>().isEmpty());
        QVERIFY(f.mdi->subWindowList().isEmpty());
        QVERIFY(f.host);
    }

    /** @brief A metadata callback can cancel a reserved dock key without blocking a later retry. */
    void cancelledMetadataReservationCanBeReused()
    {
        UiFixture f;
        DockFactory factory;
        factory.reading = [&](const QString &field) {
            if (field == "title")
                f.host->unregisterDockPanelFactory(factory.localId);
        };
        QVERIFY(!f.host->registerDockPanelFactory(&factory));
        QVERIFY(factory.created.isNull());
        QVERIFY(f.snapshot().ui.docks.isEmpty());
        factory.reading = {};
        QVERIFY(f.host->registerDockPanelFactory(&factory));
        QVERIFY(factory.created);
        QVERIFY(!dockState(f.snapshot(), factory.localId).dockId.isNull());
        f.host->unregisterDockPanelFactory(factory.localId);
        QVERIFY(factory.created.isNull());
    }

    /** @brief Shutdown from a mounted content show event discards both document and MDI wrapper. */
    void shutdownDuringMountedDocumentShowLeavesNoOrphan()
    {
        UiFixture f;
        f.window.show();
        DocumentFactory factory;
        bool stopped = false;
        bool wasMounted = false;
        QPointer<QMdiSubWindow> mounted;
        factory.makeWidget = [&] {
            auto *widget = new LifecycleHookWidget;
            widget->whenShown = [&, widget] {
                mounted = qobject_cast<QMdiSubWindow *>(widget->parentWidget());
                wasMounted = mounted && f.mdi->subWindowList().contains(mounted.data());
                f.host->prepareShutdown();
                stopped = true;
            };
            return widget;
        };
        QVERIFY(f.host->registerDocumentViewFactory(&factory));
        auto *result = f.host->openDocumentView(factory.kind(), nullptr);
        QVERIFY(stopped);
        QVERIFY(wasMounted);
        QVERIFY(!result);
        QVERIFY(factory.created.isNull());
        QVERIFY(mounted.isNull());
        QVERIFY(f.mdi->subWindowList().isEmpty());
        QVERIFY(f.window.findChildren<QMdiSubWindow *>().isEmpty());
        QVERIFY(f.host);
        QVERIFY(f.window.isVisible());
    }

    /** @brief Shutdown from native dock attachment removes a failed dock while its window survives. */
    void shutdownDuringDockAttachmentLeavesNoOrphan()
    {
        UiFixture f;
        f.window.show();
        DockFactory factory;
        bool stopped = false;
        bool wasAttached = false;
        QPointer<QDockWidget> attached;
        factory.makeWidget = [&] {
            auto *widget = new LifecycleHookWidget;
            widget->whenDockAttached = [&, widget] {
                attached = qobject_cast<QDockWidget *>(widget->parentWidget());
                wasAttached = attached && attached->parentWidget() == &f.window;
                f.host->prepareShutdown();
                stopped = true;
            };
            return widget;
        };
        const bool accepted = f.host->registerDockPanelFactory(&factory);
        QVERIFY(stopped);
        QVERIFY(wasAttached);
        QVERIFY(!accepted);
        QVERIFY(factory.created.isNull());
        QVERIFY(attached.isNull());
        QVERIFY(f.window.findChildren<QDockWidget *>().isEmpty());
        QVERIFY(f.host);
        QVERIFY(f.window.isVisible());
    }

    /** @brief Stop removes private cached state and prevents late native updates from recreating it. */
    void shutdownPreventsMetadataResurrection()
    {
        UiFixture f;
        QAction action("Own", &f.window);
        QVERIFY(f.host->registerAction("own", &action));
        const auto ownerId = f.host->activityOwnerId();
        const auto windowId = f.host->activityWindowId();
        f.host->prepareShutdown();
        action.setText("Late native change");
        QCoreApplication::processEvents();
        QVERIFY(f.registry.activitySnapshot(ownerId, windowId).ui.windowId.isNull());
        QVERIFY(f.host->activitySnapshot().ui.windowId.isNull());
        UiActivityCommand request{UiActivityCommandKind::TriggerCommand, windowId,
                                  QUuid::createUuid()};
        QString error;
        QVERIFY(!f.host->executeActivityUiCommand(request, &error));
    }
};
QTEST_MAIN(ActivityUiTest)
#include "test_activityui.moc"
