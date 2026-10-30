/**
 * @file      profileplotwidget.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BC's longitudinal section renderer.
 *
 *         Paints a SWMM/EPA-style profile view from a `PathStatic` plus
 *         one or more `SeriesBinding`s.  Rendering layers (z-order, low to
 *         high):
 *
 *           1. Soil fill — translucent earth tone between the ground line
 *              (`invertElev + maxDepth` per node) and the conduit crown,
 *              including the manhole shaft at each connecting node.
 *           2. Conduit segments — per-link upstream/downstream invert +
 *              crown lines, with offset1/offset2 honored at each end.
 *              Non-conduit links (pump / weir / orifice / outlet) render
 *              as labelled glyphs spanning the gap.
 *           3. Connecting-node glyphs — per-node vertical bar from invert
 *              to rim, with node ID labelled.  Junction/Outfall/Storage/
 *              Divider get distinct symbols.
 *           4. Max-HGL / Max-EGL envelopes — invert→max fill (always
 *              from the link/node invert up to the peak), one fill per
 *              source, toggled independently by `LayerToggles::maxHglBand`
 *              (for the fill) and `maxHglLine` / `maxEglLine`
 *              (for the outline).
 *           5. Current HGL / EGL lines — animated, driven by
 *              `setCurrentPeriod()`.  EGL is dashed.
 *
 *         Coordinates: x = chainage along path (in model units); y =
 *         elevation (in model units).  Pixel mapping is recomputed on
 *         `resizeEvent` to keep the data extent visible with a fixed
 *         margin.  Plain `QWidget` + `QPainter` — no QGraphicsScene —
 *         since the view is chart-like, not map-like.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILE_PLOT_WIDGET_H
#define PROFILE_PLOT_WIDGET_H

#include "plot/profilebuilder.h"
#include "plot/profileplotoptions.h"

#include <QBrush>
#include <QColor>
#include "io/lidstates.h"
#include <QDateTime>
#include <QPen>
#include <QPainterPath>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

#include <limits>
#include <memory>
#include <optional>

class ProfilePlotWidget : public QWidget
{
    Q_OBJECT

public:
    /*!
     * \struct SeriesBinding
     * \brief One elevation-axis curve to render: a (results-layer × output
     *        kind) pair with resolved style and pre-computed data.
     *
     *        Multi-output / multi-model overlay model: each SeriesBinding
     *        represents exactly one `OutputKind` (HGL, EGL, WaterSurface,
     *        MaxHGL, etc.).  Bindings that share the same source results
     *        layer share the same `derived` shared_ptr so the per-period
     *        and envelope arrays are computed once and reused — important
     *        so each output can select its corresponding rows/extrema without
     *        recomputing the common source. EGL renders as a line, without fill.
     *
     *        Styling is fully resolved before reaching the widget — the
     *        caller (typically ProfilePlotDialog) merges the layer's
     *        per-output pen/brush with any per-series override and hands
     *        the final QPen / QBrush in. Missing pen colours and HGL brushes have
     *        the specific fallbacks documented on the paint helpers.
     */
    struct SeriesBinding {
        QString label;
        QColor color;
        ProfileBuilder::OutputKind kind = ProfileBuilder::OutputKind::HGL;
        std::shared_ptr<ProfileBuilder::SourceDerived> derived;
        QPen pen;
        QBrush brush;
        bool visible = true;
    };

    /*!
     * \struct LayerToggles
     * \brief Label / terrain / orientation switches *and* per-output user
     *        preferences (currentHglLine, currentHglFill, maxHglBand, etc.).
     *
     *        The widget only consults the label / terrain / orientation
     *        fields directly — visibility of individual outputs lives on
     *        each `SeriesBinding`'s `visible` flag.  The per-output
     *        booleans (currentHglLine … maxEglLine) are carried here as user
     *        preferences that the host dialog reads when (re)building the
     *        series list, so the existing `ProfileLayerPanel` and
     *        `ProfilePlotOptions` Q_PROPERTYs stay unchanged.
     */
    struct LayerToggles {
        enum LabelOrientation { Vertical = 0, Horizontal = 1, Diagonal = 2 };

        // Per-output user preferences — read by the dialog, ignored by the
        // widget.  Default matches the historic look: HGL + EGL current,
        // Max HGL band on, Max EGL off.
        // Live HGL split into independent line + fill toggles. The
        // legend still shows a single HGL entry whenever either is on.
        bool currentHglLine = true;
        bool currentHglFill = true;
        bool currentEgl = true;
        bool maxHglBand = false;
        bool maxHglLine = false;
        // EGL has no fill — the only knob is the Max-EGL envelope line.
        bool maxEglLine = false;

        // Used directly by the widget.
        bool showNodeLabels = false;   /*!< secondary axis: node IDs */
        bool showLinkLabels = false;   /*!< secondary axis: link names */
        bool inlineNodeLabels = false; /*!< node IDs over the glyph */
        LabelOrientation labelOrientation = Vertical;
        int labelAngleDeg = 45;        /*!< only used when LabelOrientation == Diagonal */
        bool useTerrainGround = false; /*!< draw ground line from DEM samples */
    };

    /**
     * @brief Create a theme-aware painter section view with mouse hit testing and navigation.
     * @param[in] parent Optional QWidget owner. Starts empty with a 360x240 logical-pixel minimum, fit mode and Identify interactions. No live model/result source is retained or fetched.
     */
    explicit ProfilePlotWidget(QWidget *parent = nullptr);

    /*!
     * \brief Sets the static path + chainage to render.  Triggers a repaint.
     * @param[in] path Static snapshot copied into the widget. Caller supplies coherent nodes, exactly nodes-1 links, parallel ordered real chainage and common model length units; setters do not run ProfileBuilder::validate().
     * @note Resets the remembered 2D-overlay peak but retains the existing overlay/series/period selection. Recomputes schematic virtual X/auto bounds, updates the view only in fit mode, emits changed X range and schedules repaint.
     */
    void setPath(const ProfileBuilder::PathStatic &path);

    /*!
     * \brief Replaces all rendered series at once.  Order matters only for
     *        z-order between equal-kind series (e.g. when overlaying HGL
     *        from two models, the later series paints on top).
     * @param[in] series Binding metadata copied in order; derived histories remain shared through shared_ptr and must match the path's node ordering/units.
     * @note Recomputes bounds from visible kinds' own extrema, retaining manual view bounds. Current period/source markers are not reset. Shared derived data must not be modified concurrently with painting; later data edits require an explicit refresh/setter.
     */
    void setSeries(const QVector<SeriesBinding> &series);
    /**
     * @return Borrowed immutable binding vector; replacement invalidates references, and shared derived pointees remain separately shared/mutable. No source resolution occurs.
     */
    [[nodiscard]] const QVector<SeriesBinding> &series() const { return m_series; }

    /*!
     * \brief Selects the time-step shown by current-time series (HGL / EGL
     *        / WaterSurface).  `seriesIdx` is stored as a source marker; all current-time series
     *        use the same period index without per-source mapping.  Pass `-1` for `period` to hide the animation lines.
     * @param[in] seriesIdx Stored source marker; this implementation does not use it to select or remap individual series. No index validation occurs.
     * @param[in] period Global zero-based row index applied to every current-time binding; negative hides current curves and each short source row is independently skipped.
     * @note Stores both values and repaints without date translation, source fetching, bounds recomputation or signals. Host must align histories in advance.
     */
    void setCurrentPeriod(int seriesIdx, int period);

    /*!
     * \brief Sets the wall-clock datetime corresponding to the current
     *        period.  Used by the on-plot timestamp overlay.  Passing an
     *        invalid QDateTime hides the overlay.
     * @param[in] dt Overlay timestamp copied as represented, including timezone; invalid hides the card. Equality is a no-op.
     * @note Repaints without changing global period or any source history.
     */
    void setCurrentDateTime(const QDateTime &dt);

    /*!
     * \brief Convenience overload: applies `period` to the primary
     *        (series index 0).  Per-source time-mapping is the dialog's
     *        responsibility — the widget doesn't translate dates.
     * @param[in] period Global sample index for all animated bindings, with negative hiding current curves. Stores source marker zero when any binding exists, -1 otherwise.
     * @note Always repaints; does not map timestamps between runs or modify histories/bounds.
     */
    void setCurrentPeriod(int period);

    /**
     * @brief Replace recorded internal LID water-state presentation without refitting.
     * @param[in] frame Value snapshot copied with absolute elevations/heads in the same project length unit as path geometry, and moisture/porosity as volume fractions. The si flag is not used here to convert values.
     * @param[in] showWater Whether to paint pore-saturation tint and resolved saturation boundaries.
     * @param[in] showHead Whether to paint available Richards outlet-layer pore-water heads.
     * @note Repaints while preserving fitted/manual bounds. Node-name/porous-layer geometry matching is checked at drawing time; no file reads occur.
     */
    void setLidWaterState(const openswmmvis::io::LidStateFrame &frame, bool showWater,
                          bool showHead);
    /**
     * @return Borrowed current copied LID frame; subsequent setLidWaterState() replaces it. The frame can be unavailable/incompatible with current geometry.
     */
    const openswmmvis::io::LidStateFrame &lidWaterState() const { return m_lidWaterState; }

    /**
     * @param[in] toggles Raw snapshot copied verbatim. Label/terrain/orientation members affect this widget directly; output preference booleans are retained for the host's binding construction.
     * @note Repaints only; does not update ProfilePlotOptions or recompute bounds/virtual chainage. Diagonal angles are clamped at layout/paint use.
     */
    void setLayerToggles(const LayerToggles &toggles);
    /**
     * @return Independent raw toggle snapshot, not synthesized from the bound options or rendered-series visibility.
     */
    [[nodiscard]] LayerToggles layerToggles() const;

    /*!
     * \struct Surface2DSample
     * \brief One station of the 2D inundation overlay: the active 2D results
     *        layer's water surface sampled where the path crosses the mesh.
     *        `chainage` is REAL path chainage (converted to virtual x when
     *        painting, like terrain samples). `bed` is the mesh bed elevation
     *        at the station; `wse` is the signed reconstructed surface, including
     *        its continuation below the bed for exact shoreline clipping.
     *        NaN denotes a dry cell / off-mesh / no data (a gap).
     */
    struct Surface2DSample {
        double chainage = 0.0;
        double bed = std::numeric_limits<double>::quiet_NaN();
        double wse = std::numeric_limits<double>::quiet_NaN();
        bool breakBefore = false; // start of a separately owned mesh interval
    };

    /*!
     * \brief Replaces the 2D inundation overlay stations (static geometry +
     *        the current frame's WSE). Pass an empty vector to clear. The
     *        host re-sends the vector with fresh `wse` values on every
     *        animation tick; `ProfilePlotOptions::show2DInundation` gates
     *        drawing. Widens the y-extent to the wettest station.
     * @param[in] samples Real-chainage stations copied with bed/WSE in profile elevation units and breakBefore for independent intervals. Empty clears current stations, while the remembered peak remains until setPath().
     * @note Tracks the greatest finite WSE ever supplied for a stable fitted upper extent. Recomputes bounds only for a higher remembered WSE or changed vector size, retaining manual view. Same-sized station/bed edits without a higher WSE only repaint.
     */
    void setSurface2DSamples(const QVector<Surface2DSample> &samples);
    /**
     * @return Borrowed copied overlay vector in real-chainage units; references are invalidated by replacement. No source lifetime is retained.
     */
    [[nodiscard]] const QVector<Surface2DSample> &surface2DSamples() const { return m_surface2D; }

    /*!
     * \brief Binds a ProfilePlotOptions object — the widget's theming
     *        (per-type fills/outlines, soil colours, line widths,
     *        legend position/font/opacity) is read from it.  Pass
     *        nullptr to fall back to the hardcoded defaults.  The
     *        widget connects to `changed()` and repaints automatically.
     * @param[in] options Optional borrowed style QObject guarded by QPointer; null uses compiled fallback styles.
     * @note Equal pointer is a no-op. Disconnects previous signals and connects changed() to bounds/repaint; binding itself only schedules repaint, without immediately recomputing bounds. Does not own/reparent the options.
     */
    void setOptions(ProfilePlotOptions *options);
    /**
     * @return Borrowed guarded options pointer, null if absent/destroyed.
     */
    [[nodiscard]] ProfilePlotOptions *options() const { return m_options; }

    /*!
     * \brief Sets axis labels for the plot frame.  Defaults to
     *        "Distance" / "Elevation" if not set.
     * @param[in] xLabel Horizontal caption including units if desired; drawing may append vertical-exaggeration text or substitute Schematic spacing.
     * @param[in] yLabel Elevation caption including units if desired.
     * @note Copies strings/repaints without changing the data unit system.
     */
    void setAxisLabels(const QString &xLabel, const QString &yLabel);

    /*!
     * \brief Hit-test: returns the path-node index whose manhole glyph is
     *        nearest \p widgetPos within a reasonable tolerance, or -1.
     *        Virtual junctions count — their dashed rectangle shares the
     *        manhole footprint.  Used by Stage 6's edit-in-place context menu.
     * @param[in] widgetPos Logical widget-pixel click position.
     * @return Closest-X node whose invert-to-rim tube rectangle contains the point with four-pixel slack, or -1. Includes virtual/LID nodes and does not require the glyph to be currently inside the plot clip.
     */
    [[nodiscard]] int nodeIndexAt(const QPoint &widgetPos) const;

    /*!
     * \brief Hit-test: returns the path-link index whose conduit / glyph
     *        passes through \p widgetPos within a reasonable tolerance, or -1.
     * @param[in] widgetPos Logical widget-pixel position.
     * @return First path-order hit, or -1. Uses tube bands with three-pixel slack/four-pixel end inset, actual weir block, pump/outlet glyph bounds or an eight-pixel stroked schematic LID connection. Does not itself give nodes precedence; event handlers test them first.
     */
    [[nodiscard]] int linkIndexAt(const QPoint &widgetPos) const;

    // ── Zoom / pan controls (driven by the dialog's toolbar) ─────────────

    /*! Modal interaction state — mutually exclusive.  Default is `Identify`
     *  (clicks select / right-click context-menu). */
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
     * @param[in] m Declared Identify/Pan/ZoomIn/ZoomOut policy; equality is a no-op.
     * @note Sets mode cursor, cancels pan/zoom/axis-click state and hides the rubber band. Does not explicitly cancel an active overlay drag or change the selected-name set.
     */
    void setMode(Mode m);
    /**
     * @return Stored mouse policy, initially Identify; no source state is queried.
     */
    [[nodiscard]] Mode mode() const { return m_mode; }

    /**
     * @param[in] widgetPos Logical widget-pixel position in the bottom/left label gutters.
     * @return Endpoint within a 28-pixel along-axis corner tolerance and 48-pixel label band, or None. Bottom X endpoint tests precede Y tests.
     */
    [[nodiscard]] AxisEdge axisEdgeAt(const QPoint &widgetPos) const;
    /**
     * @param[in] edge Declared endpoint to replace; None is refused.
     * @param[in] value Finite X virtual-chainage or Y elevation coordinate, strictly preserving order with the opposite edge.
     * @return False for nonfinite/order failure/None, otherwise true and switches to manual view, even for an equal same-edge value. Pass only declared enum values.
     * @note Repaints and synchronously emits visibleXRangeChanged() only when X actually changed; no Y-specific signal exists.
     */
    bool setAxisEdgeValue(AxisEdge edge, double value);
    /**
     * @return Normalized virtual-X/elevation rectangle, not real-chainage or pixel coordinates.
     */
    [[nodiscard]] QRectF visibleDataRange() const;

    // ── Shared-x API (attribute tracks pane) ────────────────────────────
    //
    // The tracks pane below the profile reproduces this widget's horizontal
    // pixel mapping exactly — same virtual-chainage x quantity, same left/
    // right gutters — so the two charts stay column-aligned through every
    // zoom and pan. Everything it needs is exposed here rather than
    // duplicated: duplicated constants drift.

    /*! Sets the visible x-range (virtual chainage). Y is untouched. Used by
     *  the synced attribute-tracks pane to push its own pan/zoom back up.
     *  Emits visibleXRangeChanged() (once) when the range actually moves.
     * @param[in] vxMin Finite lower virtual-chainage bound.
     * @param[in] vxMax Finite upper bound strictly above vxMin.
     * @note Rejects invalid/equal requests; otherwise disables fit, replaces X only, repaints and deduplicates/emits visibleXRangeChanged(). No data-extent clamp occurs.
     */
    void setVisibleXRange(double vxMin, double vxMax);

    /*! Per-node virtual chainage, rebuilt by recomputeBounds() on every
     *  setPath()/setSeries(). Size matches the path's node count; empty
     *  before the first setPath().
     * @return Borrowed per-node schematic X vector, rebuilt by bounds computation and invalidated by future configuration/refit operations. Non-conduit links use a display gap even when their raw length is positive; real chainage stays separate.
     */
    [[nodiscard]] const QVector<double> &virtualChainageTable() const { return m_virtualChainage; }

    /*! Maps a virtual x back to real chainage — public so the tracks pane
     *  can label its shared x-axis with real stations, exactly like this
     *  widget's own bottom ticks.
     * @param[in] vx Finite virtual-X coordinate.
     * @return Piecewise-linear real chainage, clamped to real endpoint values outside the virtual range. With fewer than two matching mapping entries returns vx unchanged; caller must keep real/virtual arrays parallel. Zero real-length spans map the full visual gap to one chainage.
     */
    [[nodiscard]] double virtualToRealChainage(double vx) const;

    /*! The fixed horizontal gutters of the plot area, in pixels. The tracks
     *  pane adopts the same values so data columns line up.
     * @return Shared fixed left gutter, 64 logical pixels, for aligned companion panes.
     */
    [[nodiscard]] static int chartLeftMarginPx();
    /**
     * @return Shared fixed right gutter, 16 logical pixels.
     */
    [[nodiscard]] static int chartRightMarginPx();

    /*! Zoom the view rect by \p factor around its centre.  `< 1` zooms in.
     * @param[in] factor Finite positive multiplier of both spans; below one zooms in. Nonpositive is ignored; NaN is not explicitly rejected.
     * @note Keeps the view centre, disables fit, repaints and emits changed X range without clamping to data extents.
     */
    void zoomBy(double factor);

    /*! Reset to the full extent with independently fitted axes (automatic
     *  vertical exaggeration for long, shallow profiles).
     * @note Re-enables fit, recomputes virtual spacing/auto bounds using current widget size and visible output histories, then repaints and emits changed X range. X/Y scales fit independently.
     */
    void fitToExtent();

    /**
     * @return Ratio of pixels per elevation unit to pixels per virtual-X unit. This is a display-scale ratio; artificial non-conduit gaps affect the horizontal basis. Requires finite positive view spans.
     */
    [[nodiscard]] double verticalExaggeration() const;

    /*!
     * \brief Marks a set of element names (nodes or links) as selected so
     *        the matching glyphs render with a highlight outline.  Names
     *        not on the path are silently ignored.  Pass an empty list
     *        to clear all highlights.
     * @param[in] names Exact case-sensitive node/link identities, deduplicated into one shared set; unknown-to-path names are retained but render no highlight.
     * @note An unchanged set is a no-op; otherwise repaints without mutating the model/map selection or emitting selection signals.
     */
    void setSelectedElementNames(const QStringList &names);

signals:
    /*!
     * \brief Emitted whenever the visible x-range (virtual chainage)
     *        changes — fit, zoom, pan, wheel, axis-edge edit, or a new
     *        path/series recomputing the extent. Emitted at most once per
     *        change (values are compared against the last emission).
     *        Consumed by the attribute-tracks pane to stay column-aligned.
     * @param[in] vxMin New lower virtual-X bound.
     * @param[in] vxMax New upper virtual-X bound.
     * @note Synchronous at X mutation sites and deduplicated by exact comparison; first computed range emits because the remembered bounds start NaN. Receivers should use a guarded range setter to avoid echo loops.
     */
    void visibleXRangeChanged(double vxMin, double vxMax);

    /*!
     * \brief Emitted on right-click over a node glyph; consumed by Stage 6
     *        to pop an edit-in-place context menu.
     * @param[in] pathNodeIdx Zero-based current path index. Node hit on right press, regardless of active mouse mode.
     * @param[in] globalPos Global screen-pixel location for the host context menu.
     */
    void nodeRightClicked(int pathNodeIdx, const QPoint &globalPos);

    /*!
     * \brief Emitted on right-click over a link glyph.
     * @param[in] pathLinkIdx Zero-based current path index. Link hit on right press after a node miss, regardless of mode.
     * @param[in] globalPos Global screen-pixel location for the host context menu.
     */
    void linkRightClicked(int pathLinkIdx, const QPoint &globalPos);

    /*!
     * \brief Emitted on right-click that hits neither a node nor a link.
     *        Hosts use this to show a context menu whose element-specific
     *        items (e.g. "Zoom to on map") are disabled but other items
     *        (e.g. "Properties…") remain available.
     * @param[in] globalPos Global screen-pixel position after right press misses all node/link hit regions.
     * @note Does not change selection or create a menu itself.
     */
    void backgroundRightClicked(const QPoint &globalPos);

    /*!
     * \brief Emitted on left-click over a node glyph.  Consumed by
     *        ProfilePlotDialog to drive the main-map selection.
     * @param[in] pathNodeIdx Zero-based current path index. Node hit on an Identify left press after overlay/axis handling.
     */
    void nodeClicked(int pathNodeIdx);

    /*!
     * \brief Emitted on left-click over a conduit / non-conduit glyph.
     * @param[in] pathLinkIdx Zero-based current path index. Link hit on an Identify left press after a node miss.
     */
    void linkClicked(int pathLinkIdx);

    /*!
     * \brief Emitted on left double-click over a node glyph.  Hosts use
     *        this to zoom the main map to the element.
     * @param[in] pathNodeIdx Zero-based current path index. Node hit on an Identify left double click.
     */
    void nodeDoubleClicked(int pathNodeIdx);

    /*!
     * \brief Emitted on left double-click over a link glyph.
     * @param[in] pathLinkIdx Zero-based current path index. Link hit on an Identify left double click after a node miss.
     */
    void linkDoubleClicked(int pathLinkIdx);

    /*!
     * \brief Emitted on left-click in Identify mode when the click misses
     *        every node and link glyph.  Hosts use this to clear the
     *        selection so blank-area clicks act as a deselect.
     * @note Emitted for an Identify left press missing all nodes/links after overlay/axis handling; the host decides whether to clear map selection.
     */
    void backgroundClicked();

protected:
    /**
     * @brief Draw cached section geometry, water fills/lines, LID state, labels/highlights and draggable overlays in their explicit pass order; empty path stops after axes.
     * @param[in,out] event Borrowed Qt dispatch event, never retained/deleted; paint/resize event contents are unused.
     */
    void paintEvent(QPaintEvent *event) override;
    /**
     * @brief Recompute auto bounds only while fit mode is active, then repaint; manual bounds survive resizing.
     * @param[in,out] event Borrowed Qt dispatch event, never retained/deleted; paint/resize event contents are unused.
     */
    void resizeEvent(QResizeEvent *event) override;
    /**
     * @brief Prioritize overlay drags, right-click element signals, unconditional middle pan, endpoint-label editing and left mode actions; Identify emits node/link/background clicks.
     * @param[in,out] event Borrowed Qt dispatch event, never retained/deleted; paint/resize event contents are unused.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @brief Apply active options-overlay dragging, pan or zoom rubber band; otherwise update LID hover diagnostics from copied geometry/state and delegate unhandled movement.
     * @param[in,out] event Borrowed Qt dispatch event, never retained/deleted; paint/resize event contents are unused.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @brief Finish overlays/pan, open an axis editor for a near-stationary endpoint click, or apply clipped rectangle/click zoom and announce changed X range.
     * @param[in,out] event Borrowed Qt dispatch event, never retained/deleted; paint/resize event contents are unused.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @brief Emit node/link double-click signals only for left Identify interactions, with node precedence; other cases delegate to QWidget.
     * @param[in,out] event Borrowed Qt dispatch event, never retained/deleted; paint/resize event contents are unused.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @brief Zoom both axes about pointer with factor 0.85 per vertical angle-wheel step, switch off fit, accept and emit changed X range; zero vertical delta is ignored.
     * @param[in,out] event Borrowed Qt dispatch event, never retained/deleted; paint/resize event contents are unused.
     */
    void wheelEvent(QWheelEvent *event) override;

private:
    // Plot-rect (data area, excluding axis label gutters) in widget pixels.
    /**
     * @brief Resolve cached plot geometry/style without source access.
     * @return Logical-pixel data rectangle with fixed horizontal gutters, dynamic label-aware top margin and at least one pixel per dimension.
     */
    [[nodiscard]] QRectF plotRect() const;

    // Dynamic top margin: grows when node / link labels are visible
    // (rendered above the plot as a secondary axis) so they don't overlap
    // the bottom axis tick labels.
    /**
     * @brief Resolve cached plot geometry/style without source access.
     * @return Sixteen pixels plus enabled node/link label-row height estimates/gaps for current orientation/font/names.
     */
    [[nodiscard]] int topMargin() const;
    /**
     * @brief Resolve cached plot geometry/style without source access.
     * @return Fixed forty logical-pixel bottom reserve.
     */
    [[nodiscard]] int bottomMargin() const;

    // Data-space → pixel-space conversion.
    /**
     * @param[in] chainage Virtual-X coordinate, despite the legacy parameter name.
     * @param[in] elev Elevation in profile model length units.
     * @return Affine logical widget pixel position with inverted Y; outside values extrapolate and positive visible spans are required.
     */
    [[nodiscard]] QPointF dataToPixel(double chainage, double elev) const;

    // ── Virtual-chainage helpers ────────────────────────────────────────
    //
    // Zero-length links (pump/weir/orifice/outlet) would collapse to a single
    // x-pixel using real chainage, so we render in a "virtual" x where each
    // such link contributes a small visual gap.  The axis ticks still display
    // the real chainage at every node — adjacent ticks sharing a value make
    // the gap legible without distorting the distance axis.

    /*! Virtual x for node \p i.  Falls back to real chainage if the virtual
        table is empty (e.g. before the first setPath).
     * @param[in] nodeIdx Path-node index.
     * @return Cached virtual coordinate, otherwise corresponding real chainage if present, otherwise zero (including negative indices).
     */
    [[nodiscard]] double virtualX(int nodeIdx) const;

    /*! Virtual x interpolated along link \p i at fraction \p frac in [0,1].
        For zero-length links the result interpolates across the visual gap.
     * @param[in] linkIdx Path-link index.
     * @param[in] frac Relative fraction, clamped [0,1] for the virtual table.
     * @return Interpolated virtual X; if unavailable falls back to real chainage without fraction clamping, or zero for invalid indices.
     */
    [[nodiscard]] double virtualXAlongLink(int linkIdx, double frac) const;

    // (virtualToRealChainage is declared in the public section — the tracks
    // pane labels its shared x-axis with it.)

    // ── Virtual-junction helpers ────────────────────────────────────────

    /*! True when node \p nodeIdx is a virtual junction — a computational
        break point inside one continuous pipe rather than a physical
        structure.  There is no manhole tube to butt against, so the pipe
        body and every water graphic inside it run through the node's
        chainage uninterrupted (the break is marked by a dashed vertical
        line instead).
     * @param[in] nodeIdx Path-node index.
     * @return True for an in-range VirtualJunction, false otherwise.
     */
    [[nodiscard]] bool isVirtualNode(int nodeIdx) const;

    // LID columns need room for their material patterns. Width is schematic;
    // elevations and layer thicknesses still use the physical Y axis.
    /**
     * @param[in] nodeIdx Path-node index.
     * @return 3.5 logical pixels normally/for invalid indices; LID nodes use up to 24 pixels, limited to a quarter of neighboring pixel gaps with a 3.5-pixel floor. Elevations remain physically scaled.
     */
    [[nodiscard]] double nodeHalfWidthPx(int nodeIdx) const;
    struct LidLinkDrawing {
        QPainterPath connection, body;
    };
    /**
     * @param[in] linkIdx Current path link with consistent endpoint nodes.
     * @return Owned schematic pixel paths for the first applicable non-conduit LID bottom-layer outlet or inlet overflow weir, otherwise empty. Only port/crest elevation is physical; bends/body dimensions are display geometry. No source lookup occurs.
     */
    [[nodiscard]] std::optional<LidLinkDrawing> lidLinkDrawing(int linkIdx) const;
    /**
     * @brief Paint schematic LID-linked connector/body paths using their link kind styles, clipped to the plot.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintLidConnections(QPainter &p) const;
    /**
     * @brief Paint physical porous media/aggregate bands with schematic widths, material patterns and optional compatible pore-saturation tints; open surface ponding is left to normal water fill.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintNodeLidLayers(QPainter &p) const;
    /**
     * @brief Draw optional compatible LID water-table/perched-base lines and Richards outlet-cell head guides from the copied state.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintLidWaterLines(QPainter &p) const;
    /**
     * @param[in] node Current path-node index.
     * @return Borrowed frame node state with exact matching name and porous-layer extents within 1e-5 profile units, or null for invalid index/no compatible recorded geometry. Returned pointer is invalidated by LID-frame replacement.
     */
    const openswmmvis::io::LidNodeState *lidStateForNode(int node) const;
    /**
     * @param[in] node Current path-node index.
     * @param[out] outlet Optional connection-name output, written only when a cell is found.
     * @return Borrowed compatible Richards cell with finite head spanning the chosen outgoing branch invert (1e-6 tolerance), or null. Outgoing on-path branches take priority, then lowest invert within equal priority; authored layer anchor filters cells when present.
     */
    const openswmmvis::io::LidCellState *lidOutletCell(int node, QString *outlet = nullptr) const;

    /*! Pixel-x endpoints for link \p linkIdx's in-pipe water graphics
        (HGL line / fill / envelope).  Each end is inset by the manhole
        tube half-width so the graphic stops at the tube edge, except at a
        virtual junction where the inset is zero.  Short links collapse
        both ends to the link midpoint rather than self-intersecting.
     * @param[in] linkIdx Valid path link with coherent endpoint geometry.
     * @param[out] pxU Upstream pixel-X inset by node half width, or no inset for a virtual node.
     * @param[out] pxD Downstream analog; short links clamp both ends toward the midpoint to prevent inversion.
     */
    void hglEdgePixels(int linkIdx, qreal &pxU, qreal &pxD) const;

    // Recomputes data-space bounds (m_dataXMin, etc.) from m_path + m_series.
    /**
     * @brief Rebuild virtual spacing, bedding floor and auto extents from current snapshots.
     * @note Positive-length conduits keep length; every other link gets 40% of average positive conduit length (one unit fallback). Y includes node rims/inverts, conduit crowns, visible kind-specific extrema, finite terrain and enabled remembered 2D peak. Adds bedding, pixel-glyph/header and percentage margins, replacing visible bounds only in fit mode; always deduplicates/emits changed X. Geometry arrays must be coherent.
     */
    void recomputeBounds();
    /**
     * @param[in] edge Declared endpoint, None refused.
     * @return True after accepted locale/neutral finite text passes setAxisEdgeValue(); false for None/cancel or invalid input. Displays a warning for invalid accepted text and preserves prior bounds.
     */
    bool editAxisEdge(AxisEdge edge);

    // Emits visibleXRangeChanged if m_dataXMin/Max moved since the last
    // emission. Called after every x-mutation site so external consumers
    // (the attribute-tracks pane) see exactly one signal per change.
    /**
     * @brief Compare X bounds against the last emission and synchronously announce exact changes.
     * @note Updates remembered bounds before emitting, enabling receivers to safely use no-op-guarded synchronization setters. No Y notification is generated.
     */
    void emitXRangeIfChanged();

    // Per-layer painters (broken out so the paint pipeline reads top-down).
    /**
     * @brief Paint white plot background/theme axes and numeric ticks. X tick positions use virtual spacing but labels map to real chainage; caption includes display V:H ratio or schematic spacing.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintBackgroundAndAxes(QPainter &p) const;
    /**
     * @brief Draw enabled node/link label rows with leaders above the plot, using current vertical/horizontal/clamped-diagonal policy.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintLabelAxis(QPainter &p) const;
    /**
     * @brief Compose ground-to-crown soil with physical shaft cutouts and structure-following bedding down to the cached bedding floor, honoring terrain toggle and excavated/LID connection shapes.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintSoilFill(QPainter &p) const;
    /**
     * @brief Paint clipped conduit/orifice tubes, open channels/street gutters and inlet-anchored weir/pump/outlet glyphs. Virtual nodes have no pipe end caps; LID-specific connectors are skipped for a separate pass.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintConduits(QPainter &p) const;
    /**
     * @brief Paint physical tubes/rim symbols and virtual-junction break rectangles/inlet markers. Flood indicators examine current HGL bindings including hidden ones, excluding outfalls; LID columns remain schematic in width.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintNodes(QPainter &p) const;
    /*! Truncated stubs of the links a path node connects to that the profile
     *  does NOT follow — drawn behind the manhole tube so the tube caps them.
     *  Model-inflow links go on the upstream side, outflows downstream.
     * @brief Draw option-enabled off-path incident links at fixed schematic length, placing model inflows left/outflows right and leaving virtual nodes unstubbed.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintBranchStubs(QPainter &p) const;
    /*! Per-node plan rose above the rim: one spoke per connected link at its
     *  map bearing, arrowheads showing flow direction, path links
     *  highlighted. Suppressed where nodes are too close to draw one.
     * @brief Draw option-enabled incident-link bearing/flow roses above physical nodes. Suppresses the whole pass when neighboring nodes are too close and omits kNoBearing spokes.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintNodeRoses(QPainter &p) const;
    /**
     * @brief Overlay selected exact-name link then node halos after result graphics, with the same geometry/insets as their normal rendering.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintSelectionHighlights(QPainter &p) const;
    /**
     * @param[in] k Node/link renderer category.
     * @return Physical-node colour from options or compiled kind palette; VirtualJunction remains transparent and unknown kinds white. Returns a value copy without state mutation.
     */
    [[nodiscard]] QColor themeNodeFill(ProfileBuilder::NodeKind k) const;
    /**
     * @param[in] k Node/link renderer category.
     * @return Physical-node outline colour from options/compiled palette; VirtualJunction transparent and unknown kinds black. Returns a value copy without state mutation.
     */
    [[nodiscard]] QColor themeNodeOutline(ProfileBuilder::NodeKind k) const;
    /*! Pen for the dashed rectangle that marks a virtual junction.  Styled
        independently of the physical node kinds, which carry a fill/outline
        colour pair instead.
     * @brief Resolve cached plot geometry/style without source access.
     * @return Bound virtual-junction pen or a one-pixel dark-grey 4/3 dashed FlatCap/MiterJoin fallback.
     */
    [[nodiscard]] QPen themeVirtualJunctionPen() const;
    /**
     * @param[in] k Node/link renderer category.
     * @return Kind fill colour from options or compiled palette, white for unknown kinds. Returns a value copy without state mutation.
     */
    [[nodiscard]] QColor themeLinkFill(ProfileBuilder::LinkKind k) const;
    /**
     * @param[in] k Node/link renderer category.
     * @return Kind outline colour from options or compiled palette, black for unknown kinds. Returns a value copy without state mutation.
     */
    [[nodiscard]] QColor themeLinkOutline(ProfileBuilder::LinkKind k) const;
    /**
     * @brief Resolve cached plot geometry/style without source access.
     * @return Bound soil colour or compiled translucent earth tone.
     */
    [[nodiscard]] QColor themeSoilFill() const;
    /**
     * @brief Resolve cached plot geometry/style without source access.
     * @return Bound bedding colour or compiled translucent darker earth tone.
     */
    [[nodiscard]] QColor themeBeddingFill() const;
    /**
     * @brief Resolve cached plot geometry/style without source access.
     * @return Bound conduit pen or dark-grey 1.5-pixel solid fallback.
     */
    [[nodiscard]] QPen themeConduitOutlinePen() const;
    /**
     * @param[in] k Node/link renderer category.
     * @return Complete options pen for the declared kind; otherwise a solid 1.5-pixel pen using themeLinkOutline(). Returns a value copy without state mutation.
     */
    [[nodiscard]] QPen themeLinkOutlinePen(ProfileBuilder::LinkKind k) const;
    /**
     * @brief Resolve cached plot geometry/style without source access.
     * @return Bound pavement brush or dark-grey diagonal hatch fallback.
     */
    [[nodiscard]] QBrush themeStreetInvertBrush() const;

    // Per-kind dispatch helpers.  Each takes a series index into m_series.
    // The series carries its own resolved pen/brush; the helpers reach into
    // `series.derived` for the appropriate per-period or envelope array.
    //
    // Current-time series (HGL / EGL / WaterSurface) animate with
    // m_currentPeriod; envelope series (Max/Min HGL / EGL / WaterSurface)
    // are static.
    /**
     * @brief Stroke current HGL/EGL/free-surface per-link values. Missing rows/finite endpoints skip segments. HGL uses invert-floor endpoints and excludes pumps/outlets; a NoPen input is promoted to SolidLine in this current-curve path.
     * @param[in,out] p Active painter, with plot clipping applied by this pass.
     * @param[in] seriesIdx Binding index; invalid/null-derived or incompatible/short rows produce no drawing. Caller gates binding.visible where needed; all values must already use path units.
     */
    void paintSeriesCurrentLine(QPainter &p, int seriesIdx) const;
    /**
     * @brief Draw a static envelope with brush/pen gates. Fill uses per-link invert/crown clipping; MaxHGL outline uses per-link invert-floor segments, while other envelopes connect retained finite node points (potentially across missing nodes).
     * @param[in,out] p Active painter, with plot clipping applied by this pass.
     * @param[in] seriesIdx Binding index; invalid/null-derived or incompatible/short rows produce no drawing. Caller gates binding.visible where needed; all values must already use path units.
     */
    void paintSeriesEnvelope(QPainter &p, int seriesIdx) const;

    // Envelope-series only — fills the manhole shaft at each node from
    // the node invert up to the envelope value (capped at rim).  Counterpart
    // to paintSeriesEnvelope's per-link fill so the Max/Min HGL / EGL /
    // WaterSurface band reads continuously across nodes instead of leaving
    // empty manhole columns between links.
    /**
     * @brief Fill nonvirtual physical shafts from their water floor to the rim-clamped envelope, only with a non-NoBrush binding; LID water floors exclude porous media.
     * @param[in,out] p Active painter, with plot clipping applied by this pass.
     * @param[in] seriesIdx Binding index; invalid/null-derived or incompatible/short rows produce no drawing. Caller gates binding.visible where needed; all values must already use path units.
     */
    void paintNodeEnvelopeFill(QPainter &p, int seriesIdx) const;

    // HGL-series only — in-pipe and in-manhole water fills.  Drawn for
    // every visible HGL series so multi-model overlay reads correctly
    // (each model gets its own water tint).
    /**
     * @brief Fill finite current HGL per renderable non-LID link with invert/crown clipping and manhole-edge insets. A NoBrush binding falls back to configured/source-tint HGL fill rather than suppressing it; visibility is gated by the caller/options.
     * @param[in,out] p Active painter, with plot clipping applied by this pass.
     * @param[in] seriesIdx Binding index; invalid/null-derived or incompatible/short rows produce no drawing. Caller gates binding.visible where needed; all values must already use path units.
     */
    void paintHglFill(QPainter &p, int seriesIdx) const;
    /**
     * @brief Fill current HGL in nonvirtual physical shafts up to rim, starting at invert or the LID surface-layer bottom. NoBrush uses the HGL fill fallback; porous layers are not treated as a free-water chamber.
     * @param[in,out] p Active painter, with plot clipping applied by this pass.
     * @param[in] seriesIdx Binding index; invalid/null-derived or incompatible/short rows produce no drawing. Caller gates binding.visible where needed; all values must already use path units.
     */
    void paintNodeFill(QPainter &p, int seriesIdx) const;
    // HGL-series only — short horizontal HGL segment at each node,
    // spanning the manhole tube width.  Butts up against the link HGL
    // line at each pipe edge to form a continuous node+link trace.
    /**
     * @brief Bridge the physical-node tube width with a raw current/MaxHGL horizontal head segment. Skips virtual nodes, missing values, unsupported kinds and NoPen.
     * @param[in,out] p Active painter, with plot clipping applied by this pass.
     * @param[in] seriesIdx Binding index; invalid/null-derived or incompatible/short rows produce no drawing. Caller gates binding.visible where needed; all values must already use path units.
     */
    void paintNodeHglLine(QPainter &p, int seriesIdx) const;
    // WaterSurface-series only — in-pipe fill from invert to free-surface
    // depth (caps at rim under pressurization, unlike HGL).
    /**
     * @brief Fill current WaterSurface in conduit links only, clamped between invert and crown. Requires a non-NoBrush binding and a node-sized current row; does not apply an openTop exemption.
     * @param[in,out] p Active painter, with plot clipping applied by this pass.
     * @param[in] seriesIdx Binding index; invalid/null-derived or incompatible/short rows produce no drawing. Caller gates binding.visible where needed; all values must already use path units.
     */
    void paintWaterSurfaceFill(QPainter &p, int seriesIdx) const;
    // 2D inundation overlay — translucent band from the mesh bed up to the
    // 2D water surface plus a WSE line, per contiguous wet run of stations.
    // Drawn after the soil so it reads as water standing on the ground,
    // and before the 1D fills/lines so the network's own HGL stays on top.
    /**
     * @brief Clip each finite independent overlay interval against both source bed and drawn ground, splitting at ground/axis knots and mapping real chainage to virtual X; draw compound wet fill/line below 1D graphics.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintSurface2D(QPainter &p) const;
    // Elevation of the DRAWN ground line at a real chainage — the sampled
    // ground (DEM / 2D mesh) or rim-to-rim — so the 2D band fills exactly
    // to it.
    /**
     * @param[in] realX Finite real path chainage.
     * @return Interpolated finite terrain samples when useTerrainGround and available, otherwise rim-to-rim elevation. Source samples must be ordered; invalid entries are skipped, endpoint values clamp where possible, and entirely missing input returns NaN. Excavated/shaft drawing cutouts are not reproduced in this lookup.
     */
    [[nodiscard]] double groundElevAtReal(double realX) const;
    // Real path chainage → virtual x (zero-length links get a visual gap).
    // Shared by the terrain ground line and the 2D overlay.
    /**
     * @param[in] realX Finite real chainage.
     * @return Piecewise-linear virtual position clamped to endpoints, or input unchanged when fewer than two mapping entries exist/no interval is found. Parallel ordered real/virtual tables are caller-established invariants.
     */
    [[nodiscard]] double realChainageToVirtualX(double realX) const;

    /**
     * @brief Build visible-series, enabled 2D-overlay and LID-state swatches/status rows, then cache the last-painted drag rectangle. Clears hit rectangle when hidden/empty; styling is a display snapshot and does not fetch data.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintLegend(QPainter &p) const;
    /**
     * @brief Draw the valid copied timestamp using configured format/corner/font/offset, caching a draggable hit rectangle; invalid/hidden/empty text clears it.
     * @param[in,out] p Active painter in logical widget pixels; data is read only from current snapshots/options, without model/result-source IO.
     */
    void paintTimeLabel(QPainter &p) const;

    // Returns true if `kind` is a current-time (animated) series.  All
    // other kinds are envelope series.
    /**
     * @param[in] k Output kind.
     * @return True only for HGL/EGL/WaterSurface; all other values follow envelope dispatch.
     */
    [[nodiscard]] static bool isCurrentTimeKind(ProfileBuilder::OutputKind k);
    // Returns the appropriate per-period array on `derived` for the kind.
    // Returns an empty vector for envelope kinds.
    /**
     * @param[in] derived Borrowed computed source history.
     * @param[in] kind Animated output kind.
     * @return Borrowed corresponding period-major array, or a process-lifetime empty vector for envelope/unknown kinds. Nonempty reference lifetime follows derived.
     */
    [[nodiscard]] static const QVector<QVector<double>> &
    currentTimeArray(const ProfileBuilder::SourceDerived &derived, ProfileBuilder::OutputKind kind);
    // Returns the envelope array on `derived` for the kind.  Returns an
    // empty vector for current-time kinds.
    /**
     * @param[in] derived Borrowed source extrema.
     * @param[in] kind Min/max HGL/EGL/free-surface kind.
     * @return Borrowed corresponding node-major envelope, or static empty for current/unknown kinds; nonempty lifetime follows derived.
     */
    [[nodiscard]] static const QVector<double> &
    envelopeArray(const ProfileBuilder::SourceDerived &derived, ProfileBuilder::OutputKind kind);

    ProfileBuilder::PathStatic m_path;
    openswmmvis::io::LidStateFrame m_lidWaterState;
    bool m_showLidWater = true, m_showLidHead = false;
    QVector<SeriesBinding> m_series;
    QVector<Surface2DSample> m_surface2D; /*!< 2D inundation overlay stations */
    double m_surface2DMaxWse = std::numeric_limits<double>::quiet_NaN();
    LayerToggles m_toggles;
    QPointer<ProfilePlotOptions> m_options; /*!< theming/legend source */
    QSet<QString> m_selectedNames;          /*!< highlight set */
    int m_currentSrc = -1;
    int m_currentPeriod = -1;
    QDateTime m_currentDateTime;
    QString m_xLabel = QStringLiteral("Distance");
    QString m_yLabel = QStringLiteral("Elevation");

    // Last-painted bounding rects of the legend and timestamp chips.  Used
    // for hit-testing in mousePressEvent so the user can grab and drag
    // either overlay.  Updated inside paintLegend / paintTimeLabel.
    mutable QRectF m_legendRect;
    mutable QRectF m_timeLabelRect;
    enum class OverlayDrag { None = 0, Legend, TimeLabel };
    OverlayDrag m_overlayDrag = OverlayDrag::None;
    QPoint m_overlayDragLastPos;

    // Per-node virtual x (matches m_path.chainage size).  Rebuilt in
    // recomputeBounds().  Zero-length links contribute m_virtualGap; all
    // others contribute their real length.
    QVector<double> m_virtualChainage;
    double m_virtualGap = 0.0;
    double m_beddingFloorElev = 0.0;
    bool m_haveBeddingFloor = false;

    // Auto-computed data extent (re-derived on setPath / setSeries).
    double m_autoXMin = 0.0;
    double m_autoXMax = 1.0;
    double m_autoYMin = 0.0;
    double m_autoYMax = 1.0;
    // Currently-visible view bounds.  Equal to the auto extent when the
    // user hasn't zoomed/panned (m_fitMode == true).
    double m_dataXMin = 0.0;
    double m_dataXMax = 1.0;
    double m_dataYMin = 0.0;
    double m_dataYMax = 1.0;
    bool m_fitMode = true;
    // Last-emitted visibleXRangeChanged values (see emitXRangeIfChanged).
    double m_lastEmittedXMin = std::numeric_limits<double>::quiet_NaN();
    double m_lastEmittedXMax = std::numeric_limits<double>::quiet_NaN();
    Mode m_mode = Mode::Identify;
    bool m_panActive = false;  // pan-drag in progress
    bool m_zoomActive = false; // zoom-rubberband in progress
    AxisEdge m_pressedAxisEdge = AxisEdge::None;
    QPoint m_lastMousePos;
    QPoint m_zoomAnchor; // start of rubberband
    class QRubberBand *m_rubberBand = nullptr;
};

#endif // PROFILE_PLOT_WIDGET_H
