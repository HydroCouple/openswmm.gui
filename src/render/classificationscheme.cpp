/*!
 * \file   classificationscheme.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice US.1 — shared classification value type.
 */
#include "render/classificationscheme.h"

#include "render/symbolstyle.h"

#include <QJsonObject>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace OpenSWMM::Render
{

namespace
{

// Process-global revision source. Starts at 1 so a freshly stamped scheme
// never collides with the default-constructed revision of 0.
std::atomic<quint64> g_revisionCounter{1};

QString modeToString(ClassificationScheme::ClassMode m)
{
    return m == ClassificationScheme::ClassMode::Continuous
               ? QStringLiteral("continuous")
               : QStringLiteral("classified");
}

ClassificationScheme::ClassMode modeFromString(const QString &s)
{
    return s == QLatin1String("continuous")
               ? ClassificationScheme::ClassMode::Continuous
               : ClassificationScheme::ClassMode::Classified;
}

} // namespace

ClassificationScheme::ClassificationScheme() = default;

void ClassificationScheme::bump()
{
    m_revision = g_revisionCounter.fetch_add(1, std::memory_order_relaxed);
}

void ClassificationScheme::setMode(ClassMode m)
{
    if (m_mode == m) return;
    m_mode = m;
    bump();
}

void ClassificationScheme::setBinner(const IntervalBinner &b)
{
    if (m_binner.method() == b.method() && m_binner.binCount() == b.binCount()
        && m_binner.manualBreaks() == b.manualBreaks())
        return;
    m_binner = b;
    bump();
}

void ClassificationScheme::setMethod(BinMethod m)
{
    if (m_binner.method() == m) return;
    m_binner.setMethod(m);
    bump();
}

void ClassificationScheme::setClassCount(int n)
{
    if (n < 1) n = 1;
    if (m_binner.binCount() == n) return;
    m_binner.setBinCount(n);
    bump();
}

void ClassificationScheme::setManualBreaks(QVector<double> breaks)
{
    std::sort(breaks.begin(), breaks.end());
    if (m_binner.manualBreaks() == breaks) return;
    m_binner.setManualBreaks(std::move(breaks));
    bump();
}

void ClassificationScheme::setRampName(const QString &name)
{
    if (m_rampName == name) return;
    m_rampName = name;
    bump();
}

void ClassificationScheme::setInvertRamp(bool on)
{
    if (m_invertRamp == on) return;
    m_invertRamp = on;
    bump();
}

void ClassificationScheme::setLowColor(const QColor &c)
{
    if (m_lowColor == c) return;
    m_lowColor = c;
    bump();
}

void ClassificationScheme::setHighColor(const QColor &c)
{
    if (m_highColor == c) return;
    m_highColor = c;
    bump();
}

RasterColorRamp ClassificationScheme::resolvedRamp() const
{
    if (!m_rampName.isEmpty())
        return RasterColorRamp::builtin(m_rampName);
    RasterColorRamp ramp;
    ramp.minValue = 0.0;
    ramp.maxValue = 1.0;
    ramp.stops = { { 0.0, m_lowColor }, { 1.0, m_highColor } };
    return ramp;
}

void ClassificationScheme::setUseCustomRange(bool on)
{
    if (m_useCustomRange == on) return;
    m_useCustomRange = on;
    bump();
}

void ClassificationScheme::setRangeMin(double v)
{
    if (m_rangeMin == v) return;
    m_rangeMin = v;
    bump();
}

void ClassificationScheme::setRangeMax(double v)
{
    if (m_rangeMax == v) return;
    m_rangeMax = v;
    bump();
}

void ClassificationScheme::setRangeMode(RangeMode m)
{
    if (m_rangeMode == m) return;
    m_rangeMode = m;
    bump();
}

void ClassificationScheme::setLabelFormat(LabelFormat f)
{
    if (m_labelFormat == f) return;
    m_labelFormat = f;
    bump();
}

void ClassificationScheme::setLabelPrecision(int digits)
{
    const int d = std::clamp(digits, 0, 12);
    if (m_labelPrecision == d) return;
    m_labelPrecision = d;
    bump();
}

QString ClassificationScheme::formatValue(double v) const
{
    if (m_labelFormat == LabelFormat::Decimals)
        return QString::number(v, 'f', m_labelPrecision);
    // Significant figures: 'g' needs precision >= 1.
    return QString::number(v, 'g', std::max(1, m_labelPrecision));
}

QPair<double, double> ClassificationScheme::effectiveRange(double dataMin, double dataMax) const
{
    if (m_useCustomRange && m_rangeMax > m_rangeMin)
        return { m_rangeMin, m_rangeMax };
    return { dataMin, dataMax };
}

QColor ClassificationScheme::colorOverride(int classIndex) const
{
    return m_colorOverrides.value(classIndex, QColor());
}

void ClassificationScheme::setColorOverride(int classIndex, const QColor &c)
{
    if (!c.isValid()) { clearColorOverride(classIndex); return; }
    if (m_colorOverrides.value(classIndex) == c) return;
    m_colorOverrides.insert(classIndex, c);
    bump();
}

void ClassificationScheme::clearColorOverride(int classIndex)
{
    if (m_colorOverrides.remove(classIndex) > 0)
        bump();
}

QString ClassificationScheme::labelOverride(int classIndex) const
{
    return m_labelOverrides.value(classIndex);
}

void ClassificationScheme::setLabelOverride(int classIndex, const QString &label)
{
    if (label.isEmpty()) { clearLabelOverride(classIndex); return; }
    if (m_labelOverrides.value(classIndex) == label) return;
    m_labelOverrides.insert(classIndex, label);
    bump();
}

void ClassificationScheme::clearLabelOverride(int classIndex)
{
    if (m_labelOverrides.remove(classIndex) > 0)
        bump();
}

void ClassificationScheme::clearOverrides()
{
    if (m_colorOverrides.isEmpty() && m_labelOverrides.isEmpty()) return;
    m_colorOverrides.clear();
    m_labelOverrides.clear();
    bump();
}

QVector<double> ClassificationScheme::levelEdges(double dataMin, double dataMax,
                                                 const QVector<double> &samples) const
{
    const auto [lo, hi] = effectiveRange(dataMin, dataMax);
    if (!(hi > lo)) return {};

    const int n = m_binner.binCount();
    QVector<double> edges;

    auto equallySpaced = [&]() {
        // Mirrors Contour::evenlySpacedLevelsInclusive so default-styled
        // output is bit-identical to the pre-scheme renderers.
        QVector<double> out;
        out.reserve(n + 1);
        const double step = (hi - lo) / double(n);
        for (int i = 0; i <= n; ++i)
            out.append(lo + step * double(i));
        out.back() = hi; // guard against floating-point drift
        return out;
    };

    switch (m_binner.method())
    {
    case BinMethod::EqualInterval:
        return equallySpaced();

    case BinMethod::Manual:
    {
        edges.append(lo);
        for (double b : m_binner.manualBreaks())
            if (b > lo && b < hi && (edges.size() < 2 || b != edges.last()))
                edges.append(b);
        edges.append(hi);
        return edges;
    }

    case BinMethod::Logarithmic:
    case BinMethod::Exponential:
    case BinMethod::Quantile:
    case BinMethod::NaturalBreaks:
    case BinMethod::StdDev:
    {
        // Range-only methods (Log/Exp) get the range endpoints as their
        // sample set; data-driven methods classify the in-range finite
        // samples and degrade to the endpoints when none are available.
        QVector<double> classifyOn;
        if (m_binner.method() == BinMethod::Quantile
            || m_binner.method() == BinMethod::NaturalBreaks
            || m_binner.method() == BinMethod::StdDev) {
            classifyOn.reserve(samples.size());
            for (double v : samples)
                if (std::isfinite(v) && v >= lo && v <= hi)
                    classifyOn.append(v);
        }
        if (classifyOn.isEmpty())
            classifyOn = { lo, hi };

        QVector<double> breaks = m_binner.computeBreaks(classifyOn);
        if (breaks.isEmpty())
            return equallySpaced();
        for (double &b : breaks)
            b = std::clamp(b, lo, hi);
        std::sort(breaks.begin(), breaks.end());

        edges.reserve(breaks.size() + 2);
        edges.append(lo);
        for (double b : breaks)
            edges.append(b);
        edges.append(hi);
        return edges;
    }
    }
    return equallySpaced();
}

QVector<double> ClassificationScheme::interiorLevels(double dataMin, double dataMax,
                                                     const QVector<double> &samples) const
{
    QVector<double> edges = levelEdges(dataMin, dataMax, samples);
    if (edges.size() <= 2) return {};
    edges.removeFirst();
    edges.removeLast();
    return edges;
}

int ClassificationScheme::classIndexFor(double value, const QVector<double> &edges)
{
    if (edges.size() < 2 || !std::isfinite(value)) return 0;
    // Interior edges are edges[1..size-2]; the class index is the count of
    // interior edges <= value, clamped to the last class.
    const auto it = std::upper_bound(edges.cbegin() + 1, edges.cend() - 1, value);
    const int idx = int(std::distance(edges.cbegin() + 1, it));
    return std::clamp(idx, 0, int(edges.size()) - 2);
}

QColor ClassificationScheme::colorAtF(double f) const
{
    f = std::clamp(f, 0.0, 1.0);
    const double t = m_invertRamp ? 1.0 - f : f;
    return resolvedRamp().colorAt(t);
}

QColor ClassificationScheme::colorForClass(int classIndex) const
{
    return colorForClass(classIndex, m_binner.binCount());
}

QColor ClassificationScheme::colorForClass(int classIndex, int count) const
{
    const QColor override_ = m_colorOverrides.value(classIndex, QColor());
    if (override_.isValid())
        return override_;
    const int n = std::max(1, count);
    const double f = (double(std::clamp(classIndex, 0, n - 1)) + 0.5) / double(n);
    return colorAtF(f);
}

QColor ClassificationScheme::colorForValue(double value, double dataMin, double dataMax) const
{
    const auto [lo, hi] = effectiveRange(dataMin, dataMax);
    if (!(hi > lo))
        return colorAtF(0.0);
    return colorAtF((value - lo) / (hi - lo));
}

QList<LegendSymbolItem> ClassificationScheme::legendItems(double dataMin, double dataMax,
                                                          const QVector<double> &samples) const
{
    QList<LegendSymbolItem> out;
    const QVector<double> edges = levelEdges(dataMin, dataMax, samples);
    const int n = edges.size() >= 2 ? int(edges.size()) - 1 : m_binner.binCount();

    for (int i = 0; i < n; ++i) {
        LegendSymbolItem item;
        // Separate the numeric class range (item.range / item.label, formatted
        // per labelFormat()/labelPrecision()) from any user-supplied label
        // override (item.userLabel). The legend/editor render the range and
        // the override label as distinct fields.
        if (edges.size() >= 2) {
            item.label = QStringLiteral("%1 – %2")
                             .arg(formatValue(edges[i]))
                             .arg(formatValue(edges[i + 1]));
        } else {
            item.label = QStringLiteral("Class %1").arg(i + 1);
        }
        const QString override_ = m_labelOverrides.value(i);
        if (!override_.isEmpty())
            item.userLabel = override_;
        if (edges.size() >= 2)
            item.range = { edges[i], edges[i + 1] };
        item.classKey  = QString::number(i);
        item.sortIndex = i;

        SymbolLayer fill;
        fill.kind = SymbolLayerKind::SimpleFill;
        SymbolProps::writeColor(fill.props, QStringLiteral("color"), colorForClass(i, n));
        item.symbol.layers.append(fill);
        out.append(item);
    }
    return out;
}

QJsonObject ClassificationScheme::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("mode"),       modeToString(m_mode));
    obj.insert(QStringLiteral("binner"),     m_binner.toJson());
    obj.insert(QStringLiteral("rampName"),   m_rampName);
    obj.insert(QStringLiteral("invertRamp"), m_invertRamp);
    obj.insert(QStringLiteral("lowColor"),   m_lowColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("highColor"),  m_highColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("useCustomRange"), m_useCustomRange);
    obj.insert(QStringLiteral("rangeMin"),   m_rangeMin);
    obj.insert(QStringLiteral("rangeMax"),   m_rangeMax);
    obj.insert(QStringLiteral("rangeMode"),  rangeModeToString(m_rangeMode));
    obj.insert(QStringLiteral("labelFormat"),    int(m_labelFormat));
    obj.insert(QStringLiteral("labelPrecision"), m_labelPrecision);

    if (!m_colorOverrides.isEmpty()) {
        QJsonObject co;
        for (auto it = m_colorOverrides.cbegin(); it != m_colorOverrides.cend(); ++it)
            co.insert(QString::number(it.key()), it.value().name(QColor::HexArgb));
        obj.insert(QStringLiteral("colorOverrides"), co);
    }
    if (!m_labelOverrides.isEmpty()) {
        QJsonObject lo;
        for (auto it = m_labelOverrides.cbegin(); it != m_labelOverrides.cend(); ++it)
            lo.insert(QString::number(it.key()), it.value());
        obj.insert(QStringLiteral("labelOverrides"), lo);
    }
    return obj;
}

ClassificationScheme ClassificationScheme::fromJson(const QJsonObject &j)
{
    ClassificationScheme s;
    s.m_mode       = modeFromString(j.value(QStringLiteral("mode")).toString());
    s.m_binner     = IntervalBinner::fromJson(j.value(QStringLiteral("binner")).toObject());
    s.m_rampName   = j.value(QStringLiteral("rampName")).toString(s.m_rampName);
    s.m_invertRamp = j.value(QStringLiteral("invertRamp")).toBool(s.m_invertRamp);

    auto readColor = [&](const char *key, QColor &slot) {
        const QString t = j.value(QString::fromLatin1(key)).toString();
        if (!t.isEmpty()) { const QColor c(t); if (c.isValid()) slot = c; }
    };
    readColor("lowColor",  s.m_lowColor);
    readColor("highColor", s.m_highColor);

    s.m_useCustomRange = j.value(QStringLiteral("useCustomRange")).toBool(s.m_useCustomRange);
    s.m_rangeMin       = j.value(QStringLiteral("rangeMin")).toDouble(s.m_rangeMin);
    s.m_rangeMax       = j.value(QStringLiteral("rangeMax")).toDouble(s.m_rangeMax);
    s.m_rangeMode      = rangeModeFromString(j.value(QStringLiteral("rangeMode")).toString());
    s.m_labelFormat    = (j.value(QStringLiteral("labelFormat")).toInt(int(s.m_labelFormat))
                          == int(LabelFormat::Decimals))
                             ? LabelFormat::Decimals
                             : LabelFormat::SignificantFigures;
    s.m_labelPrecision = std::clamp(
        j.value(QStringLiteral("labelPrecision")).toInt(s.m_labelPrecision), 0, 12);

    const QJsonObject co = j.value(QStringLiteral("colorOverrides")).toObject();
    for (auto it = co.constBegin(); it != co.constEnd(); ++it) {
        bool ok = false;
        const int idx = it.key().toInt(&ok);
        const QColor c(it.value().toString());
        if (ok && idx >= 0 && c.isValid())
            s.m_colorOverrides.insert(idx, c);
    }
    const QJsonObject lo = j.value(QStringLiteral("labelOverrides")).toObject();
    for (auto it = lo.constBegin(); it != lo.constEnd(); ++it) {
        bool ok = false;
        const int idx = it.key().toInt(&ok);
        if (ok && idx >= 0 && !it.value().toString().isEmpty())
            s.m_labelOverrides.insert(idx, it.value().toString());
    }

    s.bump();
    return s;
}

bool ClassificationScheme::operator==(const ClassificationScheme &o) const
{
    return m_mode == o.m_mode
           && m_binner.method() == o.m_binner.method()
           && m_binner.binCount() == o.m_binner.binCount()
           && m_binner.manualBreaks() == o.m_binner.manualBreaks()
           && m_rampName == o.m_rampName
           && m_invertRamp == o.m_invertRamp
           && m_lowColor == o.m_lowColor
           && m_highColor == o.m_highColor
           && m_useCustomRange == o.m_useCustomRange
           && m_rangeMin == o.m_rangeMin
           && m_rangeMax == o.m_rangeMax
           && m_rangeMode == o.m_rangeMode
           && m_labelFormat == o.m_labelFormat
           && m_labelPrecision == o.m_labelPrecision
           && m_colorOverrides == o.m_colorOverrides
           && m_labelOverrides == o.m_labelOverrides;
}

} // namespace OpenSWMM::Render
