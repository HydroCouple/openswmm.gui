/**
 * @file      plotdock.cpp
 * @brief     Dock host and lifecycle management for plot panels.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/panels/plotdock.h"
#include "ui/panels/plotpanelview.h"

#include <QTabWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QTabBar>

namespace openswmmvis::ui
{

PlotDock::PlotDock(const QString &title, const QString &name, QWidget *parent)
    : QDockWidget(title, parent), m_tabs(new QTabWidget(this))
{
    setObjectName(name);
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(DockWidgetClosable | DockWidgetMovable | DockWidgetFloatable);
    m_tabs->setObjectName(name + QStringLiteral("Tabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->tabBar()->setElideMode(Qt::ElideMiddle);
    m_stack = new QStackedWidget(this);
    auto *empty =
        new QLabel(tr("Open a plot from the Analysis menu or the map to add a tab."), m_stack);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    m_stack->addWidget(empty);
    m_stack->addWidget(m_tabs);
    setWidget(m_stack);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this,
            [this](int index) { closeView(qobject_cast<PlotPanelView *>(m_tabs->widget(index))); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        updateActivation();
        emit viewSelected(qobject_cast<PlotPanelView *>(m_tabs->currentWidget()));
    });
    // Clicking an already-selected tab still selects its project after the
    // user has switched documents in the central workspace.
    connect(m_tabs, &QTabWidget::tabBarClicked, this, [this](int index) {
        emit viewSelected(qobject_cast<PlotPanelView *>(m_tabs->widget(index)));
        updateActivation();
    });
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        m_exposed = visible;
        updateActivation();
    });
}

PlotDock::~PlotDock()
{
    // Child destruction happens after our member fields are gone.
    disconnect(m_tabs, nullptr, this, nullptr);
    for (auto *view : m_contexts.keys())
        disconnect(view, nullptr, this, nullptr);
}

void PlotDock::addView(PlotPanelView *view, const QString &title, QObject *context)
{
    if (!view || m_tabs->indexOf(view) >= 0)
        return;
    m_contexts.insert(view, context);
    const QPointer<QWidget> owner = qobject_cast<QWidget *>(context);
    auto refreshTitle = [this, view, owner] {
        setViewTitle(view, owner ? tr("%1 — %2").arg(owner->windowTitle(), view->windowTitle())
                                 : view->windowTitle());
    };
    connect(view, &QWidget::windowTitleChanged, this, refreshTitle);
    if (owner)
        connect(owner, &QWidget::windowTitleChanged, view, refreshTitle);
    connect(view, &PlotPanelView::closed, this, [this, view] {
        m_contexts.remove(view);
        m_tabs->removeTab(m_tabs->indexOf(view));
        view->deleteLater();
    });
    connect(view, &QObject::destroyed, this, [this, view] { m_contexts.remove(view); });
    m_tabs->addTab(view, title);
    setViewTitle(view, title);
    reveal(view);
}

void PlotDock::setViewTitle(PlotPanelView *view, const QString &title)
{
    const int index = m_tabs->indexOf(view);
    if (index < 0)
        return;
    QString label = title;
    int suffix = 2;
    auto used = [&] {
        for (int i = 0; i < m_tabs->count(); ++i)
            if (i != index && m_tabs->tabText(i) == label)
                return true;
        return false;
    };
    while (used())
        label = tr("%1 (%2)").arg(title).arg(suffix++);
    m_tabs->setTabText(index, label);
    m_tabs->setTabToolTip(index, title);
}

void PlotDock::reveal(PlotPanelView *view)
{
    if (!view || m_tabs->indexOf(view) < 0)
        return;
    m_tabs->setCurrentWidget(view);
    show();
    raise();
    emit viewSelected(view);
    updateActivation();
    view->setFocus(Qt::OtherFocusReason);
}

void PlotDock::setActiveContext(QObject *context)
{
    m_activeContext = context;
    updateActivation();
}

QObject *PlotDock::contextFor(PlotPanelView *view) const
{
    return m_contexts.value(view).data();
}

void PlotDock::updateActivation()
{
    m_stack->setCurrentIndex(m_tabs->count() ? 1 : 0);
    // Deactivate the old view before installing another view's map overlay.
    auto *current = qobject_cast<PlotPanelView *>(m_tabs->currentWidget());
    for (auto *view : m_contexts.keys())
        if (view != current)
            view->setPlotActive(false);
    if (current)
        current->setPlotActive(m_exposed &&
                               (!contextFor(current) || contextFor(current) == m_activeContext));
}

void PlotDock::closeView(PlotPanelView *view)
{
    if (view && m_tabs->indexOf(view) >= 0)
        view->close();
}

void PlotDock::saveLayouts()
{
    for (auto *view : m_contexts.keys())
        view->saveLayout();
}

}
