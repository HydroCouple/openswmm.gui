/**
 * @file      sectionpreviewwidget.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QWidget host that paints one SectionDiagramModel.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_SECTIONPREVIEWWIDGET_H
#define OPENSWMMVIS_SECTIONVIEW_SECTIONPREVIEWWIDGET_H

#include <QPointF>
#include <QRectF>
#include <QWidget>

#include "ui/sectionview/sectiondiagram.h"

namespace openswmmvis::sectionview
{

class SectionPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QWidget owner; caller owns parentless preview.
     * @brief Create empty GUI-thread section viewer with 180×130 minimum logical size, Base
     *   background, identity viewport and translated placeholder/accessibility text.
     * @note Owns copied diagram data only, no engine/layer pointers, child editor or external
     *   signal binding.
     */
    explicit SectionPreviewWidget(QWidget *parent = nullptr);

    /*! Replace the drawing and repaint. Passing a default-constructed model
     *  clears the widget to its empty state.
     *
     *  Does NOT reset zoom/pan — see the note in the implementation. Call
     *  zoomToExtents() when the SUBJECT changes, not when its values do.
     * @param[in] model Value moved into widget storage, replacing complete prior drawing; default
     *   value yields empty state.
     * @note Always schedules repaint, no modelChanged/viewportChanged emission or zoom/pan reset.
     *   Last fit/exaggeration remain stale until later paint/export updates them; call
     *   zoomToExtents explicitly when subject changes. No engine validation or mutation.
     */
    void setModel(SectionDiagramModel model);

    /**
     * @return Borrowed stored model, valid until setModel/destruction. Const reference does not
     *   transfer ownership or promise engine synchronization.
     */
    [[nodiscard]] const SectionDiagramModel &model() const noexcept { return m_model; }

    /*! Message shown when the model has nothing to draw and carries no
     *  `emptyText` of its own.
     * @param[in] text Copied fallback when stored model emptyText absent; empty allowed.
     * @note Equality no-op, else repaint. Does not replace model emptyText or emit signal.
     */
    void setPlaceholderText(const QString &text);

    /*! Render the current model into an image — used by tests (offscreen) and
     *  by any future "copy diagram" action. Honours the current view.
     * @param[in] size Requested physical image dimensions at default DPR 1; invalid/empty defaults
     *   320×240.
     * @return Owned ARGB32 premultiplied image filled Base and painted with model/current
     *   viewport/palette; no filesystem write or GPU readback. Allocation failure has no separate
     *   status.
     * @note GUI-thread Qt font/painter operation. Const method DOES update mutable
     *   lastFitRect/achievedVE through painter outputs; export at a different size can affect
     *   subsequent zoom anchor until widget paints again. No viewportChanged or update emitted.
     *   Uses placeholder on local model copy.
     */
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

    /**
     * @return Borrowed stored viewport in logical widget pixels with relative geometry zoom; valid
     *   until next view mutation/destruction. No camera/model unit conversion.
     */
    [[nodiscard]] const DiagramViewport &viewport() const noexcept { return m_viewport; }

    /**
     * @param[in] viewport Copied desired relative zoom/pixel pan; finite zoom/pan expected, no
     *   explicit finite validation.
     * @note Clamp zoom [0.05,200], compare zoom fuzzily/pan exactly, store/emit
     *   viewportChanged/update only when changed. Infinite zoom clamps, NaN can survive std::clamp
     *   and repeated comparison. Does not reset gesture state, model or fit metrics.
     */
    void setViewport(const DiagramViewport &viewport);

    /*! Reset to the automatic fit. Invoked by middle double-click, and by
     *  hosts when the displayed object (not merely its values) changes.
     * @note Delegates identity viewport to setter (zoom 1, pan 0), emitting/repainting only if
     *   state differs. Does not rebuild model/recompute fit synchronously or cancel pan itself;
     *   double-click handler cancels separately.
     */
    void zoomToExtents();

    /*! Multiply the current zoom by \p factor, keeping the model point under
     *  \p anchorPx (widget coordinates) stationary.
     * @param[in] factor Finite positive multiplicative zoom; invalid/nonpositive ignored.
     * @param[in] anchorPx Finite logical widget pixel anchor, outside widget allowed.
     * @note Target clamped [0.05,200]; fuzzy unchanged target leaves pan unchanged. Preserves
     *   anchor relative to last valid PAINT fit centre, else widget centre, adjusting stored pixel
     *   pan then setViewport. Existing zoom must be finite positive for coherent ratio. No engine
     *   changes, raster resampling or text-size scaling.
     */
    void zoomBy(double factor, const QPointF &anchorPx);

    /*! V:H ratio used by the last paint — 1.0 for a true-scale or
     *  uniform-scale drawing. Reflects the automatic choice as well as an
     *  explicit one, so callers (and tests) never have to infer it from
     *  pixels. Valid only after something has been painted.
     * @return Last painter-written V:H ratio, initially 1 and stale until a successful qualifying
     *   paint/export. Empty/too-small draws can leave previous value; not necessarily current model
     *   requested ratio.
     */
    [[nodiscard]] double achievedVerticalExaggeration() const noexcept { return m_achievedVE; }

    /*! Pixel rect the model bounds were fitted into by the last paint, before
     *  zoom/pan. Exposed for tests and for any host that needs to relate
     *  widget coordinates back to the drawing.
     * @return Last painter-written prezoom fit rectangle in pixels of MOST RECENT render target
     *   (widget or renderToImage), initially null. Empty/too-small renders can leave prior value.
     *   No fresh fit computation.
     */
    [[nodiscard]] QRectF lastFitRect() const noexcept { return m_lastFitRect; }

signals:
    /*! Emitted whenever zoom or pan changes, so a host can mirror the state
     *  (e.g. a zoom-percentage readout).
     * @param[in] viewport Borrowed stored viewport after normalized setter transition; copy before
     *   retaining beyond call.
     * @note Emitted before repaint scheduling, only zoom/pan setter changes; setModel/paint/export
     *   do not emit. No engine/model property change.
     */
    void viewportChanged(const DiagramViewport &viewport);

protected:
    /**
     * @param[in] event Ignored borrowed paint event; implementation leaves parameter unnamed.
     * @brief Fill Base background, copy model to add fallback empty text, paint diagram/current
     *   viewport and update fit/exaggeration diagnostics.
     * @note GUI-thread synchronous draw, model unchanged, painter owns outer state. No signal or
     *   model-fetch side effects.
     */
    void paintEvent(QPaintEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, not retained.
     * @brief Empty model or fuzzy-zero vertical angleDelta delegates QWidget. Otherwise
     *   zoomBy(1.15^(angleDelta.y/120)) around event position and accept, even at clamp. Magnitude
     *   respected; horizontal/pixelDelta ignored.
     */
    void wheelEvent(QWheelEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, not retained.
     * @brief Middle press with nonempty model snapshots pixel anchor/pan, sets panning and
     *   closed-hand cursor, accepts. Other input delegates QWidget; no viewport edit yet.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, not retained.
     * @brief Armed pan sets stored pan from initial offset plus total pixel displacement, accepts;
     *   no current button-mask/model-empty check. Idle delegates QWidget.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, not retained.
     * @brief Armed middle release clears pan flag/unsets cursor and accepts, without applying final
     *   release position. Other release delegates QWidget.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, not retained.
     * @brief Any middle double-click cancels pan/unsets cursor and zooms to extents, accepts even
     *   on empty model. Other double-click delegates QWidget.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    SectionDiagramModel m_model;
    QString m_placeholder;

    DiagramViewport m_viewport;
    /*! Fit rect reported by the last paint. Zooming about the cursor needs the
     *  painter's own fit centre — the widget centre is off by the header /
     *  footer / adaptive-margin reservations, which would make the anchor
     *  drift a little on every notch. Mutable because paintEvent is const in
     *  spirit and renderToImage() genuinely is. */
    mutable QRectF m_lastFitRect;
    mutable double m_achievedVE = 1.0;
    bool m_panning = false;
    QPointF m_panAnchorPx;    //!< Cursor position when the pan began.
    QPointF m_panStartOffset; //!< Viewport pan at that moment.
};

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_SECTIONPREVIEWWIDGET_H
