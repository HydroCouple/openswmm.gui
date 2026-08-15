/*!
 * \file   meshfillsublayer.cpp
 * \brief  Slice S5.1 — static mesh terrain sublayer.
 */
#include "render/sublayers/meshfillsublayer.h"

#include <QJsonObject>

namespace OpenSWMM::Render
{

MeshFillStyle::MeshFillStyle(QObject *parent) : SublayerStyle(parent)
{
    // Default to a smooth (Continuous) elevation ramp so terrain fill keeps
    // its historic graduated look. The default names the "Terrain" builtin —
    // the same 5-stop palette the renderer used to hard-code — so the editor's
    // ramp combo and the renderer agree on the default. The renderer still
    // takes the byte-identical legacy path for this ramp; it switches to the
    // scheme's colour only once the user picks a *different* ramp, inverts it,
    // or chooses Classified mode (which bins the bed elevation).
    m_scheme.setMode(ClassificationScheme::ClassMode::Continuous);
    m_scheme.setRampName(QStringLiteral("Terrain"));
    m_scheme.setClassCount(5);
}

void MeshFillStyle::setScheme(const ClassificationScheme &s)
{
    if (m_scheme == s) return;
    m_scheme = s;
    setDirty();
}

void MeshFillStyle::setFillColor(const QColor &v)
{
    if (m_fillColor == v) return;
    m_fillColor = v;
    setDirty();
}

void MeshFillStyle::setHillshadeStrength(double v)
{
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    if (qFuzzyCompare(m_hillshadeStrength + 1.0, v + 1.0)) return;
    m_hillshadeStrength = v;
    setDirty();
}

void MeshFillStyle::setUseElevationRamp(bool v)
{
    if (m_useElevationRamp == v) return;
    m_useElevationRamp = v;
    setDirty();
}

QJsonObject MeshFillStyle::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("fillColor"),         m_fillColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("hillshadeStrength"), m_hillshadeStrength);
    obj.insert(QStringLiteral("useElevationRamp"),  m_useElevationRamp);
    obj.insert(QStringLiteral("classification"),    m_scheme.toJson());
    return obj;
}

void MeshFillStyle::fromJson(const QJsonObject &j)
{
    const QString tok = j.value(QStringLiteral("fillColor")).toString();
    if (!tok.isEmpty()) { const QColor c(tok); if (c.isValid()) m_fillColor = c; }

    double h = j.value(QStringLiteral("hillshadeStrength")).toDouble(m_hillshadeStrength);
    if (h < 0.0) h = 0.0;
    if (h > 1.0) h = 1.0;
    m_hillshadeStrength = h;

    m_useElevationRamp = j.value(QStringLiteral("useElevationRamp")).toBool(m_useElevationRamp);

    if (j.contains(QStringLiteral("classification")))
        m_scheme = ClassificationScheme::fromJson(j.value(QStringLiteral("classification")).toObject());

    setDirty();
}

MeshFillSublayer::MeshFillSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new MeshFillStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void MeshFillSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void MeshFillSublayer::setOpacity(qreal o)
{
    if (o < 0.0) o = 0.0;
    if (o > 1.0) o = 1.0;
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> MeshFillSublayer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.label      = m_style->useElevationRamp() ? tr("Terrain elevation")
                                                  : tr("Terrain fill");
    item.sublayerId = m_id;

    SymbolLayer fill;
    fill.kind = SymbolLayerKind::SimpleFill;
    SymbolProps::writeColor(fill.props, QStringLiteral("color"), m_style->fillColor());
    item.symbol.layers.append(fill);
    item.symbol.opacity = m_opacity;
    return { item };
}

QSGNode *MeshFillSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing; // Geometry wiring is deferred to the renderer slice.
}

} // namespace OpenSWMM::Render
