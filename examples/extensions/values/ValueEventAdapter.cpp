/**
 * @file ValueEventAdapter.cpp
 * @brief Explicit lossless metadata conversion for the installed value-adapter example.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "ValueEventAdapter.hpp"
#include <QThread>
#include <cmath>

using namespace OpenSWMM::Sdk;
namespace
{
/** @brief Convert a runtime identity to canonical text or explicit absence.
 * @param[in] value Runtime identity.
 * @return Braceless UUID string, or a null QVariant. */
QVariant uuidValue(const QUuid &value)
{
    return value.isNull() ? QVariant{} : QVariant(value.toString(QUuid::WithoutBraces));
}
/** @brief Convert a timestamp to portable UTC text or explicit absence.
 * @param[in] value Source timestamp.
 * @return ISO UTC timestamp with milliseconds, or null. */
QVariant timeValue(const QDateTime &value)
{
    return value.isValid() ? QVariant(value.toUTC().toString(Qt::ISODateWithMs)) : QVariant{};
}
/** @brief Keep source real values JSON-compatible without inventing a finite substitute.
 * @param[in] value Source real metadata, normally finite under its source contract.
 * @return The finite real value, or null for a nonfinite input. */
QVariant realValue(double value)
{
    return std::isfinite(value) ? QVariant(value) : QVariant{};
}
/** @brief Copy a string list to the same primitive-list representation as other arrays.
 * @param[in] values Owning source strings.
 * @return Ordered list of QString values. */
QVariantList stringValues(const QStringList &values)
{
    QVariantList result;
    result.reserve(values.size());
    for (const auto &value : values)
        result.append(value);
    return result;
}
/** @brief Convert a complete current metadata list without retaining history or native references.
 * @tparam T Source metadata value type.
 * @tparam F Explicit value projection function type.
 * @param[in] values Current owning metadata.
 * @param[in] convert Single-record projection.
 * @return Ordered list of primitive maps. */
template <class T, class F> QVariantList arrayValues(const QList<T> &values, F convert)
{
    QVariantList result;
    result.reserve(values.size());
    for (const auto &value : values)
        result.append(convert(value));
    return result;
}
/** @brief Copy every frozen model-session metadata field.
 * @param[in] v Source session.
 * @return Primitive owning session map. */
QVariantMap sessionValue(const ModelSessionState &v)
{
    return {{"sessionId", uuidValue(v.sessionId)},
            {"instanceId", uuidValue(v.instanceId)},
            {"loadGeneration", QString::number(v.loadGeneration)},
            {"stateRevision", QString::number(v.stateRevision)},
            {"modelRevision", QString::number(v.modelRevision)},
            {"status", int(v.status)},
            {"title", v.title},
            {"inputPath", v.inputPath},
            {"projectPath", v.projectPath},
            {"error", v.error},
            {"dirty", v.dirty},
            {"saving", v.saving}};
}
/** @brief Copy every correlated-save metadata field.
 * @param[in] v Source save.
 * @return Primitive owning save map. */
QVariantMap saveValue(const SaveState &v)
{
    return {{"operationId", uuidValue(v.operationId)},
            {"kind", int(v.kind)},
            {"outcome", int(v.outcome)},
            {"targetPath", v.targetPath},
            {"error", v.error}};
}
/** @brief Copy every current layer-attachment metadata field.
 * @param[in] v Source attachment.
 * @return Primitive owning attachment map. */
QVariantMap layerValue(const LayerViewState &v)
{
    return {{"layerSessionId", uuidValue(v.layerSessionId)},
            {"layerId", v.layerId},
            {"title", v.title},
            {"type", v.type},
            {"crsWkt", v.crsWkt},
            {"visible", v.visible},
            {"opacity", realValue(v.opacity)},
            {"vector", v.vector},
            {"filter", v.filter},
            {"featureCount", QString::number(v.featureCount)},
            {"selectionCount", QString::number(v.selectionCount)}};
}
/** @brief Copy a current results rendering-channel descriptor.
 * @param[in] v Source variable/channel.
 * @return Primitive owning channel map. */
QVariantMap variableValue(const ResultVariableState &v)
{
    return {{"channelId", v.channelId}, {"variableKey", v.variableKey}, {"units", v.units}};
}
/** @brief Copy every results-source metadata field, never period arrays/readers.
 * @param[in] v Source results attachment.
 * @return Primitive owning results-source map. */
QVariantMap resultsValue(const ResultsViewState &v)
{
    return {{"layerSessionId", uuidValue(v.layerSessionId)},
            {"layerId", v.layerId},
            {"sourceGeneration", QString::number(v.sourceGeneration)},
            {"dimension", v.dimension},
            {"path", v.path},
            {"availability", int(v.availability)},
            {"error", v.error},
            {"active", v.active},
            {"live", v.live},
            {"periodCount", QString::number(v.periodCount)},
            {"currentIndex", QString::number(v.currentIndex)},
            {"currentDateTime", timeValue(v.currentDateTime)},
            {"variables", arrayValues(v.variables, variableValue)}};
}
/** @brief Copy every unique process-attempt metadata field.
 * @param[in] v Source process, including captured terminal history.
 * @return Primitive owning process map. */
QVariantMap processValue(const ProcessState &v)
{
    return {{"operationId", uuidValue(v.operationId)},
            {"kind", int(v.kind)},
            {"phase", int(v.phase)},
            {"windowId", uuidValue(v.windowId)},
            {"ownerId", uuidValue(v.ownerId)},
            {"ownerPluginId", v.ownerPluginId},
            {"sessionId", uuidValue(v.sessionId)},
            {"instanceId", uuidValue(v.instanceId)},
            {"loadGeneration", QString::number(v.loadGeneration)},
            {"rowId", v.rowId},
            {"title", v.title},
            {"inputPath", v.inputPath},
            {"engineVersion", v.engineVersion},
            {"progress", realValue(v.progress)},
            {"stage", v.stage},
            {"cancelRequested", v.cancelRequested},
            {"errorCode", v.errorCode},
            {"error", v.error},
            {"acceptedAt", timeValue(v.acceptedAt)},
            {"startedAt", timeValue(v.startedAt)},
            {"finishedAt", timeValue(v.finishedAt)},
            {"revision", QString::number(v.revision)}};
}
/** @brief Copy one canonical mounted-document descriptor.
 * @param[in] v Source document.
 * @return Primitive owning document map. */
QVariantMap documentValue(const DocumentState &v)
{
    return {{"documentId", uuidValue(v.documentId)},
            {"kind", int(v.kind)},
            {"sessionId", uuidValue(v.sessionId)},
            {"ownerPluginId", v.ownerPluginId},
            {"title", v.title},
            {"active", v.active}};
}
/** @brief Copy one caller-owned dock descriptor.
 * @param[in] v Source dock.
 * @return Primitive owning dock map. */
QVariantMap dockValue(const DockState &v)
{
    return {{"dockId", uuidValue(v.dockId)},
            {"id", v.id},
            {"title", v.title},
            {"open", v.open},
            {"exposed", v.exposed},
            {"floating", v.floating},
            {"area", v.area}};
}
/** @brief Copy one own/approved command descriptor.
 * @param[in] v Source command.
 * @return Primitive owning command map. */
QVariantMap commandValue(const CommandState &v)
{
    return {{"commandId", uuidValue(v.commandId)},
            {"id", v.id},
            {"builtin", v.builtin},
            {"title", v.title},
            {"enabled", v.enabled},
            {"visible", v.visible},
            {"checkable", v.checkable},
            {"checked", v.checked},
            {"shortcuts", stringValues(v.shortcuts)}};
}
/** @brief Copy current canonical documents and scoped contribution metadata.
 * @param[in] v Source UI projection.
 * @return Primitive owning UI map. */
QVariantMap uiValue(const UiActivityState &v)
{
    return {{"windowId", uuidValue(v.windowId)},
            {"activeDocumentId", uuidValue(v.activeDocumentId)},
            {"documents", arrayValues(v.documents, documentValue)},
            {"docks", arrayValues(v.docks, dockValue)},
            {"commands", arrayValues(v.commands, commandValue)}};
}
/** @brief Construct the versioned transport-neutral example record envelope.
 * @param[in] domain State, view or activity source family.
 * @param[in] type Event or snapshot record.
 * @param[in] available Source/target availability, without claiming command authority.
 * @param[in] data Copied field map, empty for unavailable records.
 * @return Owning primitive envelope. */
QVariantMap envelope(const QString &domain, const QString &type, bool available,
                     const QVariantMap &data = {})
{
    return {{"schemaVersion", 1},     {"schema", QStringLiteral("org.openswmm.gui.value-events")},
            {"domain", domain},       {"type", type},
            {"available", available}, {"data", data}};
}
}

namespace OpenSWMM::Examples
{
ValueEventAdapter::ValueEventAdapter(QObject *sourceLifetime, QObject *parent) : QObject(parent)
{
    if (!sourceLifetime || QThread::currentThread() != thread() ||
        sourceLifetime->thread() != thread())
        return;
    mLifetime = sourceLifetime;
    mState = qobject_cast<IEventSourceV2 *>(sourceLifetime);
    mView = qobject_cast<IEventSourceV3 *>(sourceLifetime);
    mActivity = qobject_cast<IEventSourceV4 *>(sourceLifetime);
    if (mState)
        mStateSubscription.reset(mState->observeState(this, {}, [this](const StateEvent &v) {
            if (canQuery(mStateSubscription))
                emit stateOccurred(envelope("state", "event", true,
                                            {{"kind", int(v.kind)},
                                             {"sequence", QString::number(v.sequence)},
                                             {"session", sessionValue(v.session)},
                                             {"changes", int(v.changes.toInt())},
                                             {"save", saveValue(v.save)}}));
        }));
    if (mView && mLifetime)
        mViewSubscription.reset(mView->observeViews(this, {}, [this](const ViewEvent &v) {
            if (canQuery(mViewSubscription))
                emit viewOccurred(
                    envelope("view", "event", true,
                             {{"kind", int(v.kind)},
                              {"sequence", QString::number(v.sequence)},
                              {"session", sessionValue(v.session)},
                              {"layerSessionId", uuidValue(v.layerSessionId)},
                              {"layerId", v.layerId},
                              {"sourceGeneration", QString::number(v.sourceGeneration)},
                              {"objectNamespace", v.objectNamespace},
                              {"previousName", v.previousName},
                              {"objectName", v.objectName},
                              {"keys", stringValues(v.keys)},
                              {"origin", uuidValue(v.origin)}}));
        }));
    if (mActivity && mLifetime)
        mActivitySubscription.reset(
            mActivity->observeActivity(this, {}, [this](const ActivityEvent &v) {
                if (canQuery(mActivitySubscription))
                    emit activityOccurred(envelope("activity", "event", true,
                                                   {{"kind", int(v.kind)},
                                                    {"sequence", QString::number(v.sequence)},
                                                    {"process", processValue(v.process)},
                                                    {"windowId", uuidValue(v.windowId)},
                                                    {"ownerId", uuidValue(v.ownerId)},
                                                    {"entityId", uuidValue(v.entityId)},
                                                    {"origin", uuidValue(v.origin)}}));
            }));
}
ValueEventAdapter::~ValueEventAdapter()
{
    disconnect();
}
bool ValueEventAdapter::canQuery(const ScopedEventSubscription &subscription) const
{
    return QThread::currentThread() == thread() && mLifetime && mLifetime->thread() == thread() &&
           subscription.isConnected();
}
bool ValueEventAdapter::isSubscribed() const
{
    return canQuery(mStateSubscription) && canQuery(mViewSubscription) &&
           canQuery(mActivitySubscription);
}
void ValueEventAdapter::disconnect()
{
    if (QThread::currentThread() != thread())
        return;
    mStateSubscription.reset();
    mViewSubscription.reset();
    mActivitySubscription.reset();
    mState = nullptr;
    mView = nullptr;
    mActivity = nullptr;
    mLifetime = nullptr;
}
QVariantMap ValueEventAdapter::stateSnapshot() const
{
    if (!canQuery(mStateSubscription) || !mState)
        return envelope("state", "snapshot", false);
    const auto v = mState->stateSnapshot();
    return envelope("state", "snapshot", true,
                    {{"sequence", QString::number(v.sequence)},
                     {"sessions", arrayValues(v.sessions, sessionValue)}});
}
QVariantMap ValueEventAdapter::viewSnapshot(const QString &sessionId) const
{
    const QUuid id(sessionId);
    if (!canQuery(mViewSubscription) || !mView || id.isNull())
        return envelope("view", "snapshot", false);
    const auto v = mView->viewSnapshot(id);
    if (v.session.sessionId.isNull())
        return envelope("view", "snapshot", false);
    return envelope("view", "snapshot", true,
                    {{"sequence", QString::number(v.sequence)},
                     {"session", sessionValue(v.session)},
                     {"layers", arrayValues(v.layers, layerValue)},
                     {"results", arrayValues(v.results, resultsValue)},
                     {"projectUnits", v.projectUnits},
                     {"selectedObjects", QString::number(v.selectedObjects)}});
}
QVariantMap ValueEventAdapter::activitySnapshot() const
{
    if (!canQuery(mActivitySubscription) || !mActivity)
        return envelope("activity", "snapshot", false);
    const auto v = mActivity->activitySnapshot();
    return envelope("activity", "snapshot", true,
                    {{"sequence", QString::number(v.sequence)},
                     {"processes", arrayValues(v.processes, processValue)},
                     {"ui", uiValue(v.ui)}});
}
}
