/*!
 * \file   legendsymbolitem.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  JSON round-trip for LegendSymbolItem.
 */

#include "render/legendsymbolitem.h"

#include <QJsonObject>
#include <QJsonValue>

#include <cmath>

namespace OpenSWMM::Render
{

QJsonObject LegendSymbolItem::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("label"), label);
    obj.insert(QStringLiteral("symbol"), symbol.toJson());

    // range is only meaningful when both ends are finite numbers — skip
    // emitting it otherwise so categorical / single-symbol items don't
    // litter the JSON with "NaN" sentinels that some parsers reject.
    if (std::isfinite(range.first) && std::isfinite(range.second))
    {
        QJsonObject r;
        r.insert(QStringLiteral("low"), range.first);
        r.insert(QStringLiteral("high"), range.second);
        obj.insert(QStringLiteral("range"), r);
    }

    if (!userLabel.isEmpty())
        obj.insert(QStringLiteral("userLabel"), userLabel);
    if (!visible)
        obj.insert(QStringLiteral("visible"), false);
    if (sortIndex != 0)
        obj.insert(QStringLiteral("sortIndex"), sortIndex);
    if (!classKey.isEmpty())
        obj.insert(QStringLiteral("classKey"), classKey);
    return obj;
}

void LegendSymbolItem::fromJson(const QJsonObject &j)
{
    label = j.value(QStringLiteral("label")).toString();
    symbol.fromJson(j.value(QStringLiteral("symbol")).toObject());

    if (j.contains(QStringLiteral("range")))
    {
        const QJsonObject r = j.value(QStringLiteral("range")).toObject();
        range.first  = r.value(QStringLiteral("low")).toDouble(qQNaN());
        range.second = r.value(QStringLiteral("high")).toDouble(qQNaN());
    }
    else
    {
        range.first  = qQNaN();
        range.second = qQNaN();
    }

    userLabel = j.value(QStringLiteral("userLabel")).toString();
    visible   = j.value(QStringLiteral("visible")).toBool(true);
    sortIndex = j.value(QStringLiteral("sortIndex")).toInt(0);
    classKey  = j.value(QStringLiteral("classKey")).toString();
}

} // namespace OpenSWMM::Render
