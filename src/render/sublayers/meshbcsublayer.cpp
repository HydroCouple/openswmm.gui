/*!
 * \file   meshbcsublayer.cpp
 * \brief  Boundary-condition indicator sublayer — style bag + plumbing.
 */
#include "render/sublayers/meshbcsublayer.h"

#include "mesh/meshbctype.h"

#include <QJsonObject>

namespace OpenSWMM::Render
{

// The style bag indexes BC colours by the raw enum value so the render layer
// never has to include the mesh enum in its header. Lock the two together.
static_assert(int(mesh::MeshBCTypes::Type::Wall)                == 0, "BC enum order changed");
static_assert(int(mesh::MeshBCTypes::Type::NormalFlow)          == 1, "BC enum order changed");
static_assert(int(mesh::MeshBCTypes::Type::SpecifiedStageConst) == 2, "BC enum order changed");
static_assert(int(mesh::MeshBCTypes::Type::SpecifiedStageTS)    == 3, "BC enum order changed");
static_assert(int(mesh::MeshBCTypes::Type::SpecifiedFlowConst)  == 4, "BC enum order changed");
static_assert(int(mesh::MeshBCTypes::Type::SpecifiedFlowTS)     == 5, "BC enum order changed");
static_assert(int(mesh::MeshBCTypes::Type::RatingCurve)         == 6, "BC enum order changed");

namespace {

/*! JSON keys, indexed by enum value. Must stay in step with the Q_PROPERTY
 *  names so a style file is self-describing. The width key is null for Wall
 *  (Wall edges are the wireframe and carry MeshEdgeStyle's widths). */
constexpr const char *kColorKeys[MeshBcStyle::kBcTypeCount] = {
    "wallColor", "normalFlowColor", "stageConstColor", "stageTSColor",
    "flowConstColor", "flowTSColor", "ratingCurveColor",
};
constexpr const char *kWidthKeys[MeshBcStyle::kBcTypeCount] = {
    nullptr, "normalFlowWidthPx", "stageConstWidthPx", "stageTSWidthPx",
    "flowConstWidthPx", "flowTSWidthPx", "ratingCurveWidthPx",
};
constexpr const char *kVisibleKeys[MeshBcStyle::kBcTypeCount] = {
    "wallVisible", "normalFlowVisible", "stageConstVisible", "stageTSVisible",
    "flowConstVisible", "flowTSVisible", "ratingCurveVisible",
};

/*! Legacy (pre-split) MeshEdgeStyle key sets, for seedFromLegacyEdgeJson. */
constexpr const char *kLegacyColorKeys[MeshBcStyle::kBcTypeCount] = {
    "bcWallColor", "bcNormalFlowColor", "bcStageConstColor", "bcStageTSColor",
    "bcFlowConstColor", "bcFlowTSColor", "bcRatingCurveColor",
};
constexpr const char *kLegacyWidthKeys[MeshBcStyle::kBcTypeCount] = {
    nullptr, "bcNormalFlowWidthPx", "bcStageConstWidthPx", "bcStageTSWidthPx",
    "bcFlowConstWidthPx", "bcFlowTSWidthPx", "bcRatingCurveWidthPx",
};

} // namespace

MeshBcStyle::MeshBcStyle(QObject *parent) : SublayerStyle(parent)
{
    // Tab10-derived defaults, identical to the pre-split MeshEdgeStyle set:
    // stage BCs are cool (blue/cyan), flow BCs are warm (orange/pink),
    // normal-flow green, rating-curve purple. Wall mirrors the default
    // wireframe colour so recolouring the interior is a no-op until edited.
    m_colors[0] = QColor(0, 0, 0, 130);            // Wall
    m_colors[1] = QColor(0x2c, 0xa0, 0x2c, 230);   // Normal flow      — green
    m_colors[2] = QColor(0x1f, 0x77, 0xb4, 235);   // Stage (const)    — blue
    m_colors[3] = QColor(0x17, 0xbe, 0xcf, 235);   // Stage (series)   — cyan
    m_colors[4] = QColor(0xff, 0x7f, 0x0e, 235);   // Flow (const)     — orange
    m_colors[5] = QColor(0xe3, 0x77, 0xc2, 235);   // Flow (series)    — pink
    m_colors[6] = QColor(0x94, 0x67, 0xbd, 235);   // Rating curve     — purple

    // Boundary types default wide enough to read at a whole-domain zoom,
    // where the interior wireframe is suppressed and the ring is the only
    // thing on screen. Wall's slot is unused by the renderer.
    m_widths[0] = 0.35;
    m_widths[1] = 2.0;
    for (int i = 2; i < kBcTypeCount; ++i) m_widths[i] = 2.4;

    for (int i = 0; i < kBcTypeCount; ++i) m_typeVisible[i] = true;
}

void MeshBcStyle::setBcColor(int idx, const QColor &v)
{
    if (idx < 0 || idx >= kBcTypeCount) return;
    if (m_colors[idx] == v) return;
    m_colors[idx] = v;
    setDirty();
}

void MeshBcStyle::setBcWidth(int idx, double v)
{
    if (idx < 0 || idx >= kBcTypeCount) return;
    v = qBound(0.1, v, 20.0);
    if (qFuzzyCompare(m_widths[idx] + 1.0, v + 1.0)) return;
    m_widths[idx] = v;
    setDirty();
}

void MeshBcStyle::setBcTypeVisible(int idx, bool v)
{
    if (idx < 0 || idx >= kBcTypeCount) return;
    if (m_typeVisible[idx] == v) return;
    m_typeVisible[idx] = v;
    setDirty();
}

QJsonObject MeshBcStyle::toJson() const
{
    QJsonObject j;
    for (int i = 0; i < kBcTypeCount; ++i) {
        j[QLatin1String(kColorKeys[i])]   = m_colors[i].name(QColor::HexArgb);
        j[QLatin1String(kVisibleKeys[i])] = m_typeVisible[i];
        if (kWidthKeys[i])
            j[QLatin1String(kWidthKeys[i])] = m_widths[i];
    }
    return j;
}

void MeshBcStyle::fromJson(const QJsonObject &j)
{
    for (int i = 0; i < kBcTypeCount; ++i) {
        const QLatin1String ckey(kColorKeys[i]);
        if (j.contains(ckey)) {
            const QColor c(j.value(ckey).toString());
            if (c.isValid()) m_colors[i] = c;
        }
        const QLatin1String vkey(kVisibleKeys[i]);
        if (j.contains(vkey))
            m_typeVisible[i] = j.value(vkey).toBool(m_typeVisible[i]);
        if (!kWidthKeys[i]) continue;
        const QLatin1String wkey(kWidthKeys[i]);
        if (j.contains(wkey))
            m_widths[i] = qBound(0.1, j.value(wkey).toDouble(m_widths[i]), 20.0);
    }
    setDirty();
}

void MeshBcStyle::seedFromLegacyEdgeJson(const QJsonObject &j)
{
    // Oldest form: one shared "bcWidthPx" for all boundary types.
    if (j.contains(QStringLiteral("bcWidthPx"))) {
        const double legacy =
            qBound(0.1, j.value(QStringLiteral("bcWidthPx")).toDouble(1.60), 20.0);
        for (int i = 1; i < kBcTypeCount; ++i) m_widths[i] = legacy;
    }
    for (int i = 0; i < kBcTypeCount; ++i) {
        const QLatin1String ckey(kLegacyColorKeys[i]);
        if (j.contains(ckey)) {
            const QColor c(j.value(ckey).toString());
            if (c.isValid()) m_colors[i] = c;
        }
        if (!kLegacyWidthKeys[i]) continue;
        const QLatin1String wkey(kLegacyWidthKeys[i]);
        if (j.contains(wkey))
            m_widths[i] = qBound(0.1, j.value(wkey).toDouble(m_widths[i]), 20.0);
    }
    // Legacy styles had no per-type visibility — everything shown.
    for (int i = 0; i < kBcTypeCount; ++i) m_typeVisible[i] = true;
    setDirty();
}

MeshBcSublayer::MeshBcSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new MeshBcStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void MeshBcSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void MeshBcSublayer::setOpacity(qreal o)
{
    o = qBound(0.0, o, 1.0);
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o;
    emit invalidated();
}

void MeshBcSublayer::setBcTypesPresent(const QSet<int> &types)
{
    QSet<int> next = types;
    next.insert(0);   // Wall always has a row — it governs the mesh interior
    if (m_bcTypesPresent == next) return;
    m_bcTypesPresent = next;
    // Only the legend depends on this, but invalidated() is the sublayer's
    // single change channel and a legend rebuild is cheap.
    emit invalidated();
}

void MeshBcSublayer::setWallLegendWidthPx(double px)
{
    px = qBound(0.1, px, 20.0);
    if (qFuzzyCompare(m_wallLegendWidthPx + 1.0, px + 1.0)) return;
    m_wallLegendWidthPx = px;
    emit invalidated();
}

QList<LegendSymbolItem> MeshBcSublayer::legendSymbolItems() const
{
    // One row per BC type actually present in the mesh AND visible in the
    // style. Emitting all seven unconditionally would put five dead rows in
    // the dock on a typical model.
    QList<LegendSymbolItem> out;
    for (int t = 0; t < MeshBcStyle::kBcTypeCount; ++t) {
        if (!m_bcTypesPresent.contains(t)) continue;
        if (!m_style->bcTypeVisible(t)) continue;

        LegendSymbolItem item;
        item.label      = mesh::MeshBCTypes::label(mesh::MeshBCTypes::Type(t));
        item.sublayerId = m_id;

        SymbolLayer line;
        line.kind = SymbolLayerKind::SimpleLine;
        SymbolProps::writeColor(line.props, QStringLiteral("color"),
                                m_style->bcColorForType(t));
        line.props.insert(QStringLiteral("widthPx"),
                          (t == 0) ? m_wallLegendWidthPx
                                   : m_style->bcWidthForType(t));
        item.symbol.layers.append(line);
        item.symbol.opacity = m_opacity;
        item.classKey  = QString::number(t);
        item.sortIndex = t;
        out.append(item);
    }
    return out;
}

QSGNode *MeshBcSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing; // QSG geometry is built in SWMM2DMeshQSGRenderer (style is consumed there).
}

} // namespace OpenSWMM::Render
