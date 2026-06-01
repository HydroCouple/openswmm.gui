/*!
 * \file   gisvectorsymboladapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QObject wrapper exposing GISVectorSymbol fields as Q_PROPERTYs
 *         for the unified LayerStyleDialog (Slice U-7).
 *
 *         Holds a live copy of the struct + a writer that pushes edits
 *         back through GISVectorLayer::setSymbol so the canvas repaints
 *         on every property change. Same pattern as
 *         SwmmElementSymbolAdapter (Slice U-4).
 */
#ifndef OPENSWMMVIS_LAYERS_GISVECTORSYMBOLADAPTER_H
#define OPENSWMMVIS_LAYERS_GISVECTORSYMBOLADAPTER_H

#include "layers/gisvectorlayer.h"

#include <QColor>
#include <QFont>
#include <QObject>

#include <functional>

class GisVectorSymbolAdapter : public QObject
{
    Q_OBJECT
    // Point / marker
    Q_PROPERTY(MarkerShape markerShape    READ markerShape    WRITE setMarkerShape    NOTIFY symbolChanged)
    Q_PROPERTY(double      markerSize     READ markerSize     WRITE setMarkerSize     NOTIFY symbolChanged)
    Q_PROPERTY(QColor      markerFill     READ markerFill     WRITE setMarkerFill     NOTIFY symbolChanged)
    Q_PROPERTY(QColor      markerOutline  READ markerOutline  WRITE setMarkerOutline  NOTIFY symbolChanged)
    Q_PROPERTY(double      markerOutlineW READ markerOutlineW WRITE setMarkerOutlineW NOTIFY symbolChanged)

    // Line
    Q_PROPERTY(QColor      lineColor      READ lineColor      WRITE setLineColor      NOTIFY symbolChanged)
    Q_PROPERTY(double      lineWidth      READ lineWidth      WRITE setLineWidth      NOTIFY symbolChanged)
    Q_PROPERTY(Qt::PenStyle lineDash      READ lineDash       WRITE setLineDash       NOTIFY symbolChanged)

    // Polygon
    Q_PROPERTY(QColor      polygonFill    READ polygonFill    WRITE setPolygonFill    NOTIFY symbolChanged)
    Q_PROPERTY(QColor      polygonOutlineColor READ polygonOutlineColor WRITE setPolygonOutlineColor NOTIFY symbolChanged)
    Q_PROPERTY(double      polygonOutlineWidth READ polygonOutlineWidth WRITE setPolygonOutlineWidth NOTIFY symbolChanged)

    // Labels
    Q_PROPERTY(bool        showLabels     READ showLabels     WRITE setShowLabels     NOTIFY symbolChanged)
    Q_PROPERTY(QString     labelField     READ labelField     WRITE setLabelField     NOTIFY symbolChanged)
    Q_PROPERTY(QFont       labelFont      READ labelFont      WRITE setLabelFont      NOTIFY symbolChanged)
    Q_PROPERTY(QColor      labelColor     READ labelColor     WRITE setLabelColor     NOTIFY symbolChanged)

    Q_CLASSINFO("group:markerShape",          "Marker (points)")
    Q_CLASSINFO("group:markerSize",           "Marker (points)")
    Q_CLASSINFO("group:markerFill",           "Marker (points)")
    Q_CLASSINFO("group:markerOutline",        "Marker (points)")
    Q_CLASSINFO("group:markerOutlineW",       "Marker (points)")
    Q_CLASSINFO("group:lineColor",            "Line")
    Q_CLASSINFO("group:lineWidth",            "Line")
    Q_CLASSINFO("group:lineDash",             "Line")
    Q_CLASSINFO("group:polygonFill",          "Polygon")
    Q_CLASSINFO("group:polygonOutlineColor",  "Polygon")
    Q_CLASSINFO("group:polygonOutlineWidth",  "Polygon")
    Q_CLASSINFO("group:showLabels",           "Labels")
    Q_CLASSINFO("group:labelField",           "Labels")
    Q_CLASSINFO("group:labelFont",            "Labels")
    Q_CLASSINFO("group:labelColor",           "Labels")

public:
    /*! Re-export the marker shape enum so QPropertyModel can offer a combo. */
    enum MarkerShape {
        Circle   = GISVectorSymbol::Circle,
        Square   = GISVectorSymbol::Square,
        Triangle = GISVectorSymbol::Triangle,
        Diamond  = GISVectorSymbol::Diamond,
        Star     = GISVectorSymbol::Star,
        Cross    = GISVectorSymbol::Cross,
    };
    Q_ENUM(MarkerShape)

    using Writer = std::function<void(const GISVectorSymbol &)>;

    explicit GisVectorSymbolAdapter(GISVectorSymbol initial,
                                     Writer writer,
                                     QObject *parent = nullptr)
        : QObject(parent), m_sym(std::move(initial)), m_writer(std::move(writer))
    {}

    // Marker
    [[nodiscard]] MarkerShape markerShape() const { return static_cast<MarkerShape>(m_sym.markerShape); }
    [[nodiscard]] double markerSize()       const { return m_sym.markerSize; }
    [[nodiscard]] QColor markerFill()       const { return m_sym.markerFill; }
    [[nodiscard]] QColor markerOutline()    const { return m_sym.markerOutline; }
    [[nodiscard]] double markerOutlineW()   const { return m_sym.markerOutlineW; }

    void setMarkerShape(MarkerShape v)      { if (markerShape() == v) return; m_sym.markerShape = static_cast<GISVectorSymbol::MarkerShape>(v); commit(); }
    void setMarkerSize(double v)            { if (qFuzzyCompare(m_sym.markerSize, v)) return; m_sym.markerSize = v; commit(); }
    void setMarkerFill(const QColor &v)     { if (m_sym.markerFill == v) return; m_sym.markerFill = v; commit(); }
    void setMarkerOutline(const QColor &v)  { if (m_sym.markerOutline == v) return; m_sym.markerOutline = v; commit(); }
    void setMarkerOutlineW(double v)        { if (qFuzzyCompare(m_sym.markerOutlineW, v)) return; m_sym.markerOutlineW = v; commit(); }

    // Line — decompose QPen into colour/width/dash for the property editor.
    [[nodiscard]] QColor       lineColor() const { return m_sym.linePen.color(); }
    [[nodiscard]] double       lineWidth() const { return m_sym.linePen.widthF(); }
    [[nodiscard]] Qt::PenStyle lineDash()  const { return m_sym.linePen.style(); }
    void setLineColor(const QColor &v)     { QPen p = m_sym.linePen; if (p.color() == v) return; p.setColor(v); m_sym.linePen = p; commit(); }
    void setLineWidth(double v)            { QPen p = m_sym.linePen; if (qFuzzyCompare(p.widthF(), v)) return; p.setWidthF(v); m_sym.linePen = p; commit(); }
    void setLineDash(Qt::PenStyle v)       { QPen p = m_sym.linePen; if (p.style() == v) return; p.setStyle(v); m_sym.linePen = p; commit(); }

    // Polygon — same QBrush/QPen decomposition.
    [[nodiscard]] QColor polygonFill()          const { return m_sym.polygonFill.color(); }
    [[nodiscard]] QColor polygonOutlineColor()  const { return m_sym.polygonOutline.color(); }
    [[nodiscard]] double polygonOutlineWidth()  const { return m_sym.polygonOutline.widthF(); }
    void setPolygonFill(const QColor &v)        { QBrush b = m_sym.polygonFill; if (b.color() == v) return; b.setColor(v); m_sym.polygonFill = b; commit(); }
    void setPolygonOutlineColor(const QColor &v){ QPen p = m_sym.polygonOutline; if (p.color() == v) return; p.setColor(v); m_sym.polygonOutline = p; commit(); }
    void setPolygonOutlineWidth(double v)       { QPen p = m_sym.polygonOutline; if (qFuzzyCompare(p.widthF(), v)) return; p.setWidthF(v); m_sym.polygonOutline = p; commit(); }

    // Labels
    [[nodiscard]] bool    showLabels() const { return m_sym.showLabels; }
    [[nodiscard]] QString labelField() const { return m_sym.labelField; }
    [[nodiscard]] QFont   labelFont()  const { return m_sym.labelFont; }
    [[nodiscard]] QColor  labelColor() const { return m_sym.labelColor; }
    void setShowLabels(bool v)            { if (m_sym.showLabels == v) return; m_sym.showLabels = v; commit(); }
    void setLabelField(const QString &v)  { if (m_sym.labelField == v) return; m_sym.labelField = v; commit(); }
    void setLabelFont(const QFont &v)     { if (m_sym.labelFont == v) return; m_sym.labelFont = v; commit(); }
    void setLabelColor(const QColor &v)   { if (m_sym.labelColor == v) return; m_sym.labelColor = v; commit(); }

signals:
    void symbolChanged();

private:
    void commit()
    {
        if (m_writer) m_writer(m_sym);
        emit symbolChanged();
    }
    GISVectorSymbol m_sym;
    Writer          m_writer;
};

#endif // OPENSWMMVIS_LAYERS_GISVECTORSYMBOLADAPTER_H
