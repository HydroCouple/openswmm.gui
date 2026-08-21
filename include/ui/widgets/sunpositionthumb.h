/*!
 * \file   sunpositionthumb.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Compact thumbnail rendering the hillshade sun's azimuth +
 *         altitude on a compass dial. Pure repaint; no user interaction.
 *         Extracted from the retired MeshHillshadeEditor so the mesh
 *         styling panel's Terrain Fill tab can host it.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_SUNPOSITIONTHUMB_H
#define OPENSWMMVIS_UI_WIDGETS_SUNPOSITIONTHUMB_H

#include <QWidget>

namespace openswmmvis::ui {

class SunPositionThumb : public QWidget
{
    Q_OBJECT
public:
    explicit SunPositionThumb(QWidget *parent = nullptr);
    void setAzimuth(double degrees);
    void setAltitude(double degrees);

protected:
    void paintEvent(QPaintEvent *e) override;
    QSize sizeHint() const override { return {110, 110}; }
    QSize minimumSizeHint() const override { return {80, 80}; }

private:
    double m_azimuth  = 225.0;
    double m_altitude = 35.3;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_SUNPOSITIONTHUMB_H
