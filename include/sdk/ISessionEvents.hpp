/**
 * @file ISessionEvents.hpp
 * @brief Authoritative model-session lifecycle, revision snapshots and project-save observations.
 * @details This optional SDK 1.3 capability is discovered through the host lifetime
 * QObject. It preserves the separately frozen IModelEvents event contract. All operations
 * use the GUI thread; values contain identities and copied metadata, never private
 * window or engine pointers. Interface and value layouts are frozen for this IID.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QFlags>
#include <QList>
#include <QMetaObject>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUuid>
#include <functional>

/** @brief Frozen Qt discovery identity for authoritative lifecycle observations. */
#define OPENSWMM_SESSION_EVENTS_IID "io.opensimulator.openswmm.IEventSource/2.0"

namespace OpenSWMM::Sdk
{
class IModelInstance;
inline constexpr auto SESSION_EVENTS_IID =
    OPENSWMM_SESSION_EVENTS_IID; ///< Public alias for the frozen discovery identity.

/** @brief Current availability of an open model-window session. */
enum class SessionStatus : quint32 {
    Created = 1, ///< Window exists, with no completed load attempt.
    Loading = 2, ///< A particular load generation is being prepared/restored.
    Ready = 3,   ///< Native readiness barrier completed for the current generation.
    Failed = 4,  ///< Current load failed; retry is possible while the window remains open.
    Closing = 5  ///< Close committed; resolution must not return an actionable model.
};

/** @brief Broad invalidations of queryable state, without claiming changed-field deltas. */
enum class StateChange : quint32 {
    None = 0,         ///< No additional invalidation category.
    Metadata = 1,     ///< Identity, title, path or dirty metadata changed.
    Model = 2,        ///< Authored model state or its native edit revision changed.
    Selection = 4,    ///< Shared model selection changed.
    Viewport = 8,     ///< Visible area or canvas CRS changed.
    Layers = 16,      ///< Canvas layer membership changed.
    Results = 32,     ///< Active result source changed.
    ProjectState = 64 ///< Project-owned extension/presentation state changed.
};
Q_DECLARE_FLAGS(StateChanges, StateChange)
Q_DECLARE_OPERATORS_FOR_FLAGS(StateChanges)

/** @brief Owning metadata for one open lifetime, distinct from its persistent project identity. */
struct ModelSessionState {
    QUuid sessionId;            ///< Unique per-window open lifetime; never reused on reopen.
    QUuid instanceId;           ///< Persistent model UUID; multiple sessions may share this value.
    quint64 loadGeneration = 0; ///< Incremented for each accepted load attempt in this session.
    quint64 stateRevision = 0;  ///< Monotonic revision of this session's observed state.
    quint64 modelRevision = 0;  ///< Native authored-model revision, scoped by session/generation.
    SessionStatus status = SessionStatus::Created; ///< Current readiness/close status.
    QString title;                                 ///< Copied model-window caption.
    QString inputPath;   ///< Current model path, possibly empty for an unsaved model.
    QString projectPath; ///< Current/inferred project sidecar path.
    QString error;       ///< Current load failure detail; empty after a successful load.
    bool dirty = false;  ///< Current authored/project dirty indication.
    bool saving = false; ///< A scoped save operation is still active for this session.
};

/** @brief Atomic GUI-thread copy of live session metadata and its event watermark.
 * @details Subscribe first, then query this value. A current-state view may discard
 * queued observations at or below sequence when adopting this initial snapshot.
 * This contains current live metadata, not terminal-event history or replay, and
 * is not an atomic copy of arbitrary engine, geometry or result payloads. */
struct StateSnapshot {
    quint64 sequence =
        0; ///< Registry-wide committed-publication watermark, not IModelEvents' host sequence.
    QList<ModelSessionState> sessions; ///< Live Created/Loading/Ready/Failed/Closing sessions.
};

/** @brief Lifecycle or save transition recorded by the shared application registry. */
enum class StateEventKind : quint32 {
    SessionOpened = 1,  ///< An open lifetime was registered.
    LoadStarted = 2,    ///< A new accepted load generation started.
    ModelReady = 3,     ///< The generation's native readiness barrier completed.
    LoadFailed = 4,     ///< The generation ended without becoming ready.
    StateChanged = 5,   ///< Broad queryable-state invalidation with merged categories.
    SaveStarted = 6,    ///< Informational queued save start; state collection uses a separate hook.
    SaveFinished = 7,   ///< Save transaction succeeded or failed.
    SessionClosing = 8, ///< Close committed and resolution is unavailable.
    SessionClosed = 9   ///< Session was removed from live state before this record was published.
};

/** @brief Meaning of a persisted save destination. */
enum class SaveKind : quint32 {
    Project = 1, ///< Normal model/project save transaction.
    Sidecar = 2, ///< Standalone project-sidecar write.
    Recovery = 3 ///< Independent recovery snapshot/project write.
};
/** @brief Outcome state of a correlated save operation. */
enum class SaveOutcome : quint32 {
    None = 0,      ///< This event does not describe a save.
    Started = 1,   ///< Save preparation/state collection has begun.
    Succeeded = 2, ///< Final intended transaction committed successfully.
    Failed = 3     ///< The transaction did not commit successfully.
};

/** @brief Copied outcome and destination for one uniquely identified save operation. */
struct SaveState {
    QUuid operationId; ///< Correlation UUID shared by start/finish and nested save scopes.
    SaveKind kind = SaveKind::Project;       ///< Outer transaction's save category.
    SaveOutcome outcome = SaveOutcome::None; ///< Current operation outcome.
    QString targetPath; ///< Outer final destination; recovery starts may precede its selection.
    QString error;      ///< Failure detail, empty on success.
};

/** @brief Immutable transition record delivered later on the GUI thread.
 * @details Sequence is globally ordered within this registry. Session identity
 * prevents a closed/reopened persistent UUID from resolving to another lifetime.
 * Lifecycle/save records can describe historical terminal outcomes. Before acting,
 * resolve sessionId and compare load generation/current status. Closed records
 * retain the last Closing state, while live snapshots no longer contain them. */
struct StateEvent {
    StateEventKind kind = StateEventKind::SessionOpened; ///< Transition reason.
    quint64 sequence = 0;      ///< Shared registry publication identity/watermark.
    ModelSessionState session; ///< Copied affected session state.
    StateChanges changes; ///< Broad categories for StateChanged; no field/object delta is implied.
    SaveState save;       ///< Save detail for SaveStarted/SaveFinished, otherwise outcome None.
};

/** @brief Copied selection of lifecycle kinds and optionally one open lifetime. */
struct StateEventFilter {
    QList<StateEventKind> kinds; ///< Empty selects every lifecycle kind.
    QUuid
        sessionId; ///< Null selects all sessions; persistent instance IDs are not session filters.
    /** @brief Match a transition without querying mutable host state.
     * @param[in] event Copied transition being considered.
     * @return True when kind and optional open-lifetime identity match. */
    bool accepts(const StateEvent &event) const
    {
        return (kinds.isEmpty() || kinds.contains(event.kind)) &&
               (sessionId.isNull() || sessionId == event.session.sessionId);
    }
};

/** @brief Synchronous, non-vetoing opportunity to collect state before a save writes.
 * @details Keep callbacks short and update only this extension's state through
 * supported services. Nested save attempts are refused. Do not close/reload models,
 * enter nested event loops or retain this value as authority for deferred writes. */
struct SaveRequest {
    QUuid operationId;                 ///< Correlated outer save transaction.
    QUuid sessionId;                   ///< Affected open lifetime.
    QUuid instanceId;                  ///< Persistent model UUID at collection time.
    quint64 loadGeneration = 0;        ///< Generation whose state is being collected.
    SaveKind kind = SaveKind::Project; ///< Outer save category.
    QString
        targetPath; ///< Outer destination, possibly empty until a recovery snapshot path is selected.
};

/** @brief Frozen optional access to session snapshots and authoritative lifecycle observations. */
class ISessionEvents
{
public:
    /** @brief Destroy an implementation; callers borrow the host-owned capability. */
    virtual ~ISessionEvents() = default;
    /** @brief Copy live session metadata without creating facades or publishing events.
     * @return Atomic GUI-thread snapshot with a shared registry watermark. */
    virtual StateSnapshot stateSnapshot() const = 0;
    /** @brief Resolve exactly one ready, current open lifetime.
     * @param[in] sessionId Open-lifetime identity from a snapshot or observation.
     * @return Borrowed live facade, or null for an unknown/loading/failed/closing session.
     * @note Recheck after deferred work; no engine lease or lifetime ownership is transferred. */
    virtual IModelInstance *resolveSession(const QUuid &sessionId) const = 0;
    /** @brief Observe future queued state transitions with GUI-thread lifetime guards.
     * @param[in] context Required GUI-thread QObject receiver.
     * @param[in] filter Copied kind/session selection.
     * @param[in] callback GUI-thread callback borrowing one owning-value transition during delivery.
     * @return Disconnectable Qt connection, invalid when registration cannot be accepted.
     * @note No replay: events committed before registration are excluded, including pending ones.
     * Query stateSnapshot() after subscribing and ignore events at/below its watermark.
     * Host stop, receiver destruction/disconnect and invalidated live state suppress delivery. */
    virtual QMetaObject::Connection
    observeState(QObject *context, const StateEventFilter &filter,
                 std::function<void(const StateEvent &)> callback) = 0;
    /** @brief Register a short synchronous pre-save state collector.
     * @param[in] context Required GUI-thread QObject receiver.
     * @param[in] callback Non-vetoing state collector, guarded before every invocation.
     * @return Disconnectable connection, invalid when registration cannot be accepted.
     * @note Separate from queued SaveStarted observations. Exceptions are isolated;
     * deletion/reload/close during collection makes the save request no longer current. */
    virtual QMetaObject::Connection
    observeBeforeSave(QObject *context, std::function<void(const SaveRequest &)> callback) = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::ISessionEvents, OPENSWMM_SESSION_EVENTS_IID)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ModelSessionState)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::StateSnapshot)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::StateEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::StateEventKind)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::StateChanges)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::SaveRequest)
