/**
 * @file      dashstylecombo.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Combo box rendering each Qt::PenStyle as the icon of its row,
 *         mirroring QGIS's "Stroke style" picker.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_DASHSTYLECOMBO_H
#define OPENSWMMVIS_UI_WIDGETS_DASHSTYLECOMBO_H

#include <QComboBox>
#include <Qt>

namespace openswmmvis::ui
{

class DashStyleCombo : public QComboBox
{
    Q_OBJECT
public:
    /**
     * @brief Populate standard solid/dash/dot/dash-dot/dash-dot-dot/none pen styles and forward selection changes.
     * @param[in] parent Optional QWidget owner; initial row is Solid.
     */
    explicit DashStyleCombo(QWidget *parent = nullptr);

    /**
     * @brief Read the selected row's pen-style payload.
     * @return Selected Qt::PenStyle, or SolidLine if no row is selected.
     */
    [[nodiscard]] Qt::PenStyle penStyle() const;
    /**
     * @brief Select the first row matching a pen style, with normal Qt selection notifications.
     * @param[in] s Style payload; unsupported/custom styles leave the selection unchanged.
     */
    void setPenStyle(Qt::PenStyle s);

signals:
    /**
     * @brief Announce the selected pen style after a combo index change.
     * @param[in] s Current pen style; SolidLine fallback when selection is absent.
     */
    void penStyleChanged(Qt::PenStyle s);

private:
    /**
     * @brief Render a small horizontal line swatch for one Qt pen style.
     * @param[in] s Style applied to a 1.8-pixel-wide pen.
     * @param[in] wPx Positive preview width in logical pixels.
     * @param[in] hPx Positive preview height in logical pixels.
     * @return Transparent-background icon; NoPen yields no line.
     */
    static QIcon iconFor(Qt::PenStyle s, int wPx = 60, int hPx = 18);
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_DASHSTYLECOMBO_H
