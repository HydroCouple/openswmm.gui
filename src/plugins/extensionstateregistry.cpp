/**
 * @file extensionstateregistry.cpp
 * @brief Authoritative session metadata, ordered deferred observations and scoped save outcomes.
 * @details Native state changes commit synchronously. One FIFO dispatcher emits copied
 * observations on a later GUI turn, suppressing obsolete live generations and holding
 * affected sessions while synchronous save collection/native persistence are active.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionstateregistry.h"
#include <QCoreApplication>
#include <QHash>
#include <QThread>
#include <QVariant>
#include <algorithm>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
/** @brief Private authoritative records retained independently of plugin facades. */
struct ExtensionStateRegistry::Data {
    /** @brief Native owner and copied state for one live lifetime. */
    struct Entry {
        QPointer<QObject> owner; ///< Guarded native owner.
        QObject *key = nullptr;  ///< Non-dereferenced identity used to remove the owner index.
        ModelSessionState state; ///< Authoritative copied state.
    };
    /** @brief Save ownership surviving retirement of an affected native window. */
    struct Operation {
        SaveState save;                    ///< Correlation, kind and outer final destination.
        QList<ModelSessionState> captured; ///< Last states retained for terminal outcomes.
    };
    QHash<QUuid, Entry> sessions;       ///< Live states by unique session identity.
    QHash<QObject *, QUuid> owners;     ///< Native owner identity to open lifetime.
    QList<QUuid> order;                 ///< Stable registration order of live states.
    QHash<QUuid, Operation> operations; ///< Active outer save scopes.
    QHash<QUuid, QUuid> saving; ///< Session to active operation, retained through retirement.
    QList<StateEvent> pending;  ///< Globally ordered immutable observations.
    QHash<QUuid, ViewSnapshot> views; ///< Current copied native view metadata by session.
    QList<ViewEvent> pendingViews; ///< Domain observations merged into the same sequence drain.
    quint64 sequence = 0;       ///< Shared committed-publication watermark.
    bool scheduled = false;     ///< A queued drain callback is outstanding.
    bool draining = false;      ///< Prevent nested event loops from recursively dispatching.
};

namespace
{
/** @brief Apply native metadata while reporting whether any copied value changed.
 * @param[in,out] state Destination state; lifecycle/correlation fields are untouched.
 * @param[in] metadata Latest owning native values.
 * @return True when a copied metadata field changed. */
bool applyMetadata(ModelSessionState &state, const SessionMetadata &metadata)
{
    const bool changed =
        state.instanceId != metadata.instanceId || state.title != metadata.title ||
        state.inputPath != metadata.inputPath || state.projectPath != metadata.projectPath ||
        state.dirty != metadata.dirty || state.modelRevision != metadata.modelRevision;
    state.instanceId = metadata.instanceId;
    state.title = metadata.title;
    state.inputPath = metadata.inputPath;
    state.projectPath = metadata.projectPath;
    state.dirty = metadata.dirty;
    state.modelRevision = metadata.modelRevision;
    return changed;
}
}

ExtensionStateRegistry::ExtensionStateRegistry(QObject *parent)
    : QObject(parent), mData(std::make_unique<Data>())
{
    qRegisterMetaType<ModelSessionState>();
    qRegisterMetaType<StateSnapshot>();
    qRegisterMetaType<StateEvent>();
    qRegisterMetaType<SaveRequest>();
    qRegisterMetaType<ViewEvent>();
    qRegisterMetaType<ViewSnapshot>();
}
ExtensionStateRegistry::~ExtensionStateRegistry() = default;

ExtensionStateRegistry *ExtensionStateRegistry::instance()
{
    static QPointer<ExtensionStateRegistry> registry;
    auto *application = QCoreApplication::instance();
    if (!application || QCoreApplication::closingDown())
        return nullptr;
    Q_ASSERT(QThread::currentThread() == application->thread());
    if (!registry)
        registry = new ExtensionStateRegistry(application);
    return registry;
}

QUuid ExtensionStateRegistry::registerSession(QObject *owner, const SessionMetadata &metadata)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!owner || owner->thread() != thread())
        return {};
    if (const auto existing = mData->owners.value(owner); !existing.isNull())
        return existing;
    const QUuid id = QUuid::createUuid();
    ModelSessionState state;
    state.sessionId = id;
    state.stateRevision = 1;
    applyMetadata(state, metadata);
    mData->sessions.insert(id, {owner, owner, state});
    mData->owners.insert(owner, id);
    mData->order.append(id);
    connect(owner, &QObject::destroyed, this, [this, id]() {
        beginClosing(id);
        retireSession(id);
    });
    const QPointer<ExtensionStateRegistry> guard(this);
    const QPointer<QObject> ownerGuard(owner);
    owner->setProperty("extensionSessionId", id);
    if (!guard || !ownerGuard)
        return {};
    enqueue(StateEventKind::SessionOpened, state);
    emit sessionRegistered(owner, id);
    return id;
}

QUuid ExtensionStateRegistry::sessionForOwner(QObject *owner) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return mData->owners.value(owner);
}
QObject *ExtensionStateRegistry::ownerForSession(const QUuid &id) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto it = mData->sessions.constFind(id);
    return it == mData->sessions.cend() ? nullptr : it->owner.data();
}
StateSnapshot ExtensionStateRegistry::snapshot() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    StateSnapshot result;
    result.sequence = mData->sequence;
    for (const auto &id : mData->order)
        result.sessions.append(mData->sessions.value(id).state);
    return result;
}
ModelSessionState ExtensionStateRegistry::session(const QUuid &id) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return mData->sessions.value(id).state;
}

ViewSnapshot ExtensionStateRegistry::viewSnapshot(const QUuid &id) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto state = session(id);
    if (state.sessionId.isNull())
        return {};
    auto result = mData->views.value(id);
    result.session = state;
    result.sequence = mData->sequence;
    return result;
}
void ExtensionStateRegistry::refreshViews(const QUuid &id, ViewSnapshot snapshot)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto state = session(id);
    if (state.sessionId.isNull() || state.status == SessionStatus::Closing)
        return;
    snapshot.session = {};
    snapshot.sequence = 0;
    mData->views.insert(id, std::move(snapshot));
}
void ExtensionStateRegistry::publishView(const QUuid &id, ViewSnapshot snapshot, ViewEvent event)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto it = mData->sessions.find(id);
    if (it == mData->sessions.end() || !it->owner || it->state.status == SessionStatus::Closing)
        return;
    refreshViews(id, std::move(snapshot));
    ++it->state.stateRevision;
    if (it->state.status != SessionStatus::Ready) {
        ++mData->sequence;
        return;
    }
    StateChanges broad = StateChange::Model;
    switch (event.kind) {
    case ViewEventKind::ModelSelectionChanged:
        broad = StateChange::Selection;
        break;
    case ViewEventKind::LayerAdded:
    case ViewEventKind::LayerRemoved:
    case ViewEventKind::LayerMetadataChanged:
    case ViewEventKind::LayerOrderChanged:
    case ViewEventKind::LayerStyleChanged:
    case ViewEventKind::LayerSchemaChanged:
    case ViewEventKind::LayerFeaturesChanged:
    case ViewEventKind::LayerSelectionChanged:
    case ViewEventKind::LayerFilterChanged:
    case ViewEventKind::LayerCrsChanged:
    case ViewEventKind::LayerSourceChanged:
        broad = StateChange::Layers;
        break;
    case ViewEventKind::ResultsSourceChanged:
    case ViewEventKind::ResultsActiveChanged:
    case ViewEventKind::ResultsVariableChanged:
    case ViewEventKind::ResultsTimeChanged:
        broad = StateChange::Results;
        break;
    case ViewEventKind::ProjectUnitsChanged:
        broad = StateChange::ProjectState;
        break;
    default:
        break;
    }
    enqueue(StateEventKind::StateChanged, it->state, broad);
    event.session = it->state;
    if (event.keys.size() > 128)
        event.keys.clear();
    if (event.kind != ViewEventKind::ObjectRenamed && event.kind != ViewEventKind::LayerRemoved) {
        for (qsizetype i = mData->pendingViews.size(); i-- > 0;) {
            const auto old = mData->pendingViews.at(i);
            if (old.kind != event.kind || old.session.sessionId != id ||
                old.session.loadGeneration != event.session.loadGeneration ||
                old.layerSessionId != event.layerSessionId)
                continue;
            if (old.origin != event.origin)
                event.origin = {};
            if (old.objectName != event.objectName)
                event.objectName.clear();
            if (old.objectNamespace != event.objectNamespace)
                event.objectNamespace.clear();
            if (old.keys.isEmpty() || event.keys.isEmpty()) {
                event.keys.clear();
            } else {
                event.keys.append(old.keys);
                event.keys.removeDuplicates();
                if (event.keys.size() > 128)
                    event.keys.clear();
            }
            mData->pendingViews.removeAt(i);
        }
    }
    event.sequence = ++mData->sequence;
    mData->pendingViews.append(std::move(event));
    scheduleDrain();
}
bool ExtensionStateRegistry::isDeliverable(const ViewEvent &event) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto state = session(event.session.sessionId);
    if (state.sessionId.isNull() || !ownerForSession(state.sessionId) ||
        state.status != SessionStatus::Ready || state.loadGeneration != event.session.loadGeneration)
        return false;
    if (event.kind == ViewEventKind::LayerRemoved || event.layerSessionId.isNull())
        return true;
    const auto view = mData->views.value(state.sessionId);
    const bool attached = std::any_of(view.layers.cbegin(), view.layers.cend(), [&](const auto &layer) {
        return layer.layerSessionId == event.layerSessionId;
    });
    if (!attached)
        return false;
    if (event.kind == ViewEventKind::ResultsSourceChanged ||
        event.kind == ViewEventKind::ResultsActiveChanged ||
        event.kind == ViewEventKind::ResultsVariableChanged ||
        event.kind == ViewEventKind::ResultsTimeChanged)
        return std::any_of(view.results.cbegin(), view.results.cend(), [&](const auto &result) {
            return result.layerSessionId == event.layerSessionId &&
                   result.sourceGeneration == event.sourceGeneration;
        });
    return true;
}

void ExtensionStateRegistry::updateSession(const QUuid &id, const SessionMetadata &metadata,
                                           StateChanges changes)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto it = mData->sessions.find(id);
    if (it == mData->sessions.end() || it->state.status == SessionStatus::Closing)
        return;
    const bool changed = applyMetadata(it->state, metadata);
    if (!changed && (changes == StateChanges{} || changes == StateChange::Metadata))
        return;
    if (changed)
        changes |= StateChange::Metadata;
    ++it->state.stateRevision;
    if (it->state.status == SessionStatus::Loading) {
        ++mData
              ->sequence; // Queryable load metadata advances the watermark without intermediate callbacks.
        return;
    }
    enqueue(StateEventKind::StateChanged, it->state, changes);
}

quint64 ExtensionStateRegistry::beginLoad(const QUuid &id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto it = mData->sessions.find(id);
    if (it == mData->sessions.end() || it->state.status == SessionStatus::Closing || isSaving(id))
        return 0;
    ++it->state.loadGeneration;
    ++it->state.stateRevision;
    it->state.status = SessionStatus::Loading;
    it->state.error.clear();
    enqueue(StateEventKind::LoadStarted, it->state);
    return it->state.loadGeneration;
}
void ExtensionStateRegistry::finishLoad(const QUuid &id, quint64 generation, bool ok,
                                        const QString &error)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto it = mData->sessions.find(id);
    if (it == mData->sessions.end() || it->state.status != SessionStatus::Loading || !generation ||
        generation != it->state.loadGeneration)
        return;
    ++it->state.stateRevision;
    it->state.status = ok ? SessionStatus::Ready : SessionStatus::Failed;
    it->state.error = ok ? QString{} : error;
    enqueue(ok ? StateEventKind::ModelReady : StateEventKind::LoadFailed, it->state);
}
void ExtensionStateRegistry::beginClosing(const QUuid &id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto it = mData->sessions.find(id);
    if (it == mData->sessions.end() || it->state.status == SessionStatus::Closing)
        return;
    it->state.status = SessionStatus::Closing;
    ++it->state.stateRevision;
    enqueue(StateEventKind::SessionClosing, it->state);
}
void ExtensionStateRegistry::retireSession(const QUuid &id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!mData->sessions.contains(id))
        return;
    beginClosing(id);
    const auto entry = mData->sessions.take(id);
    mData->owners.remove(entry.key);
    mData->order.removeAll(id);
    mData->views.remove(id);
    const auto operation = mData->saving.value(id);
    if (auto op = mData->operations.find(operation); op != mData->operations.end())
        for (auto &captured : op->captured)
            if (captured.sessionId == id)
                captured = entry.state;
    enqueue(StateEventKind::SessionClosed, entry.state);
}

bool ExtensionStateRegistry::isDeliverable(const StateEvent &event) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto current = session(event.session.sessionId);
    const bool live = !current.sessionId.isNull() && ownerForSession(current.sessionId);
    switch (event.kind) {
    case StateEventKind::SessionOpened:
        return live && current.status != SessionStatus::Closing;
    case StateEventKind::LoadStarted:
    case StateEventKind::StateChanged:
        return live && current.status != SessionStatus::Closing &&
               current.loadGeneration == event.session.loadGeneration;
    case StateEventKind::ModelReady:
        return live && current.status == SessionStatus::Ready &&
               current.loadGeneration == event.session.loadGeneration;
    case StateEventKind::LoadFailed:
    case StateEventKind::SaveStarted:
    case StateEventKind::SaveFinished:
    case StateEventKind::SessionClosing:
    case StateEventKind::SessionClosed:
        return true;
    }
    return false;
}
bool ExtensionStateRegistry::isCurrent(const QUuid &id, QObject *owner) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto it = mData->sessions.constFind(id);
    return owner && it != mData->sessions.cend() && it->owner == owner &&
           it->state.status != SessionStatus::Closing;
}
bool ExtensionStateRegistry::isCurrent(const SaveRequest &request) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto it = mData->sessions.constFind(request.sessionId);
    return it != mData->sessions.cend() && it->owner &&
           it->state.status != SessionStatus::Closing &&
           it->state.instanceId == request.instanceId &&
           it->state.loadGeneration == request.loadGeneration &&
           operationFor(request.sessionId) == request.operationId;
}

QUuid ExtensionStateRegistry::beginSave(const QList<QUuid> &ids, SaveKind kind,
                                        const QString &targetPath, bool joinExisting,
                                        QString *error)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto refuse = [error](const QString &text) -> QUuid {
        if (error)
            *error = text;
        return {};
    };
    QList<QUuid> unique;
    for (const auto &id : ids)
        if (!unique.contains(id))
            unique.append(id);
    if (unique.isEmpty())
        return refuse(QStringLiteral("A save requires at least one live model session."));
    QUuid existing;
    bool anyFree = false;
    for (const auto &id : unique) {
        const auto it = mData->sessions.constFind(id);
        if (id.isNull() || it == mData->sessions.cend() || !it->owner ||
            it->state.status == SessionStatus::Closing)
            return refuse(QStringLiteral("The model session is unavailable for saving."));
        const auto operation = operationFor(id);
        if (operation.isNull())
            anyFree = true;
        else if (existing.isNull())
            existing = operation;
        else if (existing != operation)
            return refuse(QStringLiteral("The models belong to different active save operations."));
    }
    if (!existing.isNull()) {
        if (!joinExisting || anyFree)
            return refuse(QStringLiteral("A save operation is already active for this model."));
        return existing;
    }
    const QUuid operation = QUuid::createUuid();
    Data::Operation record;
    record.save = {operation, kind, SaveOutcome::Started, targetPath, {}};
    for (const auto &id : unique) {
        auto &state = mData->sessions[id].state;
        state.saving = true;
        ++state.stateRevision;
        record.captured.append(state);
        mData->saving.insert(id, operation);
    }
    mData->operations.insert(operation, record);
    for (const auto &state : record.captured)
        enqueue(StateEventKind::SaveStarted, state, {}, record.save);
    const QPointer<ExtensionStateRegistry> guard(this);
    for (const auto &state : record.captured) {
        const SaveRequest request{
            operation, state.sessionId, state.instanceId, state.loadGeneration, kind, targetPath};
        if (isCurrent(request))
            emit beforeSave(request);
        if (!guard)
            return {};
        const bool current = std::all_of(
            record.captured.cbegin(), record.captured.cend(),
            [this, operation, kind, &targetPath](const auto &captured) {
                return isCurrent(SaveRequest{operation, captured.sessionId, captured.instanceId,
                                             captured.loadGeneration, kind, targetPath});
            });
        if (!current) {
            const QString message =
                QStringLiteral("The model changed or closed during save state collection.");
            if (error)
                *error = message;
            endSave(operation, false, message);
            return {};
        }
    }
    return operation;
}

bool ExtensionStateRegistry::isSaving(const QUuid &id) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return mData->saving.contains(id);
}
QUuid ExtensionStateRegistry::operationFor(const QUuid &id) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return mData->saving.value(id);
}
void ExtensionStateRegistry::updateSaveTarget(const QUuid &id, const QString &targetPath)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto it = mData->operations.find(id);
    if (it != mData->operations.end())
        it->save.targetPath = targetPath;
}
void ExtensionStateRegistry::endSave(const QUuid &id, bool ok, const QString &error)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto it = mData->operations.find(id);
    if (it == mData->operations.end())
        return;
    const auto operation = mData->operations.take(id);
    SaveState outcome = operation.save;
    outcome.outcome = ok ? SaveOutcome::Succeeded : SaveOutcome::Failed;
    outcome.error =
        ok ? QString{} : (error.isEmpty() ? QStringLiteral("Save did not complete.") : error);
    for (auto state : operation.captured) {
        mData->saving.remove(state.sessionId);
        auto live = mData->sessions.find(state.sessionId);
        if (live != mData->sessions.end()) {
            live->state.saving = false;
            ++live->state.stateRevision;
            state = live->state;
        } else {
            state.saving = false;
            ++state.stateRevision;
        }
        enqueue(StateEventKind::SaveFinished, state, {}, outcome);
    }
    scheduleDrain();
}

void ExtensionStateRegistry::enqueue(StateEventKind kind, const ModelSessionState &state,
                                     StateChanges changes, const SaveState &save)
{
    if (kind == StateEventKind::StateChanged) {
        for (qsizetype i = mData->pending.size(); i-- > 0;)
            if (const auto &old = mData->pending.at(i);
                old.kind == StateEventKind::StateChanged &&
                old.session.sessionId == state.sessionId &&
                old.session.loadGeneration == state.loadGeneration) {
                changes |= old.changes;
                mData->pending.removeAt(i);
            }
    }
    mData->pending.append({kind, ++mData->sequence, state, changes, save});
    scheduleDrain();
}
void ExtensionStateRegistry::scheduleDrain()
{
    if (mData->scheduled || mData->draining ||
        (mData->pending.isEmpty() && mData->pendingViews.isEmpty()))
        return;
    const bool viewFirst = mData->pending.isEmpty() ||
        (!mData->pendingViews.isEmpty() &&
         mData->pendingViews.first().sequence < mData->pending.first().sequence);
    const auto sessionId = viewFirst ? mData->pendingViews.first().session.sessionId
                                     : mData->pending.first().session.sessionId;
    if (isSaving(sessionId))
        return;
    mData->scheduled = true;
    QMetaObject::invokeMethod(this, [this]() { drain(); }, Qt::QueuedConnection);
}
void ExtensionStateRegistry::drain()
{
    mData->scheduled = false;
    if (mData->draining)
        return;
    mData->draining = true;
    while (!mData->pending.isEmpty() || !mData->pendingViews.isEmpty()) {
        const bool viewFirst = mData->pending.isEmpty() ||
            (!mData->pendingViews.isEmpty() &&
             mData->pendingViews.first().sequence < mData->pending.first().sequence);
        const auto sessionId = viewFirst ? mData->pendingViews.first().session.sessionId
                                         : mData->pending.first().session.sessionId;
        if (isSaving(sessionId)) {
            mData->draining = false;
            return; // endSave() schedules again; nested event loops never busy-spin here.
        }
        const QPointer<ExtensionStateRegistry> guard(this);
        if (viewFirst) {
            const auto event = mData->pendingViews.takeFirst();
            if (!isDeliverable(event))
                continue;
            emit viewEventReady(event);
        } else {
            const auto event = mData->pending.takeFirst();
            if (!isDeliverable(event))
                continue;
            emit stateEventReady(event);
        }
        if (!guard)
            return;
    }
    mData->draining = false;
}

ExtensionSaveOperation::ExtensionSaveOperation(ExtensionStateRegistry *registry,
                                               const QList<QUuid> &sessions, SaveKind kind,
                                               const QString &targetPath, QString *error,
                                               bool joinExisting)
    : mRegistry(registry), mError(error)
{
    if (!registry) {
        if (error)
            *error = QStringLiteral("The application session registry is unavailable.");
        return;
    }
    const QUuid prior = sessions.isEmpty() ? QUuid{} : registry->operationFor(sessions.first());
    mOperation = registry->beginSave(sessions, kind, targetPath, joinExisting, error);
    mOwner = !mOperation.isNull() && mOperation != prior;
}
ExtensionSaveOperation::~ExtensionSaveOperation()
{
    if (mRegistry && mOwner && !mOperation.isNull())
        mRegistry->endSave(mOperation, mSucceeded, mError ? *mError : QString{});
}
bool ExtensionSaveOperation::valid() const
{
    return mRegistry && !mOperation.isNull() && mRegistry->mData->operations.contains(mOperation);
}
bool ExtensionSaveOperation::ownsOperation() const
{
    return valid() && mOwner;
}
QUuid ExtensionSaveOperation::operationId() const
{
    return mOperation;
}
void ExtensionSaveOperation::setTargetPath(const QString &targetPath)
{
    if (valid() && mOwner)
        mRegistry->updateSaveTarget(mOperation, targetPath);
}
void ExtensionSaveOperation::succeed()
{
    mSucceeded = true;
}
}
