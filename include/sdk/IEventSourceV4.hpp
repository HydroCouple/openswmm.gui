/**
 * @file IEventSourceV4.hpp
 * @brief Versioned process attempts and scoped UI lifecycle observations.
 * @details Optional SDK 1.5 capability discovered on ExtensionHost::lifetime().
 * Calls and managed callbacks use the GUI thread. Metadata owns values, never
 * runners, jobs, widgets or engine handles. Earlier event contracts remain frozen.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QDateTime>
#include <QList>
#include <QMetaObject>
#include <QMetaType>
#include <QObject>
#include <QStringList>
#include <QUuid>
#include <functional>

/** @brief Frozen Qt discovery identity for process/UI observations. */
#define OPENSWMM_EVENT_SOURCE_V4_IID "io.opensimulator.openswmm.IEventSource/4.0"

namespace OpenSWMM::Sdk
{
/** @brief Native shared simulation attempt or one calling extension's private worker. */
enum class ProcessKind : quint32 {
    Run = 1, ///< Shared native simulation attempt.
    Job = 2  ///< Private extension worker.
};
/** @brief Accepted scheduling/preparation, worker availability and distinct terminal outcomes. */
enum class ProcessPhase : quint32 {
    Queued = 1,    ///< Accepted preparation/scheduling, without claiming engine execution.
    Running = 2,   ///< Native runner/worker has started its execution path.
    Succeeded = 3, ///< Settled successfully; a late cancel cannot change this outcome.
    Failed = 4,    ///< Settled with an explicit worker/preparation error.
    Cancelled = 5  ///< Settled as cancelled; no output rollback is implied.
};

/** @brief Owning metadata for one unique accepted attempt, independent of row IDs/titles.
 * @details A run may succeed after cancellation was requested. Native run outcomes
 * follow the runner result. Void-returning extension jobs latch cancellation and
 * exceptions at worker settlement: an exception is Failed; otherwise cancellation
 * winning settlement is Cancelled. Jobs are visible only to their owning host. */
struct ProcessState {
    QUuid operationId; ///< Unique attempt UUID, never reused for another execution.
    ProcessKind kind = ProcessKind::Run;       ///< Shared run or private extension worker.
    ProcessPhase phase = ProcessPhase::Queued; ///< Current/terminal outcome.
    QUuid windowId;                            ///< Runtime main-window scope.
    QUuid ownerId;         ///< Private host lifetime for jobs; null for shared native runs.
    QString ownerPluginId; ///< Owning extension identity for jobs; empty for shared runs.
    QUuid sessionId;       ///< Captured native model lifetime for runs; null for unbound jobs.
    QUuid instanceId; ///< Captured persistent model identity, possibly duplicated across sessions.
    quint64 loadGeneration = 0; ///< Captured accepted model generation; zero for unbound jobs.
    int rowId = -1;    ///< Presentation-only Simulation Status row ID, reusable across attempts.
    QString title;     ///< Human-readable label, not an identity.
    QString inputPath; ///< Captured run input path, empty for private jobs.
    QString engineVersion; ///< Captured native engine label.
    double progress = 0.;  ///< Last finite normalized progress, preserved on failure/cancellation.
    QString stage;         ///< Bounded latest progress description.
    bool cancelRequested = false; ///< A request, independently of terminal outcome.
    int errorCode = 0;    ///< Native error code where available; zero does not imply success.
    QString error;        ///< Bounded terminal diagnostic, empty on success.
    QDateTime acceptedAt; ///< Accepted preparation/scheduling timestamp.
    QDateTime startedAt;  ///< Native runner/worker start timestamp, invalid if never started.
    QDateTime finishedAt; ///< Terminal settlement timestamp.
    quint64 revision = 0; ///< Monotonic per-operation observed-state revision.
    /** @brief Check whether this record has a settled outcome.
     * @return True for Succeeded, Failed or Cancelled. */
    bool isTerminal() const
    {
        return phase == ProcessPhase::Succeeded || phase == ProcessPhase::Failed ||
               phase == ProcessPhase::Cancelled;
    }
};

/** @brief Runtime classification of a mounted MDI document. */
enum class DocumentKind : quint32 {
    Model = 1,     ///< Native model document with a per-open session identity.
    Extension = 2, ///< Successfully mounted extension document, optionally model-bound.
    Utility = 3    ///< Other native document, without inventing a model association.
};
/** @brief Copied global MDI metadata, with actual focus separate from cached model context. */
struct DocumentState {
    QUuid documentId;                          ///< Runtime MDI attachment UUID, retired on removal.
    DocumentKind kind = DocumentKind::Utility; ///< Native model, extension or utility.
    QUuid sessionId;       ///< Associated native model lifetime where explicitly known.
    QString ownerPluginId; ///< Extension owner, otherwise empty.
    QString title;         ///< Copied current caption, without private persisted view state.
    bool active = false;   ///< Actual QMdiArea active subwindow.
};
/** @brief Copied metadata for one calling extension's registered dock. */
struct DockState {
    QUuid dockId;          ///< Runtime contribution identity, replaced on unregister/recreate.
    QString id;            ///< Extension-local registered key.
    QString title;         ///< Current user-facing caption.
    bool open = false;     ///< Logically open, independent of tab exposure.
    bool exposed = false;  ///< Last native visibility state; a tabified open dock may be unexposed.
    bool floating = false; ///< Current native floating state.
    int area = 0;          ///< Qt dock-area value, zero when not established.
};
/** @brief Calling extension's command or an approved existing built-in dialog command. */
struct CommandState {
    QUuid commandId;        ///< Runtime QAction registration identity, replaced on recreation.
    QString id;             ///< Local extension command key or approved built-in dialog key.
    bool builtin = false;   ///< True for an approved existing host dialog command.
    QString title;          ///< Current action caption.
    bool enabled = false;   ///< Current native enabled state.
    bool visible = false;   ///< Current native action visibility.
    bool checkable = false; ///< Native action supports a checked state.
    bool checked = false;   ///< Current checked state.
    QStringList shortcuts;  ///< Copied portable shortcut strings.
};
/** @brief Copied UI metadata scoped to one main window and the calling extension's contributions. */
struct UiActivityState {
    QUuid windowId;                 ///< Runtime main-window identity.
    QUuid activeDocumentId;         ///< Actual active MDI document, null when none is active.
    QList<DocumentState> documents; ///< Global MDI attachment metadata; no private view JSON.
    QList<DockState> docks;         ///< Calling extension's registered docks only.
    QList<CommandState> commands;   ///< Own commands plus approved built-in dialog commands.
};
/** @brief Pure copied retained process/UI metadata at the shared V2/V3/V4 watermark.
 * @details Subscribe before the initial query. This is current retained state,
 * not replay: superseded run attempts and older completed job records can retire.
 * Active operations are retained; private terminal job snapshots are bounded.
 * Retiring metadata never invalidates a borrowed IJob handle's existing lifetime. */
struct ActivitySnapshot {
    quint64 sequence = 0; ///< Shared application registry publication watermark.
    QList<ProcessState>
        processes;      ///< Shared native runs in this window and this host's private jobs.
    UiActivityState ui; ///< Current scoped UI metadata.
};
/** @brief Stable process transitions and UI invalidation reasons. */
enum class ActivityEventKind : quint32 {
    OperationAccepted = 1,     ///< Unique attempt accepted for preparation/scheduling.
    OperationStarted = 2,      ///< Native runner/worker entered its start path.
    OperationProgress = 3,     ///< Coalesced latest progress; query current state for display.
    CancellationRequested = 4, ///< A request won before settlement; outcome is still independent.
    OperationFinished = 5,     ///< Exactly one immutable terminal outcome for an accepted attempt.
    OperationRemoved = 6,      ///< Retained metadata retired; the UUID is never reassigned.
    ActiveDocumentChanged =
        7,               ///< Actual MDI focus changed, including utility documents/no document.
    DocumentChanged = 8, ///< Mounted document metadata changed.
    DocumentRemoved = 9, ///< Historical document retirement; target no longer resolves.
    DockChanged = 10,    ///< Calling extension's logical/exposed dock state changed.
    DockRemoved = 11,    ///< Calling extension's dock registration retired.
    CommandChanged = 12, ///< Own/approved command metadata changed.
    CommandRemoved = 13  ///< Own/approved command registration retired.
};
/** @brief Owning deferred process transition or scoped UI invalidation.
 * @details Terminal/removal records retain captured identities after model or row
 * removal. They do not grant authority to act on a closed model or another attempt.
 * UI changes identify runtime targets; query current snapshots before commands. */
struct ActivityEvent {
    ActivityEventKind kind =
        ActivityEventKind::OperationAccepted; ///< Transition/invalidation reason.
    quint64 sequence = 0;                     ///< Shared application publication sequence.
    ProcessState process; ///< Process metadata, with null operationId for UI observations.
    QUuid windowId;       ///< Affected main-window scope.
    QUuid ownerId;  ///< Private host scope for jobs/UI projections, null for shared native runs.
    QUuid entityId; ///< Runtime document/dock/command identity; null for no active document.
    QUuid origin;   ///< Optional caller token for synchronous UI-command effects.
};
/** @brief Registration-time event reason and optional process/session selection. */
struct ActivityEventFilter {
    QList<ActivityEventKind> kinds; ///< Empty accepts all scoped V4 observations.
    QUuid operationId;              ///< Null accepts all processes and UI changes.
    QUuid
        sessionId; ///< Null accepts all; a specific session selects matching processes and excludes UI.
    /** @brief Match copied metadata without native queries.
     * @param[in] event Candidate observation.
     * @return True when all specified selectors match. */
    bool accepts(const ActivityEvent &event) const
    {
        return (kinds.isEmpty() || kinds.contains(event.kind)) &&
               (operationId.isNull() || operationId == event.process.operationId) &&
               (sessionId.isNull() || sessionId == event.process.sessionId);
    }
};
/** @brief Frozen optional GUI-thread process and UI snapshot/observation capability. */
class IEventSourceV4
{
public:
    /** @brief Destroy an implementation; consumers borrow the host-owned capability. */
    virtual ~IEventSourceV4() = default;
    /** @brief Copy current retained process and scoped UI metadata without creating state.
     * @return Owning snapshot, empty after shutdown or on the wrong thread. */
    virtual ActivitySnapshot activitySnapshot() const = 0;
    /** @brief Observe future queued scoped transitions with managed lifetime/thread guards.
     * @param[in] context Required GUI-thread lifetime guard.
     * @param[in] filter Copied reason/operation/session selection.
     * @param[in] callback Short exception-isolated observer; copy values for later use.
     * @return Disconnectable handle, invalid for an unavailable registration.
     * @note No replay. Subscribe then query the initial snapshot and retain its
     * initial cutoff. Host stop, disconnect and receiver destruction suppress
     * future managed callbacks. Ordinary queued downstream Qt rules still apply. */
    virtual QMetaObject::Connection
    observeActivity(QObject *context, const ActivityEventFilter &filter,
                    std::function<void(const ActivityEvent &)> callback) = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IEventSourceV4, OPENSWMM_EVENT_SOURCE_V4_IID)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ProcessState)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ActivityEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ActivitySnapshot)
