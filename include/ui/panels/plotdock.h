#pragma once

#include <QDockWidget>
#include <QHash>
#include <QPointer>

class QTabWidget;
class QStackedWidget;

namespace openswmmvis::ui {
class PlotPanelView;

class PlotDock : public QDockWidget
{
    Q_OBJECT
public:
    PlotDock(const QString &title, const QString &name, QWidget *parent = nullptr);
    ~PlotDock() override;
    void addView(PlotPanelView *view, const QString &title, QObject *context = nullptr);
    void reveal(PlotPanelView *view);
    void setViewTitle(PlotPanelView *view, const QString &title);
    QSize sizeHint() const override { return QSize(900, 360); }
    void setActiveContext(QObject *context);
    void closeView(PlotPanelView *view);
    void saveLayouts();
    QTabWidget *tabs() const { return m_tabs; }
    QObject *contextFor(PlotPanelView *view) const;

signals:
    void viewSelected(PlotPanelView *view);

private:
    void updateActivation();
    QTabWidget *m_tabs;
    QStackedWidget *m_stack;
    QHash<PlotPanelView *, QPointer<QObject>> m_contexts;
    QPointer<QObject> m_activeContext;
    bool m_exposed = false;
};

}
