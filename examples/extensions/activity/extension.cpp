/**
 * @file extension.cpp
 * @brief Installed-SDK process observation, private worker and utility-document example.
 * @details Observers refresh copied metadata. Explicit buttons schedule a worker
 * or open a model-independent document, demonstrating ordinary typed Qt signals
 * and controller calls without native GUI or engine headers.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "sdk/ExtensionHost.hpp"
#include "sdk/IGuiExtension.hpp"
#include "sdk/IActivityControls.hpp"
#include "sdk/qt/ActivitySignalAdapter.hpp"
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

using namespace OpenSWMM::Sdk;

/** @brief Scoped activity monitor with explicit private work and document commands. */
class ActivityExample final : public QObject,
                              public IGuiExtension,
                              public IDockPanelFactory,
                              public IDocumentViewFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OPENSWMM_GUI_EXTENSION_IID FILE "plugin.json")
    Q_INTERFACES(OpenSWMM::Sdk::IGuiExtension)
public:
    /** @brief Describe this independently configured example module.
     * @return Owning metadata matching plugin.json. */
    ExtensionInfo info() const override
    {
        return {QStringLiteral(OPENSWMM_ACTIVITY_EXAMPLE_ID),
                tr("Process and document example"),
                tr("Unique attempts, private tasks and actual document focus"),
                "1.0.0",
                "OpenSWMM",
                {},
                "GPL-3.0-or-later",
                {"activity", "example"}};
    }
    /** @brief Discover activity services and subscribe before the initial snapshot.
     * @param[in] host Borrowed host, valid until shutdown.
     * @param[out] error Optional required-capability/registration diagnostic.
     * @return True after managed observation and UI registration succeed. */
    bool initialize(ExtensionHost *host, QString *error) override
    {
        mLifetime = host ? host->lifetime() : nullptr;
        mHost = host;
        mSource = mLifetime ? qobject_cast<IActivityEvents *>(mLifetime.data()) : nullptr;
        mControls = mLifetime ? qobject_cast<IActivityControls *>(mLifetime.data()) : nullptr;
        if (!mSource || !mControls) {
            if (error)
                *error = tr("This example requires SDK 1.5 activity services.");
            return false;
        }
        setObjectName(info().id);
        mAdapter = new ActivitySignalAdapter(mSource, {}, this);
        mAdapter->setObjectName("ext." + info().id + ".activity-adapter");
        connect(mAdapter, &ActivitySignalAdapter::occurred, this,
                [this](const ActivityEvent &event) {
                    if (event.sequence <= mInitialWatermark)
                        return;
                    mLastSequence = event.sequence;
                    if (event.kind <= ActivityEventKind::OperationRemoved)
                        ++mProcessEvents;
                    else
                        ++mUiEvents;
                    if (event.kind == ActivityEventKind::OperationFinished) {
                        if (event.process.kind == ProcessKind::Job)
                            mJobTerminals.insert(event.process.operationId);
                        else
                            mRunTerminals.insert(event.process.operationId);
                    }
                    refresh();
                });
        mInitialWatermark = mSource->activitySnapshot().sequence;
        if (!mAdapter->isSubscribed() || !host->ui()->registerDocumentViewFactory(this) ||
            !host->ui()->registerDockPanelFactory(this)) {
            if (error)
                *error = tr("The host could not register the activity example.");
            return false;
        }
        return true;
    }
    /** @brief End observation and discard borrowed host services before unload. */
    void shutdown() override
    {
        delete mAdapter.data();
        mAdapter = nullptr;
        mHost = nullptr;
        mLifetime = nullptr;
        mSource = nullptr;
        mControls = nullptr;
        mStatus = nullptr;
    }
    /** @brief Identify the own monitor dock.
     * @return Stable local dock key. */
    QString id() const override { return "activity"; }
    /** @brief Name the monitor panel.
     * @return User-facing dock caption. */
    QString title() const override { return tr("Process and document example"); }
    /** @brief Create a copied-state summary and explicit worker/document actions.
     * @param[in] host Borrowed host whose services have already been discovered.
     * @return New widget transferred to host ownership. */
    QWidget *create(ExtensionHost *host) override
    {
        Q_UNUSED(host);
        auto *widget = new QWidget;
        auto *layout = new QVBoxLayout(widget);
        mStatus = new QLabel(widget);
        mStatus->setObjectName("ext." + info().id + ".activity-status");
        auto *work = new QPushButton(tr("Run a short background task"), widget);
        work->setObjectName("ext." + info().id + ".activity-work");
        auto *document = new QPushButton(tr("Open activity document"), widget);
        document->setObjectName("ext." + info().id + ".activity-document");
        layout->addWidget(mStatus);
        layout->addWidget(work);
        layout->addWidget(document);
        connect(work, &QPushButton::clicked, this, [this] {
            if (!mLifetime || !mHost || !mControls)
                return;
            auto *job = mHost->runJob(tr("Example task"), [](IJobContext &context) {
                for (int step = 1; step <= 4; ++step) {
                    if (context.isCancelled())
                        return;
                    context.setProgress(step / 4., QStringLiteral("Step %1").arg(step));
                }
            });
            mLastJob = job ? mControls->jobOperationId(job) : QUuid{};
            refresh();
        });
        connect(document, &QPushButton::clicked, this, [this] {
            if (mLifetime && mHost)
                mHost->ui()->openDocumentView(kind(), nullptr);
        });
        refresh();
        return widget;
    }
    /** @brief Identify this model-independent document factory.
     * @return Stable extension-local kind. */
    QString kind() const override { return "activity-log"; }
    /** @brief Accept independent utility views without borrowing a model.
     * @param[in] instance Optional model, ignored.
     * @return True; document state contains no model data. */
    bool accepts(IModelInstance *instance) const override
    {
        Q_UNUSED(instance);
        return true;
    }
    /** @brief Create a utility document to demonstrate actual MDI focus.
     * @param[in] instance Optional model, ignored.
     * @param[in] host Borrowed host, ignored.
     * @return New widget transferred to MDI ownership. */
    QWidget *create(IModelInstance *instance, ExtensionHost *host) override
    {
        Q_UNUSED(instance);
        Q_UNUSED(host);
        auto *label = new QLabel(tr("This document has no associated model."));
        label->setWindowTitle(tr("Activity document"));
        return label;
    }
    /** @brief Save an independent view without private state.
     * @param[in] view Borrowed factory widget, ignored.
     * @return Empty state, suitable for forward-compatible restoration. */
    QJsonObject saveState(QWidget *view) const override
    {
        Q_UNUSED(view);
        return {};
    }
    /** @brief Restore this stateless utility document.
     * @param[in] view Borrowed factory widget, ignored.
     * @param[in] state Owning state, ignored. */
    void restoreState(QWidget *view, const QJsonObject &state) override
    {
        Q_UNUSED(view);
        Q_UNUSED(state);
    }

private:
    /** @brief Refresh copied display state without issuing native commands. */
    void refresh()
    {
        if (!mLifetime || !mSource)
            return;
        const auto state = mSource->activitySnapshot();
        const auto publish = [&](QObject *target) {
            target->setProperty("processEvents", mProcessEvents);
            target->setProperty("uiEvents", mUiEvents);
            target->setProperty("ownJobTerminals", mJobTerminals.size());
            target->setProperty("sharedRunTerminals", mRunTerminals.size());
            target->setProperty("lastSequence", QVariant::fromValue(mLastSequence));
            target->setProperty("lastJobId", mLastJob);
            target->setProperty("activeDocumentId", state.ui.activeDocumentId);
            target->setProperty("documentCount", state.ui.documents.size());
        };
        publish(this);
        if (mStatus) {
            mStatus->setText(tr("Processes: %1\nDocuments: %2")
                                 .arg(state.processes.size())
                                 .arg(state.ui.documents.size()));
            publish(mStatus);
        }
    }
    QPointer<QObject> mLifetime;              ///< Borrowed host lifetime.
    ExtensionHost *mHost = nullptr;           ///< Borrowed scoped host, discarded on shutdown.
    IActivityEvents *mSource = nullptr;        ///< Borrowed copied-state source.
    IActivityControls *mControls = nullptr; ///< Borrowed identity-checked controls.
    QPointer<ActivitySignalAdapter> mAdapter; ///< Consumer-owned typed QObject projection.
    QPointer<QLabel> mStatus;                 ///< Host-owned status summary.
    QSet<QUuid> mJobTerminals, mRunTerminals; ///< Copied distinct terminal identities.
    QUuid mLastJob; ///< Most recently explicitly scheduled private job UUID.
    quint64 mInitialWatermark = 0,
            mLastSequence = 0;             ///< Initial cutoff and last observed sequence.
    int mProcessEvents = 0, mUiEvents = 0; ///< Diagnostic managed-observation counts.
};
#include "extension.moc"
