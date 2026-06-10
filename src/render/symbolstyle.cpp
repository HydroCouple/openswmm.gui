/*!
 * \file   symbolstyle.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  JSON round-trip for SymbolStyle (stack of SymbolLayer + opacity).
 */

#include "render/symbolstyle.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace OpenSWMM::Render
{

QJsonObject SymbolStyle::toJson() const
{
    QJsonObject obj;
    QJsonArray arr;
    for (const SymbolLayer &sl : layers)
    {
        arr.append(sl.toJson());
    }
    obj.insert(QStringLiteral("layers"), arr);
    obj.insert(QStringLiteral("opacity"), opacity);
    return obj;
}

void SymbolStyle::fromJson(const QJsonObject &j)
{
    layers.clear();
    const QJsonArray arr = j.value(QStringLiteral("layers")).toArray();
    layers.reserve(arr.size());
    for (const QJsonValue &v : arr)
    {
        SymbolLayer sl;
        sl.fromJson(v.toObject());
        layers.append(sl);
    }
    // Tolerate missing opacity by defaulting to 1.0 (matches struct default).
    const QJsonValue op = j.value(QStringLiteral("opacity"));
    opacity = op.isDouble() ? op.toDouble() : 1.0;
}

// ── SymbolProps — canonical colour prop accessors (gap A1.2) ──────────

namespace SymbolProps
{

QColor readColor(const QVariantMap &props, const QString &key,
                 const QColor &fallback)
{
    const auto it = props.constFind(key);
    if (it == props.constEnd()) return fallback;
    const QVariant &v = it.value();
    if (v.userType() == QMetaType::QColor) {
        // A QColor-typed variant is authoritative: an invalid QColor means
        // "explicitly no colour" → fallback. (Do NOT fall through to the
        // string parse — Qt stringifies an invalid QColor as "#000000",
        // which would silently turn rejection into black.)
        const QColor c = v.value<QColor>();
        return c.isValid() ? c : fallback;
    }
    // Legacy encoding: hex ("#AARRGGBB") or named string.
    const QColor c(v.toString());
    return c.isValid() ? c : fallback;
}

void writeColor(QVariantMap &props, const QString &key, const QColor &c)
{
    props.insert(key, QVariant::fromValue(c));
}

QColor firstColor(const SymbolStyle &style, const QColor &fallback)
{
    // Key order matches the legend-swatch convention established by the
    // X1 fix in the legend views: fill first (marker/fill grammar), then
    // line colour, then outline as a last resort.
    static const QString kKeys[] = { QStringLiteral("fillColor"),
                                     QStringLiteral("color"),
                                     QStringLiteral("outlineColor") };
    for (const SymbolLayer &sl : style.layers) {
        for (const QString &key : kKeys) {
            const QColor c = readColor(sl.props, key);
            if (c.isValid()) return c;
        }
    }
    return fallback;
}

void overrideColorInPlace(SymbolStyle &style, const QColor &c)
{
    for (SymbolLayer &sl : style.layers) {
        if (sl.props.contains(QStringLiteral("fillColor")))
            writeColor(sl.props, QStringLiteral("fillColor"), c);
        if (sl.props.contains(QStringLiteral("color")))
            writeColor(sl.props, QStringLiteral("color"), c);
    }
}

} // namespace SymbolProps

} // namespace OpenSWMM::Render
