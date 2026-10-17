/*!
 * \file   flowarroweditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  "Flow direction arrows" group for a SWMM link kind.
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

namespace openswmmvis::ui {

class ColorButton;

class FlowArrowEditor : public QGroupBox
{
    Q_OBJECT
public:
    FlowArrowEditor(OpenSWMMVisLayer *hostLayer, OpenSWMMVis::SwmmCategory category,
                    QWidget *parent = nullptr);

    /*! True when the host exposes an arrow channel for this kind. */
    [[nodiscard]] bool hasChannel() const;

    /*! Re-seeds the controls from the host and shows the group only when
     *  the kind has a channel. */
    void refreshFromModel();

private:
    void onChanged();

    SWMMModelLayer   *m_modelLayer   = nullptr;
    SWMMResultsLayer *m_resultsLayer = nullptr;
    OpenSWMMVis::SwmmCategory m_category = OpenSWMMVis::CatJunctions;

    QCheckBox      *m_showChk  = nullptr;
    QDoubleSpinBox *m_sizeSpin = nullptr;   ///< length ALONG the link
    QDoubleSpinBox *m_widSpin  = nullptr;   ///< width ACROSS the link
    ColorButton    *m_colorBtn = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_FLOWARROWEDITOR_H
