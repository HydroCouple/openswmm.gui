/**
 * @file      plotdock.h
 * @brief     Dock host and lifecycle management for plot panels.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QDockWidget>
#include <QHash>
#include <QPointer>

class QTabWidget;
class QStackedWidget;

namespace openswmmvis::ui
{
class PlotPanelView;

class PlotDock : public QDockWidget
{
    Q_OBJECT
public:
    /**
     * @param[in] title Copied dock title.
     * @param[in] name Stable objectName, also prefixes tab widget identity; caller supplies
     *   persistence-compatible unique name.
     * @param[in] parent Optional QWidget owner, caller owns parentless dock.
     * @brief Create owned movable/closable tab and empty-placeholder stack, wire
     *   visibility/current-tab/context activation and close routing.
     * @note GUI-thread only. Tabbed PlotPanelViews reparented/owned by tabs, contexts
     *   guarded/nonowning. No context binding or plot added initially; dock m_exposed follows
     *   visibilityChanged rather than immediate isVisible query.
     */
    PlotDock(const QString &title, const QString &name, QWidget *parent = nullptr);
    /**
     * @brief Disconnect tab/current registered-view callbacks before member hashes die; QWidget
     *   hierarchy later destroys adopted views. Context objects remain externally owned. No
     *   automatic saveLayouts or closed emission here.
     */
    ~PlotDock() override;
    /**
     * @param[in] view Optional PlotPanelView adopted by QTabWidget ownership; null/already tabbed
     *   here ignored.
     * @param[in] title Initial copied visible label/tooltip, collision suffix added.
     * @param[in] context Optional borrowed guarded project/context, used for activation; QWidget
     *   context also supplies title prefix on later title updates.
     * @note Register weak context, hook view/window titles, closed→remove tab/context+deleteLater
     *   and destroyed→context removal. Insert tab then reveal (show/raise/focus/select). Initial
     *   title passed verbatim, context/window title combination recomputed only on subsequent title
     *   signals. Does not close views for destroyed context or verify view not hosted elsewhere.
     */
    void addView(PlotPanelView *view, const QString &title, QObject *context = nullptr);
    /**
     * @param[in] view Optional currently tabbed view, null/foreign ignored.
     * @note Select tab, show/raise dock, emit viewSelected even if already current, update
     *   activation, focus view. Current-tab change can emit an additional viewSelected. Does not
     *   set active project context itself; receiver handles project activation.
     */
    void reveal(PlotPanelView *view);
    /**
     * @param[in] view Optional tabbed view identity; not found no-op.
     * @param[in] title Raw label base/tooltip; empty allowed.
     * @note Choose first unique case-sensitive label, appending (2), (3)... against other current
     *   tabs. Tooltip stays original title, view windowTitle unchanged. No persistence, context
     *   change or sort.
     */
    void setViewTitle(PlotPanelView *view, const QString &title);
    /**
     * @return Preferred 900×360 logical pixel dock size, not minimum or restored layout.
     */
    QSize sizeHint() const override { return QSize(900, 360); }
    /**
     * @param[in] context Optional borrowed QObject guarded by QPointer; null permits context-free
     *   plots only.
     * @note Always stores and recomputes activation, no equality guard. Does not switch selected
     *   tab or own context. Destruction nullifies pointer without automatic activation refresh
     *   until another trigger.
     */
    void setActiveContext(QObject *context);
    /**
     * @param[in] view Optional currently tabbed view, otherwise no-op.
     * @note Calls QWidget close; PlotPanelView base accepts/saves/deactivates/emits closed, dock
     *   closed hook removes tab and queues deletion. Derived close override can veto or omit
     *   signal; no forced deletion here.
     */
    void closeView(PlotPanelView *view);
    /**
     * @brief Invoke saveLayout on every currently registered view, including hidden/inactive tabs.
     * @note View handles never-restored suppression; writes user layout settings, no plot-data save
     *   or context/model mutation.
     */
    void saveLayouts();
    /**
     * @return Borrowed dock-owned live tab widget. External removal/insertion can bypass
     *   context/close hooks, so use addView/closeView for managed lifecycle.
     */
    QTabWidget *tabs() const { return m_tabs; }
    /**
     * @param[in] view Borrowed pointer identity, need not be live to look up hash key.
     * @return Borrowed guarded context, null for unknown/context-free/destroyed. No
     *   ownership/lifetime pin.
     */
    QObject *contextFor(PlotPanelView *view) const;

signals:
    /**
     * @param[in] view Borrowed selected/clicked/revealed plot, null possible on no/current invalid
     *   tab.
     * @note Emitted on currentChanged, tabBarClicked(even same tab), and reveal explicitly. Does
     *   not guarantee plot active or matching project until receiver updates active context; order
     *   differs by route.
     */
    void viewSelected(PlotPanelView *view);

private:
    /**
     * @brief Show tabs or empty placeholder, deactivate every registered noncurrent plot BEFORE
     *   activating current one.
     * @note Current active only when m_exposed and context absent OR equals activeContext.
     *   Destroyed context counts absent, so later refresh can activate it globally. Does not
     *   hide/filter other-project tabs, switch document or save data; virtual activation hooks can
     *   affect overlays.
     */
    void updateActivation();
    QTabWidget *m_tabs;
    QStackedWidget *m_stack;
    QHash<PlotPanelView *, QPointer<QObject>> m_contexts;
    QPointer<QObject> m_activeContext;
    bool m_exposed = false;
};

}
