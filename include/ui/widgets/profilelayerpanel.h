/**
 * @file      profilelayerpanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Right-side toggle panel for Slice BC's profile plot.
 *
 *         Pure UI surface — owns the layer-visibility checkboxes
 *         (Current HGL / Current EGL / Max HGL) plus the Max-HGL
 *         render-mode radios (Min↔max band vs Invert→max fill) and
 *         emits a single `togglesChanged` signal when any value
 *         changes.  The hosting `ProfilePlotDialog` connects that
 *         signal to `ProfilePlotWidget::setLayerToggles()`.  Max-EGL
 *         exposes only a line (no fill) — surface that via the styles
 *         tree in ProfileOptionsDialog, not here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILE_LAYER_PANEL_H
#define PROFILE_LAYER_PANEL_H

#include "plot/profileplotwidget.h"

#include <QWidget>

class QCheckBox;
class QRadioButton;
class QButtonGroup;
class QSpinBox;

class ProfileLayerPanel : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Build profile visibility/label/terrain controls and connect their changes to aggregate toggle notifications.
     * @param[in] parent Optional QWidget owner; no plot/model object is owned or modified directly.
     */
    explicit ProfileLayerPanel(QWidget *parent = nullptr);

    /**
     * @brief Collect current HGL/EGL visibility, label orientation and terrain-ground selections.
     * @return LayerToggles value; diagonal labelAngleDeg comes from the 1–89° spin box. Current Max-HGL mode radio selection is not read into this value.
     */
    [[nodiscard]] ProfilePlotWidget::LayerToggles toggles() const;
    /**
     * @brief Hydrate profile controls from supplied values and emit the supplied aggregate toggles.
     * @param[in] t Input values; displayed label angle is clamped to 1–89° but emitted payload remains t unchanged.
     * @note Blocks individual checkbox/group/spin signals. Does not update Max-HGL mode radios or recompute all enable-state gates. Emits togglesChanged() even if inputs are unchanged.
     */
    void setToggles(const ProfilePlotWidget::LayerToggles &t);

signals:
    /**
     * @brief Forward aggregate profile toggle state to the host plot.
     * @param[in] t Supplied setter payload or current controls collected after a user change.
     */
    void togglesChanged(const ProfilePlotWidget::LayerToggles &t);

private slots:
    /**
     * @brief Emit togglesChanged() using the current controls collected by toggles().
     */
    void emitTogglesChanged();

private:
    QCheckBox *m_currentHglLine = nullptr;
    QCheckBox *m_currentHglFill = nullptr;
    QCheckBox *m_currentEgl = nullptr;
    QCheckBox *m_maxHgl = nullptr;
    QRadioButton *m_hglEnvelope = nullptr;
    QRadioButton *m_hglInvertMax = nullptr;
    QButtonGroup *m_hglGroup = nullptr;
    QCheckBox *m_nodeLabels = nullptr;
    QCheckBox *m_linkLabels = nullptr;
    QCheckBox *m_inlineLabels = nullptr;
    QCheckBox *m_useTerrain = nullptr;
    QRadioButton *m_orientVertical = nullptr;
    QRadioButton *m_orientDiagonal = nullptr;
    QRadioButton *m_orientHorizontal = nullptr;
    QButtonGroup *m_orientGroup = nullptr;
    QSpinBox *m_angleSpin = nullptr;
};

#endif // PROFILE_LAYER_PANEL_H
