/**
 * @file      plotpanelview.h
 * @brief     Shared plot content lifecycle for dock and tab hosts.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QWidget>

namespace openswmmvis::ui
{

// Shared lifecycle for plot content. The host owns docking and tab removal;
// this widget owns its internal layout and activation-dependent map tools.
class PlotPanelView : public QWidget
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QWidget owner, host may adopt via tab insertion.
     * @brief Create inactive/unrestored plot content base, no data/model/map ownership. GUI-thread
     *   lifecycle, derived hooks own overlay setup.
     */
    explicit PlotPanelView(QWidget *parent = nullptr);
    /**
     * @return Stored explicit activation flag, independent of QWidget visible/current tab state
     *   unless host synchronizes.
     */
    bool isPlotActive() const { return m_active; }
    /**
     * @param[in] active Desired stored lifecycle state.
     * @note Equality no-op; stores state, invokes virtual plotActivationChanged, then emits
     *   plotActiveChanged. Does not show/hide widget itself or catch hook exceptions/reentry.
     */
    void setPlotActive(bool active);
    /**
     * @note After first show restoration, calls saveDialogLayout(this,false), persisting embedded
     *   layout policy without top-level geometry. Never shown/restored no-op. Does not save plot
     *   data/model or engine.
     */
    void saveLayout();

signals:
    /**
     * @note Base closeEvent accepts, deactivates and saves before emission. Signal asks host to
     *   remove/delete; does not itself guarantee destruction. Hide or destructor does not emit.
     */
    void closed();
    /**
     * @param[in] active New stored flag after virtual activation hook returns.
     * @note Only setPlotActive transitions emit; no promise widget visible or map overlay exists.
     */
    void plotActiveChanged(bool active);

protected:
    /**
     * @param[in] event Required borrowed matching Qt event; not retained.
     * @note First show marks restored and invokes restoreDialogLayout(this,false) once BEFORE
     *   QWidget base; standalone isWindow then activates. Embedded host handles activation.
     */
    void showEvent(QShowEvent *event) override;
    /**
     * @param[in] event Required borrowed matching Qt event; not retained.
     * @note Deactivate and save layout, then QWidget base. Does not close/remove tab or emit
     *   closed.
     */
    void hideEvent(QHideEvent *event) override;
    /**
     * @param[in] event Required borrowed matching Qt event; not retained.
     * @note Deactivate/save, accept event and emit closed; does NOT call QWidget base or delete
     *   self. Host decides removal/deferred delete.
     */
    void closeEvent(QCloseEvent *event) override;
    /**
     * @param[in] active New explicit state, ignored by base.
     * @brief Base no-op hook for derived map overlay/tool lifecycles, called synchronously before
     *   plotActiveChanged. Does not change widget visibility or persistence.
     */
    virtual void plotActivationChanged(bool active) { Q_UNUSED(active); }

private:
    bool m_active = false;
    bool m_restored = false;
};

}
