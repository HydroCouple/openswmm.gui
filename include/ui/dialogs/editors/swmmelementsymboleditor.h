/**
 * @file      swmmelementsymboleditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     GIS-style editor for SwmmElementSymbolAdapter (Slice U-V3).
 *
 *         Wraps the live SwmmElementSymbolAdapter Q_PROPERTYs with
 *         colour, outline/size, label/font and flow-arrow controls, with
 *         a live base-symbol preview swatch.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_SWMMELEMENTSYMBOLEDITOR_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_SWMMELEMENTSYMBOLEDITOR_H

#include "ui/dialogs/istyleeditorwidget.h"

class QCheckBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLineEdit;

class SwmmElementSymbolAdapter;

namespace openswmmvis::ui
{

class ColorButton;
class StylePreviewSwatch;

class SwmmElementSymbolEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build live base-symbol, label and flow-arrow controls for a persistent SWMM symbol
     * adapter.
     * @param adapter Borrowed raw adapter required for interactive use; caller keeps it alive
     * through this widget and all callbacks. Null makes refresh inert but does not make
     * connected control writes safe.
     * @param parent Optional Qt owner; caller owns a parentless widget.
     * Reads under control signal blockers, subscribes to symbolChanged and writes individual
     * properties immediately on control changes. Size0.5..60pixels and arrow2..60pixel
     * controls display one decimal; outline0..20pixels displays two. Arrow controls are
     * always exposed; applicability belongs to adapter setters. No direct undo, Cancel
     * snapshot or destruction rollback; use on GUI thread.
     */
    explicit SwmmElementSymbolEditor(SwmmElementSymbolAdapter *adapter, QWidget *parent = nullptr);

    /**
     * @brief Seed all symbol/label/arrow controls under signal blockers, then refresh the swatch.
     * No-op for null target, otherwise requires borrowed raw adapter lifetime. Widget ranges
     * may round/clamp displayed values without writing the model. External symbol changes
     * refresh the editor; no target signals are blocked or snapshot captured.
     */
    void refreshFromModel() override;

private:
    /**
     * @brief Update the point-kind swatch's fill/outline/size/line width and arrow visibility
     * from the adapter.
     * No-op for missing adapter or preview; no target mutation. Does not preview label text,
     * fonts, arrow dimensions/color or change the swatch into a line-kind preview.
     */
    void updatePreview();

    SwmmElementSymbolAdapter *m_adapter = nullptr;

    // Fill / outline / size
    ColorButton *m_fillBtn = nullptr;
    ColorButton *m_outlineBtn = nullptr;
    QDoubleSpinBox *m_outlineWSpin = nullptr;
    QDoubleSpinBox *m_sizeSpin = nullptr;

    // Labels
    QCheckBox *m_showLabelBox = nullptr;
    QFontComboBox *m_labelFontCombo = nullptr;
    ColorButton *m_labelColorBtn = nullptr;

    // Flow arrows
    QCheckBox *m_showArrowsBox = nullptr;
    QDoubleSpinBox *m_arrowSizeSpin = nullptr; ///< length ALONG the link
    QDoubleSpinBox *m_arrowWidSpin = nullptr;  ///< width ACROSS the link
    ColorButton *m_arrowColorBtn = nullptr;
    QCheckBox *m_arrowsFlowPosBox = nullptr;

    StylePreviewSwatch *m_preview = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_SWMMELEMENTSYMBOLEDITOR_H
