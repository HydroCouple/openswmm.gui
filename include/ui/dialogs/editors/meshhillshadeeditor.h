/*!
 * \file   meshhillshadeeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  GIS-style hillshade / lighting editor for SWMM2DMeshLayer + DEM
 *         rasters (Slice S7).
 *
 *         Sliders for azimuth (compass bearing of incident light, 0..360°),
 *         altitude (sun angle above horizon, 0..90°), z-exaggeration
 *         (vertical scale used when computing face normals), and shadow
 *         floor (minimum lit value so deep shadows don't go fully black).
 *         A live "sun-position" thumbnail at the bottom shows the
 *         azimuth+altitude vector on a compass dial so the user gets a
 *         feel for the lighting geometry.
 *
 *         Bound directly to the layer's Q_PROPERTYs. Edits fire
 *         repaintRequested() through the existing setters, so the canvas
 *         updates live as the user drags.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_MESHHILLSHADEEDITOR_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_MESHHILLSHADEEDITOR_H

#include "ui/dialogs/istyleeditorwidget.h"

class SWMM2DMeshLayer;
class ColorRampComboBox;

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSlider;
class QSpinBox;
class QToolButton;

namespace openswmmvis::ui {

class ColorButton;

/*! Compact thumbnail rendering the sun's azimuth + altitude on a compass
 *  dial. Pure repaint; no user interaction. */
class SunPositionThumb : public QWidget
{
    Q_OBJECT
public:
    explicit SunPositionThumb(QWidget *parent = nullptr);
    void setAzimuth(double degrees);
    void setAltitude(double degrees);

protected:
    void paintEvent(QPaintEvent *e) override;
    QSize sizeHint() const override { return {110, 110}; }
    QSize minimumSizeHint() const override { return {80, 80}; }

private:
    double m_azimuth  = 225.0;
    double m_altitude = 35.3;
};

/*! Full hillshade + contour editor. Bound to a SWMM2DMeshLayer. */
class MeshHillshadeEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit MeshHillshadeEditor(SWMM2DMeshLayer *layer, QWidget *parent = nullptr);
    void refreshFromModel() override;

private:
    SWMM2DMeshLayer *m_layer = nullptr;

    // Display
    QCheckBox *m_showEdges  = nullptr;
    QCheckBox *m_showNodes  = nullptr;

    // Hillshade
    QDoubleSpinBox *m_azimuth    = nullptr;
    QSlider        *m_azSlider   = nullptr;
    QDoubleSpinBox *m_altitude   = nullptr;
    QSlider        *m_altSlider  = nullptr;
    QDoubleSpinBox *m_zExag      = nullptr;
    QDoubleSpinBox *m_minLit     = nullptr;
    QSlider        *m_minLitSlider = nullptr;
    SunPositionThumb *m_sunThumb = nullptr;

    // Contours
    QCheckBox      *m_showContours      = nullptr;
    QSpinBox       *m_intervals         = nullptr;
    QComboBox      *m_contourMethod     = nullptr;   // Slice US.3 — band classification method
    ColorButton    *m_contourColor      = nullptr;
    QDoubleSpinBox *m_contourWidth      = nullptr;
    QCheckBox      *m_filledContours    = nullptr;
    ColorRampComboBox *m_contourRamp    = nullptr;   // Slice US.3 — band colour scale
    ColorRampComboBox *m_terrainRamp    = nullptr;   // terrain-fill colour scale
    QCheckBox         *m_terrainInvert  = nullptr;   // terrain-fill ramp inversion
    QDoubleSpinBox    *m_edgeZoomPx     = nullptr;   // wireframe auto-show threshold
    QDoubleSpinBox    *m_vertexZoomPx   = nullptr;   // vertex auto-show threshold
    QDoubleSpinBox *m_filledOpacity     = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_MESHHILLSHADEEDITOR_H
