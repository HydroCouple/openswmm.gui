#pragma once
/**
 * @file rulestylesync.h
 * @brief Keep a 2D layer's seed Rules (Layer Styling dock) in step with its live sublayer styles.
 *
 * The mesh and 2D results layers each expose a fixed rule list whose single symbols mirror
 * sublayer styles the renderers read. A binding seeds its rule from the live style, and on an
 * edit writes back only the props that changed since, so editing one field in the dock never
 * resets the others to stale or default values.
 */

#include "render/renderers/singlesymbolrenderer.h"
#include "render/rule.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshnodesublayer.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

#include <QPointer>
#include <QVariantMap>
#include <functional>

namespace OpenSWMM::Render
{

/**
 * @brief One rule bound to the live style it mirrors.
 * read() returns the live values under the dock adapters' prop keys; write() applies one
 * changed key (unknown keys are ignored); the opacity pair maps the symbol's overall opacity.
 */
struct RuleStyleBinding {
    QPointer<Rule> rule;
    SymbolLayerKind kind = SymbolLayerKind::SimpleMarker;
    std::function<SymbolLayer()> defaults;
    std::function<QVariantMap()> read;
    std::function<void(const QString &, const QVariant &)> write;
    std::function<qreal()> readOpacity;
    std::function<void(qreal)> writeOpacity;
    QVariantMap last;
    qreal lastOpacity = 1.0;
    bool applying = false;
};

/**
 * @brief Copy the live style into the rule's single symbol, without notifying.
 * A rule whose renderer the user switched away from Single Symbol is left alone.
 */
inline void seedRuleFromStyle(RuleStyleBinding &b)
{
    auto *single = b.rule ? dynamic_cast<SingleSymbolRenderer *>(b.rule->renderer()) : nullptr;
    if (!single || b.applying)
        return;
    SymbolStyle style = single->symbol();
    if (style.layers.isEmpty() || style.layers.first().kind != b.kind) {
        style.layers.clear();
        style.layers.append(b.defaults());
    }
    QVariantMap &props = style.layers.first().props;
    const QVariantMap live = b.read();
    for (auto it = live.cbegin(); it != live.cend(); ++it)
        props.insert(it.key(), it.value());
    if (b.readOpacity)
        style.opacity = b.readOpacity();
    b.last = props;
    b.lastOpacity = style.opacity;
    single->setSymbol(style);
}

/**
 * @brief Write the props an edit changed since the last seed or edit onto the live style.
 * Connected to Rule::rendererReplaced; a symbol of another kind or renderer writes nothing.
 */
inline void applyRuleEdit(RuleStyleBinding &b)
{
    const auto *single =
        b.rule ? dynamic_cast<const SingleSymbolRenderer *>(b.rule->renderer()) : nullptr;
    if (b.applying || !single || single->symbol().layers.isEmpty() ||
        single->symbol().layers.first().kind != b.kind)
        return;
    // Copies: a write can re-enter through the style's signals.
    const QVariantMap props = single->symbol().layers.first().props;
    const qreal opacity = single->symbol().opacity;
    b.applying = true;
    for (auto it = props.cbegin(); it != props.cend(); ++it)
        if (b.last.value(it.key()) != it.value())
            b.write(it.key(), it.value());
    if (b.writeOpacity && !qFuzzyCompare(1.0 + opacity, 1.0 + b.lastOpacity))
        b.writeOpacity(opacity);
    b.applying = false;
    b.last = props;
    b.lastOpacity = opacity;
}

// ── Prop maps of the shared 2D decoration styles ────────────────────────
// Keys as the dock's SymbolStyleAdapters write them, plus the specs' own keys
// (rastersymbollayers.cpp) where they differ. Writers ignore unknown keys.

/** @return The contour band style under its adapter keys. */
inline QVariantMap contourBandProps(const ContourBandStyle &st)
{
    return {{QStringLiteral("attribute"), st.attribute()},
            {QStringLiteral("bandCount"), st.bandCount()},
            {QStringLiteral("lowColor"), st.lowColor()},
            {QStringLiteral("highColor"), st.highColor()},
            {QStringLiteral("belowMinColor"), st.belowMinColor()},
            {QStringLiteral("aboveMaxColor"), st.aboveMaxColor()},
            {QStringLiteral("smoothBands"), st.smoothBands()}};
}
/** @brief Write one contour band prop; a picked low/high colour selects the two-colour ramp. */
inline void writeContourBandProp(ContourBandStyle *st, const QString &key, const QVariant &v)
{
    if (!st)
        return;
    if (key == QLatin1String("attribute"))
        st->setAttribute(v.toString());
    else if (key == QLatin1String("bandCount"))
        st->setBandCount(v.toInt());
    else if (key == QLatin1String("lowColor") || key == QLatin1String("highColor")) {
        // A named ramp ignores the gradient ends; picked colours replace it.
        st->setColorRampName(QString());
        key == QLatin1String("lowColor") ? st->setLowColor(v.value<QColor>())
                                         : st->setHighColor(v.value<QColor>());
    } else if (key == QLatin1String("belowMinColor"))
        st->setBelowMinColor(v.value<QColor>());
    else if (key == QLatin1String("aboveMaxColor"))
        st->setAboveMaxColor(v.value<QColor>());
    else if (key == QLatin1String("smoothBands"))
        st->setSmoothBands(v.toBool());
}

/** @return The isoline style under its adapter keys and the contour spec's aliases. */
inline QVariantMap isolineProps(const IsolineStyle &st)
{
    return {{QStringLiteral("attribute"), st.attribute()},
            {QStringLiteral("isoValueCount"), st.isoValueCount()},
            {QStringLiteral("color"), st.color()},
            {QStringLiteral("lineColor"), st.color()},
            {QStringLiteral("width"), st.lineWidthPx()},
            {QStringLiteral("lineWidthPx"), st.lineWidthPx()},
            {QStringLiteral("penStyle"), int(st.dashPattern())},
            {QStringLiteral("labels"), st.labels()}};
}
/** @brief Write one isoline prop. */
inline void writeIsolineProp(IsolineStyle *st, const QString &key, const QVariant &v)
{
    if (!st)
        return;
    if (key == QLatin1String("attribute"))
        st->setAttribute(v.toString());
    else if (key == QLatin1String("isoValueCount"))
        st->setIsoValueCount(v.toInt());
    else if (key == QLatin1String("color") || key == QLatin1String("lineColor"))
        st->setColor(v.value<QColor>());
    else if (key == QLatin1String("width") || key == QLatin1String("lineWidthPx"))
        st->setLineWidthPx(v.toDouble());
    else if (key == QLatin1String("penStyle"))
        st->setDashPattern(static_cast<Qt::PenStyle>(v.toInt()));
    else if (key == QLatin1String("labels"))
        st->setLabels(v.toBool());
    else if (key == QLatin1String("labelEveryN"))
        st->setLabels(v.toInt() > 0);
}

/** @return The mesh edge style under its adapter keys. */
inline QVariantMap meshEdgeProps(const MeshEdgeStyle &st)
{
    return {{QStringLiteral("color"), st.color()},
            {QStringLiteral("width"), st.lineWidthPx()},
            {QStringLiteral("penStyle"), int(st.dashPattern())},
            {QStringLiteral("useSlopeDrivenWidth"), st.useSlopeDrivenWidth()},
            {QStringLiteral("slopeBreak"), st.slopeBreak()},
            {QStringLiteral("wideWidthPx"), st.wideWidthPx()},
            {QStringLiteral("wideColor"), st.wideColor()}};
}
/** @brief Write one mesh edge prop. */
inline void writeMeshEdgeProp(MeshEdgeStyle *st, const QString &key, const QVariant &v)
{
    if (!st)
        return;
    if (key == QLatin1String("color"))
        st->setColor(v.value<QColor>());
    else if (key == QLatin1String("width"))
        st->setLineWidthPx(v.toDouble());
    else if (key == QLatin1String("penStyle"))
        st->setDashPattern(static_cast<Qt::PenStyle>(v.toInt()));
    else if (key == QLatin1String("useSlopeDrivenWidth"))
        st->setUseSlopeDrivenWidth(v.toBool());
    else if (key == QLatin1String("slopeBreak"))
        st->setSlopeBreak(v.toDouble());
    else if (key == QLatin1String("wideWidthPx"))
        st->setWideWidthPx(v.toDouble());
    else if (key == QLatin1String("wideColor"))
        st->setWideColor(v.value<QColor>());
}

/** @return The mesh vertex style under the marker keys. */
inline QVariantMap meshNodeProps(const MeshNodeStyle &st)
{
    return {{QStringLiteral("fillColor"), st.color()},
            {QStringLiteral("size"), st.markerSizePx()},
            {QStringLiteral("shape"), int(st.shape())},
            {QStringLiteral("outlineColor"), st.outlineColor()},
            {QStringLiteral("outlineWidth"), st.outlineWidthPx()}};
}
/** @brief Write one mesh vertex prop; shapes past Diamond draw as a circle. */
inline void writeMeshNodeProp(MeshNodeStyle *st, const QString &key, const QVariant &v)
{
    if (!st)
        return;
    if (key == QLatin1String("fillColor"))
        st->setColor(v.value<QColor>());
    else if (key == QLatin1String("size"))
        st->setMarkerSizePx(v.toDouble());
    else if (key == QLatin1String("shape")) {
        const int shape = v.toInt();
        st->setShape(static_cast<MeshNodeStyle::MarkerShape>(shape >= 0 && shape <= 3 ? shape : 0));
    } else if (key == QLatin1String("outlineColor"))
        st->setOutlineColor(v.value<QColor>());
    else if (key == QLatin1String("outlineWidth"))
        st->setOutlineWidthPx(v.toDouble());
}

} // namespace OpenSWMM::Render
