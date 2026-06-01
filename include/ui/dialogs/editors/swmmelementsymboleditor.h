/*!
 * \file   swmmelementsymboleditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  GIS-style editor for SwmmElementSymbolAdapter (Slice U-V3).
 *
 *         Wraps the live SwmmElementSymbolAdapter Q_PROPERTYs with
 *         familiar QGIS controls: colour buttons + swatches, marker
 *         shape combo, dash combo for flow arrows, live preview swatch.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_SWMMELEMENTSYMBOLEDITOR_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_SWMMELEMENTSYMBOLEDITOR_H

#include "ui/dialogs/istyleeditorwidget.h"

class QCheckBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLineEdit;

class SwmmElementSymbolAdapter;

namespace openswmmvis::ui {

class ColorButton;
class StylePreviewSwatch;

class SwmmElementSymbolEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit SwmmElementSymbolEditor(SwmmElementSymbolAdapter *adapter,
                                      QWidget *parent = nullptr);

    void refreshFromModel() override;

private:
    void updatePreview();

    SwmmElementSymbolAdapter *m_adapter = nullptr;

    // Fill / outline / size
    ColorButton    *m_fillBtn    = nullptr;
    ColorButton    *m_outlineBtn = nullptr;
    QDoubleSpinBox *m_outlineWSpin = nullptr;
    QDoubleSpinBox *m_sizeSpin   = nullptr;

    // Labels
    QCheckBox     *m_showLabelBox = nullptr;
    QFontComboBox *m_labelFontCombo = nullptr;
    ColorButton   *m_labelColorBtn  = nullptr;

    // Flow arrows
    QCheckBox      *m_showArrowsBox  = nullptr;
    QDoubleSpinBox *m_arrowSizeSpin  = nullptr;
    ColorButton    *m_arrowColorBtn  = nullptr;
    QCheckBox      *m_arrowsFlowPosBox = nullptr;

    StylePreviewSwatch *m_preview = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_SWMMELEMENTSYMBOLEDITOR_H
