/*!
 * \file   labelstab.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Labels tab content for LayerStyleDialog.  Slice X.18.
 *
 *         A GIS-aligned label-style editor — Enabled toggle, font family
 *         + size + bold/italic, text colour, halo (colour + radius),
 *         placement (auto / above / below / left / right / centre), field
 *         expression (vector layers only), and visibility scale window.
 *         The editor reads from and writes back through the host layer's
 *         `labelConfig()` / `setLabelConfig()` pair so the canvas + the
 *         legend dock pick up changes via the standard signal channel.
 *
 *         Hosted in the Labels tab of `LayerStyleDialog`.  Capability
 *         gating in the dialog skips this tab for layer kinds that don't
 *         paint text labels.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_LABELSTAB_H
#define OPENSWMMVIS_UI_DIALOGS_LABELSTAB_H

#include "render/labelconfig.h"

#include <QWidget>

class OpenSWMMVisLayer;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLineEdit;
class QToolButton;

namespace openswmmvis::ui {

class ColorButton;

class LabelsTab : public QWidget
{
    Q_OBJECT
public:
    explicit LabelsTab(OpenSWMMVisLayer *layer, QWidget *parent = nullptr);

private slots:
    /*! Pulls current label state from the host layer and pushes it into
     *  the widget controls.  Called on construction and when the layer
     *  emits `labelConfigChanged` (catches edits that came from another
     *  view of the same data, e.g. legend dock). */
    void refreshFromModel();

    /*! Reads control state and pushes a new LabelConfig back through
     *  the layer's setLabelConfig(). */
    void pushToModel();

private:
    OpenSWMMVisLayer *m_layer = nullptr;

    // Master
    QCheckBox       *m_enabledChk    = nullptr;
    QLineEdit       *m_fieldEdit     = nullptr;
    QComboBox       *m_fieldCombo    = nullptr;   // vector layers: pre-populated

    // Font
    QFontComboBox   *m_fontCombo     = nullptr;
    QDoubleSpinBox  *m_fontSizeSpin  = nullptr;
    QToolButton     *m_boldBtn       = nullptr;
    QToolButton     *m_italicBtn     = nullptr;
    ColorButton     *m_colorBtn      = nullptr;

    // Halo
    QCheckBox       *m_haloChk       = nullptr;
    ColorButton     *m_haloColorBtn  = nullptr;
    QDoubleSpinBox  *m_haloRadiusSpin = nullptr;

    // Placement
    QComboBox       *m_placementCombo = nullptr;

    // Scale window
    QDoubleSpinBox  *m_minScaleSpin  = nullptr;
    QDoubleSpinBox  *m_maxScaleSpin  = nullptr;

    // Slice X.24 — background frame + priority
    QCheckBox       *m_bgChk         = nullptr;
    ColorButton     *m_bgColorBtn    = nullptr;
    QDoubleSpinBox  *m_bgPaddingSpin = nullptr;
    QDoubleSpinBox  *m_bgRadiusSpin  = nullptr;
    QLineEdit       *m_priorityEdit  = nullptr;

    bool             m_suppress       = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LABELSTAB_H
