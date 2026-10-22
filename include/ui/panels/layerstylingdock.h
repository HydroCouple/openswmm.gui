/**
 * @file      layerstylingdock.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Always-open layer-styling dock (Slice Z.18).
 *
 *         RENDERING_RULE_MODEL_PLAN.md §13 — a docked, non-modal variant
 *         of the LayerStyleDialog's Symbology tab. Lives in the main
 *         window so users can leave it open and edit a layer's styling
 *         while panning / zooming the map.
 *
 *         Live preview is automatic — the Rule Model's signal cascade
 *         (Rule::rendererReplaced → layer's render path → canvas
 *         repaint) already drives the map on every edit. The dock just
 *         hosts the editor widget; no extra "apply" plumbing is needed.
 *
 *         When the active layer has no RuleList (e.g. basemap / WMS),
 *         the dock shows a placeholder message instead of swapping in
 *         an empty editor.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_LAYERSTYLINGDOCK_H
#define OPENSWMMVIS_UI_PANELS_LAYERSTYLINGDOCK_H

#include <QDockWidget>
#include <QPointer>

class OpenSWMMVisLayer;
class QLabel;
class QStackedWidget;

namespace openswmmvis::ui
{

class RuleSymbologyTab;

class LayerStylingDock : public QDockWidget
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QWidget owner.
     * @brief Create left/right-capable styling dock with owned scroll/header/placeholder body, no
     *   active layer. GUI-thread only.
     */
    explicit LayerStylingDock(QWidget *parent = nullptr);
    /**
     * @brief Destroy owned rule tab/widgets/connections; guarded layer and its RuleList stay
     *   externally owned. No explicit commit/discard or engine save.
     */
    ~LayerStylingDock() override;

public slots:
    /*! Re-target the dock at \p layer. Pass nullptr to clear (show the
     *  placeholder). Idempotent — re-setting the same layer is a no-op.
     * @param[in] layer Optional borrowed layer guarded by QPointer, null requests placeholder.
     * @note Equal pointer no-op; stores then rebuilds tab from current ruleList. No subscription to
     *   layer rename/ruleList replacement/destroyed beyond guard, so header/tab refresh requires
     *   retarget/rebuild. Does not own layer or provide undo/apply plumbing.
     */
    void setLayer(OpenSWMMVisLayer *layer);

private:
    /**
     * @brief Create and mount owned scrollable header/stack/placeholder controls once during
     *   construction.
     * @note Not idempotent rebuild API; calling again replaces body/field pointers. No layer
     *   RuleList edits or external model binding.
     */
    void buildUi();

    /*! Tear down the current RuleSymbologyTab (if any) and re-mount the
     *  body content for m_layer.
     * @brief Remove old symbology tab and queue deletion, refresh header, then create
     *   RuleSymbologyTab for live layer RuleList or show placeholder.
     * @note Does not clone rule list or stage a transaction; edits through new tab apply live via
     *   rule model signals. Previous tab deferred lifetime/receiver connections persist until
     *   deletion. GUI-thread serialized layer/list lifetime required.
     */
    void rebuild();

    /*! Display string for the header label.
     * @return Translated no-layer header or current layer name by value. No automatic rename
     *   subscription, ownership or mutation.
     */
    [[nodiscard]] QString headerTextForCurrent() const;

    QPointer<OpenSWMMVisLayer> m_layer;

    QLabel *m_header = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_placeholder = nullptr;
    RuleSymbologyTab *m_tab = nullptr; // recreated per layer
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_LAYERSTYLINGDOCK_H
