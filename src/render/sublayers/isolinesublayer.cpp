/*!
 * \file   isolinesublayer.cpp
 * \brief  Slice S5.4.
 */
#include "render/sublayers/isolinesublayer.h"

#include <QJsonObject>
#include <QMetaEnum>

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

IsolineStyle::IsolineStyle(QObject *parent) : SublayerStyle(parent)
{
    // Defaults reproduce the pre-US.2 isoline output: 8 equal-interval levels.
    m_scheme.setMode(ClassificationScheme::ClassMode::Classified);
    m_scheme.setMethod(BinMethod::EqualInterval);
    m_scheme.setClassCount(8);
}

void IsolineStyle::setAttribute(const QString &v)        { if (m_attribute != v) { m_attribute = v; setDirty(); } }
void IsolineStyle::setIsoValueCount(int v)
{
    if (v < 1) v = 1;
    if (m_scheme.classCount() == v) return;
    m_scheme.setClassCount(v);
    setDirty();
}
void IsolineStyle::setScheme(const ClassificationScheme &s)
{
    if (m_scheme == s) return;
    m_scheme = s;
    setDirty();
}
void IsolineStyle::setLineWidthPx(double v)
{
    if (v < 0.0) v = 0.0;
    if (qFuzzyCompare(m_lineWidthPx + 1.0, v + 1.0)) return;
    m_lineWidthPx = v;
    setDirty();
}
void IsolineStyle::setColor(const QColor &v)            { if (m_color       != v) { m_color       = v; setDirty(); } }
void IsolineStyle::setDashPattern(Qt::PenStyle v)       { if (m_dashPattern != v) { m_dashPattern = v; setDirty(); } }
void IsolineStyle::setLabels(bool v)                    { if (m_labels      != v) { m_labels      = v; setDirty(); } }
void IsolineStyle::setLevelMode(LevelMode v)            { if (m_levelMode   != v) { m_levelMode   = v; setDirty(); } }
void IsolineStyle::setLevelInterval(double v)
{
    if (v <= 0.0) v = 1e-6;
    if (qFuzzyCompare(m_levelInterval + 1.0, v + 1.0)) return;
    m_levelInterval = v;
    setDirty();
}
void IsolineStyle::setBaseLevel(double v)
{
    if (qFuzzyCompare(m_baseLevel + 1.0, v + 1.0)) return;
    m_baseLevel = v;
    setDirty();
}
void IsolineStyle::setIndexEvery(int v)
{
    if (v < 0) v = 0;
    if (m_indexEvery == v) return;
    m_indexEvery = v;
    setDirty();
}
void IsolineStyle::setIndexWidthPx(double v)
{
    if (v < 0.1) v = 0.1;
    if (qFuzzyCompare(m_indexWidthPx + 1.0, v + 1.0)) return;
    m_indexWidthPx = v;
    setDirty();
}
void IsolineStyle::setLabelDecimals(int v)
{
    v = std::clamp(v, 0, 9);
    if (m_labelDecimals == v) return;
    m_labelDecimals = v;
    setDirty();
}
void IsolineStyle::setLabelFontPt(double v)
{
    v = std::clamp(v, 4.0, 72.0);
    if (qFuzzyCompare(m_labelFontPt + 1.0, v + 1.0)) return;
    m_labelFontPt = v;
    setDirty();
}
void IsolineStyle::setLabelHalo(bool v)                 { if (m_labelHalo != v) { m_labelHalo = v; setDirty(); } }

std::vector<double> IsolineStyle::levelsForRange(double vMin, double vMax,
                                                 const QVector<double> &samples) const
{
    std::vector<double> out;
    if (!(vMax > vMin)) return out;

    if (m_levelMode == LevelMode::Count) {
        // Slice US.2 — interior levels from the scheme. EqualInterval matches
        // the legacy even spacing exactly; data-driven methods bin against
        // the supplied samples (empty → degrade to even spacing).
        const QVector<double> levels = m_scheme.interiorLevels(vMin, vMax, samples);
        out.assign(levels.cbegin(), levels.cend());
        return out;
    }

    // FixedInterval — levels at baseLevel + k·interval strictly inside the
    // range. 256-level cap guards against an interval orders of magnitude
    // smaller than the data range locking up the paint pass.
    const double interval = std::max(m_levelInterval, 1e-12);
    double first = m_baseLevel
        + std::ceil((vMin - m_baseLevel) / interval) * interval;
    if (first <= vMin) first += interval;
    constexpr int kMaxLevels = 256;
    for (double level = first; level < vMax && int(out.size()) < kMaxLevels;
         level += interval)
        out.push_back(level);
    return out;
}

QJsonObject IsolineStyle::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("attribute"),     m_attribute);
    obj.insert(QStringLiteral("levelMode"),     int(m_levelMode));
    obj.insert(QStringLiteral("classification"), m_scheme.toJson());
    obj.insert(QStringLiteral("levelInterval"), m_levelInterval);
    obj.insert(QStringLiteral("baseLevel"),     m_baseLevel);
    obj.insert(QStringLiteral("lineWidthPx"),   m_lineWidthPx);
    obj.insert(QStringLiteral("color"),         m_color.name(QColor::HexArgb));
    obj.insert(QStringLiteral("indexEvery"),    m_indexEvery);
    obj.insert(QStringLiteral("indexWidthPx"),  m_indexWidthPx);
    obj.insert(QStringLiteral("labels"),        m_labels);
    obj.insert(QStringLiteral("labelDecimals"), m_labelDecimals);
    obj.insert(QStringLiteral("labelFontPt"),   m_labelFontPt);
    obj.insert(QStringLiteral("labelHalo"),     m_labelHalo);

    const QMetaEnum me = QMetaEnum::fromType<Qt::PenStyle>();
    obj.insert(QStringLiteral("dashPattern"),
               QString::fromLatin1(me.valueToKey(static_cast<int>(m_dashPattern))));
    return obj;
}

void IsolineStyle::fromJson(const QJsonObject &j)
{
    m_attribute     = j.value(QStringLiteral("attribute")).toString(m_attribute);
    if (j.contains(QStringLiteral("classification")))
        m_scheme = ClassificationScheme::fromJson(j.value(QStringLiteral("classification")).toObject());
    m_lineWidthPx = j.value(QStringLiteral("lineWidthPx")).toDouble(m_lineWidthPx);
    if (m_lineWidthPx < 0.0) m_lineWidthPx = 0.0;
    m_labels = j.value(QStringLiteral("labels")).toBool(m_labels);

    const int mode = j.value(QStringLiteral("levelMode")).toInt(int(m_levelMode));
    if (mode >= int(LevelMode::Count) && mode <= int(LevelMode::FixedInterval))
        m_levelMode = LevelMode(mode);
    m_levelInterval = j.value(QStringLiteral("levelInterval")).toDouble(m_levelInterval);
    if (m_levelInterval <= 0.0) m_levelInterval = 1e-6;
    m_baseLevel  = j.value(QStringLiteral("baseLevel")).toDouble(m_baseLevel);
    m_indexEvery = j.value(QStringLiteral("indexEvery")).toInt(m_indexEvery);
    if (m_indexEvery < 0) m_indexEvery = 0;
    m_indexWidthPx = j.value(QStringLiteral("indexWidthPx")).toDouble(m_indexWidthPx);
    if (m_indexWidthPx < 0.1) m_indexWidthPx = 0.1;
    m_labelDecimals = std::clamp(
        j.value(QStringLiteral("labelDecimals")).toInt(m_labelDecimals), 0, 9);
    m_labelFontPt = std::clamp(
        j.value(QStringLiteral("labelFontPt")).toDouble(m_labelFontPt), 4.0, 72.0);
    m_labelHalo = j.value(QStringLiteral("labelHalo")).toBool(m_labelHalo);

    const QString colorTok = j.value(QStringLiteral("color")).toString();
    if (!colorTok.isEmpty()) { const QColor c(colorTok); if (c.isValid()) m_color = c; }

    const QString dashTok = j.value(QStringLiteral("dashPattern")).toString();
    if (!dashTok.isEmpty())
    {
        const QMetaEnum me = QMetaEnum::fromType<Qt::PenStyle>();
        bool ok = false;
        const int v = me.keyToValue(dashTok.toLatin1().constData(), &ok);
        if (ok) m_dashPattern = static_cast<Qt::PenStyle>(v);
    }
    setDirty();
}

IsolineSublayer::IsolineSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new IsolineStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void IsolineSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void IsolineSublayer::setOpacity(qreal o)
{
    if (o < 0.0) o = 0.0;
    if (o > 1.0) o = 1.0;
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> IsolineSublayer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.label      = QStringLiteral("%1 isolines (%2)").arg(m_style->attribute()).arg(m_style->isoValueCount());
    item.sublayerId = m_id;

    SymbolLayer line;
    line.kind = SymbolLayerKind::SimpleLine;
    SymbolProps::writeColor(line.props, QStringLiteral("color"), m_style->color());
    line.props.insert(QStringLiteral("width"), m_style->lineWidthPx());
    const QMetaEnum me = QMetaEnum::fromType<Qt::PenStyle>();
    line.props.insert(QStringLiteral("dashPattern"),
                      QString::fromLatin1(me.valueToKey(static_cast<int>(m_style->dashPattern()))));
    item.symbol.layers.append(line);
    item.symbol.opacity = m_opacity;
    return { item };
}

QSGNode *IsolineSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing;
}

} // namespace OpenSWMM::Render
