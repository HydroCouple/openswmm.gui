/**
 * @file      symbolstyleeditors.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     SE.1–SE.3 — dedicated IStyleEditorWidget editors for the rule-path
 *         *SymbolStyleAdapter family (Point / Line / Polygon).
 *
 *         These are the polished editors mounted by SingleSymbolPanel for a
 *         Rule's SingleSymbolRenderer. They bind directly to the adapter's
 *         Q_PROPERTYs (which write into the rule's renderer and fire
 *         Rule::notifyRendererStateChanged → the SWMMModelLayer per-kind
 *         bridge → legacy struct → repaint), and refresh on the adapter's
 *         changed() signal.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS_H

#include "ui/dialogs/istyleeditorwidget.h"

namespace OpenSWMM::Render
{
class PointSymbolStyleAdapter;
class LineSymbolStyleAdapter;
class PolygonSymbolStyleAdapter;
}

class QDoubleSpinBox;

namespace openswmmvis::ui
{

class ColorButton;
class MarkerShapeCombo;
class DashStyleCombo;

// ---------------------------------------------------------------------------
// SE.1 — Point archetype (junctions, outfalls, storage, dividers, rain gages)
// ---------------------------------------------------------------------------
class PointSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build a live rule-point symbol editor with marker, fill/outline and opacity
     * controls.
     * @param adapter Optional borrowed raw adapter; null bindings leave writes/refresh inert.
     * A nonnull adapter must outlive the widget since destruction does not null the pointer.
     * @param parent Optional Qt owner; caller owns a parentless editor.
     * Seeds controls under blockers and refreshes on changed(). Individual edits write only
     * their field immediately; opacity percent maps to0..1. Display bounds are size0.5..60
     * pixels/one decimal, outline0..20/two decimals and integer0..100percent. Color alpha is
     * available. No undo, snapshot or Cancel rollback is supplied here; GUI thread only.
     */
    explicit PointSymbolStyleEditor(OpenSWMM::Render::PointSymbolStyleAdapter *adapter,
                                    QWidget *parent = nullptr);
    /**
     * @brief Read the live point adapter into controls without emitting their edit notifications.
     * No-op for null target; a nonnull raw adapter must remain alive. Applies widget display
     * rounding/clamping and opacity-times100 conversion without writing target values.
     * Does not replace the adapter or own its renderer/rule.
     */
    void refreshFromModel() override;

private:
    OpenSWMM::Render::PointSymbolStyleAdapter *m_a = nullptr;
    MarkerShapeCombo *m_shape = nullptr;
    QDoubleSpinBox *m_size = nullptr;
    ColorButton *m_fill = nullptr;
    ColorButton *m_outline = nullptr;
    QDoubleSpinBox *m_outlineW = nullptr;
    QDoubleSpinBox *m_opacity = nullptr;
};

// ---------------------------------------------------------------------------
// SE.2 — Line archetype (conduits, pumps, orifices, weirs, outlets)
// ---------------------------------------------------------------------------
class LineSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build a live rule-line editor for alpha-enabled color, width, dash and opacity.
     * @param adapter Optional borrowed raw adapter; null makes writes/refresh inert. Nonnull
     * adapter must outlive this widget and callbacks.
     * @param parent Optional Qt owner; caller owns parentless widget.
     * Width uses0.25..30pixels/two decimals; integer0..100percent opacity maps to0..1. Edits
     * write one property immediately and changed() refreshes controls under blockers; no
     * undo, snapshot or Cancel rollback here. GUI thread only.
     */
    explicit LineSymbolStyleEditor(OpenSWMM::Render::LineSymbolStyleAdapter *adapter,
                                   QWidget *parent = nullptr);
    /**
     * @brief Read line color/width/dash and opacity into controls under signal blockers.
     * No-op for null adapter; requires a nonnull raw target to remain alive. Does not apply
     * display rounding/clamping to stored state or mutate renderer ownership.
     */
    void refreshFromModel() override;

private:
    OpenSWMM::Render::LineSymbolStyleAdapter *m_a = nullptr;
    ColorButton *m_color = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    DashStyleCombo *m_dash = nullptr;
    QDoubleSpinBox *m_opacity = nullptr;
};

// ---------------------------------------------------------------------------
// SE.3 — Polygon archetype (subcatchments)
// ---------------------------------------------------------------------------
class PolygonSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build a live rule-polygon editor for fill, outline width/color and overall opacity.
     * @param adapter Optional borrowed raw adapter; null makes edits/refresh inert. Nonnull
     * adapter must outlive this widget and callbacks.
     * @param parent Optional Qt owner; caller owns parentless widget.
     * Alpha-enabled color buttons and0..20pixel/two-decimal outline plus integer0..100percent
     * opacity write individual properties immediately. Refreshes on changed(), blocking only
     * control signals; no undo, snapshot or Cancel policy. GUI thread only.
     */
    explicit PolygonSymbolStyleEditor(OpenSWMM::Render::PolygonSymbolStyleAdapter *adapter,
                                      QWidget *parent = nullptr);
    /**
     * @brief Seed polygon fill/outline/width and percentage opacity without writing the adapter.
     * No-op for null, otherwise requires raw target lifetime. Display range/rounding can
     * differ from stored precision; no snapshot or renderer ownership transfer.
     */
    void refreshFromModel() override;

private:
    OpenSWMM::Render::PolygonSymbolStyleAdapter *m_a = nullptr;
    ColorButton *m_fill = nullptr;
    ColorButton *m_outline = nullptr;
    QDoubleSpinBox *m_outlineW = nullptr;
    QDoubleSpinBox *m_opacity = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS_H
