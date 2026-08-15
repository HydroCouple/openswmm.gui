/*!
 * \file   meshedgesublayer.cpp
 * \brief  Mesh wireframe-edge sublayer — style bag + sublayer plumbing.
 */
#include "render/sublayers/meshedgesublayer.h"

#include <QJsonObject>

namespace OpenSWMM::Render
{

MeshEdgeStyle::MeshEdgeStyle(QObject *parent) : SublayerStyle(parent)
{
    seedSchemeFromLegacy();
}

void MeshEdgeStyle::seedSchemeFromLegacy()
{
    // Mirror the legacy two-tier slope split as a 2-class scheme over the
    // normalised slope range [0,1] (slope / maxSlope). The single interior
    // break sits at slopeBreak; low class = color, high class = wideColor.
    // The renderer keeps its dedicated thin/wide path while the scheme stays
    // at this default; once classCount > 2 (or the user edits it) the edge
    // pass colours by slope class instead.
    m_scheme.setMode(ClassificationScheme::ClassMode::Classified);
    m_scheme.setRampName(QString());
    m_scheme.setUseCustomRange(true);
    m_scheme.setRangeMin(0.0);
    m_scheme.setRangeMax(1.0);
    m_scheme.setMethod(BinMethod::Manual);
    m_scheme.setManualBreaks({ m_slopeBreak });
    m_scheme.setClassCount(2);
    m_scheme.setLowColor(m_color);
    m_scheme.setHighColor(m_wideColor);
    m_scheme.setColorOverride(0, m_color);
    m_scheme.setColorOverride(1, m_wideColor);
}

void MeshEdgeStyle::setScheme(const ClassificationScheme &s)
{
    if (m_scheme == s) return;
    m_scheme = s;
    setDirty();
}

void MeshEdgeStyle::setLineWidthPx(double v)
{
    v = qBound(0.1, v, 20.0);
    if (qFuzzyCompare(m_lineWidthPx + 1.0, v + 1.0)) return;
    m_lineWidthPx = v;
    setDirty();
}

void MeshEdgeStyle::setSlopeBreak(double v)
{
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_slopeBreak + 1.0, v + 1.0)) return;
    m_slopeBreak = v;
    setDirty();
}

void MeshEdgeStyle::setWideWidthPx(double v)
{
    v = qBound(0.1, v, 20.0);
    if (qFuzzyCompare(m_wideWidthPx + 1.0, v + 1.0)) return;
    m_wideWidthPx = v;
    setDirty();
}

QJsonObject MeshEdgeStyle::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("color")]               = m_color.name(QColor::HexArgb);
    j[QStringLiteral("lineWidthPx")]         = m_lineWidthPx;
    j[QStringLiteral("dashPattern")]         = int(m_dashPattern);
    j[QStringLiteral("useSlopeDrivenWidth")] = m_useSlopeDrivenWidth;
    j[QStringLiteral("slopeBreak")]          = m_slopeBreak;
    j[QStringLiteral("wideWidthPx")]         = m_wideWidthPx;
    j[QStringLiteral("wideColor")]           = m_wideColor.name(QColor::HexArgb);
    j[QStringLiteral("classification")]      = m_scheme.toJson();
    return j;
}

void MeshEdgeStyle::fromJson(const QJsonObject &j)
{
    if (j.contains(QStringLiteral("color"))) {
        const QColor c(j.value(QStringLiteral("color")).toString());
        if (c.isValid()) m_color = c;
    }
    if (j.contains(QStringLiteral("lineWidthPx")))
        m_lineWidthPx = qBound(0.1, j.value(QStringLiteral("lineWidthPx")).toDouble(m_lineWidthPx), 20.0);
    if (j.contains(QStringLiteral("dashPattern")))
        m_dashPattern = static_cast<Qt::PenStyle>(j.value(QStringLiteral("dashPattern")).toInt(int(Qt::SolidLine)));
    if (j.contains(QStringLiteral("useSlopeDrivenWidth")))
        m_useSlopeDrivenWidth = j.value(QStringLiteral("useSlopeDrivenWidth")).toBool(m_useSlopeDrivenWidth);
    if (j.contains(QStringLiteral("slopeBreak")))
        m_slopeBreak = qBound(0.0, j.value(QStringLiteral("slopeBreak")).toDouble(m_slopeBreak), 1.0);
    if (j.contains(QStringLiteral("wideWidthPx")))
        m_wideWidthPx = qBound(0.1, j.value(QStringLiteral("wideWidthPx")).toDouble(m_wideWidthPx), 20.0);
    if (j.contains(QStringLiteral("wideColor"))) {
        const QColor c(j.value(QStringLiteral("wideColor")).toString());
        if (c.isValid()) m_wideColor = c;
    }

    if (j.contains(QStringLiteral("classification")))
        m_scheme = ClassificationScheme::fromJson(j.value(QStringLiteral("classification")).toObject());
    else
        seedSchemeFromLegacy();   // older file — reproduce the legacy 2-tier look

    setDirty();
}

MeshEdgeSublayer::MeshEdgeSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new MeshEdgeStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void MeshEdgeSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void MeshEdgeSublayer::setOpacity(qreal o)
{
    o = qBound(0.0, o, 1.0);
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> MeshEdgeSublayer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.label      = tr("Mesh Edges");
    item.sublayerId = m_id;

    SymbolLayer line;
    line.kind = SymbolLayerKind::SimpleLine;
    SymbolProps::writeColor(line.props, QStringLiteral("color"), m_style->color());
    line.props.insert(QStringLiteral("widthPx"), m_style->lineWidthPx());
    item.symbol.layers.append(line);
    item.symbol.opacity = m_opacity;
    return { item };
}

QSGNode *MeshEdgeSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing; // QSG geometry is built in SWMM2DMeshQSGRenderer (style is consumed there).
}

} // namespace OpenSWMM::Render
