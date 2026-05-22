/*!
 * \file   singlebandpseudocolorrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/renderers/singlebandpseudocolorrenderer.h"

#include "render/symbollayer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

namespace
{

/*!
 * \brief Linearly interpolate two QColors in ARGB space.
 */
QColor lerpColor(const QColor &a, const QColor &b, double f)
{
    const double cf = std::clamp(f, 0.0, 1.0);
    const int ar = a.red();   const int br = b.red();
    const int ag = a.green(); const int bg = b.green();
    const int ab = a.blue();  const int bb = b.blue();
    const int aa = a.alpha(); const int ba = b.alpha();
    return QColor(static_cast<int>(std::lround(ar + cf * (br - ar))),
                  static_cast<int>(std::lround(ag + cf * (bg - ag))),
                  static_cast<int>(std::lround(ab + cf * (bb - ab))),
                  static_cast<int>(std::lround(aa + cf * (ba - aa))));
}

/*!
 * \brief Build a legend swatch SymbolStyle whose only layer is a
 *        SimpleFill with the supplied colour.  Keeps the legend
 *        symbol shape consistent across the two renderer hierarchies.
 */
SymbolStyle makeSwatch(const QColor &c)
{
    SymbolStyle s;
    SymbolLayer sl;
    sl.kind = SymbolLayerKind::SimpleFill;
    sl.props.insert(QStringLiteral("color"), c.name(QColor::HexArgb));
    s.layers.append(sl);
    return s;
}

QJsonArray stopsToJson(const QList<SingleBandPseudoColorRenderer::Stop> &stops)
{
    QJsonArray arr;
    for (const auto &s : stops)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("pos"),   s.first);
        obj.insert(QStringLiteral("color"), s.second.name(QColor::HexArgb));
        arr.append(obj);
    }
    return arr;
}

QList<SingleBandPseudoColorRenderer::Stop> jsonToStops(const QJsonArray &arr)
{
    QList<SingleBandPseudoColorRenderer::Stop> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
    {
        const QJsonObject obj = v.toObject();
        const double pos = obj.value(QStringLiteral("pos")).toDouble(0.0);
        QColor c(obj.value(QStringLiteral("color")).toString());
        if (!c.isValid())
            c = Qt::transparent;
        out.append({ pos, c });
    }
    return out;
}

} // namespace

void SingleBandPseudoColorRenderer::setRange(double minValue, double maxValue)
{
    m_minValue = minValue;
    m_maxValue = maxValue;
}

void SingleBandPseudoColorRenderer::setStops(QList<Stop> stops)
{
    m_stops = std::move(stops);
}

QColor SingleBandPseudoColorRenderer::colorForValue(double value, bool isNoData) const
{
    if (isNoData)
        return Qt::transparent;
    if (!std::isfinite(value))
        return Qt::transparent;
    if (m_stops.isEmpty())
        return Qt::transparent;

    // Out-of-range handling per clamp policy.
    if (value < m_minValue)
        return m_clampMin ? QColor(Qt::transparent) : m_stops.first().second;
    if (value > m_maxValue)
        return m_clampMax ? QColor(Qt::transparent) : m_stops.last().second;

    // Degenerate range — fall back to the first stop.
    if (m_maxValue <= m_minValue)
        return m_stops.first().second;

    const double t = (value - m_minValue) / (m_maxValue - m_minValue);

    // Single-stop ramp behaves as a constant.
    if (m_stops.size() == 1)
        return m_stops.first().second;

    // Find the surrounding pair (lo, hi) such that lo.pos <= t <= hi.pos.
    // The stops list is required to be in ascending-position order; we do
    // not sort here (see header contract).
    int hiIdx = 1;
    while (hiIdx < m_stops.size() && m_stops[hiIdx].first < t)
        ++hiIdx;
    if (hiIdx >= m_stops.size())
        return m_stops.last().second;

    const Stop &lo = m_stops.at(hiIdx - 1);
    const Stop &hi = m_stops.at(hiIdx);
    if (hi.first <= lo.first)
        return lo.second;

    const double f = (t - lo.first) / (hi.first - lo.first);
    return lerpColor(lo.second, hi.second, f);
}

QList<LegendSymbolItem> SingleBandPseudoColorRenderer::legendSymbolItems() const
{
    QList<LegendSymbolItem> items;
    items.reserve(m_stops.size());

    const double span = m_maxValue - m_minValue;

    int i = 0;
    for (const Stop &s : m_stops)
    {
        LegendSymbolItem item;
        // Compute the data-space value represented by this stop's
        // normalised position.
        const double v = m_minValue + s.first * span;
        item.label = QString::number(v);
        item.symbol = makeSwatch(s.second);
        item.sortIndex = i;
        // Range is intentionally left as the default (NaN, NaN) — stops
        // are single points on the ramp, not bins with extent.
        items.append(item);
        ++i;
    }
    return items;
}

QJsonObject SingleBandPseudoColorRenderer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), rendererId());
    obj.insert(QStringLiteral("minValue"), m_minValue);
    obj.insert(QStringLiteral("maxValue"), m_maxValue);
    obj.insert(QStringLiteral("stops"),    stopsToJson(m_stops));
    obj.insert(QStringLiteral("clampMin"), m_clampMin);
    obj.insert(QStringLiteral("clampMax"), m_clampMax);
    return obj;
}

void SingleBandPseudoColorRenderer::fromJson(const QJsonObject &j)
{
    m_minValue = j.value(QStringLiteral("minValue")).toDouble(0.0);
    m_maxValue = j.value(QStringLiteral("maxValue")).toDouble(1.0);
    m_stops    = jsonToStops(j.value(QStringLiteral("stops")).toArray());
    m_clampMin = j.value(QStringLiteral("clampMin")).toBool(false);
    m_clampMax = j.value(QStringLiteral("clampMax")).toBool(false);
}

std::unique_ptr<IRasterRenderer> SingleBandPseudoColorRenderer::clone() const
{
    return std::make_unique<SingleBandPseudoColorRenderer>(*this);
}

} // namespace OpenSWMM::Render
