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
 *         picker and expression builder, and visibility scale window.
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

#include <QWidget>
#include <QPointer>

class OpenSWMMVisLayer;

namespace openswmmvis::ui {

class LabelConfigEditor;

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
    QPointer<OpenSWMMVisLayer> m_layer;
    LabelConfigEditor *m_editor = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LABELSTAB_H
