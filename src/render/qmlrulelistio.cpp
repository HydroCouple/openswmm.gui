/**
 * @file      qmlrulelistio.cpp
 * @brief     Cross-tool interop for users who maintain styles in QGIS and want to share them with
 *            SWMMVis (or vice versa).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/qmlrulelistio.h"

#include "render/renderers/singlesymbolrenderer.h"
#include "render/rule.h"
#include "render/rulelist.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

#include <QColor>
#include <QFile>
#include <QObject>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace OpenSWMM::Render::QmlRuleListIO
{

namespace
{

// ── QGIS color encoding ────────────────────────────────────────────────
//
// QGIS writes colors as "r,g,b,a" strings (e.g. "255,128,0,255"). The
// alpha is always present; we follow that convention so the file works
// in QGIS as authored.

/** @brief Encode valid QColor channels as decimal r,g,b,a.
 * @param c Color to serialize without premultiplication.
 * @return Comma-separated bytes, or opaque black for an invalid QColor. */
[[nodiscard]] QString qgisColor(const QColor &c)
{
    if (!c.isValid())
        return QStringLiteral("0,0,0,255");
    return QStringLiteral("%1,%2,%3,%4").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

/** @brief Decode a QGIS comma color or a Qt-recognized color string.
 * @param s Input text; empty returns invalid. Three or more comma components
 * use unchecked toInt(), default alpha 255 and ignore extra components.
 * @return QColor following Qt channel validation; fewer than three components
 * are passed directly to QColor's string parser. Invalid conversions are not diagnosed. */
[[nodiscard]] QColor parseQgisColor(const QString &s)
{
    if (s.isEmpty())
        return {};
    const QStringList parts = s.split(QLatin1Char(','));
    if (parts.size() < 3) {
        // Try hex form. QColor handles "#RRGGBB" and "#AARRGGBB".
        return QColor(s);
    }
    int r = parts.value(0).toInt();
    int g = parts.value(1).toInt();
    int b = parts.value(2).toInt();
    int a = parts.size() >= 4 ? parts.value(3).toInt() : 255;
    return QColor(r, g, b, a);
}

// ── prop helpers ───────────────────────────────────────────────────────

/** @brief Write one XML prop element with escaped k and v attributes.
 * @param[in,out] w Writer currently inside the containing layer element.
 * @param k Property key copied into the k attribute.
 * @param v Property value copied into the v attribute; empty strings are allowed. */
void writeProp(QXmlStreamWriter &w, const QString &k, const QString &v)
{
    w.writeStartElement(QStringLiteral("prop"));
    w.writeAttribute(QStringLiteral("k"), k);
    w.writeAttribute(QStringLiteral("v"), v);
    w.writeEndElement();
}

/** @brief Convert a property to export text, preferring any valid QColor conversion.
 * @param v Borrowed property variant; even a color-like string may convert as color.
 * @return QGIS r,g,b,a for a valid converted color, otherwise QVariant::toString(). */
[[nodiscard]] QString variantToString(const QVariant &v)
{
    // QColor → QGIS "r,g,b,a"; other variants → toString().
    if (v.canConvert<QColor>()) {
        const QColor c = v.value<QColor>();
        if (c.isValid())
            return qgisColor(c);
    }
    return v.toString();
}

// ── symbol-layer kind names ────────────────────────────────────────────
//
// QGIS uses "SimpleMarker", "SimpleLine", "SimpleFill" — these match our
// SymbolLayerKind enum names exactly for the three v1-supported kinds.

/** @brief Map a supported symbol-layer kind to its QGIS class token.
 * @param k Layer kind; only SimpleMarker, SimpleLine and SimpleFill are recognized.
 * @return Corresponding token, or SimpleFill for every other kind without a warning. */
[[nodiscard]] QString qgisLayerKindFor(SymbolLayerKind k)
{
    switch (k) {
    case SymbolLayerKind::SimpleMarker:
        return QStringLiteral("SimpleMarker");
    case SymbolLayerKind::SimpleLine:
        return QStringLiteral("SimpleLine");
    case SymbolLayerKind::SimpleFill:
        return QStringLiteral("SimpleFill");
    default:
        return QStringLiteral("SimpleFill"); // fallback
    }
}

// QGIS expects "marker" / "line" / "fill" as the <symbol> "type" attr
// (different from per-layer "class" attr above). Match against the first
// layer kind in the style.
/** @brief Choose the QGIS symbol type from the first style layer only.
 * @param s Borrowed style; subsequent layers do not influence its type.
 * @return marker, line or fill; empty/unsupported first layers produce marker. */
[[nodiscard]] QString qgisSymbolTypeFor(const SymbolStyle &s)
{
    if (s.layers.isEmpty())
        return QStringLiteral("marker");
    switch (s.layers.first().kind) {
    case SymbolLayerKind::SimpleMarker:
        return QStringLiteral("marker");
    case SymbolLayerKind::SimpleLine:
        return QStringLiteral("line");
    case SymbolLayerKind::SimpleFill:
        return QStringLiteral("fill");
    default:
        return QStringLiteral("marker");
    }
}

// Write one <symbol> element. Caller is responsible for the surrounding
// <symbols> container. \p name is the QGIS symbol id (we use the Rule's
// name when invoked from the multi-Rule path, otherwise "0" for the
// singleSymbol fallback).
/** @brief Write a symbol with selected simple marker/line/fill property mappings.
 * @param[in,out] w Writer currently within the surrounding symbols container.
 * @param name Symbol ID copied verbatim into the name attribute.
 * @param style Borrowed style. Every layer writes an element, but unsupported
 * kinds fall back to an empty SimpleFill. Enabled flags, expressions, shape,
 * rotation and most properties are omitted; overall alpha is always one.
 * Widths/sizes are stringified without numeric unit conversion or range checks. */
void writeQgisSymbol(QXmlStreamWriter &w, const QString &name, const SymbolStyle &style)
{
    w.writeStartElement(QStringLiteral("symbol"));
    w.writeAttribute(QStringLiteral("name"), name);
    w.writeAttribute(QStringLiteral("type"), qgisSymbolTypeFor(style));
    w.writeAttribute(QStringLiteral("alpha"), QStringLiteral("1"));

    for (const SymbolLayer &layer : style.layers) {
        const QString layerKind = qgisLayerKindFor(layer.kind);
        w.writeStartElement(QStringLiteral("layer"));
        w.writeAttribute(QStringLiteral("class"), layerKind);
        w.writeAttribute(QStringLiteral("pass"), QStringLiteral("0"));

        // Map canonical SymbolLayer props → QGIS prop keys. We pick the
        // smallest useful set; missing keys are tolerated by QGIS on
        // import (it fills in defaults).
        if (layer.kind == SymbolLayerKind::SimpleMarker) {
            if (layer.props.contains(QStringLiteral("fillColor")))
                writeProp(w, QStringLiteral("color"),
                          variantToString(layer.props.value(QStringLiteral("fillColor"))));
            if (layer.props.contains(QStringLiteral("outlineColor")))
                writeProp(w, QStringLiteral("outline_color"),
                          variantToString(layer.props.value(QStringLiteral("outlineColor"))));
            if (layer.props.contains(QStringLiteral("outlineWidth")))
                writeProp(w, QStringLiteral("outline_width"),
                          variantToString(layer.props.value(QStringLiteral("outlineWidth"))));
            if (layer.props.contains(QStringLiteral("size")))
                writeProp(w, QStringLiteral("size"),
                          variantToString(layer.props.value(QStringLiteral("size"))));
        } else if (layer.kind == SymbolLayerKind::SimpleLine) {
            if (layer.props.contains(QStringLiteral("color")))
                writeProp(w, QStringLiteral("line_color"),
                          variantToString(layer.props.value(QStringLiteral("color"))));
            if (layer.props.contains(QStringLiteral("width")))
                writeProp(w, QStringLiteral("line_width"),
                          variantToString(layer.props.value(QStringLiteral("width"))));
        } else if (layer.kind == SymbolLayerKind::SimpleFill) {
            if (layer.props.contains(QStringLiteral("fillColor")))
                writeProp(w, QStringLiteral("color"),
                          variantToString(layer.props.value(QStringLiteral("fillColor"))));
            if (layer.props.contains(QStringLiteral("outlineColor")))
                writeProp(w, QStringLiteral("outline_color"),
                          variantToString(layer.props.value(QStringLiteral("outlineColor"))));
            if (layer.props.contains(QStringLiteral("outlineWidth")))
                writeProp(w, QStringLiteral("outline_width"),
                          variantToString(layer.props.value(QStringLiteral("outlineWidth"))));
        }

        w.writeEndElement(); // </layer>
    }
    w.writeEndElement(); // </symbol>
}

// Lift the SingleSymbolRenderer's SymbolStyle out of a Rule. Returns
// empty style when the Rule's renderer is a different class.
/** @brief Copy a rule's concrete SingleSymbolRenderer style when available.
 * @param r Borrowed optional rule; neither rule nor renderer is modified.
 * @return Independent style value, or empty style for null/another renderer type. */
[[nodiscard]] SymbolStyle singleSymbolFor(const Rule *r)
{
    if (!r)
        return {};
    const auto *single = dynamic_cast<const SingleSymbolRenderer *>(r->renderer());
    return single ? single->symbol() : SymbolStyle{};
}

} // namespace

// ---------------------------------------------------------------------------

RuleListIoResult save(const RuleList *list, const QString &path)
{
    RuleListIoResult result;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QObject::tr("Cannot open %1 for writing: %2").arg(path, f.errorString());
        return result;
    }

    QXmlStreamWriter w(&f);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("qgis"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("3.30.0-swmmvis-z17b"));

    const int count = list ? list->count() : 0;

    if (count <= 1) {
        // ── Single-rule path → QGIS singleSymbol renderer ───────────────
        SymbolStyle style;
        if (count == 1)
            style = singleSymbolFor(list->at(0));
        if (style.layers.isEmpty() && count == 1) {
            const auto *r = list->at(0);
            if (r && r->renderer())
                result.warnings.append(
                    QObject::tr("Renderer '%1' isn't exported in full fidelity to .qml — "
                                "wrote a fallback singleSymbol entry. Round-trip via "
                                ".swmm-rule.json preserves everything.")
                        .arg(r->renderer()->rendererId()));
        }
        w.writeStartElement(QStringLiteral("renderer-v2"));
        w.writeAttribute(QStringLiteral("type"), QStringLiteral("singleSymbol"));
        w.writeStartElement(QStringLiteral("symbols"));
        writeQgisSymbol(w, QStringLiteral("0"), style);
        w.writeEndElement(); // </symbols>
        w.writeEndElement(); // </renderer-v2>
    } else {
        // ── Multi-rule path → QGIS RuleRenderer ─────────────────────────
        //
        // QGIS RuleRenderer has a tree of <rule> elements, each
        // referencing a symbol by id. We use the Rule's index as the
        // symbol id so the linkage is trivial.
        w.writeStartElement(QStringLiteral("renderer-v2"));
        w.writeAttribute(QStringLiteral("type"), QStringLiteral("RuleRenderer"));

        w.writeStartElement(QStringLiteral("rules"));
        w.writeAttribute(QStringLiteral("key"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        for (int i = 0; i < count; ++i) {
            const Rule *r = list->at(i);
            if (!r)
                continue;
            w.writeStartElement(QStringLiteral("rule"));
            w.writeAttribute(QStringLiteral("symbol"), QString::number(i));
            w.writeAttribute(QStringLiteral("label"), r->name());
            if (!r->filterExpression().isEmpty())
                w.writeAttribute(QStringLiteral("filter"), r->filterExpression());
            if (r->minScale() > 0.0)
                w.writeAttribute(QStringLiteral("scalemindenom"),
                                 QString::number(r->minScale(), 'f', 0));
            if (r->maxScale() > 0.0)
                w.writeAttribute(QStringLiteral("scalemaxdenom"),
                                 QString::number(r->maxScale(), 'f', 0));
            w.writeAttribute(QStringLiteral("key"),
                             QUuid::createUuid().toString(QUuid::WithoutBraces));
            w.writeEndElement(); // </rule>
        }
        w.writeEndElement(); // </rules>

        w.writeStartElement(QStringLiteral("symbols"));
        for (int i = 0; i < count; ++i) {
            const Rule *r = list->at(i);
            const SymbolStyle style = singleSymbolFor(r);
            if (style.layers.isEmpty() && r && r->renderer())
                result.warnings.append(
                    QObject::tr("Rule '%1' renderer '%2' isn't exported in full fidelity "
                                "to .qml — wrote a fallback symbol.")
                        .arg(r->name(), r->renderer()->rendererId()));
            writeQgisSymbol(w, QString::number(i), style);
        }
        w.writeEndElement(); // </symbols>
        w.writeEndElement(); // </renderer-v2>
    }

    w.writeEndElement(); // </qgis>
    w.writeEndDocument();

    if (f.error() != QFile::NoError) {
        result.error = QObject::tr("I/O error writing %1: %2").arg(path, f.errorString());
        return result;
    }
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------

namespace
{

/*! Read one symbol's nested layer/property entries into a list of
 *  SymbolLayer instances. Walks until the matching symbol end element. */
/** @param[in,out] x Reader positioned on a symbol start element; advanced through
 * its matching end, or until XML failure/end-of-input. The caller checks hasError().
 * @return Collected layers. Unknown classes become SimpleMarker; recognized
 * prop/Option entries require nonempty k/v attributes. Unknown keys are skipped,
 * invalid colors omitted and numeric sizes/widths use unchecked toDouble().
 * Units, enabled state, shape, opacity and expressions are not reconstructed. */
[[nodiscard]] QList<SymbolLayer> readQgisSymbolLayers(QXmlStreamReader &x)
{
    QList<SymbolLayer> layers;
    int depth = 1; // we've already consumed the <symbol> StartElement
    SymbolLayer cur;
    bool inLayer = false;
    while (depth > 0 && !x.atEnd()) {
        const auto t = x.readNext();
        if (t == QXmlStreamReader::StartElement) {
            ++depth;
            const QString name = x.name().toString();
            if (name == QLatin1String("layer")) {
                inLayer = true;
                cur = {};
                const QString cls = x.attributes().value(QStringLiteral("class")).toString();
                if (cls == QLatin1String("SimpleMarker"))
                    cur.kind = SymbolLayerKind::SimpleMarker;
                else if (cls == QLatin1String("SimpleLine"))
                    cur.kind = SymbolLayerKind::SimpleLine;
                else if (cls == QLatin1String("SimpleFill"))
                    cur.kind = SymbolLayerKind::SimpleFill;
                else
                    cur.kind = SymbolLayerKind::SimpleMarker; // best effort
            } else if (inLayer &&
                       (name == QLatin1String("prop") || name == QLatin1String("Option"))) {
                const auto attrs = x.attributes();
                const QString k = attrs.value(QStringLiteral("k")).toString();
                const QString v = attrs.value(QStringLiteral("v")).toString();
                if (k.isEmpty() || v.isEmpty())
                    continue;
                // Map QGIS prop keys → our canonical keys.
                if (k == QLatin1String("color")) {
                    const QColor c = parseQgisColor(v);
                    // For Marker/Fill, our key is "fillColor"; for Line
                    // it's "color".
                    const QString ourKey = (cur.kind == SymbolLayerKind::SimpleLine)
                                               ? QStringLiteral("color")
                                               : QStringLiteral("fillColor");
                    if (c.isValid())
                        cur.props.insert(ourKey, c);
                } else if (k == QLatin1String("line_color")) {
                    const QColor c = parseQgisColor(v);
                    if (c.isValid())
                        cur.props.insert(QStringLiteral("color"), c);
                } else if (k == QLatin1String("outline_color")) {
                    const QColor c = parseQgisColor(v);
                    if (c.isValid())
                        cur.props.insert(QStringLiteral("outlineColor"), c);
                } else if (k == QLatin1String("line_width")) {
                    cur.props.insert(QStringLiteral("width"), v.toDouble());
                } else if (k == QLatin1String("outline_width")) {
                    cur.props.insert(QStringLiteral("outlineWidth"), v.toDouble());
                } else if (k == QLatin1String("size")) {
                    cur.props.insert(QStringLiteral("size"), v.toDouble());
                }
            }
        } else if (t == QXmlStreamReader::EndElement) {
            --depth;
            if (x.name() == QLatin1String("layer") && inLayer) {
                layers.append(cur);
                inLayer = false;
            }
        }
    }
    return layers;
}

} // namespace

// ---------------------------------------------------------------------------

RuleListIoResult load(const QString &path, RuleList *list)
{
    RuleListIoResult result;
    if (!list) {
        result.error = QObject::tr("RuleList target is null.");
        return result;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        result.error = QObject::tr("Cannot open %1: %2").arg(path, f.errorString());
        return result;
    }
    QXmlStreamReader x(&f);

    // Pass 1: collect <rule> entries (RuleRenderer only) and <symbol>
    // entries (any renderer). singleSymbol's lone <symbol> ends up at
    // index 0 of symbolsByName via its "name" attribute.
    struct RuleEntry {
        QString symbol; // symbol-name reference
        QString label;
        QString filter;
        double minScale = 0.0;
        double maxScale = 0.0;
    };
    QList<RuleEntry> ruleEntries;
    QHash<QString, QList<SymbolLayer>> symbolsByName;
    QString rendererType;

    while (!x.atEnd()) {
        const auto t = x.readNext();
        if (t != QXmlStreamReader::StartElement)
            continue;
        const QString name = x.name().toString();
        if (name == QLatin1String("renderer-v2")) {
            rendererType = x.attributes().value(QStringLiteral("type")).toString();
        } else if (name == QLatin1String("rule")) {
            RuleEntry e;
            const auto attrs = x.attributes();
            e.symbol = attrs.value(QStringLiteral("symbol")).toString();
            e.label = attrs.value(QStringLiteral("label")).toString();
            e.filter = attrs.value(QStringLiteral("filter")).toString();
            e.minScale = attrs.value(QStringLiteral("scalemindenom")).toDouble();
            e.maxScale = attrs.value(QStringLiteral("scalemaxdenom")).toDouble();
            ruleEntries.append(e);
        } else if (name == QLatin1String("symbol")) {
            const QString sname = x.attributes().value(QStringLiteral("name")).toString();
            const QList<SymbolLayer> layers = readQgisSymbolLayers(x);
            if (!sname.isEmpty())
                symbolsByName.insert(sname, layers);
        }
    }
    if (x.hasError()) {
        result.error = QObject::tr("QML parse error: %1").arg(x.errorString());
        return result;
    }

    // Build the RuleList from what we collected.
    list->clear();

    auto makeRuleFromSymbol = [](const QString &name,
                                 const QList<SymbolLayer> &layers) -> std::unique_ptr<Rule> {
        SymbolStyle style;
        style.layers = layers;
        auto single = std::make_unique<SingleSymbolRenderer>();
        if (!style.layers.isEmpty())
            single->setSymbol(style);
        return std::make_unique<Rule>(name, std::move(single));
    };

    if (rendererType == QLatin1String("RuleRenderer") && !ruleEntries.isEmpty()) {
        for (const RuleEntry &e : ruleEntries) {
            const QList<SymbolLayer> layers = symbolsByName.value(e.symbol);
            auto r =
                makeRuleFromSymbol(e.label.isEmpty() ? QStringLiteral("Rule") : e.label, layers);
            if (!e.filter.isEmpty())
                r->setFilterExpression(e.filter);
            if (e.minScale > 0.0)
                r->setMinScale(e.minScale);
            if (e.maxScale > 0.0)
                r->setMaxScale(e.maxScale);
            list->append(std::move(r));
            ++result.rulesLoaded;
        }
    } else if (rendererType == QLatin1String("singleSymbol") || rendererType.isEmpty()) {
        // singleSymbol path — there should be exactly one <symbol>; pick
        // the first we found by name (QGIS uses "0").
        const QList<SymbolLayer> layers = symbolsByName.value(
            QStringLiteral("0"),
            symbolsByName.isEmpty() ? QList<SymbolLayer>{} : symbolsByName.constBegin().value());
        list->append(makeRuleFromSymbol(QStringLiteral("Imported"), layers));
        ++result.rulesLoaded;
        if (rendererType.isEmpty())
            result.warnings.append(
                QObject::tr("No renderer-v2 type attribute found — treated as singleSymbol."));
    } else {
        // Categorized / Graduated / etc. — not v1 supported here. We
        // still build a one-Rule fallback from any first symbol so the
        // user gets *something* to edit, plus a warning.
        const QList<SymbolLayer> layers =
            symbolsByName.isEmpty() ? QList<SymbolLayer>{} : symbolsByName.constBegin().value();
        list->append(makeRuleFromSymbol(QStringLiteral("Imported"), layers));
        ++result.rulesLoaded;
        result.warnings.append(
            QObject::tr("Renderer type '%1' isn't fully supported on .qml import — "
                        "imported a single-Rule fallback using the first symbol entry.")
                .arg(rendererType));
    }

    result.ok = true;
    return result;
}

} // namespace OpenSWMM::Render::QmlRuleListIO
