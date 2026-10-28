/**
 * @file extension.cpp
 * @brief Installed-SDK diagnostic consumer of copied primitive event values.
 * @details Demonstrates Qt connections and explicit snapshot queries without a
 * scripting runtime. Optional JSONL capture writes only to an explicitly supplied
 * local path; no capture, interpreter, command gateway or network activity starts
 * by default.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "ValueEventAdapter.hpp"
#include <sdk/ExtensionHost.hpp>
#include <sdk/IGuiExtension.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace OpenSWMM::Sdk;
using OpenSWMM::Examples::ValueEventAdapter;

/** @brief Consumer-owned adapter and host-owned monitor, containing no native model handles. */
class ValueExampleExtension final : public QObject, public IGuiExtension, public IDockPanelFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OPENSWMM_GUI_EXTENSION_IID FILE "plugin.json")
    Q_INTERFACES(OpenSWMM::Sdk::IGuiExtension)
public:
    /** @brief Describe this independently configured diagnostic module.
     * @return Owning plugin metadata matching plugin.json. */
    ExtensionInfo info() const override
    {
        return {QStringLiteral(OPENSWMM_VALUE_EXAMPLE_ID),
                tr("Copied event values example"),
                tr("Ordinary Qt values for lifecycle, view and activity observations"),
                "1.0.0",
                "OpenSWMM",
                {},
                "GPL-3.0-or-later",
                {"events", "values", "example"}};
    }
    /** @brief Subscribe before the initial snapshot and register a read-only monitor.
     * @param[in] host Borrowed calling host, valid through shutdown.
     * @param[out] error Optional required-capability, explicit capture or UI failure detail.
     * @return True after all source subscriptions and contribution registrations succeed. */
    bool initialize(ExtensionHost *host, QString *error) override
    {
        shutdown();
        mHost = host;
        mLifetime = host ? host->lifetime() : nullptr;
        mStateEvents = mViewEvents = mActivityEvents = 0;
        mInitialWatermark = mLastSequence = 0;
        setObjectName(info().id);
        setProperty("captureFailed", false);
        mAdapter = new ValueEventAdapter(mLifetime, this);
        mAdapter->setObjectName("ext." + info().id + ".value-adapter");
        if (!mAdapter->isSubscribed()) {
            if (error)
                *error = tr("This example requires SDK 1.5 lifecycle, view and activity services.");
            shutdown();
            return false;
        }
        const auto capturePath =
            qEnvironmentVariable("SDK_VALUE_CAPTURE_PATH").replace("{pluginId}", info().id);
        if (!capturePath.isEmpty()) {
            mCapture.setFileName(capturePath);
            if (!QDir::isAbsolutePath(capturePath) || !QFileInfo(capturePath).dir().exists() ||
                !mCapture.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                if (error)
                    *error =
                        tr("The requested capture needs an absolute file path with an existing "
                           "parent directory: %1 (%2)")
                            .arg(capturePath, mCapture.errorString());
                shutdown();
                return false;
            }
        }
        connect(mAdapter, &ValueEventAdapter::stateOccurred, this,
                [this](const QVariantMap &record) { acceptEvent(record); });
        connect(mAdapter, &ValueEventAdapter::viewOccurred, this,
                [this](const QVariantMap &record) { acceptEvent(record); });
        connect(mAdapter, &ValueEventAdapter::activityOccurred, this,
                [this](const QVariantMap &record) { acceptEvent(record); });
        const auto initial = mAdapter->stateSnapshot();
        mInitialWatermark =
            initial.value("data").toMap().value("sequence").toString().toULongLong();
        mLastSequence = mInitialWatermark;
        captureSnapshots();
        auto *show = new QAction(tr("Show copied event values"), mLifetime);
        connect(show, &QAction::triggered, this, [this] {
            if (mLifetime && mHost)
                mHost->ui()->showDock("values");
        });
        if (!host->ui()->registerAction("show", show) ||
            !host->ui()->registerDockPanelFactory(this)) {
            if (error)
                *error = tr("The host could not register the copied-value monitor.");
            shutdown();
            return false;
        }
        refresh();
        return true;
    }
    /** @brief End observation/capture and discard borrowed host references before UI removal. */
    void shutdown() override
    {
        delete mAdapter.data();
        mAdapter = nullptr;
        mCapture.close();
        mHost = nullptr;
        mLifetime = nullptr;
        mStatus = nullptr;
    }
    /** @brief Advertise the monitor's registered UI contribution families.
     * @return Command and dock contribution names. */
    QStringList contributions() const override { return {"actions", "dock"}; }
    /** @brief Identify the extension-local diagnostic panel.
     * @return Stable local dock key. */
    QString id() const override { return "values"; }
    /** @brief Name the diagnostic dock.
     * @return Translated user-facing title. */
    QString title() const override { return tr("Copied event values"); }
    /** @brief Mount copied counters and an explicit read-only snapshot refresh.
     * @param[in] host Borrowed host; service discovery already occurred during initialization.
     * @return New widget transferred to host ownership. */
    QWidget *create(ExtensionHost *host) override
    {
        Q_UNUSED(host);
        auto *widget = new QWidget;
        auto *layout = new QVBoxLayout(widget);
        mStatus = new QLabel(widget);
        mStatus->setObjectName("ext." + info().id + ".values-status");
        mStatus->setTextFormat(Qt::PlainText);
        layout->addWidget(mStatus);
        auto *refreshButton = new QPushButton(tr("Refresh copied metadata"), widget);
        refreshButton->setObjectName("ext." + info().id + ".values-refresh");
        connect(refreshButton, &QPushButton::clicked, this, [this] {
            captureSnapshots();
            refresh();
        });
        layout->addWidget(refreshButton);
        layout->addStretch();
        refresh();
        return widget;
    }

private:
    /** @brief Consume a future observation without using a new query as a moving event cutoff.
     * @param[in] record Owning primitive event envelope. */
    void acceptEvent(const QVariantMap &record)
    {
        const auto sequence =
            record.value("data").toMap().value("sequence").toString().toULongLong();
        if (sequence <= mInitialWatermark)
            return;
        const auto domain = record.value("domain").toString();
        if (domain == "state")
            ++mStateEvents;
        else if (domain == "view")
            ++mViewEvents;
        else if (domain == "activity")
            ++mActivityEvents;
        mLastSequence = sequence;
        capture(record);
        refresh();
    }
    /** @brief Append one explicit diagnostic record without retaining a replay buffer.
     * @param[in] record Owning primitive event or snapshot envelope.
     * @note A requested capture error is reported once and disables further file output. */
    void capture(const QVariantMap &record)
    {
        if (!mCapture.isOpen())
            return;
        const auto line =
            QJsonDocument(QJsonObject::fromVariantMap(record)).toJson(QJsonDocument::Compact) +
            '\n';
        if (mCapture.write(line) != line.size() || !mCapture.flush()) {
            qWarning().noquote() << "Value example capture failed:" << mCapture.errorString();
            setProperty("captureFailed", true);
            mCapture.close();
        }
    }
    /** @brief Query/capture current metadata explicitly, preserving the initial event cutoff. */
    void captureSnapshots()
    {
        if (!mAdapter)
            return;
        const auto state = mAdapter->stateSnapshot();
        capture(state);
        capture(mAdapter->activitySnapshot());
        for (const auto &item : state.value("data").toMap().value("sessions").toList()) {
            const auto sessionId = item.toMap().value("sessionId").toString();
            capture(mAdapter->viewSnapshot(sessionId));
        }
    }
    /** @brief Refresh copied counters on the plugin and guarded diagnostic label. */
    void refresh()
    {
        if (!mAdapter)
            return;
        const auto state = mAdapter->stateSnapshot();
        const auto data = state.value("data").toMap();
        const auto publish = [&](QObject *target) {
            target->setProperty("stateEvents", QString::number(mStateEvents));
            target->setProperty("viewEvents", QString::number(mViewEvents));
            target->setProperty("activityEvents", QString::number(mActivityEvents));
            target->setProperty("lastSequence", QString::number(mLastSequence));
            target->setProperty("snapshotWatermark", QString::number(mInitialWatermark));
            target->setProperty("lastSnapshotSequence", data.value("sequence"));
            target->setProperty("sessionCount",
                                QString::number(data.value("sessions").toList().size()));
        };
        publish(this);
        if (mStatus) {
            mStatus->setText(tr("State updates: %1\nView updates: %2\nActivity updates: %3")
                                 .arg(mStateEvents)
                                 .arg(mViewEvents)
                                 .arg(mActivityEvents));
            publish(mStatus);
        }
    }
    ExtensionHost *mHost = nullptr;       ///< Borrowed own host; discarded during shutdown.
    QPointer<QObject> mLifetime;          ///< Borrowed source guard for the explicit show action.
    QPointer<ValueEventAdapter> mAdapter; ///< Consumer-owned observation/value converter.
    QPointer<QLabel> mStatus;             ///< Guarded host-owned diagnostic label.
    QFile mCapture;                ///< Optional explicitly requested append-only diagnostic output.
    quint64 mStateEvents = 0;      ///< Accepted session events after the immutable initial cutoff.
    quint64 mViewEvents = 0;       ///< Accepted view events after the immutable initial cutoff.
    quint64 mActivityEvents = 0;   ///< Accepted activity events after the immutable initial cutoff.
    quint64 mInitialWatermark = 0; ///< Initial shared cutoff, never advanced by refresh.
    quint64 mLastSequence = 0;     ///< Last accepted observation's shared sequence.
};
#include "extension.moc"
