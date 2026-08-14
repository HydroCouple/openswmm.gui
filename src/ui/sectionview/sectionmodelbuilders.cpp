/*!
 * \file   sectionmodelbuilders.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/sectionview/sectionmodelbuilders.h"

#include "ui/properties/xsectshapegeom.h"
#include "ui/sectionview/xsectsampler.h"

#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>

#include <QCoreApplication>
#include <QVector>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace openswmmvis::sectionview {

namespace {

//! Translations for free functions live under one context.
inline QString tr_(const char *s)
{
    return QCoreApplication::translate("openswmmvis::sectionview", s);
}

//! Consistent numeric formatting for every dimension / elevation label.
inline QString num(double v, int decimals = 3)
{
    return QString::number(v, 'f', decimals);
}

inline QString lenText(double v, const DiagramUnits &u, int decimals = 3)
{
    return QStringLiteral("%1 %2").arg(num(v, decimals), u.lengthLabel);
}

inline QString idOf(const char *raw)
{
    return raw ? QString::fromUtf8(raw) : QString();
}

/*! geom1 of these shapes is an index into the transect / street / curve
 *  tables, not a dimension, so the geometry has to be fetched from the table
 *  the index points at. */
inline bool isTabulatedShape(int shape)
{
    return shape == SWMM_XSECT_IRREGULAR
        || shape == SWMM_XSECT_STREET
        || shape == SWMM_XSECT_CUSTOM;
}

/*!
 * Rebuild a STREET section from the engine's own [STREETS] parameters.
 *
 * For STREET — and ONLY for STREET — geom1 really does carry the table index:
 * swmm_link_get_xsect has a dedicated branch that resolves the retained street
 * name back to an index. Verified against the live engine with
 * tests/scratch/sp_geom1_probe.inp (link C4 → ST_B, the second street → g1 = 1).
 *
 * Preferred over swmm_link_create_xsect() because swmm_street_get_params reads
 * stored input and therefore works in EVERY lifecycle state, whereas the
 * link-derived handle needs resolved geometry and returns SWMM_ERR_LIFECYCLE
 * while the model is still being edited — i.e. exactly when the user is
 * looking at the preview.
 */
XsectSampler samplerFromStreetIndex(SWMM_Engine engine, int streetIdx, bool si)
{
    if (!engine || streetIdx < 0 || streetIdx >= swmm_street_count(engine))
        return {};

    double tCrown = 0.0, hCurb = 0.0, sx = 0.0, nRoad = 0.0;
    double gutterDepress = 0.0, gutterWidth = 0.0;
    int    sides = 1;
    double backWidth = 0.0, backSlope = 0.0, backN = 0.0;

    if (swmm_street_get_params(engine, streetIdx, &tCrown, &hCurb, &sx, &nRoad,
                               &gutterDepress, &gutterWidth, &sides,
                               &backWidth, &backSlope, &backN) != SWMM_OK)
        return {};

    return XsectSampler::fromStreet(tCrown, hCurb, sx, nRoad,
                                    gutterDepress, gutterWidth, sides,
                                    backWidth, backSlope, backN, si);
}

/*!
 * Build a sampler for a link:
 *   - self-contained shapes → rebuilt from the stored geoms (works always);
 *   - STREET                → rebuilt from [STREETS], which geom1 indexes;
 *   - IRREGULAR / CUSTOM    → only the link-derived handle, which needs the
 *                             model to have resolved geometry.
 *
 * \warning geom1 is a table index for STREET **only**. For IRREGULAR the
 * engine does NOT round-trip the transect index: the [XSECTIONS] parser skips
 * populating geom1..4 for irregular sections, and swmm_link_get_xsect (which
 * has a name→index branch for STREET but none for IRREGULAR) falls through to
 * reporting derived geometry instead — g1 = full depth, g2 = max width,
 * g3 = area. Verified with tests/scratch/sp_geom1_probe.inp: three conduits on
 * three different transects returned g1 = 5 / 9 / 3, their depths, while the
 * street conduit correctly returned its index. Treating g1 as an index here
 * would silently draw a DIFFERENT transect whenever the depth happened to land
 * inside [0, transectCount) — worse than drawing nothing. See the handoff's
 * "engine gaps" note; closing this needs an engine-side getter.
 *
 * CUSTOM is blocked separately: its geom2 indexes a SHAPE curve, and the
 * engine's own header documents two conflicting type codes for that curve kind
 * (openswmm_tables.h:79 says 4 = CURVE_SHAPE, :107 says 5 = SHAPE), so reading
 * the curve points directly would be guesswork.
 */
XsectSampler samplerForLink(SWMM_Engine engine, int linkIdx, int shape,
                            double g1, double g2, double g3, double g4,
                            bool si)
{
    if (shape == SWMM_XSECT_IRREGULAR)
        return XsectSampler::fromLink(engine, linkIdx);

    if (shape == SWMM_XSECT_STREET) {
        XsectSampler s = samplerFromStreetIndex(
            engine, static_cast<int>(std::lround(g1)), si);
        if (s.isValid()) return s;
        return XsectSampler::fromLink(engine, linkIdx);
    }
    if (shape == SWMM_XSECT_CUSTOM)
        return XsectSampler::fromLink(engine, linkIdx);

    return XsectSampler::fromShape(shape, g1, g2, g3, g4, si);
}

/*! Section outline + the two dimensions every section carries. */
void addSectionGeometry(SectionDiagramModel &m, const XsectSampler &sampler,
                        const DiagramUnits &units, int highlightOrdinal)
{
    const QPolygonF outline = sampler.outline();
    if (outline.isEmpty()) return;

    const XsectFullProps fp = sampler.fullProps();

    DiagramPoly body;
    body.pts     = outline;
    body.role    = DiagramRole::Conduit;
    body.openTop = fp.open;
    m.polys << body;

    // Full-depth dimension, to the right of the section.
    DiagramDim depth;
    depth.from        = QPointF(0.5 * fp.wMax, 0.0);
    depth.to          = QPointF(0.5 * fp.wMax, fp.yFull);
    depth.text        = tr_("Depth %1").arg(lenText(fp.yFull, units));
    depth.pixelOffset = 34.0;   // +n = right of the section (see DiagramDim)
    depth.accent      = (highlightOrdinal == 1);
    m.dims << depth;

    // Max-width dimension, above the section.
    if (fp.wMax > 0.0) {
        DiagramDim width;
        width.from        = QPointF(-0.5 * fp.wMax, fp.yFull);
        width.to          = QPointF( 0.5 * fp.wMax, fp.yFull);
        width.text        = tr_("Width %1").arg(lenText(fp.wMax, units));
        width.pixelOffset = -26.0;
        width.accent      = (highlightOrdinal == 2);
        m.dims << width;
    }
}

QString sectionFooter(const XsectFullProps &fp, const DiagramUnits &u,
                      int barrels)
{
    QString s = tr_("A %1 %2²   R %3 %2   W %4 %2")
                    .arg(num(fp.aFull), u.lengthLabel,
                         num(fp.rFull), num(fp.wMax));
    if (barrels > 1) s += tr_("   Barrels %1").arg(barrels);
    if (fp.open)     s += tr_("   (open channel)");
    return s;
}

//! Shape name for display, falling back to the engine's own name table for
//! the ids the GUI's picker doesn't surface.
QString shapeDisplayName(int shape)
{
    const QString fromTable = openswmmvis::xsectShapeName(shape);
    if (!fromTable.isEmpty()) return fromTable;
    return tr_("SHAPE %1").arg(shape);
}

} // namespace

// ---------------------------------------------------------------------------
// Link cross-section
// ---------------------------------------------------------------------------

SectionDiagramModel buildLinkSection(SWMM_Engine engine, int linkIdx,
                                     const DiagramUnits &units)
{
    SectionDiagramModel m;
    m.uniformScale = true;

    if (!engine || linkIdx < 0) {
        m.emptyText = tr_("No link selected.");
        return m;
    }

    m.title = tr_("%1 — Cross-Section").arg(idOf(swmm_link_id(engine, linkIdx)));

    int linkType = SWMM_LINK_CONDUIT;
    swmm_link_get_type(engine, linkIdx, &linkType);
    if (linkType == SWMM_LINK_PUMP) {
        m.subtitle  = tr_("PUMP");
        m.emptyText = tr_("Pumps have no cross-section.");
        return m;
    }

    int shape = SWMM_XSECT_CIRCULAR;
    double g1 = 0.0, g2 = 0.0, g3 = 0.0, g4 = 0.0;
    if (swmm_link_get_xsect(engine, linkIdx, &shape, &g1, &g2, &g3, &g4) != SWMM_OK) {
        m.emptyText = tr_("Cross-section could not be read from the engine.");
        return m;
    }
    m.subtitle = shapeDisplayName(shape);
    // Only STREET's geom1 is a table index (see samplerForLink) — there is no
    // way to recover an irregular section's transect name from the engine, so
    // don't guess at one.
    if (shape == SWMM_XSECT_STREET) {
        const int sIdx = static_cast<int>(std::lround(g1));
        if (sIdx >= 0 && sIdx < swmm_street_count(engine))
            m.subtitle = tr_("STREET — %1").arg(idOf(swmm_street_id(engine, sIdx)));
    }

    if (shape == SWMM_XSECT_DUMMY) {
        m.emptyText = tr_("DUMMY sections carry no geometry.");
        return m;
    }

    const XsectSampler sampler =
        samplerForLink(engine, linkIdx, shape, g1, g2, g3, g4, units.si);
    if (!sampler.isValid()) {
        if (shape == SWMM_XSECT_CUSTOM || shape == SWMM_XSECT_IRREGULAR) {
            m.emptyText = tr_("%1 geometry comes from a table the engine only "
                              "resolves when the model is validated or run. "
                              "Open the cross-section editor to preview it now.")
                              .arg(shapeDisplayName(shape));
        } else if (isTabulatedShape(shape)) {
            m.emptyText = tr_("The %1 this section refers to could not be read.")
                              .arg(shapeDisplayName(shape));
        } else {
            m.emptyText = tr_("Cross-section geometry is incomplete.");
        }
        return m;
    }

    addSectionGeometry(m, sampler, units, /*highlightOrdinal=*/0);
    if (m.polys.isEmpty()) {
        m.emptyText = tr_("Cross-section geometry is degenerate.");
        return m;
    }

    const XsectFullProps fp = sampler.fullProps();

    // Invert / crown elevations, taken from the upstream node + offset so the
    // numbers match what the profile view and the property grid report.
    int upNode = -1;
    double offsetUp = 0.0, invertUp = 0.0;
    if (swmm_link_get_from_node(engine, linkIdx, &upNode) == SWMM_OK && upNode >= 0) {
        swmm_link_get_offset_up(engine, linkIdx, &offsetUp);
        swmm_node_get_invert_elev(engine, upNode, &invertUp);

        const double invertEl = invertUp + offsetUp;
        m.leaders << DiagramLeader{ QPointF(0.0, 0.0),
                                    tr_("Invert El. %1").arg(lenText(invertEl, units, 2)),
                                    QPointF(-70.0, 34.0) };
        m.leaders << DiagramLeader{ QPointF(0.0, fp.yFull),
                                    tr_("Crown El. %1").arg(lenText(invertEl + fp.yFull, units, 2)),
                                    QPointF(-70.0, -30.0) };
    }

    int barrels = 1;
    swmm_link_get_barrels(engine, linkIdx, &barrels);
    m.footer = sectionFooter(fp, units, barrels);

    return m;
}

// ---------------------------------------------------------------------------
// Link profile
// ---------------------------------------------------------------------------

SectionDiagramModel buildLinkProfile(SWMM_Engine engine, int linkIdx,
                                     const DiagramUnits &units)
{
    SectionDiagramModel m;
    m.uniformScale = false;   // 120 m of run against 3 m of depth.

    if (!engine || linkIdx < 0) {
        m.emptyText = tr_("No link selected.");
        return m;
    }

    const QString linkName = idOf(swmm_link_id(engine, linkIdx));
    m.title = tr_("%1 — Profile").arg(linkName);

    int upNode = -1, dnNode = -1;
    if (swmm_link_get_from_node(engine, linkIdx, &upNode) != SWMM_OK
        || swmm_link_get_to_node(engine, linkIdx, &dnNode) != SWMM_OK
        || upNode < 0 || dnNode < 0)
    {
        m.emptyText = tr_("Link end nodes could not be read.");
        return m;
    }

    const QString upName = idOf(swmm_node_id(engine, upNode));
    const QString dnName = idOf(swmm_node_id(engine, dnNode));
    m.subtitle = tr_("%1 → %2").arg(upName, dnName);

    double invUp = 0.0, invDn = 0.0, depthUp = 0.0, depthDn = 0.0;
    swmm_node_get_invert_elev(engine, upNode, &invUp);
    swmm_node_get_invert_elev(engine, dnNode, &invDn);
    swmm_node_get_max_depth(engine, upNode, &depthUp);
    swmm_node_get_max_depth(engine, dnNode, &depthDn);

    double length = 0.0, offUp = 0.0, offDn = 0.0;
    swmm_link_get_length(engine, linkIdx, &length);
    swmm_link_get_offset_up(engine, linkIdx, &offUp);
    swmm_link_get_offset_dn(engine, linkIdx, &offDn);
    // Orifices / weirs / outlets have no length; give them a nominal run so
    // the structures don't collapse onto each other.
    if (!(length > 0.0)) length = 1.0;

    // Barrel depth from the section (0 for a pump / DUMMY — drawn as a line).
    int shape = SWMM_XSECT_CIRCULAR;
    double g1 = 0.0, g2 = 0.0, g3 = 0.0, g4 = 0.0;
    double yFull = 0.0;
    if (swmm_link_get_xsect(engine, linkIdx, &shape, &g1, &g2, &g3, &g4) == SWMM_OK) {
        const XsectSampler s =
            samplerForLink(engine, linkIdx, shape, g1, g2, g3, g4, units.si);
        if (s.isValid()) yFull = s.fullProps().yFull;
    }

    const double rimUp = invUp + depthUp;
    const double rimDn = invDn + depthDn;
    const double pipeInvUp = invUp + offUp;
    const double pipeInvDn = invDn + offDn;

    // Structures are drawn `mw` wide in model x-units; the barrel spans
    // between their inner faces so the drawing reads like a real profile.
    const double mw = std::max(length * 0.035, 1.0e-6);
    const double x0 = 0.0, x1 = length;
    const double structTop = std::max(rimUp, rimDn);
    const double structBot = std::min({ invUp, invDn, pipeInvUp, pipeInvDn });

    auto structure = [&](double xc, double rim, double inv) {
        DiagramPoly s;
        s.role = DiagramRole::Structure;
        s.pts << QPointF(xc - mw, rim) << QPointF(xc + mw, rim)
              << QPointF(xc + mw, inv) << QPointF(xc - mw, inv);
        return s;
    };
    m.polys << structure(x0, rimUp, invUp - (structTop - structBot) * 0.02);
    m.polys << structure(x1, rimDn, invDn - (structTop - structBot) * 0.02);

    // Ground line: flat at each rim, sloping between them.
    m.grounds << DiagramGround{ x0 - length * 0.12, x0 - mw, rimUp };
    m.grounds << DiagramGround{ x1 + mw, x1 + length * 0.12, rimDn };
    m.polylines << DiagramPolyline{
        QPolygonF({ QPointF(x0 + mw, rimUp), QPointF(x1 - mw, rimDn) }),
        DiagramRole::Muted, true, QString() };

    // Barrel.
    DiagramPoly barrel;
    barrel.role = DiagramRole::Conduit;
    barrel.pts << QPointF(x0 + mw, pipeInvUp + yFull)
               << QPointF(x1 - mw, pipeInvDn + yFull)
               << QPointF(x1 - mw, pipeInvDn)
               << QPointF(x0 + mw, pipeInvUp);
    m.polys << barrel;

    // Elevations.
    m.leaders << DiagramLeader{ QPointF(x0, rimUp),
                                tr_("Rim %1").arg(num(rimUp, 2)),
                                QPointF(44.0, -18.0) };
    m.leaders << DiagramLeader{ QPointF(x1, rimDn),
                                tr_("Rim %1").arg(num(rimDn, 2)),
                                QPointF(-52.0, -18.0) };
    m.leaders << DiagramLeader{ QPointF(x0 + mw, pipeInvUp),
                                tr_("Inv %1").arg(num(pipeInvUp, 2)),
                                QPointF(60.0, 30.0) };
    m.leaders << DiagramLeader{ QPointF(x1 - mw, pipeInvDn),
                                tr_("Inv %1").arg(num(pipeInvDn, 2)),
                                QPointF(-64.0, 26.0) };
    if (yFull > 0.0) {
        // Anchored at a quarter point, not mid-span: the run dimension below
        // writes "L … S … %" centred on the barrel axis, and a mid-span crown
        // leader lands its label on top of that text.
        const double xq = x0 + length * 0.28;
        const double crownAtQ = pipeInvUp + (pipeInvDn - pipeInvUp) * 0.28 + yFull;
        m.leaders << DiagramLeader{ QPointF(xq, crownAtQ),
                                    tr_("Crown %1").arg(num(crownAtQ, 2)),
                                    QPointF(26.0, -48.0) };
    }

    // Upstream offset — the number engineers actually check on a profile.
    if (std::abs(offUp) > 1.0e-9) {
        DiagramDim d;
        d.from        = QPointF(x0 - mw, invUp);
        d.to          = QPointF(x0 - mw, pipeInvUp);
        d.text        = tr_("Offset %1").arg(lenText(offUp, units, 2));
        d.pixelOffset = -22.0;  // left of the upstream structure
        m.dims << d;
    }

    // Length + slope along the barrel axis.
    // `length` is already floored to a positive nominal run above.
    const double slopePct = 100.0 * (pipeInvUp - pipeInvDn) / length;
    DiagramDim run;
    run.from        = QPointF(x0 + mw, pipeInvUp + yFull);
    run.to          = QPointF(x1 - mw, pipeInvDn + yFull);
    run.text        = tr_("L %1   S %2 %")
                          .arg(lenText(length, units, 1), num(slopePct, 2));
    run.pixelOffset = -20.0;
    m.dims << run;

    m.footer = tr_("%1   %2   inverts %3 → %4 %5")
                   .arg(shapeDisplayName(shape),
                        yFull > 0.0 ? lenText(yFull, units) : tr_("no section"),
                        num(pipeInvUp, 2), num(pipeInvDn, 2), units.lengthLabel);
    return m;
}

// ---------------------------------------------------------------------------
// Node profile
// ---------------------------------------------------------------------------

namespace {

/*! One connecting link, resolved for drawing. */
struct NodeConnection
{
    QString name;
    double  invert  = 0.0;   //!< Absolute invert elevation at this node.
    double  height  = 0.0;   //!< Full depth of the section (0 → thin stub).
    bool    inbound = true;  //!< True when this node is the link's `to` node.
    double  headingDeg = 0.0;
    bool    hasHeading = false;
};

} // namespace

SectionDiagramModel buildNodeProfile(SWMM_Engine engine, int nodeIdx,
                                     const DiagramUnits &units, int maxLinks)
{
    SectionDiagramModel m;
    m.uniformScale = false;

    if (!engine || nodeIdx < 0) {
        m.emptyText = tr_("No node selected.");
        return m;
    }

    const QString nodeName = idOf(swmm_node_id(engine, nodeIdx));
    m.title = tr_("%1 — Node Profile").arg(nodeName);

    int nodeType = SWMM_NODE_JUNCTION;
    swmm_node_get_type(engine, nodeIdx, &nodeType);
    const QString kindName = [nodeType]() {
        switch (nodeType) {
        case SWMM_NODE_OUTFALL: return tr_("OUTFALL");
        case SWMM_NODE_STORAGE: return tr_("STORAGE");
        case SWMM_NODE_DIVIDER: return tr_("DIVIDER");
        default:                return tr_("JUNCTION");
        }
    }();

    double invert = 0.0, maxDepth = 0.0, surcharge = 0.0;
    swmm_node_get_invert_elev(engine, nodeIdx, &invert);
    swmm_node_get_max_depth(engine, nodeIdx, &maxDepth);
    swmm_node_get_surcharge_depth(engine, nodeIdx, &surcharge);
    const double rim = invert + maxDepth;

    // ---- Collect connections ----------------------------------------------
    double nx = 0.0, ny = 0.0;
    const bool haveCoord =
        (swmm_spatial_get_node_coord(engine, nodeIdx, &nx, &ny) == SWMM_OK);

    QVector<NodeConnection> conns;
    const int nLinks = swmm_link_count(engine);
    for (int i = 0; i < nLinks; ++i) {
        int from = -1, to = -1;
        if (swmm_link_get_from_node(engine, i, &from) != SWMM_OK) continue;
        if (swmm_link_get_to_node(engine, i, &to) != SWMM_OK) continue;
        if (from != nodeIdx && to != nodeIdx) continue;

        NodeConnection c;
        c.name    = idOf(swmm_link_id(engine, i));
        c.inbound = (to == nodeIdx);

        double off = 0.0;
        if (c.inbound) swmm_link_get_offset_dn(engine, i, &off);
        else           swmm_link_get_offset_up(engine, i, &off);
        c.invert = invert + off;

        int shape = SWMM_XSECT_CIRCULAR;
        double g1 = 0.0, g2 = 0.0, g3 = 0.0, g4 = 0.0;
        if (swmm_link_get_xsect(engine, i, &shape, &g1, &g2, &g3, &g4) == SWMM_OK) {
            const XsectSampler s =
                samplerForLink(engine, i, shape, g1, g2, g3, g4, units.si);
            if (s.isValid()) c.height = s.fullProps().yFull;
        }

        // Plan heading from this node toward the far end of the link.
        if (haveCoord) {
            const int other = c.inbound ? from : to;
            double ox = 0.0, oy = 0.0;
            if (other >= 0
                && swmm_spatial_get_node_coord(engine, other, &ox, &oy) == SWMM_OK)
            {
                const double dx = ox - nx, dy = oy - ny;
                if (std::hypot(dx, dy) > 1.0e-9) {
                    c.headingDeg = std::atan2(dy, dx) * 180.0 / M_PI;
                    c.hasHeading = true;
                }
            }
        }
        conns << c;
    }

    const int total = static_cast<int>(conns.size());
    // Deepest first — matches how a manhole schedule is read.
    std::stable_sort(conns.begin(), conns.end(),
                     [](const NodeConnection &a, const NodeConnection &b) {
                         return a.invert < b.invert;
                     });
    if (maxLinks > 0 && conns.size() > maxLinks) conns.resize(maxLinks);

    m.subtitle = tr_("%1 · %2 connecting link(s)").arg(kindName).arg(total);

    // ---- Structure ---------------------------------------------------------
    // Model x is arbitrary here (nothing horizontal is to scale), so use a
    // normalized 0..1 frame: chamber centred, stubs to either side.
    constexpr double kChamberHalf = 0.10;
    constexpr double kWall        = 0.025;
    constexpr double kStubLen     = 0.34;

    const double vSpan = std::max(maxDepth, 1.0e-6);

    DiagramPoly walls;
    walls.role = DiagramRole::Structure;
    walls.pts << QPointF(-kChamberHalf - kWall, rim)
              << QPointF( kChamberHalf + kWall, rim)
              << QPointF( kChamberHalf + kWall, invert - vSpan * 0.03)
              << QPointF(-kChamberHalf - kWall, invert - vSpan * 0.03);
    m.polys << walls;

    DiagramPoly chamber;
    chamber.role = DiagramRole::Conduit;
    chamber.pts << QPointF(-kChamberHalf, rim)
                << QPointF( kChamberHalf, rim)
                << QPointF( kChamberHalf, invert)
                << QPointF(-kChamberHalf, invert);
    m.polys << chamber;

    m.grounds << DiagramGround{ -kChamberHalf - kWall - kStubLen - 0.08,
                                -kChamberHalf - kWall, rim };
    m.grounds << DiagramGround{  kChamberHalf + kWall,
                                 kChamberHalf + kWall + kStubLen + 0.08, rim };

    // ---- Connecting pipes --------------------------------------------------
    for (const NodeConnection &c : conns) {
        const double sign = c.inbound ? -1.0 : 1.0;
        const double xIn  = sign * (kChamberHalf + kWall);
        const double xOut = xIn + sign * kStubLen;
        // Zero-height sections (pump / dummy) still need a visible stub.
        const double h = (c.height > 0.0) ? c.height : vSpan * 0.06;

        DiagramPoly stub;
        stub.role = DiagramRole::Conduit;
        stub.pts << QPointF(xIn,  c.invert + h) << QPointF(xOut, c.invert + h)
                 << QPointF(xOut, c.invert)     << QPointF(xIn,  c.invert);
        m.polys << stub;

        m.leaders << DiagramLeader{
            QPointF(xOut, c.invert),
            tr_("%1  Inv %2").arg(c.name, num(c.invert, 2)),
            QPointF(c.inbound ? -18.0 : 18.0, 16.0) };

        if (c.hasHeading)
            m.plan << PlanSpoke{ c.headingDeg, c.name, c.inbound };
    }

    // ---- Rim / invert / depth annotations ---------------------------------
    m.leaders << DiagramLeader{ QPointF(-kChamberHalf - kWall, rim),
                                tr_("Rim El. %1").arg(lenText(rim, units, 2)),
                                QPointF(-40.0, -20.0) };
    m.leaders << DiagramLeader{ QPointF(0.0, invert),
                                tr_("Invert El. %1").arg(lenText(invert, units, 2)),
                                QPointF(34.0, 26.0) };

    DiagramDim depthDim;
    depthDim.from        = QPointF(kChamberHalf + kWall, invert);
    depthDim.to          = QPointF(kChamberHalf + kWall, rim);
    depthDim.text        = tr_("Max depth %1").arg(lenText(maxDepth, units));
    depthDim.pixelOffset = 30.0;   // right of the chamber
    m.dims << depthDim;

    if (surcharge > 0.0) {
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-kChamberHalf, rim + surcharge),
                        QPointF( kChamberHalf, rim + surcharge) }),
            DiagramRole::Accent, true,
            tr_("surcharge +%1").arg(num(surcharge, 2)) };
    }

    QString footer = tr_("Invert %1   Rim %2   Max depth %3 %4")
                         .arg(num(invert, 2), num(rim, 2),
                              num(maxDepth, 2), units.lengthLabel);
    if (total > static_cast<int>(conns.size()))
        footer += tr_("   (+%1 more link(s) not drawn)")
                      .arg(total - static_cast<int>(conns.size()));
    if (nodeType == SWMM_NODE_STORAGE)
        footer += tr_("   — storage geometry not yet drawn");
    m.footer = footer;

    return m;
}

// ---------------------------------------------------------------------------
// Editor previews
// ---------------------------------------------------------------------------

SectionDiagramModel buildXsectEditorPreview(int shape, double geom1,
                                            double geom2, double geom3,
                                            double geom4,
                                            const DiagramUnits &units,
                                            int highlightOrdinal)
{
    SectionDiagramModel m;
    m.uniformScale = true;
    m.subtitle     = shapeDisplayName(shape);

    if (shape == SWMM_XSECT_DUMMY) {
        m.emptyText = tr_("DUMMY sections carry no geometry.");
        return m;
    }
    if (isTabulatedShape(shape)) {
        m.emptyText = tr_("Pick a %1 below to preview its geometry.")
                          .arg(shapeDisplayName(shape));
        return m;
    }

    const XsectSampler sampler =
        XsectSampler::fromShape(shape, geom1, geom2, geom3, geom4, units.si);
    if (!sampler.isValid()) {
        m.emptyText = tr_("Enter the dimensions for a %1 section.")
                          .arg(shapeDisplayName(shape));
        return m;
    }

    addSectionGeometry(m, sampler, units, highlightOrdinal);
    if (m.polys.isEmpty()) {
        m.emptyText = tr_("Section geometry is degenerate.");
        return m;
    }
    m.footer = sectionFooter(sampler.fullProps(), units, /*barrels=*/1);
    return m;
}

SectionDiagramModel buildSamplerPreview(const XsectSampler &sampler,
                                        const QString &title,
                                        const QString &subtitle,
                                        const DiagramUnits &units)
{
    SectionDiagramModel m;
    m.uniformScale = true;
    m.title        = title;
    m.subtitle     = subtitle;

    if (!sampler.isValid()) {
        m.emptyText = tr_("No geometry available.");
        return m;
    }

    addSectionGeometry(m, sampler, units, /*highlightOrdinal=*/0);
    if (m.polys.isEmpty()) {
        m.emptyText = tr_("Section geometry is degenerate.");
        return m;
    }
    m.footer = sectionFooter(sampler.fullProps(), units, /*barrels=*/1);
    return m;
}

} // namespace openswmmvis::sectionview
