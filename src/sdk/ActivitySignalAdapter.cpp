/**
 * @file ActivitySignalAdapter.cpp
 * @brief Consumer-local typed Qt projection of V4 process/UI observations.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "sdk/qt/ActivitySignalAdapter.hpp"
#include <QPointer>

namespace OpenSWMM::Sdk
{
ActivitySignalAdapter::ActivitySignalAdapter(IEventSourceV4 *source,
                                             const ActivityEventFilter &filter, QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ProcessState>();
    qRegisterMetaType<ActivityEvent>();
    qRegisterMetaType<ActivitySnapshot>();
    if (source)
        mSubscription.reset(source->observeActivity(
            this, filter, [this](const ActivityEvent &event) { dispatch(event); }));
}
ActivitySignalAdapter::~ActivitySignalAdapter()
{
    disconnect();
}
bool ActivitySignalAdapter::isSubscribed() const
{
    return mSubscription.isConnected();
}
void ActivitySignalAdapter::disconnect()
{
    mSubscription.reset();
}
void ActivitySignalAdapter::dispatch(const ActivityEvent &event)
{
    const QPointer<ActivitySignalAdapter> guard(this);
    emit occurred(event);
    if (!guard || !mSubscription.isConnected())
        return;
    if (event.kind <= ActivityEventKind::OperationRemoved)
        emit processChanged(event);
    else
        emit uiChanged(event);
}
}
