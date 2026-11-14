/**
 * @file      symbolstyleeditors2d.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     SE.4 — dedicated IStyleEditorWidget editors for the raster / mesh /
 *         2D-results *SymbolStyleAdapter archetypes. These cover every
 *         adapter the rule path can produce so the generic property-grid
 *         fallback can be removed from the symbology editor (the dialog never
 *         needs to fall back for a symbol layer).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS2D_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS2D_H

#include "ui/dialogs/istyleeditorwidget.h"

namespace OpenSWMM::Render
{
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

namespace openswmmvis::ui
{

class ColorButton;
class DashStyleCombo;
class MarkerShapeCombo;

class RasterColorRampSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Construct the GUI-thread live editor for attribute/minimum/maximum, logarithmic
     * scaling, endpoint/out-of-range colors and opacity. Numeric range controls clamp display to
     * [-1e9,1e9] without cross-validating minimum against maximum.
     * @param[in,out] a Nullable borrowed adapter stored as a raw pointer. A non-null adapter must
     * outlive the editor and all queued callbacks; QObject destruction is not tracked by this raw
     * member.
     * @param[in] parent Optional Qt widget owner for the editor/controls.
     * Initial refresh and subsequent changed() signals hydrate under signal blockers; user edits
     * call adapter setters directly with no local undo/snapshot. Null adapter leaves controls
     * present but edits/refresh do nothing. Opacity is displayed in 0..100 percent and stored as a
     * 0..1 fraction.
     */
    explicit RasterColorRampSymbolStyleEditor(
        OpenSWMM::Render::RasterColorRampSymbolStyleAdapter *a, QWidget *parent = nullptr);
    /**
     * @brief Read the bound adapter into all editor controls while blocking control signals.
     * No-op for null adapter; otherwise requires its borrowed raw pointer to remain live on the GUI
     * thread. Converts opacity fraction to percent and lets widget bounds/precision constrain
     * displayed values without writing those constrained values back.
     */
    void refreshFromModel() override;

private:
    OpenSWMM::Render::RasterColorRampSymbolStyleAdapter *m_a = nullptr;
    QLineEdit *m_attr = nullptr;
    QDoubleSpinBox *m_min = nullptr, *m_max = nullptr, *m_opacity = nullptr;
    ColorButton *m_low = nullptr, *m_high = nullptr, *m_below = nullptr, *m_above = nullptr;
    QCheckBox *m_log = nullptr;
};

class HillshadeSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Construct the GUI-thread live editor for flat fill/elevation-ramp choice,
     * dimensionless hillshade strength [0,1] and opacity.
     * @param[in,out] a Nullable borrowed adapter stored as a raw pointer. A non-null adapter must
     * outlive the editor and all queued callbacks; QObject destruction is not tracked by this raw
     * member.
     * @param[in] parent Optional Qt widget owner for the editor/controls.
     * Initial refresh and subsequent changed() signals hydrate under signal blockers; user edits
     * call adapter setters directly with no local undo/snapshot. Null adapter leaves controls
     * present but edits/refresh do nothing. Opacity is displayed in 0..100 percent and stored as a
     * 0..1 fraction.
     */
    explicit HillshadeSymbolStyleEditor(OpenSWMM::Render::HillshadeSymbolStyleAdapter *a,
                                        QWidget *parent = nullptr);
    /**
     * @brief Read the bound adapter into all editor controls while blocking control signals.
     * No-op for null adapter; otherwise requires its borrowed raw pointer to remain live on the GUI
     * thread. Converts opacity fraction to percent and lets widget bounds/precision constrain
     * displayed values without writing those constrained values back.
     */
    void refreshFromModel() override;

private:
    OpenSWMM::Render::HillshadeSymbolStyleAdapter *m_a = nullptr;
    ColorButton *m_fill = nullptr;
    QDoubleSpinBox *m_strength = nullptr, *m_opacity = nullptr;
    QCheckBox *m_useRamp = nullptr;
};

class ContourBandSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Construct the GUI-thread live editor for attribute, two through sixty-four bands,
     * endpoint/out-of-range colors, smooth boundaries and opacity. The group title is chosen from
     * the initial attribute.
     * @param[in,out] a Nullable borrowed adapter stored as a raw pointer. A non-null adapter must
     * outlive the editor and all queued callbacks; QObject destruction is not tracked by this raw
     * member.
     * @param[in] parent Optional Qt widget owner for the editor/controls.
     * Initial refresh and subsequent changed() signals hydrate under signal blockers; user edits
     * call adapter setters directly with no local undo/snapshot. Null adapter leaves controls
     * present but edits/refresh do nothing. Opacity is displayed in 0..100 percent and stored as a
     * 0..1 fraction.
     */
    explicit ContourBandSymbolStyleEditor(OpenSWMM::Render::ContourBandSymbolStyleAdapter *a,
                                          QWidget *parent = nullptr);
    /**
     * @brief Read the bound adapter into all editor controls while blocking control signals.
     * No-op for null adapter; otherwise requires its borrowed raw pointer to remain live on the GUI
     * thread. Converts opacity fraction to percent and lets widget bounds/precision constrain
     * displayed values without writing those constrained values back.
     */
    void refreshFromModel() override;

private:
    OpenSWMM::Render::ContourBandSymbolStyleAdapter *m_a = nullptr;
    QLineEdit *m_attr = nullptr;
    QSpinBox *m_bands = nullptr;
    QDoubleSpinBox *m_opacity = nullptr;
    ColorButton *m_low = nullptr, *m_high = nullptr, *m_below = nullptr, *m_above = nullptr;
    QCheckBox *m_smooth = nullptr;
};

class IsolineSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Construct the GUI-thread live editor for attribute, one through sixty-four isolines,
     * color, dash, labels, 0.25..30-pixel width and opacity. The group title follows only the
     * initial attribute.
     * @param[in,out] a Nullable borrowed adapter stored as a raw pointer. A non-null adapter must
     * outlive the editor and all queued callbacks; QObject destruction is not tracked by this raw
     * member.
     * @param[in] parent Optional Qt widget owner for the editor/controls.
     * Initial refresh and subsequent changed() signals hydrate under signal blockers; user edits
     * call adapter setters directly with no local undo/snapshot. Null adapter leaves controls
     * present but edits/refresh do nothing. Opacity is displayed in 0..100 percent and stored as a
     * 0..1 fraction.
     */
    explicit IsolineSymbolStyleEditor(OpenSWMM::Render::IsolineSymbolStyleAdapter *a,
                                      QWidget *parent = nullptr);
    /**
     * @brief Read the bound adapter into all editor controls while blocking control signals.
     * No-op for null adapter; otherwise requires its borrowed raw pointer to remain live on the GUI
     * thread. Converts opacity fraction to percent and lets widget bounds/precision constrain
     * displayed values without writing those constrained values back.
     */
    void refreshFromModel() override;

private:
    OpenSWMM::Render::IsolineSymbolStyleAdapter *m_a = nullptr;
    QLineEdit *m_attr = nullptr;
    QSpinBox *m_count = nullptr;
    ColorButton *m_color = nullptr;
    QDoubleSpinBox *m_width = nullptr, *m_opacity = nullptr;
    DashStyleCombo *m_dash = nullptr;
    QCheckBox *m_labels = nullptr;
};

class MeshEdgeSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Construct the GUI-thread live editor for wireframe color/dash, 0.1..20-pixel
     * normal/wide widths, slope emphasis threshold [0,10] and opacity.
     * @param[in,out] a Nullable borrowed adapter stored as a raw pointer. A non-null adapter must
     * outlive the editor and all queued callbacks; QObject destruction is not tracked by this raw
     * member.
     * @param[in] parent Optional Qt widget owner for the editor/controls.
     * Initial refresh and subsequent changed() signals hydrate under signal blockers; user edits
     * call adapter setters directly with no local undo/snapshot. Null adapter leaves controls
     * present but edits/refresh do nothing. Opacity is displayed in 0..100 percent and stored as a
     * 0..1 fraction.
     */
    explicit MeshEdgeSymbolStyleEditor(OpenSWMM::Render::MeshEdgeSymbolStyleAdapter *a,
                                       QWidget *parent = nullptr);
    /**
     * @brief Read the bound adapter into all editor controls while blocking control signals.
     * No-op for null adapter; otherwise requires its borrowed raw pointer to remain live on the GUI
     * thread. Converts opacity fraction to percent and lets widget bounds/precision constrain
     * displayed values without writing those constrained values back.
     */
    void refreshFromModel() override;

private:
    OpenSWMM::Render::MeshEdgeSymbolStyleAdapter *m_a = nullptr;
    ColorButton *m_color = nullptr, *m_wideColor = nullptr;
    QDoubleSpinBox *m_width = nullptr, *m_opacity = nullptr, *m_slopeBreak = nullptr,
                   *m_wideWidth = nullptr;
    DashStyleCombo *m_dash = nullptr;
    QCheckBox *m_slopeDriven = nullptr;
};

class MeshNodeSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Construct the GUI-thread live editor for canonical marker shape, 0.5..60-pixel
     * normal/tagged size, 0..20-pixel outline, tagged highlighting and opacity.
     * @param[in,out] a Nullable borrowed adapter stored as a raw pointer. A non-null adapter must
     * outlive the editor and all queued callbacks; QObject destruction is not tracked by this raw
     * member.
     * @param[in] parent Optional Qt widget owner for the editor/controls.
     * Initial refresh and subsequent changed() signals hydrate under signal blockers; user edits
     * call adapter setters directly with no local undo/snapshot. Null adapter leaves controls
     * present but edits/refresh do nothing. Opacity is displayed in 0..100 percent and stored as a
     * 0..1 fraction.
     */
    explicit MeshNodeSymbolStyleEditor(OpenSWMM::Render::MeshNodeSymbolStyleAdapter *a,
                                       QWidget *parent = nullptr);
    /**
     * @brief Read the bound adapter into all editor controls while blocking control signals.
     * No-op for null adapter; otherwise requires its borrowed raw pointer to remain live on the GUI
     * thread. Converts opacity fraction to percent and lets widget bounds/precision constrain
     * displayed values without writing those constrained values back.
     */
    void refreshFromModel() override;

private:
    OpenSWMM::Render::MeshNodeSymbolStyleAdapter *m_a = nullptr;
    ColorButton *m_color = nullptr, *m_outline = nullptr, *m_taggedColor = nullptr;
    QDoubleSpinBox *m_size = nullptr, *m_outlineW = nullptr, *m_opacity = nullptr,
                   *m_taggedSize = nullptr;
    MarkerShapeCombo *m_shape = nullptr;
    QCheckBox *m_highlightTagged = nullptr;
};

class VelocityVectorSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Construct the GUI-thread live editor for color, scale [0,500] pixels/(m/s), minimum
     * [0,200]/maximum [0,400] length pixels, spacing [1,400] pixels, head [0,64] pixels, dry cutoff
     * [0,100] in adapter depth units and opacity. No minimum/maximum relationship is validated
     * here.
     * @param[in,out] a Nullable borrowed adapter stored as a raw pointer. A non-null adapter must
     * outlive the editor and all queued callbacks; QObject destruction is not tracked by this raw
     * member.
     * @param[in] parent Optional Qt widget owner for the editor/controls.
     * Initial refresh and subsequent changed() signals hydrate under signal blockers; user edits
     * call adapter setters directly with no local undo/snapshot. Null adapter leaves controls
     * present but edits/refresh do nothing. Opacity is displayed in 0..100 percent and stored as a
     * 0..1 fraction.
     */
    explicit VelocityVectorSymbolStyleEditor(OpenSWMM::Render::VelocityVectorSymbolStyleAdapter *a,
                                             QWidget *parent = nullptr);
    /**
     * @brief Read the bound adapter into all editor controls while blocking control signals.
     * No-op for null adapter; otherwise requires its borrowed raw pointer to remain live on the GUI
     * thread. Converts opacity fraction to percent and lets widget bounds/precision constrain
     * displayed values without writing those constrained values back.
     */
    void refreshFromModel() override;

private:
    OpenSWMM::Render::VelocityVectorSymbolStyleAdapter *m_a = nullptr;
    ColorButton *m_color = nullptr;
    QDoubleSpinBox *m_scale = nullptr, *m_minLen = nullptr, *m_maxLen = nullptr,
                   *m_spacing = nullptr, *m_head = nullptr, *m_dry = nullptr, *m_opacity = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS2D_H
