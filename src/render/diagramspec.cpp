/*!
 * \file   diagramspec.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  DiagramSpec impl + JSON round-trip (Slice Z.12-data).
 */

#include "render/diagramspec.h"

#include <QJsonArray>

#include <array>

namespace OpenSWMM::Render
{

namespace {

struct Mapping { DiagramType t; const char *token; };
constexpr std::array<Mapping, 4> kTypeMap = {{
    {DiagramType::Pie,        "pie"},
    {DiagramType::Bar,        "bar"},
    {DiagramType::TimeSeries, "timeSeries"},
    {DiagramType::Histogram,  "histogram"},
}};

} // namespace

QString diagramTypeToString(DiagramType t)
{
    for (const auto &m : kTypeMap)
        if (m.t == t) return QString::fromLatin1(m.token);
    return QStringLiteral("pie");
}

DiagramType diagramTypeFromString(const QString &s)
{
    for (const auto &m : kTypeMap)
        if (s == QLatin1String(m.token)) return m.t;
    return DiagramType::Pie;
}

QJsonObject DiagramSpec::toJson() const
{
    QJsonObject j;
    if (enabled)
        j[QStringLiteral("enabled")] = enabled;
    if (type != DiagramType::Pie)
        j[QStringLiteral("type")] = diagramTypeToString(type);
    if (!attributes.isEmpty()) {
        QJsonArray arr;
        for (const QString &a : attributes) arr.append(a);
        j[QStringLiteral("attributes")] = arr;
    }
    if (!seriesExpression.isEmpty())
        j[QStringLiteral("seriesExpression")] = seriesExpression;
    if (sizePx.width() != 40.0)
        j[QStringLiteral("sizeW")] = sizePx.width();
    if (sizePx.height() != 40.0)
        j[QStringLiteral("sizeH")] = sizePx.height();
    if (offsetPx.x() != 0.0)
        j[QStringLiteral("offsetX")] = offsetPx.x();
    if (offsetPx.y() != 0.0)
        j[QStringLiteral("offsetY")] = offsetPx.y();
    if (!palette.isEmpty()) {
        QJsonArray arr;
        for (const QColor &c : palette)
            arr.append(c.name(QColor::HexArgb));
        j[QStringLiteral("palette")] = arr;
    }
    if (rangeMin != 0.0)
        j[QStringLiteral("rangeMin")] = rangeMin;
    if (rangeMax != 0.0)
        j[QStringLiteral("rangeMax")] = rangeMax;
    return j;
}

DiagramSpec DiagramSpec::fromJson(const QJsonObject &j)
{
    DiagramSpec s;
    s.enabled = j.value(QStringLiteral("enabled")).toBool(false);
    s.type    = diagramTypeFromString(j.value(QStringLiteral("type")).toString());
    const QJsonArray attrs = j.value(QStringLiteral("attributes")).toArray();
    s.attributes.reserve(attrs.size());
    for (const QJsonValue &v : attrs)
        s.attributes.append(v.toString());
    s.seriesExpression = j.value(QStringLiteral("seriesExpression")).toString();
    s.sizePx = QSizeF(
        j.value(QStringLiteral("sizeW")).toDouble(40.0),
        j.value(QStringLiteral("sizeH")).toDouble(40.0));
    s.offsetPx = QPointF(
        j.value(QStringLiteral("offsetX")).toDouble(0.0),
        j.value(QStringLiteral("offsetY")).toDouble(0.0));
    const QJsonArray pal = j.value(QStringLiteral("palette")).toArray();
    s.palette.reserve(pal.size());
    for (const QJsonValue &v : pal)
        s.palette.append(QColor(v.toString()));
    s.rangeMin = j.value(QStringLiteral("rangeMin")).toDouble(0.0);
    s.rangeMax = j.value(QStringLiteral("rangeMax")).toDouble(0.0);
    return s;
}

bool DiagramSpec::operator==(const DiagramSpec &other) const
{
    return enabled          == other.enabled
        && type             == other.type
        && attributes       == other.attributes
        && seriesExpression == other.seriesExpression
        && sizePx           == other.sizePx
        && offsetPx         == other.offsetPx
        && palette          == other.palette
        && qFuzzyCompare(rangeMin + 1.0, other.rangeMin + 1.0)
        && qFuzzyCompare(rangeMax + 1.0, other.rangeMax + 1.0);
}

} // namespace OpenSWMM::Render
