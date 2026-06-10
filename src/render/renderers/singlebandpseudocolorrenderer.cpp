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

// P5/R-1 — interpolation mirrored from RasterColorRamp (render/colorramp.cpp)
// so the renderer reproduces the legacy ramp output byte-for-byte after the
// full switch. Kept in sync deliberately: the legacy ramp is the reference.
QColor interpRgb(const QColor &c0, const QColor &c1, double f)
{
    return QColor::fromRgbF(
        c0.redF()   + f * (c1.redF()   - c0.redF()),
        c0.greenF() + f * (c1.greenF() - c0.greenF()),
        c0.blueF()  + f * (c1.blueF()  - c0.blueF()),
        c0.alphaF() + f * (c1.alphaF() - c0.alphaF()));
}

bool isAchromatic(const QColor &c)
{
    float h = 0.0f, s = 0.0f, v = 0.0f, a = 0.0f;
    c.getHsvF(&h, &s, &v, &a);
    return h < 0.0f || s <= 1e-6f;
}

QColor interpHsv(const QColor &c0, const QColor &c1, double f, bool longArc)
{
    if (isAchromatic(c0) || isAchromatic(c1))
        return interpRgb(c0, c1, f);

    float h0 = 0.0f, s0 = 0.0f, v0 = 0.0f, a0 = 0.0f;
    float h1 = 0.0f, s1 = 0.0f, v1 = 0.0f, a1 = 0.0f;
    c0.getHsvF(&h0, &s0, &v0, &a0);
    c1.getHsvF(&h1, &s1, &v1, &a1);

    double dh = h1 - h0;
    if (longArc)
    {
        if (dh > 0.0 && dh < 0.5) dh -= 1.0;
        else if (dh < 0.0 && dh > -0.5) dh += 1.0;
    }
    else
    {
        if (dh > 0.5)  dh -= 1.0;
        else if (dh < -0.5) dh += 1.0;
    }
    double h = h0 + f * dh;
    h = h - std::floor(h);
    const double s = s0 + f * (s1 - s0);
    const double v = v0 + f * (v1 - v0);
    const double a = a0 + f * (a1 - a0);
    return QColor::fromHsvF(static_cast<float>(h),
                            static_cast<float>(std::clamp(s, 0.0, 1.0)),
                            static_cast<float>(std::clamp(v, 0.0, 1.0)),
                            static_cast<float>(std::clamp(a, 0.0, 1.0)));
}

QColor interpStops(const QColor &c0, const QColor &c1, double f, RampInterp interp)
{
    switch (interp)
    {
    case RampInterp::Rgb:      return interpRgb(c0, c1, f);
    case RampInterp::HsvShort: return interpHsv(c0, c1, f, false);
    case RampInterp::HsvLong:  return interpHsv(c0, c1, f, true);
    }
    return interpRgb(c0, c1, f);
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
    SymbolProps::writeColor(sl.props, QStringLiteral("color"), c);
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

    // Out-of-range transparency per clamp policy (matches the legacy
    // RasterColorRamp::colorForValue gate exactly).
    if (m_clampMin && value < m_minValue)
        return Qt::transparent;
    if (m_clampMax && value > m_maxValue)
        return Qt::transparent;

    // Single-stop ramp behaves as a constant.
    if (m_stops.size() == 1)
        return m_stops.first().second;

    // Degenerate range — preserve this renderer's documented "first stop"
    // contract (covered by test_irasterrenderer). This is the one edge
    // where it diverges from RasterColorRamp (which would sample the
    // midpoint); only a flat raster (min == max) hits it, where the choice
    // is visually immaterial.
    const double range = m_maxValue - m_minValue;
    if (range <= 0.0)
        return m_stops.first().second;

    // Normalise to [0,1] and walk the stops exactly like
    // RasterColorRamp::colorAt — clamping pins non-clamped out-of-range
    // values to an end stop; interpStops honours the RGB/HSV interp mode.
    double t = std::clamp((value - m_minValue) / range, 0.0, 1.0);

    if (t <= m_stops.first().first)
        return m_stops.first().second;
    if (t >= m_stops.last().first)
        return m_stops.last().second;

    // Find the surrounding pair (lo, hi) such that lo.pos <= t <= hi.pos.
    // Stops are required to be in ascending-position order (header contract).
    for (int i = 1; i < m_stops.size(); ++i)
    {
        if (t <= m_stops[i].first)
        {
            const Stop &lo = m_stops.at(i - 1);
            const Stop &hi = m_stops.at(i);
            if (hi.first <= lo.first)
                return lo.second;
            const double f = (t - lo.first) / (hi.first - lo.first);
            return interpStops(lo.second, hi.second, f, m_interp);
        }
    }
    return m_stops.last().second;
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
    obj.insert(QStringLiteral("interp"),   static_cast<int>(m_interp));
    return obj;
}

void SingleBandPseudoColorRenderer::fromJson(const QJsonObject &j)
{
    m_minValue = j.value(QStringLiteral("minValue")).toDouble(0.0);
    m_maxValue = j.value(QStringLiteral("maxValue")).toDouble(1.0);
    m_stops    = jsonToStops(j.value(QStringLiteral("stops")).toArray());
    m_clampMin = j.value(QStringLiteral("clampMin")).toBool(false);
    m_clampMax = j.value(QStringLiteral("clampMax")).toBool(false);
    m_interp   = static_cast<RampInterp>(
        j.value(QStringLiteral("interp")).toInt(static_cast<int>(RampInterp::Rgb)));
}

std::unique_ptr<IRasterRenderer> SingleBandPseudoColorRenderer::clone() const
{
    return std::make_unique<SingleBandPseudoColorRenderer>(*this);
}

} // namespace OpenSWMM::Render
