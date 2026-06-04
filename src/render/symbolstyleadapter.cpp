/*!
 * \file   symbolstyleadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  SymbolStyleAdapter implementation (Slice B.6c) + archetype-aware
 *         siblings (Slice SS.1).
 */

#include "render/symbolstyleadapter.h"

#include "render/ifeaturerenderer.h"
#include "render/markershape.h"   // X1 dialog-read — markerShapeFromString
#include "render/renderers/singlesymbolrenderer.h"
#include "render/rule.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

#include <QMetaType>

#include <algorithm>

namespace OpenSWMM::Render
{

namespace {

// ── Free helpers shared by the legacy adapter and the SS.1 siblings ──

/*! Try to get a writable pointer to the Rule's underlying
 *  SingleSymbolRenderer. Returns null when the Rule's renderer isn't a
 *  SingleSymbol (e.g. user already switched to Graduated). */
SingleSymbolRenderer *singleRendererOf(Rule *rule)
{
    if (!rule) return nullptr;
    return dynamic_cast<SingleSymbolRenderer *>(rule->renderer());
}

const SingleSymbolRenderer *singleRendererOf(const Rule *rule)
{
    if (!rule) return nullptr;
    return dynamic_cast<const SingleSymbolRenderer *>(rule->renderer());
}

/*! Return the first SymbolLayer in the Rule's SingleSymbolRenderer's
 *  symbol, or nullptr when the Rule isn't a SingleSymbol or the symbol
 *  has no layers. */
const SymbolLayer *firstLayerOf(const Rule *rule)
{
    const SingleSymbolRenderer *s = singleRendererOf(rule);
    if (!s) return nullptr;
    const SymbolStyle &style = s->symbol();
    return style.layers.isEmpty() ? nullptr : &style.layers.first();
}

/*! Read a typed prop from the Rule's first SymbolLayer. */
template <typename T>
T readPropOn(const Rule *rule, const QString &key, const T &fallback)
{
    const SymbolLayer *layer = firstLayerOf(rule);
    if (!layer) return fallback;
    const auto it = layer->props.constFind(key);
    if (it == layer->props.constEnd()) return fallback;
    if (!it.value().canConvert<T>()) return fallback;
    return it.value().value<T>();
}

/*! Tolerant colour read for the properties dialog. A SymbolLayer colour prop
 *  may be stored as a QColor variant (the *SymbolStyleAdapter write path) OR a
 *  "#AARRGGBB" hex string (the struct-regen path styleFromElementSymbol), and
 *  the fill may live under either "fillColor" (typed-spec path) or "color"
 *  (struct-regen path). value<QColor>() does NOT parse a hex QString in this
 *  codebase, so a plain read returned an invalid colour and the dialog opened
 *  showing defaults. This reader accepts both encodings and an optional
 *  secondary key. */
QColor readColorOn(const Rule *rule, const QString &key,
                   const QColor &fallback, const QString &altKey = QString())
{
    const SymbolLayer *layer = firstLayerOf(rule);
    if (!layer) return fallback;
    auto tryKey = [&](const QString &k, QColor &out) -> bool {
        const auto it = layer->props.constFind(k);
        if (it == layer->props.constEnd()) return false;
        QColor c = it.value().value<QColor>();              // QColor variant
        if (!c.isValid()) c = QColor(it.value().toString()); // hex string
        if (c.isValid()) { out = c; return true; }
        return false;
    };
    QColor c;
    if (tryKey(key, c)) return c;
    if (!altKey.isEmpty() && tryKey(altKey, c)) return c;
    return fallback;
}

/*! Tolerant marker-shape read: the typed-spec path stores "shape" as an int,
 *  the struct-regen path stores it as a token string (markerShapeToString).
 *  Accept both. */
int readShapeOn(const Rule *rule, int fallback)
{
    const SymbolLayer *layer = firstLayerOf(rule);
    if (!layer) return fallback;
    const auto it = layer->props.constFind(QStringLiteral("shape"));
    if (it == layer->props.constEnd()) return fallback;
    const QVariant &v = it.value();
    if (v.typeId() == QMetaType::QString)
        return static_cast<int>(
            OpenSWMM::Render::markerShapeFromString(v.toString()));
    bool ok = false;
    const int i = v.toInt(&ok);
    return ok ? i : fallback;
}

/*! Write a typed prop into the Rule's first SymbolLayer. Creates a
 *  layer entry if the symbol has none yet. Returns true when the value
 *  actually changed (so callers know whether to fire notifyRendererStateChanged). */
template <typename T>
bool writePropOn(Rule *rule, const QString &key, const T &value)
{
    SingleSymbolRenderer *s = singleRendererOf(rule);
    if (!s) return false;
    SymbolStyle style = s->symbol();
    if (style.layers.isEmpty())
        style.layers.append(SymbolLayer{});
    QVariant currentVar = style.layers.first().props.value(key);
    if (currentVar.isValid() && currentVar.canConvert<T>()
        && currentVar.value<T>() == value)
        return false;
    style.layers.first().props.insert(key, QVariant::fromValue(value));
    s->setSymbol(style);
    rule->notifyRendererStateChanged();
    return true;
}

/*! Slice SS.1 — map a SymbolLayerKind to its archetype bucket. Drives
 *  the SymbolStyleAdapter::createFor factory.
 *
 *  RasterColorRamp / Hillshade / Contour / MeshEdge / MeshNode have no
 *  single-symbol panel in the SWMM 1D / results context — they're
 *  edited via the dedicated raster / mesh decoration panels. The factory
 *  falls back to the generic SymbolStyleAdapter for those. */
enum class Archetype { Point, Line, Polygon, Other };

Archetype archetypeFor(SymbolLayerKind kind)
{
    switch (kind) {
    case SymbolLayerKind::SimpleMarker:
    case SymbolLayerKind::SvgMarker:
    case SymbolLayerKind::FontMarker:
    case SymbolLayerKind::MeshNode:
        return Archetype::Point;
    case SymbolLayerKind::SimpleLine:
    case SymbolLayerKind::MarkerLine:
    case SymbolLayerKind::MeshEdge:
        return Archetype::Line;
    case SymbolLayerKind::SimpleFill:
    case SymbolLayerKind::HatchFill:
    case SymbolLayerKind::PatternFill:
        return Archetype::Polygon;
    case SymbolLayerKind::RasterColorRamp:
    case SymbolLayerKind::Hillshade:
    case SymbolLayerKind::Contour:
    case SymbolLayerKind::VectorGlyph:  // Slice AN.1 — falls through
                                        // because the factory's earlier
                                        // explicit switch already returns
                                        // VelocityVectorSymbolStyleAdapter
                                        // before reaching archetypeFor.
        return Archetype::Other;
    }
    return Archetype::Other;
}

// Canonical prop keys reused by the archetype adapters. Keys for fill
// / outline / marker / line mirror MarkerSymbolLayerSpec (Z.4) and
// LineSymbolLayerSpec (Z.5). Label and fill-opacity keys are introduced
// here for the Single Symbol panel; SS.3 promotes them into the typed
// specs.
//
// Slice AN.2 — additional raster / mesh / glyph keys reused by the
// 2D-results archetype adapters. Keys mirror the canonical names from
// the matching XxxSymbolLayerSpec where possible (see
// rastersymbollayers.h §§Color ramp / Contour / Mesh edge / Mesh node /
// Velocity vector). New keys ("attribute", "minValue", "maxValue",
// "useLogScale", "belowMinColor", "aboveMaxColor", "bandCount",
// "isoValueCount", "labels", "highlightTagged", "taggedColor",
// "taggedSizePx", "hillshadeStrength", "useElevationRamp") are
// introduced here; AN.5 promotes them into the typed specs.
namespace keys {
constexpr auto kFillColor    = "fillColor";
constexpr auto kFillOpacity  = "fillOpacity";
constexpr auto kOutlineColor = "outlineColor";
constexpr auto kOutlineWidth = "outlineWidth";
constexpr auto kSize         = "size";
constexpr auto kShape        = "shape";
constexpr auto kLineColor    = "color";
constexpr auto kLineWidth    = "width";
constexpr auto kPenStyle     = "penStyle";
constexpr auto kOffsetPx     = "offsetPx";
constexpr auto kDrawArrows   = "drawArrows";
constexpr auto kArrowColor   = "arrowColor";
constexpr auto kArrowLength  = "arrowLengthPx";
constexpr auto kArrowOnlyPos = "arrowOnlyWhenFlowPos";
constexpr auto kShowLabel    = "showLabel";
constexpr auto kLabelFont    = "labelFont";
constexpr auto kLabelColor   = "labelColor";
// AN.2 — raster color ramp / contour / mesh / velocity.
constexpr auto kOpacity            = "opacity";
constexpr auto kAttribute          = "attribute";
constexpr auto kMinValue           = "minValue";
constexpr auto kMaxValue           = "maxValue";
constexpr auto kLowColor           = "lowColor";
constexpr auto kHighColor          = "highColor";
constexpr auto kBelowMinColor      = "belowMinColor";
constexpr auto kAboveMaxColor      = "aboveMaxColor";
constexpr auto kUseLogScale        = "useLogScale";
constexpr auto kHillshadeStrength  = "hillshadeStrength";
constexpr auto kUseElevationRamp   = "useElevationRamp";
constexpr auto kBandCount          = "bandCount";
constexpr auto kSmoothBands        = "smoothBands";
constexpr auto kIsoValueCount      = "isoValueCount";
constexpr auto kLabels             = "labels";
constexpr auto kContourMode        = "mode";
constexpr auto kUseSlopeDriven     = "useSlopeDrivenWidth";
constexpr auto kSlopeBreak         = "slopeBreak";
constexpr auto kWideWidthPx        = "wideWidthPx";
constexpr auto kWideColor          = "wideColor";
constexpr auto kHighlightTagged    = "highlightTagged";
constexpr auto kTaggedColor        = "taggedColor";
constexpr auto kTaggedSizePx       = "taggedSizePx";
// Velocity vector glyphs (AN.1).
constexpr auto kGlyphScale         = "glyphLengthScalePxPerMps";
constexpr auto kGlyphMin           = "glyphLengthMinPx";
constexpr auto kGlyphMax           = "glyphLengthMaxPx";
constexpr auto kGlyphSpacing       = "glyphSpacingPx";
constexpr auto kHeadSize           = "headSizePx";
constexpr auto kDryDepth           = "dryDepthCutoff";
} // namespace keys

// Slice AN.2 — opacity write helper shared by every adapter. Same shape
// as the legacy SymbolStyleAdapter::setOpacity but free-function so the
// raster adapters can call it without a base-class dependency.
void writeOpacityOn(Rule *rule, qreal v)
{
    SingleSymbolRenderer *s = singleRendererOf(rule);
    if (!s) return;
    const qreal clamped = std::clamp(v, 0.0, 1.0);
    SymbolStyle style = s->symbol();
    if (qFuzzyCompare(style.opacity + 1.0, clamped + 1.0)) return;
    style.opacity = clamped;
    s->setSymbol(style);
    rule->notifyRendererStateChanged();
}

qreal readOpacityOn(const Rule *rule)
{
    const SingleSymbolRenderer *s = singleRendererOf(rule);
    return s ? s->symbol().opacity : 1.0;
}

} // namespace

// ===========================================================================
// SymbolStyleAdapter — original Slice B.6c implementation (unchanged).
// ===========================================================================

SymbolStyleAdapter::SymbolStyleAdapter(Rule *rule, QObject *parent)
    : QObject(parent), m_rule(rule)
{
    if (m_rule) {
        QObject::connect(m_rule, &Rule::rendererReplaced,
                         this, &SymbolStyleAdapter::onRendererReplaced);
    }
}

SymbolStyleAdapter::~SymbolStyleAdapter() = default;

void SymbolStyleAdapter::onRendererReplaced()
{
    // External swap (SymbologyTab class change) — refresh the adapter
    // view so editors re-read.
    emit changed();
}

// ── opacity ─────────────────────────────────────────────────────────

qreal SymbolStyleAdapter::opacity() const
{
    const SingleSymbolRenderer *s = singleRendererOf(m_rule);
    return s ? s->symbol().opacity : 1.0;
}

void SymbolStyleAdapter::setOpacity(qreal v)
{
    SingleSymbolRenderer *s = singleRendererOf(m_rule);
    if (!s) return;
    const qreal clamped = std::clamp(v, 0.0, 1.0);
    SymbolStyle style = s->symbol();
    if (qFuzzyCompare(style.opacity + 1.0, clamped + 1.0))
        return;
    style.opacity = clamped;
    s->setSymbol(style);
    m_rule->notifyRendererStateChanged();
}

// ── prop read / write helpers ───────────────────────────────────────

template <typename T>
T SymbolStyleAdapter::readProp(const QString &key, const T &fallback) const
{
    return readPropOn<T>(m_rule, key, fallback);
}

template <typename T>
bool SymbolStyleAdapter::writeProp(const QString &key, const T &value)
{
    return writePropOn<T>(m_rule, key, value);
}

// Explicit instantiations for the types this header uses.
template QColor SymbolStyleAdapter::readProp<QColor>(const QString &, const QColor &) const;
template qreal  SymbolStyleAdapter::readProp<qreal>(const QString &, const qreal &) const;
template int    SymbolStyleAdapter::readProp<int>(const QString &, const int &) const;

template bool SymbolStyleAdapter::writeProp<QColor>(const QString &, const QColor &);
template bool SymbolStyleAdapter::writeProp<qreal>(const QString &, const qreal &);
template bool SymbolStyleAdapter::writeProp<int>(const QString &, const int &);

// ── Q_PROPERTY accessors ────────────────────────────────────────────

QColor SymbolStyleAdapter::fillColor() const
{
    return readProp<QColor>(QStringLiteral("fillColor"), QColor());
}

void SymbolStyleAdapter::setFillColor(const QColor &c)
{
    writeProp<QColor>(QStringLiteral("fillColor"), c);
}

QColor SymbolStyleAdapter::strokeColor() const
{
    // Lines store stroke as "color"; markers/fills store as "outlineColor".
    QColor c = readProp<QColor>(QStringLiteral("color"), QColor());
    if (!c.isValid())
        c = readProp<QColor>(QStringLiteral("outlineColor"), QColor());
    return c;
}

void SymbolStyleAdapter::setStrokeColor(const QColor &c)
{
    // Archetype-agnostic: write both canonical keys so whichever the
    // renderer's first layer uses gets updated.
    const bool a = writeProp<QColor>(QStringLiteral("color"), c);
    const bool b = writeProp<QColor>(QStringLiteral("outlineColor"), c);
    if (!a && !b) {
        // Nothing changed (neither key existed differently) — still
        // ensure both are set so subsequent reads find a valid colour.
        writeProp<QColor>(QStringLiteral("color"), c);
    }
}

qreal SymbolStyleAdapter::strokeWidth() const
{
    qreal w = readProp<qreal>(QStringLiteral("width"), -1.0);
    if (w < 0.0)
        w = readProp<qreal>(QStringLiteral("outlineWidth"), 0.0);
    return w;
}

void SymbolStyleAdapter::setStrokeWidth(qreal v)
{
    writeProp<qreal>(QStringLiteral("width"), v);
    writeProp<qreal>(QStringLiteral("outlineWidth"), v);
}

qreal SymbolStyleAdapter::markerSize() const
{
    return readProp<qreal>(QStringLiteral("size"), 0.0);
}

void SymbolStyleAdapter::setMarkerSize(qreal v)
{
    writeProp<qreal>(QStringLiteral("size"), v);
}

MarkerShape SymbolStyleAdapter::markerShape() const
{
    return static_cast<MarkerShape>(
        readProp<int>(QStringLiteral("shape"),
                      static_cast<int>(MarkerShape::Circle)));
}

void SymbolStyleAdapter::setMarkerShape(MarkerShape v)
{
    writeProp<int>(QStringLiteral("shape"), static_cast<int>(v));
}

// ===========================================================================
// Slice SS.1 — factory
// ===========================================================================

QObject *SymbolStyleAdapter::createFor(Rule *rule, QObject *parent)
{
    const SymbolLayer *layer = firstLayerOf(rule);
    if (!layer)
        return new SymbolStyleAdapter(rule, parent);

    // Slice AN.2 — raster / mesh / glyph kinds get their own archetype
    // adapter so the dialog surfaces the full Q_PROPERTY set of the
    // matching legacy style class.
    switch (layer->kind) {
    case SymbolLayerKind::RasterColorRamp:
        return new RasterColorRampSymbolStyleAdapter(rule, parent);
    case SymbolLayerKind::Hillshade:
        return new HillshadeSymbolStyleAdapter(rule, parent);
    case SymbolLayerKind::Contour: {
        // ContourBand vs Isoline share SymbolLayerKind::Contour; the
        // "mode" prop (ContourMode int: Lines=0, Filled=1, Both=2)
        // disambiguates. Default to Lines when the prop is absent.
        const int mode = layer->props.value(
            QLatin1String(keys::kContourMode), 0).toInt();
        if (mode == 0)  // Lines
            return new IsolineSymbolStyleAdapter(rule, parent);
        return new ContourBandSymbolStyleAdapter(rule, parent);
    }
    case SymbolLayerKind::MeshEdge:
        return new MeshEdgeSymbolStyleAdapter(rule, parent);
    case SymbolLayerKind::MeshNode:
        return new MeshNodeSymbolStyleAdapter(rule, parent);
    case SymbolLayerKind::VectorGlyph:
        return new VelocityVectorSymbolStyleAdapter(rule, parent);
    default:
        break;
    }

    switch (archetypeFor(layer->kind)) {
    case Archetype::Point:   return new PointSymbolStyleAdapter(rule, parent);
    case Archetype::Line:    return new LineSymbolStyleAdapter(rule, parent);
    case Archetype::Polygon: return new PolygonSymbolStyleAdapter(rule, parent);
    case Archetype::Other:   break;
    }
    return new SymbolStyleAdapter(rule, parent);
}

// ===========================================================================
// PointSymbolStyleAdapter — point archetype Single Symbol surface.
// ===========================================================================

PointSymbolStyleAdapter::PointSymbolStyleAdapter(Rule *rule, QObject *parent)
    : QObject(parent), m_rule(rule)
{
    if (m_rule) {
        QObject::connect(m_rule, &Rule::rendererReplaced,
                         this, &PointSymbolStyleAdapter::onRendererReplaced);
    }
}

PointSymbolStyleAdapter::~PointSymbolStyleAdapter() = default;

void PointSymbolStyleAdapter::onRendererReplaced() { emit changed(); }

qreal PointSymbolStyleAdapter::opacity() const
{
    const SingleSymbolRenderer *s = singleRendererOf(m_rule);
    return s ? s->symbol().opacity : 1.0;
}

void PointSymbolStyleAdapter::setOpacity(qreal v)
{
    SingleSymbolRenderer *s = singleRendererOf(m_rule);
    if (!s) return;
    const qreal clamped = std::clamp(v, 0.0, 1.0);
    SymbolStyle style = s->symbol();
    if (qFuzzyCompare(style.opacity + 1.0, clamped + 1.0)) return;
    style.opacity = clamped;
    s->setSymbol(style);
    m_rule->notifyRendererStateChanged();
}

MarkerShape PointSymbolStyleAdapter::markerShape() const
{
    // X1 dialog-read fix — "shape" may be an int (typed-spec) or a token
    // string (struct-regen path). readShapeOn accepts both.
    return static_cast<MarkerShape>(
        readShapeOn(m_rule, static_cast<int>(MarkerShape::Circle)));
}

void PointSymbolStyleAdapter::setMarkerShape(MarkerShape v)
{
    writePropOn<int>(m_rule, QLatin1String(keys::kShape), static_cast<int>(v));
}

qreal PointSymbolStyleAdapter::markerSize() const
{
    return readPropOn<qreal>(m_rule, QLatin1String(keys::kSize), 8.0);
}

void PointSymbolStyleAdapter::setMarkerSize(qreal v)
{
    writePropOn<qreal>(m_rule, QLatin1String(keys::kSize), v);
}

QColor PointSymbolStyleAdapter::fillColor() const
{
    // X1 dialog-read fix — accept "fillColor" (typed-spec) OR "color"
    // (struct-regen), QColor variant OR hex string.
    return readColorOn(m_rule, QLatin1String(keys::kFillColor), QColor(),
                       QStringLiteral("color"));
}

void PointSymbolStyleAdapter::setFillColor(const QColor &c)
{
    writePropOn<QColor>(m_rule, QLatin1String(keys::kFillColor), c);
}

QColor PointSymbolStyleAdapter::outlineColor() const
{
    return readColorOn(m_rule, QLatin1String(keys::kOutlineColor), QColor());
}

void PointSymbolStyleAdapter::setOutlineColor(const QColor &c)
{
    writePropOn<QColor>(m_rule, QLatin1String(keys::kOutlineColor), c);
}

qreal PointSymbolStyleAdapter::outlineWidth() const
{
    return readPropOn<qreal>(m_rule, QLatin1String(keys::kOutlineWidth), 0.5);
}

void PointSymbolStyleAdapter::setOutlineWidth(qreal v)
{
    writePropOn<qreal>(m_rule, QLatin1String(keys::kOutlineWidth), v);
}

bool PointSymbolStyleAdapter::showLabel() const
{
    return readPropOn<bool>(m_rule, QLatin1String(keys::kShowLabel), false);
}

void PointSymbolStyleAdapter::setShowLabel(bool v)
{
    writePropOn<bool>(m_rule, QLatin1String(keys::kShowLabel), v);
}

QFont PointSymbolStyleAdapter::labelFont() const
{
    return readPropOn<QFont>(m_rule, QLatin1String(keys::kLabelFont), QFont());
}

void PointSymbolStyleAdapter::setLabelFont(const QFont &f)
{
    writePropOn<QFont>(m_rule, QLatin1String(keys::kLabelFont), f);
}

QColor PointSymbolStyleAdapter::labelColor() const
{
    return readColorOn(m_rule, QLatin1String(keys::kLabelColor), QColor(Qt::black));
}

void PointSymbolStyleAdapter::setLabelColor(const QColor &c)
{
    writePropOn<QColor>(m_rule, QLatin1String(keys::kLabelColor), c);
}

// ===========================================================================
// LineSymbolStyleAdapter — line archetype Single Symbol surface.
// ===========================================================================

LineSymbolStyleAdapter::LineSymbolStyleAdapter(Rule *rule, QObject *parent)
    : QObject(parent), m_rule(rule)
{
    if (m_rule) {
        QObject::connect(m_rule, &Rule::rendererReplaced,
                         this, &LineSymbolStyleAdapter::onRendererReplaced);
    }
}

LineSymbolStyleAdapter::~LineSymbolStyleAdapter() = default;

void LineSymbolStyleAdapter::onRendererReplaced() { emit changed(); }

qreal LineSymbolStyleAdapter::opacity() const
{
    const SingleSymbolRenderer *s = singleRendererOf(m_rule);
    return s ? s->symbol().opacity : 1.0;
}

void LineSymbolStyleAdapter::setOpacity(qreal v)
{
    SingleSymbolRenderer *s = singleRendererOf(m_rule);
    if (!s) return;
    const qreal clamped = std::clamp(v, 0.0, 1.0);
    SymbolStyle style = s->symbol();
    if (qFuzzyCompare(style.opacity + 1.0, clamped + 1.0)) return;
    style.opacity = clamped;
    s->setSymbol(style);
    m_rule->notifyRendererStateChanged();
}

QColor LineSymbolStyleAdapter::lineColor() const
{
    return readColorOn(m_rule, QLatin1String(keys::kLineColor), QColor());
}

void LineSymbolStyleAdapter::setLineColor(const QColor &c)
{
    writePropOn<QColor>(m_rule, QLatin1String(keys::kLineColor), c);
}

qreal LineSymbolStyleAdapter::lineWidth() const
{
    return readPropOn<qreal>(m_rule, QLatin1String(keys::kLineWidth), 1.0);
}

void LineSymbolStyleAdapter::setLineWidth(qreal v)
{
    writePropOn<qreal>(m_rule, QLatin1String(keys::kLineWidth), v);
}

Qt::PenStyle LineSymbolStyleAdapter::dashPattern() const
{
    return static_cast<Qt::PenStyle>(readPropOn<int>(
        m_rule, QLatin1String(keys::kPenStyle), static_cast<int>(Qt::SolidLine)));
}

void LineSymbolStyleAdapter::setDashPattern(Qt::PenStyle s)
{
    writePropOn<int>(m_rule, QLatin1String(keys::kPenStyle), static_cast<int>(s));
}

qreal LineSymbolStyleAdapter::offsetPx() const
{
    return readPropOn<qreal>(m_rule, QLatin1String(keys::kOffsetPx), 0.0);
}

void LineSymbolStyleAdapter::setOffsetPx(qreal v)
{
    writePropOn<qreal>(m_rule, QLatin1String(keys::kOffsetPx), v);
}

bool LineSymbolStyleAdapter::showLabel() const
{
    return readPropOn<bool>(m_rule, QLatin1String(keys::kShowLabel), false);
}

void LineSymbolStyleAdapter::setShowLabel(bool v)
{
    writePropOn<bool>(m_rule, QLatin1String(keys::kShowLabel), v);
}

QFont LineSymbolStyleAdapter::labelFont() const
{
    return readPropOn<QFont>(m_rule, QLatin1String(keys::kLabelFont), QFont());
}

void LineSymbolStyleAdapter::setLabelFont(const QFont &f)
{
    writePropOn<QFont>(m_rule, QLatin1String(keys::kLabelFont), f);
}

QColor LineSymbolStyleAdapter::labelColor() const
{
    return readColorOn(m_rule, QLatin1String(keys::kLabelColor), QColor(Qt::black));
}

void LineSymbolStyleAdapter::setLabelColor(const QColor &c)
{
    writePropOn<QColor>(m_rule, QLatin1String(keys::kLabelColor), c);
}

bool LineSymbolStyleAdapter::showArrows() const
{
    return readPropOn<bool>(m_rule, QLatin1String(keys::kDrawArrows), false);
}

void LineSymbolStyleAdapter::setShowArrows(bool v)
{
    writePropOn<bool>(m_rule, QLatin1String(keys::kDrawArrows), v);
}

qreal LineSymbolStyleAdapter::arrowSize() const
{
    return readPropOn<qreal>(m_rule, QLatin1String(keys::kArrowLength), 10.0);
}

void LineSymbolStyleAdapter::setArrowSize(qreal v)
{
    writePropOn<qreal>(m_rule, QLatin1String(keys::kArrowLength), v);
}

QColor LineSymbolStyleAdapter::arrowColor() const
{
    return readColorOn(m_rule, QLatin1String(keys::kArrowColor),
                       QColor(34, 34, 34));
}

void LineSymbolStyleAdapter::setArrowColor(const QColor &c)
{
    writePropOn<QColor>(m_rule, QLatin1String(keys::kArrowColor), c);
}

bool LineSymbolStyleAdapter::arrowOnlyWhenFlowPos() const
{
    return readPropOn<bool>(m_rule, QLatin1String(keys::kArrowOnlyPos), false);
}

void LineSymbolStyleAdapter::setArrowOnlyWhenFlowPos(bool v)
{
    writePropOn<bool>(m_rule, QLatin1String(keys::kArrowOnlyPos), v);
}

// ===========================================================================
// PolygonSymbolStyleAdapter — polygon archetype Single Symbol surface.
// ===========================================================================

PolygonSymbolStyleAdapter::PolygonSymbolStyleAdapter(Rule *rule, QObject *parent)
    : QObject(parent), m_rule(rule)
{
    if (m_rule) {
        QObject::connect(m_rule, &Rule::rendererReplaced,
                         this, &PolygonSymbolStyleAdapter::onRendererReplaced);
    }
}

PolygonSymbolStyleAdapter::~PolygonSymbolStyleAdapter() = default;

void PolygonSymbolStyleAdapter::onRendererReplaced() { emit changed(); }

qreal PolygonSymbolStyleAdapter::opacity() const
{
    const SingleSymbolRenderer *s = singleRendererOf(m_rule);
    return s ? s->symbol().opacity : 1.0;
}

void PolygonSymbolStyleAdapter::setOpacity(qreal v)
{
    SingleSymbolRenderer *s = singleRendererOf(m_rule);
    if (!s) return;
    const qreal clamped = std::clamp(v, 0.0, 1.0);
    SymbolStyle style = s->symbol();
    if (qFuzzyCompare(style.opacity + 1.0, clamped + 1.0)) return;
    style.opacity = clamped;
    s->setSymbol(style);
    m_rule->notifyRendererStateChanged();
}

QColor PolygonSymbolStyleAdapter::fillColor() const
{
    return readColorOn(m_rule, QLatin1String(keys::kFillColor), QColor(),
                       QStringLiteral("color"));
}

void PolygonSymbolStyleAdapter::setFillColor(const QColor &c)
{
    writePropOn<QColor>(m_rule, QLatin1String(keys::kFillColor), c);
}

qreal PolygonSymbolStyleAdapter::fillOpacity() const
{
    return readPropOn<qreal>(m_rule, QLatin1String(keys::kFillOpacity), 0.55);
}

void PolygonSymbolStyleAdapter::setFillOpacity(qreal v)
{
    writePropOn<qreal>(m_rule, QLatin1String(keys::kFillOpacity),
                       std::clamp(v, 0.0, 1.0));
}

QColor PolygonSymbolStyleAdapter::outlineColor() const
{
    return readColorOn(m_rule, QLatin1String(keys::kOutlineColor), QColor());
}

void PolygonSymbolStyleAdapter::setOutlineColor(const QColor &c)
{
    writePropOn<QColor>(m_rule, QLatin1String(keys::kOutlineColor), c);
}

qreal PolygonSymbolStyleAdapter::outlineWidth() const
{
    return readPropOn<qreal>(m_rule, QLatin1String(keys::kOutlineWidth), 0.5);
}

void PolygonSymbolStyleAdapter::setOutlineWidth(qreal v)
{
    writePropOn<qreal>(m_rule, QLatin1String(keys::kOutlineWidth), v);
}

bool PolygonSymbolStyleAdapter::showLabel() const
{
    return readPropOn<bool>(m_rule, QLatin1String(keys::kShowLabel), false);
}

void PolygonSymbolStyleAdapter::setShowLabel(bool v)
{
    writePropOn<bool>(m_rule, QLatin1String(keys::kShowLabel), v);
}

QFont PolygonSymbolStyleAdapter::labelFont() const
{
    return readPropOn<QFont>(m_rule, QLatin1String(keys::kLabelFont), QFont());
}

void PolygonSymbolStyleAdapter::setLabelFont(const QFont &f)
{
    writePropOn<QFont>(m_rule, QLatin1String(keys::kLabelFont), f);
}

QColor PolygonSymbolStyleAdapter::labelColor() const
{
    return readPropOn<QColor>(m_rule, QLatin1String(keys::kLabelColor), QColor(Qt::black));
}

void PolygonSymbolStyleAdapter::setLabelColor(const QColor &c)
{
    writePropOn<QColor>(m_rule, QLatin1String(keys::kLabelColor), c);
}

// ===========================================================================
// Slice AN.2 — raster / mesh / glyph adapters (2D results layer).
//
// Each adapter is the same shape as Point/Line/Polygon above: hold a
// Rule*, watch rendererReplaced, read/write canonical prop keys on the
// first SymbolLayer. The set of properties each surfaces matches the
// matching legacy style class (MeshFillStyle / DepthColorRampStyle /
// ContourBandStyle / IsolineStyle / MeshEdgeStyle / MeshNodeStyle /
// VelocityVectorStyle). See docs/RENDERING_2D_RESULTS_STYLING_PLAN.md.
// ===========================================================================

// ── Macro: boilerplate ctor / dtor / opacity / onRendererReplaced ─────
// Shared across the 7 raster adapters. Cuts ~70 lines of repetitive
// member-function bodies. Each adapter calls this once near the top of
// its block.
#define OSWMM_RASTER_ADAPTER_COMMON(ClassName)                             \
    ClassName::ClassName(Rule *rule, QObject *parent)                      \
        : QObject(parent), m_rule(rule)                                    \
    {                                                                      \
        if (m_rule) {                                                      \
            QObject::connect(m_rule, &Rule::rendererReplaced,              \
                             this, &ClassName::onRendererReplaced);        \
        }                                                                  \
    }                                                                      \
    ClassName::~ClassName() = default;                                     \
    void ClassName::onRendererReplaced() { emit changed(); }               \
    qreal ClassName::opacity() const { return readOpacityOn(m_rule); }     \
    void  ClassName::setOpacity(qreal v) { writeOpacityOn(m_rule, v); }

// ── RasterColorRampSymbolStyleAdapter ────────────────────────────────

OSWMM_RASTER_ADAPTER_COMMON(RasterColorRampSymbolStyleAdapter)

QString RasterColorRampSymbolStyleAdapter::attribute() const
{ return readPropOn<QString>(m_rule, QLatin1String(keys::kAttribute), QString()); }
void RasterColorRampSymbolStyleAdapter::setAttribute(const QString &v)
{ writePropOn<QString>(m_rule, QLatin1String(keys::kAttribute), v); }

qreal RasterColorRampSymbolStyleAdapter::minValue() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kMinValue), 0.0); }
void RasterColorRampSymbolStyleAdapter::setMinValue(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kMinValue), v); }

qreal RasterColorRampSymbolStyleAdapter::maxValue() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kMaxValue), 1.0); }
void RasterColorRampSymbolStyleAdapter::setMaxValue(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kMaxValue), v); }

QColor RasterColorRampSymbolStyleAdapter::lowColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kLowColor), QColor(0, 0, 255)); }
void RasterColorRampSymbolStyleAdapter::setLowColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kLowColor), c); }

QColor RasterColorRampSymbolStyleAdapter::highColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kHighColor), QColor(255, 0, 0)); }
void RasterColorRampSymbolStyleAdapter::setHighColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kHighColor), c); }

QColor RasterColorRampSymbolStyleAdapter::belowMinColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kBelowMinColor), QColor(0, 0, 0, 0)); }
void RasterColorRampSymbolStyleAdapter::setBelowMinColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kBelowMinColor), c); }

QColor RasterColorRampSymbolStyleAdapter::aboveMaxColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kAboveMaxColor), QColor(0, 0, 0, 0)); }
void RasterColorRampSymbolStyleAdapter::setAboveMaxColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kAboveMaxColor), c); }

bool RasterColorRampSymbolStyleAdapter::useLogScale() const
{ return readPropOn<bool>(m_rule, QLatin1String(keys::kUseLogScale), false); }
void RasterColorRampSymbolStyleAdapter::setUseLogScale(bool v)
{ writePropOn<bool>(m_rule, QLatin1String(keys::kUseLogScale), v); }

// ── HillshadeSymbolStyleAdapter ──────────────────────────────────────

OSWMM_RASTER_ADAPTER_COMMON(HillshadeSymbolStyleAdapter)

QColor HillshadeSymbolStyleAdapter::fillColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kFillColor), QColor(180, 180, 180)); }
void HillshadeSymbolStyleAdapter::setFillColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kFillColor), c); }

qreal HillshadeSymbolStyleAdapter::hillshadeStrength() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kHillshadeStrength), 0.5); }
void HillshadeSymbolStyleAdapter::setHillshadeStrength(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kHillshadeStrength), v); }

bool HillshadeSymbolStyleAdapter::useElevationRamp() const
{ return readPropOn<bool>(m_rule, QLatin1String(keys::kUseElevationRamp), false); }
void HillshadeSymbolStyleAdapter::setUseElevationRamp(bool v)
{ writePropOn<bool>(m_rule, QLatin1String(keys::kUseElevationRamp), v); }

// ── ContourBandSymbolStyleAdapter ────────────────────────────────────

OSWMM_RASTER_ADAPTER_COMMON(ContourBandSymbolStyleAdapter)

QString ContourBandSymbolStyleAdapter::attribute() const
{ return readPropOn<QString>(m_rule, QLatin1String(keys::kAttribute), QString()); }
void ContourBandSymbolStyleAdapter::setAttribute(const QString &v)
{ writePropOn<QString>(m_rule, QLatin1String(keys::kAttribute), v); }

int ContourBandSymbolStyleAdapter::bandCount() const
{ return readPropOn<int>(m_rule, QLatin1String(keys::kBandCount), 8); }
void ContourBandSymbolStyleAdapter::setBandCount(int v)
{ writePropOn<int>(m_rule, QLatin1String(keys::kBandCount), v); }

QColor ContourBandSymbolStyleAdapter::lowColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kLowColor), QColor(0, 0, 255)); }
void ContourBandSymbolStyleAdapter::setLowColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kLowColor), c); }

QColor ContourBandSymbolStyleAdapter::highColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kHighColor), QColor(255, 0, 0)); }
void ContourBandSymbolStyleAdapter::setHighColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kHighColor), c); }

QColor ContourBandSymbolStyleAdapter::belowMinColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kBelowMinColor), QColor(0, 0, 0, 0)); }
void ContourBandSymbolStyleAdapter::setBelowMinColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kBelowMinColor), c); }

QColor ContourBandSymbolStyleAdapter::aboveMaxColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kAboveMaxColor), QColor(0, 0, 0, 0)); }
void ContourBandSymbolStyleAdapter::setAboveMaxColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kAboveMaxColor), c); }

bool ContourBandSymbolStyleAdapter::smoothBands() const
{ return readPropOn<bool>(m_rule, QLatin1String(keys::kSmoothBands), true); }
void ContourBandSymbolStyleAdapter::setSmoothBands(bool v)
{ writePropOn<bool>(m_rule, QLatin1String(keys::kSmoothBands), v); }

// ── IsolineSymbolStyleAdapter ────────────────────────────────────────

OSWMM_RASTER_ADAPTER_COMMON(IsolineSymbolStyleAdapter)

QString IsolineSymbolStyleAdapter::attribute() const
{ return readPropOn<QString>(m_rule, QLatin1String(keys::kAttribute), QString()); }
void IsolineSymbolStyleAdapter::setAttribute(const QString &v)
{ writePropOn<QString>(m_rule, QLatin1String(keys::kAttribute), v); }

int IsolineSymbolStyleAdapter::isoValueCount() const
{ return readPropOn<int>(m_rule, QLatin1String(keys::kIsoValueCount), 5); }
void IsolineSymbolStyleAdapter::setIsoValueCount(int v)
{ writePropOn<int>(m_rule, QLatin1String(keys::kIsoValueCount), v); }

QColor IsolineSymbolStyleAdapter::color() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kLineColor), QColor(40, 40, 40)); }
void IsolineSymbolStyleAdapter::setColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kLineColor), c); }

qreal IsolineSymbolStyleAdapter::lineWidthPx() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kLineWidth), 0.75); }
void IsolineSymbolStyleAdapter::setLineWidthPx(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kLineWidth), v); }

Qt::PenStyle IsolineSymbolStyleAdapter::dashPattern() const
{ return static_cast<Qt::PenStyle>(readPropOn<int>(
    m_rule, QLatin1String(keys::kPenStyle), static_cast<int>(Qt::SolidLine))); }
void IsolineSymbolStyleAdapter::setDashPattern(Qt::PenStyle s)
{ writePropOn<int>(m_rule, QLatin1String(keys::kPenStyle), static_cast<int>(s)); }

bool IsolineSymbolStyleAdapter::labels() const
{ return readPropOn<bool>(m_rule, QLatin1String(keys::kLabels), false); }
void IsolineSymbolStyleAdapter::setLabels(bool v)
{ writePropOn<bool>(m_rule, QLatin1String(keys::kLabels), v); }

// ── MeshEdgeSymbolStyleAdapter ───────────────────────────────────────

OSWMM_RASTER_ADAPTER_COMMON(MeshEdgeSymbolStyleAdapter)

QColor MeshEdgeSymbolStyleAdapter::color() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kLineColor), QColor(80, 80, 80)); }
void MeshEdgeSymbolStyleAdapter::setColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kLineColor), c); }

qreal MeshEdgeSymbolStyleAdapter::lineWidthPx() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kLineWidth), 0.5); }
void MeshEdgeSymbolStyleAdapter::setLineWidthPx(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kLineWidth), v); }

Qt::PenStyle MeshEdgeSymbolStyleAdapter::dashPattern() const
{ return static_cast<Qt::PenStyle>(readPropOn<int>(
    m_rule, QLatin1String(keys::kPenStyle), static_cast<int>(Qt::SolidLine))); }
void MeshEdgeSymbolStyleAdapter::setDashPattern(Qt::PenStyle s)
{ writePropOn<int>(m_rule, QLatin1String(keys::kPenStyle), static_cast<int>(s)); }

bool MeshEdgeSymbolStyleAdapter::useSlopeDrivenWidth() const
{ return readPropOn<bool>(m_rule, QLatin1String(keys::kUseSlopeDriven), false); }
void MeshEdgeSymbolStyleAdapter::setUseSlopeDrivenWidth(bool v)
{ writePropOn<bool>(m_rule, QLatin1String(keys::kUseSlopeDriven), v); }

qreal MeshEdgeSymbolStyleAdapter::slopeBreak() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kSlopeBreak), 0.5); }
void MeshEdgeSymbolStyleAdapter::setSlopeBreak(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kSlopeBreak), v); }

qreal MeshEdgeSymbolStyleAdapter::wideWidthPx() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kWideWidthPx), 1.5); }
void MeshEdgeSymbolStyleAdapter::setWideWidthPx(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kWideWidthPx), v); }

QColor MeshEdgeSymbolStyleAdapter::wideColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kWideColor), QColor(40, 40, 40)); }
void MeshEdgeSymbolStyleAdapter::setWideColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kWideColor), c); }

// ── MeshNodeSymbolStyleAdapter ───────────────────────────────────────

OSWMM_RASTER_ADAPTER_COMMON(MeshNodeSymbolStyleAdapter)

QColor MeshNodeSymbolStyleAdapter::color() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kFillColor), QColor(60, 120, 200)); }
void MeshNodeSymbolStyleAdapter::setColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kFillColor), c); }

qreal MeshNodeSymbolStyleAdapter::markerSizePx() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kSize), 6.0); }
void MeshNodeSymbolStyleAdapter::setMarkerSizePx(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kSize), v); }

MarkerShape MeshNodeSymbolStyleAdapter::shape() const
{ return static_cast<MarkerShape>(readPropOn<int>(
    m_rule, QLatin1String(keys::kShape), static_cast<int>(MarkerShape::Circle))); }
void MeshNodeSymbolStyleAdapter::setShape(MarkerShape s)
{ writePropOn<int>(m_rule, QLatin1String(keys::kShape), static_cast<int>(s)); }

QColor MeshNodeSymbolStyleAdapter::outlineColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kOutlineColor), QColor(40, 40, 40)); }
void MeshNodeSymbolStyleAdapter::setOutlineColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kOutlineColor), c); }

qreal MeshNodeSymbolStyleAdapter::outlineWidthPx() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kOutlineWidth), 0.5); }
void MeshNodeSymbolStyleAdapter::setOutlineWidthPx(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kOutlineWidth), v); }

bool MeshNodeSymbolStyleAdapter::highlightTagged() const
{ return readPropOn<bool>(m_rule, QLatin1String(keys::kHighlightTagged), false); }
void MeshNodeSymbolStyleAdapter::setHighlightTagged(bool v)
{ writePropOn<bool>(m_rule, QLatin1String(keys::kHighlightTagged), v); }

QColor MeshNodeSymbolStyleAdapter::taggedColor() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kTaggedColor), QColor(255, 140, 0)); }
void MeshNodeSymbolStyleAdapter::setTaggedColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kTaggedColor), c); }

qreal MeshNodeSymbolStyleAdapter::taggedSizePx() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kTaggedSizePx), 8.0); }
void MeshNodeSymbolStyleAdapter::setTaggedSizePx(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kTaggedSizePx), v); }

// ── VelocityVectorSymbolStyleAdapter ─────────────────────────────────

OSWMM_RASTER_ADAPTER_COMMON(VelocityVectorSymbolStyleAdapter)

qreal VelocityVectorSymbolStyleAdapter::glyphLengthScalePxPerMps() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kGlyphScale), 20.0); }
void VelocityVectorSymbolStyleAdapter::setGlyphLengthScalePxPerMps(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kGlyphScale), v); }

qreal VelocityVectorSymbolStyleAdapter::glyphLengthMinPx() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kGlyphMin), 4.0); }
void VelocityVectorSymbolStyleAdapter::setGlyphLengthMinPx(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kGlyphMin), v); }

qreal VelocityVectorSymbolStyleAdapter::glyphLengthMaxPx() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kGlyphMax), 40.0); }
void VelocityVectorSymbolStyleAdapter::setGlyphLengthMaxPx(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kGlyphMax), v); }

qreal VelocityVectorSymbolStyleAdapter::glyphSpacingPx() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kGlyphSpacing), 30.0); }
void VelocityVectorSymbolStyleAdapter::setGlyphSpacingPx(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kGlyphSpacing), v); }

qreal VelocityVectorSymbolStyleAdapter::headSizePx() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kHeadSize), 5.0); }
void VelocityVectorSymbolStyleAdapter::setHeadSizePx(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kHeadSize), v); }

QColor VelocityVectorSymbolStyleAdapter::color() const
{ return readPropOn<QColor>(m_rule, QLatin1String(keys::kLineColor),
                            QColor(20, 20, 20, 220)); }
void VelocityVectorSymbolStyleAdapter::setColor(const QColor &c)
{ writePropOn<QColor>(m_rule, QLatin1String(keys::kLineColor), c); }

qreal VelocityVectorSymbolStyleAdapter::dryDepthCutoff() const
{ return readPropOn<qreal>(m_rule, QLatin1String(keys::kDryDepth), 0.01); }
void VelocityVectorSymbolStyleAdapter::setDryDepthCutoff(qreal v)
{ writePropOn<qreal>(m_rule, QLatin1String(keys::kDryDepth), v); }

#undef OSWMM_RASTER_ADAPTER_COMMON

} // namespace OpenSWMM::Render
