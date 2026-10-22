/**
 * @file      gisvectorsymboleditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     GIS-style editor for GisVectorSymbolAdapter (Slice U-V4).
 *
 *         Surfaces live GISVectorSymbol controls
 *         (markers, lines, polygons, labels) in a single tabbed
 *         widget. Geometry-specific sub-tabs let the user style
 *         points/lines/polygons independently; labels are always
 *         applicable.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_GISVECTORSYMBOLEDITOR_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_GISVECTORSYMBOLEDITOR_H

#include "ui/dialogs/istyleeditorwidget.h"

class GisVectorSymbolAdapter;

class QCheckBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLineEdit;

namespace openswmmvis::ui
{

class ColorButton;
class DashStyleCombo;
class MarkerShapeCombo;
class StylePreviewSwatch;

class GisVectorSymbolEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build live marker/line/polygon/label controls bound directly to a GIS symbol
     * adapter.
     * @param adapter Borrowed raw adapter required for interactive use; it must outlive this
     * widget and every control callback. A null adapter leaves refresh inert but callbacks
     * still dereference it, so null is not a safe editable binding.
     * @param parent Optional Qt owner; caller owns parentless widget.
     * Copies values into controls under signal blockers, then listens to symbolChanged for
     * external edits/Cancel restoration. Individual callbacks submit only their property;
     * label field commits on editingFinished, other controls on change. No explicit undo,
     * snapshot or rollback policy. Marker fill/polygon fill expose alpha. GUI thread only.
     */
    explicit GisVectorSymbolEditor(GisVectorSymbolAdapter *adapter, QWidget *parent = nullptr);

    /**
     * @brief Read adapter values into every control under blockers and refresh three preview
     * swatches.
     * No-op for null adapter; requires a nonnull raw target to remain alive. Spin boxes round/
     * clamp display values without writing them, label text refresh replaces any uncommitted
     * draft, and target signals remain unblocked. No ownership transfer or snapshot.
     */
    void refreshFromModel() override;

private:
    /**
     * @brief Render point/line/polygon swatch properties from the adapter's actual values.
     * Requires a live nonnull adapter; guards each preview pointer only. Sets point shape/
     * fill/outline/size, line color/dash/width and polygon fill/outline/alpha. No style mutation,
     * label preview, canvas repaint or write of displayed rounded values.
     */
    void updatePreview();

    GisVectorSymbolAdapter *m_adapter = nullptr;

    // Marker
    MarkerShapeCombo *m_shapeCombo = nullptr;
    QDoubleSpinBox *m_markerSize = nullptr;
    ColorButton *m_markerFill = nullptr;
    ColorButton *m_markerStroke = nullptr;
    QDoubleSpinBox *m_markerStrokeW = nullptr;

    // Line
    ColorButton *m_lineColor = nullptr;
    QDoubleSpinBox *m_lineWidth = nullptr;
    DashStyleCombo *m_lineDash = nullptr;

    // Polygon
    ColorButton *m_polyFill = nullptr;
    ColorButton *m_polyOutline = nullptr;
    QDoubleSpinBox *m_polyOutlineW = nullptr;

    // Labels
    QCheckBox *m_showLabels = nullptr;
    QLineEdit *m_labelField = nullptr;
    QFontComboBox *m_labelFont = nullptr;
    ColorButton *m_labelColor = nullptr;

    StylePreviewSwatch *m_pointPreview = nullptr;
    StylePreviewSwatch *m_linePreview = nullptr;
    StylePreviewSwatch *m_polygonPreview = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_GISVECTORSYMBOLEDITOR_H
