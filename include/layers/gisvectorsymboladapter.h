/**
 * @file      gisvectorsymboladapter.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QObject wrapper exposing GISVectorSymbol fields as Q_PROPERTYs
 *         for the unified LayerStyleDialog (Slice U-7).
 *
 *         Holds a live copy of the struct + a writer that pushes edits
 *         back through GISVectorLayer::setSymbol so the canvas repaints
 *         on every property change. Same pattern as
 *         SwmmElementSymbolAdapter (Slice U-4).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    Q_PROPERTY(MarkerShape markerShape READ markerShape WRITE setMarkerShape NOTIFY symbolChanged)
    Q_PROPERTY(double markerSize READ markerSize WRITE setMarkerSize NOTIFY symbolChanged)
    Q_PROPERTY(QColor markerFill READ markerFill WRITE setMarkerFill NOTIFY symbolChanged)
    Q_PROPERTY(QColor markerOutline READ markerOutline WRITE setMarkerOutline NOTIFY symbolChanged)
    Q_PROPERTY(
        double markerOutlineW READ markerOutlineW WRITE setMarkerOutlineW NOTIFY symbolChanged)

    // Line
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY symbolChanged)
    Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth NOTIFY symbolChanged)
    Q_PROPERTY(Qt::PenStyle lineDash READ lineDash WRITE setLineDash NOTIFY symbolChanged)

    // Polygon
    Q_PROPERTY(QColor polygonFill READ polygonFill WRITE setPolygonFill NOTIFY symbolChanged)
    Q_PROPERTY(QColor polygonOutlineColor READ polygonOutlineColor WRITE setPolygonOutlineColor
                   NOTIFY symbolChanged)
    Q_PROPERTY(double polygonOutlineWidth READ polygonOutlineWidth WRITE setPolygonOutlineWidth
                   NOTIFY symbolChanged)

    // Labels
    Q_PROPERTY(bool showLabels READ showLabels WRITE setShowLabels NOTIFY symbolChanged)
    Q_PROPERTY(QString labelField READ labelField WRITE setLabelField NOTIFY symbolChanged)
    Q_PROPERTY(QFont labelFont READ labelFont WRITE setLabelFont NOTIFY symbolChanged)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY symbolChanged)

    Q_CLASSINFO("group:markerShape", "Marker (points)")
    Q_CLASSINFO("group:markerSize", "Marker (points)")
    Q_CLASSINFO("group:markerFill", "Marker (points)")
    Q_CLASSINFO("group:markerOutline", "Marker (points)")
    Q_CLASSINFO("group:markerOutlineW", "Marker (points)")
    Q_CLASSINFO("group:lineColor", "Line")
    Q_CLASSINFO("group:lineWidth", "Line")
    Q_CLASSINFO("group:lineDash", "Line")
    Q_CLASSINFO("group:polygonFill", "Polygon")
    Q_CLASSINFO("group:polygonOutlineColor", "Polygon")
    Q_CLASSINFO("group:polygonOutlineWidth", "Polygon")
    Q_CLASSINFO("group:showLabels", "Labels")
    Q_CLASSINFO("group:labelField", "Labels")
    Q_CLASSINFO("group:labelFont", "Labels")
    Q_CLASSINFO("group:labelColor", "Labels")

public:
    /*! Re-export the marker shape enum so QPropertyModel can offer a combo. */
    enum MarkerShape {
        Circle = GISVectorSymbol::Circle,
        Square = GISVectorSymbol::Square,
        Triangle = GISVectorSymbol::Triangle,
        Diamond = GISVectorSymbol::Diamond,
        Star = GISVectorSymbol::Star,
        Cross = GISVectorSymbol::Cross,
    };
    Q_ENUM(MarkerShape)

    using Writer = std::function<void(const GISVectorSymbol &)>;

    /**
     * @brief Create a cached property-editing facade with synchronous write-back.
     * @param[in] initial Symbol copied/moved into local state; the source layer is not read automatically.
     * @param[in] writer Optional callback retained by value, invoked after each effective setter with the whole cached symbol. Captured objects must remain valid through callbacks.
     * @param[in] parent Optional QObject owner, normally the source layer.
     * @note Use on the owning GUI thread. No numeric clamping, renderer validation, source IO or error reporting is added.
     */
    explicit GisVectorSymbolAdapter(GISVectorSymbol initial, Writer writer,
                                    QObject *parent = nullptr)
        : QObject(parent), m_sym(std::move(initial)), m_writer(std::move(writer))
    {
    }

    // Marker
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Point glyph ordinal; only point geometry uses it. Returned by value without consulting the source layer.
     */
    [[nodiscard]] MarkerShape markerShape() const
    {
        return static_cast<MarkerShape>(m_sym.markerShape);
    }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Point marker diameter in logical pixels. Returned by value without consulting the source layer.
     */
    [[nodiscard]] double markerSize() const { return m_sym.markerSize; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Point fill colour including alpha. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QColor markerFill() const { return m_sym.markerFill; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Point outline colour including alpha. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QColor markerOutline() const { return m_sym.markerOutline; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Point outline width in logical pixels. Returned by value without consulting the source layer.
     */
    [[nodiscard]] double markerOutlineW() const { return m_sym.markerOutlineW; }

    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Point glyph ordinal; only point geometry uses it. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setMarkerShape(MarkerShape v)
    {
        if (markerShape() == v)
            return;
        m_sym.markerShape = static_cast<GISVectorSymbol::MarkerShape>(v);
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Point marker diameter in logical pixels. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note qFuzzyCompare equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setMarkerSize(double v)
    {
        if (qFuzzyCompare(m_sym.markerSize, v))
            return;
        m_sym.markerSize = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Point fill colour including alpha. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setMarkerFill(const QColor &v)
    {
        if (m_sym.markerFill == v)
            return;
        m_sym.markerFill = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Point outline colour including alpha. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setMarkerOutline(const QColor &v)
    {
        if (m_sym.markerOutline == v)
            return;
        m_sym.markerOutline = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Point outline width in logical pixels. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note qFuzzyCompare equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setMarkerOutlineW(double v)
    {
        if (qFuzzyCompare(m_sym.markerOutlineW, v))
            return;
        m_sym.markerOutlineW = v;
        commit();
    }

    // Line — decompose QPen into colour/width/dash for the property editor.
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Line pen colour including alpha; other pen properties are retained. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QColor lineColor() const { return m_sym.linePen.color(); }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Line pen width in logical pixels because scene pens are cosmetic. Returned by value without consulting the source layer.
     */
    [[nodiscard]] double lineWidth() const { return m_sym.linePen.widthF(); }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Line pen dash/style enum; other pen properties are retained. Returned by value without consulting the source layer.
     */
    [[nodiscard]] Qt::PenStyle lineDash() const { return m_sym.linePen.style(); }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Line pen colour including alpha; other pen properties are retained. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setLineColor(const QColor &v)
    {
        QPen p = m_sym.linePen;
        if (p.color() == v)
            return;
        p.setColor(v);
        m_sym.linePen = p;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Line pen width in logical pixels because scene pens are cosmetic. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note qFuzzyCompare equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setLineWidth(double v)
    {
        QPen p = m_sym.linePen;
        if (qFuzzyCompare(p.widthF(), v))
            return;
        p.setWidthF(v);
        m_sym.linePen = p;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Line pen dash/style enum; other pen properties are retained. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setLineDash(Qt::PenStyle v)
    {
        QPen p = m_sym.linePen;
        if (p.style() == v)
            return;
        p.setStyle(v);
        m_sym.linePen = p;
        commit();
    }

    // Polygon — same QBrush/QPen decomposition.
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Polygon brush colour including alpha; existing brush style is retained. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QColor polygonFill() const { return m_sym.polygonFill.color(); }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Polygon outline colour including alpha; other pen properties are retained. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QColor polygonOutlineColor() const { return m_sym.polygonOutline.color(); }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Cosmetic polygon outline width in logical pixels. Returned by value without consulting the source layer.
     */
    [[nodiscard]] double polygonOutlineWidth() const { return m_sym.polygonOutline.widthF(); }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Polygon brush colour including alpha; existing brush style is retained. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setPolygonFill(const QColor &v)
    {
        QBrush b = m_sym.polygonFill;
        if (b.color() == v)
            return;
        b.setColor(v);
        m_sym.polygonFill = b;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Polygon outline colour including alpha; other pen properties are retained. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setPolygonOutlineColor(const QColor &v)
    {
        QPen p = m_sym.polygonOutline;
        if (p.color() == v)
            return;
        p.setColor(v);
        m_sym.polygonOutline = p;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Cosmetic polygon outline width in logical pixels. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note qFuzzyCompare equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setPolygonOutlineWidth(double v)
    {
        QPen p = m_sym.polygonOutline;
        if (qFuzzyCompare(p.widthF(), v))
            return;
        p.setWidthF(v);
        m_sym.polygonOutline = p;
        commit();
    }

    // Labels
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Legacy label visibility request; no inherited layer label-policy synchronization is performed by this adapter. Returned by value without consulting the source layer.
     */
    [[nodiscard]] bool showLabels() const { return m_sym.showLabels; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Exact physical OGR field name for legacy labels; not validated against a source. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QString labelField() const { return m_sym.labelField; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Legacy label font, retaining Qt point/pixel sizing. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QFont labelFont() const { return m_sym.labelFont; }
    /**
     * @brief Inspect the adapter's current cached symbol property.
     * @return Legacy label colour including alpha. Returned by value without consulting the source layer.
     */
    [[nodiscard]] QColor labelColor() const { return m_sym.labelColor; }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Legacy label visibility request; no inherited layer label-policy synchronization is performed by this adapter. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setShowLabels(bool v)
    {
        if (m_sym.showLabels == v)
            return;
        m_sym.showLabels = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Exact physical OGR field name for legacy labels; not validated against a source. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setLabelField(const QString &v)
    {
        if (m_sym.labelField == v)
            return;
        m_sym.labelField = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Legacy label font, retaining Qt point/pixel sizing. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setLabelFont(const QFont &v)
    {
        if (m_sym.labelFont == v)
            return;
        m_sym.labelFont = v;
        commit();
    }
    /**
     * @brief Edit cached symbology and write the whole updated value back synchronously.
     * @param[in] v Legacy label colour including alpha. Numeric values should be finite and meaningful; the adapter adds no range/colour/enum validation.
     * @note Exact equality is a no-op. Otherwise changes local state, invokes the optional writer and emits symbolChanged(). Writer-side resynchronization can emit additional notifications; no rollback/error result is provided.
     */
    void setLabelColor(const QColor &v)
    {
        if (m_sym.labelColor == v)
            return;
        m_sym.labelColor = v;
        commit();
    }

    /*! Refresh the cached symbol from the layer WITHOUT invoking the writer
     *  (no write-back loop). Emits symbolChanged so mounted editors re-read.
     *  Used by GISVectorLayer to keep its persistent adapter truthful.
     * @param[in] s Whole source symbol copied unconditionally into the cache; writer is not called.
     * @note Always emits symbolChanged(), even for equal state, so editors reread the complete bag. GUI-thread operation; does not validate or update the source layer.
     */
    void resyncFrom(const GISVectorSymbol &s)
    {
        m_sym = s;
        emit symbolChanged();
    }

signals:
    /**
     * @brief Tell property editors to reread cached state after commit or resynchronization.
     * @note May be emitted more than once for one edit when the writer updates a layer that resynchronizes this adapter. No payload or equality guarantee is supplied.
     */
    void symbolChanged();

private:
    /**
     * @brief Invoke the retained writer with current cached symbology, then emit symbolChanged().
     * @note Empty writer still permits local edits/notification. Callback runs synchronously on the caller thread; no error handling, rollback, reentrancy guard or thread dispatch is supplied.
     */
    void commit()
    {
        if (m_writer)
            m_writer(m_sym);
        emit symbolChanged();
    }
    GISVectorSymbol m_sym;
    Writer m_writer;
};

#endif // OPENSWMMVIS_LAYERS_GISVECTORSYMBOLADAPTER_H
