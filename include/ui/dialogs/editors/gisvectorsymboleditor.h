/*!
 * \file   gisvectorsymboleditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  GIS-style editor for GisVectorSymbolAdapter (Slice U-V4).
 *
 *         Surfaces every Q_PROPERTY of the GISVectorSymbol struct
 *         (markers, lines, polygons, labels) in a single tabbed
 *         widget. Geometry-specific sub-tabs let the user style
 *         points/lines/polygons independently; labels are always
 *         applicable.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_GISVECTORSYMBOLEDITOR_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_GISVECTORSYMBOLEDITOR_H

#include "ui/dialogs/istyleeditorwidget.h"

class GisVectorSymbolAdapter;

class QCheckBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLineEdit;

namespace openswmmvis::ui {

class ColorButton;
class DashStyleCombo;
class MarkerShapeCombo;
class StylePreviewSwatch;

class GisVectorSymbolEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit GisVectorSymbolEditor(GisVectorSymbolAdapter *adapter,
                                    QWidget *parent = nullptr);

    void refreshFromModel() override;

private:
    void updatePreview();

    GisVectorSymbolAdapter *m_adapter = nullptr;

    // Marker
    MarkerShapeCombo *m_shapeCombo  = nullptr;
    QDoubleSpinBox   *m_markerSize  = nullptr;
    ColorButton      *m_markerFill  = nullptr;
    ColorButton      *m_markerStroke= nullptr;
    QDoubleSpinBox   *m_markerStrokeW = nullptr;

    // Line
    ColorButton      *m_lineColor   = nullptr;
    QDoubleSpinBox   *m_lineWidth   = nullptr;
    DashStyleCombo   *m_lineDash    = nullptr;

    // Polygon
    ColorButton      *m_polyFill    = nullptr;
    ColorButton      *m_polyOutline = nullptr;
    QDoubleSpinBox   *m_polyOutlineW = nullptr;

    // Labels
    QCheckBox        *m_showLabels  = nullptr;
    QLineEdit        *m_labelField  = nullptr;
    QFontComboBox    *m_labelFont   = nullptr;
    ColorButton      *m_labelColor  = nullptr;

    StylePreviewSwatch *m_pointPreview   = nullptr;
    StylePreviewSwatch *m_linePreview    = nullptr;
    StylePreviewSwatch *m_polygonPreview = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_GISVECTORSYMBOLEDITOR_H
