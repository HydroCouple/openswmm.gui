/*!
 * \file   meshedgesublayer.cpp
 * \brief  Mesh wireframe-edge sublayer — style bag + sublayer plumbing.
 */
#include "render/sublayers/meshedgesublayer.h"

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

/*! JSON keys for the seven BC colours, indexed by enum value. Must stay in
 *  step with the Q_PROPERTY names so a style file is self-describing. */
constexpr const char *kBcColorKeys[MeshEdgeStyle::kBcTypeCount] = {
    "bcWallColor", "bcNormalFlowColor", "bcStageConstColor", "bcStageTSColor",
    "bcFlowConstColor", "bcFlowTSColor", "bcRatingCurveColor",
};

/*! JSON keys for the six per-type BC widths. Index 0 (Wall) is null: Wall
 *  edges are the interior wireframe and carry lineWidthPx, so there is no
 *  width to persist for them. */
constexpr const char *kBcWidthKeys[MeshEdgeStyle::kBcTypeCount] = {
    nullptr, "bcNormalFlowWidthPx", "bcStageConstWidthPx", "bcStageTSWidthPx",
    "bcFlowConstWidthPx", "bcFlowTSWidthPx", "bcRatingCurveWidthPx",
};

} // namespace

MeshEdgeStyle::MeshEdgeStyle(QObject *parent) : SublayerStyle(parent)
{
    // Tab10-derived defaults, assigned so the semantics read off the map:
    // stage BCs are cool (blue/cyan), flow BCs are warm (orange/pink),
    // normal-flow is green, rating-curve purple. Wall inherits the plain
    // edge colour so switching colorByBC on does not repaint the interior.
    m_bcColors[0] = m_color;                          // Wall
    m_bcColors[1] = QColor(0x2c, 0xa0, 0x2c, 230);    // Normal flow      — green
    m_bcColors[2] = QColor(0x1f, 0x77, 0xb4, 235);    // Stage (const)    — blue
    m_bcColors[3] = QColor(0x17, 0xbe, 0xcf, 235);    // Stage (series)   — cyan
    m_bcColors[4] = QColor(0xff, 0x7f, 0x0e, 235);    // Flow (const)     — orange
    m_bcColors[5] = QColor(0xe3, 0x77, 0xc2, 235);    // Flow (series)    — pink
    m_bcColors[6] = QColor(0x94, 0x67, 0xbd, 235);    // Rating curve     — purple

    // Wall keeps the wireframe's own width (it IS the wireframe); the six
    // boundary types default wide enough to read at a whole-domain zoom,
    // where the interior wireframe is suppressed entirely and the ring is
    // the only thing on screen.
    m_bcWidths[0] = m_lineWidthPx;                    // Wall — unused by the renderer
    m_bcWidths[1] = 2.0;                              // Normal flow
    m_bcWidths[2] = 2.4;                              // Stage (const)
    m_bcWidths[3] = 2.4;                              // Stage (series)
    m_bcWidths[4] = 2.4;                              // Flow (const)
    m_bcWidths[5] = 2.4;                              // Flow (series)
    m_bcWidths[6] = 2.4;                              // Rating curve

    seedSchemeFromLegacy();
}

void MeshEdgeStyle::setBcColor(int idx, const QColor &v)
{
    if (idx < 0 || idx >= kBcTypeCount) return;
    if (m_bcColors[idx] == v) return;
    m_bcColors[idx] = v;
    setDirty();
}

void MeshEdgeStyle::setBcWidth(int idx, double v)
{
    if (idx < 0 || idx >= kBcTypeCount) return;
    v = qBound(0.1, v, 20.0);
    if (qFuzzyCompare(m_bcWidths[idx] + 1.0, v + 1.0)) return;
    m_bcWidths[idx] = v;
    setDirty();
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

    j[QStringLiteral("colorByBC")] = m_colorByBC;
    for (int i = 0; i < kBcTypeCount; ++i) {
        j[QLatin1String(kBcColorKeys[i])] = m_bcColors[i].name(QColor::HexArgb);
        if (kBcWidthKeys[i])
            j[QLatin1String(kBcWidthKeys[i])] = m_bcWidths[i];
    }
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

    // BC colouring — absent keys keep the ctor defaults, so a pre-BC v1 style
    // file loads with colorByBC == false and renders exactly as it used to.
    if (j.contains(QStringLiteral("colorByBC")))
        m_colorByBC = j.value(QStringLiteral("colorByBC")).toBool(m_colorByBC);
    // Migration: styles written before per-type widths carry a single
    // "bcWidthPx". Seed all six boundary types from it so an existing file
    // reloads looking exactly as it did, then let the per-type keys below
    // override where present.
    if (j.contains(QStringLiteral("bcWidthPx"))) {
        const double legacy =
            qBound(0.1, j.value(QStringLiteral("bcWidthPx")).toDouble(1.60), 20.0);
        for (int i = 1; i < kBcTypeCount; ++i) m_bcWidths[i] = legacy;
    }
    for (int i = 0; i < kBcTypeCount; ++i) {
        const QLatin1String key(kBcColorKeys[i]);
        if (j.contains(key)) {
            const QColor c(j.value(key).toString());
            if (c.isValid()) m_bcColors[i] = c;
        }
        if (!kBcWidthKeys[i]) continue;
        const QLatin1String wkey(kBcWidthKeys[i]);
        if (j.contains(wkey))
            m_bcWidths[i] = qBound(0.1, j.value(wkey).toDouble(m_bcWidths[i]), 20.0);
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

void MeshEdgeSublayer::setBcTypesPresent(const QSet<int> &types)
{
    QSet<int> next = types;
    next.insert(0);   // Wall always has a row — it governs the mesh interior
    if (m_bcTypesPresent == next) return;
    m_bcTypesPresent = next;
    // Only the legend depends on this, but invalidated() is the sublayer's
    // single change channel and a legend rebuild is cheap.
    emit invalidated();
}

QList<LegendSymbolItem> MeshEdgeSublayer::legendSymbolItems() const
{
    // BC mode: one row per BC type actually present in the mesh. Emitting all
    // seven unconditionally would put five dead rows in the dock on a typical
    // model, so the layer pushes the present set via setBcTypesPresent().
    if (m_style->colorByBC()) {
        QList<LegendSymbolItem> out;
        for (int t = 0; t < MeshEdgeStyle::kBcTypeCount; ++t) {
            if (!m_bcTypesPresent.contains(t)) continue;

            LegendSymbolItem item;
            item.label      = mesh::MeshBCTypes::label(mesh::MeshBCTypes::Type(t));
            item.sublayerId = m_id;

            SymbolLayer line;
            line.kind = SymbolLayerKind::SimpleLine;
            SymbolProps::writeColor(line.props, QStringLiteral("color"),
                                    m_style->bcColorForType(t));
            line.props.insert(QStringLiteral("widthPx"),
                              (t == 0) ? m_style->lineWidthPx()
                                       : m_style->bcWidthForType(t));
            item.symbol.layers.append(line);
            item.symbol.opacity = m_opacity;
            item.classKey  = QString::number(t);
            item.sortIndex = t;
            out.append(item);
        }
        return out;
    }

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
