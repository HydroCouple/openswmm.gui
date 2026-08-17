/*!
 * \file   featuresublayerstyle.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Common style-bag base for the per-Category result sublayers.
 *
 *         Slice U-0 — granular per-Category sublayer refactor. The legacy
 *         four-sublayer mix (NodeMarker / ConduitLine / ConduitArrow /
 *         SubcatchmentFill) is replaced by one FeatureSublayer instance per
 *         SWMMModelLayer::Category (11 in total). Each instance owns a
 *         derived FeatureSublayerStyle whose Q_PROPERTYs match the
 *         archetype (Point / Line / Polygon) of its category, so the
 *         QPropertyModel-backed editor only surfaces meaningful knobs for
 *         each kind.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_FEATURE_FEATURESUBLAYERSTYLE_H
#define OPENSWMM_RENDER_SUBLAYERS_FEATURE_FEATURESUBLAYERSTYLE_H

#include "render/sublayerstyle.h"
#include "render/labelconfig.h"   // L-1 — per-sublayer labels

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <Qt>

namespace OpenSWMM::Render
{

/*!
 * \class FeatureSublayerStyle
 * \brief Common property bag for every per-Category result sublayer.
 *
 *        All sublayers — points, lines, polygons — share these knobs:
 *
 *          attribute     QString  output variable name driving the colour
 *          color         QColor   fallback / single-symbol colour
 *          useColorRamp  bool     when true, paint uses the layer-level
 *                                 ramp keyed by `attribute`; when false the
 *                                 single `color` is used (handy for static
 *                                 base symbology before results load).
 *
 *        Subclasses add archetype-specific knobs.
 */
class FeatureSublayerStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QString attribute    READ attribute    WRITE setAttribute    NOTIFY styleChanged)
    Q_PROPERTY(QColor  color        READ color        WRITE setColor        NOTIFY styleChanged)
    Q_PROPERTY(bool    useColorRamp READ useColorRamp WRITE setUseColorRamp NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute",    "Classification")
    Q_CLASSINFO("group:color",        "Symbology")
    Q_CLASSINFO("group:useColorRamp", "Symbology")

public:
    explicit FeatureSublayerStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    [[nodiscard]] QString attribute()    const { return m_attribute; }
    [[nodiscard]] QColor  color()        const { return m_color; }
    [[nodiscard]] bool    useColorRamp() const { return m_useColorRamp; }

    void setAttribute(const QString &v)    { if (m_attribute    == v) return; m_attribute    = v; setDirty(); }
    void setColor(const QColor &v)         { if (m_color        == v) return; m_color        = v; setDirty(); }
    void setUseColorRamp(bool v)           { if (m_useColorRamp == v) return; m_useColorRamp = v; setDirty(); }

    // L-1 — per-sublayer labels. Each sublayer (kind) carries its own
    // LabelConfig so the user can label, say, Junctions but not Conduits,
    // and give each its own expression ("{name}: {depth} m"). The results
    // layer's refreshLabels() reads this instead of the layer-level config.
    [[nodiscard]] const LabelConfig &labelConfig() const { return m_labelConfig; }
    void setLabelConfig(const LabelConfig &c) { if (m_labelConfig == c) return; m_labelConfig = c; setDirty(); }

    [[nodiscard]] QJsonObject toJson() const override
    {
        QJsonObject j;
        j[QStringLiteral("attribute")]    = m_attribute;
        j[QStringLiteral("color")]        = m_color.name(QColor::HexArgb);
        j[QStringLiteral("useColorRamp")] = m_useColorRamp;
        if (m_labelConfig.enabled || !m_labelConfig.expression.isEmpty())
            j[QStringLiteral("labelConfig")] = m_labelConfig.toJson();
        return j;
    }
    void fromJson(const QJsonObject &j) override
    {
        if (j.contains(QStringLiteral("attribute")))
            m_attribute = j.value(QStringLiteral("attribute")).toString();
        if (j.contains(QStringLiteral("color"))) {
            const QColor c(j.value(QStringLiteral("color")).toString());
            if (c.isValid()) m_color = c;
        }
        if (j.contains(QStringLiteral("useColorRamp")))
            m_useColorRamp = j.value(QStringLiteral("useColorRamp")).toBool(true);
        if (j.contains(QStringLiteral("labelConfig")))
            m_labelConfig.fromJson(j.value(QStringLiteral("labelConfig")).toObject());
        setDirty();
    }

private:
    QString     m_attribute    = QStringLiteral("depth");
    QColor      m_color        = QColor(60, 60, 60);
    bool        m_useColorRamp = true;
    LabelConfig m_labelConfig;            // L-1 — per-sublayer label settings
};

// ---------------------------------------------------------------------------
// Point archetype — Junction / Outfall / Storage / Divider / RainGage
// ---------------------------------------------------------------------------

class PointFeatureSublayerStyle : public FeatureSublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(double      markerSizePx READ markerSizePx WRITE setMarkerSizePx NOTIFY styleChanged)
    Q_PROPERTY(MarkerShape shape        READ shape        WRITE setShape        NOTIFY styleChanged)

    Q_CLASSINFO("group:markerSizePx", "Symbology")
    Q_CLASSINFO("group:shape",        "Symbology")

public:
    enum MarkerShape { Circle = 0, Square, Triangle, Diamond, Star };
    Q_ENUM(MarkerShape)

    explicit PointFeatureSublayerStyle(QObject *parent = nullptr)
        : FeatureSublayerStyle(parent) {}

    [[nodiscard]] double      markerSizePx() const { return m_markerSizePx; }
    [[nodiscard]] MarkerShape shape()        const { return m_shape; }

    void setMarkerSizePx(double v) { if (qFuzzyCompare(m_markerSizePx, v)) return; m_markerSizePx = v; setDirty(); }
    void setShape(MarkerShape v)   { if (m_shape == v) return; m_shape = v; setDirty(); }

    [[nodiscard]] QJsonObject toJson() const override
    {
        QJsonObject j = FeatureSublayerStyle::toJson();
        j[QStringLiteral("markerSizePx")] = m_markerSizePx;
        j[QStringLiteral("shape")]        = int(m_shape);
        return j;
    }
    void fromJson(const QJsonObject &j) override
    {
        FeatureSublayerStyle::fromJson(j);
        if (j.contains(QStringLiteral("markerSizePx")))
            m_markerSizePx = j.value(QStringLiteral("markerSizePx")).toDouble(6.0);
        if (j.contains(QStringLiteral("shape")))
            m_shape = static_cast<MarkerShape>(j.value(QStringLiteral("shape")).toInt(int(Circle)));
        setDirty();
    }

private:
    double      m_markerSizePx = 6.0;
    MarkerShape m_shape        = Circle;
};

// ---------------------------------------------------------------------------
// Line archetype — Conduit / Pump / Orifice / Weir / Outlet
//
// Folds the legacy ConduitArrowSublayer in via showFlowArrows + the
// arrow*Px knobs, so each link kind controls its own arrow behaviour.
// ---------------------------------------------------------------------------

class LineFeatureSublayerStyle : public FeatureSublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(double       lineWidthPx     READ lineWidthPx     WRITE setLineWidthPx     NOTIFY styleChanged)
    Q_PROPERTY(Qt::PenStyle dashPattern     READ dashPattern     WRITE setDashPattern     NOTIFY styleChanged)
    Q_PROPERTY(bool         showFlowArrows  READ showFlowArrows  WRITE setShowFlowArrows  NOTIFY styleChanged)
    Q_PROPERTY(double       arrowLengthPx   READ arrowLengthPx   WRITE setArrowLengthPx   NOTIFY styleChanged)
    Q_PROPERTY(double       arrowWidthPx    READ arrowWidthPx    WRITE setArrowWidthPx    NOTIFY styleChanged)
    Q_PROPERTY(QColor       arrowColor      READ arrowColor      WRITE setArrowColor      NOTIFY styleChanged)
    Q_PROPERTY(bool         renderAsLine    READ renderAsLine    WRITE setRenderAsLine    NOTIFY styleChanged)

    Q_CLASSINFO("group:lineWidthPx",    "Line")
    Q_CLASSINFO("group:dashPattern",    "Line")
    Q_CLASSINFO("group:renderAsLine",   "Line")
    Q_CLASSINFO("group:showFlowArrows", "Flow arrows")
    Q_CLASSINFO("group:arrowLengthPx",  "Flow arrows")
    Q_CLASSINFO("group:arrowWidthPx",   "Flow arrows")
    Q_CLASSINFO("group:arrowColor",     "Flow arrows")

public:
    explicit LineFeatureSublayerStyle(QObject *parent = nullptr)
        : FeatureSublayerStyle(parent) {}

    [[nodiscard]] double       lineWidthPx()     const { return m_lineWidthPx; }
    [[nodiscard]] Qt::PenStyle dashPattern()     const { return m_dashPattern; }
    [[nodiscard]] bool         showFlowArrows()  const { return m_showFlowArrows; }
    [[nodiscard]] double       arrowLengthPx()   const { return m_arrowLengthPx; }
    [[nodiscard]] double       arrowWidthPx()    const { return m_arrowWidthPx; }
    [[nodiscard]] QColor       arrowColor()      const { return m_arrowColor; }
    /*! For point-like links (pumps, orifices, weirs, outlets) the user can
     *  flip this OFF to fall back to a midpoint glyph instead of tracing
     *  the (often-degenerate) start→end polyline. Conduits default ON. */
    [[nodiscard]] bool         renderAsLine()    const { return m_renderAsLine; }

    void setLineWidthPx(double v)        { if (qFuzzyCompare(m_lineWidthPx, v)) return; m_lineWidthPx = v; setDirty(); }
    void setDashPattern(Qt::PenStyle v)  { if (m_dashPattern == v) return; m_dashPattern = v; setDirty(); }
    void setShowFlowArrows(bool v)       { if (m_showFlowArrows == v) return; m_showFlowArrows = v; setDirty(); }
    void setArrowLengthPx(double v)      { if (qFuzzyCompare(m_arrowLengthPx, v)) return; m_arrowLengthPx = v; setDirty(); }
    void setArrowWidthPx(double v)       { if (qFuzzyCompare(m_arrowWidthPx, v)) return; m_arrowWidthPx = v; setDirty(); }
    void setArrowColor(const QColor &v)  { if (m_arrowColor == v) return; m_arrowColor = v; setDirty(); }
    void setRenderAsLine(bool v)         { if (m_renderAsLine == v) return; m_renderAsLine = v; setDirty(); }

    [[nodiscard]] QJsonObject toJson() const override
    {
        QJsonObject j = FeatureSublayerStyle::toJson();
        j[QStringLiteral("lineWidthPx")]    = m_lineWidthPx;
        j[QStringLiteral("dashPattern")]    = int(m_dashPattern);
        j[QStringLiteral("showFlowArrows")] = m_showFlowArrows;
        j[QStringLiteral("arrowLengthPx")]  = m_arrowLengthPx;
        j[QStringLiteral("arrowWidthPx")]   = m_arrowWidthPx;
        j[QStringLiteral("arrowColor")]     = m_arrowColor.name(QColor::HexArgb);
        j[QStringLiteral("renderAsLine")]   = m_renderAsLine;
        return j;
    }
    void fromJson(const QJsonObject &j) override
    {
        FeatureSublayerStyle::fromJson(j);
        if (j.contains(QStringLiteral("lineWidthPx")))    m_lineWidthPx    = j.value(QStringLiteral("lineWidthPx")).toDouble(1.5);
        if (j.contains(QStringLiteral("dashPattern")))    m_dashPattern    = static_cast<Qt::PenStyle>(j.value(QStringLiteral("dashPattern")).toInt(int(Qt::SolidLine)));
        if (j.contains(QStringLiteral("showFlowArrows"))) m_showFlowArrows = j.value(QStringLiteral("showFlowArrows")).toBool(true);
        if (j.contains(QStringLiteral("arrowLengthPx")))  m_arrowLengthPx  = j.value(QStringLiteral("arrowLengthPx")).toDouble(16.0);
        if (j.contains(QStringLiteral("arrowWidthPx")))   m_arrowWidthPx   = j.value(QStringLiteral("arrowWidthPx")).toDouble(8.0);
        if (j.contains(QStringLiteral("arrowColor"))) {
            const QColor c(j.value(QStringLiteral("arrowColor")).toString());
            if (c.isValid()) m_arrowColor = c;
        }
        if (j.contains(QStringLiteral("renderAsLine")))   m_renderAsLine   = j.value(QStringLiteral("renderAsLine")).toBool(true);
        setDirty();
    }

private:
    double       m_lineWidthPx    = 1.5;
    Qt::PenStyle m_dashPattern    = Qt::SolidLine;
    // Line sublayers are the link kinds on a results layer, so arrows are
    // on out of the box here too (matches the model layer's per-kind seed).
    bool         m_showFlowArrows = true;
    double       m_arrowLengthPx  = 16.0;
    double       m_arrowWidthPx   = 8.0;
    QColor       m_arrowColor     = QColor(20, 20, 20, 220);
    bool         m_renderAsLine   = true;
};

// ---------------------------------------------------------------------------
// Polygon archetype — Subcatchment
// ---------------------------------------------------------------------------

class PolygonFeatureSublayerStyle : public FeatureSublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor outlineColor   READ outlineColor   WRITE setOutlineColor   NOTIFY styleChanged)
    Q_PROPERTY(double outlineWidthPx READ outlineWidthPx WRITE setOutlineWidthPx NOTIFY styleChanged)
    Q_PROPERTY(double fillOpacity    READ fillOpacity    WRITE setFillOpacity    NOTIFY styleChanged)
    // VS.2b — Qt::BrushStyle (as int) so hatch / pattern fills are reachable
    // and round-trip; the paint path composes the brush via FillSymbolLayerSpec.
    // Qt::SolidPattern == 1, Qt::NoBrush == 0, hatches are 9..14.
    Q_PROPERTY(int    fillStyle      READ fillStyle      WRITE setFillStyle      NOTIFY styleChanged)

    Q_CLASSINFO("group:outlineColor",   "Outline")
    Q_CLASSINFO("group:outlineWidthPx", "Outline")
    Q_CLASSINFO("group:fillOpacity",    "Fill")
    Q_CLASSINFO("group:fillStyle",      "Fill")

public:
    explicit PolygonFeatureSublayerStyle(QObject *parent = nullptr)
        : FeatureSublayerStyle(parent) {}

    [[nodiscard]] QColor outlineColor()   const { return m_outlineColor; }
    [[nodiscard]] double outlineWidthPx() const { return m_outlineWidthPx; }
    [[nodiscard]] double fillOpacity()    const { return m_fillOpacity; }
    [[nodiscard]] int    fillStyle()      const { return m_fillStyle; }

    void setOutlineColor(const QColor &v)  { if (m_outlineColor == v) return; m_outlineColor = v; setDirty(); }
    void setOutlineWidthPx(double v)       { if (qFuzzyCompare(m_outlineWidthPx, v)) return; m_outlineWidthPx = v; setDirty(); }
    void setFillOpacity(double v)          { if (qFuzzyCompare(m_fillOpacity, v)) return; m_fillOpacity = v; setDirty(); }
    void setFillStyle(int v)               { if (m_fillStyle == v) return; m_fillStyle = v; setDirty(); }

    [[nodiscard]] QJsonObject toJson() const override
    {
        QJsonObject j = FeatureSublayerStyle::toJson();
        j[QStringLiteral("outlineColor")]   = m_outlineColor.name(QColor::HexArgb);
        j[QStringLiteral("outlineWidthPx")] = m_outlineWidthPx;
        j[QStringLiteral("fillOpacity")]    = m_fillOpacity;
        j[QStringLiteral("fillStyle")]      = m_fillStyle;
        return j;
    }
    void fromJson(const QJsonObject &j) override
    {
        FeatureSublayerStyle::fromJson(j);
        if (j.contains(QStringLiteral("outlineColor"))) {
            const QColor c(j.value(QStringLiteral("outlineColor")).toString());
            if (c.isValid()) m_outlineColor = c;
        }
        if (j.contains(QStringLiteral("outlineWidthPx")))
            m_outlineWidthPx = j.value(QStringLiteral("outlineWidthPx")).toDouble(0.5);
        if (j.contains(QStringLiteral("fillOpacity")))
            m_fillOpacity = j.value(QStringLiteral("fillOpacity")).toDouble(0.55);
        if (j.contains(QStringLiteral("fillStyle")))
            m_fillStyle = j.value(QStringLiteral("fillStyle")).toInt(int(Qt::SolidPattern));
        setDirty();
    }

private:
    QColor m_outlineColor   = QColor(40, 40, 40, 220);
    double m_outlineWidthPx = 0.5;
    double m_fillOpacity    = 0.55;
    int    m_fillStyle      = int(Qt::SolidPattern);   // VS.2b
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SUBLAYERS_FEATURE_FEATURESUBLAYERSTYLE_H
