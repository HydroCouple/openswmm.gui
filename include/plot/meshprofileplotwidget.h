/*!
 * \file   meshprofileplotwidget.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Longitudinal cross-section renderer for a path traced across the 2D
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
 */
#ifndef MESH_PROFILE_PLOT_WIDGET_H
#define MESH_PROFILE_PLOT_WIDGET_H

#include "plot/meshprofilesampler.h"

#include <QDateTime>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class MeshProfilePlotOptions;
class QRubberBand;

class MeshProfilePlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MeshProfilePlotWidget(QWidget *parent = nullptr);

    /*! \brief Sets the full cross-section (recomputes bounds + repaints). */
    void setProfile(const MeshProfileSampler::MeshProfile &profile);

    /*! \brief Cheap per-frame update of the animated depth column. Size must
     *  match the current sample count; mismatched sizes are ignored. */
    void setCurrentDepths(const QVector<double> &depthNow);

    /*! \brief Binds the styling object (connects changed() → repaint). */
    void setOptions(MeshProfilePlotOptions *options);
    [[nodiscard]] MeshProfilePlotOptions *options() const { return m_options; }

    /*! \brief Wall-clock time shown by the timestamp overlay. */
    void setCurrentDateTime(const QDateTime &dt);

    void setAxisLabels(const QString &xLabel, const QString &yLabel);

    // ── Zoom / pan (driven by the dialog toolbar) ───────────────────────
    enum class Mode { Identify = 0, Pan, ZoomIn, ZoomOut };
    void setMode(Mode m);
    [[nodiscard]] Mode mode() const { return m_mode; }
    void zoomBy(double factor);
    void fitToExtent();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    [[nodiscard]] QRectF  plotRect() const;
    [[nodiscard]] QPointF dataToPixel(double chainage, double elev) const;
    void recomputeBounds();

    void paintBackgroundAndAxes(QPainter &p) const;
    void paintSoilFill(QPainter &p) const;
    void paintMaxEnvelope(QPainter &p) const;
    void paintDepthFill(QPainter &p) const;
    void paintWseLine(QPainter &p) const;
    void paintGroundLine(QPainter &p) const;
    void paintLegend(QPainter &p) const;
    void paintTimeLabel(QPainter &p) const;

    MeshProfileSampler::MeshProfile      m_profile;
    QPointer<MeshProfilePlotOptions>     m_options;
    QDateTime                            m_currentDateTime;
    QString                              m_xLabel = QStringLiteral("Distance");
    QString                              m_yLabel = QStringLiteral("Elevation");

    // Auto extent (data bounds) + current view bounds.
    double m_autoXMin = 0.0, m_autoXMax = 1.0, m_autoYMin = 0.0, m_autoYMax = 1.0;
    double m_dataXMin = 0.0, m_dataXMax = 1.0, m_dataYMin = 0.0, m_dataYMax = 1.0;
    bool   m_fitMode  = true;

    Mode   m_mode        = Mode::Identify;
    bool   m_panActive   = false;
    bool   m_zoomActive  = false;
    QPoint m_lastMousePos;
    QPoint m_zoomAnchor;
    QRubberBand *m_rubberBand = nullptr;

    // Last-painted overlay rects (drag hit-test).
    mutable QRectF m_legendRect;
    mutable QRectF m_timeLabelRect;
    enum class OverlayDrag { None = 0, Legend, TimeLabel };
    OverlayDrag m_overlayDrag = OverlayDrag::None;
    QPoint      m_overlayDragLastPos;
};

#endif // MESH_PROFILE_PLOT_WIDGET_H
