/*!
 * \file   symbolstyleeditors.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  SE.1–SE.3 — dedicated IStyleEditorWidget editors for the rule-path
 *         *SymbolStyleAdapter family (Point / Line / Polygon).
 *
 *         These are the polished editors mounted by SingleSymbolPanel for a
 *         Rule's SingleSymbolRenderer. They bind directly to the adapter's
 *         Q_PROPERTYs (which write into the rule's renderer and fire
 *         Rule::notifyRendererStateChanged → the SWMMModelLayer per-kind
 *         bridge → legacy struct → repaint), and refresh on the adapter's
 *         changed() signal.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS_H

#include "ui/dialogs/istyleeditorwidget.h"

namespace OpenSWMM::Render {
class PointSymbolStyleAdapter;
class LineSymbolStyleAdapter;
class PolygonSymbolStyleAdapter;
}

class QDoubleSpinBox;

namespace openswmmvis::ui {

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
    explicit PointSymbolStyleEditor(OpenSWMM::Render::PointSymbolStyleAdapter *adapter,
                                    QWidget *parent = nullptr);
    void refreshFromModel() override;

private:
    OpenSWMM::Render::PointSymbolStyleAdapter *m_a = nullptr;
    MarkerShapeCombo *m_shape       = nullptr;
    QDoubleSpinBox   *m_size        = nullptr;
    ColorButton      *m_fill        = nullptr;
    ColorButton      *m_outline     = nullptr;
    QDoubleSpinBox   *m_outlineW    = nullptr;
    QDoubleSpinBox   *m_opacity     = nullptr;
};

// ---------------------------------------------------------------------------
// SE.2 — Line archetype (conduits, pumps, orifices, weirs, outlets)
// ---------------------------------------------------------------------------
class LineSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit LineSymbolStyleEditor(OpenSWMM::Render::LineSymbolStyleAdapter *adapter,
                                   QWidget *parent = nullptr);
    void refreshFromModel() override;

private:
    OpenSWMM::Render::LineSymbolStyleAdapter *m_a = nullptr;
    ColorButton    *m_color    = nullptr;
    QDoubleSpinBox *m_width     = nullptr;
    DashStyleCombo *m_dash      = nullptr;
    QDoubleSpinBox *m_opacity   = nullptr;
};

// ---------------------------------------------------------------------------
// SE.3 — Polygon archetype (subcatchments)
// ---------------------------------------------------------------------------
class PolygonSymbolStyleEditor : public IStyleEditorWidget
{
    Q_OBJECT
public:
    explicit PolygonSymbolStyleEditor(OpenSWMM::Render::PolygonSymbolStyleAdapter *adapter,
                                      QWidget *parent = nullptr);
    void refreshFromModel() override;

private:
    OpenSWMM::Render::PolygonSymbolStyleAdapter *m_a = nullptr;
    ColorButton    *m_fill      = nullptr;
    ColorButton    *m_outline   = nullptr;
    QDoubleSpinBox *m_outlineW  = nullptr;
    QDoubleSpinBox *m_opacity   = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_SYMBOLSTYLEEDITORS_H
