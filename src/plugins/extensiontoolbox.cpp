/** @file extensiontoolbox.cpp
 * @brief Validated registration and lazy panel lifecycle for plugin tools.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensiontoolbox.h"
#include "plugins/guiextensionhost.h"
#include "ui/panels/toolboxpanel.h"
#include "ui/panels/toolrunspanel.h"
#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QRegularExpression>
#include <QScrollArea>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <exception>

namespace openswmmvis
{
using namespace OpenSWMM::Sdk;
namespace
{
/** @brief Offer native dock placement without requiring a precise drag gesture.
 * @param[in] dock Tool dock owning the menu actions and their connections.
 * @param[in] window Main window receiving the existing dock, guarded across native events.
 * @details The standard title bar remains available for dragging and double-clicking.
 * Actions use normal main-window placement, so saved layouts and manual tab groups
 * remain authoritative until the user explicitly chooses a new position. */
void addDockPlacementActions(QPointer<QDockWidget> dock, QPointer<QMainWindow> window)
{
    const auto addPlacement = [&](const QString &name, const QString &text,
                                  Qt::DockWidgetArea area) {
        if (!dock)
            return;
        QPointer<QAction> action(new QAction(text, dock));
        if (!dock || !action)
            return;
        action->setObjectName(name);
        dock->addAction(action);
        QObject::connect(action, &QAction::triggered, dock, [dock, window, area] {
            if (!dock || !window)
                return;
            dock->setFloating(false);
            if (!dock || !window)
                return;
            window->addDockWidget(area, dock);
            if (!dock || !window)
                return;
            dock->show();
            if (dock)
                dock->raise();
        });
    };
    addPlacement("toolDockLeft", QObject::tr("Dock left"), Qt::LeftDockWidgetArea);
    addPlacement("toolDockRight", QObject::tr("Dock right"), Qt::RightDockWidgetArea);
    addPlacement("toolDockTop", QObject::tr("Dock top"), Qt::TopDockWidgetArea);
    addPlacement("toolDockBottom", QObject::tr("Dock bottom"), Qt::BottomDockWidgetArea);
    if (!dock)
        return;
    QPointer<QAction> floating(new QAction(QObject::tr("Float panel"), dock));
    if (!dock || !floating)
        return;
    floating->setObjectName("toolDockFloating");
    floating->setCheckable(true);
    floating->setChecked(dock->isFloating());
    dock->addAction(floating);
    QObject::connect(floating, &QAction::triggered, dock, [dock](bool checked) {
        if (dock)
            dock->setFloating(checked);
    });
    QObject::connect(dock, &QDockWidget::topLevelChanged, floating, &QAction::setChecked);
}
}
ExtensionToolbox::ExtensionToolbox(GuiExtensionHost *host, QMainWindow *window, QSettings *settings)
    : QObject(host), mHost(host), mWindow(window), mSettings(settings)
{
    mPanel = window ? window->findChild<ui::ToolboxPanel *>("analysisToolboxPanel") : nullptr;
    if (mPanel)
        mPanel->setHistoryWidget(createToolRunsPanel(window, {}, {}, mPanel));
}
ExtensionToolbox::~ExtensionToolbox() { stop(); }
QString ExtensionToolbox::key(const QString &id) const
{
    return "extensions/" + mHost->pluginId() + "/toolbox/" + id + "/open";
}
void ExtensionToolbox::persist(const QString &id, bool open)
{
    if (mSettings)
        mSettings->setValue(key(id), open);
}
bool ExtensionToolbox::registerTool(const ToolSpec &input, QObject *context,
                                    std::function<QWidget *(ExtensionHost *)> create, QString *error)
{
    if (error)
        error->clear();
    auto refuse = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (QThread::currentThread() != thread() || mStopping || !mWindow ||
        !context || context->thread() != thread() || !create)
        return refuse(tr("The tool requires a live GUI-thread owner and panel factory."));
    static const QRegularExpression identifier("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$");
    static const QRegularExpression tagPattern("^[a-z][a-z0-9]*(-[a-z0-9]+)*$");
    if (!identifier.match(input.id).hasMatch() || mTools.contains(input.id))
        return refuse(tr("The tool identity is invalid or already registered."));
    auto *action = mHost->registeredToolAction(input.actionId);
    if (!action)
        return refuse(tr("Register the tool's launch action before registering its tool."));
    for (const auto &tool : std::as_const(mTools))
        if (tool.action == action)
            return refuse(tr("Each tool requires its own launch action."));
    ToolSpec spec = input;
    spec.tags.removeDuplicates();
    if (spec.tags.isEmpty() || spec.tags.size() > 64)
        return refuse(tr("Each tool requires at least one tag (up to 64)."));
    for (const auto &tag : spec.tags)
        if (tag.size() > 64 || !tagPattern.match(tag).hasMatch())
            return refuse(tr("Use lowercase words joined with hyphens for tool tags."));
    if (spec.toolsets.size() > 8)
        return refuse(tr("Toolset nesting is limited to eight folders."));
    for (const auto &folder : spec.toolsets)
        if (!identifier.match(folder.id).hasMatch() || folder.title.trimmed().isEmpty())
            return refuse(tr("Toolset folders require a stable identity and caption."));
    if (!mPanel) {
        mPanel = ui::ensureToolboxPanel(mWindow);
        if (!mPanel)
            return refuse(tr("The Analysis Toolbox is unavailable."));
        mPanel->setHistoryWidget(createToolRunsPanel(mWindow, {}, {}, mPanel));
    }
    Tool tool;
    tool.spec = spec;
    tool.action = action;
    tool.context = context;
    tool.create = std::move(create);
    const auto id = spec.id;
    tool.connections.append(connect(action, &QAction::triggered, this, [this, id] {
        QString error;
        if (!openTool(id, &error) && !error.isEmpty())
            mHost->console()->append("warning", error);
    }));
    tool.connections.append(connect(action, &QObject::destroyed, this,
                                    [this, id] { unregisterTool(id); }));
    tool.connections.append(connect(context, &QObject::destroyed, this,
                                    [this, id] { unregisterTool(id); }));
    mTools.insert(id, tool);
    const QPointer<ExtensionToolbox> guard(this);
    const auto registration = tool.registrationId;
    mPanel->addTool(mHost->pluginId(), spec, action);
    if (!guard || mStopping || !mTools.contains(id) ||
        mTools.value(id).registrationId != registration)
        return false;
    if (mSettings && mSettings->value(key(id), false).toBool())
        QTimer::singleShot(0, this, [this, id] {
            if (!mStopping && mTools.contains(id))
                openTool(id, nullptr);
        });
    return true;
}
void ExtensionToolbox::unregisterTool(const QString &id)
{
    if (QThread::currentThread() != thread() || !mTools.contains(id))
        return;
    const auto tool = mTools.take(id);
    for (const auto &connection : tool.connections)
        disconnect(connection);
    if (mPanel)
        mPanel->removeTool(mHost->pluginId(), id);
    if (tool.dock) {
        mHost->trackToolDock("toolpanel." + id, nullptr);
        // A widget's ParentChange/Show handler may unregister while Qt is
        // still mounting the dock. The opener disposes it after Qt returns.
        if (tool.creating)
            tool.dock->deleteLater();
        else
            delete tool.dock;
    }
}
void ExtensionToolbox::removeAction(const QString &id)
{
    const auto ids = mTools.keys();
    for (const auto &toolId : ids)
        if (mTools.value(toolId).spec.actionId == id)
            unregisterTool(toolId);
}
bool ExtensionToolbox::openTool(const QString &inputId, QString *error)
{
    const QString id = inputId;
    if (error)
        error->clear();
    auto refuse = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (QThread::currentThread() != thread())
        return refuse(tr("The tool must be opened on the GUI thread."));
    auto found = mTools.find(id);
    if (mStopping || !mWindow ||
        found == mTools.end() || !found->context || found->context->thread() != thread() ||
        !found->action || !found->action->isVisible() || !found->action->isEnabled())
        return refuse(tr("The tool is unavailable or disabled."));
    if (found->creating)
        return refuse(tr("This tool panel is already being created."));
    QPointer<ExtensionToolbox> owner(this);
    QPointer<GuiExtensionHost> host(mHost);
    QPointer<QMainWindow> window(mWindow);
    const auto registration = found->registrationId;
    const auto registered = [&] {
        return owner && host && window && !owner->mStopping &&
               owner->mWindow == window && owner->mTools.contains(id) &&
               owner->mTools.value(id).registrationId == registration;
    };
    QPointer<QDockWidget> dock = found->dock;
    if (!dock) {
        found->creating = true;
        const auto create = found->create;
        const auto caption = found->action->text();
        QPointer<QObject> context = found->context;
        QPointer<QWidget> content;
        try {
            content = create(host);
        } catch (const std::exception &exception) {
            if (error)
                *error = tr("Could not create the tool panel. %1").arg(exception.what());
        } catch (...) {
            if (error)
                *error = tr("Could not create the tool panel.");
        }
        if (!registered() || !context || context->thread() != owner->thread()) {
            delete content;
            return false;
        }
        if (!content) {
            owner->mTools[id].creating = false;
            return refuse(tr("The tool's panel factory did not return a panel."));
        }
        dock = new QDockWidget(caption, window);
        const auto discard = [&] {
            if (registered() && owner->mTools.value(id).dock == dock) {
                owner->mTools[id].dock = nullptr;
                owner->mTools[id].creating = false;
                host->trackToolDock("toolpanel." + id, nullptr);
            }
            delete dock.data();
            delete content.data();
            return false;
        };
        if (!registered() || !dock)
            return discard();
        owner->mTools[id].dock = dock;
        const auto usable = [&] {
            return registered() && context && dock && content &&
                   owner->mTools.value(id).dock == dock;
        };
        dock->setObjectName("ext." + host->pluginId() + ".toolpanel." + id);
        if (!usable())
            return discard();
        dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                          QDockWidget::DockWidgetFloatable);
        if (!usable())
            return discard();
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        if (!usable())
            return discard();
        dock->setContextMenuPolicy(Qt::ActionsContextMenu);
        if (!usable())
            return discard();
        addDockPlacementActions(dock, window);
        if (!usable())
            return discard();
        QPointer<QScrollArea> scroll(new QScrollArea(dock));
        if (!usable() || !scroll)
            return discard();
        scroll->setObjectName("toolPanelScrollArea");
        scroll->setAccessibleName(tr("Tool panel contents"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        if (!usable() || !scroll)
            return discard();
        QPointer<QWidget> body(new QWidget(scroll));
        if (!usable() || !body)
            return discard();
        QPointer<QVBoxLayout> layout(new QVBoxLayout(body));
        if (!usable() || !body || !layout)
            return discard();
        layout->setContentsMargins(0, 0, 0, 0);
        // Update the fallback scroll range when a plugin or the run history
        // expands, without propagating their combined minimum to the window.
        layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
        if (!usable() || !body || !layout)
            return discard();
        layout->addWidget(content, 1);
        if (!usable() || !body || !layout)
            return discard();
        QPointer<QWidget> history(createToolRunsPanel(window, host->pluginId(), id, body));
        if (!usable() || !body || !layout || !history)
            return discard();
        layout->addWidget(history);
        if (!usable() || !body || !layout || !history)
            return discard();
        scroll->setWidget(body);
        if (!usable() || !body || !scroll)
            return discard();
        dock->setWidget(scroll);
        if (!usable() || !body || !scroll)
            return discard();
        window->addDockWidget(Qt::RightDockWidgetArea, dock);
        if (!usable())
            return discard();
        window->restoreDockWidget(dock);
        if (!usable())
            return discard();
        host->trackToolDock("toolpanel." + id, dock);
        if (!usable())
            return discard();
        connect(dock, &QDockWidget::visibilityChanged, owner,
                [owner, id, dock, registration](bool) {
            if (owner && !owner->mStopping && dock &&
                owner->mTools.value(id).registrationId == registration)
                owner->persist(id, !dock->isHidden());
        });
        connect(owner->mTools[id].action, &QAction::changed, dock,
                [owner, id, dock, registration] {
            if (!owner)
                return;
            const auto tool = owner->mTools.value(id);
            if (dock && tool.action && tool.registrationId == registration)
                dock->setWindowTitle(tool.action->text());
        });
    }
    dock->show();
    if (registered() && dock)
        dock->raise();
    if (!registered() || !dock)
        return false;
    owner->mTools[id].creating = false;
    owner->persist(id, true);
    return true;
}
void ExtensionToolbox::stop()
{
    if (mStopping)
        return;
    mStopping = true;
    for (auto it = mTools.cbegin(); it != mTools.cend(); ++it)
        if (it->dock)
            persist(it.key(), !it->dock->isHidden());
    const auto ids = mTools.keys();
    for (const auto &id : ids)
        unregisterTool(id);
}
}
