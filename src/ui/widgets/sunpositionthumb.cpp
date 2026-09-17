/*!
 * \file   sunpositionthumb.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/sunpositionthumb.h"

#include <QPaintEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace openswmmvis::ui {

SunPositionThumb::SunPositionThumb(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
}

void SunPositionThumb::setAzimuth(double degrees)  { m_azimuth = degrees;  update(); }
void SunPositionThumb::setAltitude(double degrees) { m_altitude = degrees; update(); }

void SunPositionThumb::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Compass dial — a circle with N/E/S/W ticks.
    const QPointF c = QPointF(width(), height()) * 0.5;
    const double r = std::min(width(), height()) * 0.42;
    p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    p.setBrush(QColor(245, 245, 245));
    p.drawEllipse(c, r, r);

    // Cardinal direction labels.
    const QFont prev = p.font();
    QFont labelFont = prev; labelFont.setBold(true); labelFont.setPointSizeF(prev.pointSizeF() - 1);
    p.setFont(labelFont);
    p.setPen(palette().color(QPalette::Text));
    auto label = [&](const QString &txt, const QPointF &pos) {
        const QRectF box(pos.x() - 12, pos.y() - 10, 24, 20);
        p.drawText(box, Qt::AlignCenter, txt);
    };
    label("N", QPointF(c.x(),          c.y() - r - 8));
    label("S", QPointF(c.x(),          c.y() + r + 8));
    label("E", QPointF(c.x() + r + 10, c.y()));
    label("W", QPointF(c.x() - r - 10, c.y()));
    p.setFont(prev);

    // Sun position: compass bearing measured clockwise from north.
    constexpr double kPi = 3.14159265358979323846;
    const double azRad = m_azimuth * kPi / 180.0;
    // Altitude collapses the vector toward centre at high sun angles.
    const double altT = std::clamp(m_altitude / 90.0, 0.0, 1.0);
    const double rr = r * (1.0 - altT);  // sun overhead → vector length 0
    const double dx =  std::sin(azRad) * rr;
    const double dy = -std::cos(azRad) * rr;
    const QPointF sun = c + QPointF(dx, dy);

    // Arrow from centre to sun, plus a sun glyph.
    p.setPen(QPen(QColor(220, 160, 0), 2.0));
    p.setBrush(QColor(255, 200, 60));
    p.drawLine(c, sun);
    p.drawEllipse(sun, 7.0, 7.0);

    // Altitude readout at the bottom.
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(rect().adjusted(0, 0, 0, -2),
               Qt::AlignBottom | Qt::AlignHCenter,
               QString::number(m_azimuth, 'f', 0) + "° / " +
               QString::number(m_altitude, 'f', 0) + "°");
}

} // namespace openswmmvis::ui
