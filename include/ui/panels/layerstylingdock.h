/*!
 * \file   layerstylingdock.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Always-open layer-styling dock (Slice Z.18).
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
 */

#ifndef OPENSWMMVIS_UI_PANELS_LAYERSTYLINGDOCK_H
#define OPENSWMMVIS_UI_PANELS_LAYERSTYLINGDOCK_H

#include <QDockWidget>
#include <QPointer>

class OpenSWMMVisLayer;
class QLabel;
class QStackedWidget;

namespace openswmmvis::ui {

class RuleSymbologyTab;

class LayerStylingDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit LayerStylingDock(QWidget *parent = nullptr);
    ~LayerStylingDock() override;

public slots:
    /*! Re-target the dock at \p layer. Pass nullptr to clear (show the
     *  placeholder). Idempotent — re-setting the same layer is a no-op. */
    void setLayer(OpenSWMMVisLayer *layer);

private:
    void buildUi();

    /*! Tear down the current RuleSymbologyTab (if any) and re-mount the
     *  body content for m_layer. */
    void rebuild();

    /*! Display string for the header label. */
    [[nodiscard]] QString headerTextForCurrent() const;

    QPointer<OpenSWMMVisLayer> m_layer;

    QLabel          *m_header     = nullptr;
    QStackedWidget  *m_stack      = nullptr;
    QWidget         *m_placeholder = nullptr;
    RuleSymbologyTab *m_tab       = nullptr;   // recreated per layer
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_LAYERSTYLINGDOCK_H
