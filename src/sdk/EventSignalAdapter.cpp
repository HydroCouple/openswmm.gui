/**
 * @file EventSignalAdapter.cpp
 * @brief Plugin-local Qt signal projection of the optional V1 host event service.
 * @details This source is compiled by consumers against the installed public SDK.
 * It registers owning value metatypes and retains one receiver-scoped subscription.
 * Direct slots may disconnect or destroy the adapter while handling occurred();
 * a QPointer guard prevents later access or domain emission after that action.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "sdk/qt/EventSignalAdapter.hpp"

#include <QPointer>

namespace OpenSWMM::Sdk
{
EventSignalAdapter::EventSignalAdapter(IEventSourceV1 *source, const EventFilter &filter,
                                       QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<EventKind>();
    qRegisterMetaType<ModelEvent>();
    qRegisterMetaType<SpatialEvent>();
    qRegisterMetaType<ResultsEvent>();
    qRegisterMetaType<RunsEvent>();
    qRegisterMetaType<SettingsEvent>();
    qRegisterMetaType<ThemeEvent>();
    qRegisterMetaType<JobProgressEvent>();
    qRegisterMetaType<JobFinishedEvent>();
    qRegisterMetaType<Event>();
    if (source)
        mSubscription.reset(
            source->subscribe(this, filter, [this](const Event &event) { dispatch(event); }));
}

EventSignalAdapter::~EventSignalAdapter()
{
    disconnect();
}

bool EventSignalAdapter::isSubscribed() const
{
    return mSubscription.isConnected();
}

void EventSignalAdapter::disconnect()
{
    mSubscription.reset();
}

void EventSignalAdapter::dispatch(const Event &event)
{
    const QPointer<EventSignalAdapter> guard(this);
    emit occurred(event);
    if (!guard || !mSubscription.isConnected())
        return;
    if (const auto *value = std::get_if<ModelEvent>(&event.payload))
        emit modelChanged(event.kind, *value, event.sequence);
    else if (const auto *value = std::get_if<SpatialEvent>(&event.payload))
        emit spatialChanged(event.kind, *value, event.sequence);
    else if (const auto *value = std::get_if<ResultsEvent>(&event.payload))
        emit resultsChanged(*value, event.sequence);
    else if (const auto *value = std::get_if<RunsEvent>(&event.payload))
        emit runsChanged(*value, event.sequence);
    else if (const auto *value = std::get_if<SettingsEvent>(&event.payload))
        emit settingsChanged(*value, event.sequence);
    else if (const auto *value = std::get_if<ThemeEvent>(&event.payload))
        emit themeChanged(*value, event.sequence);
    else if (const auto *value = std::get_if<JobProgressEvent>(&event.payload))
        emit jobProgress(*value, event.sequence);
    else if (const auto *value = std::get_if<JobFinishedEvent>(&event.payload))
        emit jobFinished(*value, event.sequence);
}
}
