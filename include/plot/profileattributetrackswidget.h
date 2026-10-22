/**
 * @file      profileattributetrackswidget.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Attribute-tracks pane: N stacked mini-charts ("tracks"), one per
 *         selected result attribute, sharing the profile plot's x-axis.
 *
 * Same technology as ProfilePlotWidget — plain QWidget + QPainter — because
 * pixel-exact column alignment with the profile above is the whole point:
 * the pane adopts the profile's left/right gutters and its virtual-chainage
 * x quantity, and the two stay locked through zoom/pan via
 * setVisibleXRange() / visibleXRangeChanged().
 *
 * Data model: the host dialog pushes fully-resolved `Track`s (title, pen,
 * per-source sampled profiles). The widget owns no fetching, no layers, no
 * attribute knowledge beyond node-vs-link rendering:
 *   - node attributes draw a polyline through the value at each node's
 *     virtual x;
 *   - link attributes draw one horizontal segment per link (a link's flow /
 *     velocity is a single value over its length — interpolating across
 *     nodes would invent data), with no vertical connectors.
 *
 * Animation: setCurrentPeriod() indexes the pre-fetched arrays — nothing is
 * fetched per frame (fetch-once / index-per-frame, the 2D-profile pattern).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_PROFILEATTRIBUTETRACKSWIDGET_H
#define OPENSWMMVIS_PLOT_PROFILEATTRIBUTETRACKSWIDGET_H

#include "plot/plotattribute.h"
#include "plot/profileattributesampler.h"

#include <QPen>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>

class ProfileAttributeTrackOptions;

class ProfileAttributeTracksWidget : public QWidget
{
    Q_OBJECT

public:
    /*! One source's sampled data for one track. `color` is the source tint
     *  (SWMMResultsLayer::profileLineColor) used to distinguish overlaid
     *  scenarios; with a single source the track pen's own color wins. */
    struct SourceProfile {
        QString label;
        QColor color;
        bool primary = false; ///< envelope drawn for the primary only
        std::shared_ptr<const ProfileAttributeSampler::AttributeProfile> data;
    };

    /*! One track, fully resolved by the host dialog. */
    struct Track {
        openswmmvis::plot::PlotAttribute attribute = openswmmvis::plot::PlotAttribute::Unknown;
        bool isNodeAttribute = true;
        QString title; ///< e.g. "Velocity (ft/s)" — resolved upstream
        QPen pen;      ///< base line style from the options object
        QVector<SourceProfile> sources;
    };

    /**
     * @brief Create an empty painter-based tracks pane with a [0,1] virtual X range.
     * @param[in] parent Optional QWidget owner. The pane does not own/fetch result layers.
     */
    explicit ProfileAttributeTracksWidget(QWidget *parent = nullptr);

    /*! Per-node virtual chainage — MUST be the profile widget's own table
     *  (ProfilePlotWidget::virtualChainageTable()) so both panes agree on
     *  where every node sits. Size = node count; links span [i, i+1].
     * @param[in] table Ordered finite per-node virtual X coordinates copied without validation; links use adjacent entries.
     * @note Requests repaint without changing the visible range, recalculating Y ranges or emitting synchronization signals.
     */
    void setVirtualChainage(const QVector<double> &table);

    /*! The profile widget's horizontal gutters, so columns align.
     * @param[in] leftPx Left logical-pixel gutter, clamped at zero minimum.
     * @param[in] rightPx Right logical-pixel gutter, clamped at zero minimum.
     * @note Requests repaint; effective plotting width is at least one pixel and no synchronization signal is emitted.
     */
    void setHorizontalMargins(int leftPx, int rightPx);

    /*! Maps virtual x → real chainage for the shared bottom tick labels.
     *  Wrap the profile widget's virtualToRealChainage. Null = label raw.
     * @param[in] fn Copied/moved callback invoked during axis painting, with virtual X as input and real chainage in the label's unit as output. Empty means identity.
     * @note Captured object lifetimes are the caller's responsibility. Replaces the callback and repaints, with no data resampling.
     */
    void setRealChainageMapper(std::function<double(double)> fn);

    /*! Replaces all tracks. Order is the display order (top to bottom).
     * @param[in] tracks Metadata copied in display order; source data is retained through shared immutable pointers. Envelope arrays should be parallel and finite where valid.
     * @note Recomputes full-path Y ranges from supplied min/max envelopes only, sets minimum height and repaints. Does not change the X range or animation index. Later external mutations of shared data are not observed automatically.
     */
    void setTracks(const QVector<Track> &tracks);
    /**
     * @return Number of stored tracks, regardless of whether any source has samples.
     */
    [[nodiscard]] int trackCount() const { return m_tracks.size(); }

    /*! Chrome + styling source; the widget connects to changed() and
     *  repaints. May be null (defaults used).
     * @param[in] options Optional borrowed QObject cache, guarded by QPointer. No ownership transfer/reparenting occurs.
     * @note Disconnects the old binding (even when identical), connects changed() to size/repaint updates, then refreshes immediately. Track pen values already copied through setTracks() are not replaced by this binding.
     */
    void setOptions(ProfileAttributeTrackOptions *options);

    /*! Shared x-label (e.g. "Distance (ft)") drawn under the last track.
     * @param[in] label Display text including units if desired, copied verbatim.
     * @note Repaints the shared bottom strip without changing geometry/data units.
     */
    void setXLabel(const QString &label);

    // ── Pixel mapping (public for the alignment unit test) ─────────────
    /**
     * @param[in] vx Virtual chainage coordinate, expected finite.
     * @return Affine logical-pixel X using current gutters/range; not clipped, so off-view values extrapolate.
     */
    [[nodiscard]] double pixelForVirtualX(double vx) const;
    /**
     * @param[in] px Logical-pixel X, expected finite.
     * @return Affine virtual chainage coordinate without clipping; gutter/outside pixels extrapolate.
     */
    [[nodiscard]] double virtualXForPixel(double px) const;

public slots:
    /*! Sets the visible x-range in virtual chainage. The profile widget's
     *  visibleXRangeChanged connects here (and this widget's own signal
     *  connects back to ProfilePlotWidget::setVisibleXRange — the host
     *  guards the loop).
     * @param[in] vxMin Finite lower virtual chainage.
     * @param[in] vxMax Finite upper value strictly larger than vxMin.
     * @note Invalid/identical ranges are ignored. Updates/repaints without visibleXRangeChanged() to avoid echo loops. No data-extent clamp occurs.
     */
    void setVisibleXRange(double vxMin, double vxMax);

    /*! Animation cursor — index into each profile's [period] arrays.
     *  -1 hides the current-time curves (envelopes stay).
     * @param[in] period Zero-based per-source sample index; any negative hides current curves, and an index beyond a particular row leaves a gap.
     * @note Changed index repaints only; no source fetch or cursor signal occurs. Envelopes remain visible according to options.
     */
    void setCurrentPeriod(int period);

signals:
    /*! Emitted when THIS pane pans/zooms its x-axis (wheel / drag), so the
     *  profile above can follow. Never emitted from setVisibleXRange().
     * @param[in] vxMin New lower virtual chainage after local drag/wheel interaction.
     * @param[in] vxMax New upper virtual chainage.
     * @note Host uses these values to synchronize the main profile; programmatic setVisibleXRange() does not emit. Interactive range is not constrained to data extent.
     */
    void visibleXRangeChanged(double vxMin, double vxMax);

protected:
    /**
     * @brief Draw theme background, all tracks and a bottom axis when nonempty; event contents are unused.
     * @param[in,out] event Borrowed Qt event valid only during dispatch; never retained or deleted.
     */
    void paintEvent(QPaintEvent *event) override;
    /**
     * @brief Zoom X about the cursor with factor 0.85 per angle wheel step, repaint and emit visibleXRangeChanged(); zero vertical delta is ignored.
     * @param[in,out] event Borrowed Qt event valid only during dispatch; never retained or deleted.
     */
    void wheelEvent(QWheelEvent *event) override;
    /**
     * @brief Begin X panning on left/middle button anywhere in the pane, accept and show a closed-hand cursor; other presses delegate to QWidget.
     * @param[in,out] event Borrowed Qt event valid only during dispatch; never retained or deleted.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @brief While panning, translate range by horizontal pixel displacement, repaint and emit visibleXRangeChanged(); otherwise delegate to QWidget.
     * @param[in,out] event Borrowed Qt event valid only during dispatch; never retained or deleted.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @brief End active left/middle panning, restore cursor and accept; other releases delegate to QWidget.
     * @param[in,out] event Borrowed Qt event valid only during dispatch; never retained or deleted.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /*! Configured track height (ProfileAttributeTrackOptions::trackHeightPx).
     *  This is the FLOOR and the basis of the minimum height — never the
     *  painted height. Keep updateSizeHint() on this and painting on
     *  trackHeight(), or minimumHeight feeds back into itself and ratchets.
     * @return Option-supplied minimum track height, or 110 logical pixels when no options exist.
     */
    [[nodiscard]] int preferredTrackHeight() const;
    /*! Painted track height: shares any surplus widget height across the
     *  tracks so they fill the pane as the splitter is dragged.
     * @return At least preferredTrackHeight(), sharing surplus pane height across stored tracks after gaps/bottom strip; empty returns the preferred height.
     */
    [[nodiscard]] int trackHeight() const;
    /**
     * @return Fixed bottom-axis reserve of 34 logical pixels, independent of the label/font.
     */
    [[nodiscard]] int bottomAxisHeight() const;
    /**
     * @param[in] trackIdx Zero-based track index, expected in range; not checked.
     * @return Body rectangle from gutters and track height, with at least one-pixel width. Coordinates are logical widget pixels.
     */
    [[nodiscard]] QRectF trackRect(int trackIdx) const;
    /**
     * @brief Set the pane's minimum height from track count and the configured height floor.
     * @note Empty tracks set zero minimum; nonempty reserve each preferred height/gap plus bottom strip. Does not feed current stretched height back into the minimum.
     */
    void updateSizeHint();
    /**
     * @brief Paint one track's frame, cached Y ticks, envelopes and current sample curves.
     * @param[in,out] p Active widget painter; state is saved/restored.
     * @param[in] trackIdx Valid stored track index with matching cached Y range.
     * @note Node curves/bands break at missing data; link values draw independent horizontal intervals. Only primary sources get envelopes, whose alpha replaces pen alpha. Multiple source records use valid source tint; no data units are converted.
     */
    void paintTrack(QPainter &p, int trackIdx) const;
    /**
     * @brief Paint mapped chainage ticks and the caller-supplied X label.
     * @param[in,out] p Active painter, saved/restored. Invokes the optional chainage callback synchronously; no source fetch occurs.
     */
    void paintBottomAxis(QPainter &p) const;

    /*! Per-track y-range from supplied finite min/max envelopes only.
     *  Cached per setTracks(); {0,1} when no finite envelope is available. */
    struct YRange {
        double lo = 0.0;
        double hi = 1.0;
    };
    /**
     * @param[in] t Track with parallel minByPath/maxByPath arrays across all supplied sources.
     * @return Finite envelope extrema with 5% padding; constant extrema first expand upward by one, and absent/nonfinite extrema return [0,1]. Does not scan byPath sample rows when envelopes are absent.
     */
    [[nodiscard]] YRange yRangeFor(const Track &t) const;

    QVector<Track> m_tracks;
    QVector<YRange> m_yRanges; ///< parallel to m_tracks
    QVector<double> m_vx;      ///< per-node virtual chainage
    std::function<double(double)> m_toRealChainage;
    QPointer<ProfileAttributeTrackOptions> m_options;
    QString m_xLabel;

    int m_marginLeft = 64; ///< overwritten by setHorizontalMargins
    int m_marginRight = 16;
    double m_vxMin = 0.0;
    double m_vxMax = 1.0;
    int m_currentPeriod = -1;

    bool m_panActive = false;
    QPoint m_lastMousePos;
};

#endif // OPENSWMMVIS_PLOT_PROFILEATTRIBUTETRACKSWIDGET_H
