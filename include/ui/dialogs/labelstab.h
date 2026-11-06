/**
 * @file      labelstab.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Labels tab content for LayerStyleDialog.  Slice X.18.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_LABELSTAB_H
#define OPENSWMMVIS_UI_DIALOGS_LABELSTAB_H

#include <QWidget>
#include <QPointer>

class OpenSWMMVisLayer;

namespace openswmmvis::ui
{

class LabelConfigEditor;

class LabelsTab : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create a live full LabelConfig editor for a host layer and subscribe to label
     * changes/destruction.
     * @param layer Optional borrowed layer tracked by QPointer; destruction disables the
     * widget and makes push/refresh no-ops. Initially null leaves controls enabled but inert.
     * @param parent Optional Qt owner; caller owns parentless tab.
     * Immediately gathers field hints and seeds config, then forwards edits directly through
     * setLabelConfig. No undo, draft Apply, snapshot or Cancel rollback supplied here;
     * external label changes replace local editor state. GUI thread only.
     */
    explicit LabelsTab(OpenSWMMVisLayer *layer, QWidget *parent = nullptr);

private slots:
    /**
     * @brief Gather field hints across all SWMM category indices and seed the layer's current
     * label config.
     * No-op for missing target. Uses IAttributeProvider when available, deduplicating exact
     * canonical field names in first-seen order across categories; does not filter by type
     * or restrict to visible categories. LabelConfigEditor seeding avoids edit notifications.
     * No target mutation, ownership transfer or persisted snapshot.
     */
    void refreshFromModel();

    /**
     * @brief Forward the current editor config immediately through a live layer's normal label
     * setter.
     * No-op after layer destruction/null binding; target supplies validation/signals. No
     * undo command, error result or independent draft/rollback semantics.
     */
    void pushToModel();

private:
    QPointer<OpenSWMMVisLayer> m_layer;
    LabelConfigEditor *m_editor = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LABELSTAB_H
