/*!
 * \file   sectionpreviewwidget.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/sectionview/sectionpreviewwidget.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace openswmmvis::sectionview {

namespace {

//! Zoom per wheel notch. 1.15 is ~5 notches per octave — fine enough to land
//! on a readable scale without hunting, coarse enough to cross a decade fast.
constexpr double kWheelZoomStep = 1.15;
//! Matches the clamp inside paintSectionDiagram, so the widget's stored state
//! can never disagree with what is actually drawn.
constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 200.0;

} // namespace

SectionPreviewWidget::SectionPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sectionPreviewWidget"));
    setMinimumSize(180, 130);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    m_placeholder = tr("No section to display");
    // Accessible name: the drawing carries no focusable children, so screen
    // readers would otherwise announce an unlabelled pane (dialog_a11y_checks).
    setAccessibleName(tr("Section preview"));
    setToolTip(tr("Scroll to zoom · hold the middle button to pan · "
                  "double-click the middle button to zoom to extents"));
}

void SectionPreviewWidget::setModel(SectionDiagramModel model)
{
    m_model = std::move(model);
    // The view is deliberately NOT reset here. Hosts rebuild the model on every
    // keystroke (a geom spin box, a LID thickness), and resetting the zoom on
    // each one would yank the drawing back to fit while the user is typing —
    // exactly when they have zoomed in to read a dimension. Hosts call
    // zoomToExtents() themselves when the subject changes (a new selection, a
    // new shape), which is the only time a stale zoom is meaningless.
    update();
}

void SectionPreviewWidget::setPlaceholderText(const QString &text)
{
    if (m_placeholder == text) return;
    m_placeholder = text;
    update();
}

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------

void SectionPreviewWidget::setViewport(const DiagramViewport &viewport)
{
    DiagramViewport v = viewport;
    v.zoom = std::clamp(v.zoom, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(v.zoom, m_viewport.zoom) && v.panPx == m_viewport.panPx)
        return;

    m_viewport = v;
    emit viewportChanged(m_viewport);
    update();
}

void SectionPreviewWidget::zoomToExtents()
{
    setViewport(DiagramViewport{});
}

void SectionPreviewWidget::zoomBy(double factor, const QPointF &anchorPx)
{
    if (!(factor > 0.0) || !std::isfinite(factor)) return;

    const double target = std::clamp(m_viewport.zoom * factor, kMinZoom, kMaxZoom);
    // Clamped out — don't pan either, or the drawing creeps at the zoom limits.
    if (qFuzzyCompare(target, m_viewport.zoom)) return;

    // Keep the model point under the cursor stationary.
    //
    // The painter maps model→screen as  px = C + s·m  (with C folding in the
    // fit centre and the pan). Scaling s by k about a fixed screen point A
    // requires C' such that C' + k·s·m = A + k·(C + s·m − A), i.e. the pan
    // offset moves by (1 − k)·(A − C). Working in screen space like this avoids
    // duplicating the fit maths here — only the delta matters, and the fit
    // contribution cancels because it is the same in both frames.
    const double k = target / m_viewport.zoom;
    const QPointF fitCentre = m_lastFitRect.isValid() ? m_lastFitRect.center()
                                                      : QRectF(rect()).center();
    const QPointF anchorRel = anchorPx - fitCentre - m_viewport.panPx;

    DiagramViewport v = m_viewport;
    v.zoom  = target;
    v.panPx = m_viewport.panPx + (1.0 - k) * anchorRel;
    setViewport(v);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void SectionPreviewWidget::wheelEvent(QWheelEvent *event)
{
    if (m_model.isEmpty()) { QWidget::wheelEvent(event); return; }

    // angleDelta is in eighths of a degree; a notch is 120. Trackpads deliver
    // smaller continuous deltas, so derive the exponent rather than counting
    // notches — that keeps two-finger scrolling smooth instead of steppy.
    const double notches = event->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(notches)) { QWidget::wheelEvent(event); return; }

    zoomBy(std::pow(kWheelZoomStep, notches), event->position());
    event->accept();
}

void SectionPreviewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && !m_model.isEmpty()) {
        m_panning        = true;
        m_panAnchorPx    = event->position();
        m_panStartOffset = m_viewport.panPx;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SectionPreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        DiagramViewport v = m_viewport;
        v.panPx = m_panStartOffset + (event->position() - m_panAnchorPx);
        setViewport(v);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void SectionPreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SectionPreviewWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        // The press half of the double-click already started a pan; cancel it
        // so the reset isn't immediately undone by a stray drag.
        m_panning = false;
        unsetCursor();
        zoomToExtents();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

QImage SectionPreviewWidget::renderToImage(const QSize &size) const
{
    QImage img(size.isValid() && !size.isEmpty() ? size : QSize(320, 240),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(palette().color(QPalette::Base));

    {
        QPainter p(&img);
        SectionDiagramModel m = m_model;
        if (m.emptyText.isEmpty()) m.emptyText = m_placeholder;
        paintSectionDiagram(p, QRectF(QPointF(0.0, 0.0), QSizeF(img.size())),
                            m, palette(), m_viewport, &m_lastFitRect,
                            &m_achievedVE);
    }   // painter must be finished before the image is handed back
    return img;
}

void SectionPreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Base));

    SectionDiagramModel m = m_model;
    if (m.emptyText.isEmpty()) m.emptyText = m_placeholder;
    paintSectionDiagram(p, QRectF(rect()), m, palette(), m_viewport,
                        &m_lastFitRect, &m_achievedVE);
}

} // namespace openswmmvis::sectionview
