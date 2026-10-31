/**
 * @file extensiontoolruns.h
 * @brief Shared copied history and per-owner execution controller for GUI tools.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IToolRuns.hpp"
#include <QHash>
#include <QPointer>

class QMainWindow;
namespace openswmmvis
{
/** @brief Format a recorded time in the local timezone with date, milliseconds and UTC offset.
 * @param[in] at Recorded UTC timestamp. @return Local human-readable timestamp, or empty if invalid. */
QString toolRunTimestamp(const QDateTime &at);
/** @brief Format elapsed wall-clock time as hours, minutes, seconds and milliseconds.
 * @param[in] startedAt Recorded start. @param[in] endedAt Recorded end or current time.
 * @return Nonnegative duration; hours may exceed 24. */
QString toolRunDuration(const QDateTime &startedAt, const QDateTime &endedAt);
/** @brief Main-window history independent of plugin code and tool widgets. */
class ToolRunStore final : public QObject
{
    Q_OBJECT
public:
    /** @brief Construct copied history. @param[in] parent Main window owning session history. */
    explicit ToolRunStore(QObject *parent = nullptr);
    /** @brief Resolve the unique store for one main window. @param[in] window Store owner. @return Borrowed store. */
    static ToolRunStore *forWindow(QObject *window);
    /** @brief Copy visible records newest first. @return Bounded terminal history and all active runs. */
    QList<OpenSWMM::Sdk::ToolRunState> runs() const;
    /** @brief Copy one record. @param[in] id Execution UUID. @return Empty value if absent. */
    OpenSWMM::Sdk::ToolRunState run(const QUuid &id) const;
    /** @brief Remove only a finished entry. @param[in] id Execution UUID. @return Accepted removal. */
    bool remove(const QUuid &id);
    /** @brief Request active cancellation through the owner controller. @param[in] id UUID. @return Accepted request. */
    bool requestCancel(const QUuid &id);
    /** @brief Set or clear a native layer navigation route. @param[in] route Validates exact session and layer identity. */
    void setLayerNavigator(std::function<bool(const OpenSWMM::Sdk::ToolResult &, bool)> route);
    /** @brief Resolve or activate an output. @param[in] result Copied output. @param[in] activate False queries availability. @return Available. */
    bool navigate(const OpenSWMM::Sdk::ToolResult &result, bool activate = false) const;
signals:
    /** @brief Notify history views after copied state changes. */
    void changed();
private:
    friend class ExtensionToolRuns;
    void retain(const OpenSWMM::Sdk::ToolRunState &state);
    QList<OpenSWMM::Sdk::ToolRunState> mRuns;
    QHash<QUuid, std::function<bool()>> mCancel;
    std::function<bool(const OpenSWMM::Sdk::ToolResult &, bool)> mLayerNavigator;
};

/** @brief Native callbacks used without depending on the concrete main window. */
struct ToolRunBridge {
    std::function<void(const QUuid &, const QString &, double, const QString &)> progressChanged; ///< Status-bar publication.
    std::function<void(const QUuid &)> progressFinished; ///< Remove one status-bar operation.
    std::function<bool(const QString &)> toolExists; ///< Validate extension-local registration.
};

/** @brief Per-host service with guarded plugin callbacks and shared copied output history. */
class ExtensionToolRuns final : public QObject, public OpenSWMM::Sdk::IToolRuns
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Sdk::IToolRuns)
public:
    /** @brief Bind host and window history. @param[in] host Borrowed extension facade. @param[in] store Window-owned store. @param[in] bridge Native callbacks. @param[in] parent Lifetime owner. */
    ExtensionToolRuns(OpenSWMM::Sdk::ExtensionHost *host, ToolRunStore *store,
                      ToolRunBridge bridge, QObject *parent = nullptr);
    /** @brief Settle callbacks before plugin code can unload. */
    ~ExtensionToolRuns() override;
    /** @brief Accept a user action with copied input metadata and a guarded cancellation route.
     * @param[in] input Registered tool identity and reviewed input summary.
     * @param[in] context Required GUI-thread lifetime guard.
     * @param[in] cancel Optional controller cancelling all current stages.
     * @return Fresh execution UUID, or null when refused. */
    QUuid beginToolRun(const OpenSWMM::Sdk::ToolRunRequest &input, QObject *context,
                       std::function<void()> cancel = {}) override;
    /** @copydoc OpenSWMM::Sdk::IToolRuns::updateToolRun */
    bool updateToolRun(const QUuid &id, double fraction, const QString &stage) override;
    /** @copydoc OpenSWMM::Sdk::IToolRuns::appendToolMessage */
    bool appendToolMessage(const QUuid &id, const QString &level, const QString &text) override;
    /** @copydoc OpenSWMM::Sdk::IToolRuns::addToolResult */
    bool addToolResult(const QUuid &id, const OpenSWMM::Sdk::ToolResult &result) override;
    /** @copydoc OpenSWMM::Sdk::IToolRuns::finishToolRun */
    bool finishToolRun(const QUuid &id, OpenSWMM::Sdk::ToolRunOutcome outcome,
                       const QString &detail = {}) override;
    /** @copydoc OpenSWMM::Sdk::IToolRuns::cancelToolRun */
    bool cancelToolRun(const QUuid &id) override;
    /** @copydoc OpenSWMM::Sdk::IToolRuns::toolRuns */
    QList<OpenSWMM::Sdk::ToolRunState> toolRuns() const override;
    /** @copydoc OpenSWMM::Sdk::IToolRuns::runToolJob */
    OpenSWMM::Sdk::IJob *runToolJob(
        const QUuid &id, const QString &title,
        std::function<void(OpenSWMM::Sdk::IJobContext &)> work,
        std::function<void(bool, const QString &)> finished = {}) override;
    /** @copydoc OpenSWMM::Sdk::IToolRuns::importToolTimeSeries */
    OpenSWMM::Sdk::IJob *importToolTimeSeries(
        const QUuid &id, const OpenSWMM::Sdk::TimeSeriesImportRequest &request, QObject *context,
        std::function<void(const OpenSWMM::Sdk::TimeSeriesImportResult &)> finished,
        QString *error = nullptr) override;
    /** @brief Suppress later updates and retire guarded callbacks during host shutdown. */
    void stop();
    /** @brief Bind an operation before its first progress event. @param[in] id New child operation. @return True when captured by scoped submission. */
    bool captureOperation(const QUuid &id);
    /** @brief Route child progress through its parent. @param[in] id Captured operation. @param[in] fraction Progress. @param[in] stage Stage text. @return True when legacy UI/log publication should be suppressed. */
    bool updateOperation(const QUuid &id, double fraction, const QString &stage);
    /** @brief Retire a child without settling its parent. @param[in] id Operation. @return True when legacy terminal publication should be suppressed. */
    bool finishOperation(const QUuid &id);
private:
    struct Control {
        QPointer<QObject> context;
        QMetaObject::Connection destroyed;
        std::function<void()> cancel;
    };
    bool usable(const QUuid &id) const;
    void publish(const OpenSWMM::Sdk::ToolRunState &state);
    OpenSWMM::Sdk::ExtensionHost *mHost;
    QPointer<ToolRunStore> mStore;
    ToolRunBridge mBridge;
    QHash<QUuid, Control> mControls;
    QHash<QUuid, QUuid> mOperations;
    QUuid mSubmittingRun;
    bool mStopping = false;
};
}
