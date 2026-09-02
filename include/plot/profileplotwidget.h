/*!
 * \file   profileplotwidget.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BC's longitudinal section renderer.
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
 */

#ifndef PROFILE_PLOT_WIDGET_H
#define PROFILE_PLOT_WIDGET_H

#include "plot/profilebuilder.h"
#include "plot/profileplotoptions.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QPen>
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
     *        because EGL fill (velocity-head band) needs both HGL and EGL
     *        from the same source.
     *
     *        Styling is fully resolved before reaching the widget — the
     *        caller (typically ProfilePlotDialog) merges the layer's
     *        per-output pen/brush with any per-series override and hands
     *        the final QPen / QBrush in.  No theme fallback inside the
     *        widget.
     */
    struct SeriesBinding
    {
        QString                                        label;
        QColor                                         color;
        ProfileBuilder::OutputKind                     kind   = ProfileBuilder::OutputKind::HGL;
        std::shared_ptr<ProfileBuilder::SourceDerived> derived;
        QPen                                           pen;
        QBrush                                         brush;
        bool                                           visible = true;
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
    struct LayerToggles
    {
        enum LabelOrientation { Vertical = 0, Horizontal = 1, Diagonal = 2 };

        // Per-output user preferences — read by the dialog, ignored by the
        // widget.  Default matches the historic look: HGL + EGL current,
        // Max HGL band on, Max EGL off.
        // Live HGL split into independent line + fill toggles. The
        // legend still shows a single HGL entry whenever either is on.
        bool             currentHglLine   = true;
        bool             currentHglFill   = true;
        bool             currentEgl       = true;
        bool             maxHglBand       = false;
        bool             maxHglLine       = false;
        // EGL has no fill — the only knob is the Max-EGL envelope line.
        bool             maxEglLine       = false;

        // Used directly by the widget.
        bool             showNodeLabels   = false;   /*!< secondary axis: node IDs */
        bool             showLinkLabels   = false;   /*!< secondary axis: link names */
        bool             inlineNodeLabels = false;   /*!< node IDs over the glyph */
        LabelOrientation labelOrientation = Vertical;
        int              labelAngleDeg    = 45;      /*!< only used when LabelOrientation == Diagonal */
        bool             useTerrainGround = false;   /*!< draw ground line from DEM samples */
    };

    explicit ProfilePlotWidget(QWidget *parent = nullptr);

    /*!
     * \brief Sets the static path + chainage to render.  Triggers a repaint.
     */
    void setPath(const ProfileBuilder::PathStatic &path);

    /*!
     * \brief Replaces all rendered series at once.  Order matters only for
     *        z-order between equal-kind series (e.g. when overlaying HGL
     *        from two models, the later series paints on top).
     */
    void setSeries(const QVector<SeriesBinding> &series);
    [[nodiscard]] const QVector<SeriesBinding> &series() const { return m_series; }

    /*!
     * \brief Selects the time-step shown by current-time series (HGL / EGL
     *        / WaterSurface).  `seriesIdx` is the source-of-truth for the
     *        animation cursor; other current-time series advance in lock-
     *        step.  Pass `-1` for `period` to hide the animation lines.
     */
    void setCurrentPeriod(int seriesIdx, int period);

    /*!
     * \brief Sets the wall-clock datetime corresponding to the current
     *        period.  Used by the on-plot timestamp overlay.  Passing an
     *        invalid QDateTime hides the overlay.
     */
    void setCurrentDateTime(const QDateTime &dt);

    /*!
     * \brief Convenience overload: applies `period` to the primary
     *        (series index 0).  Per-source time-mapping is the dialog's
     *        responsibility — the widget doesn't translate dates.
     */
    void setCurrentPeriod(int period);

    void setLayerToggles(const LayerToggles &toggles);
    [[nodiscard]] LayerToggles layerToggles() const;

    /*!
     * \struct Surface2DSample
     * \brief One station of the 2D inundation overlay: the active 2D results
     *        layer's water surface sampled where the path crosses the mesh.
     *        `chainage` is REAL path chainage (converted to virtual x when
     *        painting, like terrain samples). `bed` is the mesh bed elevation
     *        at the station; `wse` is bed + interpolated depth, or NaN when
     *        the station is dry / off-mesh / has no data (a gap).
     */
    struct Surface2DSample
    {
        double chainage = 0.0;
        double bed      = std::numeric_limits<double>::quiet_NaN();
        double wse      = std::numeric_limits<double>::quiet_NaN();
    };

    /*!
     * \brief Replaces the 2D inundation overlay stations (static geometry +
     *        the current frame's WSE). Pass an empty vector to clear. The
     *        host re-sends the vector with fresh `wse` values on every
     *        animation tick; `ProfilePlotOptions::show2DInundation` gates
     *        drawing. Widens the y-extent to the wettest station.
     */
    void setSurface2DSamples(const QVector<Surface2DSample> &samples);
    [[nodiscard]] const QVector<Surface2DSample> &surface2DSamples() const
    { return m_surface2D; }

    /*!
     * \brief Binds a ProfilePlotOptions object — the widget's theming
     *        (per-type fills/outlines, soil colours, line widths,
     *        legend position/font/opacity) is read from it.  Pass
     *        nullptr to fall back to the hardcoded defaults.  The
     *        widget connects to `changed()` and repaints automatically.
     */
    void setOptions(ProfilePlotOptions *options);
    [[nodiscard]] ProfilePlotOptions *options() const { return m_options; }

    /*!
     * \brief Sets axis labels for the plot frame.  Defaults to
     *        "Chainage" / "Elevation" if not set.
     */
    void setAxisLabels(const QString &xLabel, const QString &yLabel);

    /*!
     * \brief Hit-test: returns the path-node index whose manhole glyph is
     *        nearest \p widgetPos within a reasonable tolerance, or -1.
     *        Virtual junctions count — their dashed rectangle shares the
     *        manhole footprint.  Used by Stage 6's edit-in-place context menu.
     */
    [[nodiscard]] int nodeIndexAt(const QPoint &widgetPos) const;

    /*!
     * \brief Hit-test: returns the path-link index whose conduit / glyph
     *        passes through \p widgetPos within a reasonable tolerance, or -1.
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

    void setMode(Mode m);
    [[nodiscard]] Mode mode() const { return m_mode; }

    [[nodiscard]] AxisEdge axisEdgeAt(const QPoint &widgetPos) const;
    bool setAxisEdgeValue(AxisEdge edge, double value);
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
     *  Emits visibleXRangeChanged() (once) when the range actually moves. */
    void setVisibleXRange(double vxMin, double vxMax);

    /*! Per-node virtual chainage, rebuilt by recomputeBounds() on every
     *  setPath()/setSeries(). Size matches the path's node count; empty
     *  before the first setPath(). */
    [[nodiscard]] const QVector<double> &virtualChainageTable() const
    { return m_virtualChainage; }

    /*! Maps a virtual x back to real chainage — public so the tracks pane
     *  can label its shared x-axis with real stations, exactly like this
     *  widget's own bottom ticks. */
    [[nodiscard]] double virtualToRealChainage(double vx) const;

    /*! The fixed horizontal gutters of the plot area, in pixels. The tracks
     *  pane adopts the same values so data columns line up. */
    [[nodiscard]] static int chartLeftMarginPx();
    [[nodiscard]] static int chartRightMarginPx();

    /*! Zoom the view rect by \p factor around its centre.  `< 1` zooms in. */
    void zoomBy(double factor);

    /*! Reset the view rect to the auto-computed data extent (Fit). */
    void fitToExtent();

    /*!
     * \brief Marks a set of element names (nodes or links) as selected so
     *        the matching glyphs render with a highlight outline.  Names
     *        not on the path are silently ignored.  Pass an empty list
     *        to clear all highlights.
     */
    void setSelectedElementNames(const QStringList &names);

signals:
    /*!
     * \brief Emitted whenever the visible x-range (virtual chainage)
     *        changes — fit, zoom, pan, wheel, axis-edge edit, or a new
     *        path/series recomputing the extent. Emitted at most once per
     *        change (values are compared against the last emission).
     *        Consumed by the attribute-tracks pane to stay column-aligned.
     */
    void visibleXRangeChanged(double vxMin, double vxMax);

    /*!
     * \brief Emitted on right-click over a node glyph; consumed by Stage 6
     *        to pop an edit-in-place context menu.
     */
    void nodeRightClicked(int pathNodeIdx, const QPoint &globalPos);

    /*!
     * \brief Emitted on right-click over a link glyph.
     */
    void linkRightClicked(int pathLinkIdx, const QPoint &globalPos);

    /*!
     * \brief Emitted on right-click that hits neither a node nor a link.
     *        Hosts use this to show a context menu whose element-specific
     *        items (e.g. "Zoom to on map") are disabled but other items
     *        (e.g. "Properties…") remain available.
     */
    void backgroundRightClicked(const QPoint &globalPos);

    /*!
     * \brief Emitted on left-click over a node glyph.  Consumed by
     *        ProfilePlotDialog to drive the main-map selection.
     */
    void nodeClicked(int pathNodeIdx);

    /*!
     * \brief Emitted on left-click over a conduit / non-conduit glyph.
     */
    void linkClicked(int pathLinkIdx);

    /*!
     * \brief Emitted on left double-click over a node glyph.  Hosts use
     *        this to zoom the main map to the element.
     */
    void nodeDoubleClicked(int pathNodeIdx);

    /*!
     * \brief Emitted on left double-click over a link glyph.
     */
    void linkDoubleClicked(int pathLinkIdx);

    /*!
     * \brief Emitted on left-click in Identify mode when the click misses
     *        every node and link glyph.  Hosts use this to clear the
     *        selection so blank-area clicks act as a deselect.
     */
    void backgroundClicked();

protected:
    void paintEvent       (QPaintEvent  *event) override;
    void resizeEvent      (QResizeEvent *event) override;
    void mousePressEvent  (QMouseEvent  *event) override;
    void mouseMoveEvent   (QMouseEvent  *event) override;
    void mouseReleaseEvent(QMouseEvent  *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent       (QWheelEvent  *event) override;

private:
    // Plot-rect (data area, excluding axis label gutters) in widget pixels.
    [[nodiscard]] QRectF plotRect() const;

    // Dynamic top margin: grows when node / link labels are visible
    // (rendered above the plot as a secondary axis) so they don't overlap
    // the bottom axis tick labels.
    [[nodiscard]] int    topMargin()    const;
    [[nodiscard]] int    bottomMargin() const;

    // Data-space → pixel-space conversion.
    [[nodiscard]] QPointF dataToPixel(double chainage, double elev) const;

    // ── Virtual-chainage helpers ────────────────────────────────────────
    //
    // Zero-length links (pump/weir/orifice/outlet) would collapse to a single
    // x-pixel using real chainage, so we render in a "virtual" x where each
    // such link contributes a small visual gap.  The axis ticks still display
    // the real chainage at every node — adjacent ticks sharing a value make
    // the gap legible without distorting the distance axis.

    /*! Virtual x for node \p i.  Falls back to real chainage if the virtual
        table is empty (e.g. before the first setPath). */
    [[nodiscard]] double virtualX(int nodeIdx) const;

    /*! Virtual x interpolated along link \p i at fraction \p frac in [0,1].
        For zero-length links the result interpolates across the visual gap. */
    [[nodiscard]] double virtualXAlongLink(int linkIdx, double frac) const;

    // (virtualToRealChainage is declared in the public section — the tracks
    // pane labels its shared x-axis with it.)

    // ── Virtual-junction helpers ────────────────────────────────────────

    /*! True when node \p nodeIdx is a virtual junction — a computational
        break point inside one continuous pipe rather than a physical
        structure.  There is no manhole tube to butt against, so the pipe
        body and every water graphic inside it run through the node's
        chainage uninterrupted (the break is marked by a dashed vertical
        line instead). */
    [[nodiscard]] bool isVirtualNode(int nodeIdx) const;

    /*! Pixel-x endpoints for link \p linkIdx's in-pipe water graphics
        (HGL line / fill / envelope).  Each end is inset by the manhole
        tube half-width so the graphic stops at the tube edge, except at a
        virtual junction where the inset is zero.  Short links collapse
        both ends to the link midpoint rather than self-intersecting. */
    void hglEdgePixels(int linkIdx, qreal &pxU, qreal &pxD) const;

    // Recomputes data-space bounds (m_dataXMin, etc.) from m_path + m_series.
    void recomputeBounds();
    bool editAxisEdge(AxisEdge edge);

    // Emits visibleXRangeChanged if m_dataXMin/Max moved since the last
    // emission. Called after every x-mutation site so external consumers
    // (the attribute-tracks pane) see exactly one signal per change.
    void emitXRangeIfChanged();

    // Per-layer painters (broken out so the paint pipeline reads top-down).
    void paintBackgroundAndAxes  (QPainter &p) const;
    void paintLabelAxis          (QPainter &p) const;
    void paintSoilFill           (QPainter &p) const;
    void paintConduits           (QPainter &p) const;
    void paintNodes              (QPainter &p) const;
    /*! Truncated stubs of the links a path node connects to that the profile
     *  does NOT follow — drawn behind the manhole tube so the tube caps them.
     *  Model-inflow links go on the upstream side, outflows downstream. */
    void paintBranchStubs        (QPainter &p) const;
    /*! Per-node plan rose above the rim: one spoke per connected link at its
     *  map bearing, arrowheads showing flow direction, path links
     *  highlighted. Suppressed where nodes are too close to draw one. */
    void paintNodeRoses          (QPainter &p) const;
    void paintSelectionHighlights(QPainter &p) const;
    [[nodiscard]] QColor themeNodeFill   (ProfileBuilder::NodeKind k) const;
    [[nodiscard]] QColor themeNodeOutline(ProfileBuilder::NodeKind k) const;
    /*! Pen for the dashed rectangle that marks a virtual junction.  Styled
        independently of the physical node kinds, which carry a fill/outline
        colour pair instead. */
    [[nodiscard]] QPen   themeVirtualJunctionPen() const;
    [[nodiscard]] QColor themeLinkFill   (ProfileBuilder::LinkKind k) const;
    [[nodiscard]] QColor themeLinkOutline(ProfileBuilder::LinkKind k) const;
    [[nodiscard]] QColor themeSoilFill   () const;
    [[nodiscard]] QColor themeBeddingFill() const;
    [[nodiscard]] QPen   themeConduitOutlinePen() const;
    [[nodiscard]] QPen   themeLinkOutlinePen(ProfileBuilder::LinkKind k) const;

    // Per-kind dispatch helpers.  Each takes a series index into m_series.
    // The series carries its own resolved pen/brush; the helpers reach into
    // `series.derived` for the appropriate per-period or envelope array.
    //
    // Current-time series (HGL / EGL / WaterSurface) animate with
    // m_currentPeriod; envelope series (Max/Min HGL / EGL / WaterSurface)
    // are static.
    void paintSeriesCurrentLine  (QPainter &p, int seriesIdx) const;
    void paintSeriesEnvelope     (QPainter &p, int seriesIdx) const;

    // Envelope-series only — fills the manhole shaft at each node from
    // the node invert up to the envelope value (capped at rim).  Counterpart
    // to paintSeriesEnvelope's per-link fill so the Max/Min HGL / EGL /
    // WaterSurface band reads continuously across nodes instead of leaving
    // empty manhole columns between links.
    void paintNodeEnvelopeFill   (QPainter &p, int seriesIdx) const;

    // HGL-series only — in-pipe and in-manhole water fills.  Drawn for
    // every visible HGL series so multi-model overlay reads correctly
    // (each model gets its own water tint).
    void paintHglFill            (QPainter &p, int seriesIdx) const;
    void paintNodeFill           (QPainter &p, int seriesIdx) const;
    // HGL-series only — short horizontal HGL segment at each node,
    // spanning the manhole tube width.  Butts up against the link HGL
    // line at each pipe edge to form a continuous node+link trace.
    void paintNodeHglLine        (QPainter &p, int seriesIdx) const;
    // WaterSurface-series only — in-pipe fill from invert to free-surface
    // depth (caps at rim under pressurization, unlike HGL).
    void paintWaterSurfaceFill   (QPainter &p, int seriesIdx) const;
    // 2D inundation overlay — translucent band from the mesh bed up to the
    // 2D water surface plus a WSE line, per contiguous wet run of stations.
    // Drawn after the soil so it reads as water standing on the ground,
    // and before the 1D fills/lines so the network's own HGL stays on top.
    void paintSurface2D          (QPainter &p) const;
    // True when the 2D overlay is on and has stations: the mesh bed then
    // replaces the rim-to-rim line as the drawn ground (a DEM still wins).
    [[nodiscard]] bool surface2DGroundActive() const;
    // Elevation of the DRAWN ground line at a real chainage — DEM samples,
    // 2D mesh bed, or rim-to-rim — so the 2D band fills exactly to it.
    [[nodiscard]] double groundElevAtReal(double realX) const;
    // Real path chainage → virtual x (zero-length links get a visual gap).
    // Shared by the terrain ground line and the 2D overlay.
    [[nodiscard]] double realChainageToVirtualX(double realX) const;

    void paintLegend             (QPainter &p) const;
    void paintTimeLabel          (QPainter &p) const;

    // Returns true if `kind` is a current-time (animated) series.  All
    // other kinds are envelope series.
    [[nodiscard]] static bool isCurrentTimeKind(ProfileBuilder::OutputKind k);
    // Returns the appropriate per-period array on `derived` for the kind.
    // Returns an empty vector for envelope kinds.
    [[nodiscard]] static const QVector<QVector<double>> &
        currentTimeArray(const ProfileBuilder::SourceDerived &derived,
                         ProfileBuilder::OutputKind kind);
    // Returns the envelope array on `derived` for the kind.  Returns an
    // empty vector for current-time kinds.
    [[nodiscard]] static const QVector<double> &
        envelopeArray(const ProfileBuilder::SourceDerived &derived,
                      ProfileBuilder::OutputKind kind);

    ProfileBuilder::PathStatic   m_path;
    QVector<SeriesBinding>       m_series;
    QVector<Surface2DSample>     m_surface2D;      /*!< 2D inundation overlay stations */
    double                       m_surface2DMaxWse = std::numeric_limits<double>::quiet_NaN();
    LayerToggles                 m_toggles;
    QPointer<ProfilePlotOptions> m_options;        /*!< theming/legend source */
    QSet<QString>                m_selectedNames;  /*!< highlight set */
    int                          m_currentSrc    = -1;
    int                          m_currentPeriod = -1;
    QDateTime                    m_currentDateTime;
    QString                      m_xLabel        = QStringLiteral("Distance");
    QString                      m_yLabel        = QStringLiteral("Elevation");

    // Last-painted bounding rects of the legend and timestamp chips.  Used
    // for hit-testing in mousePressEvent so the user can grab and drag
    // either overlay.  Updated inside paintLegend / paintTimeLabel.
    mutable QRectF               m_legendRect;
    mutable QRectF               m_timeLabelRect;
    enum class OverlayDrag { None = 0, Legend, TimeLabel };
    OverlayDrag                  m_overlayDrag      = OverlayDrag::None;
    QPoint                       m_overlayDragLastPos;

    // Per-node virtual x (matches m_path.chainage size).  Rebuilt in
    // recomputeBounds().  Zero-length links contribute m_virtualGap; all
    // others contribute their real length.
    QVector<double>              m_virtualChainage;
    double                       m_virtualGap        = 0.0;
    double                       m_beddingFloorElev  = 0.0;
    bool                         m_haveBeddingFloor  = false;

    // Auto-computed data extent (re-derived on setPath / setSeries).
    double                       m_autoXMin   = 0.0;
    double                       m_autoXMax   = 1.0;
    double                       m_autoYMin   = 0.0;
    double                       m_autoYMax   = 1.0;
    // Currently-visible view bounds.  Equal to the auto extent when the
    // user hasn't zoomed/panned (m_fitMode == true).
    double                       m_dataXMin   = 0.0;
    double                       m_dataXMax   = 1.0;
    double                       m_dataYMin   = 0.0;
    double                       m_dataYMax   = 1.0;
    bool                         m_fitMode    = true;
    // Last-emitted visibleXRangeChanged values (see emitXRangeIfChanged).
    double                       m_lastEmittedXMin = std::numeric_limits<double>::quiet_NaN();
    double                       m_lastEmittedXMax = std::numeric_limits<double>::quiet_NaN();
    Mode                         m_mode       = Mode::Identify;
    bool                         m_panActive  = false;     // pan-drag in progress
    bool                         m_zoomActive = false;     // zoom-rubberband in progress
    AxisEdge                     m_pressedAxisEdge = AxisEdge::None;
    QPoint                       m_lastMousePos;
    QPoint                       m_zoomAnchor;             // start of rubberband
    class QRubberBand           *m_rubberBand = nullptr;
};

#endif // PROFILE_PLOT_WIDGET_H
