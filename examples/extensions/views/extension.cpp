/**
 * @file extension.cpp
 * @brief Independently installed SDK example using typed view signals and validated layer commands.
 * @details The observer refreshes copied metadata only. A separate explicit user
 * action issues a validated visibility command, demonstrating normal Qt slots
 * without depending on native GUI objects, slots or an engine library.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "sdk/IGuiExtension.hpp"
#include "sdk/ExtensionHost.hpp"
#include "sdk/IViewControls.hpp"
#include "sdk/qt/ViewSignalAdapter.hpp"
#include <QComboBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

using namespace OpenSWMM::Sdk;

/** @brief Small native view monitor with a separate, user-initiated visibility command. */
class ViewsExample final : public QObject, public IGuiExtension, public IDockPanelFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OPENSWMM_GUI_EXTENSION_IID FILE "plugin.json")
    Q_INTERFACES(OpenSWMM::Sdk::IGuiExtension)
public:
    /** @brief Return metadata matching the configured independent module.
     * @return Owning identity/caption/license values. */
    ExtensionInfo info() const override
    {
        return {QStringLiteral(OPENSWMM_VIEW_EXAMPLE_ID),
                tr("Map and results example"),
                tr("Typed view signals and validated map controls"),
                "1.0.0",
                "OpenSWMM",
                {},
                "GPL-3.0-or-later",
                {"views", "example"}};
    }
    /** @brief Discover optional interfaces, subscribe before snapshotting and mount a panel.
     * @param[in] host Borrowed per-plugin host.
     * @param[out] error Missing service or panel-registration detail.
     * @return True after all required services and the panel are registered. */
    bool initialize(ExtensionHost *host, QString *error) override
    {
        mLifetime = host ? host->lifetime() : nullptr;
        mSessions = mLifetime ? qobject_cast<ISessionEvents *>(mLifetime.data()) : nullptr;
        mSource = mLifetime ? qobject_cast<IViewEvents *>(mLifetime.data()) : nullptr;
        mControls = mLifetime ? qobject_cast<IViewControls *>(mLifetime.data()) : nullptr;
        if (!mSessions || !mSource || !mControls) {
            if (error)
                *error = tr("The map and results example requires SDK 1.4 view services.");
            return false;
        }
        setObjectName(info().id);
        mAdapter = new ViewSignalAdapter(mSource, {}, this);
        mAdapter->setObjectName("ext." + info().id + ".view-adapter");
        connect(mAdapter, &ViewSignalAdapter::occurred, this, [this](const ViewEvent &event) {
            if (event.sequence <= mInitialWatermark)
                return;
            ++mEvents;
            mLastSequence = event.sequence;
            refresh();
        });
        mLifecycle = mSessions->observeState(this, {}, [this](const StateEvent &) { refresh(); });
        mInitialWatermark = mSessions->stateSnapshot().sequence;
        return mAdapter->isSubscribed() && mLifecycle && host->ui()->registerDockPanelFactory(this);
    }
    /** @brief End managed subscriptions and release borrowed capabilities before unload. */
    void shutdown() override
    {
        QObject::disconnect(mLifecycle);
        delete mAdapter.data();
        mAdapter = nullptr;
        mLifetime = nullptr;
        mSessions = nullptr;
        mSource = nullptr;
        mControls = nullptr;
        mStatus = nullptr;
        mChoices = nullptr;
        mToggle = nullptr;
    }
    /** @brief Identify the extension-local dock contribution.
     * @return Stable local dock key. */
    QString id() const override { return "views"; }
    /** @brief Name the view monitor.
     * @return User-facing caption. */
    QString title() const override { return tr("Map and results example"); }
    /** @brief Construct a copied metadata summary and an explicit layer command.
     * @param[in] host Borrowed host; the capabilities have already been discovered.
     * @return New widget transferred to host ownership. */
    QWidget *create(ExtensionHost *host) override
    {
        Q_UNUSED(host);
        auto *widget = new QWidget;
        auto *layout = new QVBoxLayout(widget);
        mStatus = new QLabel(widget);
        mStatus->setObjectName("ext." + info().id + ".views-status");
        mChoices = new QComboBox(widget);
        mChoices->setObjectName("ext." + info().id + ".views-layer");
        mToggle = new QPushButton(tr("Toggle layer visibility"), widget);
        mToggle->setObjectName("ext." + info().id + ".views-toggle");
        layout->addWidget(mStatus);
        layout->addWidget(mChoices);
        layout->addWidget(mToggle);
        connect(mToggle, &QPushButton::clicked, this, [this] { toggleSelectedLayer(); });
        refresh();
        return widget;
    }

private:
    /** @brief Query fresh copied metadata; observers perform no native mutation. */
    void refresh()
    {
        if (!mLifetime || !mSessions || !mSource)
            return;
        const auto previous = mChoices ? mChoices->currentData().toUuid() : QUuid{};
        if (mChoices)
            mChoices->clear();
        int layers = 0, results = 0;
        for (const auto &session : mSessions->stateSnapshot().sessions) {
            const auto view = mSource->viewSnapshot(session.sessionId);
            layers += view.layers.size();
            results += view.results.size();
            if (mChoices && session.status == SessionStatus::Ready)
                for (const auto &layer : view.layers) {
                    mChoices->addItem(session.title + ": " + layer.title, layer.layerSessionId);
                    mChoices->setItemData(mChoices->count() - 1, session.sessionId,
                                          Qt::UserRole + 1);
                }
        }
        if (mChoices && !previous.isNull()) {
            const int selected = mChoices->findData(previous);
            if (selected >= 0)
                mChoices->setCurrentIndex(selected);
        }
        if (mToggle)
            mToggle->setEnabled(mChoices && mChoices->count());
        const auto publish = [&](QObject *object) {
            object->setProperty("viewEvents", mEvents);
            object->setProperty("lastSequence", QVariant::fromValue(mLastSequence));
            object->setProperty("layerCount", layers);
            object->setProperty("resultCount", results);
            object->setProperty("commandsAccepted", mCommands);
        };
        publish(this);
        if (mStatus) {
            mStatus->setText(tr("Layers: %1\nResults: %2").arg(layers).arg(results));
            publish(mStatus);
        }
    }
    /** @brief Execute one explicit action against the currently selected runtime attachment. */
    void toggleSelectedLayer()
    {
        if (!mLifetime || !mControls || !mSource || !mChoices || mChoices->currentIndex() < 0)
            return;
        const auto layerId = mChoices->currentData().toUuid();
        const auto sessionId = mChoices->currentData(Qt::UserRole + 1).toUuid();
        const auto snapshot = mSource->viewSnapshot(sessionId);
        for (const auto &layer : snapshot.layers) {
            if (layer.layerSessionId != layerId)
                continue;
            ViewCommand command;
            command.kind = ViewCommandKind::SetLayerVisible;
            command.sessionId = sessionId;
            command.loadGeneration = snapshot.session.loadGeneration;
            command.layerSessionId = layerId;
            command.origin = QUuid::createUuid();
            command.enabled = !layer.visible;
            QString error;
            if (mControls->executeViewCommand(command, &error))
                ++mCommands;
            if (mStatus)
                mStatus->setToolTip(error);
            refresh();
            return;
        }
    }
    QPointer<QObject> mLifetime;          ///< Borrowed capability lifetime.
    ISessionEvents *mSessions = nullptr;  ///< Borrowed open-session query source.
    IViewEvents *mSource = nullptr;    ///< Borrowed copied view query source.
    IViewControls *mControls = nullptr; ///< Borrowed validated command entry point.
    QPointer<ViewSignalAdapter> mAdapter; ///< Consumer-owned typed QObject signal adapter.
    QMetaObject::Connection mLifecycle;   ///< Managed lifecycle refresh connection.
    QPointer<QLabel> mStatus;             ///< Host-owned copied metadata summary.
    QPointer<QComboBox> mChoices;         ///< Host-owned current attachment choices.
    QPointer<QPushButton> mToggle;        ///< Explicit user command button.
    quint64 mInitialWatermark = 0,
            mLastSequence = 0;      ///< Initial cutoff and last accepted publication.
    int mEvents = 0, mCommands = 0; ///< Diagnostic event/accepted-action counts.
};
#include "extension.moc"
