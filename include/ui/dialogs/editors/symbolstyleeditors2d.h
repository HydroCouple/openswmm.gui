/*!
 * \file   symbolstyleeditors2d.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  SE.4 — dedicated IStyleEditorWidget editors for the raster / mesh /
 *         2D-results *SymbolStyleAdapter archetypes. These cover every
 *         adapter the rule path can produce so the generic property-grid
 *         fallback can be removed from the symbology editor (the dialog never
 *         needs to fall back for a symbol layer).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS2D_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS2D_H

#include "ui/dialogs/istyleeditorwidget.h"

namespace OpenSWMM::Render {
class RasterColorRampSymbolStyleAdapter;
class HillshadeSymbolStyleAdapter;
class ContourBandSymbolStyleAdapter;
class IsolineSymbolStyleAdapter;
class MeshEdgeSymbolStyleAdapter;
class MeshNodeSymbolStyleAdapter;
class VelocityVectorSymbolStyleAdapter;
}

class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;

namespace openswmmvis::ui {

class ColorButton;
class DashStyleCombo;
class MarkerShapeCombo;

class RasterColorRampSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit RasterColorRampSymbolStyleEditor(
        OpenSWMM::Render::RasterColorRampSymbolStyleAdapter *a, QWidget *parent = nullptr);
    void refreshFromModel() override;
private:
    OpenSWMM::Render::RasterColorRampSymbolStyleAdapter *m_a = nullptr;
    QLineEdit *m_attr = nullptr; QDoubleSpinBox *m_min = nullptr, *m_max = nullptr, *m_opacity = nullptr;
    ColorButton *m_low = nullptr, *m_high = nullptr, *m_below = nullptr, *m_above = nullptr;
    QCheckBox *m_log = nullptr;
};

class HillshadeSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit HillshadeSymbolStyleEditor(
        OpenSWMM::Render::HillshadeSymbolStyleAdapter *a, QWidget *parent = nullptr);
    void refreshFromModel() override;
private:
    OpenSWMM::Render::HillshadeSymbolStyleAdapter *m_a = nullptr;
    ColorButton *m_fill = nullptr; QDoubleSpinBox *m_strength = nullptr, *m_opacity = nullptr;
    QCheckBox *m_useRamp = nullptr;
};

class ContourBandSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit ContourBandSymbolStyleEditor(
        OpenSWMM::Render::ContourBandSymbolStyleAdapter *a, QWidget *parent = nullptr);
    void refreshFromModel() override;
private:
    OpenSWMM::Render::ContourBandSymbolStyleAdapter *m_a = nullptr;
    QLineEdit *m_attr = nullptr; QSpinBox *m_bands = nullptr; QDoubleSpinBox *m_opacity = nullptr;
    ColorButton *m_low = nullptr, *m_high = nullptr, *m_below = nullptr, *m_above = nullptr;
    QCheckBox *m_smooth = nullptr;
};

class IsolineSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit IsolineSymbolStyleEditor(
        OpenSWMM::Render::IsolineSymbolStyleAdapter *a, QWidget *parent = nullptr);
    void refreshFromModel() override;
private:
    OpenSWMM::Render::IsolineSymbolStyleAdapter *m_a = nullptr;
    QLineEdit *m_attr = nullptr; QSpinBox *m_count = nullptr;
    ColorButton *m_color = nullptr; QDoubleSpinBox *m_width = nullptr, *m_opacity = nullptr;
    DashStyleCombo *m_dash = nullptr; QCheckBox *m_labels = nullptr;
};

class MeshEdgeSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit MeshEdgeSymbolStyleEditor(
        OpenSWMM::Render::MeshEdgeSymbolStyleAdapter *a, QWidget *parent = nullptr);
    void refreshFromModel() override;
private:
    OpenSWMM::Render::MeshEdgeSymbolStyleAdapter *m_a = nullptr;
    ColorButton *m_color = nullptr, *m_wideColor = nullptr;
    QDoubleSpinBox *m_width = nullptr, *m_opacity = nullptr, *m_slopeBreak = nullptr, *m_wideWidth = nullptr;
    DashStyleCombo *m_dash = nullptr; QCheckBox *m_slopeDriven = nullptr;
};

class MeshNodeSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit MeshNodeSymbolStyleEditor(
        OpenSWMM::Render::MeshNodeSymbolStyleAdapter *a, QWidget *parent = nullptr);
    void refreshFromModel() override;
private:
    OpenSWMM::Render::MeshNodeSymbolStyleAdapter *m_a = nullptr;
    ColorButton *m_color = nullptr, *m_outline = nullptr, *m_taggedColor = nullptr;
    QDoubleSpinBox *m_size = nullptr, *m_outlineW = nullptr, *m_opacity = nullptr, *m_taggedSize = nullptr;
    MarkerShapeCombo *m_shape = nullptr; QCheckBox *m_highlightTagged = nullptr;
};

class VelocityVectorSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit VelocityVectorSymbolStyleEditor(
        OpenSWMM::Render::VelocityVectorSymbolStyleAdapter *a, QWidget *parent = nullptr);
    void refreshFromModel() override;
private:
    OpenSWMM::Render::VelocityVectorSymbolStyleAdapter *m_a = nullptr;
    ColorButton *m_color = nullptr;
    QDoubleSpinBox *m_scale = nullptr, *m_minLen = nullptr, *m_maxLen = nullptr,
                   *m_spacing = nullptr, *m_head = nullptr, *m_dry = nullptr, *m_opacity = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS2D_H
