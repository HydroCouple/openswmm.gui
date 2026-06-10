/*!
 * \file   isolinesublayer.cpp
 * \brief  Slice S5.4.
 */
#include "render/sublayers/isolinesublayer.h"

#include <QJsonObject>
#include <QMetaEnum>

namespace OpenSWMM::Render
{

void IsolineStyle::setAttribute(const QString &v)        { if (m_attribute != v) { m_attribute = v; setDirty(); } }
void IsolineStyle::setIsoValueCount(int v)
{
    if (v < 1) v = 1;
    if (m_isoValueCount == v) return;
    m_isoValueCount = v;
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

QJsonObject IsolineStyle::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("attribute"),     m_attribute);
    obj.insert(QStringLiteral("isoValueCount"), m_isoValueCount);
    obj.insert(QStringLiteral("lineWidthPx"),   m_lineWidthPx);
    obj.insert(QStringLiteral("color"),         m_color.name(QColor::HexArgb));
    obj.insert(QStringLiteral("labels"),        m_labels);

    const QMetaEnum me = QMetaEnum::fromType<Qt::PenStyle>();
    obj.insert(QStringLiteral("dashPattern"),
               QString::fromLatin1(me.valueToKey(static_cast<int>(m_dashPattern))));
    return obj;
}

void IsolineStyle::fromJson(const QJsonObject &j)
{
    m_attribute     = j.value(QStringLiteral("attribute")).toString(m_attribute);
    m_isoValueCount = j.value(QStringLiteral("isoValueCount")).toInt(m_isoValueCount);
    if (m_isoValueCount < 1) m_isoValueCount = 1;
    m_lineWidthPx = j.value(QStringLiteral("lineWidthPx")).toDouble(m_lineWidthPx);
    if (m_lineWidthPx < 0.0) m_lineWidthPx = 0.0;
    m_labels = j.value(QStringLiteral("labels")).toBool(m_labels);

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
