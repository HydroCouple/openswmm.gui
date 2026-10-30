/**
 * @file IActivityControls.hpp
 * @brief Versioned identity-checked process cancellation/start and scoped UI commands.
 * @details Optional SDK 1.5 GUI-thread capability. Existing IJob, IJobContext,
 * ExtensionHost and IRunRegistry vtables remain unchanged. No arbitrary private
 * Qt slot invocation or cross-plugin contribution mutation is provided.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QObject>
#include <QString>
#include <QUuid>

/** @brief Frozen Qt identity for typed process and scoped UI commands. */
#define OPENSWMM_ACTIVITY_CONTROLS_IID "io.opensimulator.openswmm.IActivityControls/1.0"

namespace OpenSWMM::Sdk
{
class IJob;
/** @brief Supported native UI routes with runtime identities rather than object names. */
enum class UiActivityCommandKind : quint32 {
    ActivateDocument = 1, ///< Focus a current mounted document in this main window.
    SetDockOpen = 2,      ///< Show/raise or hide a calling extension's own dock.
    TriggerCommand = 3    ///< Trigger an enabled own action or approved built-in dialog command.
};
/** @brief Owning explicit UI action; observers should defer their own state-changing work. */
struct UiActivityCommand {
    UiActivityCommandKind kind = UiActivityCommandKind::ActivateDocument; ///< Native route.
    QUuid windowId;   ///< Exact current main-window lifetime.
    QUuid entityId;   ///< Current document/dock/command UUID from a copied snapshot.
    bool open = true; ///< Desired logical dock state for SetDockOpen.
    QUuid origin;     ///< Optional caller token on synchronous resulting observations.
};
/** @brief Frozen optional typed activity control capability. */
class IActivityControls
{
public:
    /** @brief Destroy an implementation; consumers borrow the host-owned capability. */
    virtual ~IActivityControls() = default;
    /** @brief Start through the existing native run controller for an exact Ready model generation.
     * @param[in] sessionId Per-open model identity, never a persistent project UUID.
     * @param[in] loadGeneration Exact accepted generation from a current state snapshot.
     * @param[out] error Optional explicit refusal detail.
     * @return Unique accepted attempt UUID, or null when no attempt was accepted.
     * @note Accepted preparation can settle Failed before a worker exists. Saving
     * follows the existing run controller. Native refusal before acceptance is
     * not a synthetic process. Reusing a status row always creates a new UUID. */
    virtual QUuid startRunAttempt(const QUuid &sessionId, quint64 loadGeneration,
                                  QString *error = nullptr) = 0;
    /** @brief Resolve this host's retained borrowed job handle to its unique UUID.
     * @param[in] job Handle returned by this host's runJob(); never dereferenced here.
     * @return UUID for a known own job, or null; metadata retirement does not destroy the handle. */
    virtual QUuid jobOperationId(IJob *job) const = 0;
    /** @brief Request cancellation of this window's exact run or this host's own job.
     * @param[in] operationId Unique accepted attempt, never a row ID or title.
     * @param[out] error Optional refusal detail.
     * @return True for an active accepted/idempotent request; false after settlement,
     * retirement, unavailable controller or foreign private-job ownership.
     * @note Cancellation is cooperative and cannot guarantee output rollback. */
    virtual bool cancelOperation(const QUuid &operationId, QString *error = nullptr) = 0;
    /** @brief Apply one validated scoped UI action through its existing native controller.
     * @param[in] command Current window and runtime target identities.
     * @param[out] error Optional explicit refusal detail, cleared on success.
     * @return True for an accepted action/valid no-op; false for stale, unavailable,
     * disabled or foreign contribution targets. */
    virtual bool executeActivityUiCommand(const UiActivityCommand &command,
                                          QString *error = nullptr) = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IActivityControls, OPENSWMM_ACTIVITY_CONTROLS_IID)
