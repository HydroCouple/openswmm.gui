/**
 * @file EventSignalAdapter.hpp
 * @brief Optional plugin-local QObject projection of typed extension observations.
 * @details Compile this header and EventSignalAdapter.cpp into each consumer with
 * AUTOMOC, using the installed SDK's adapter helper. No GUI executable symbols or
 * mandatory SDK runtime library are needed. Host subscriptions remain GUI-only;
 * ordinary downstream Qt connections follow Qt's normal delivery/lifetime rules.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once

#include "sdk/IModelEvents.hpp"
#include <QObject>

namespace OpenSWMM::Sdk
{
/** @brief Consumer-owned typed Qt signals backed by one managed host subscription.
 * @details Create on the GUI thread after discovering IModelEvents. This
 * adapter borrows the source only during construction; its QObject context
 * guards subsequent callbacks. Destroying or disconnecting it ends its managed
 * source subscription. Ordinary queued downstream slots may still receive
 * calls posted before a disconnect. Such slots must not throw; exception
 * isolation applies to host subscriptions, not arbitrary Qt slot chains.
 *
 * Each event emits occurred() first, then its domain signal if the adapter
 * remains alive/subscribed. A slot may disconnect or destroy the adapter during
 * occurred() to suppress the domain projection of that same observation.
 * Sequence values identify publications, not model revisions or delivery order. */
class EventSignalAdapter final : public QObject
{
    Q_OBJECT
public:
    /** @brief Register value metatypes and subscribe using this adapter as the receiver.
     * @param[in] source Borrowed optional host capability; null creates an unsubscribed adapter.
     * @param[in] filter Copied selection of event kinds and optional persistent model UUID.
     * @param[in] parent Optional QObject owner, on the same GUI thread.
     * @note Invalid/wrong-thread registration leaves isSubscribed() false. */
    explicit EventSignalAdapter(IModelEvents *source, const EventFilter &filter = {},
                                QObject *parent = nullptr);
    /** @brief Disconnect the source subscription before QObject teardown. */
    ~EventSignalAdapter() override;
    /** @brief Inspect the managed source connection handle.
     * @return True when registration produced a valid Qt connection that has not been reset. */
    [[nodiscard]] bool isSubscribed() const;
    /** @brief Disconnect the managed source subscription.
     * @note GUI-thread operation; downstream Qt connections retain normal Qt semantics. */
    void disconnect();

signals:
    /** @brief Deliver the complete typed observation before its domain projection.
     * @param[in] event Borrowed owning-value event during direct delivery; Qt copies queued values. */
    void occurred(const OpenSWMM::Sdk::Event &event);
    /** @brief Deliver discovery, closing, activation, selection, edits or authored attributes.
     * @param[in] kind Model event reason from the original observation.
     * @param[in] event Copied model identity and optional native object identifier.
     * @param[in] sequence Host-local publication identity, not state revision. */
    void modelChanged(OpenSWMM::Sdk::EventKind kind, const OpenSWMM::Sdk::ModelEvent &event,
                      quint64 sequence);
    /** @brief Deliver viewport, canvas membership or watched boundary invalidation.
     * @param[in] kind Spatial event reason from the original observation.
     * @param[in] event Copied model identity and boundary-layer identity when present.
     * @param[in] sequence Host-local publication identity, not state revision. */
    void spatialChanged(OpenSWMM::Sdk::EventKind kind, const OpenSWMM::Sdk::SpatialEvent &event,
                        quint64 sequence);
    /** @brief Deliver active results-source invalidation.
     * @param[in] event Copied model identity; no reader or playback state is retained.
     * @param[in] sequence Host-local publication identity, not state revision. */
    void resultsChanged(const OpenSWMM::Sdk::ResultsEvent &event, quint64 sequence);
    /** @brief Request a fresh Simulation Status snapshot.
     * @param[in] event Typed registry invalidation without an individual attempt identity.
     * @param[in] sequence Host-local publication identity, not state revision. */
    void runsChanged(const OpenSWMM::Sdk::RunsEvent &event, quint64 sequence);
    /** @brief Deliver this extension's settings-key/group invalidation.
     * @param[in] event Copied extension-local settings key/group.
     * @param[in] sequence Host-local publication identity, not state revision. */
    void settingsChanged(const OpenSWMM::Sdk::SettingsEvent &event, quint64 sequence);
    /** @brief Request a fresh host palette/token snapshot.
     * @param[in] event Typed palette invalidation without widget ownership.
     * @param[in] sequence Host-local publication identity, not state revision. */
    void themeChanged(const OpenSWMM::Sdk::ThemeEvent &event, quint64 sequence);
    /** @brief Deliver progress from one of this extension's workers.
     * @param[in] event Copied display title, normalized progress and stage text.
     * @param[in] sequence Host-local publication identity, not state revision. */
    void jobProgress(const OpenSWMM::Sdk::JobProgressEvent &event, quint64 sequence);
    /** @brief Deliver completion from one of this extension's workers.
     * @param[in] event Copied display title and worker status/error.
     * @param[in] sequence Host-local publication identity, not state revision. */
    void jobFinished(const OpenSWMM::Sdk::JobFinishedEvent &event, quint64 sequence);

private:
    /** @brief Emit one whole observation followed by its typed domain projection.
     * @param[in] event Borrowed host value, valid through this callback. */
    void dispatch(const Event &event);
    ScopedEventSubscription mSubscription; ///< Managed source connection, reset before destruction.
};
}
