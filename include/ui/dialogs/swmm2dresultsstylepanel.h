/*!
 * \file   swmm2dresultsstylepanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  O2-1 — layer-level display controls for a SWMM2DResultsLayer
 *         (depth range / ramp mode / classes, filled contours, isolines,
 *         velocity arrows). Per-sublayer colour/shape styling is edited via
 *         the layer-tree sub-rows + the SE.4 sublayer editors; this panel
 *         covers the layer-scope knobs that previously had no UI.
 *
 *         Bindings are live: every control writes through the layer's setter
 *         (which repaints), matching the apply-on-edit model used elsewhere.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H

#include <QWidget>
#include <QPointer>

class SWMM2DResultsLayer;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QComboBox;

namespace openswmmvis::ui {

class ColorButton;

class Swmm2DResultsStylePanel : public QWidget
{
    Q_OBJECT
public:
    explicit Swmm2DResultsStylePanel(SWMM2DResultsLayer *layer, QWidget *parent = nullptr);

private:
    void refreshFromLayer();

    QPointer<SWMM2DResultsLayer> m_layer;
    QDoubleSpinBox *m_dryDepth   = nullptr;
    QDoubleSpinBox *m_maxDepth   = nullptr;
    QComboBox      *m_rampStyle  = nullptr;
    QSpinBox       *m_classes    = nullptr;
    QCheckBox      *m_bandsOn     = nullptr;
    QSpinBox       *m_bandLevels  = nullptr;
    QDoubleSpinBox *m_bandOpacity = nullptr;
    QCheckBox      *m_isoOn       = nullptr;
    QSpinBox       *m_isoLevels   = nullptr;
    ColorButton    *m_isoColor    = nullptr;
    QDoubleSpinBox *m_isoWidth    = nullptr;
    QCheckBox      *m_velOn       = nullptr;
    QDoubleSpinBox *m_velOpacity  = nullptr;
    QDoubleSpinBox *m_velScale    = nullptr;
    QDoubleSpinBox *m_velMax      = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H
