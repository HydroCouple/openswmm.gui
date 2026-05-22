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

} // namespace OpenSWMM::Render
