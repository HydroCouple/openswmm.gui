/**
 * @file ViewSignalAdapter.cpp
 * @brief Consumer-local typed Qt signals for model, layer and result metadata changes.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "sdk/qt/ViewSignalAdapter.hpp"
#include <QPointer>

namespace OpenSWMM::Sdk
{
ViewSignalAdapter::ViewSignalAdapter(IViewEvents *source, const ViewEventFilter &filter,
                                     QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ViewEvent>();
    qRegisterMetaType<ViewEventKind>();
    qRegisterMetaType<ViewSnapshot>();
    if (source)
        mSubscription.reset(source->observeViews(
            this, filter, [this](const ViewEvent &event) { dispatch(event); }));
}
ViewSignalAdapter::~ViewSignalAdapter()
{
    disconnect();
}
bool ViewSignalAdapter::isSubscribed() const
{
    return mSubscription.isConnected();
}
void ViewSignalAdapter::disconnect()
{
    mSubscription.reset();
}
void ViewSignalAdapter::dispatch(const ViewEvent &event)
{
    const QPointer<ViewSignalAdapter> guard(this);
    emit occurred(event);
    if (!guard || !mSubscription.isConnected())
        return;
    if (event.kind >= ViewEventKind::ModelGeometryChanged &&
        event.kind <= ViewEventKind::ModelSelectionChanged)
        emit modelChanged(event);
    else if (event.kind >= ViewEventKind::LayerAdded &&
             event.kind <= ViewEventKind::LayerSourceChanged)
        emit layerChanged(event);
    else if (event.kind >= ViewEventKind::ResultsSourceChanged &&
             event.kind <= ViewEventKind::ResultsTimeChanged)
        emit resultsChanged(event);
    else if (event.kind == ViewEventKind::ProjectUnitsChanged)
        emit unitsChanged(event);
}
}
