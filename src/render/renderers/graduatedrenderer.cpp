/*!
 * \file   graduatedrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/renderers/graduatedrenderer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenSWMM::Render
{

namespace
{

// Gap A1.2 — colour convention now lives in one place: SymbolProps
// (render/symbolstyle.h). Local alias keeps the call sites unchanged.
using SymbolProps::overrideColorInPlace;

// Slice BB-α legacy-load shim: synthesise a discrete ramp from a list of
// per-bin colours saved by pre-BB-α schemas. The synthesised ramp uses N
// even-spaced stops so colorAt at each bin midpoint reproduces the
// original colour exactly.
RasterColorRamp rampFromBinColors(const QList<QColor> &binColors, double mn, double mx)
{
    RasterColorRamp r;
    r.minValue = mn;
    r.maxValue = mx;
    r.interp   = RampInterp::Rgb;
    const int n = binColors.size();
    if (n <= 0)
        return RasterColorRamp::viridis(mn, mx);
    QGradientStops stops;
    stops.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        // Sample at bin midpoint so the ramp evaluated at each midpoint
        // returns the original colour.
        const double pos = (n == 1) ? 0.5
                                    : (static_cast<double>(i) + 0.5) / static_cast<double>(n);
        stops.append({pos, binColors.at(i)});
    }
    r.stops = stops;
    return r;
}

} // namespace

void GraduatedRenderer::setRange(double minValue, double maxValue)
{
    m_ramp.minValue = minValue;
    m_ramp.maxValue = maxValue;
}

void GraduatedRenderer::setRamp(RasterColorRamp ramp)
{
    m_ramp = std::move(ramp);
}

void GraduatedRenderer::setBinner(IntervalBinner b)
{
    m_binner = std::move(b);
    // The binner config (method / bin count / manual breaks) just changed, so
    // the cached breaks are stale. Clear them; the owning layer's next rebuild
    // re-derives them from data via classifyIfNeeded(). This is what makes a
    // "change bin count / method" edit actually re-sample. (Load paths set
    // m_binner directly in fromJson, so saved breaks are unaffected.)
    m_lastBreaks.clear();
}

QList<QColor> GraduatedRenderer::binColors() const
{
    QList<QColor> out;
    const int n = m_binner.binCount();
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.append(colorForBin(i));
    return out;
}

void GraduatedRenderer::setBinColors(QList<QColor> colors)
{
    // Legacy compatibility: build a discrete ramp from the per-bin colours
    // and adopt the colour count as the bin count. autoClassify can be
    // called afterwards to recompute breaks from real data.
    if (colors.isEmpty())
        return;
    m_binner.setBinCount(colors.size());
    m_ramp = rampFromBinColors(colors, m_ramp.minValue, m_ramp.maxValue);
}

void GraduatedRenderer::setBaseSymbol(SymbolStyle s)
{
    m_baseSymbol = std::move(s);
}

void GraduatedRenderer::autoClassify(const QVector<double> &samples)
{
    bool any = false;
    double mn = std::numeric_limits<double>::infinity();
    double mx = -std::numeric_limits<double>::infinity();
    for (double v : samples)
    {
        if (!std::isfinite(v))
            continue;
        any = true;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    if (!any)
        return;
    if (mn == mx)
    {
        const double eps = std::abs(mn) > 0.0 ? std::abs(mn) * 1e-9 : 1e-9;
        mn -= eps;
        mx += eps;
    }
    m_ramp.minValue = mn;
    m_ramp.maxValue = mx;
    m_lastBreaks = m_binner.computeBreaks(samples);
}

void GraduatedRenderer::setOutputSizeRange(double minPx, double maxPx)
{
    m_outputSizeMin = minPx;
    m_outputSizeMax = maxPx;
}

double GraduatedRenderer::sizeForBin(int bin) const
{
    const int n = m_binner.binCount();
    if (n <= 0) return m_outputSizeMin;
    if (bin < 0)    bin = 0;
    if (bin >= n)   bin = n - 1;
    if (n == 1)     return (m_outputSizeMin + m_outputSizeMax) * 0.5;
    const double t = static_cast<double>(bin) / static_cast<double>(n - 1);
    return m_outputSizeMin + t * (m_outputSizeMax - m_outputSizeMin);
}

void GraduatedRenderer::setOutputWidthRange(double minPx, double maxPx)
{
    m_outputWidthMin = minPx;
    m_outputWidthMax = maxPx;
}

double GraduatedRenderer::widthForBin(int bin) const
{
    const int n = m_binner.binCount();
    if (n <= 0) return m_outputWidthMin;
    if (bin < 0)    bin = 0;
    if (bin >= n)   bin = n - 1;
    if (n == 1)     return (m_outputWidthMin + m_outputWidthMax) * 0.5;
    const double t = static_cast<double>(bin) / static_cast<double>(n - 1);
    return m_outputWidthMin + t * (m_outputWidthMax - m_outputWidthMin);
}

QColor GraduatedRenderer::colorForBin(int bin) const
{
    const int n = m_binner.binCount();
    if (n <= 0)
        return Qt::transparent;
    if (bin < 0)     bin = 0;
    if (bin >= n)    bin = n - 1;
    // Slice BB Phase 8.6.16 — user override wins over ramp sampling.
    // Overrides survive ramp swaps; cleared explicitly via clearClassEditOverrides().
    if (const auto it = m_binColorOverrides.constFind(bin); it != m_binColorOverrides.constEnd())
        return it.value();
    // Sample the ramp at the bin's normalised midpoint so the legend
    // swatch and the painted pixel agree.
    const double t = (static_cast<double>(bin) + 0.5) / static_cast<double>(n);
    return m_ramp.colorAt(t);
}

QColor GraduatedRenderer::colorForClass(const QString &classKey) const
{
    bool ok = false;
    const int bin = classKey.toInt(&ok);
    if (!ok || bin < 0) return {};
    // Only report the override; absence is signalled by an invalid colour
    // so undo-command snapshots can tell "no override" from "override = X".
    const auto it = m_binColorOverrides.constFind(bin);
    return it != m_binColorOverrides.constEnd() ? it.value() : QColor{};
}

void GraduatedRenderer::setColorForClass(const QString &classKey, const QColor &color)
{
    bool ok = false;
    const int bin = classKey.toInt(&ok);
    if (!ok || bin < 0) return;
    if (color.isValid())
        m_binColorOverrides.insert(bin, color);
    else
        m_binColorOverrides.remove(bin);   // invalid colour ⇒ drop override.
}

void GraduatedRenderer::clearClassEditOverrides()
{
    m_binColorOverrides.clear();
}

QColor GraduatedRenderer::colorForValue(double v) const
{
    const int n = m_binner.binCount();
    if (n <= 0)
        return Qt::transparent;
    if (!std::isfinite(v))
        return colorForBin(0);
    // If the binner has not been classified yet, derive an inline equal-
    // interval bin from the ramp's range.
    if (m_lastBreaks.isEmpty())
    {
        if (m_ramp.maxValue <= m_ramp.minValue)
            return colorForBin(0);
        const double t = (v - m_ramp.minValue) / (m_ramp.maxValue - m_ramp.minValue);
        int bin = static_cast<int>(std::floor(t * static_cast<double>(n)));
        if (bin < 0)   bin = 0;
        if (bin >= n)  bin = n - 1;
        return colorForBin(bin);
    }
    return colorForBin(m_binner.binFor(v, m_lastBreaks));
}

SymbolStyle GraduatedRenderer::symbolFor(const FeatureRef &, const QVariantMap &attrs) const
{
    SymbolStyle styled = m_baseSymbol;
    const QVariant v = attrs.value(m_classifyAttribute);
    bool ok = false;
    const double dv = v.toDouble(&ok);
    if (!ok)
        return styled;

    // Compute the bin index once; both color + size outputs key off it.
    const int n = m_binner.binCount();
    int bin = 0;
    if (n > 0) {
        if (!m_lastBreaks.isEmpty()) {
            bin = m_binner.binFor(dv, m_lastBreaks);
        } else if (m_ramp.maxValue > m_ramp.minValue) {
            const double t = (dv - m_ramp.minValue) / (m_ramp.maxValue - m_ramp.minValue);
            bin = static_cast<int>(std::floor(t * static_cast<double>(n)));
        }
        if (bin < 0)   bin = 0;
        if (bin >= n)  bin = n - 1;
    }

    // Output axis 1 — colour. Default-on (preserves prior behaviour).
    if (m_outputColorEnabled)
        overrideColorInPlace(styled, colorForBin(bin));

    // Output axis 2 — marker size (Slice BI Phase 8.13.43-α). Default-off.
    if (m_outputSizeEnabled) {
        const double sz = sizeForBin(bin);
        for (SymbolLayer &sl : styled.layers) {
            if (sl.props.contains(QStringLiteral("size")))
                sl.props.insert(QStringLiteral("size"), sz);
        }
    }

    // Output axis 3 — line width (VS.4). Independent from size so node
    // markers and link strokes scale on their own px ranges. Default-off.
    if (m_outputWidthEnabled) {
        const double w = widthForBin(bin);
        for (SymbolLayer &sl : styled.layers) {
            if (sl.props.contains(QStringLiteral("width")))
                sl.props.insert(QStringLiteral("width"), w);
        }
    }
    return styled;
}

QList<LegendSymbolItem> GraduatedRenderer::legendSymbolItems() const
{
    QList<LegendSymbolItem> items;
    const int n = m_binner.binCount();
    if (n <= 0)
        return items;

    // Derive bin ranges from the last computed breaks when available;
    // otherwise fall back to equal-interval over the ramp's range.
    const double mn = m_ramp.minValue;
    const double mx = m_ramp.maxValue;
    const bool useBreaks = (m_lastBreaks.size() == n - 1);

    items.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        LegendSymbolItem item;
        double low  = (i == 0)     ? mn
                                   : (useBreaks ? m_lastBreaks.at(i - 1)
                                                : mn + (mx - mn) * static_cast<double>(i)     / n);
        double high = (i == n - 1) ? mx
                                   : (useBreaks ? m_lastBreaks.at(i)
                                                : mn + (mx - mn) * static_cast<double>(i + 1) / n);
        item.range = { low, high };
        item.label = QStringLiteral("%1 – %2").arg(low).arg(high);
        item.symbol = m_baseSymbol;
        overrideColorInPlace(item.symbol, colorForBin(i));
        item.sortIndex = i;
        item.classKey  = QString::number(i);
        items.append(item);
    }
    return items;
}

QJsonObject GraduatedRenderer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), rendererId());
    obj.insert(QStringLiteral("classifyAttribute"), m_classifyAttribute);
    obj.insert(QStringLiteral("ramp"), m_ramp.toJson());
    obj.insert(QStringLiteral("binner"), m_binner.toJson());
    QJsonArray jb;
    for (double b : m_lastBreaks) jb.append(b);
    obj.insert(QStringLiteral("lastBreaks"), jb);
    obj.insert(QStringLiteral("baseSymbol"), m_baseSymbol.toJson());
    // Slice BI Phase 8.13.43-α — output-axis toggles.
    obj.insert(QStringLiteral("outputColorEnabled"), m_outputColorEnabled);
    obj.insert(QStringLiteral("outputSizeEnabled"),  m_outputSizeEnabled);
    obj.insert(QStringLiteral("outputSizeMin"),      m_outputSizeMin);
    obj.insert(QStringLiteral("outputSizeMax"),      m_outputSizeMax);
    // VS.4 — independent line-width output axis.
    obj.insert(QStringLiteral("outputWidthEnabled"), m_outputWidthEnabled);
    obj.insert(QStringLiteral("outputWidthMin"),     m_outputWidthMin);
    obj.insert(QStringLiteral("outputWidthMax"),     m_outputWidthMax);
    // P2 — static/dynamic source + range mode.
    obj.insert(QStringLiteral("sourceKind"), attributeSourceKindToString(m_sourceKind));
    obj.insert(QStringLiteral("rangeMode"),  rangeModeToString(m_rangeMode));
    // Slice BB Phase 8.6.16 — per-bin colour overrides. Stored as a flat
    // {"bin": "#aarrggbb", …} object so missing keys = no override.
    if (!m_binColorOverrides.isEmpty()) {
        QJsonObject jo;
        for (auto it = m_binColorOverrides.constBegin();
             it != m_binColorOverrides.constEnd(); ++it) {
            jo.insert(QString::number(it.key()), it.value().name(QColor::HexArgb));
        }
        obj.insert(QStringLiteral("binColorOverrides"), jo);
    }
    return obj;
}

void GraduatedRenderer::fromJson(const QJsonObject &j)
{
    m_classifyAttribute = j.value(QStringLiteral("classifyAttribute")).toString();

    // BB-α schema: prefer the new "ramp" + "binner" keys when present.
    if (j.contains(QStringLiteral("ramp")))
    {
        m_ramp = RasterColorRamp::fromJson(j.value(QStringLiteral("ramp")).toObject());
    }
    if (j.contains(QStringLiteral("binner")))
    {
        m_binner = IntervalBinner::fromJson(j.value(QStringLiteral("binner")).toObject());
    }

    m_lastBreaks.clear();
    const QJsonArray jb = j.value(QStringLiteral("lastBreaks")).toArray();
    m_lastBreaks.reserve(jb.size());
    for (const QJsonValue &v : jb) m_lastBreaks.append(v.toDouble());

    // Legacy load shim: pre-BB-α projects stored a flat `binColors` array
    // + `minValue` + `maxValue`. Synthesise a discrete ramp + equal-
    // interval binner so the project's visuals survive.
    const bool hasLegacy = j.contains(QStringLiteral("binColors"))
                       && !j.contains(QStringLiteral("ramp"));
    if (hasLegacy)
    {
        const double mn = j.value(QStringLiteral("minValue")).toDouble(0.0);
        const double mx = j.value(QStringLiteral("maxValue")).toDouble(1.0);
        const QJsonArray arr = j.value(QStringLiteral("binColors")).toArray();
        QList<QColor> legacyColors;
        legacyColors.reserve(arr.size());
        for (const QJsonValue &v : arr)
        {
            const QColor c(v.toString());
            legacyColors.append(c.isValid() ? c : QColor(Qt::transparent));
        }
        m_binner.setMethod(BinMethod::EqualInterval);
        m_binner.setBinCount(legacyColors.size() > 0 ? legacyColors.size() : 5);
        m_ramp = rampFromBinColors(legacyColors, mn, mx);
    }

    m_baseSymbol = SymbolStyle{};
    m_baseSymbol.fromJson(j.value(QStringLiteral("baseSymbol")).toObject());

    // Slice BI Phase 8.13.43-α — output-axis toggles. Default-on for color
    // (preserves prior behaviour), default-off for size.
    if (j.contains(QStringLiteral("outputColorEnabled")))
        m_outputColorEnabled = j.value(QStringLiteral("outputColorEnabled")).toBool(true);
    if (j.contains(QStringLiteral("outputSizeEnabled")))
        m_outputSizeEnabled = j.value(QStringLiteral("outputSizeEnabled")).toBool(false);
    if (j.contains(QStringLiteral("outputSizeMin")))
        m_outputSizeMin = j.value(QStringLiteral("outputSizeMin")).toDouble(2.0);
    if (j.contains(QStringLiteral("outputSizeMax")))
        m_outputSizeMax = j.value(QStringLiteral("outputSizeMax")).toDouble(14.0);

    // VS.4 — independent line-width axis. If the project predates VS.4
    // (no width keys) but used the size axis, migrate: the old size axis
    // wrote BOTH "size" and "width" from the same px range, so reproduce
    // that width behaviour to preserve the project's visuals.
    if (j.contains(QStringLiteral("outputWidthEnabled"))) {
        m_outputWidthEnabled = j.value(QStringLiteral("outputWidthEnabled")).toBool(false);
        m_outputWidthMin     = j.value(QStringLiteral("outputWidthMin")).toDouble(0.5);
        m_outputWidthMax     = j.value(QStringLiteral("outputWidthMax")).toDouble(6.0);
    } else {
        m_outputWidthEnabled = m_outputSizeEnabled;
        m_outputWidthMin     = m_outputSizeMin;
        m_outputWidthMax     = m_outputSizeMax;
    }

    // P2 — static/dynamic source + range mode (default Static / FixedOverRun
    // so pre-P2 projects load unchanged).
    m_sourceKind = attributeSourceKindFromString(
        j.value(QStringLiteral("sourceKind")).toString());
    m_rangeMode = rangeModeFromString(
        j.value(QStringLiteral("rangeMode")).toString());

    // Slice BB Phase 8.6.16 — per-bin colour overrides.
    m_binColorOverrides.clear();
    const QJsonObject jo = j.value(QStringLiteral("binColorOverrides")).toObject();
    for (auto it = jo.constBegin(); it != jo.constEnd(); ++it) {
        bool ok = false;
        const int bin = it.key().toInt(&ok);
        const QColor c = QColor(it.value().toString());
        if (ok && bin >= 0 && c.isValid())
            m_binColorOverrides.insert(bin, c);
    }
}

std::unique_ptr<IFeatureRenderer> GraduatedRenderer::clone() const
{
    return std::make_unique<GraduatedRenderer>(*this);
}

} // namespace OpenSWMM::Render
