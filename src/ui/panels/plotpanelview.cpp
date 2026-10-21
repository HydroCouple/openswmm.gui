#include "ui/panels/plotpanelview.h"
#include "ui/dialogs/dialoglayoutpersistence.h"

#include <QCloseEvent>
#include <QHideEvent>
#include <QShowEvent>

namespace openswmmvis::ui {

PlotPanelView::PlotPanelView(QWidget *parent) : QWidget(parent) {}

void PlotPanelView::setPlotActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    plotActivationChanged(active);
    emit plotActiveChanged(active);
}

void PlotPanelView::saveLayout()
{
    if (m_restored) saveDialogLayout(this, false);
}

void PlotPanelView::showEvent(QShowEvent *event)
{
    if (!m_restored) {
        m_restored = true;
        restoreDialogLayout(this, false);
    }
    QWidget::showEvent(event);
    // Standalone use (including widget tests) has no dock managing activation.
    if (isWindow()) setPlotActive(true);
}

void PlotPanelView::hideEvent(QHideEvent *event)
{
    setPlotActive(false);
    saveLayout();
    QWidget::hideEvent(event);
}

void PlotPanelView::closeEvent(QCloseEvent *event)
{
    setPlotActive(false);
    saveLayout();
    event->accept();
    emit closed();
}

}
