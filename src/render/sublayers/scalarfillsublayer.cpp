/*!
 * \file   scalarfillsublayer.cpp
 * \brief  ScalarFillStyle + CellDepthFillSublayer + SmoothDepthFillSublayer.
 *         Geometry is built by SWMM2DResultsQSGRenderer (these sublayers, like
 *         the other mesh sublayers, defer node construction to the renderer).
 */
#include "render/sublayers/scalarfillsublayer.h"

#include <QJsonObject>
#include <algorithm>

namespace OpenSWMM::Render
{

// ===========================================================================
// ScalarFillStyle
// ===========================================================================
ScalarFillStyle::ScalarFillStyle(QObject *parent) : SublayerStyle(parent)
{
    // Default to the smooth continuous "Water Depth" ramp (2026-09-01):
    // translucent near-white blue at barely-wet deepening to a fully opaque
    // navy at maximum depth (user direction: deeper water = dark blue,
    // shallower = lighter shade). Classified mode (set via the style panel)
    // reuses the same ramp binned into bandCount classes. The display name
    // is stored (not the "water-depth" key) so the ramp combo shows it —
    // the MeshFillStyle "Terrain" precedent.
    m_scheme.setMode(ClassificationScheme::ClassMode::Continuous);
    m_scheme.setMethod(BinMethod::EqualInterval);
    m_scheme.setClassCount(8);
    m_scheme.setRampName(QStringLiteral("Water Depth"));
    m_scheme.setLowColor(QColor(0xf7, 0xfb, 0xff, 100));
    m_scheme.setHighColor(QColor(0x08, 0x30, 0x6b, 255));
}

void ScalarFillStyle::setAttribute(const QString &v)
{ if (m_attribute != v) { m_attribute = v; setDirty(); } }

void ScalarFillStyle::setClassified(bool v)
{
    const auto want = v ? ClassificationScheme::ClassMode::Classified
                        : ClassificationScheme::ClassMode::Continuous;
    if (m_scheme.mode() != want) { m_scheme.setMode(want); setDirty(); }
}

void ScalarFillStyle::setBandCount(int v)
{
    if (v < 1) v = 1;
    if (m_scheme.classCount() == v) return;
    m_scheme.setClassCount(v);
    setDirty();
}

void ScalarFillStyle::setColorRampName(const QString &v)
{ if (m_scheme.rampName()  != v) { m_scheme.setRampName(v);  setDirty(); } }
void ScalarFillStyle::setInvertRamp(bool v)
{ if (m_scheme.invertRamp() != v) { m_scheme.setInvertRamp(v); setDirty(); } }
void ScalarFillStyle::setLowColor(const QColor &v)
{ if (m_scheme.lowColor()  != v) { m_scheme.setLowColor(v);  setDirty(); } }
void ScalarFillStyle::setHighColor(const QColor &v)
{ if (m_scheme.highColor() != v) { m_scheme.setHighColor(v); setDirty(); } }
void ScalarFillStyle::setUseCustomRange(bool v)
{ if (m_scheme.useCustomRange() != v) { m_scheme.setUseCustomRange(v); setDirty(); } }
void ScalarFillStyle::setRangeMin(double v)
{ if (!qFuzzyCompare(m_scheme.rangeMin() + 1.0, v + 1.0)) { m_scheme.setRangeMin(v); setDirty(); } }
void ScalarFillStyle::setRangeMax(double v)
{ if (!qFuzzyCompare(m_scheme.rangeMax() + 1.0, v + 1.0)) { m_scheme.setRangeMax(v); setDirty(); } }

void ScalarFillStyle::setScheme(const ClassificationScheme &s)
{ if (m_scheme == s) return; m_scheme = s; setDirty(); }

QJsonObject ScalarFillStyle::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("attribute"),      m_attribute);
    obj.insert(QStringLiteral("classification"), m_scheme.toJson());
    return obj;
}

void ScalarFillStyle::fromJson(const QJsonObject &j)
{
    m_attribute = j.value(QStringLiteral("attribute")).toString(m_attribute);
    if (j.contains(QStringLiteral("classification")))
        m_scheme = ClassificationScheme::fromJson(
            j.value(QStringLiteral("classification")).toObject());
    setDirty();
}

// ---------------------------------------------------------------------------
// Shared legend builder (continuous → 6-stop ramp; classified → bandCount).
// ---------------------------------------------------------------------------
namespace {
QList<LegendSymbolItem> buildFillLegend(const ScalarFillStyle *style,
                                        const QString &subId, qreal opacity)
{
    QList<LegendSymbolItem> out;
    if (!style) return out;
    const bool classified = style->classified();
    const bool haveRange  = style->useCustomRange()
                            && style->rangeMax() > style->rangeMin();
    const int n = classified ? std::max(1, style->bandCount()) : 6;
    for (int i = 0; i < n; ++i) {
        LegendSymbolItem item;
        QColor c;
        if (classified) {
            c = style->colorForClass(i, n);
            if (haveRange) {
                const double lo = style->rangeMin()
                    + (style->rangeMax() - style->rangeMin()) * double(i) / n;
                const double hi = style->rangeMin()
                    + (style->rangeMax() - style->rangeMin()) * double(i + 1) / n;
                item.label = QStringLiteral("%1 – %2").arg(lo, 0, 'g', 3).arg(hi, 0, 'g', 3);
                item.range = { lo, hi };
            } else {
                item.label = QObject::tr("Class %1").arg(i + 1);
            }
        } else {
            // Continuous: sample the ramp at evenly spaced stops.
            const double f = (n > 1) ? double(i) / double(n - 1) : 0.0;
            c = style->colorForValue(f, 0.0, 1.0);
            item.label = haveRange
                ? QString::number(style->rangeMin()
                      + (style->rangeMax() - style->rangeMin()) * f, 'g', 3)
                : QString();
        }
        item.sublayerId = subId;
        item.classKey   = QString::number(i);
        SymbolLayer fill;
        fill.kind = SymbolLayerKind::SimpleFill;
        SymbolProps::writeColor(fill.props, QStringLiteral("color"), c);
        item.symbol.layers.append(fill);
        item.symbol.opacity = opacity;
        out.append(item);
    }
    return out;
}
} // namespace

// ===========================================================================
// CellDepthFillSublayer
// ===========================================================================
CellDepthFillSublayer::CellDepthFillSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)),
      m_style(new ScalarFillStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}
void CellDepthFillSublayer::setVisible(bool v)
{ if (m_visible == v) return; m_visible = v; emit invalidated(); }
void CellDepthFillSublayer::setOpacity(qreal o)
{
    o = std::clamp<qreal>(o, 0.0, 1.0);
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o; emit invalidated();
}
QList<LegendSymbolItem> CellDepthFillSublayer::legendSymbolItems() const
{ return buildFillLegend(m_style, m_id, m_opacity); }
QSGNode *CellDepthFillSublayer::buildOrUpdateNode(QSGNode *existing,
                                                  const SublayerContext &ctx)
{ Q_UNUSED(ctx); return existing; }

// ===========================================================================
// SmoothDepthFillSublayer
// ===========================================================================
SmoothDepthFillSublayer::SmoothDepthFillSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)),
      m_style(new ScalarFillStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}
void SmoothDepthFillSublayer::setVisible(bool v)
{ if (m_visible == v) return; m_visible = v; emit invalidated(); }
void SmoothDepthFillSublayer::setOpacity(qreal o)
{
    o = std::clamp<qreal>(o, 0.0, 1.0);
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o; emit invalidated();
}
QList<LegendSymbolItem> SmoothDepthFillSublayer::legendSymbolItems() const
{ return buildFillLegend(m_style, m_id, m_opacity); }
QSGNode *SmoothDepthFillSublayer::buildOrUpdateNode(QSGNode *existing,
                                                    const SublayerContext &ctx)
{ Q_UNUSED(ctx); return existing; }

} // namespace OpenSWMM::Render
