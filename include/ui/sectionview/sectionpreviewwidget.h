/*!
 * \file   sectionpreviewwidget.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QWidget host that paints one SectionDiagramModel.
 *
 * Slice SP.2 (workplans/SECTION_PREVIEW_WORKPLAN.md).
 *
 * Deliberately dumb: it owns a model, repaints when the model is replaced, and
 * nothing else. Every surface that shows a section diagram — the Section View
 * dock, the cross-section editor, the LID editor — hosts one of these and
 * feeds it from its own builder, so the drawing code exists once.
 *
 * Follows the StreetSectionPreview / StylePreviewSwatch idiom already in the
 * codebase (plain QWidget + paintEvent, palette-driven colours).
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_SECTIONPREVIEWWIDGET_H
#define OPENSWMMVIS_SECTIONVIEW_SECTIONPREVIEWWIDGET_H

#include <QPointF>
#include <QRectF>
#include <QWidget>

#include "ui/sectionview/sectiondiagram.h"

namespace openswmmvis::sectionview {

class SectionPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SectionPreviewWidget(QWidget *parent = nullptr);

    /*! Replace the drawing and repaint. Passing a default-constructed model
     *  clears the widget to its empty state.
     *
     *  Does NOT reset zoom/pan — see the note in the implementation. Call
     *  zoomToExtents() when the SUBJECT changes, not when its values do. */
    void setModel(SectionDiagramModel model);

    [[nodiscard]] const SectionDiagramModel &model() const noexcept
    { return m_model; }

    /*! Message shown when the model has nothing to draw and carries no
     *  `emptyText` of its own. */
    void setPlaceholderText(const QString &text);

    /*! Render the current model into an image — used by tests (offscreen) and
     *  by any future "copy diagram" action. Honours the current view. */
    [[nodiscard]] QImage renderToImage(const QSize &size) const;

    // ---- View ---------------------------------------------------------------
    //
    // Navigation matches the map canvas so the gestures transfer:
    //   wheel                 → zoom about the cursor
    //   middle-button drag    → pan
    //   middle double-click   → zoom to extents
    //
    // Zoom scales the GEOMETRY only; labels keep their point size, which is what
    // makes zooming a legibility tool — the drawing spreads out from under
    // crowded dimension text instead of magnifying the crowding.

    [[nodiscard]] const DiagramViewport &viewport() const noexcept
    { return m_viewport; }

    void setViewport(const DiagramViewport &viewport);

    /*! Reset to the automatic fit. Invoked by middle double-click, and by
     *  hosts when the displayed object (not merely its values) changes. */
    void zoomToExtents();

    /*! Multiply the current zoom by \p factor, keeping the model point under
     *  \p anchorPx (widget coordinates) stationary. */
    void zoomBy(double factor, const QPointF &anchorPx);

    /*! V:H ratio used by the last paint — 1.0 for a true-scale or
     *  uniform-scale drawing. Reflects the automatic choice as well as an
     *  explicit one, so callers (and tests) never have to infer it from
     *  pixels. Valid only after something has been painted. */
    [[nodiscard]] double achievedVerticalExaggeration() const noexcept
    { return m_achievedVE; }

    /*! Pixel rect the model bounds were fitted into by the last paint, before
     *  zoom/pan. Exposed for tests and for any host that needs to relate
     *  widget coordinates back to the drawing. */
    [[nodiscard]] QRectF lastFitRect() const noexcept { return m_lastFitRect; }

signals:
    /*! Emitted whenever zoom or pan changes, so a host can mirror the state
     *  (e.g. a zoom-percentage readout). */
    void viewportChanged(const DiagramViewport &viewport);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    SectionDiagramModel m_model;
    QString             m_placeholder;

    DiagramViewport m_viewport;
    /*! Fit rect reported by the last paint. Zooming about the cursor needs the
     *  painter's own fit centre — the widget centre is off by the header /
     *  footer / adaptive-margin reservations, which would make the anchor
     *  drift a little on every notch. Mutable because paintEvent is const in
     *  spirit and renderToImage() genuinely is. */
    mutable QRectF  m_lastFitRect;
    mutable double  m_achievedVE = 1.0;
    bool            m_panning = false;
    QPointF         m_panAnchorPx;      //!< Cursor position when the pan began.
    QPointF         m_panStartOffset;   //!< Viewport pan at that moment.
};

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_SECTIONPREVIEWWIDGET_H
