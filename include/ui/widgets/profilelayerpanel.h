/*!
 * \file   profilelayerpanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Right-side toggle panel for Slice BC's profile plot.
 *
 *         Pure UI surface — owns the layer-visibility checkboxes
 *         (Current HGL / Current EGL / Max HGL) plus the Max-HGL
 *         render-mode radios (Min↔max band vs Invert→max fill) and
 *         emits a single `togglesChanged` signal when any value
 *         changes.  The hosting `ProfilePlotDialog` connects that
 *         signal to `ProfilePlotWidget::setLayerToggles()`.  Max-EGL
 *         exposes only a line (no fill) — surface that via the styles
 *         tree in ProfileOptionsDialog, not here.
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
    explicit ProfileLayerPanel(QWidget *parent = nullptr);

    [[nodiscard]] ProfilePlotWidget::LayerToggles toggles() const;
    void setToggles(const ProfilePlotWidget::LayerToggles &t);

signals:
    void togglesChanged(const ProfilePlotWidget::LayerToggles &t);

private slots:
    void emitTogglesChanged();

private:
    QCheckBox    *m_currentHglLine = nullptr;
    QCheckBox    *m_currentHglFill = nullptr;
    QCheckBox    *m_currentEgl     = nullptr;
    QCheckBox    *m_maxHgl         = nullptr;
    QRadioButton *m_hglEnvelope    = nullptr;
    QRadioButton *m_hglInvertMax   = nullptr;
    QButtonGroup *m_hglGroup       = nullptr;
    QCheckBox    *m_nodeLabels     = nullptr;
    QCheckBox    *m_linkLabels     = nullptr;
    QCheckBox    *m_inlineLabels   = nullptr;
    QCheckBox    *m_useTerrain     = nullptr;
    QRadioButton *m_orientVertical = nullptr;
    QRadioButton *m_orientDiagonal = nullptr;
    QRadioButton *m_orientHorizontal = nullptr;
    QButtonGroup *m_orientGroup    = nullptr;
    QSpinBox     *m_angleSpin      = nullptr;
};

#endif // PROFILE_LAYER_PANEL_H
