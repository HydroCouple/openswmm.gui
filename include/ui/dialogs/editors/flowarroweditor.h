/**
 * @file      flowarroweditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     "Flow direction arrows" group for a SWMM link kind.
 *
 *         Arrows are a property of the LINK KIND, not of the renderer, and
 *         two channels carry them, chosen by the host:
 *           - model layers   → SWMMElementSymbol::showArrows / arrowSize /
 *                              arrowColor, through the layer's per-kind
 *                              setters (which resync the persistent symbol
 *                              adapter the single-symbol editor and Cancel
 *                              rollback use);
 *           - results layers → LineFeatureSublayerStyle::showFlowArrows /
 *                              arrowLengthPx / arrowWidthPx / arrowColor.
 *         Anything else (no host, non-link kind) has no channel and the
 *         group hides itself.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_FLOWARROWEDITOR_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_FLOWARROWEDITOR_H

#include "layers/swmm_category.h"

#include <QGroupBox>

class OpenSWMMVisLayer;
class SWMMModelLayer;
class SWMMResultsLayer;

class QCheckBox;
class QDoubleSpinBox;

namespace openswmmvis::ui
{

class ColorButton;

class FlowArrowEditor : public QGroupBox
{
    Q_OBJECT
public:
    /**
     * @brief Build a live per-kind arrow editor and seed controls from its host channel.
     * @param hostLayer Optional borrowed model/results layer; other/null hosts have no
     * channel and hide the group. Stored typed raw pointers require the host to outlive
     * this widget and all refresh/edit callbacks; use on GUI thread.
     * @param category Fixed SWMM kind; only conduit/pump/orifice/weir/outlet expose arrows.
     * @param parent Optional Qt owner; caller owns a parentless editor.
     * Creates owned widgets with2..60pixel, one-decimal length/width controls and connects
     * user changes to immediate host setters. Does not subscribe to external host changes,
     * create undo commands, snapshot styles or roll back edits on destruction.
     */
    FlowArrowEditor(OpenSWMMVisLayer *hostLayer, OpenSWMMVis::SwmmCategory category,
                    QWidget *parent = nullptr);

    /**
     * @brief Check whether the bound kind/host currently exposes an arrow channel.
     * @return True for a link kind on any model host or a results host with a line feature
     * style; false otherwise. No engine/result-data availability check or ownership transfer.
     * Requires stored raw hosts to remain alive; does not change widget visibility itself.
     */
    [[nodiscard]] bool hasChannel() const;

    /**
     * @brief Read current channel values, show/hide the group and seed controls without
     * triggering host writes.
     * A missing channel hides the group and leaves previous control values/enabled states.
     * For a channel, blocks child signals, applies2..60pixel/one-decimal display constraints
     * and enables size/width/color only when arrows are on. Does not preserve out-of-range
     * or extra-precision values in controls; later edits write the full displayed tuple.
     */
    void refreshFromModel();

private:
    /**
     * @brief Write the complete current arrow tuple immediately through the host's live setters.
     * Updates control enabled state, then calls four model-kind setters or available result
     * line-style setters. No hasChannel/link-kind guard is repeated here; normal signals
     * come from the constructed group. All displayed/clamped values are submitted, even
     * when only one control changed. No atomic transaction, undo, snapshot or error result.
     */
    void onChanged();

    SWMMModelLayer *m_modelLayer = nullptr;
    SWMMResultsLayer *m_resultsLayer = nullptr;
    OpenSWMMVis::SwmmCategory m_category = OpenSWMMVis::CatJunctions;

    QCheckBox *m_showChk = nullptr;
    QDoubleSpinBox *m_sizeSpin = nullptr; ///< length ALONG the link
    QDoubleSpinBox *m_widSpin = nullptr;  ///< width ACROSS the link
    ColorButton *m_colorBtn = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_FLOWARROWEDITOR_H
