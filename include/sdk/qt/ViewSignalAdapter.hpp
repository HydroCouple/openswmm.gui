/**
 * @file ViewSignalAdapter.hpp
 * @brief Optional consumer-local Qt signal projection of SDK 1.4 view observations.
 * @details Compile with openswmmvis_add_view_adapter(). The host subscription is
 * GUI-thread, managed and exception-isolated; downstream ordinary Qt slots must
 * not throw and retain Qt's normal queued-disconnect semantics.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IEventSourceV1.hpp"
#include "sdk/IEventSourceV3.hpp"

namespace OpenSWMM::Sdk
{
/** @brief Consumer-owned QObject with typed signals from one guarded V3 subscription. */
class ViewSignalAdapter final : public QObject
{
    Q_OBJECT
public:
    /** @brief Register owning event metatypes and subscribe with this QObject context.
     * @param[in] source Borrowed optional host capability, null creates an inert adapter.
     * @param[in] filter Copied kind/session/attachment selection.
     * @param[in] parent Optional GUI-thread QObject owner. */
    explicit ViewSignalAdapter(IEventSourceV3 *source, const ViewEventFilter &filter = {},
                               QObject *parent = nullptr);
    /** @brief Disconnect before QObject teardown. */
    ~ViewSignalAdapter() override;
    /** @brief Inspect the managed registration handle.
     * @return True when a connection has been accepted and not explicitly reset. */
    bool isSubscribed() const;
    /** @brief End managed host delivery; ordinary downstream Qt rules still apply. */
    void disconnect();
signals:
    /** @brief Emit a complete observation before its domain projection.
     * @param[in] event Copied-value observation, borrowed during direct delivery. */
    void occurred(const OpenSWMM::Sdk::ViewEvent &event);
    /** @brief Emit model geometry/options/rename/attribute/selection invalidation.
     * @param[in] event Original owning metadata. */
    void modelChanged(const OpenSWMM::Sdk::ViewEvent &event);
    /** @brief Emit layer membership/configuration/source/feature invalidation.
     * @param[in] event Original owning metadata and runtime attachment identity. */
    void layerChanged(const OpenSWMM::Sdk::ViewEvent &event);
    /** @brief Emit attached-source/active/variable/published-time invalidation.
     * @param[in] event Original owning metadata and source epoch. */
    void resultsChanged(const OpenSWMM::Sdk::ViewEvent &event);
    /** @brief Emit project display-unit invalidation, separate from source units.
     * @param[in] event Original owning metadata. */
    void unitsChanged(const OpenSWMM::Sdk::ViewEvent &event);

private:
    /** @brief Guard domain projection after potentially reentrant occurred() slots.
     * @param[in] event Borrowed host event. */
    void dispatch(const ViewEvent &event);
    ScopedEventSubscription mSubscription; ///< Disconnectable managed host subscription.
};
}
