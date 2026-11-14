/**
 * @file IToolRuns.hpp
 * @brief Owner-scoped analysis runs, copied results and cancellable staged execution.
 * @details SDK 1.10 optional capability discovered on ExtensionHost::lifetime(). All calls
 * use the GUI thread. Returned records own their values. Run identity spans network work,
 * workers and native import. Existing SDK interfaces and binary layouts remain frozen.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "ExtensionHost.hpp"
#include "ITimeSeriesLayers.hpp"
#include <QDateTime>
#include <QUuid>

/** @brief Frozen discovery identity for tool execution records. */
#define OPENSWMM_TOOL_RUNS_IID "io.opensimulator.openswmm.IToolRuns/1.0"

namespace OpenSWMM::Sdk
{
/** @brief Settled user-visible outcome. Cancellation requests await actual settlement. */
enum class ToolRunOutcome { Succeeded, Warning, Failed, Cancelled };
/** @brief Typed reference or small literal result value. */
enum class ToolResultKind { Value, File, Layer };
/** @brief Owning publication of a produced asset or literal summary, never a plugin callback. */
struct ToolResult {
    ToolResultKind kind = ToolResultKind::Value; ///< Determines the supported navigation action.
    QString title; ///< Human-readable result name.
    QString value; ///< Literal value or description, optionally including units.
    QString path; ///< Completed local file path, optionally also the source of a layer.
    QString layerId; ///< Persistent native layer ID, resolved only in the captured session.
    QUuid sessionId; ///< Exact open model lifetime for layer navigation.
    QUuid layerSessionId; ///< Host-captured runtime attachment identity, never an ID-only match.
    quint64 loadGeneration = 0; ///< Host-captured model generation at output publication.
};
/** @brief Copied literal message owned by one accepted execution. */
struct ToolRunMessage {
    QDateTime at; ///< UTC publication time.
    QString level; ///< info, warning or error.
    QString stage; ///< Stage at publication.
    QString text; ///< Literal diagnostic, never HTML.
};
/** @brief Inputs copied at acceptance, without granting permission to edit a model. */
struct ToolRunRequest {
    QString toolId; ///< Registered extension-local tool identity.
    QString title; ///< Short action caption, such as Preview or Download.
    QString inputs; ///< Small reviewed input summary without credentials.
    QUuid sessionId; ///< Optional exact open model lifetime.
    quint64 loadGeneration = 0; ///< Captured model generation, zero when unbound.
};
/** @brief Bounded owning history value. Terminal entries are immutable through the SDK. */
struct ToolRunState {
    QUuid id; ///< Host-issued unique execution identity.
    QString pluginId; ///< Owner inferred by the host.
    ToolRunRequest request; ///< Frozen inputs at acceptance.
    QString modelTitle; ///< Captured user-facing model caption, retained after closure.
    QDateTime startedAt; ///< Acceptance time in UTC.
    QDateTime finishedAt; ///< Invalid while active.
    double fraction = -1.; ///< Negative means indeterminate.
    QString stage; ///< Latest stage or terminal detail.
    ToolRunOutcome outcome = ToolRunOutcome::Succeeded; ///< Meaningful only after settlement.
    bool cancelRequested = false; ///< Independent of final outcome.
    bool cancellable = false; ///< A live owner-guarded cancellation route exists.
    bool messagesTruncated = false; ///< Older messages were removed by the 1000-message bound.
    QList<ToolRunMessage> messages; ///< At most 1000 retained messages.
    QList<ToolResult> results; ///< At most 1000 small results.
    QList<QUuid> operationIds; ///< Child operations attributed before their first publication.
    /** @brief Check terminal state. @return True after exactly one accepted settlement. */
    bool isFinished() const { return finishedAt.isValid(); }
};

/** @brief GUI-thread execution service scoped to the calling extension. */
class IToolRuns
{
public:
    /** @brief Destroy implementation; the plugin borrows this service. */
    virtual ~IToolRuns() = default;
    /** @brief Accept one user action after input validation.
     * @param[in] request Registered tool identity and copied reviewed inputs.
     * @param[in] context Required GUI-thread lifetime guard.
     * @param[in] cancel Optional short cancellation controller for all current stages.
     * @return Fresh UUID, or null when unavailable or invalid.
     * @note Context destruction settles active work as canceled and retires its callback. */
    virtual QUuid beginToolRun(const ToolRunRequest &request, QObject *context,
                              std::function<void()> cancel = {}) = 0;
    /** @brief Update one active run and shared progress, coalescing identical stages.
     * @param[in] id Own active execution.
     * @param[in] fraction Negative finite busy value or [0,1] completion.
     * @param[in] stage Literal human-readable stage.
     * @return True when accepted, false for stale/foreign IDs or invalid input. */
    virtual bool updateToolRun(const QUuid &id, double fraction, const QString &stage) = 0;
    /** @brief Append one literal message and mirror it once to ordinary plugin logs.
     * @param[in] id Own active execution.
     * @param[in] level Severity as accepted by IConsole::append().
     * @param[in] text Literal bounded message.
     * @return True after acceptance. */
    virtual bool appendToolMessage(const QUuid &id, const QString &level,
                                   const QString &text) = 0;
    /** @brief Publish a copied output, including completed files retained after cancellation.
     * @param[in] id Own active execution.
     * @param[in] result Typed completed result or literal summary.
     * @return False for unavailable/stale execution, malformed result or capacity exhaustion. */
    virtual bool addToolResult(const QUuid &id, const ToolResult &result) = 0;
    /** @brief Settle exactly once after all stages and callbacks complete.
     * @param[in] id Own active execution.
     * @param[in] outcome Actual settled outcome.
     * @param[in] detail Literal terminal description.
     * @return False for foreign, already finished or unavailable execution. */
    virtual bool finishToolRun(const QUuid &id, ToolRunOutcome outcome,
                               const QString &detail = {}) = 0;
    /** @brief Request cooperative cancellation through this run's guarded controller.
     * @param[in] id Own active execution.
     * @return True for an accepted/idempotent request, false if unavailable. */
    virtual bool cancelToolRun(const QUuid &id) = 0;
    /** @brief Copy retained own runs, including active work. @return Newest-first bounded records. */
    virtual QList<ToolRunState> toolRuns() const = 0;
    /** @brief Submit a worker bound before its first operation event.
     * @param[in] id Own active parent execution.
     * @param[in] title Short child stage caption.
     * @param[in] work Existing worker-safe callback.
     * @param[in] finished Existing GUI-thread completion callback.
     * @return Borrowed worker handle, or null when refused. Child settlement does not settle parent. */
    virtual IJob *runToolJob(const QUuid &id, const QString &title,
                            std::function<void(IJobContext &)> work,
                            std::function<void(bool, const QString &)> finished = {}) = 0;
    /** @brief Submit native import under an active parent execution before its first event.
     * @param[in] id Own active parent execution.
     * @param[in] request Immutable completed import assets and exact target.
     * @param[in] context Required GUI-thread receiver guard.
     * @param[in] finished Guarded terminal import receiver.
     * @param[out] error Optional immediate refusal detail.
     * @return Borrowed cancellable worker, or null on refusal. */
    virtual IJob *importToolTimeSeries(
        const QUuid &id, const TimeSeriesImportRequest &request, QObject *context,
        std::function<void(const TimeSeriesImportResult &)> finished,
        QString *error = nullptr) = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IToolRuns, OPENSWMM_TOOL_RUNS_IID)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ToolRunState)
