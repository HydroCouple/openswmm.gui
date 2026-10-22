/**
 * @file      meshprofileplotwidget.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Longitudinal cross-section renderer for a path traced across the 2D
 *         surface mesh.  Plain QWidget + QPainter (chart-like, not map-like),
 *         mirroring ProfilePlotWidget's scaffolding (plotRect / dataToPixel /
 *         zoom-pan / legend + timestamp drag overlays) but with continuous
 *         ground + water lines instead of node/link glyphs.
 *
 *         Render passes (z-order, low → high):
 *           1. background + axes (x = Distance, y = Elevation)
 *           2. soil fill — ground polyline down to the plot floor (broken at
 *              off-mesh NaN gaps)
 *           3. max-depth envelope — band (ground → ground+maxDepth) + line
 *           4. animated depth fill — ground → ground+depthNow
 *           5. animated water-surface line — ground+depthNow
 *           6. ground line (crisp terrain edge over the soil)
 *           7. legend + timestamp overlay
 *
 *         Geometry (chainage/ground/maxDepth) is static once set via
 *         setProfile(); only depthNow animates — push it cheaply per frame
 *         with setCurrentDepths().
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MESH_PROFILE_PLOT_WIDGET_H
#define MESH_PROFILE_PLOT_WIDGET_H

#include "plot/meshprofileplotoptions.h"
#include "plot/meshprofilesampler.h"

#include <QDateTime>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class QRubberBand;

class MeshProfilePlotWidget : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create a theme-aware painter profile with keyboard focus and mouse navigation.
     * @param[in] parent Optional QWidget owner. Initial minimum size is 360x240 logical pixels; no layer/source or options object is owned/fetched.
     */
    explicit MeshProfilePlotWidget(QWidget *parent = nullptr);

    /*! \brief Sets the full cross-section (recomputes bounds + repaints).
     * @param[in] profile Complete section copied into the widget, including additional elevation series and provenance. Stations should be finite/ordered and use consistent horizontal/vertical units, with valid break flags for independent intervals.
     * @note Refreshes accessible metadata and auto bounds. If fit mode remains enabled it also replaces the view range; manual views are retained. Existing cursor state is retained without reclamping to a changed path. Repaint is scheduled, not immediate.
     */
    void setProfile(const MeshProfileSampler::MeshProfile &profile);

    /*! \brief Cheap per-frame update of the animated depth column. Size must
     *  match the current sample count; mismatched sizes are ignored.
     *  \p cellHasSurface (optional, same size) refreshes the frame-dependent
     *  Sample::cellHasSurface flag that gates dry-gap bridging.
     * @param[in] depthNow Current depths in profile vertical units, one per existing sample. Wrong size ignores the entire update.
     * @param[in] cellHasSurface Optional parallel current surface-validity flags; wrong/empty size preserves existing flags.
     * @param[in] signedDepths Optional parallel signed depths for exact-interval rendering; wrong/empty size preserves previous signed values.
     * @note Copies columns and schedules repaint without recomputing bounds, accessibility metadata, hasResults or current timestamp. Caller must keep depth/sign/validity semantics consistent.
     */
    void setCurrentDepths(const QVector<double> &depthNow, const QVector<bool> &cellHasSurface = {},
                          const QVector<double> &signedDepths = {});

    /*! \brief Binds the styling object (connects changed() → repaint).
     * @param[in] options Optional borrowed QObject style cache guarded by QPointer; null enables hardcoded defaults.
     * @note An identical pointer is a no-op; otherwise disconnects the old binding, connects changed() to repaint and repaints. Does not reparent, own, resample or recompute bounds.
     */
    void setOptions(MeshProfilePlotOptions *options);
    /**
     * @return Borrowed style cache or null after its destruction; caller must not assume ownership.
     */
    [[nodiscard]] MeshProfilePlotOptions *options() const { return m_options; }

    /*! \brief Wall-clock time shown by the timestamp overlay.
     * @param[in] dt Timestamp copied as supplied, including its time-zone representation; invalid hides the overlay. Equality is a no-op.
     * @note Repaints only; does not change samples, per-series effective times or source frame.
     */
    void setCurrentDateTime(const QDateTime &dt);

    /**
     * @brief Replace the display captions without converting profile values.
     * @param[in] xLabel Horizontal caption including units if desired.
     * @param[in] yLabel Vertical caption including units if desired.
     * @note Copies both strings and schedules repaint.
     */
    void setAxisLabels(const QString &xLabel, const QString &yLabel);

    // ── Position cursor (chart ↔ map sync) ──────────────────────────────
    /*! \brief Show/move the vertical position cursor at \p chainage (scene
     *  units along the path). Clamped to the path extent. Display-only — does
     *  NOT emit cursorChainageChanged, so the map can drive it without an echo
     *  loop. Pass a negative value to hide the cursor.
     * @param[in] chainage Finite scene-unit distance from zero to the last sample chainage, clamped to that interval. Negative hides; an empty profile also hides. NaN is not explicitly rejected, so callers must supply finite values.
     * @note Active changes refresh accessibility and repaint without cursorChainageChanged(); movements below 1e-9 are suppressed. Hiding retains the stored coordinate returned by cursorChainage().
     */
    void setCursorChainage(double chainage);
    /**
     * @return Whether the position guide is enabled; not a guarantee that its coordinate lies in the current visible range.
     */
    [[nodiscard]] bool hasCursor() const { return m_hasCursor; }
    /**
     * @return Last stored profile-unit coordinate, retained while hidden; inspect hasCursor() before treating it as active.
     */
    [[nodiscard]] double cursorChainage() const { return m_cursorChainage; }

    // ── Zoom / pan (driven by the dialog toolbar) ───────────────────────
    enum class Mode { Identify = 0, Pan, ZoomIn, ZoomOut };
    enum class AxisEdge {
        None = 0,
        XMinimum,
        XMaximum,
        YMinimum,
        YMaximum,
    };
    Q_ENUM(AxisEdge)

    /**
     * @brief Select mouse navigation policy and reset pan/zoom/axis-click state.
     * @param[in] m Valid Identify/Pan/ZoomIn/ZoomOut mode; identical is a no-op.
     * @note Updates cursor and hides an existing rubber band. Does not clear position cursor or explicitly reset overlay/cursor-drag state.
     */
    void setMode(Mode m);
    /**
     * @return Stored mouse interaction mode, initially Identify.
     */
    [[nodiscard]] Mode mode() const { return m_mode; }
    /**
     * @brief Hit-test editable endpoint labels in the bottom/left gutters.
     * @param[in] widgetPos Logical widget-pixel position.
     * @return X min/max within 28 pixels along bottom corners and 48-pixel label band, or Y min/max in the analogous left band; otherwise None. Bottom X tests take priority at shared corners.
     */
    [[nodiscard]] AxisEdge axisEdgeAt(const QPoint &widgetPos) const;
    /**
     * @brief Set one manual data-axis endpoint while preserving a positive span.
     * @param[in] edge Declared axis endpoint; None is refused.
     * @param[in] value Finite coordinate in horizontal/vertical profile units, strictly below/above the opposite edge as appropriate.
     * @return False for nonfinite/invalidly ordered values or None, leaving bounds unchanged; true disables fit mode and repaints, even for equality to the current same edge. Pass only declared enum values.
     * @note viewRangeChanged() is emitted later from paintEvent() if the effective rectangle changed.
     */
    bool setAxisEdgeValue(AxisEdge edge, double value);
    /**
     * @return Normalized data rectangle with horizontal chainage and vertical elevation units; no pixel transformation or source access occurs.
     */
    [[nodiscard]] QRectF visibleDataRange() const;
    /**
     * @brief Multiply both visible spans about the current centre.
     * @param[in] factor Finite positive span multiplier; below one zooms in, above one out. Nonpositive is ignored; NaN is not explicitly rejected.
     * @note Disables fit mode and schedules repaint; no data-extent clamp or immediate range signal occurs.
     */
    void zoomBy(double factor);
    /**
     * @brief Re-enable fit mode and adopt cached padded auto bounds.
     * @note Schedules repaint without resampling data or immediately emitting viewRangeChanged(). Future setProfile() updates follow auto bounds.
     */
    void fitToExtent();

signals:
    /*! \brief Emitted while the user drags the position cursor on the chart.
     *  \p chainage is the scene-unit distance along the path. The dialog maps
     *  it to a scene point and moves the map arrow.
     * @param[in] chainage Requested user cursor coordinate in path units. Mouse gestures clamp to the primary path; keyboard uses the union of sample/series stations and emits the selected station even if the display setter clamps it.
     * @note Programmatic setCursorChainage() never emits this synchronization signal.
     */
    void cursorChainageChanged(double chainage);
    /**
     * @brief Notify a changed visible data rectangle from the paint pass.
     * @param[in] range Normalized chainage/elevation bounds.
     * @note Emission is deferred until paintEvent() and deduplicated against the last painted range; setting bounds alone does not synchronously notify.
     */
    void viewRangeChanged(const QRectF &range);

protected:
    /**
     * @brief Paint axes and section layers, notifying a changed view rectangle before drawing; an empty primary sample vector stops further paint passes.
     * @param[in,out] event Borrowed Qt event valid only for dispatch; never retained/deleted. Paint/resize event contents are unused.
     */
    void paintEvent(QPaintEvent *event) override;
    /**
     * @brief Schedule repaint after resize without changing numeric view/fit bounds.
     * @param[in,out] event Borrowed Qt event valid only for dispatch; never retained/deleted. Paint/resize event contents are unused.
     */
    void resizeEvent(QResizeEvent *event) override;
    /**
     * @brief Prioritize draggable timestamp/legend overlays, then middle-button pan, editable axis labels and mode-specific left-button pan/zoom/identify cursor placement.
     * @param[in,out] event Borrowed Qt event valid only for dispatch; never retained/deleted. Paint/resize event contents are unused.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @brief Update active overlay offsets, position cursor, numeric pan or rubber band. Overlay edits mutate the bound options; cursor movement emits synchronization; unhandled movement delegates to QWidget.
     * @param[in,out] event Borrowed Qt event valid only for dispatch; never retained/deleted. Paint/resize event contents are unused.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @brief Finish overlay/cursor/pan gestures or apply click/rectangle zoom. Axis-label clicks under five pixels open the endpoint editor; unhandled releases delegate to QWidget.
     * @param[in,out] event Borrowed Qt event valid only for dispatch; never retained/deleted. Paint/resize event contents are unused.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @brief Scale both numeric axes by 0.85 per vertical angle-wheel step about the pointer, disable fit and repaint. Zero vertical delta is ignored; nonzero events are accepted.
     * @param[in,out] event Borrowed Qt event valid only for dispatch; never retained/deleted. Paint/resize event contents are unused.
     */
    void wheelEvent(QWheelEvent *event) override;
    /**
     * @brief Move cursor with Left/Right/Home/End through sorted unique primary/additional series stations, including hidden/error series. No stations or other keys delegate to QWidget; handled keys announce cursor synchronization.
     * @param[in,out] event Borrowed Qt event valid only for dispatch; never retained/deleted. Paint/resize event contents are unused.
     */
    void keyPressEvent(QKeyEvent *event) override;

private:
    /**
     * @brief Replace accessible text with active station, navigation help and visible additional elevation-series provenance.
     * @note Does not sample a source or emit cursor synchronization.
     */
    void refreshAccessibleDescription();
    /**
     * @return Logical-pixel data rectangle with fixed 64/16/16/40 left/right/top/bottom gutters and at least one pixel per dimension.
     */
    [[nodiscard]] QRectF plotRect() const;
    /**
     * @param[in] chainage Finite horizontal profile coordinate.
     * @param[in] elev Finite vertical elevation in profile units.
     * @return Affine widget pixel coordinate with inverted Y; outside data values extrapolate without clipping. Requires positive visible spans.
     */
    [[nodiscard]] QPointF dataToPixel(double chainage, double elev) const;
    /**
     * @param[in] px Logical widget-pixel X.
     * @return Affine profile chainage without clipping; outside plot pixels extrapolate.
     */
    [[nodiscard]] double pixelToChainage(double px) const;
    /*! \brief Linearly interpolate ground + water-surface elevation at
     *  \p chain from the samples. Returns false off-mesh / no samples.
     * @param[in] chain Finite requested station, in profile chainage units.
     * @param[out] ground Interpolated finite-ground elevation on success; unchanged on failure.
     * @param[out] wse Ground plus nonnegative interpolated depth, in profile vertical units, on success.
     * @return False for empty/off-mesh/no exact containing interval. Exact mode requires strictly positive interval widths and honors breakBefore; fallback mode clamps outside sample extents and interpolates depthNow. No catalog/elevation-overlay series are sampled here.
     */
    [[nodiscard]] bool sampleAtChainage(double chain, double &ground, double &wse) const;
    /**
     * @brief Cache padded path/ground/maximum-water/additional-elevation bounds.
     * @note X uses zero to last primary sample chainage, Y finite ground plus maxDepth and visible valid additional elevation values. Current depths alone do not expand bounds. Invalid/flat Y defaults [0,1], then adds 6% Y/2% X padding; empty profile caches [0,1]. Updates the live view only in fit mode.
     */
    void recomputeBounds();
    /**
     * @brief Open a locale-aware endpoint-entry dialog and validate the accepted value.
     * @param[in] edge Declared endpoint; None returns immediately.
     * @return True after a successful setAxisEdgeValue(), false for None/cancel/parse/range failure. Invalid accepted input displays a warning and leaves bounds unchanged.
     */
    bool editAxisEdge(AxisEdge edge);

    /**
     * @brief Paint the white data background, theme grid/axes, numeric-format ticks and captions.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintBackgroundAndAxes(QPainter &p) const;
    /**
     * @brief Fill finite-ground runs down to the plot floor, breaking at missing ground/breakBefore and composing intervals without alpha seams.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintSoilFill(QPainter &p) const;
    /**
     * @brief Paint option-enabled temporal maximum fill/outline using signed exact geometry or bridged legacy top elevations.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintMaxEnvelope(QPainter &p) const;
    /**
     * @brief Paint option-enabled current wet fill using exact signed intervals or the legacy bridged surface.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintDepthFill(QPainter &p) const;
    /**
     * @brief Stroke the option-enabled current WSE using exact/legacy wet-band helpers.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintWseLine(QPainter &p) const;
    /**
     * @brief Stroke finite-ground runs while honoring breakBefore; isolated samples produce no segment.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintGroundLine(QPainter &p) const;
    /**
     * @brief Draw visible, error-free Elevation-role series with per-series pen/opacity, clipped to the data rectangle and broken at invalid status or interval boundaries.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintAdditionalElevations(QPainter &p) const;
    /**
     * @brief Draw option-enabled three-pixel crossing markers near the visible plot rectangle.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintCellBoundaryDots(QPainter &p) const;
    /**
     * @brief Draw the active in-view guide/handle/readout using primary ground/depth interpolation. Depth text requires hasResults and WSE above ground by 1e-4 profile vertical units.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintCursor(QPainter &p) const;
    /**
     * @brief Build enabled built-in and visible elevation-series swatches/provenance, recording the last-painted drag hit rectangle. Clears its rectangle when hidden.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintLegend(QPainter &p) const;
    /**
     * @brief Draw the valid supplied timestamp in the selected QDateTime format/corner/offset and cache its drag hit rectangle; hidden/invalid/empty text clears the rectangle.
     * @param[in,out] p Active widget painter. This pass reads cached samples/options only and never queries result sources; individual passes preserve state where needed by their drawing sequence.
     */
    void paintTimeLabel(QPainter &p) const;

    MeshProfileSampler::MeshProfile m_profile;
    QPointer<MeshProfilePlotOptions> m_options;
    QDateTime m_currentDateTime;
    QRectF m_lastEmittedViewRange;
    QString m_xLabel = QStringLiteral("Distance");
    QString m_yLabel = QStringLiteral("Elevation");

    // Auto extent (data bounds) + current view bounds.
    double m_autoXMin = 0.0, m_autoXMax = 1.0, m_autoYMin = 0.0, m_autoYMax = 1.0;
    double m_dataXMin = 0.0, m_dataXMax = 1.0, m_dataYMin = 0.0, m_dataYMax = 1.0;
    bool m_fitMode = true;

    Mode m_mode = Mode::Identify;
    bool m_panActive = false;
    bool m_zoomActive = false;
    AxisEdge m_pressedAxisEdge = AxisEdge::None;
    QPoint m_lastMousePos;
    QPoint m_zoomAnchor;
    QRubberBand *m_rubberBand = nullptr;

    // Last-painted overlay rects (drag hit-test).
    mutable QRectF m_legendRect;
    mutable QRectF m_timeLabelRect;
    enum class OverlayDrag { None = 0, Legend, TimeLabel };
    OverlayDrag m_overlayDrag = OverlayDrag::None;
    QPoint m_overlayDragLastPos;

    // Position cursor (a draggable vertical line synced with the map arrow).
    bool m_hasCursor = false;
    double m_cursorChainage = 0.0;
    bool m_cursorDragging = false;
};

#endif // MESH_PROFILE_PLOT_WIDGET_H
