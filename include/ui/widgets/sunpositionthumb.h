/**
 * @file      sunpositionthumb.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Compact thumbnail rendering the hillshade sun's azimuth +
 *         altitude on a compass dial. Pure repaint; no user interaction.
 *         Extracted from the retired MeshHillshadeEditor so the mesh
 *         styling panel's Terrain Fill tab can host it.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_SUNPOSITIONTHUMB_H
#define OPENSWMMVIS_UI_WIDGETS_SUNPOSITIONTHUMB_H

#include <QWidget>

namespace openswmmvis::ui
{

class SunPositionThumb : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create a passive hillshade direction thumbnail with azimuth 225° and altitude 35.3°.
     * @param[in] parent Optional QWidget owner.
     */
    explicit SunPositionThumb(QWidget *parent = nullptr);
    /**
     * @brief Store the compass bearing and schedule repaint without emitting a signal.
     * @param[in] degrees Clockwise angle from north, expected finite; stored without normalization/range checks.
     */
    void setAzimuth(double degrees);
    /**
     * @brief Store solar altitude and schedule repaint without emitting a signal.
     * @param[in] degrees Angle above horizon, expected finite. Rendering clamps only its radial effect to 0–90°; the displayed value remains unchanged.
     */
    void setAltitude(double degrees);

protected:
    /**
     * @brief Draw compass ticks, solar vector and rounded angle readout.
     * @param[in] e Qt paint event, unused; the whole thumbnail is redrawn.
     */
    void paintEvent(QPaintEvent *e) override;
    /**
     * @brief Supply the preferred square thumbnail extent.
     * @return 110 by 110 logical pixels.
     */
    QSize sizeHint() const override { return {110, 110}; }
    /**
     * @brief Supply the minimum square layout hint.
     * @return 80 by 80 logical pixels.
     */
    QSize minimumSizeHint() const override { return {80, 80}; }

private:
    double m_azimuth = 225.0;
    double m_altitude = 35.3;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_SUNPOSITIONTHUMB_H
