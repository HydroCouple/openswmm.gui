/*!
 * \file   kindrendererpanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renderer-mode chooser + classification controls (Slice S2+S5).
 *
 *         Embedded in each FeatureStyleEditor below the basic single-symbol
 *         controls. Lets the user override the per-kind paint with a
 *         GraduatedRenderer or CategorizedRenderer.
 *
 *         Slice US.1 (UNIFIED_STYLING plan S1): the graduated classification
 *         block (ramp / method / classes / range mode / breaks table /
 *         auto-classify) now lives in the SHARED ClassificationEditor, driven
 *         here by a GraduatedRendererBinding. This panel keeps the 1D-specific
 *         glue: the mode chooser, the IAttributeProvider-backed attribute
 *         picker, the archetype-gated output-axis row (size/width by value),
 *         and the Categorized branch. The same editor drives the 2D results /
 *         mesh sublayers, so similar attributes get identical UI.
 *
 *         The widget owns no model state — every edit routes back through
 *         hostLayer->setKindRenderer(category, ...) (or Rule::setRenderer)
 *         which fires the layer's repaintRequested. Cancel rollback is handled
 *         at the dialog level via the ILayerStyleSubject snapshot.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_KINDRENDERERPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_KINDRENDERERPANEL_H

#include "layers/swmm_category.h"

#include <QWidget>

#include <memory>

class OpenSWMMVisLayer;
class SWMMModelLayer;
class SWMMResultsLayer;

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace OpenSWMM::Render {
class IFeatureRenderer;
class Rule;          // Slice B.6a — Rule-aware ctor below.
}

namespace openswmmvis::ui {

class ClassificationEditor;
class GraduatedRendererBinding;

class KindRendererPanel : public QWidget
{
    Q_OBJECT
public:
    /*! \param hostLayer  Either a SWMMModelLayer or a SWMMResultsLayer.
     *                    The panel uses the layer's kindRenderer /
     *                    setKindRenderer / resetKindRendererToDefaults
     *                    API to read and write per-kind renderers. */
    KindRendererPanel(OpenSWMMVisLayer *hostLayer,
                      OpenSWMMVis::SwmmCategory category,
                      QWidget *parent = nullptr);

    /*! Slice B.6a — Rule-aware constructor. The panel reads/writes the
     *  Rule's owned IFeatureRenderer directly. \p rule must outlive
     *  the panel; the panel does not take ownership. */
    explicit KindRendererPanel(OpenSWMM::Render::Rule *rule,
                                QWidget *parent = nullptr);

    // Out-of-line so unique_ptr<GraduatedRendererBinding> (forward-declared)
    // is destroyed where the type is complete.
    ~KindRendererPanel() override;

    /*! Re-read the current renderer from the host layer and update the
     *  panel's controls. Called by the dialog after a Cancel rollback. */
    void refreshFromModel();

private slots:
    void onModeChanged(int comboRow);
    // Slice DM.2 — attribute picker. Populated from IAttributeProvider
    // when the host layer (or the Rule's owning layer) implements it;
    // hidden otherwise.
    void onAttributeChanged(int comboRow);
    // Gap A4.5 — output axes: size-by-value (points) / width-by-value (lines).
    void onOutputAxisChanged();

private:
    void buildUi();
    OpenSWMM::Render::IFeatureRenderer *currentRenderer() const;
    /*! IAttributeProvider host — the layer itself, or the Rule's owning
     *  layer (two QObject parents up). */
    OpenSWMMVisLayer *attributeProviderHost() const;

    OpenSWMMVisLayer       *m_hostLayer = nullptr;
    SWMMModelLayer         *m_modelLayer = nullptr;     // either m_modelLayer
    SWMMResultsLayer       *m_resultsLayer = nullptr;   // or m_resultsLayer is set
    OpenSWMMVis::SwmmCategory m_category = OpenSWMMVis::CatJunctions;

    // Slice B.6a — Rule-aware mode. When non-null, m_modelLayer /
    // m_resultsLayer are both null and read/write paths route through
    // Rule::renderer / Rule::setRenderer.
    OpenSWMM::Render::Rule *m_rule = nullptr;

    // Controls — 1D glue retained by this panel.
    QComboBox          *m_modeCombo    = nullptr;
    QComboBox          *m_attrCombo    = nullptr;   // Slice DM.2
    QWidget            *m_attrRow      = nullptr;   // Slice DM.2 — hidden when no provider
    QWidget            *m_graduatedBox = nullptr;
    // Gap A4.5 — output-axis row, archetype-gated (size → points,
    // width → lines, hidden for polygons).
    QWidget            *m_axisRow      = nullptr;
    QCheckBox          *m_axisCheck    = nullptr;
    QDoubleSpinBox     *m_axisMinSpin  = nullptr;
    QDoubleSpinBox     *m_axisMaxSpin  = nullptr;

    // Slice US.1 — shared classification block.
    ClassificationEditor                     *m_classEditor = nullptr;
    std::unique_ptr<GraduatedRendererBinding> m_binding;

    // Re-entrancy guard while rebuilding the controls from the renderer.
    bool m_suppressEdits = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_KINDRENDERERPANEL_H
