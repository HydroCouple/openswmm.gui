#pragma once

#include <QWidget>

namespace openswmmvis::ui {

// Shared lifecycle for plot content. The host owns docking and tab removal;
// this widget owns its internal layout and activation-dependent map tools.
class PlotPanelView : public QWidget
{
    Q_OBJECT
public:
    explicit PlotPanelView(QWidget *parent = nullptr);
    bool isPlotActive() const { return m_active; }
    void setPlotActive(bool active);
    void saveLayout();

signals:
    void closed();
    void plotActiveChanged(bool active);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    virtual void plotActivationChanged(bool active) { Q_UNUSED(active); }

private:
    bool m_active = false;
    bool m_restored = false;
};

}
