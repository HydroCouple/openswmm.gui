/**
 * @file ValueEventAdapter.hpp
 * @brief Consumer-local example projecting SDK metadata into ordinary Qt values.
 * @details This demonstration is compiled by its consumer, not exported by the
 * GUI or a shared SDK runtime. It does not bind Python, expose engine objects,
 * dispatch commands, or change the frozen event interfaces.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <sdk/IEventSourceV1.hpp>
#include <sdk/IEventSourceV2.hpp>
#include <sdk/IEventSourceV3.hpp>
#include <sdk/IEventSourceV4.hpp>
#include <QPointer>
#include <QVariantMap>

namespace OpenSWMM::Examples
{
/** @brief Owning primitive-value projection of managed V2/V3/V4 observations.
 * @details Each record has schemaVersion=1, schema=org.openswmm.gui.value-events,
 * domain=state/view/activity, type=event/snapshot, available, and data. Data keys
 * follow the frozen source fields. All 64-bit integers are decimal strings;
 * UUIDs are lowercase braceless strings or null; times are ISO UTC strings or
 * null. Lists contain current metadata only, without geometry, engine pointers,
 * feature identifiers, result arrays, replay, or a second event backlog.
 *
 * Construct and query on the source GUI thread. Connect signals before initial
 * snapshots and keep each initial sequence cutoff immutable. Managed disconnect,
 * source stop/destruction and invalid receiver affinity suppress later host
 * delivery; ordinary downstream queued Qt connections retain Qt's usual rules.
 * Slots connected directly to these signals must not throw. */
class ValueEventAdapter final : public QObject
{
    Q_OBJECT
public:
    /** @brief Discover the three optional services and subscribe using this receiver.
     * @param[in] sourceLifetime Borrowed host lifetime QObject; never reparented or owned.
     * @param[in] parent Optional consumer QObject owner on the source GUI thread.
     * @note Null, missing or wrong-thread services leave the corresponding domain unavailable. */
    explicit ValueEventAdapter(QObject *sourceLifetime, QObject *parent = nullptr);
    /** @brief Disconnect managed subscriptions before QObject teardown. */
    ~ValueEventAdapter() override;
    /** @brief Inspect complete managed service availability without querying source state.
     * @return True only while all three source subscriptions are live on their GUI thread. */
    Q_INVOKABLE bool isSubscribed() const;
    /** @brief Stop managed delivery and make all subsequent snapshots unavailable.
     * @note Receiver-thread operation; this does not cancel downstream queued Qt calls. */
    Q_INVOKABLE void disconnect();
    /** @brief Copy current V2 metadata without creating or modifying native state.
     * @return State snapshot envelope; unavailable/off-thread/stopped sources have available=false
     * and empty data. An available source may legitimately contain zero sessions. */
    Q_INVOKABLE QVariantMap stateSnapshot() const;
    /** @brief Copy one current V3 view snapshot by its per-open lifetime identity.
     * @param[in] sessionId UUID text from the V2 snapshot, not a persistent model UUID.
     * @return View snapshot envelope; invalid, retired or unavailable targets have available=false
     * and empty data. No target is resolved or made actionable by this query. */
    Q_INVOKABLE QVariantMap viewSnapshot(const QString &sessionId) const;
    /** @brief Copy current V4 metadata in the source host's existing scope.
     * @return Activity snapshot envelope; unavailable/off-thread/stopped sources have available=false
     * and empty data. Private jobs/contributions retain their source ownership boundaries. */
    Q_INVOKABLE QVariantMap activitySnapshot() const;
signals:
    /** @brief Deliver one copied V2 lifecycle/save observation as primitive values.
     * @param[in] record Owning state event envelope, borrowed through direct delivery. */
    void stateOccurred(const QVariantMap &record);
    /** @brief Deliver one copied V3 model/layer/results observation as primitive values.
     * @param[in] record Owning view event envelope, borrowed through direct delivery. */
    void viewOccurred(const QVariantMap &record);
    /** @brief Deliver one scoped V4 process/UI observation as primitive values.
     * @param[in] record Owning activity event envelope, borrowed through direct delivery. */
    void activityOccurred(const QVariantMap &record);

private:
    /** @brief Check receiver/source affinity and managed connection availability.
     * @param[in] subscription Domain connection being inspected.
     * @return True when a borrowed service may be queried on this thread. */
    bool canQuery(const OpenSWMM::Sdk::ScopedEventSubscription &subscription) const;
    QPointer<QObject> mLifetime; ///< Borrowed source lifetime, guarding all raw capabilities.
    OpenSWMM::Sdk::IEventSourceV2 *mState =
        nullptr; ///< Borrowed V2 source, never used without guard.
    OpenSWMM::Sdk::IEventSourceV3 *mView =
        nullptr; ///< Borrowed V3 source, never used without guard.
    OpenSWMM::Sdk::IEventSourceV4 *mActivity =
        nullptr; ///< Borrowed V4 source, never used without guard.
    OpenSWMM::Sdk::ScopedEventSubscription mStateSubscription; ///< Managed lifecycle subscription.
    OpenSWMM::Sdk::ScopedEventSubscription mViewSubscription;  ///< Managed view subscription.
    OpenSWMM::Sdk::ScopedEventSubscription
        mActivitySubscription; ///< Managed activity subscription.
};
}
