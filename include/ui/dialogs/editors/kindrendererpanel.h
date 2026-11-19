/**
 * @file      kindrendererpanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Renderer-mode chooser + classification controls (Slice S2+S5).
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace OpenSWMM::Render
{
class IFeatureRenderer;
class Rule; // Slice B.6a — Rule-aware ctor below.
}

namespace openswmmvis::ui
{

class ClassificationEditor;
class FlowArrowEditor;
class GraduatedRendererBinding;

class KindRendererPanel : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build a per-kind renderer editor for a borrowed model/results host.
     * @param hostLayer Optional raw host; only SWMMModelLayer/SWMMResultsLayer supply legacy
     * renderer slots. Any nonnull host and its styles must outlive panel/control callbacks.
     * @param category Fixed category used for renderer, attribute-provider and archetype lookup.
     * @param parent Optional Qt owner; caller owns parentless panel.
     * Builds owned controls/binding and immediately refreshes; refresh can install a shown
     * first numeric attribute when a graduated renderer has none. All edits are live with
     * host notifications; no undo/snapshot/Cancel policy or host-destruction tracking.
     */
    KindRendererPanel(OpenSWMMVisLayer *hostLayer, OpenSWMMVis::SwmmCategory category,
                      QWidget *parent = nullptr);

    /**
     * @brief Build a rule-bound editor through a hostless Junctions-sentinel context, then
     * refresh for the rule.
     * @param rule Optional borrowed raw rule, required to outlive panel and all callbacks.
     * Nonnull rule renderer takes priority over layer slots; no ownership transfer.
     * @param parent Optional Qt owner; caller owns parentless panel.
     * Rule-only context can discover a provider by two-parent ancestry but keeps CatJunctions
     * for category/archetype and builds its arrow group before rule assignment with no host.
     * Use the contextual overload for accurate per-kind arrows/archetype. No undo/snapshot.
     */
    explicit KindRendererPanel(OpenSWMM::Render::Rule *rule, QWidget *parent = nullptr);

    /**
     * @brief Build a rule-first renderer editor while retaining explicit host/kind context for
     * nonrenderer properties.
     * @param rule Optional borrowed raw rule; when nonnull all renderer reads/installs/resets
     * route through it. Must outlive panel and callbacks.
     * @param hostLayer Optional borrowed raw host supplying attribute/category/arrow channels;
     * must outlive panel and embedded editors even when renderer writes use the rule.
     * @param category Fixed host kind/archetype. Caller ensures host/rule represent the same
     * logical target; no consistency validation.
     * @param parent Optional Qt owner; caller owns parentless panel.
     * Delegates host UI construction/initial refresh before assigning rule, then refreshes
     * again. Either refresh can seed a missing graduated attribute. No undo or Cancel snapshot.
     */
    KindRendererPanel(OpenSWMM::Render::Rule *rule, OpenSWMMVisLayer *hostLayer,
                      OpenSWMMVis::SwmmCategory category, QWidget *parent = nullptr);

    // Out-of-line so unique_ptr<GraduatedRendererBinding> (forward-declared)
    // is destroyed where the type is complete.
    /**
     * @brief Release the owned classification binding and normal child widgets.
     * Does not own/destroy host/rule/renderers, restore snapshots or revert live edits.
     * Binding callbacks capture this and are valid only during panel lifetime.
     */
    ~KindRendererPanel() override;

    /**
     * @brief Read live renderer/provider state and rebuild picker, shared classification, axes
     * and arrows.
     * Blocks picker/control notifications while seeding ranges; preserves unknown nonempty
     * attributes, excludes only QString metadata from numeric choices and hides axes for
     * polygon/results hosts. Display pixels clamp to0.1..256/one decimal. Resyncs binding,
     * refreshes shared editor/arrows and emits outputAxisToggled unconditionally. If a
     * Graduated renderer has no attribute but a field is displayed, installs that first
     * field last, so this refresh/constructor can mutate renderer state and notify views.
     * Does not subscribe to host changes by itself; callers trigger refresh after rollback.
     */
    void refreshFromModel();

    /**
     * @brief Report the visible-to-own-parent output-axis row's checked state.
     * @return True only with an existing locally visible axis row and checked control;
     * this is widget state, not a fresh renderer/property query or proof the whole dialog is
     * visible. Results-layer size channels use another editor and return false here.
     */
    [[nodiscard]] bool sizeByValueEnabled() const;

signals:
    /**
     * @brief Notify parent editors that static-size enablement may need updating.
     * @param on Current axis toggle or sizeByValueEnabled() result supplied by emitter.
     * Emitted on every refresh and before installing an output-axis edit; not a success or
     * changed-value-only notification, and handlers must respect panel/target lifetime.
     */
    void outputAxisToggled(bool on);

private slots:
    /**
     * @brief Replace/reset the renderer according to the selected mode and refresh controls.
     * @param comboRow Row whose stored integer chooses None/Graduated/Categorized. Invalid
     * row data converts to zero and takes the reset path; no range/applicability check.
     * None resets host defaults or installs an unseeded default SingleSymbolRenderer on a
     * rule. Other modes use the shared factory with outgoing symbol/provider fields, then
     * transfer ownership if construction succeeds. No same-mode guard, undo or result.
     */
    void onModeChanged(int comboRow);
    // Slice DM.2 — attribute picker. Populated from IAttributeProvider
    // when the host layer (or the Rule's owning layer) implements it;
    // hidden otherwise.
    /**
     * @brief Clone a graduated renderer with the chosen color attribute and invalidate its
     * breaks.
     * @param comboRow Attribute item row; missing/invalid row data becomes an empty name.
     * No-op under suppression, wrong renderer class or identical field. Does not validate
     * provider membership/type. Installs clone through rule/host, then resyncs binding and
     * shared editor so dynamic range capability follows the new field. No undo/error result.
     */
    void onAttributeChanged(int comboRow);
    // Gap A4.5 — output axes: size-by-value (points) / width-by-value (lines).
    /**
     * @brief Clone a graduated point/line renderer with current enabled flag and sorted pixel
     * bounds.
     * No-op under suppression, missing graduated target or polygon archetype. Updates local
     * control enabled state and emits outputAxisToggled before installation; no results-host
     * visibility check is repeated here. Copies display values, installs clone and resyncs
     * binding without undo, success validation or automatic axis-control reseeding.
     */
    void onOutputAxisChanged();
    // Independent size attribute — "(same as color)" or a numeric field.
    /**
     * @brief Clone a graduated renderer with an independent size/width field.
     * @param comboRow Item row; empty/invalid data selects same-as-color semantics.
     * No-op under suppression, wrong class or identical name. Renderer setter invalidates
     * its sampled size range; no provider/type/axis-visibility validation or binding resync
     * is done here. Installation is live, without undo/status result.
     */
    void onSizeAttributeChanged(int comboRow);

private:
    /**
     * @brief Build owned controls, callback binding and flow-arrow group once during
     * construction.
     * Binding callbacks capture this, borrow current renderer and transfer cloned replacements;
     * attribute list callback is empty because this panel owns its picker. Dynamic range gate
     * requires results host and permits unknown/providerless attributes, otherwise follows
     * field isDynamic. Pixel factory returns axis-row-owned0.1..256/one-decimal spins.
     * Results-host arrow group is intentionally hostless because arrows live in another tab.
     * No duplicate-build guard, external host-change subscription, undo or snapshot.
     */
    void buildUi();
    /**
     * @brief Resolve the current renderer with rule, model-kind, then results-kind priority.
     * @return Borrowed renderer or null; expires upon replacement/target destruction. Does
     * not cache, clone or transfer ownership, and does not fall back from an empty rule.
     */
    OpenSWMM::Render::IFeatureRenderer *currentRenderer() const;
    /**
     * @brief Choose explicit host or discover the rule's owning layer two QObject parents upward.
     * @return Borrowed layer or null; does not verify IAttributeProvider support or category
     * consistency. Explicit host wins over rule ancestry; raw targets must remain alive.
     */
    OpenSWMMVisLayer *attributeProviderHost() const;

    OpenSWMMVisLayer *m_hostLayer = nullptr;
    SWMMModelLayer *m_modelLayer = nullptr;     // either m_modelLayer
    SWMMResultsLayer *m_resultsLayer = nullptr; // or m_resultsLayer is set
    OpenSWMMVis::SwmmCategory m_category = OpenSWMMVis::CatJunctions;

    // Rule-aware mode: renderer reads/writes prefer this pointer. Contextual
    // construction also retains model/results pointers for the kind channels.
    OpenSWMM::Render::Rule *m_rule = nullptr;

    // Controls — 1D glue retained by this panel.
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_attrCombo = nullptr; // Slice DM.2
    QWidget *m_attrRow = nullptr;     // Slice DM.2 — hidden when no provider
    QWidget *m_graduatedBox = nullptr;
    // Gap A4.5 — output-axis row, archetype-gated (size → points,
    // width → lines, hidden for polygons).
    QWidget *m_axisRow = nullptr;
    QCheckBox *m_axisCheck = nullptr;
    QDoubleSpinBox *m_axisMinSpin = nullptr;
    QDoubleSpinBox *m_axisMaxSpin = nullptr;
    // Independent size attribute picker ("Size by:") — row hidden when the
    // axis row is hidden; combo disabled until the axis toggle is on.
    QWidget *m_sizeAttrRow = nullptr;
    QComboBox *m_sizeAttrCombo = nullptr;

    // Flow-direction arrows — a property of the LINK KIND, not of the
    // renderer, so it belongs on every editor a link kind can mount. The
    // single-symbol editors (SwmmElementSymbolEditor for model layers,
    // the Line feature-style editor for results layers) each carry their
    // own copy; this one covers the graduated / categorized panels, which
    // otherwise made the arrows vanish the moment a conduit was themed.
    // Bound to whichever channel the host exposes — see FlowArrowEditor
    // for the two-channel split. Hidden on results layers, whose kind page
    // carries an Arrows tab.
    FlowArrowEditor *m_arrowBox = nullptr;

    // Slice US.1 — shared classification block.
    ClassificationEditor *m_classEditor = nullptr;
    std::unique_ptr<GraduatedRendererBinding> m_binding;

    // Re-entrancy guard while rebuilding the controls from the renderer.
    bool m_suppressEdits = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_KINDRENDERERPANEL_H
