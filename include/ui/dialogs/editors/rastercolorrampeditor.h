/*!
 * \file   rastercolorrampeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QGIS-style editor for a GISRasterLayer's display + ramp settings
 *         (Slice U-V5).
 *
 *         Drives the layer's Q_PROPERTYs directly — renderBand, noDataValue
 *         (read-only label), colorRamp via ColorRampComboBox + min/max
 *         spinboxes + Auto-stretch button. Plays nicely with the existing
 *         GISRasterLayer NOTIFY signals so external mutations (Cancel
 *         rollback) refresh the UI.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_RASTERCOLORRAMPEDITOR_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_RASTERCOLORRAMPEDITOR_H

#include "ui/dialogs/istyleeditorwidget.h"

class GISRasterLayer;
class ColorRampComboBox;

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QToolButton;

namespace openswmmvis::ui {

class RasterColorRampEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit RasterColorRampEditor(GISRasterLayer *layer, QWidget *parent = nullptr);

    void refreshFromModel() override;

private slots:
    void onAutoStretch();

private:
    GISRasterLayer    *m_layer       = nullptr;

    QSpinBox          *m_bandSpin    = nullptr;
    QLabel            *m_nodataLabel = nullptr;
    ColorRampComboBox *m_rampCombo   = nullptr;
    QDoubleSpinBox    *m_minSpin     = nullptr;
    QDoubleSpinBox    *m_maxSpin     = nullptr;
    QToolButton       *m_autoBtn     = nullptr;

    // VS.6 hillshade relief overlay (R-2).
    QCheckBox         *m_hsEnable    = nullptr;
    QDoubleSpinBox    *m_hsAzimuth   = nullptr;
    QDoubleSpinBox    *m_hsAltitude  = nullptr;
    QDoubleSpinBox    *m_hsZFactor   = nullptr;
    QDoubleSpinBox    *m_hsStrength  = nullptr;

    void pushHillshade();
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_RASTERCOLORRAMPEDITOR_H
