/**
 * @file      featuresublayerstyle.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Common style-bag base for the per-Category result sublayers.
 *
 *         Slice U-0 — granular per-Category sublayer refactor. The legacy
 *         four-sublayer mix (NodeMarker / ConduitLine / ConduitArrow /
 *         SubcatchmentFill) is replaced by one FeatureSublayer instance per
 *         SWMMModelLayer::Category (11 in total). Each instance owns a
 *         derived FeatureSublayerStyle whose Q_PROPERTYs match the
 *         archetype (Point / Line / Polygon) of its category, so the
 *         QPropertyModel-backed editor only surfaces meaningful knobs for
 *         each kind.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_FEATURE_FEATURESUBLAYERSTYLE_H
#define OPENSWMM_RENDER_SUBLAYERS_FEATURE_FEATURESUBLAYERSTYLE_H

#include "render/sublayerstyle.h"
#include "render/labelconfig.h" // L-1 — per-sublayer labels
#include "render/sizechannel.h"

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
    Q_PROPERTY(QString attribute READ attribute WRITE setAttribute NOTIFY styleChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(bool useColorRamp READ useColorRamp WRITE setUseColorRamp NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute", "Classification")
    Q_CLASSINFO("group:color", "Symbology")
    Q_CLASSINFO("group:useColorRamp", "Symbology")

public:
    /** @brief Construct the style with its declared default fields.
     * @param parent Optional QObject owner; destruction of the parent destroys this style.
     * Access and edits occur on its owning Qt thread. */
    explicit FeatureSublayerStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    /** @brief Read the color-driving output-variable key.
     * @return Stored value. Initially "depth"; names are stored verbatim without checking attribute availability. */
    [[nodiscard]] QString attribute() const { return m_attribute; }
    /** @brief Read the single-symbol/fallback color.
     * @return Stored value. Initially RGB(60,60,60); invalid QColor values are accepted by this setter. */
    [[nodiscard]] QColor color() const { return m_color; }
    /** @brief Read the attribute-driven color-ramp switch.
     * @return Stored value. Initially true; changing it does not clear the attribute key. */
    [[nodiscard]] bool useColorRamp() const { return m_useColorRamp; }

    /** @brief Replace the color-driving output-variable key and notify when it changes.
     * @param v New value. Initially "depth"; names are stored verbatim without checking attribute availability.
     * Uses equality to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setAttribute(const QString &v)
    {
        if (m_attribute == v)
            return;
        m_attribute = v;
        setDirty();
    }
    /** @brief Replace the single-symbol/fallback color and notify when it changes.
     * @param v New value. Initially RGB(60,60,60); invalid QColor values are accepted by this setter.
     * Uses equality to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setColor(const QColor &v)
    {
        if (m_color == v)
            return;
        m_color = v;
        setDirty();
    }
    /** @brief Replace the attribute-driven color-ramp switch and notify when it changes.
     * @param v New value. Initially true; changing it does not clear the attribute key.
     * Uses equality to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setUseColorRamp(bool v)
    {
        if (m_useColorRamp == v)
            return;
        m_useColorRamp = v;
        setDirty();
    }

    // L-1 — per-sublayer labels. Each sublayer (kind) carries its own
    // LabelConfig so the user can label, say, Junctions but not Conduits,
    // and give each its own expression ("{name}: {depth} m"). The results
    // layer's refreshLabels() reads this instead of the layer-level config.
    /** @brief Borrow the per-category label settings.
     * @return Stored config, replaced by setLabelConfig()/fromJson(); no label evaluation occurs. */
    [[nodiscard]] const LabelConfig &labelConfig() const { return m_labelConfig; }
    /** @brief Copy per-category label settings and notify on inequality.
     * @param c Configuration copied without validating font, scale or expression.
     * Emits styleChanged once when LabelConfig equality detects a difference. */
    void setLabelConfig(const LabelConfig &c)
    {
        if (m_labelConfig == c)
            return;
        m_labelConfig = c;
        setDirty();
    }

    /** @brief Serialize the current style without validating it.
     * @return Independent JSON object. Writes attribute, ARGB color and useColorRamp. labelConfig is included only
     * when enabled or its expression is nonempty; other dormant label changes can be omitted. */
    [[nodiscard]] QJsonObject toJson() const override
    {
        QJsonObject j;
        j[QStringLiteral("attribute")] = m_attribute;
        j[QStringLiteral("color")] = m_color.name(QColor::HexArgb);
        j[QStringLiteral("useColorRamp")] = m_useColorRamp;
        if (m_labelConfig.enabled || !m_labelConfig.expression.isEmpty())
            j[QStringLiteral("labelConfig")] = m_labelConfig.toJson();
        return j;
    }
    /** @brief Read a partial style object and emit normal style notifications.
     * @param j Borrowed JSON object; unknown keys are ignored.
     * Absent keys retain current fields. Invalid color text is ignored; present attribute
     * uses toString(), present useColorRamp defaults to true on type mismatch. A present
     * labelConfig delegates even a nonobject value as an empty object. Emits styleChanged
     * once unconditionally after loading, without using the individual setters. */
    void fromJson(const QJsonObject &j) override
    {
        if (j.contains(QStringLiteral("attribute")))
            m_attribute = j.value(QStringLiteral("attribute")).toString();
        if (j.contains(QStringLiteral("color"))) {
            const QColor c(j.value(QStringLiteral("color")).toString());
            if (c.isValid())
                m_color = c;
        }
        if (j.contains(QStringLiteral("useColorRamp")))
            m_useColorRamp = j.value(QStringLiteral("useColorRamp")).toBool(true);
        if (j.contains(QStringLiteral("labelConfig")))
            m_labelConfig.fromJson(j.value(QStringLiteral("labelConfig")).toObject());
        setDirty();
    }

private:
    QString m_attribute = QStringLiteral("depth");
    QColor m_color = QColor(60, 60, 60);
    bool m_useColorRamp = true;
    LabelConfig m_labelConfig; // L-1 — per-sublayer label settings
};

// ---------------------------------------------------------------------------
// Point archetype — Junction / Outfall / Storage / Divider / RainGage
// ---------------------------------------------------------------------------

class PointFeatureSublayerStyle : public FeatureSublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(double markerSizePx READ markerSizePx WRITE setMarkerSizePx NOTIFY styleChanged)
    Q_PROPERTY(MarkerShape shape READ shape WRITE setShape NOTIFY styleChanged)

    Q_CLASSINFO("group:markerSizePx", "Symbology")
    Q_CLASSINFO("group:shape", "Symbology")

public:
    enum MarkerShape { Circle = 0, Square, Triangle, Diamond, Star };
    Q_ENUM(MarkerShape)

    /** @brief Construct the style with its declared default fields.
     * @param parent Optional QObject owner; destruction of the parent destroys this style.
     * Access and edits occur on its owning Qt thread. */
    explicit PointFeatureSublayerStyle(QObject *parent = nullptr) : FeatureSublayerStyle(parent) {}

    /** @brief Read the marker size in logical screen pixels.
     * @return Stored value. Initially 6; negative and nonfinite values are not rejected or clamped. */
    [[nodiscard]] double markerSizePx() const { return m_markerSizePx; }
    /** @brief Read the local point-shape enum.
     * @return Stored value. Initially Circle; unchecked enum integers can be retained. */
    [[nodiscard]] MarkerShape shape() const { return m_shape; }

    /** @brief Replace the marker size in logical screen pixels and notify when it changes.
     * @param v New value. Initially 6; negative and nonfinite values are not rejected or clamped.
     * Uses qFuzzyCompare to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setMarkerSizePx(double v)
    {
        if (qFuzzyCompare(m_markerSizePx, v))
            return;
        m_markerSizePx = v;
        setDirty();
    }
    /** @brief Replace the local point-shape enum and notify when it changes.
     * @param v New value. Initially Circle; unchecked enum integers can be retained.
     * Uses equality to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setShape(MarkerShape v)
    {
        if (m_shape == v)
            return;
        m_shape = v;
        setDirty();
    }

    /*! Results layers: marker size follows a result variable (by area),
     *  whatever renderer colours the kind. */
    /** @return Borrowed area-scaled marker channel, replaced by setSizeChannel()/fromJson(). */
    [[nodiscard]] const SizeChannel &sizeChannel() const { return m_sizeChannel; }
    /** @brief Replace the result-driven marker-size channel using area scaling.
     * @param c Configuration copied by value; areaScaled is forced true before
     * equality comparison and move into storage. Other fields are not validated.
     * Emits styleChanged on a change, independent of the selected color renderer. */
    void setSizeChannel(SizeChannel c)
    {
        c.areaScaled = true;
        if (m_sizeChannel == c)
            return;
        m_sizeChannel = std::move(c);
        setDirty();
    }

    /** @brief Serialize the current style without validating it.
     * @return Independent JSON object. Adds markerSizePx, shape integer and the area-scaled sizeChannel to base JSON. */
    [[nodiscard]] QJsonObject toJson() const override
    {
        QJsonObject j = FeatureSublayerStyle::toJson();
        j[QStringLiteral("markerSizePx")] = m_markerSizePx;
        j[QStringLiteral("shape")] = int(m_shape);
        j[QStringLiteral("sizeChannel")] = m_sizeChannel.toJson();
        return j;
    }
    /** @brief Read a partial style object and emit normal style notifications.
     * @param j Borrowed JSON object; unknown keys are ignored.
     * Loads base fields first (emitting styleChanged before point fields change), then
     * present markerSizePx/shape with defaults 6/Circle on type mismatch; no range/enum
     * validation is applied. A present sizeChannel is read with areaScaled=true.
     * Emits styleChanged again unconditionally after the derived fields are loaded. */
    void fromJson(const QJsonObject &j) override
    {
        FeatureSublayerStyle::fromJson(j);
        if (j.contains(QStringLiteral("markerSizePx")))
            m_markerSizePx = j.value(QStringLiteral("markerSizePx")).toDouble(6.0);
        if (j.contains(QStringLiteral("shape")))
            m_shape = static_cast<MarkerShape>(j.value(QStringLiteral("shape")).toInt(int(Circle)));
        if (j.contains(QStringLiteral("sizeChannel")))
            m_sizeChannel =
                SizeChannel::fromJson(j.value(QStringLiteral("sizeChannel")).toObject(), true);
        setDirty();
    }

private:
    double m_markerSizePx = 6.0;
    MarkerShape m_shape = Circle;
    SizeChannel m_sizeChannel = [] {
        SizeChannel c;
        c.areaScaled = true;
        return c;
    }();
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
    Q_PROPERTY(double lineWidthPx READ lineWidthPx WRITE setLineWidthPx NOTIFY styleChanged)
    Q_PROPERTY(Qt::PenStyle dashPattern READ dashPattern WRITE setDashPattern NOTIFY styleChanged)
    Q_PROPERTY(bool showFlowArrows READ showFlowArrows WRITE setShowFlowArrows NOTIFY styleChanged)
    Q_PROPERTY(double arrowLengthPx READ arrowLengthPx WRITE setArrowLengthPx NOTIFY styleChanged)
    Q_PROPERTY(double arrowWidthPx READ arrowWidthPx WRITE setArrowWidthPx NOTIFY styleChanged)
    Q_PROPERTY(QColor arrowColor READ arrowColor WRITE setArrowColor NOTIFY styleChanged)
    Q_PROPERTY(bool renderAsLine READ renderAsLine WRITE setRenderAsLine NOTIFY styleChanged)

    Q_CLASSINFO("group:lineWidthPx", "Line")
    Q_CLASSINFO("group:dashPattern", "Line")
    Q_CLASSINFO("group:renderAsLine", "Line")
    Q_CLASSINFO("group:showFlowArrows", "Flow arrows")
    Q_CLASSINFO("group:arrowLengthPx", "Flow arrows")
    Q_CLASSINFO("group:arrowWidthPx", "Flow arrows")
    Q_CLASSINFO("group:arrowColor", "Flow arrows")

public:
    /** @brief Construct the style with its declared default fields.
     * @param parent Optional QObject owner; destruction of the parent destroys this style.
     * Access and edits occur on its owning Qt thread. */
    explicit LineFeatureSublayerStyle(QObject *parent = nullptr) : FeatureSublayerStyle(parent) {}

    /** @brief Read the stroke width in logical screen pixels.
     * @return Stored value. Initially 1.5; negative and nonfinite values are not rejected or clamped. */
    [[nodiscard]] double lineWidthPx() const { return m_lineWidthPx; }
    /** @brief Read the line pen-style enum.
     * @return Stored value. Initially Qt::SolidLine; unchecked enum integers can be retained. */
    [[nodiscard]] Qt::PenStyle dashPattern() const { return m_dashPattern; }
    /** @brief Read the flow-arrow visibility switch.
     * @return Stored value. Initially true, independent of renderAsLine. */
    [[nodiscard]] bool showFlowArrows() const { return m_showFlowArrows; }
    /** @brief Read the flow-arrow length in logical screen pixels.
     * @return Stored value. Initially 16; negative and nonfinite values are not rejected or clamped. */
    [[nodiscard]] double arrowLengthPx() const { return m_arrowLengthPx; }
    /** @brief Read the full arrowhead-base width in logical screen pixels.
     * @return Stored value. Initially 8; negative and nonfinite values are not rejected or clamped. */
    [[nodiscard]] double arrowWidthPx() const { return m_arrowWidthPx; }
    /** @brief Read the flow-arrow color.
     * @return Stored value. Initially RGBA(20,20,20,220); invalid QColor values are accepted by this setter. */
    [[nodiscard]] QColor arrowColor() const { return m_arrowColor; }
    /*! For point-like links (pumps, orifices, weirs, outlets) the user can
     *  flip this OFF to fall back to a midpoint glyph instead of tracing
     *  the (often-degenerate) start→end polyline. Conduits default ON. */
    /** @brief Read the polyline-versus-midpoint-glyph switch.
     * @return Stored value. Initially true in this style class; FeatureSublayer changes nonconduit links to false. */
    [[nodiscard]] bool renderAsLine() const { return m_renderAsLine; }

    /** @brief Replace the stroke width in logical screen pixels and notify when it changes.
     * @param v New value. Initially 1.5; negative and nonfinite values are not rejected or clamped.
     * Uses qFuzzyCompare to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setLineWidthPx(double v)
    {
        if (qFuzzyCompare(m_lineWidthPx, v))
            return;
        m_lineWidthPx = v;
        setDirty();
    }
    /** @brief Replace the line pen-style enum and notify when it changes.
     * @param v New value. Initially Qt::SolidLine; unchecked enum integers can be retained.
     * Uses equality to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setDashPattern(Qt::PenStyle v)
    {
        if (m_dashPattern == v)
            return;
        m_dashPattern = v;
        setDirty();
    }
    /** @brief Replace the flow-arrow visibility switch and notify when it changes.
     * @param v New value. Initially true, independent of renderAsLine.
     * Uses equality to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setShowFlowArrows(bool v)
    {
        if (m_showFlowArrows == v)
            return;
        m_showFlowArrows = v;
        setDirty();
    }
    /** @brief Replace the flow-arrow length in logical screen pixels and notify when it changes.
     * @param v New value. Initially 16; negative and nonfinite values are not rejected or clamped.
     * Uses qFuzzyCompare to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setArrowLengthPx(double v)
    {
        if (qFuzzyCompare(m_arrowLengthPx, v))
            return;
        m_arrowLengthPx = v;
        setDirty();
    }
    /** @brief Replace the full arrowhead-base width in logical screen pixels and notify when it changes.
     * @param v New value. Initially 8; negative and nonfinite values are not rejected or clamped.
     * Uses qFuzzyCompare to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setArrowWidthPx(double v)
    {
        if (qFuzzyCompare(m_arrowWidthPx, v))
            return;
        m_arrowWidthPx = v;
        setDirty();
    }
    /** @brief Replace the flow-arrow color and notify when it changes.
     * @param v New value. Initially RGBA(20,20,20,220); invalid QColor values are accepted by this setter.
     * Uses equality to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setArrowColor(const QColor &v)
    {
        if (m_arrowColor == v)
            return;
        m_arrowColor = v;
        setDirty();
    }
    /** @brief Replace the polyline-versus-midpoint-glyph switch and notify when it changes.
     * @param v New value. Initially true in this style class; FeatureSublayer changes nonconduit links to false.
     * Uses equality to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setRenderAsLine(bool v)
    {
        if (m_renderAsLine == v)
            return;
        m_renderAsLine = v;
        setDirty();
    }

    /*! Results layers: stroke width follows a result variable (linearly),
     *  whatever renderer colours the kind. */
    /** @return Borrowed linear stroke-width channel, replaced by setWidthChannel()/fromJson(). */
    [[nodiscard]] const SizeChannel &widthChannel() const { return m_widthChannel; }
    /** @brief Replace the result-driven stroke-width channel using linear scaling.
     * @param c Configuration copied by value; areaScaled is forced false before
     * equality comparison and move into storage. Other fields are not validated.
     * Emits styleChanged on a change, independent of the selected color renderer. */
    void setWidthChannel(SizeChannel c)
    {
        c.areaScaled = false;
        if (m_widthChannel == c)
            return;
        m_widthChannel = std::move(c);
        setDirty();
    }

    /** @brief Serialize the current style without validating it.
     * @return Independent JSON object. Adds stroke, arrow, renderAsLine and linear width-channel fields to base JSON.
     * The width channel is persisted under the historical sizeChannel key. */
    [[nodiscard]] QJsonObject toJson() const override
    {
        QJsonObject j = FeatureSublayerStyle::toJson();
        j[QStringLiteral("lineWidthPx")] = m_lineWidthPx;
        j[QStringLiteral("dashPattern")] = int(m_dashPattern);
        j[QStringLiteral("showFlowArrows")] = m_showFlowArrows;
        j[QStringLiteral("arrowLengthPx")] = m_arrowLengthPx;
        j[QStringLiteral("arrowWidthPx")] = m_arrowWidthPx;
        j[QStringLiteral("arrowColor")] = m_arrowColor.name(QColor::HexArgb);
        j[QStringLiteral("renderAsLine")] = m_renderAsLine;
        j[QStringLiteral("sizeChannel")] = m_widthChannel.toJson();
        return j;
    }
    /** @brief Read a partial style object and emit normal style notifications.
     * @param j Borrowed JSON object; unknown keys are ignored.
     * Loads base fields first (emitting styleChanged before line fields change). Absent
     * derived keys retain values; present numeric/bool/enum keys use their member defaults
     * on type mismatch, with no range checks. Invalid arrowColor text is ignored. A
     * present sizeChannel is read with areaScaled=false. Emits styleChanged again
     * unconditionally after the derived fields are loaded. */
    void fromJson(const QJsonObject &j) override
    {
        FeatureSublayerStyle::fromJson(j);
        if (j.contains(QStringLiteral("lineWidthPx")))
            m_lineWidthPx = j.value(QStringLiteral("lineWidthPx")).toDouble(1.5);
        if (j.contains(QStringLiteral("dashPattern")))
            m_dashPattern = static_cast<Qt::PenStyle>(
                j.value(QStringLiteral("dashPattern")).toInt(int(Qt::SolidLine)));
        if (j.contains(QStringLiteral("showFlowArrows")))
            m_showFlowArrows = j.value(QStringLiteral("showFlowArrows")).toBool(true);
        if (j.contains(QStringLiteral("arrowLengthPx")))
            m_arrowLengthPx = j.value(QStringLiteral("arrowLengthPx")).toDouble(16.0);
        if (j.contains(QStringLiteral("arrowWidthPx")))
            m_arrowWidthPx = j.value(QStringLiteral("arrowWidthPx")).toDouble(8.0);
        if (j.contains(QStringLiteral("arrowColor"))) {
            const QColor c(j.value(QStringLiteral("arrowColor")).toString());
            if (c.isValid())
                m_arrowColor = c;
        }
        if (j.contains(QStringLiteral("renderAsLine")))
            m_renderAsLine = j.value(QStringLiteral("renderAsLine")).toBool(true);
        if (j.contains(QStringLiteral("sizeChannel")))
            m_widthChannel =
                SizeChannel::fromJson(j.value(QStringLiteral("sizeChannel")).toObject(), false);
        setDirty();
    }

private:
    double m_lineWidthPx = 1.5;
    Qt::PenStyle m_dashPattern = Qt::SolidLine;
    // Line sublayers are the link kinds on a results layer, so arrows are
    // on out of the box here too (matches the model layer's per-kind seed).
    bool m_showFlowArrows = true;
    double m_arrowLengthPx = 16.0;
    /*! FULL width of the arrowhead base across the link — the same meaning
     *  the key carries on the model layer (SWMMElementSymbol::arrowWidth,
     *  halved by drawFlowArrow), and the same 8 px default, so a style seeded
     *  from a model layer draws the same arrow on both. */
    double m_arrowWidthPx = 8.0;
    QColor m_arrowColor = QColor(20, 20, 20, 220);
    bool m_renderAsLine = true;
    SizeChannel m_widthChannel;
};

// ---------------------------------------------------------------------------
// Polygon archetype — Subcatchment
// ---------------------------------------------------------------------------

class PolygonFeatureSublayerStyle : public FeatureSublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY styleChanged)
    Q_PROPERTY(
        double outlineWidthPx READ outlineWidthPx WRITE setOutlineWidthPx NOTIFY styleChanged)
    Q_PROPERTY(double fillOpacity READ fillOpacity WRITE setFillOpacity NOTIFY styleChanged)
    // VS.2b — Qt::BrushStyle (as int) so hatch / pattern fills are reachable
    // and round-trip; the paint path composes the brush via FillSymbolLayerSpec.
    // Qt::SolidPattern == 1, Qt::NoBrush == 0, hatches are 9..14.
    Q_PROPERTY(int fillStyle READ fillStyle WRITE setFillStyle NOTIFY styleChanged)

    Q_CLASSINFO("group:outlineColor", "Outline")
    Q_CLASSINFO("group:outlineWidthPx", "Outline")
    Q_CLASSINFO("group:fillOpacity", "Fill")
    Q_CLASSINFO("group:fillStyle", "Fill")

public:
    /** @brief Construct the style with its declared default fields.
     * @param parent Optional QObject owner; destruction of the parent destroys this style.
     * Access and edits occur on its owning Qt thread. */
    explicit PolygonFeatureSublayerStyle(QObject *parent = nullptr) : FeatureSublayerStyle(parent)
    {
    }

    /** @brief Read the polygon-outline color.
     * @return Stored value. Initially RGBA(40,40,40,220); invalid QColor values are accepted by this setter. */
    [[nodiscard]] QColor outlineColor() const { return m_outlineColor; }
    /** @brief Read the polygon-outline width in logical screen pixels.
     * @return Stored value. Initially 0.5; negative and nonfinite values are not rejected or clamped. */
    [[nodiscard]] double outlineWidthPx() const { return m_outlineWidthPx; }
    /** @brief Read the polygon-fill opacity multiplier.
     * @return Stored value. Initially 0.55; the advertised [0,1] range is not enforced. */
    [[nodiscard]] double fillOpacity() const { return m_fillOpacity; }
    /** @brief Read the Qt::BrushStyle integer for the polygon fill.
     * @return Stored value. Initially Qt::SolidPattern; arbitrary integers are accepted without validation. */
    [[nodiscard]] int fillStyle() const { return m_fillStyle; }

    /** @brief Replace the polygon-outline color and notify when it changes.
     * @param v New value. Initially RGBA(40,40,40,220); invalid QColor values are accepted by this setter.
     * Uses equality to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setOutlineColor(const QColor &v)
    {
        if (m_outlineColor == v)
            return;
        m_outlineColor = v;
        setDirty();
    }
    /** @brief Replace the polygon-outline width in logical screen pixels and notify when it changes.
     * @param v New value. Initially 0.5; negative and nonfinite values are not rejected or clamped.
     * Uses qFuzzyCompare to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setOutlineWidthPx(double v)
    {
        if (qFuzzyCompare(m_outlineWidthPx, v))
            return;
        m_outlineWidthPx = v;
        setDirty();
    }
    /** @brief Replace the polygon-fill opacity multiplier and notify when it changes.
     * @param v New value. Initially 0.55; the advertised [0,1] range is not enforced.
     * Uses qFuzzyCompare to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setFillOpacity(double v)
    {
        if (qFuzzyCompare(m_fillOpacity, v))
            return;
        m_fillOpacity = v;
        setDirty();
    }
    /** @brief Replace the Qt::BrushStyle integer for the polygon fill and notify when it changes.
     * @param v New value. Initially Qt::SolidPattern; arbitrary integers are accepted without validation.
     * Uses equality to suppress unchanged assignments; otherwise stores it
     * and calls setDirty(), emitting styleChanged synchronously. */
    void setFillStyle(int v)
    {
        if (m_fillStyle == v)
            return;
        m_fillStyle = v;
        setDirty();
    }

    /** @brief Serialize the current style without validating it.
     * @return Independent JSON object. Adds ARGB outlineColor, outlineWidthPx, fillOpacity and fillStyle to base JSON. */
    [[nodiscard]] QJsonObject toJson() const override
    {
        QJsonObject j = FeatureSublayerStyle::toJson();
        j[QStringLiteral("outlineColor")] = m_outlineColor.name(QColor::HexArgb);
        j[QStringLiteral("outlineWidthPx")] = m_outlineWidthPx;
        j[QStringLiteral("fillOpacity")] = m_fillOpacity;
        j[QStringLiteral("fillStyle")] = m_fillStyle;
        return j;
    }
    /** @brief Read a partial style object and emit normal style notifications.
     * @param j Borrowed JSON object; unknown keys are ignored.
     * Loads base fields first (emitting styleChanged before polygon fields change).
     * Absent derived keys retain values; invalid outline color text is ignored. Present
     * width/opacity/brush integers use 0.5/0.55/Qt::SolidPattern on type mismatch with
     * no range/enum validation. Emits styleChanged again after loading unconditionally. */
    void fromJson(const QJsonObject &j) override
    {
        FeatureSublayerStyle::fromJson(j);
        if (j.contains(QStringLiteral("outlineColor"))) {
            const QColor c(j.value(QStringLiteral("outlineColor")).toString());
            if (c.isValid())
                m_outlineColor = c;
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
    QColor m_outlineColor = QColor(40, 40, 40, 220);
    double m_outlineWidthPx = 0.5;
    double m_fillOpacity = 0.55;
    int m_fillStyle = int(Qt::SolidPattern); // VS.2b
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SUBLAYERS_FEATURE_FEATURESUBLAYERSTYLE_H
