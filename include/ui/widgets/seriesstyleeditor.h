/*!
 * \file   seriesstyleeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BL-Polish — full-featured per-series style editor widget.
 *
 * Replaces ComparisonPlotDialog's stop-gap double-click-to-recolor with a
 * proper editor surface that exposes every field of `SeriesStyle`:
 *   - Colour swatch (opens QColorDialog)
 *   - Line: visible toggle / width spin / dash combo
 *   - Marker: visible toggle / shape combo / size spin
 *   - Opacity slider
 *   - Legend name override
 *
 * The widget edits a local `SeriesStyle` and emits `styleChanged(SeriesStyle)`
 * after every keystroke / value-change — the dialog can hook this signal
 * to live-update the rendered chart, then commit the final style to the
 * ComparisonPlotModel on close. Used standalone in a tiny QDialog wrapper
 * launched from the SeriesPanel's right-click "Style…" entry.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_SERIESSTYLEEDITOR_H
#define OPENSWMMVIS_UI_WIDGETS_SERIESSTYLEEDITOR_H

#include "plot/seriesstyle.h"

#include <QWidget>

class QPushButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSlider;
class QLabel;

namespace openswmmvis::ui {

class SeriesStyleEditor : public QWidget
{
    Q_OBJECT
public:
    explicit SeriesStyleEditor(QWidget *parent = nullptr);

    /*! \brief Replace the current style; updates all controls.
     *  Suppresses signals while widgets are repopulated. */
    void setStyle(const openswmmvis::plot::SeriesStyle& style);

    /*! \brief Current style as edited by the user. */
    [[nodiscard]] openswmmvis::plot::SeriesStyle style() const { return m_style; }

signals:
    /*! \brief Emitted whenever any control changes value. */
    void styleChanged(const openswmmvis::plot::SeriesStyle& style);

private slots:
    void onColorClicked();
    void onAnyControlChanged();

private:
    void buildUi();
    void pushStyleToControls();
    QString swatchStyleSheet(const QColor& c) const;

    openswmmvis::plot::SeriesStyle m_style;
    bool m_suppressSignals = false;

    QPushButton    *m_colorBtn      = nullptr;
    QCheckBox      *m_lineVisible   = nullptr;
    QDoubleSpinBox *m_lineWidth     = nullptr;
    QComboBox      *m_dashCombo     = nullptr;
    QCheckBox      *m_markerVisible = nullptr;
    QComboBox      *m_shapeCombo    = nullptr;
    QDoubleSpinBox *m_markerSize    = nullptr;
    QSlider        *m_opacitySlider = nullptr;
    QLabel         *m_opacityLabel  = nullptr;
    QLineEdit      *m_legendEdit    = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_SERIESSTYLEEDITOR_H
