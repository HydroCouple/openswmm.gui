/*!
 * \file   featurestyleeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  GIS-themed editor for FeatureSublayerStyle (Point/Line/Polygon
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

namespace OpenSWMM::Render {
    class FeatureSublayerStyle;
    class PointFeatureSublayerStyle;
    class LineFeatureSublayerStyle;
    class PolygonFeatureSublayerStyle;
}

namespace openswmmvis::ui {

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
    explicit FeatureStyleEditorBase(OpenSWMM::Render::FeatureSublayerStyle *style,
                                     QWidget *parent = nullptr);

    void refreshFromModel() override;

protected:
    /*! Subclasses build their archetype-specific rows then call
     *  addPreviewRow() at the bottom so the swatch follows live edits. */
    void buildCommonRows();
    void addPreviewRow();

    /*! Subclass hook — repaint the preview swatch with whatever the
     *  archetype-specific state should look like. */
    virtual void updatePreview() = 0;

    OpenSWMM::Render::FeatureSublayerStyle *m_style = nullptr;

    QComboBox          *m_attributeCombo = nullptr;
    ColorButton        *m_singleColorBtn = nullptr;
    QCheckBox          *m_useRampBox    = nullptr;
    StylePreviewSwatch *m_preview       = nullptr;

    // L-1 — per-sublayer label controls (built in buildCommonRows()).
    // Full-fidelity LabelConfig editor (LAYER_STYLING_LABELING_PLAN).
    LabelConfigEditor  *m_labelEditor   = nullptr;

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
    explicit PointFeatureStyleEditor(OpenSWMM::Render::PointFeatureSublayerStyle *style,
                                      QWidget *parent = nullptr);

    void refreshFromModel() override;

protected:
    void updatePreview() override;

private:
    OpenSWMM::Render::PointFeatureSublayerStyle *m_pointStyle = nullptr;
    QDoubleSpinBox  *m_sizeSpin  = nullptr;
    MarkerShapeCombo *m_shapeCombo = nullptr;
};

// ---------------------------------------------------------------------------
// Line archetype
// ---------------------------------------------------------------------------

class LineFeatureStyleEditor : public FeatureStyleEditorBase
{
    Q_OBJECT
public:
    explicit LineFeatureStyleEditor(OpenSWMM::Render::LineFeatureSublayerStyle *style,
                                     QWidget *parent = nullptr);

    void refreshFromModel() override;

protected:
    void updatePreview() override;

private:
    OpenSWMM::Render::LineFeatureSublayerStyle *m_lineStyle = nullptr;
    QDoubleSpinBox *m_widthSpin       = nullptr;
    DashStyleCombo *m_dashCombo       = nullptr;
    QCheckBox      *m_renderAsLineBox = nullptr;
    QCheckBox      *m_showArrowsBox   = nullptr;
    QDoubleSpinBox *m_arrowLenSpin    = nullptr;
    QDoubleSpinBox *m_arrowWidSpin    = nullptr;
    ColorButton    *m_arrowColorBtn   = nullptr;
};

// ---------------------------------------------------------------------------
// Polygon archetype
// ---------------------------------------------------------------------------

class PolygonFeatureStyleEditor : public FeatureStyleEditorBase
{
    Q_OBJECT
public:
    explicit PolygonFeatureStyleEditor(OpenSWMM::Render::PolygonFeatureSublayerStyle *style,
                                       QWidget *parent = nullptr);

    void refreshFromModel() override;

protected:
    void updatePreview() override;

private:
    OpenSWMM::Render::PolygonFeatureSublayerStyle *m_polyStyle = nullptr;
    ColorButton    *m_outlineColorBtn = nullptr;
    QDoubleSpinBox *m_outlineWidthSpin = nullptr;
    QDoubleSpinBox *m_fillOpacitySpin = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_FEATURESTYLEEDITOR_H
