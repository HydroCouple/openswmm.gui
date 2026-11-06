/**
 * @file extensionsharedribbon.cpp
 * @brief Validated owner-scoped groups sharing one custom ribbon tab.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionsharedribbon.h"
#include "ui/toolbars/compacttoolbarcontroller.h"
#include "ui/toolbars/ribboncompactor.h"
#include "ui/toolbars/ribbongroup.h"
#include <QMainWindow>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QToolBar>

namespace openswmmvis
{
using namespace OpenSWMM::Sdk;
ExtensionSharedRibbon::ExtensionSharedRibbon(QMainWindow *window,
                                             ui::CompactToolbarController *controller)
    : QObject(window), mWindow(window), mController(controller)
{
    setObjectName("extensionSharedRibbon");
}
ExtensionSharedRibbon::~ExtensionSharedRibbon()
{
    mStopping = true;
    const auto contributions = mContributions.values();
    for (const auto &contribution : contributions)
        remove(contribution.ownerId, contribution.localId);
}
bool ExtensionSharedRibbon::add(const QString &ownerId, const SharedRibbonTabSpec &spec,
                                 const QHash<QString, QPointer<QAction>> &actions, QString *error)
{
    if (error)
        error->clear();
    auto refuse = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (QThread::currentThread() != thread() || mStopping || !mWindow || !mController)
        return refuse(tr("Shared ribbon tabs are unavailable."));
    static const QRegularExpression identity("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$");
    if (ownerId.isEmpty() || ownerId.contains('/') || !identity.match(spec.id).hasMatch() ||
        !identity.match(spec.tabId).hasMatch() || spec.title.trimmed().isEmpty())
        return refuse(tr("A shared ribbon contribution requires stable IDs and a tab caption."));
    const QString key = ownerId + '/' + spec.id;
    const QString tabId = "shared." + spec.tabId;
    if (mContributions.contains(key))
        return refuse(tr("This shared ribbon contribution is already registered."));
    if (mTabs.contains(tabId) && mTabs.value(tabId).title != spec.title)
        return refuse(tr("The shared tab '%1' already uses the caption '%2'.")
                          .arg(spec.tabId, mTabs.value(tabId).title));
    if (!mTabs.contains(tabId) && mController->tabIds().contains(tabId))
        return refuse(tr("That ribbon tab is owned by another UI registration."));
    if (spec.groups.isEmpty())
        return refuse(tr("A shared ribbon contribution requires at least one command group."));
    for (const auto &group : spec.groups) {
        if (group.caption.trimmed().isEmpty() || group.actionIds.isEmpty())
            return refuse(tr("Each shared ribbon group requires a caption and commands."));
        for (const auto &id : group.actionIds)
            if (!actions.value(id) || actions.value(id)->thread() != thread())
                return refuse(tr("Register each shared ribbon command before using it in a group."));
    }
    Contribution contribution;
    contribution.ownerId = ownerId;
    contribution.localId = spec.id;
    contribution.tabId = tabId;
    contribution.registrationId = QUuid::createUuid();
    contribution.assembling = true;
    const auto token = contribution.registrationId;
    mContributions.insert(key, contribution);
    if (!mTabs.contains(tabId))
        mTabs.insert(tabId, {spec.title, {}});
    mTabs[tabId].contributions.insert(key);

    const QPointer<ExtensionSharedRibbon> owner(this);
    const auto window = mWindow;
    const auto controller = mController;
    const auto registered = [&] {
        return owner && window && controller && !owner->mStopping &&
               owner->mWindow == window && owner->mController == controller &&
               owner->mContributions.contains(key) &&
               owner->mContributions.value(key).registrationId == token;
    };
    QPointer<QToolBar> bar(new QToolBar(spec.title, window));
    const auto discard = [&] {
        if (registered())
            owner->remove(ownerId, spec.id);
        delete bar.data();
        return false;
    };
    if (!registered() || !bar)
        return discard();
    mContributions[key].toolbar = bar;
    bar->setObjectName("ext." + ownerId + ".shared." + spec.id);
    if (!registered() || !bar)
        return discard();
    // A contribution's raw action references must retire before the plugin's action
    // is destroyed. Host unregistration also removes references while actions live.
    QSet<QAction *> watched;
    for (const auto &group : spec.groups) {
        for (const auto &id : group.actionIds) {
            const auto action = actions.value(id);
            if (!action || !registered() || !bar)
                return discard();
            if (watched.contains(action))
                continue;
            watched.insert(action);
            mContributions[key].connections.append(connect(action, &QObject::destroyed, this,
                [owner, ownerId, id = spec.id, key, token] {
                    if (owner && owner->mContributions.value(key).registrationId == token)
                        owner->remove(ownerId, id);
                }));
        }
    }
    QList<ui::RibbonGroup *> groups;
    for (const auto &specGroup : spec.groups) {
        QPointer<ui::RibbonGroup> group(new ui::RibbonGroup(specGroup.caption, bar));
        if (!registered() || !bar || !group)
            return discard();
        for (const auto &id : specGroup.actionIds) {
            auto action = actions.value(id);
            if (!action)
                return discard();
            group->addAction(action);
            if (!registered() || !bar || !group || !action)
                return discard();
        }
        bar->addWidget(group);
        if (!registered() || !bar || !group)
            return discard();
        groups.append(group);
    }
    new ui::RibbonCompactor(bar, groups);
    if (!registered() || !bar)
        return discard();
    if (!controller->tabIds().contains(tabId)) {
        controller->addTab(tabId, spec.title, {}, false);
        if (!registered() || !bar || !controller->tabIds().contains(tabId))
            return discard();
    }
    if (!controller->addToolbarToTab(tabId, bar) || !registered() || !bar)
        return discard();
    // The controller drops a destroyed toolbar from its borrowed pointer list
    // synchronously. Defer registry retirement until that cleanup has returned.
    mContributions[key].connections.append(connect(bar, &QObject::destroyed, this,
        [owner, ownerId, id = spec.id, key, token] {
            if (owner)
                QTimer::singleShot(0, owner, [owner, ownerId, id, key, token] {
                    if (owner && owner->mContributions.value(key).registrationId == token)
                        owner->remove(ownerId, id);
                });
        }));
    mContributions[key].assembling = false;
    return true;
}
void ExtensionSharedRibbon::remove(const QString &ownerId, const QString &localId)
{
    if (QThread::currentThread() != thread())
        return;
    const QString key = ownerId + '/' + localId;
    if (!mContributions.contains(key))
        return;
    const auto contribution = mContributions.take(key);
    for (const auto &connection : contribution.connections)
        disconnect(connection);
    auto tab = mTabs.find(contribution.tabId);
    if (tab != mTabs.end())
        tab->contributions.remove(key);
    const QPointer<ExtensionSharedRibbon> owner(this);
    const auto controller = mController;
    const auto bar = contribution.toolbar;
    if (controller && bar)
        controller->removeToolbarFromTab(contribution.tabId, bar);
    if (!owner) {
        if (bar) {
            if (contribution.assembling)
                bar->deleteLater();
            else
                delete bar.data();
        }
        return;
    }
    tab = mTabs.find(contribution.tabId);
    if (tab != mTabs.end() && tab->contributions.isEmpty()) {
        mTabs.erase(tab);
        if (controller)
            controller->removeTab(contribution.tabId);
    }
    if (bar) {
        if (contribution.assembling)
            bar->deleteLater();
        else
            delete bar.data();
    }
}
void ExtensionSharedRibbon::removeOwner(const QString &ownerId)
{
    if (QThread::currentThread() != thread())
        return;
    const auto contributions = mContributions.values();
    const QPointer<ExtensionSharedRibbon> guard(this);
    for (const auto &contribution : contributions) {
        if (!guard)
            return;
        if (contribution.ownerId == ownerId)
            remove(ownerId, contribution.localId);
    }
}
}
