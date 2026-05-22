/*!
 * \file   perattributethemingwidget.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BA — status-bar theming widget that picks which result
 *         variable drives the SWMMResultsLayer colour fill.
 *
 * Mirrors the legacy SWMM-GUI's per-attribute combo: one combo box per
 * object kind (Node / Link / Subcatchment) plus a "follow primary"
 * toggle so multi-layer projects can sync on the primary layer's
 * variable. Pushed into the main-window status bar as one composite
 * widget so AnimationController and SWMMResultsLayer stay decoupled
 * from the chrome that drives them.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_PERATTRIBUTETHEMINGWIDGET_H
#define OPENSWMMVIS_UI_WIDGETS_PERATTRIBUTETHEMINGWIDGET_H

#include "layers/swmmresultslayer.h"

#include <QPointer>
#include <QWidget>

class QComboBox;
class QLabel;
class AnimationController;

namespace openswmmvis::ui {

class PerAttributeThemingWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PerAttributeThemingWidget(QWidget *parent = nullptr);

    /*! \brief Bind to the animation controller; subscribes to
     *  `primaryLayerChanged` so the combos always reflect the active
     *  result-layer's current variable. */
    void setAnimationController(AnimationController *ac);

    /*! \brief Direct binding (bypasses the AC) used in test fixtures. */
    void setPrimaryLayer(SWMMResultsLayer *layer);

private slots:
    void onNodeVarChanged(int index);
    void onLinkVarChanged(int index);
    void onSubVarChanged(int index);
    void onPrimaryLayerChanged(SWMMResultsLayer *layer);
    void onLayerVariableChanged(SWMMResultVariable var);

private:
    void buildUi();
    void populateCombos();
    void pushLayerVariableToCombos();

    QComboBox *m_nodeCombo = nullptr;
    QComboBox *m_linkCombo = nullptr;
    QComboBox *m_subCombo  = nullptr;
    QLabel    *m_layerLabel = nullptr;

    QPointer<AnimationController> m_ac;
    QPointer<SWMMResultsLayer>    m_layer;
    bool                          m_suppress = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_PERATTRIBUTETHEMINGWIDGET_H
