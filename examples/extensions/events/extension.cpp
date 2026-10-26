/**
 * @file extension.cpp
 * @brief Independently built example of typed event subscriptions and Qt signal adaptation.
 * @details Uses only the installed SDK and Qt. One receiver-scoped callback and one
 * plugin-local QObject adapter observe this extension's own settings notifications.
 * An explicit button changes a setting; event handlers only update the example's view.
 * Build the source in separate directories with different plugin identities to verify
 * that independently compiled adapter metaobjects and settings scopes remain isolated.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include <sdk/ExtensionHost.hpp>
#include <sdk/IEventSourceV1.hpp>
#include <sdk/IGuiExtension.hpp>
#include <sdk/qt/EventSignalAdapter.hpp>

#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>
#include <variant>

using namespace OpenSWMM::Sdk;

/**
 * @brief Small dock plugin demonstrating both supported typed subscription styles.
 * @details The plugin QObject exposes typedEvents, adapterEvents, lastSequence,
 * lastKey and eventAdapter dynamic properties for a consumer conformance test.
 * The dock's status label mirrors the counters so a test can inspect them through
 * ordinary Qt widgets without accessing a private host object. Adapter callbacks
 * use direct GUI-thread connections and must not throw.
 */
class EventExampleExtension final : public QObject, public IGuiExtension, public IDockPanelFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OPENSWMM_GUI_EXTENSION_IID FILE "plugin.json")
    Q_INTERFACES(OpenSWMM::Sdk::IGuiExtension)
public:
    /** @brief Describe the independently configured plugin identity.
     * @return Owning metadata matching this build's embedded plugin.json. */
    ExtensionInfo info() const override
    {
        return {QStringLiteral(OPENSWMM_EVENTS_EXAMPLE_ID),
                tr("Typed event example"),
                tr("Typed callbacks and plugin-local Qt signals from the installed SDK"),
                QStringLiteral("1.0.0"),
                QStringLiteral("OpenSWMM"),
                {},
                QStringLiteral("GPL-3.0-or-later"),
                {QStringLiteral("events"), QStringLiteral("example")}};
    }

    /** @brief Subscribe to scoped settings changes and contribute a small dock.
     * @param[in] host Borrowed per-plugin SDK host, kept alive through shutdown().
     * @param[out] error Optional missing-capability or contribution failure detail.
     * @return True after both subscriptions receive the initial demonstration event.
     * @note Registration, callbacks and widget operations all use the GUI thread. */
    bool initialize(ExtensionHost *host, QString *error) override
    {
        shutdown();
        mHost = host;
        mTypedEvents = 0;
        mAdapterEvents = 0;
        mLastSequence = 0;
        mLastKey.clear();
        setObjectName(info().id);
        refreshStatus();
        auto *source = host ? qobject_cast<IEventSourceV1 *>(host->lifetime()) : nullptr;
        if (!source) {
            if (error)
                *error = tr("This example requires the typed event service from SDK 1.2.");
            shutdown();
            return false;
        }

        EventFilter filter;
        filter.kinds = {EventKind::SettingsChanged};
        mTypedConnection = source->subscribe(this, filter, [this](const Event &event) {
            const auto *settings = std::get_if<SettingsEvent>(&event.payload);
            if (!settings)
                return;
            ++mTypedEvents;
            mLastSequence = qMax(mLastSequence, event.sequence);
            mLastKey = settings->key;
            refreshStatus();
        });
        mAdapter = new EventSignalAdapter(source, filter, this);
        mAdapter->setObjectName(QStringLiteral("eventSignalAdapter"));
        setProperty("eventAdapter", QVariant::fromValue(static_cast<QObject *>(mAdapter.data())));
        connect(mAdapter, &EventSignalAdapter::settingsChanged, this,
                [this](const SettingsEvent &event, quint64 sequence) {
                    ++mAdapterEvents;
                    mLastSequence = qMax(mLastSequence, sequence);
                    mLastKey = event.key;
                    refreshStatus();
                });
        if (!mTypedConnection || !mAdapter->isSubscribed()) {
            if (error)
                *error = tr("The host could not register the typed event subscriptions.");
            shutdown();
            return false;
        }

        auto *show = new QAction(tr("Show typed event example"), host->lifetime());
        connect(show, &QAction::triggered, host->lifetime(),
                [host]() { host->ui()->showDock(QStringLiteral("events")); });
        if (!host->ui()->registerAction(QStringLiteral("show"), show) ||
            !host->ui()->registerDockPanelFactory(this)) {
            if (error)
                *error = tr("The host could not register the typed event example panel.");
            shutdown();
            return false;
        }

        // Commands originate here or in the explicit user action, never in an observer.
        host->settings()->setValue(QStringLiteral("typedExampleReady"), true);
        return true;
    }

    /** @brief Disconnect managed observations and delete the plugin-owned adapter.
     * @note Idempotent; host-owned widgets/actions are removed by host cleanup. */
    void shutdown() override
    {
        QObject::disconnect(mTypedConnection);
        mTypedConnection = {};
        delete mAdapter.data();
        mAdapter = nullptr;
        setProperty("eventAdapter", QVariant::fromValue(static_cast<QObject *>(nullptr)));
        mStatus = nullptr;
        mHost = nullptr;
    }

    /** @brief Advertise the UI contributions demonstrated by this plugin.
     * @return Names of the registered command and dock contribution families. */
    QStringList contributions() const override
    {
        return {QStringLiteral("actions"), QStringLiteral("dock")};
    }

    /** @brief Supply the extension-local dock identity.
     * @return Identity qualified by the host with this plugin's configured ID. */
    QString id() const override { return QStringLiteral("events"); }

    /** @brief Supply the dock caption.
     * @return Translated title for the example panel. */
    QString title() const override { return tr("Typed event example"); }

    /** @brief Create the counter display and explicit settings command.
     * @param[in] host Borrowed SDK host supplied by dock registration.
     * @return New widget transferred to host ownership.
     * @note Widget callbacks issue public service commands; they do not write private GUI state. */
    QWidget *create(ExtensionHost *host) override
    {
        Q_UNUSED(host);
        auto *widget = new QWidget;
        auto *layout = new QVBoxLayout(widget);
        auto *description = new QLabel(
            tr("Both counters observe this extension's settings. Select the button to issue "
               "a settings command and receive its typed notification."),
            widget);
        description->setWordWrap(true);
        layout->addWidget(description);
        mStatus = new QLabel(widget);
        mStatus->setObjectName(QStringLiteral("ext.%1.events-status").arg(info().id));
        layout->addWidget(mStatus);
        auto *pulse = new QPushButton(tr("Change example setting"), widget);
        pulse->setObjectName(QStringLiteral("ext.%1.events-pulse").arg(info().id));
        connect(pulse, &QPushButton::clicked, widget, [this]() {
            if (!mHost)
                return;
            const auto key = QStringLiteral("examplePulse");
            mHost->settings()->setValue(key, mHost->settings()->value(key, 0).toInt() + 1);
        });
        layout->addWidget(pulse);
        layout->addStretch();
        refreshStatus();
        return widget;
    }

private:
    /** @brief Publish copied counters on the plugin object and its optional label. */
    void refreshStatus()
    {
        setProperty("typedEvents", QVariant::fromValue(mTypedEvents));
        setProperty("adapterEvents", QVariant::fromValue(mAdapterEvents));
        setProperty("lastSequence", QVariant::fromValue(mLastSequence));
        setProperty("lastKey", mLastKey);
        if (!mStatus)
            return;
        mStatus->setText(tr("Typed callbacks: %1; Qt signals: %2; last sequence: %3")
                             .arg(mTypedEvents)
                             .arg(mAdapterEvents)
                             .arg(mLastSequence));
        mStatus->setProperty("typedEvents", QVariant::fromValue(mTypedEvents));
        mStatus->setProperty("adapterEvents", QVariant::fromValue(mAdapterEvents));
        mStatus->setProperty("lastSequence", QVariant::fromValue(mLastSequence));
        mStatus->setProperty("lastKey", mLastKey);
    }

    ExtensionHost *mHost = nullptr;           ///< Borrowed initialized host; null after shutdown.
    QMetaObject::Connection mTypedConnection; ///< Receiver-scoped typed callback subscription.
    QPointer<EventSignalAdapter> mAdapter; ///< Plugin-owned adapter, deleted before host cleanup.
    QPointer<QLabel> mStatus;   ///< Host-owned label, guarded through contribution teardown.
    quint64 mTypedEvents = 0;   ///< Settings events received through the typed callback interface.
    quint64 mAdapterEvents = 0; ///< Settings events received through the Qt signal adapter.
    quint64 mLastSequence = 0;  ///< Latest sequence observed by either subscription.
    QString mLastKey;           ///< Latest owning settings key observed by either subscription.
};

#include "extension.moc"
