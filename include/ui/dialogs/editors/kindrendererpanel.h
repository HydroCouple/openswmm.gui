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
class ColorButton;
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

    /*! Rule-aware constructor that ALSO knows which kind of which layer the
     *  Rule mirrors.
     *
     *  The renderer still reads and writes through \p rule (it takes priority
     *  in currentRenderer / installRenderer / resetToDefaults, so behaviour is
     *  unchanged). The extra context exists for the controls that are NOT
     *  renderer state and therefore cannot be reached through a Rule at all —
     *  today that is the flow-direction-arrow group, which lives on the
     *  layer's per-kind channel (SWMMElementSymbol for model layers,
     *  LineFeatureSublayerStyle for results layers).
     *
     *  Without this, the rule-only constructor left the panel with a null host
     *  and a `CatJunctions` sentinel category, so `isLinkKind()` was false and
     *  the arrow group stayed hidden for every rule-backed kind — which is
     *  every kind on a results layer. */
    KindRendererPanel(OpenSWMM::Render::Rule *rule,
                      OpenSWMMVisLayer *hostLayer,
                      OpenSWMMVis::SwmmCategory category,
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
    /*! Pushes the arrow controls' values onto the host's arrow channel. */
    void onArrowsChanged();
    // Independent size attribute — "(same as color)" or a numeric field.
    void onSizeAttributeChanged(int comboRow);

private:
    void buildUi();
    OpenSWMM::Render::IFeatureRenderer *currentRenderer() const;
    /*! IAttributeProvider host — the layer itself, or the Rule's owning
     *  layer (two QObject parents up). */
    OpenSWMMVisLayer *attributeProviderHost() const;

    /*! Seeds the arrow controls from the host and shows the group only for
     *  link kinds that actually have an arrow channel. */
    void syncArrowsFromHost();

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
    // Independent size attribute picker ("Size by:") — row hidden when the
    // axis row is hidden; combo disabled until the axis toggle is on.
    QWidget            *m_sizeAttrRow   = nullptr;
    QComboBox          *m_sizeAttrCombo = nullptr;

    // Flow-direction arrows — a property of the LINK KIND, not of the
    // renderer, so it belongs on every editor a link kind can mount. The
    // single-symbol editors (SwmmElementSymbolEditor for model layers,
    // the Line feature-style editor for results layers) each carry their
    // own copy; this one covers the graduated / categorized panels, which
    // otherwise made the arrows vanish the moment a conduit was themed.
    // Bound to whichever channel the host exposes — see
    // syncArrowsFromHost() for the two-channel split.
    QWidget            *m_arrowBox      = nullptr;
    QCheckBox          *m_arrowShowChk  = nullptr;
    QDoubleSpinBox     *m_arrowSizeSpin = nullptr;
    ColorButton        *m_arrowColorBtn = nullptr;

    // Slice US.1 — shared classification block.
    ClassificationEditor                     *m_classEditor = nullptr;
    std::unique_ptr<GraduatedRendererBinding> m_binding;

    // Re-entrancy guard while rebuilding the controls from the renderer.
    bool m_suppressEdits = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_KINDRENDERERPANEL_H
