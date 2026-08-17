/*!
 * \file   selectionbeaconoverlay.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * See selectionbeaconoverlay.h for the contracts.
 */
#include "map/selectionbeaconoverlay.h"

#include <QPainter>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace SelectionBeacon;

SelectionBeaconOverlay::SelectionBeaconOverlay(QObject *parent)
    : QObject(parent)
{
    m_frameTimer.setInterval(kFlashFrameMs);
    connect(&m_frameTimer, &QTimer::timeout, this, [this]() {
        if (flashProgress() >= 1.0) {
            m_flashing = false;
            m_frameTimer.stop();
        }
        // One last repaint after the stop so the final frame lands with the
        // flash gone and only the steady beacon left.
        emit needsRepaint();
    });
}

void SelectionBeaconOverlay::setAnchors(const QVector<QPointF> &sceneAnchors)
{
    m_anchors = sceneAnchors;
    if (m_anchors.isEmpty()) {
        m_flashing = false;
        m_frameTimer.stop();
        emit needsRepaint();
        return;
    }
    m_flashing = true;
    m_flashClock.restart();
    m_frameTimer.start();
    emit needsRepaint();
}

void SelectionBeaconOverlay::setColor(const QColor &c)
{
    if (c.isValid() && c != m_color) {
        m_color = c;
        emit needsRepaint();
    }
}

double SelectionBeaconOverlay::flashProgress() const
{
    if (!m_flashing || !m_flashClock.isValid()) return 2.0;   // not running
    return double(m_flashClock.elapsed()) / double(kFlashDurationMs);
}

void SelectionBeaconOverlay::paint(
    QPainter &p,
    const std::function<QPointF(const QPointF &)> &toPixel,
    const QSize &viewport)
{
    if (m_anchors.isEmpty() || !toPixel) return;

    // Project once; work in device pixels from here so nothing scales.
    QVector<QPointF> px;
    px.reserve(m_anchors.size());
    // Track the span by hand: QRectF::united() treats a zero-size rect as
    // null and returns the other operand, so folding point-rects that way
    // silently leaves an empty span — and the aggregate test below would
    // then fire at every zoom.
    double xMin =  std::numeric_limits<double>::infinity();
    double yMin =  std::numeric_limits<double>::infinity();
    double xMax = -std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (const QPointF &a : m_anchors) {
        const QPointF q = toPixel(a);
        if (!std::isfinite(q.x()) || !std::isfinite(q.y())) continue;
        px.push_back(q);
        xMin = std::min(xMin, q.x()); xMax = std::max(xMax, q.x());
        yMin = std::min(yMin, q.y()); yMax = std::max(yMax, q.y());
    }
    if (px.isEmpty()) return;
    const QRectF bounds(QPointF(xMin, yMin), QPointF(xMax, yMax));

    // Zoomed out far enough that the whole selection lands inside one
    // beacon: collapse to a single ring at its centre. Drawing N coincident
    // rings there would just stack alpha into a blob and say nothing more.
    const bool aggregate = px.size() > kMaxBeacons
                        || (bounds.width()  <= kBeaconRadiusPx * 2
                         && bounds.height() <= kBeaconRadiusPx * 2);
    QVector<QPointF> draw;
    if (aggregate) draw.push_back(bounds.center());
    else           draw = px;

    // Off-screen selections draw nothing — "Zoom to Selection" covers that.
    const QRectF view(QPointF(0, 0), QSizeF(viewport));
    const QRectF slack = view.adjusted(-kFlashMaxRadiusPx, -kFlashMaxRadiusPx,
                                        kFlashMaxRadiusPx,  kFlashMaxRadiusPx);

    const double t = flashProgress();
    const bool   flashing = t >= 0.0 && t < 1.0;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);

    for (const QPointF &c : std::as_const(draw)) {
        if (!slack.contains(c)) continue;

        // ── Steady beacon — a ring plus a short cross, so it reads as a
        // deliberate marker rather than a stray circle in the symbology.
        QColor ring = m_color;
        ring.setAlpha(230);
        p.setPen(QPen(ring, 2.0));
        p.drawEllipse(c, double(kBeaconRadiusPx), double(kBeaconRadiusPx));

        QColor tick = m_color;
        tick.setAlpha(160);
        p.setPen(QPen(tick, 1.2));
        const double r0 = kBeaconRadiusPx + 3.0, r1 = kBeaconRadiusPx + 8.0;
        p.drawLine(QPointF(c.x() - r1, c.y()), QPointF(c.x() - r0, c.y()));
        p.drawLine(QPointF(c.x() + r0, c.y()), QPointF(c.x() + r1, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - r1), QPointF(c.x(), c.y() - r0));
        p.drawLine(QPointF(c.x(), c.y() + r0), QPointF(c.x(), c.y() + r1));

        // ── Flash — one ring expanding out and fading to nothing.
        if (flashing) {
            const double eased = 1.0 - std::pow(1.0 - t, 3.0);   // ease-out
            const double r = kBeaconRadiusPx
                           + eased * (kFlashMaxRadiusPx - kBeaconRadiusPx);
            QColor pulse = m_color;
            pulse.setAlphaF(std::clamp(1.0 - t, 0.0, 1.0) * 0.85);
            p.setPen(QPen(pulse, 3.0));
            p.drawEllipse(c, r, r);
        }
    }

    p.restore();
}
