/**
 * @file extension.cpp
 * @brief Installed-SDK example of authoritative lifecycle snapshots and pre-save collection.
 * @details Subscribes before reading the initial watermark, discards observations
 * already represented by that snapshot, and refreshes its view from current metadata.
 * Queued observers never mutate models. A separate short synchronous pre-save
 * collector stores only this extension's project data and leaves recovery saves alone.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include <sdk/ExtensionHost.hpp>
#include <sdk/ISessionEvents.hpp>
#include <sdk/IGuiExtension.hpp>
#include <sdk/IProject.hpp>

#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QVariant>
#include <QVBoxLayout>

using namespace OpenSWMM::Sdk;

/** @brief Small session-state monitor and namespaced project-state collection example.
 * @details Model UUIDs identify persistent content; session UUIDs identify an open
 * lifetime. The plugin keeps copied snapshots and event counters rather than raw
 * model handles. Dynamic properties on this object and its summary label support
 * independent consumer conformance checks using ordinary Qt APIs. */
class LifecycleExampleExtension final : public QObject,
                                        public IGuiExtension,
                                        public IDockPanelFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OPENSWMM_GUI_EXTENSION_IID FILE "plugin.json")
    Q_INTERFACES(OpenSWMM::Sdk::IGuiExtension)
public:
    /** @brief Describe the independently configured plugin identity.
     * @return Owning metadata matching this module's embedded plugin.json. */
    ExtensionInfo info() const override
    {
        return {QStringLiteral(OPENSWMM_LIFECYCLE_EXAMPLE_ID),
                tr("Session lifecycle example"),
                tr("Ready model sessions, revision snapshots and project-save collection"),
                QStringLiteral("1.0.0"),
                QStringLiteral("OpenSWMM"),
                {},
                QStringLiteral("GPL-3.0-or-later"),
                {QStringLiteral("lifecycle"), QStringLiteral("example")}};
    }

    /** @brief Discover lifecycle services, subscribe before snapshotting and mount a monitor.
     * @param[in] host Borrowed per-plugin host, valid through shutdown().
     * @param[out] error Optional missing-capability, subscription or UI-registration diagnostic.
     * @return True after the snapshot handshake and UI contribution succeed.
     * @note All registration, queries, callbacks and widget operations use the GUI thread. */
    bool initialize(ExtensionHost *host, QString *error) override
    {
        shutdown();
        mSourceLifetime = host ? host->lifetime() : nullptr;
        mSource =
            mSourceLifetime ? qobject_cast<ISessionEvents *>(mSourceLifetime.data()) : nullptr;
        if (!mSource) {
            if (error)
                *error = tr("This example requires the lifecycle event service from SDK 1.3.");
            shutdown();
            return false;
        }
        setObjectName(info().id);
        mSnapshot = {};
        mSnapshotWatermark = 0;
        mQueuedEvents = 0;
        mReadyEvents = 0;
        mBeforeSaves = 0;
        mLastSequence = 0;

        // No nested event loop occurs between subscribing and taking this snapshot.
        mStateConnection = mSource->observeState(this, {}, [this](const StateEvent &event) {
            if (event.sequence <= mSnapshotWatermark)
                return;
            ++mQueuedEvents;
            if (event.kind == StateEventKind::ModelReady)
                ++mReadyEvents;
            mLastSequence = event.sequence;
            // Resolve current display state through a query, not a historical event delta.
            refreshSnapshot();
        });
        mSaveConnection = mSource->observeBeforeSave(this, [this](const SaveRequest &request) {
            ++mBeforeSaves;
            if (request.kind != SaveKind::Recovery && mSourceLifetime && mSource) {
                auto *model = mSource->resolveSession(request.sessionId);
                if (model && model->isOpen()) {
                    if (auto *project = model->project()) {
                        const auto identity = request.sessionId.toString(QUuid::WithoutBraces);
                        auto state = project->extensionData();
                        if (state.value(QStringLiteral("lastCollectedSession")).toString() !=
                            identity) {
                            state.insert(QStringLiteral("lastCollectedSession"), identity);
                            project->setExtensionData(state);
                        }
                    }
                }
            }
            publishView();
        });
        if (!mStateConnection || !mSaveConnection) {
            if (error)
                *error = tr("The host could not register the lifecycle subscriptions.");
            shutdown();
            return false;
        }

        mSnapshot = mSource->stateSnapshot();
        mSnapshotWatermark = mSnapshot.sequence;
        publishView();
        auto *show = new QAction(tr("Show session lifecycle example"), host->lifetime());
        connect(show, &QAction::triggered, host->lifetime(),
                [host]() { host->ui()->showDock(QStringLiteral("lifecycle")); });
        if (!host->ui()->registerAction(QStringLiteral("show"), show) ||
            !host->ui()->registerDockPanelFactory(this)) {
            if (error)
                *error = tr("The host could not register the lifecycle monitor panel.");
            shutdown();
            return false;
        }
        return true;
    }

    /** @brief Disconnect both observation paths before host-owned UI cleanup.
     * @note Idempotent; snapshots/counters are values and no model handle is retained. */
    void shutdown() override
    {
        QObject::disconnect(mStateConnection);
        QObject::disconnect(mSaveConnection);
        mStateConnection = {};
        mSaveConnection = {};
        mSource = nullptr;
        mSourceLifetime = nullptr;
        mStatus = nullptr;
        mDetails = nullptr;
    }

    /** @brief Advertise this example's UI contribution families.
     * @return Names of the registered command and dock contribution families. */
    QStringList contributions() const override
    {
        return {QStringLiteral("actions"), QStringLiteral("dock")};
    }

    /** @brief Identify the extension-local monitor dock.
     * @return Identity qualified by the host with the configured plugin ID. */
    QString id() const override { return QStringLiteral("lifecycle"); }

    /** @brief Name the monitor dock for the View menu.
     * @return Translated user-facing panel title. */
    QString title() const override { return tr("Session lifecycle example"); }

    /** @brief Create a summary, copied session metadata and an explicit refresh button.
     * @param[in] host Borrowed registration host; lifecycle queries use the discovered capability.
     * @return New widget transferred to host ownership.
     * @note The button performs a read-only snapshot query; queued observers issue no mutations. */
    QWidget *create(ExtensionHost *host) override
    {
        Q_UNUSED(host);
        auto *widget = new QWidget;
        auto *layout = new QVBoxLayout(widget);
        auto *description = new QLabel(
            tr("Session IDs identify open lifetimes. Model IDs persist in saved projects. "
               "Ready means the model and synchronous host restoration are complete; "
               "optional map resources can finish loading later."),
            widget);
        description->setWordWrap(true);
        layout->addWidget(description);
        mStatus = new QLabel(widget);
        mStatus->setObjectName(QStringLiteral("ext.%1.lifecycle-status").arg(info().id));
        mStatus->setTextFormat(Qt::PlainText);
        mStatus->setWordWrap(true);
        layout->addWidget(mStatus);
        mDetails = new QLabel(widget);
        mDetails->setObjectName(QStringLiteral("ext.%1.lifecycle-sessions").arg(info().id));
        mDetails->setTextFormat(Qt::PlainText);
        mDetails->setWordWrap(true);
        mDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(mDetails);
        auto *refresh = new QPushButton(tr("Refresh session snapshot"), widget);
        refresh->setObjectName(QStringLiteral("ext.%1.lifecycle-refresh").arg(info().id));
        connect(refresh, &QPushButton::clicked, widget, [this]() { refreshSnapshot(); });
        layout->addWidget(refresh);
        layout->addStretch();
        publishView();
        return widget;
    }

private:
    /** @brief Describe the copied readiness status without private model enums.
     * @param[in] status Public lifecycle status from a snapshot.
     * @return Translated status caption. */
    static QString statusText(SessionStatus status)
    {
        switch (status) {
        case SessionStatus::Created:
            return tr("Created");
        case SessionStatus::Loading:
            return tr("Loading");
        case SessionStatus::Ready:
            return tr("Ready");
        case SessionStatus::Failed:
            return tr("Failed");
        case SessionStatus::Closing:
            return tr("Closing");
        }
        return tr("Unknown");
    }

    /** @brief Refresh current metadata without changing the initial event cutoff.
     * @note Keeping the initial watermark lets counters include every future accepted
     * observation even when a fresh display snapshot already includes later transitions. */
    void refreshSnapshot()
    {
        if (!mSourceLifetime || !mSource)
            return;
        mSnapshot = mSource->stateSnapshot();
        publishView();
    }

    /** @brief Mirror copied state/counters on the plugin object and optional summary label. */
    void publishView()
    {
        const auto setCounters = [this](QObject *object) {
            object->setProperty("queuedEvents", QVariant::fromValue(mQueuedEvents));
            object->setProperty("readyEvents", QVariant::fromValue(mReadyEvents));
            object->setProperty("beforeSaves", QVariant::fromValue(mBeforeSaves));
            object->setProperty("lastSequence", QVariant::fromValue(mLastSequence));
            object->setProperty("sessionCount",
                                QVariant::fromValue(quint64(mSnapshot.sessions.size())));
            object->setProperty("snapshotWatermark", QVariant::fromValue(mSnapshotWatermark));
            object->setProperty("lastSnapshotSequence", QVariant::fromValue(mSnapshot.sequence));
        };
        setCounters(this);
        if (mStatus) {
            setCounters(mStatus);
            mStatus->setText(tr("%1 open sessions; %2 queued updates; %3 ready events; "
                                "%4 save collections.")
                                 .arg(mSnapshot.sessions.size())
                                 .arg(mQueuedEvents)
                                 .arg(mReadyEvents)
                                 .arg(mBeforeSaves));
        }
        if (!mDetails)
            return;
        QStringList rows;
        for (const auto &session : mSnapshot.sessions) {
            rows.append(tr("%1\nSession: %2\nModel: %3\n%4; load %5; state revision %6; "
                           "model revision %7\nInput: %8\nProject: %9")
                            .arg(session.title, session.sessionId.toString(QUuid::WithoutBraces),
                                 session.instanceId.toString(QUuid::WithoutBraces),
                                 statusText(session.status))
                            .arg(session.loadGeneration)
                            .arg(session.stateRevision)
                            .arg(session.modelRevision)
                            .arg(session.inputPath, session.projectPath));
        }
        mDetails->setText(rows.isEmpty() ? tr("Open a model to observe its session.")
                                         : rows.join(QStringLiteral("\n\n")));
    }

    ISessionEvents *mSource =
        nullptr; ///< Borrowed capability, used only with the live source guard.
    QPointer<QObject> mSourceLifetime; ///< Borrowed host lifetime; never transferred or reparented.
    QMetaObject::Connection mStateConnection; ///< Queued lifecycle observation subscription.
    QMetaObject::Connection mSaveConnection;  ///< Synchronous pre-save collection subscription.
    QPointer<QLabel> mStatus;  ///< Host-owned summary label, guarded through contribution teardown.
    QPointer<QLabel> mDetails; ///< Host-owned copied-state details, guarded through teardown.
    StateSnapshot mSnapshot;   ///< Latest owning metadata snapshot, containing no model handles.
    quint64 mSnapshotWatermark =
        0; ///< Initial subscribe-then-snapshot cutoff, never advanced by refresh.
    quint64 mQueuedEvents = 0; ///< Future accepted observations beyond the initial watermark.
    quint64 mReadyEvents = 0;  ///< Accepted ModelReady observations beyond the initial watermark.
    quint64 mBeforeSaves =
        0; ///< Synchronous collection callbacks, including skipped recovery writes.
    quint64 mLastSequence =
        0; ///< Last delivered queued event, separate from the snapshot watermark.
};

#include "extension.moc"
