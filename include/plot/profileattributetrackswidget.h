/*!
 * \file   profileattributetrackswidget.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Attribute-tracks pane: N stacked mini-charts ("tracks"), one per
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
    struct SourceProfile
    {
        QString label;
        QColor  color;
        bool    primary = false;   ///< envelope drawn for the primary only
        std::shared_ptr<const ProfileAttributeSampler::AttributeProfile> data;
    };

    /*! One track, fully resolved by the host dialog. */
    struct Track
    {
        openswmmvis::plot::PlotAttribute attribute =
            openswmmvis::plot::PlotAttribute::Unknown;
        bool    isNodeAttribute = true;
        QString title;              ///< e.g. "Velocity (ft/s)" — resolved upstream
        QPen    pen;                ///< base line style from the options object
        QVector<SourceProfile> sources;
    };

    explicit ProfileAttributeTracksWidget(QWidget *parent = nullptr);

    /*! Per-node virtual chainage — MUST be the profile widget's own table
     *  (ProfilePlotWidget::virtualChainageTable()) so both panes agree on
     *  where every node sits. Size = node count; links span [i, i+1]. */
    void setVirtualChainage(const QVector<double> &table);

    /*! The profile widget's horizontal gutters, so columns align. */
    void setHorizontalMargins(int leftPx, int rightPx);

    /*! Maps virtual x → real chainage for the shared bottom tick labels.
     *  Wrap the profile widget's virtualToRealChainage. Null = label raw. */
    void setRealChainageMapper(std::function<double(double)> fn);

    /*! Replaces all tracks. Order is the display order (top to bottom). */
    void setTracks(const QVector<Track> &tracks);
    [[nodiscard]] int trackCount() const { return m_tracks.size(); }

    /*! Chrome + styling source; the widget connects to changed() and
     *  repaints. May be null (defaults used). */
    void setOptions(ProfileAttributeTrackOptions *options);

    /*! Shared x-label (e.g. "Distance (ft)") drawn under the last track. */
    void setXLabel(const QString &label);

    // ── Pixel mapping (public for the alignment unit test) ─────────────
    [[nodiscard]] double pixelForVirtualX(double vx) const;
    [[nodiscard]] double virtualXForPixel(double px) const;

public slots:
    /*! Sets the visible x-range in virtual chainage. The profile widget's
     *  visibleXRangeChanged connects here (and this widget's own signal
     *  connects back to ProfilePlotWidget::setVisibleXRange — the host
     *  guards the loop). */
    void setVisibleXRange(double vxMin, double vxMax);

    /*! Animation cursor — index into each profile's [period] arrays.
     *  -1 hides the current-time curves (envelopes stay). */
    void setCurrentPeriod(int period);

signals:
    /*! Emitted when THIS pane pans/zooms its x-axis (wheel / drag), so the
     *  profile above can follow. Never emitted from setVisibleXRange(). */
    void visibleXRangeChanged(double vxMin, double vxMax);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /*! Configured track height (ProfileAttributeTrackOptions::trackHeightPx).
     *  This is the FLOOR and the basis of the minimum height — never the
     *  painted height. Keep updateSizeHint() on this and painting on
     *  trackHeight(), or minimumHeight feeds back into itself and ratchets. */
    [[nodiscard]] int    preferredTrackHeight() const;
    /*! Painted track height: shares any surplus widget height across the
     *  tracks so they fill the pane as the splitter is dragged. */
    [[nodiscard]] int    trackHeight() const;
    [[nodiscard]] int    bottomAxisHeight() const;
    [[nodiscard]] QRectF trackRect(int trackIdx) const;
    void updateSizeHint();
    void paintTrack(QPainter &p, int trackIdx) const;
    void paintBottomAxis(QPainter &p) const;

    /*! Per-track y-range over the full path (envelopes when present, else
     *  scanning the rows). Cached per setTracks(); {0,1} when empty. */
    struct YRange { double lo = 0.0; double hi = 1.0; };
    [[nodiscard]] YRange yRangeFor(const Track &t) const;

    QVector<Track>               m_tracks;
    QVector<YRange>              m_yRanges;       ///< parallel to m_tracks
    QVector<double>              m_vx;            ///< per-node virtual chainage
    std::function<double(double)> m_toRealChainage;
    QPointer<ProfileAttributeTrackOptions> m_options;
    QString                      m_xLabel;

    int    m_marginLeft  = 64;    ///< overwritten by setHorizontalMargins
    int    m_marginRight = 16;
    double m_vxMin = 0.0;
    double m_vxMax = 1.0;
    int    m_currentPeriod = -1;

    bool   m_panActive = false;
    QPoint m_lastMousePos;
};

#endif // OPENSWMMVIS_PLOT_PROFILEATTRIBUTETRACKSWIDGET_H
