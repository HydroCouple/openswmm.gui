/*!
 * \file   stylepreviewswatch.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Small live-preview widget used by every style editor.
 *
 *         Renders a representative "point" / "line" / "polygon" sample
 *         using the current colour, size/width, and shape parameters
 *         provided via setter calls. Editors push state into the swatch
 *         on every property change and the swatch repaints.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_STYLEPREVIEWSWATCH_H
#define OPENSWMMVIS_UI_WIDGETS_STYLEPREVIEWSWATCH_H

#include <QColor>
#include <QPen>
#include <QWidget>
#include <Qt>

namespace openswmmvis::ui {

class StylePreviewSwatch : public QWidget
{
    Q_OBJECT
public:
    enum Kind { PointKind, LineKind, PolygonKind };

    explicit StylePreviewSwatch(QWidget *parent = nullptr);

    void setKind(Kind k);

    /*! Fill / single-symbol colour. For lines this is the stroke colour. */
    void setColor(const QColor &c);

    /*! Stroke / outline pen (points use it as marker outline). */
    void setStrokePen(const QPen &pen);

    /*! For PointKind: marker diameter in pixels + shape (matches the
     *  PointFeatureSublayerStyle::MarkerShape integer values). */
    void setMarkerSizePx(double px);
    void setMarkerShape(int shapeEnum);

    /*! For LineKind: line width in pixels. */
    void setLineWidthPx(double px);

    /*! For LineKind: show an arrow glyph at the midpoint of the
     *  preview line (mirrors LineFeatureSublayerStyle::showFlowArrows). */
    void setShowArrows(bool v);

    /*! For PolygonKind: fill alpha multiplier (0..1) used on top of the
     *  single-symbol colour. */
    void setFillOpacity(double v);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override { return QSize(180, 56); }
    QSize minimumSizeHint() const override { return QSize(120, 40); }

private:
    Kind   m_kind         = PointKind;
    QColor m_color        = QColor(80, 130, 200);
    QPen   m_strokePen    = QPen(QColor(40, 40, 40), 1.0);
    double m_markerSizePx = 12.0;
    int    m_markerShape  = 0;
    double m_lineWidthPx  = 2.0;
    bool   m_showArrows   = false;
    double m_fillOpacity  = 0.55;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_STYLEPREVIEWSWATCH_H
