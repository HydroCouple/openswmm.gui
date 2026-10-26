/**
 * @file extensionviewaccess.cpp
 * @brief Window-owned canonical native view coordination and validated SDK command dispatch.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionviewaccess.h"
#include "plugins/extensionstateregistry.h"
#include "swmmvisprojectwindow.h"
#include <QScopeGuard>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
ExtensionViewAccess::ExtensionViewAccess(SWMMVisProjectWindow *window)
    : QObject(window), mWindow(window), mRegistry(ExtensionStateRegistry::instance())
{
    setObjectName(QStringLiteral("openswmm.extension-view-access"));
    const auto changed = [this](ViewEvent event) { record(std::move(event)); };
    mLayers = std::make_unique<ExtensionLayerBridge>(window, changed);
    mResults = std::make_unique<ExtensionResultsBridge>(
        window, [this](OpenSWMMVisLayer *layer) { return mLayers->identity(layer); }, changed);
    installExtensionModelViewEvents(window, this, changed);
    connect(window, &SWMMVisProjectWindow::modelLoaded, this, [this] { refresh(); });
    connect(window, &SWMMVisProjectWindow::extensionStateRestored, this, [this] { refresh(); });
    mInitializing = false;
    refresh();
}
ExtensionViewAccess::~ExtensionViewAccess()
{
    mInitializing =
        true; // Bridge teardown must not publish through a partially destroyed coordinator.
}
ExtensionViewAccess *ExtensionViewAccess::find(SWMMVisProjectWindow *window)
{
    return window
               ? window->findChild<ExtensionViewAccess *>(
                     QStringLiteral("openswmm.extension-view-access"), Qt::FindDirectChildrenOnly)
               : nullptr;
}
ViewSnapshot ExtensionViewAccess::capture() const
{
    ViewSnapshot result;
    if (!mWindow || mWindow->isClosing())
        return result;
    result.layers = mLayers->snapshot();
    result.results = mResults->snapshot();
    populateExtensionModelViewState(mWindow, result);
    return result;
}
void ExtensionViewAccess::refresh()
{
    if (!mInitializing && mRegistry && mWindow && !mWindow->isClosing())
        mRegistry->refreshViews(mWindow->extensionSessionId(), capture());
}
void ExtensionViewAccess::record(ViewEvent event)
{
    if (mInitializing || !mRegistry || !mWindow || mWindow->isClosing())
        return;
    event.origin = mOrigin;
    mRegistry->publishView(mWindow->extensionSessionId(), capture(), std::move(event));
}
bool ExtensionViewAccess::execute(const ViewCommand &command, QString *error)
{
    if (error)
        error->clear();
    const auto refuse = [error](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (!mWindow || mWindow->isClosing() || !mRegistry ||
        command.sessionId != mWindow->extensionSessionId())
        return refuse(QStringLiteral("The model session is unavailable."));
    if (mRegistry->session(command.sessionId).status != SessionStatus::Ready ||
        mRegistry->session(command.sessionId).loadGeneration != command.loadGeneration ||
        mRegistry->isSaving(command.sessionId))
        return refuse(QStringLiteral(
            "The model generation must be current, ready and outside a save operation."));
    auto *layer = mLayers->resolve(command.layerSessionId);
    if (!layer)
        return refuse(QStringLiteral("The layer attachment is no longer available."));
    const QPointer<ExtensionViewAccess> guard(this);
    const auto previousOrigin = mOrigin;
    mOrigin = command.origin;
    const auto restoreOrigin = qScopeGuard([guard, previousOrigin] {
        if (guard)
            guard->mOrigin = previousOrigin;
    });
    const bool result = command.kind == ViewCommandKind::ActivateResults ||
                                command.kind == ViewCommandKind::SetResultsTime ||
                                command.kind == ViewCommandKind::SetResultsVariable
                            ? mResults->execute(command, layer, error)
                            : mLayers->execute(command, error);
    if (!guard || !guard->mWindow || guard->mWindow->isClosing())
        return refuse(QStringLiteral("The model closed while applying the view command."));
    const auto current =
        guard->mRegistry ? guard->mRegistry->session(command.sessionId) : ModelSessionState{};
    if (current.status != SessionStatus::Ready ||
        current.loadGeneration != command.loadGeneration ||
        (command.kind != ViewCommandKind::RemoveLayer &&
         guard->mLayers->resolve(command.layerSessionId) != layer))
        return refuse(QStringLiteral(
            "The model reloaded or layer detached while applying the view command."));
    if (result)
        guard->refresh();
    return result;
}
QList<qint64> ExtensionViewAccess::selectedFeatures(const QUuid &id, int offset, int limit) const
{
    return mWindow && !mWindow->isClosing() ? mLayers->selectedFeatures(id, offset, limit)
                                            : QList<qint64>{};
}
}
