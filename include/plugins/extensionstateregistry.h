/**
 * @file extensionstateregistry.h
 * @brief Shared application-owned authoritative extension session state and save scopes.
 * @details This QObject-only registry is independent of concrete GUI model classes.
 * Native controllers supply metadata and explicit completion barriers; plugin hosts
 * expose guarded public SDK observations. Stack instances support isolated tests.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IViewEvents.hpp"
#include "sdk/IActivityEvents.hpp"
#include <QPointer>
#include <memory>

namespace openswmmvis
{
/** @brief Native-controller metadata copied into an authoritative session record. */
struct SessionMetadata {
    QUuid instanceId;          ///< Persistent model identity; distinct sessions may share it.
    QString title;             ///< Current model-window caption.
    QString inputPath;         ///< Current model input path, empty while unsaved.
    QString projectPath;       ///< Current/inferred project-sidecar path.
    bool dirty = false;        ///< Native model/project dirty state.
    quint64 modelRevision = 0; ///< Native model edit revision.
};

/** @brief GUI-thread session registry shared by every plugin host in the application. */
class ExtensionStateRegistry final : public QObject
{
    Q_OBJECT
public:
    /** @brief Construct an independent registry, or the application-owned instance.
     * @param[in] parent Optional QObject owner on this GUI thread. */
    explicit ExtensionStateRegistry(QObject *parent = nullptr);
    /** @brief Discard queued observations and retained operation metadata. */
    ~ExtensionStateRegistry() override;
    /** @brief Resolve the application-owned registry without recreating it during teardown.
     * @return Borrowed child of QCoreApplication, or null without an active application. */
    static ExtensionStateRegistry *instance();
    /** @brief Register one open lifetime, idempotently for an existing owner.
     * @param[in] owner Required GUI-thread native QObject, guarded until destruction.
     * @param[in] metadata Owning current metadata.
     * @return New session UUID, existing UUID, or null for an unavailable owner.
     * @note Stores extensionSessionId on owner before synchronous sessionRegistered(). */
    QUuid registerSession(QObject *owner, const SessionMetadata &metadata);
    /** @brief Resolve an already registered owner without creating state.
     * @param[in] owner Borrowed native object identity.
     * @return Session UUID, or null. */
    QUuid sessionForOwner(QObject *owner) const;
    /** @brief Resolve the native owner of a live session.
     * @param[in] sessionId Open-lifetime identity.
     * @return Guarded borrowed QObject, or null after removal/destruction. */
    QObject *ownerForSession(const QUuid &sessionId) const;
    /** @brief Copy all live states and the committed-publication watermark.
     * @return Side-effect-free GUI-thread snapshot. */
    OpenSWMM::Sdk::StateSnapshot snapshot() const;
    /** @brief Copy one live state without creating it.
     * @param[in] sessionId Open-lifetime identity.
     * @return State with a null sessionId when absent. */
    OpenSWMM::Sdk::ModelSessionState session(const QUuid &sessionId) const;
    /** @brief Copy tracked view metadata without touching native objects.
     * @param[in] sessionId Registered open lifetime.
     * @return Current copied state/watermark, or a null-session snapshot. */
    OpenSWMM::Sdk::ViewSnapshot viewSnapshot(const QUuid &sessionId) const;
    /** @brief Refresh copied view state at a native construction/restoration boundary.
     * @param[in] sessionId Registered open lifetime.
     * @param[in] snapshot Native copied metadata; supplied session/sequence are ignored.
     * @note This boundary does not publish a domain invalidation. */
    void refreshViews(const QUuid &sessionId, OpenSWMM::Sdk::ViewSnapshot snapshot);
    /** @brief Commit one domain invalidation and its latest copied view state.
     * @param[in] sessionId Registered open lifetime.
     * @param[in] snapshot Native metadata after the semantic operation.
     * @param[in] event Bounded reason/details; session/revision/sequence assigned here. */
    void publishView(const QUuid &sessionId, OpenSWMM::Sdk::ViewSnapshot snapshot,
                     OpenSWMM::Sdk::ViewEvent event);
    /** @brief Recheck a queued domain observation per receiving host.
     * @param[in] event Owning copied observation.
     * @return True for the current ready generation/attachment/source, or its detach history. */
    bool isDeliverable(const OpenSWMM::Sdk::ViewEvent &event) const;
    /** @brief Copy retained window runs, private owner jobs and scoped UI metadata.
     * @param[in] ownerId Calling host lifetime.
     * @param[in] windowId Main-window scope.
     * @return Owning state with the shared publication watermark. */
    OpenSWMM::Sdk::ActivitySnapshot activitySnapshot(const QUuid &ownerId,
                                                     const QUuid &windowId) const;
    /** @brief Copy a retained attempt without creating state.
     * @param[in] operationId Unique attempt UUID.
     * @return Copied state, with null operationId when absent. */
    OpenSWMM::Sdk::ProcessState operation(const QUuid &operationId) const;
    /** @brief Accept a new uniquely identified attempt in Queued state.
     * @param[in] state Captured ownership/model/window metadata; null operationId generates a UUID.
     * @return Accepted UUID, or null for duplicate/invalid ownership. */
    QUuid beginOperation(OpenSWMM::Sdk::ProcessState state);
    /** @brief Record native runner/worker start once for a live attempt.
     * @param[in] operationId Unique retained attempt.
     * @param[in] startedAt Native timestamp, or invalid to use current UTC time. */
    void startOperation(const QUuid &operationId, const QDateTime &startedAt = {});
    /** @brief Commit coalesced normalized progress without accepting nonfinite values.
     * @param[in] operationId Active unique attempt.
     * @param[in] fraction Finite progress, clamped to [0,1].
     * @param[in] stage Bounded copied display description. */
    void progressOperation(const QUuid &operationId, double fraction, const QString &stage = {});
    /** @brief Record an idempotent cancellation request before settlement.
     * @param[in] operationId Active retained attempt.
     * @return True for an active accepted/repeated request, false after settlement/retirement. */
    bool requestOperationCancel(const QUuid &operationId);
    /** @brief Commit exactly one immutable terminal outcome.
     * @param[in] operationId Unique retained attempt.
     * @param[in] outcome Succeeded, Failed or Cancelled; other phases are ignored.
     * @param[in] error Copied bounded failure/cancellation detail.
     * @param[in] errorCode Native diagnostic code where available.
     * @param[in] finishedAt Native settlement timestamp, or invalid to use current UTC time. */
    void finishOperation(const QUuid &operationId, OpenSWMM::Sdk::ProcessPhase outcome,
                          const QString &error = {}, int errorCode = 0,
                          const QDateTime &finishedAt = {});
    /** @brief Retire completed metadata while preserving its queued outcome history.
     * @param[in] operationId Retained terminal attempt; active attempts are not removed. */
    void retireOperation(const QUuid &operationId);
    /** @brief Copy scoped UI state at an explicit registration/backfill boundary.
     * @param[in] ownerId Private host lifetime.
     * @param[in] state Copied current UI metadata, with an established window identity. */
    void refreshActivityUi(const QUuid &ownerId, OpenSWMM::Sdk::UiActivityState state);
    /** @brief Commit copied scoped UI metadata and queue its invalidation.
     * @param[in] ownerId Owner whose cached UI snapshot is updated.
     * @param[in] state Current copied UI state.
     * @param[in] event UI reason/identity; sequence is assigned here. */
    void publishActivityUi(const QUuid &ownerId, OpenSWMM::Sdk::UiActivityState state,
                            OpenSWMM::Sdk::ActivityEvent event);
    /** @brief Discard a stopped host's private snapshots and completed job metadata.
     * @param[in] ownerId Private host lifetime; native shared runs remain. */
    void forgetActivityOwner(const QUuid &ownerId);
    /** @brief Recheck nonterminal progress and live UI targets before each receiver.
     * @param[in] event Copied activity observation.
     * @return True for current live invalidations or immutable transition/removal history. */
    bool isDeliverable(const OpenSWMM::Sdk::ActivityEvent &event) const;
    /** @brief Update copied native metadata and record a broad invalidation.
     * @param[in] sessionId A registered open lifetime.
     * @param[in] metadata Owning latest native metadata.
     * @param[in] changes Invalidation categories; unchanged metadata alone is suppressed.
     * @note Loading updates remain queryable but do not emit intermediate StateChanged. */
    void updateSession(const QUuid &sessionId, const SessionMetadata &metadata,
                       OpenSWMM::Sdk::StateChanges changes = OpenSWMM::Sdk::StateChange::Metadata);
    /** @brief Start a new accepted load generation.
     * @param[in] sessionId Registered session not closing or saving.
     * @return Generation, or zero on refusal. */
    quint64 beginLoad(const QUuid &sessionId);
    /** @brief Commit a settled readiness/failure outcome only for the current attempt.
     * @param[in] sessionId Registered session identity.
     * @param[in] generation Accepted load generation returned by beginLoad().
     * @param[in] ok True after the native readiness barrier succeeds.
     * @param[in] error Copied failure diagnostic, empty on success. */
    void finishLoad(const QUuid &sessionId, quint64 generation, bool ok, const QString &error = {});
    /** @brief Commit close and make the session unavailable for resolution.
     * @param[in] sessionId Registered open lifetime; repeated calls are inert. */
    void beginClosing(const QUuid &sessionId);
    /** @brief Remove live state before queuing its terminal close record.
     * @param[in] sessionId Registered open lifetime; repeated calls are inert. */
    void retireSession(const QUuid &sessionId);
    /** @brief Check whether a copied observation still describes a deliverable transition.
     * @param[in] event Owning queued observation.
     * @return False for obsolete live/ready/change generations; terminal history remains valid. */
    bool isDeliverable(const OpenSWMM::Sdk::StateEvent &event) const;
    /** @brief Check native owner availability after reentrant callbacks.
     * @param[in] sessionId Captured open-lifetime identity.
     * @param[in] expectedOwner Borrowed original owner identity; never dereferenced.
     * @return True for the same surviving owner whose session is not closing. */
    bool isCurrent(const QUuid &sessionId, QObject *expectedOwner) const;
    /** @brief Check a synchronous save request after potentially reentrant collection.
     * @param[in] request Captured operation/session/load-generation identity.
     * @return True only while that operation still owns the same available generation. */
    bool isCurrent(const OpenSWMM::Sdk::SaveRequest &request) const;
    /** @brief Begin a scoped save or join a compatible outer operation.
     * @param[in] sessionIds Affected live sessions; duplicate IDs are ignored.
     * @param[in] kind Outer save category.
     * @param[in] targetPath Intended final destination, retained by joined nested scopes.
     * @param[in] joinExisting Permit joining when all sessions share one active operation.
     * @param[out] error Optional refusal/invalidated-collection detail.
     * @return Operation UUID or null on refusal/failure during synchronous collection. */
    QUuid beginSave(const QList<QUuid> &sessionIds, OpenSWMM::Sdk::SaveKind kind,
                    const QString &targetPath, bool joinExisting, QString *error = nullptr);
    /** @brief Inspect whether a scope still owns this session's save.
     * @param[in] sessionId Open-lifetime identity, possibly already retired.
     * @return True until the outer operation ends. */
    bool isSaving(const QUuid &sessionId) const;
    /** @brief Resolve a session's active save correlation identity.
     * @param[in] sessionId Open-lifetime identity.
     * @return Operation UUID or null. */
    QUuid operationFor(const QUuid &sessionId) const;
    /** @brief Resolve an outer save destination selected after preparation began.
     * @param[in] operationId Active save correlation identity.
     * @param[in] targetPath Final intended destination for the terminal outcome.
     * @note Existing immutable SaveStarted records retain their original target. */
    void updateSaveTarget(const QUuid &operationId, const QString &targetPath);
    /** @brief Commit one terminal save outcome and release affected sessions.
     * @param[in] operationId Correlation identity returned by beginSave().
     * @param[in] ok True only after the intended final transaction commits.
     * @param[in] error Copied terminal failure detail, empty on success. */
    void endSave(const QUuid &operationId, bool ok, const QString &error = {});

signals:
    /** @brief Synchronously attach native facades after authoritative state is available.
     * @param[in] owner Guarded native owner whose extensionSessionId is already set.
     * @param[in] sessionId Unique open-lifetime identity. */
    void sessionRegistered(QObject *owner, const QUuid &sessionId);
    /** @brief Deliver a copied transition from the ordered GUI-thread drain.
     * @param[in] event Immutable observation; hosts recheck per receiver before forwarding. */
    void stateEventReady(const OpenSWMM::Sdk::StateEvent &event);
    /** @brief Deliver domain metadata in the same globally ordered session/view drain.
     * @param[in] event Copied native view invalidation. */
    void viewEventReady(const OpenSWMM::Sdk::ViewEvent &event);
    /** @brief Deliver process/UI metadata from the ordered session/view/activity drain.
     * @param[in] event Immutable owning activity observation. */
    void activityEventReady(const OpenSWMM::Sdk::ActivityEvent &event);
    /** @brief Synchronously collect extension state before native persistence starts.
     * @param[in] request Captured save identity, revalidated after each observer by the host. */
    void beforeSave(const OpenSWMM::Sdk::SaveRequest &request);

private:
    friend class ExtensionSaveOperation;
    struct Data; ///< Private live records, queued observations and active save ownership.
    std::unique_ptr<Data> mData; ///< Owning private implementation.
    /** @brief Sequence/coalesce one process or UI observation.
     * @param[in] event Owning unsequenced activity metadata. */
    void enqueueActivity(OpenSWMM::Sdk::ActivityEvent event);
    /** @brief Queue one immutable globally sequenced record.
     * @param[in] kind Transition reason.
     * @param[in] state Copied affected session state.
     * @param[in] changes Broad invalidation categories.
     * @param[in] save Optional copied save detail. */
    void enqueue(OpenSWMM::Sdk::StateEventKind kind, const OpenSWMM::Sdk::ModelSessionState &state,
                 OpenSWMM::Sdk::StateChanges changes = {},
                 const OpenSWMM::Sdk::SaveState &save = {});
    /** @brief Post one drain unless its FIFO head is held by an active save. */
    void scheduleDrain();
    /** @brief Drain stable FIFO publications without nested/reentrant dispatch. */
    void drain();
};

/** @brief Native RAII save scope; nested joined scopes leave completion to their owner. */
class ExtensionSaveOperation final
{
public:
    /** @brief Begin or join a correlated save scope.
     * @param[in] registry Borrowed application registry, guarded through callbacks.
     * @param[in] sessions Affected open-lifetime identities.
     * @param[in] kind Outer transaction category.
     * @param[in] targetPath Intended final destination.
     * @param[in,out] error Optional borrowed diagnostic destination, outliving this scope.
     * @param[in] joinExisting Permit a nested scope to join one shared outer operation. */
    ExtensionSaveOperation(ExtensionStateRegistry *registry, const QList<QUuid> &sessions,
                           OpenSWMM::Sdk::SaveKind kind, const QString &targetPath,
                           QString *error = nullptr, bool joinExisting = false);
    /** @brief End an owned operation with success or a captured/fallback failure diagnostic. */
    ~ExtensionSaveOperation();
    /** @brief Forbid copying a terminal-outcome responsibility.
     * @param[in] other Unused noncopyable source. */
    ExtensionSaveOperation(const ExtensionSaveOperation &other) = delete;
    /** @brief Forbid assigning a terminal-outcome responsibility.
     * @param[in] other Unused noncopyable source.
     * @return Assignment is unavailable. */
    ExtensionSaveOperation &operator=(const ExtensionSaveOperation &other) = delete;
    /** @brief Check whether construction accepted an operation.
     * @return True for a surviving registry and accepted correlation identity. */
    bool valid() const;
    /** @brief Inspect terminal-outcome ownership.
     * @return True only for a newly created outer scope. */
    bool ownsOperation() const;
    /** @brief Read the shared save correlation identity.
     * @return Operation UUID or null after refused construction. */
    QUuid operationId() const;
    /** @brief Set a newly determined final destination on an owned outer operation.
     * @param[in] targetPath Final destination, such as a recovery sidecar selected after snapshotting.
     * @note Joined nested scopes cannot replace their outer transaction's destination. */
    void setTargetPath(const QString &targetPath);
    /** @brief Record successful native commit; outer destruction publishes the outcome. */
    void succeed();

private:
    QPointer<ExtensionStateRegistry> mRegistry; ///< Guarded application registry.
    QUuid mOperation;          ///< Accepted correlation identity, null after refusal.
    QString *mError = nullptr; ///< Borrowed diagnostic destination, outliving this scope.
    bool mOwner = false;       ///< This scope created, rather than joined, the operation.
    bool mSucceeded = false;   ///< Native caller confirmed final transaction commit.
};
}
