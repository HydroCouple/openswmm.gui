/*!
 * \file   seriesstyleobject.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QObject wrapper around `SeriesStyle` exposing every visual
 *         attribute as a `Q_PROPERTY` so a `QPropertyModel` can drive
 *         per-series editing in a tree view.
 *
 * Mirrors the design of `ChartProperties` for chart-level editing.
 * Every setter both updates the underlying `SeriesStyle` cache and
 * emits a typed Notify signal (for QPropertyModel to repaint the row)
 * and the aggregate `styleChanged(SeriesStyle)` signal so host dialogs
 * can live-update their chart in one place.
 *
 * Property groupings (rendered via `displayLabelFor`):
 *   - Identity:   color, opacity, legendName, legendOverride
 *   - Line:       showLine, lineWidth, dash, capStyle, joinStyle
 *   - Markers:    showMarkers, shape, markerSize,
 *                 markerFillColor, markerBorderColor, markerBorderWidth
 *   - Labels:     showPointLabels, pointLabelFont, pointLabelColor,
 *                 pointLabelPrecision
 *   - Area:       showAreaFill, areaFillColor
 */
#ifndef OPENSWMMVIS_PLOT_SERIESSTYLEOBJECT_H
#define OPENSWMMVIS_PLOT_SERIESSTYLEOBJECT_H

#include "plot/seriesstyle.h"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>

namespace openswmmvis::plot {

class SeriesStyleObject : public QObject
{
    Q_OBJECT

    // ---- Identity ----------------------------------------------------------
    Q_PROPERTY(QColor   color        READ color        WRITE setColor        NOTIFY colorChanged)
    Q_PROPERTY(qreal    opacity      READ opacity      WRITE setOpacity      NOTIFY opacityChanged)
    Q_PROPERTY(QString  legendName     READ legendName     WRITE setLegendName     NOTIFY legendNameChanged)
    Q_PROPERTY(QString  legendOverride READ legendOverride WRITE setLegendOverride NOTIFY legendOverrideChanged)

    // ---- Line --------------------------------------------------------------
    Q_PROPERTY(bool             showLine  READ showLine  WRITE setShowLine  NOTIFY showLineChanged)
    Q_PROPERTY(qreal            lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(Qt::PenStyle     dash      READ dash      WRITE setDash      NOTIFY dashChanged)
    Q_PROPERTY(Qt::PenCapStyle  capStyle  READ capStyle  WRITE setCapStyle  NOTIFY capStyleChanged)
    Q_PROPERTY(Qt::PenJoinStyle joinStyle READ joinStyle WRITE setJoinStyle NOTIFY joinStyleChanged)

    // ---- Markers -----------------------------------------------------------
    Q_PROPERTY(bool                              showMarkers       READ showMarkers       WRITE setShowMarkers       NOTIFY showMarkersChanged)
    Q_PROPERTY(MarkerShapeQ                      shape             READ shape             WRITE setShape             NOTIFY shapeChanged)
    Q_PROPERTY(qreal                             markerSize        READ markerSize        WRITE setMarkerSize        NOTIFY markerSizeChanged)
    Q_PROPERTY(QColor                            markerFillColor   READ markerFillColor   WRITE setMarkerFillColor   NOTIFY markerFillColorChanged)
    Q_PROPERTY(QColor                            markerBorderColor READ markerBorderColor WRITE setMarkerBorderColor NOTIFY markerBorderColorChanged)
    Q_PROPERTY(qreal                             markerBorderWidth READ markerBorderWidth WRITE setMarkerBorderWidth NOTIFY markerBorderWidthChanged)

    // ---- Point labels ------------------------------------------------------
    Q_PROPERTY(bool   showPointLabels     READ showPointLabels     WRITE setShowPointLabels     NOTIFY showPointLabelsChanged)
    Q_PROPERTY(QFont  pointLabelFont      READ pointLabelFont      WRITE setPointLabelFont      NOTIFY pointLabelFontChanged)
    Q_PROPERTY(QColor pointLabelColor     READ pointLabelColor     WRITE setPointLabelColor     NOTIFY pointLabelColorChanged)
    Q_PROPERTY(LabelFormatModeQ pointLabelFormatMode READ pointLabelFormatMode WRITE setPointLabelFormatMode NOTIFY pointLabelFormatModeChanged)
    Q_PROPERTY(int    pointLabelPrecision READ pointLabelPrecision WRITE setPointLabelPrecision NOTIFY pointLabelPrecisionChanged)
    Q_PROPERTY(QString pointLabelFormat   READ pointLabelFormat    WRITE setPointLabelFormat    NOTIFY pointLabelFormatChanged)

    // ---- Area fill ---------------------------------------------------------
    Q_PROPERTY(bool   showAreaFill   READ showAreaFill   WRITE setShowAreaFill   NOTIFY showAreaFillChanged)
    Q_PROPERTY(QColor areaFillColor  READ areaFillColor  WRITE setAreaFillColor  NOTIFY areaFillColorChanged)

public:
    /*! \brief A Q_ENUM-registered mirror of `MarkerShape` so QPropertyModel
     *  can render a combo-box for it. Q_ENUM requires the enum to live in
     *  a QObject, hence this duplicate. Values are kept in sync 1:1. */
    enum class MarkerShapeQ : int {
        Circle   = static_cast<int>(MarkerShape::Circle),
        Square   = static_cast<int>(MarkerShape::Square),
        Triangle = static_cast<int>(MarkerShape::Triangle),
        Diamond  = static_cast<int>(MarkerShape::Diamond),
        Cross    = static_cast<int>(MarkerShape::Cross),
        Plus     = static_cast<int>(MarkerShape::Plus),
    };
    Q_ENUM(MarkerShapeQ)

    /*! \brief Q_ENUM mirror of `NumberFormatMode` so QPropertyModel can
     *  render a combo-box for the point-label number format. Values track
     *  `NumberFormatMode` 1:1. */
    enum class LabelFormatModeQ : int {
        Decimals           = static_cast<int>(NumberFormatMode::Decimals),
        SignificantFigures = static_cast<int>(NumberFormatMode::SignificantFigures),
    };
    Q_ENUM(LabelFormatModeQ)

    explicit SeriesStyleObject(QObject *parent = nullptr);
    explicit SeriesStyleObject(const SeriesStyle& initial, QObject *parent = nullptr);
    ~SeriesStyleObject() override = default;

    /*! \brief Snapshot of the current style. */
    SeriesStyle style() const noexcept { return m_style; }

    /*! \brief Replace every field at once. Emits per-field NOTIFYs that
     *  actually differ from the previous values, then the aggregate
     *  `styleChanged()` signal once. */
    void setStyle(const SeriesStyle& s);

    /*! \brief Spec-level legend override (mirrors `SeriesSpec::legendOverride`).
     *
     *  Distinct from the style-level `legendName` above:
     *   - `legendName`     lives in `SeriesStyle`  (per-style, JSON-persisted).
     *   - `legendOverride` lives in `SeriesSpec`   (per-series instance, used
     *                                              by the Comparison Plot
     *                                              dialog as the canonical
     *                                              "what to call this series").
     *
     *  Stored on this object so the QPropertyModel-backed editor can edit it
     *  alongside the style fields; not part of `m_style`, so it never enters
     *  `style()` and never fires `styleChanged()`. Hosts read it via
     *  `legendOverride()` and subscribe to `legendOverrideChanged()`. */
    QString legendOverride() const { return m_legendOverride; }

    /*! \brief Human-friendly, group-prefixed label for the QPropertyModel
     *  tree. Returns labels like "Line — Width", "Marker — Fill Colour".
     *  Returns an empty string for unknown property names so the default
     *  generated name is used. Mirrors `ChartProperties::displayLabelFor`. */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    // ---- Getters -----------------------------------------------------------
    QColor  color()             const noexcept { return m_style.color; }
    qreal   opacity()           const noexcept { return m_style.opacity; }
    QString legendName()        const          { return m_style.legendName; }

    bool             showLine()  const noexcept { return m_style.showLine; }
    qreal            lineWidth() const noexcept { return m_style.lineWidth; }
    Qt::PenStyle     dash()      const noexcept { return m_style.dash; }
    Qt::PenCapStyle  capStyle()  const noexcept { return m_style.capStyle; }
    Qt::PenJoinStyle joinStyle() const noexcept { return m_style.joinStyle; }

    bool          showMarkers()       const noexcept { return m_style.showMarkers; }
    MarkerShapeQ  shape()             const noexcept { return static_cast<MarkerShapeQ>(m_style.shape); }
    qreal         markerSize()        const noexcept { return m_style.markerSize; }
    QColor        markerFillColor()   const noexcept { return m_style.markerFillColor; }
    QColor        markerBorderColor() const noexcept { return m_style.markerBorderColor; }
    qreal         markerBorderWidth() const noexcept { return m_style.markerBorderWidth; }

    bool   showPointLabels()     const noexcept { return m_style.showPointLabels; }
    QFont  pointLabelFont()      const noexcept { return m_style.pointLabelFont; }
    QColor pointLabelColor()     const noexcept { return m_style.pointLabelColor; }
    LabelFormatModeQ pointLabelFormatMode() const noexcept
    { return static_cast<LabelFormatModeQ>(m_style.pointLabelFormatMode); }
    int    pointLabelPrecision() const noexcept { return m_style.pointLabelPrecision; }
    QString pointLabelFormat()   const          { return m_style.pointLabelFormat; }

    bool   showAreaFill()  const noexcept { return m_style.showAreaFill; }
    QColor areaFillColor() const noexcept { return m_style.areaFillColor; }

public slots:
    void setColor(const QColor& c);
    void setOpacity(qreal v);
    void setLegendName(const QString& s);
    void setLegendOverride(const QString& s);

    void setShowLine(bool on);
    void setLineWidth(qreal v);
    void setDash(Qt::PenStyle s);
    void setCapStyle(Qt::PenCapStyle s);
    void setJoinStyle(Qt::PenJoinStyle s);

    void setShowMarkers(bool on);
    void setShape(MarkerShapeQ s);
    void setMarkerSize(qreal v);
    void setMarkerFillColor(const QColor& c);
    void setMarkerBorderColor(const QColor& c);
    void setMarkerBorderWidth(qreal v);

    void setShowPointLabels(bool on);
    void setPointLabelFont(const QFont& f);
    void setPointLabelColor(const QColor& c);
    void setPointLabelFormatMode(LabelFormatModeQ m);
    void setPointLabelPrecision(int n);
    void setPointLabelFormat(const QString& spec);

    void setShowAreaFill(bool on);
    void setAreaFillColor(const QColor& c);

signals:
    void colorChanged(const QColor&);
    void opacityChanged(qreal);
    void legendNameChanged(const QString&);
    void legendOverrideChanged(const QString&);

    void showLineChanged(bool);
    void lineWidthChanged(qreal);
    void dashChanged(Qt::PenStyle);
    void capStyleChanged(Qt::PenCapStyle);
    void joinStyleChanged(Qt::PenJoinStyle);

    void showMarkersChanged(bool);
    void shapeChanged(MarkerShapeQ);
    void markerSizeChanged(qreal);
    void markerFillColorChanged(const QColor&);
    void markerBorderColorChanged(const QColor&);
    void markerBorderWidthChanged(qreal);

    void showPointLabelsChanged(bool);
    void pointLabelFontChanged(const QFont&);
    void pointLabelColorChanged(const QColor&);
    void pointLabelFormatModeChanged(LabelFormatModeQ);
    void pointLabelPrecisionChanged(int);
    void pointLabelFormatChanged(const QString&);

    void showAreaFillChanged(bool);
    void areaFillColorChanged(const QColor&);

    /*! \brief Aggregate change signal — fires after every per-field setter.
     *  Hosts subscribe to this once to live-update the chart. */
    void styleChanged(const openswmmvis::plot::SeriesStyle& s);

private:
    void emitAggregate_();

    SeriesStyle m_style;
    QString     m_legendOverride;   ///< Mirror of SeriesSpec::legendOverride.
};

} // namespace openswmmvis::plot

Q_DECLARE_METATYPE(openswmmvis::plot::SeriesStyleObject::MarkerShapeQ)
Q_DECLARE_METATYPE(openswmmvis::plot::SeriesStyleObject::LabelFormatModeQ)

#endif // OPENSWMMVIS_PLOT_SERIESSTYLEOBJECT_H
