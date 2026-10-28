/**
 * @file ActivitySignalAdapter.hpp
 * @brief Optional consumer-local Qt signals for process and scoped UI activity.
 * @details Compile with openswmmvis_add_activity_adapter(). Managed host delivery
 * is GUI-thread and lifetime guarded. Ordinary downstream Qt slots must not throw
 * and retain Qt's normal queued-disconnect behavior.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IModelEvents.hpp"
#include "sdk/IActivityEvents.hpp"

namespace OpenSWMM::Sdk
{
/** @brief Consumer-owned typed QObject projection of one managed IActivityEvents subscription. */
class ActivitySignalAdapter final : public QObject
{
    Q_OBJECT
public:
    /** @brief Register copied-value metatypes and observe with this QObject context.
     * @param[in] source Borrowed optional host capability; null creates an inert adapter.
     * @param[in] filter Copied reason/operation/session selection.
     * @param[in] parent Optional GUI-thread QObject owner. */
    explicit ActivitySignalAdapter(IActivityEvents *source, const ActivityEventFilter &filter = {},
                                   QObject *parent = nullptr);
    /** @brief Disconnect before QObject teardown. */
    ~ActivitySignalAdapter() override;
    /** @brief Inspect the managed registration handle.
     * @return True when registration was accepted and has not been explicitly reset. */
    bool isSubscribed() const;
    /** @brief End managed host delivery; ordinary queued downstream rules still apply. */
    void disconnect();
signals:
    /** @brief Emit a complete copied-value observation before domain projection.
     * @param[in] event Borrowed during direct delivery; Qt copies queued values. */
    void occurred(const OpenSWMM::Sdk::ActivityEvent &event);
    /** @brief Emit accepted/start/progress/cancel/terminal/retirement metadata.
     * @param[in] event Owning unique attempt metadata. */
    void processChanged(const OpenSWMM::Sdk::ActivityEvent &event);
    /** @brief Emit current focus/document/dock/approved-command invalidation.
     * @param[in] event Runtime UI identity; query current scoped state before acting. */
    void uiChanged(const OpenSWMM::Sdk::ActivityEvent &event);

private:
    /** @brief Guard domain emission after potentially reentrant occurred() slots.
     * @param[in] event Borrowed source observation. */
    void dispatch(const ActivityEvent &event);
    ScopedEventSubscription mSubscription; ///< Managed connection, reset before destruction.
};
}
