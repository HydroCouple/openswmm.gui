/**
 * @file      stylepreviewswatch.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Small live-preview widget used by every style editor.
 *
 *         Renders a representative "point" / "line" / "polygon" sample
 *         using the current colour, size/width, and shape parameters
 *         provided via setter calls. Editors push state into the swatch
 *         on every property change and the swatch repaints.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_STYLEPREVIEWSWATCH_H
#define OPENSWMMVIS_UI_WIDGETS_STYLEPREVIEWSWATCH_H

#include <QColor>
#include <QPen>
#include <QWidget>
#include <Qt>

namespace openswmmvis::ui
{

class StylePreviewSwatch : public QWidget
{
    Q_OBJECT
public:
    enum Kind { PointKind, LineKind, PolygonKind };

    /**
     * @brief Create a passive point preview with default blue fill and dark outline.
     * @param[in] parent Optional QWidget owner; all setters only store preview state and schedule repaint.
     */
    explicit StylePreviewSwatch(QWidget *parent = nullptr);

    /**
     * @brief Choose the representative point/line/polygon preview.
     * @param[in] k Supported sample kind; unknown enum values paint only the frame.
     */
    void setKind(Kind k);

    /*! Fill / single-symbol colour. For lines this is the stroke colour.
     * @param[in] c Sample colour copied without validity checks; polygon alpha is replaced by fill opacity when painting.
     */
    void setColor(const QColor &c);

    /*! Stroke / outline pen (points use it as marker outline).
     * @param[in] pen Point/polygon outline pen; line preview uses its dash style with separately configured width/colour.
     */
    void setStrokePen(const QPen &pen);

    /*! For PointKind: marker diameter in pixels + shape (matches the
     *  PointFeatureSublayerStyle::MarkerShape integer values).
     * @param[in] px Requested finite logical-pixel marker edge; stored as supplied and rendered with a four-pixel minimum.
     */
    void setMarkerSizePx(double px);
    /**
     * @brief Select a point preview glyph using the canonical marker drawing path.
     * @param[in] shapeEnum Canonical MarkerShape integer, passed through without a separate range check.
     */
    void setMarkerShape(int shapeEnum);

    /*! For LineKind: line width in pixels.
     * @param[in] px Finite logical-pixel line width; rendered with a 0.5-pixel minimum, while arrow scale uses the requested value.
     */
    void setLineWidthPx(double px);

    /*! For LineKind: show an arrow glyph at the midpoint of the
     *  preview line (mirrors LineFeatureSublayerStyle::showFlowArrows).
     * @param[in] v True paints a midpoint arrow for LineKind only.
     */
    void setShowArrows(bool v);

    /*! For PolygonKind: fill alpha (0..1), replacing the single-symbol colour
     *  alpha during preview rendering.
     * @param[in] v Finite alpha value; stored unchanged then clamped to [0,1] when painting polygons.
     */
    void setFillOpacity(double v);

protected:
    /**
     * @brief Paint the framed representative sample from current preview values.
     * @param[in] event Qt paint event, unused; no model state or signals are changed.
     */
    void paintEvent(QPaintEvent *event) override;
    /**
     * @brief Supply the preferred preview extent.
     * @return 180 by 56 logical pixels.
     */
    QSize sizeHint() const override { return QSize(180, 56); }
    /**
     * @brief Supply the minimum layout hint.
     * @return 120 by 40 logical pixels.
     */
    QSize minimumSizeHint() const override { return QSize(120, 40); }

private:
    Kind m_kind = PointKind;
    QColor m_color = QColor(80, 130, 200);
    QPen m_strokePen = QPen(QColor(40, 40, 40), 1.0);
    double m_markerSizePx = 12.0;
    int m_markerShape = 0;
    double m_lineWidthPx = 2.0;
    bool m_showArrows = false;
    double m_fillOpacity = 0.55;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_STYLEPREVIEWSWATCH_H
