/*!
 * \file   meshfillsublayer.cpp
 * \brief  Slice S5.1 — static mesh terrain sublayer.
 */
#include "render/sublayers/meshfillsublayer.h"

#include "mesh/meshcellparams.h"

#include <QJsonObject>

#include <iterator>

namespace OpenSWMM::Render
{

namespace {

/*! The one place CellAttribute meets mesh::cellParamSpecs(). Index is the
 *  enum ordinal; entry 0 is the elevation sentinel. */
constexpr const char *kAttrKeys[] = {
    "elevation", "mannings", "initDepth",
    "gw.Ks", "gw.zs", "gw.thetaS", "gw.hu0", "gw.hg0",
};
constexpr int kAttrKeyCount = int(std::size(kAttrKeys));

} // namespace

QByteArray MeshFillStyle::attributeKey(CellAttribute a)
{
    const int i = int(a);
    return QByteArray(kAttrKeys[(i >= 0 && i < kAttrKeyCount) ? i : 0]);
}

MeshFillStyle::CellAttribute MeshFillStyle::attributeFromKey(const QByteArray &key)
{
    for (int i = 0; i < kAttrKeyCount; ++i)
        if (key == kAttrKeys[i]) return CellAttribute(i);
    return CellAttribute::Elevation;
}

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

void MeshFillStyle::setColorByAttribute(CellAttribute v)
{
    if (m_colorByAttribute == v) return;
    m_colorByAttribute = v;

    // Drop any pinned range: attributes differ by orders of magnitude
    // (elevation ~10^1 m, Manning's n ~10^-2), so a range pinned for the old
    // attribute collapses the new one into a single class. Clearing it makes
    // the renderer re-derive the range from the new attribute's data.
    if (m_scheme.useCustomRange()) {
        ClassificationScheme s = m_scheme;
        s.setUseCustomRange(false);
        m_scheme = s;
    }
    setDirty();
}

void MeshFillStyle::setNoDataColor(const QColor &v)
{
    if (m_noDataColor == v) return;
    m_noDataColor = v;
    setDirty();
}

QJsonObject MeshFillStyle::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("fillColor"),         m_fillColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("hillshadeStrength"), m_hillshadeStrength);
    obj.insert(QStringLiteral("useElevationRamp"),  m_useElevationRamp);
    // Persisted by key, not ordinal, so CellAttribute can be reordered.
    obj.insert(QStringLiteral("colorByAttribute"),
               QString::fromUtf8(attributeKey(m_colorByAttribute)));
    obj.insert(QStringLiteral("noDataColor"),       m_noDataColor.name(QColor::HexArgb));
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

    // Absent in pre-change style files → stays Elevation → historic render.
    {
        const QString a = j.value(QStringLiteral("colorByAttribute")).toString();
        if (!a.isEmpty()) m_colorByAttribute = attributeFromKey(a.toUtf8());
    }
    {
        const QString tokNd = j.value(QStringLiteral("noDataColor")).toString();
        if (!tokNd.isEmpty()) { const QColor c(tokNd); if (c.isValid()) m_noDataColor = c; }
    }

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

void MeshFillSublayer::setAttributeHasData(bool hasData)
{
    if (m_attributeHasData == hasData) return;
    m_attributeHasData = hasData;
    emit invalidated();
}

QList<LegendSymbolItem> MeshFillSublayer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.sublayerId = m_id;

    if (!m_style->useElevationRamp()) {
        item.label = tr("Terrain fill");
    } else if (m_style->colorsByElevation()) {
        item.label = tr("Terrain elevation");
    } else {
        // cellParamLabel() carries the registry's translated label; the unit
        // suffix needs the project depth unit, which the layer pushes in.
        item.label = mesh::cellParamLabel(m_style->colorByAttributeKey(),
                                          m_depthUnitLabel);
    }

    SymbolLayer fill;
    fill.kind = SymbolLayerKind::SimpleFill;

    // A selected attribute with no data at all (every gw.* key today) must
    // not render as a plausible-looking uniform fill — say so.
    if (m_style->useElevationRamp() && !m_style->colorsByElevation() && !m_attributeHasData) {
        item.label = tr("%1 — no data (engine support pending)").arg(item.label);
        SymbolProps::writeColor(fill.props, QStringLiteral("color"),
                                m_style->noDataColor());
    } else {
        SymbolProps::writeColor(fill.props, QStringLiteral("color"),
                                m_style->fillColor());
    }

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
