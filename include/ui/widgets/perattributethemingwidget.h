/**
 * @file      perattributethemingwidget.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BA — status-bar theming widget that picks which result
 *         variable drives the SWMMResultsLayer colour fill.
 *
 * Mirrors the legacy SWMM-GUI's per-attribute combo: one combo box per
 * object kind (Node / Link / Subcatchment) plus a "follow primary"
 * toggle so multi-layer projects can sync on the primary layer's
 * variable. Pushed into the main-window status bar as one composite
 * widget so AnimationController and SWMMResultsLayer stay decoupled
 * from the chrome that drives them.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_PERATTRIBUTETHEMINGWIDGET_H
#define OPENSWMMVIS_UI_WIDGETS_PERATTRIBUTETHEMINGWIDGET_H

#include "layers/swmmresultslayer.h"

#include <QPointer>
#include <QWidget>

class QComboBox;
class QLabel;
class AnimationController;

namespace openswmmvis::ui
{

class PerAttributeThemingWidget : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create node/link/subcatchment variable pickers without a bound results layer.
     * @param[in] parent Optional QWidget owner; edits apply live on the GUI thread.
     */
    explicit PerAttributeThemingWidget(QWidget *parent = nullptr);

    /*! \brief Bind to the animation controller; subscribes to
     *  `primaryLayerChanged` so the combos always reflect the active
     *  result-layer's current variable.
     * @param[in] ac Borrowed controller tracked by QPointer; null disconnects prior controller notifications.
     * @note Does not immediately fetch the controller's existing primary layer or clear the currently bound layer; future primaryLayerChanged events drive rebinding.
     */
    void setAnimationController(AnimationController *ac);

    /*! \brief Direct binding (bypasses the AC) used in test fixtures.
     * @param[in] layer Borrowed results layer tracked by QPointer; null disconnects and clears binding without resetting displayed combo values. Identical pointers are ignored.
     * @note Replaces variable-change subscriptions and updates the matching combo for a non-null layer.
     */
    void setPrimaryLayer(SWMMResultsLayer *layer);

private slots:
    /**
     * @brief Set the bound layer's active variable from the node combo.
     * @param[in] index Current combo row; negative, suppressed or unbound requests are ignored. No undo command is created here.
     */
    void onNodeVarChanged(int index);
    /**
     * @brief Set the bound layer's active variable from the link combo.
     * @param[in] index Current combo row; negative, suppressed or unbound requests are ignored. No undo command is created here.
     */
    void onLinkVarChanged(int index);
    /**
     * @brief Set the bound layer's active variable from the sub combo.
     * @param[in] index Current combo row; negative, suppressed or unbound requests are ignored. No undo command is created here.
     */
    void onSubVarChanged(int index);
    /**
     * @brief Rebind to the controller's announced primary results layer.
     * @param[in] layer Borrowed new layer, including null.
     */
    void onPrimaryLayerChanged(SWMMResultsLayer *layer);
    /**
     * @brief Refresh the matching combo from the currently bound layer's live variable.
     * @param[in] var Notification payload, unused because the layer is read directly.
     */
    void onLayerVariableChanged(SWMMResultVariable var);

private:
    /**
     * @brief Create three owned result-kind pickers and connect live variable edits.
     */
    void buildUi();
    /**
     * @brief Refill the fixed node/link/subcatchment variable catalogs under edit suppression.
     * @note Does not query result-file availability or change a bound layer.
     */
    void populateCombos();
    /**
     * @brief Select the matching row in any combo containing the layer's one active variable.
     * @note Unmatched combos retain their prior selection; suppression prevents feedback writes. Missing layer is a no-op.
     */
    void pushLayerVariableToCombos();

    QComboBox *m_nodeCombo = nullptr;
    QComboBox *m_linkCombo = nullptr;
    QComboBox *m_subCombo = nullptr;
    QLabel *m_layerLabel = nullptr;

    QPointer<AnimationController> m_ac;
    QPointer<SWMMResultsLayer> m_layer;
    bool m_suppress = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_PERATTRIBUTETHEMINGWIDGET_H
