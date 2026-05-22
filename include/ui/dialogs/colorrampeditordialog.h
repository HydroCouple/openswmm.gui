/*!
 * \file   colorrampeditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BB — RasterColorRamp editor with 5-interval framed
 *         intervals + per-attribute persistence.
 *
 * Edits the colour ramp on a SWMMResultsLayer (or any layer-like object
 * carrying a RasterColorRamp). Layout:
 *   ┌─ Min / Max + Stretch button ──────────────────────────┐
 *   │  Min: [    ]  Max: [    ]  [Auto-stretch from data]  │
 *   ├───────────────────────────────────────────────────────┤
 *   │  Preset ramp ▾    [Reverse]  [Discrete N intervals]  │
 *   │   ▢ Viridis  ▢ Cividis  ▢ Plasma  ▢ Magma  ▢ Turbo   │
 *   │   ▢ Blue-Red (diverging)  ▢ Custom 5-color           │
 *   ├───────────────────────────────────────────────────────┤
 *   │  Interval count: [5]  [   gradient preview   ]       │
 *   ├───────────────────────────────────────────────────────┤
 *   │  Per-interval colours (when Discrete):               │
 *   │   ◼ ◼ ◼ ◼ ◼  ← five clickable swatches               │
 *   └───────────────────────────────────────────────────────┘
 *
 * Settings round-trip through QJsonObject so per-attribute ramps
 * survive across sessions and `.oswp` projects.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_COLORRAMPEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_COLORRAMPEDITORDIALOG_H

#include "layers/gisrasterlayer.h"   // RasterColorRamp

#include <QDialog>
#include <QPointer>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class SWMMResultsLayer;

namespace openswmmvis::ui {

class ColorRampEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ColorRampEditorDialog(SWMMResultsLayer *layer,
                                    QWidget *parent = nullptr);
    ~ColorRampEditorDialog() override;

    /*! \brief Current edited ramp (committed to the layer on accept). */
    [[nodiscard]] RasterColorRamp ramp() const { return m_ramp; }

private slots:
    void onAutoStretchClicked();
    void onPresetChanged(int index);
    void onIntervalsChanged(int n);
    void onReverseToggled(bool on);
    void onDiscreteToggled(bool on);
    void onMinChanged(double v);
    void onMaxChanged(double v);
    void onSwatchClicked();
    void accept() override;

private:
    void buildUi();
    void applyPreset(const QString &key);
    void rebuildSwatches();
    void refreshGradientPreview();
    QString swatchStyleSheet(const QColor &c) const;

    QPointer<SWMMResultsLayer> m_layer;
    RasterColorRamp            m_ramp;

    QDoubleSpinBox *m_minSpin    = nullptr;
    QDoubleSpinBox *m_maxSpin    = nullptr;
    QPushButton    *m_autoBtn    = nullptr;
    QComboBox      *m_presetCombo = nullptr;
    QCheckBox      *m_reverseCb  = nullptr;
    QCheckBox      *m_discreteCb = nullptr;
    QSpinBox       *m_intervalsSpin = nullptr;
    QLabel         *m_gradientLabel = nullptr;
    QWidget        *m_swatchHost   = nullptr;
    QVector<QPushButton *> m_swatchButtons;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_COLORRAMPEDITORDIALOG_H
