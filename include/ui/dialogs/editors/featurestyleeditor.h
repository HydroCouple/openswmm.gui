/**
 * @file      featurestyleeditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     GIS-themed editor for FeatureSublayerStyle (Point/Line/Polygon
 *         archetypes). Slice U-V2.
 *
 *         Three derived classes — one per archetype — share a tiny base
 *         that handles the common attribute/color/colorRamp row and the
 *         live preview swatch. Per-archetype subclasses add their specific
 *         editors (marker shape + size; line width/dash/arrows; outline
 *         + fill opacity).
 *
 *         All controls bind direct to the style bag's Q_PROPERTY setters
 *         and listen to styleChanged() for external mutations (Cancel
 *         rollback, .oswp load).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_FEATURESTYLEEDITOR_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_FEATURESTYLEEDITOR_H

#include "ui/dialogs/istyleeditorwidget.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;

namespace OpenSWMM::Render
{
class FeatureSublayerStyle;
class PointFeatureSublayerStyle;
class LineFeatureSublayerStyle;
class PolygonFeatureSublayerStyle;
}

namespace openswmmvis::ui
{

class ColorButton;
class LabelConfigEditor;
class MarkerShapeCombo;
class DashStyleCombo;
class StylePreviewSwatch;

/*! Base — handles the attribute / color / useColorRamp row + preview. */
class FeatureStyleEditorBase : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Construct common classification controls and subscribe to the borrowed feature
     * style's notifications.
     * @param style Borrowed raw style; concrete derived editors require a live nonnull target
     * and style/owning host must outlive the editor. Base construction alone tolerates null,
     * but later callbacks/addPreviewRow dereference it.
     * @param parent Optional Qt owner; caller owns a parentless instance of a concrete subclass.
     * Attribute names/display labels are copied from the owning host's provider, excluding
     * QString fields only; unknown legacy names remain editable. Builds no bindings/label
     * editor/preview until subclass setup calls the protected builders. GUI thread only.
     */
    explicit FeatureStyleEditorBase(OpenSWMM::Render::FeatureSublayerStyle *style,
                                    QWidget *parent = nullptr);

    /**
     * @brief Refresh common attribute/color/ramp and existing label controls, then call the
     * virtual preview hook.
     * No-op for null target; requires raw target lifetime otherwise. Finds attribute by data
     * then text, preserving an unknown name as editable text. Blocks common control signals;
     * LabelConfigEditor::setConfig supplies its own no-notify seeding. Replaces uncommitted
     * label/control drafts with model values, without snapshotting or writing a property.
     */
    void refreshFromModel() override;

protected:
    /**
     * @brief Wire common live property writes and append the full LabelConfig editor once during
     * subclass construction.
     * Requires initialized common widgets and a live nonnull style for attribute/color/ramp
     * callbacks. Attribute selection uses item data or text; editingFinished accepts raw
     * unknown field text without validation. Label hints copy host provider metadata, and
     * configChanged writes current config. Calling twice duplicates connections/widgets;
     * no guard, undo command or transactional draft storage is provided.
     */
    void buildCommonRows();
    /**
     * @brief Append a swatch and, when style parentage resolves a host, size/classification
     * subpanels.
     * Requires a live nonnull style; dereferences it to find FeatureSublayer then host. Adds
     * SizeChannelEditor for nonpolygon archetypes and KindRendererPanel for any resolved
     * host/category. Created widgets are editor-owned; style/host remain borrowed. Intended
     * once after subclass rows, without initializing preview kind or drawing sampled results.
     */
    void addPreviewRow();

    /**
     * @brief Refresh the subclass swatch from current style values without writing style state.
     * Subclass implements a null-safe preview/typed-style check and handles only its static
     * symbol properties. Called during common refresh, so concrete widgets must already
     * exist; no result sampling or dynamic renderer evaluation is required.
     */
    virtual void updatePreview() = 0;

    OpenSWMM::Render::FeatureSublayerStyle *m_style = nullptr;

    QComboBox *m_attributeCombo = nullptr;
    ColorButton *m_singleColorBtn = nullptr;
    QCheckBox *m_useRampBox = nullptr;
    StylePreviewSwatch *m_preview = nullptr;

    // L-1 — per-sublayer label controls (built in buildCommonRows()).
    // Full-fidelity LabelConfig editor (LAYER_STYLING_LABELING_PLAN).
    LabelConfigEditor *m_labelEditor = nullptr;

private:
    // Forward-declared in the global namespace to avoid pulling in
    // <QFormLayout> from a widely-included header.
    ::QFormLayout *m_form = nullptr;
};

// ---------------------------------------------------------------------------
// Point archetype
// ---------------------------------------------------------------------------

class PointFeatureStyleEditor : public FeatureStyleEditorBase
{
    Q_OBJECT
public:
    /**
     * @brief Build point marker controls plus common labels/classification, preview and optional
     * host panels.
     * @param style Required borrowed Point style, retained raw and required to outlive editor/
     * callbacks; host must also remain alive for embedded panels.
     * @param parent Optional Qt owner; caller owns a parentless editor.
     * Static size0.5..60pixels/one decimal and the legacy default marker subset write
     * individual style fields immediately. Named size control supports external classification
     * disabling; styleChanged refreshes all rows under blockers. No undo or Cancel snapshot.
     */
    explicit PointFeatureStyleEditor(OpenSWMM::Render::PointFeatureSublayerStyle *style,
                                     QWidget *parent = nullptr);

    /**
     * @brief Refresh common state then blocked point size/shape controls and the static swatch.
     * Returns after base refresh if typed style is null, though concrete construction requires
     * nonnull style. Display clamping/rounding does not write the target.
     */
    void refreshFromModel() override;

protected:
    /**
     * @brief Set point swatch color, static size and marker shape from the live typed style.
     * No-op when style/preview missing; no color-ramp classification, size-channel sampling,
     * labels or target mutation.
     */
    void updatePreview() override;

private:
    OpenSWMM::Render::PointFeatureSublayerStyle *m_pointStyle = nullptr;
    QDoubleSpinBox *m_sizeSpin = nullptr;
    MarkerShapeCombo *m_shapeCombo = nullptr;
};

// ---------------------------------------------------------------------------
// Line archetype
// ---------------------------------------------------------------------------

class LineFeatureStyleEditor : public FeatureStyleEditorBase
{
    Q_OBJECT
public:
    /**
     * @brief Build live line/dash/glyph/arrow controls and shared label/classification panels.
     * @param style Required borrowed Line style/owning host, both retained nonowningly through
     * widget/subpanel callbacks and required to outlive this editor.
     * @param parent Optional Qt owner; caller owns a parentless editor.
     * Width0.25..30pixels/two decimals, arrow length2..80/one decimal and width1..40/one decimal
     * write individual fields immediately. Refresh blocks controls; preview remains a static
     * line representation. No undo, snapshot or delayed Apply semantics here.
     */
    explicit LineFeatureStyleEditor(OpenSWMM::Render::LineFeatureSublayerStyle *style,
                                    QWidget *parent = nullptr);

    /**
     * @brief Refresh common rows then line width/dash/render mode and arrow controls under
     * blockers.
     * Uses target values through display ranges, without writing the style; updates preview
     * after all fields. Requires stored raw style lifetime.
     */
    void refreshFromModel() override;

protected:
    /**
     * @brief Set static line swatch color, dash, width and arrow visibility from its style.
     * No-op without preview/style. Does not reflect renderAsLine midpoint-glyph choice,
     * arrow dimensions/color, dynamic width/ramp or result values, and never writes style.
     */
    void updatePreview() override;

private:
    OpenSWMM::Render::LineFeatureSublayerStyle *m_lineStyle = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    DashStyleCombo *m_dashCombo = nullptr;
    QCheckBox *m_renderAsLineBox = nullptr;
    QCheckBox *m_showArrowsBox = nullptr;
    QDoubleSpinBox *m_arrowLenSpin = nullptr;
    QDoubleSpinBox *m_arrowWidSpin = nullptr;
    ColorButton *m_arrowColorBtn = nullptr;
};

// ---------------------------------------------------------------------------
// Polygon archetype
// ---------------------------------------------------------------------------

class PolygonFeatureStyleEditor : public FeatureStyleEditorBase
{
    Q_OBJECT
public:
    /**
     * @brief Build live polygon outline/fill-opacity and shared label/classification controls.
     * @param style Required borrowed Polygon style/host; must outlive the editor and any
     * embedded host panels because raw pointers are retained.
     * @param parent Optional Qt owner; caller owns a parentless editor.
     * Outline0..20pixels/two decimals and fractional opacity0..1/two decimals write individual
     * properties immediately. Refreshes under blockers and previews static styling; no undo,
     * Cancel snapshot or transactional editing here.
     */
    explicit PolygonFeatureStyleEditor(OpenSWMM::Render::PolygonFeatureSublayerStyle *style,
                                       QWidget *parent = nullptr);

    /**
     * @brief Refresh common rows then blocked polygon outline color/width and fractional fill
     * opacity.
     * Applies display constraints without writing them back, then refreshes the swatch.
     * Requires stored raw target lifetime; no ownership transfer.
     */
    void refreshFromModel() override;

protected:
    /**
     * @brief Set static polygon swatch color, outline pen and fill opacity from the live style.
     * No-op without preview/style; no classification, labels, result sampling or mutation.
     */
    void updatePreview() override;

private:
    OpenSWMM::Render::PolygonFeatureSublayerStyle *m_polyStyle = nullptr;
    ColorButton *m_outlineColorBtn = nullptr;
    QDoubleSpinBox *m_outlineWidthSpin = nullptr;
    QDoubleSpinBox *m_fillOpacitySpin = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_FEATURESTYLEEDITOR_H
