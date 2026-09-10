/*!
 * \file   graduatedrasterrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/renderers/graduatedrasterrenderer.h"

#include "render/symbollayer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>

namespace OpenSWMM::Render
{

namespace
{

/*! Legend swatch: a single SimpleFill layer carrying the colour — the same
 *  shape every raster / feature renderer emits, so the legend views share
 *  one code path (§J.5). */
SymbolStyle makeSwatch(const QColor &c)
{
    SymbolStyle s;
    SymbolLayer sl;
    sl.kind = SymbolLayerKind::SimpleFill;
    SymbolProps::writeColor(sl.props, QStringLiteral("color"), c);
    s.layers.append(sl);
    return s;
}

QJsonArray edgesToJson(const QVector<double> &edges)
{
    QJsonArray arr;
    for (double e : edges)
        arr.append(e);
    return arr;
}

QVector<double> edgesFromJson(const QJsonArray &arr)
{
    QVector<double> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
        out.append(v.toDouble());
    // Reject anything that is not a usable ascending edge list; the caller
    // recomputes from the scheme instead.
    if (out.size() < 2 || !std::is_sorted(out.cbegin(), out.cend()))
        out.clear();
    for (double e : out)
        if (!std::isfinite(e)) { out.clear(); break; }
    return out;
}

} // namespace

GraduatedRasterRenderer::GraduatedRasterRenderer()
{
    m_scheme.setMode(ClassificationScheme::ClassMode::Continuous);
    m_scheme.setRampName(QStringLiteral("grayscale"));
    reclassify();
}

void GraduatedRasterRenderer::setScheme(const ClassificationScheme &scheme)
{
    m_scheme = scheme;
    reclassify();
}

void GraduatedRasterRenderer::setDataRange(double dataMin, double dataMax)
{
    m_dataMin = dataMin;
    m_dataMax = dataMax;
    reclassify();
}

void GraduatedRasterRenderer::setClipOutOfRange(bool on)
{
    m_clipOutOfRange = on;
}

void GraduatedRasterRenderer::reclassify(const QVector<double> &samples)
{
    m_edges = m_scheme.levelEdges(m_dataMin, m_dataMax, samples);
    rebake();
}

void GraduatedRasterRenderer::rebake()
{
    const auto [lo, hi] = m_scheme.effectiveRange(m_dataMin, m_dataMax);
    m_lo = lo;
    m_hi = hi;

    // Continuous LUT: texel i samples the ramp at positionForTexel(i) so a
    // value round-trips through ScalarRampLut::indexFor to the same colour
    // the scheme would return (inversion is applied by colorAtF).
    for (int i = 0; i < ScalarRampLut::kSize; ++i)
        m_lut[size_t(i)] = m_scheme.colorAtF(ScalarRampLut::positionForTexel(i)).rgba();

    // Classified colours: one per class over the CURRENT edges (n may differ
    // from scheme.classCount() for Manual breaks); overrides win inside
    // colorForClass.
    const int n = classCount();
    m_classColors.resize(n);
    for (int i = 0; i < n; ++i)
        m_classColors[i] = m_scheme.colorForClass(i, n).rgba();
}

QColor GraduatedRasterRenderer::colorForValue(double value, bool isNoData) const
{
    if (isNoData || !std::isfinite(value))
        return QColor(Qt::transparent);
    if (m_clipOutOfRange && (value < m_lo || value > m_hi))
        return QColor(Qt::transparent);

    if (m_scheme.mode() == ClassificationScheme::ClassMode::Continuous
        || m_classColors.isEmpty())   // degenerate range: flat raster
        return QColor::fromRgba(m_lut[size_t(ScalarRampLut::indexFor(value, m_lo, m_hi))]);

    return QColor::fromRgba(
        m_classColors.at(ClassificationScheme::classIndexFor(value, m_edges)));
}

QList<LegendSymbolItem> GraduatedRasterRenderer::legendSymbolItems() const
{
    QList<LegendSymbolItem> items;

    if (m_scheme.mode() == ClassificationScheme::ClassMode::Continuous)
    {
        // Sampled swatch rows lo → hi — the same idiom as the 2D depth fill.
        const int rows = kContinuousLegendRows;
        items.reserve(rows);
        for (int i = 0; i < rows; ++i)
        {
            const double f = rows > 1 ? double(i) / double(rows - 1) : 0.0;
            const double v = m_lo + f * (m_hi - m_lo);
            LegendSymbolItem item;
            item.label     = m_scheme.formatValue(v);
            item.symbol    = makeSwatch(QColor::fromRgba(
                m_lut[size_t(std::lround(f * (ScalarRampLut::kSize - 1)))]));
            item.sortIndex = i;
            items.append(item);
        }
        return items;
    }

    const int n = classCount();
    items.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        LegendSymbolItem item;
        item.label = QStringLiteral("%1 – %2")
                         .arg(m_scheme.formatValue(m_edges.at(i)))
                         .arg(m_scheme.formatValue(m_edges.at(i + 1)));
        item.userLabel = m_scheme.labelOverride(i);
        item.range     = { m_edges.at(i), m_edges.at(i + 1) };
        item.classKey  = QString::number(i);
        item.sortIndex = i;
        item.symbol    = makeSwatch(QColor::fromRgba(m_classColors.at(i)));
        items.append(item);
    }
    return items;
}

QJsonObject GraduatedRasterRenderer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"),             rendererId());
    obj.insert(QStringLiteral("scheme"),         m_scheme.toJson());
    obj.insert(QStringLiteral("dataMin"),        m_dataMin);
    obj.insert(QStringLiteral("dataMax"),        m_dataMax);
    obj.insert(QStringLiteral("edges"),          edgesToJson(m_edges));
    obj.insert(QStringLiteral("clipOutOfRange"), m_clipOutOfRange);
    return obj;
}

void GraduatedRasterRenderer::fromJson(const QJsonObject &j)
{
    if (j.contains(QStringLiteral("scheme")))
        m_scheme = ClassificationScheme::fromJson(
            j.value(QStringLiteral("scheme")).toObject());
    m_dataMin        = j.value(QStringLiteral("dataMin")).toDouble(m_dataMin);
    m_dataMax        = j.value(QStringLiteral("dataMax")).toDouble(m_dataMax);
    m_clipOutOfRange = j.value(QStringLiteral("clipOutOfRange")).toBool(false);

    // Persisted edges are authoritative (a Quantile / Jenks classification
    // must reload without the sample it was derived from); fall back to a
    // fresh derivation only when they are missing or malformed.
    const QVector<double> edges =
        edgesFromJson(j.value(QStringLiteral("edges")).toArray());
    if (!edges.isEmpty())
    {
        m_edges = edges;
        rebake();
    }
    else
    {
        reclassify();
    }
}

std::unique_ptr<IRasterRenderer> GraduatedRasterRenderer::clone() const
{
    return std::make_unique<GraduatedRasterRenderer>(*this);
}

} // namespace OpenSWMM::Render
