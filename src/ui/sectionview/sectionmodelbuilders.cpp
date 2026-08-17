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
#include <openswmm/engine/openswmm_tables.h>

#include <QCoreApplication>
#include <QStringList>
#include <QVector>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace openswmmvis::sectionview {

namespace {

/*!
 * Cap on the automatic vertical exaggeration of a link profile.
 *
 * 10:1 is the conventional ceiling on a drainage profile sheet. Above it the
 * drawn gradient stops being a useful cue and starts being a misleading one —
 * which is the complaint this constant exists to answer. Users who want the
 * undistorted picture can set 1:1 from the Section View dock.
 */
constexpr double kProfileMaxExaggeration = 10.0;

/*!
 * Width:height the drawn profile aims for.
 *
 * 6:1 keeps a manhole and its annotations readable while leaving the barrel
 * long enough to read as a reach. Because it is a property of the DRAWING and
 * not of the pane, the resulting exaggeration is the same whatever size the
 * dock is — a short or steep reach lands at true scale on its own.
 */
constexpr double kProfileTargetAspect = 6.0;

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

// ---------------------------------------------------------------------------
// Node-type presentation
// ---------------------------------------------------------------------------

/*!
 * How one node TYPE is drawn, expressed as multiples of the drawing's nominal
 * structure size rather than in absolutes, so the node profile (normalised x)
 * and the link profile (x = real stationing) can share the table.
 *
 * The point of this struct is that the four SWMM node types were previously all
 * drawn as the same grey box: a tank, a manhole and a sea outfall are different
 * objects, and a reader should not have to check the subtitle to tell which one
 * is on screen.
 */
struct NodeShellStyle
{
    double      halfWidthMult = 1.0;
    double      wallMult      = 1.0;
    DiagramRole role          = DiagramRole::Structure;
    bool        cover         = false;  //!< Frame & cover sat on the rim.
    bool        corbel        = false;  //!< Neck the top in, brick-manhole style.
};

NodeShellStyle shellStyleFor(int nodeType)
{
    switch (nodeType) {
    // Wide, brown and heavier-stroked. A storage unit is the one node whose plan
    // size is part of its definition, so it gets the room.
    case SWMM_NODE_STORAGE: return { 2.2, 1.7, DiagramRole::Storage, false, false };
    // A headwall is a slab, not a chamber: narrow, uncovered, and identified by
    // the receiving water drawn against it.
    case SWMM_NODE_OUTFALL: return { 0.80, 1.4, DiagramRole::Structure, false, false };
    // A divider is a junction that splits; keep the manhole read and let the
    // split arrows carry the difference.
    case SWMM_NODE_DIVIDER: return { 1.30, 1.0, DiagramRole::Structure, true, false };
    default:                return { 1.0, 1.0, DiagramRole::Structure, true, true };
    }
}

/*! Corbel silhouette: full width up to 80 % of the depth, then a cone into the
 *  access shaft. Expressed in the same (depth-fraction, half-width-fraction)
 *  form as a storage silhouette so one sampler serves both. */
QVector<QPointF> corbelSilhouette()
{
    return { QPointF(0.00, 1.00), QPointF(0.80, 1.00),
             QPointF(0.92, 0.66), QPointF(1.00, 0.66) };
}

/*!
 * Half-width profile of a storage unit, bottom→top, as
 * (depth / maxDepth, half-width / widest half-width).
 *
 * Half-width tracks sqrt(area) wherever the shape is defined by an area: a
 * profile is a slice, so what the reader sees is the side of an equivalent
 * square. Plotting area directly would turn a mildly flaring pond into a
 * trumpet — a shape the model does not describe.
 *
 * An empty return means "vertical walls, or unreadable"; the caller then draws a
 * plain rectangle, which is exactly right for a cylinder and honest for the
 * rest.
 */
QVector<QPointF> storageSilhouette(SWMM_Engine engine, int nodeIdx,
                                   double maxDepth)
{
    if (!(maxDepth > 0.0)) return {};

    int shape = -1;
    if (swmm_node_get_storage_shape(engine, nodeIdx, &shape) != SWMM_OK) return {};

    constexpr int kLevels = 16;
    QVector<QPointF> out;

    // Widths are normalised at the end, so any positive measure of "how wide
    // this level is" works here; every branch below returns sqrt(area) or a
    // linear width consistently within itself.
    switch (shape) {
    case SWMM_STORAGE_TABULAR: {
        int curve = -1;
        if (swmm_node_get_storage_curve(engine, nodeIdx, &curve) != SWMM_OK
            || curve < 0)
            return {};
        int n = 0;
        if (swmm_table_get_point_count(engine, curve, &n) != SWMM_OK || n < 2)
            return {};
        for (int i = 0; i < n; ++i) {
            double d = 0.0, a = 0.0;
            if (swmm_table_get_point(engine, curve, i, &d, &a) != SWMM_OK) continue;
            out << QPointF(std::clamp(d / maxDepth, 0.0, 1.0),
                           std::sqrt(std::max(a, 0.0)));
        }
        break;
    }
    case SWMM_STORAGE_FUNCTIONAL: {
        double a = 0.0, b = 0.0, c = 0.0;
        if (swmm_node_get_storage_functional(engine, nodeIdx, &a, &b, &c) != SWMM_OK)
            return {};
        for (int i = 0; i <= kLevels; ++i) {
            const double f = static_cast<double>(i) / kLevels;
            const double area = c + a * std::pow(f * maxDepth, b);
            out << QPointF(f, std::sqrt(std::max(area, 0.0)));
        }
        break;
    }
    case SWMM_STORAGE_CONICAL:
    case SWMM_STORAGE_PYRAMIDAL: {
        // Straight batter: the base axis grows by 2 × side slope per unit rise,
        // which is already a width, so no square root here.
        double p1 = 0.0, p2 = 0.0, p3 = 0.0;
        if (swmm_node_get_storage_geometry(engine, nodeIdx, &p1, &p2, &p3) != SWMM_OK)
            return {};
        if (!(p3 > 0.0)) return {};       // vertical walls
        for (int i = 0; i <= kLevels; ++i) {
            const double f = static_cast<double>(i) / kLevels;
            out << QPointF(f, std::max(p1, 0.0) + 2.0 * p3 * f * maxDepth);
        }
        break;
    }
    case SWMM_STORAGE_PARABOLOID: {
        // Elliptical paraboloid: the axis at depth d is p1·sqrt(d / p3), so the
        // silhouette is the parabola on its side that gives the shape its name.
        double p1 = 0.0, p2 = 0.0, p3 = 0.0;
        if (swmm_node_get_storage_geometry(engine, nodeIdx, &p1, &p2, &p3) != SWMM_OK)
            return {};
        if (!(p3 > 0.0) || !(p1 > 0.0)) return {};
        for (int i = 0; i <= kLevels; ++i) {
            const double f = static_cast<double>(i) / kLevels;
            out << QPointF(f, p1 * std::sqrt(f * maxDepth / p3));
        }
        break;
    }
    default:                              // CYLINDRICAL and anything unknown
        return {};
    }

    // Normalise, and drop a silhouette that carries no shape information — a
    // flat one would only add sampling points to a rectangle.
    double wMax = 0.0, wMin = 0.0;
    bool   first = true;
    for (const QPointF &s : out) {
        if (first) { wMax = wMin = s.y(); first = false; }
        wMax = std::max(wMax, s.y());
        wMin = std::min(wMin, s.y());
    }
    if (!(wMax > 0.0) || out.size() < 2)          return {};
    if (wMax - wMin < 0.02 * wMax)                return {};
    // Normalise, on a floor. A functional storage with a large exponent is
    // genuinely ~10 % as wide at its invert as at its rim, and drawing that
    // literally leaves a razor for the pipes to attach to and a sliver of water
    // at the bottom. The floor is a drawing minimum on a horizontal scale the
    // footer already declares schematic — no elevation depends on it.
    constexpr double kMinWidthFrac = 0.22;
    for (QPointF &s : out)
        s.setY(std::max(s.y() / wMax, kMinWidthFrac));

    std::stable_sort(out.begin(), out.end(),
                     [](const QPointF &a, const QPointF &b) {
                         return a.x() < b.x();
                     });
    return out;
}

/*! Half-width fraction at depth fraction \p f. A silhouette with no samples is
 *  a straight wall, so it answers 1.0 everywhere. */
double silhouetteWidth(const QVector<QPointF> &sil, double f)
{
    if (sil.isEmpty()) return 1.0;
    if (f <= sil.first().x()) return sil.first().y();
    if (f >= sil.last().x())  return sil.last().y();
    for (int i = 1; i < sil.size(); ++i) {
        if (f > sil.at(i).x()) continue;
        const QPointF a = sil.at(i - 1), b = sil.at(i);
        const double span = b.x() - a.x();
        if (span <= 0.0) return b.y();
        return a.y() + (b.y() - a.y()) * (f - a.x()) / span;
    }
    return sil.last().y();
}

//! Display name for a storage shape code.
QString storageShapeName(int shape)
{
    switch (shape) {
    case SWMM_STORAGE_TABULAR:     return tr_("tabular");
    case SWMM_STORAGE_FUNCTIONAL:  return tr_("functional");
    case SWMM_STORAGE_CYLINDRICAL: return tr_("cylindrical");
    case SWMM_STORAGE_CONICAL:     return tr_("conical");
    case SWMM_STORAGE_PARABOLOID:  return tr_("paraboloid");
    case SWMM_STORAGE_PYRAMIDAL:   return tr_("pyramidal");
    default:                       return tr_("unknown shape");
    }
}

//! Display name for a divider method code.
QString dividerTypeName(int type)
{
    switch (type) {
    case SWMM_DIVIDER_CUTOFF:   return tr_("CUTOFF");
    case SWMM_DIVIDER_OVERFLOW: return tr_("OVERFLOW");
    case SWMM_DIVIDER_TABULAR:  return tr_("TABULAR");
    case SWMM_DIVIDER_WEIR:     return tr_("WEIR");
    default:                    return QString();
    }
}

} // namespace

double profileMaxExaggeration() noexcept { return kProfileMaxExaggeration; }

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

    // Invert / crown elevations, taken from each end node + its offset so the
    // numbers match what the profile view and the property grid report. The
    // shape is one section but the run has two ends, so label both.
    int upNode = -1, dnNode = -1;
    double offsetUp = 0.0, offsetDn = 0.0, invertUp = 0.0, invertDn = 0.0;
    const bool haveUp =
        swmm_link_get_from_node(engine, linkIdx, &upNode) == SWMM_OK && upNode >= 0;
    const bool haveDn =
        swmm_link_get_to_node(engine, linkIdx, &dnNode) == SWMM_OK && dnNode >= 0;
    if (haveUp) {
        swmm_link_get_offset_up(engine, linkIdx, &offsetUp);
        swmm_node_get_invert_elev(engine, upNode, &invertUp);
    }
    if (haveDn) {
        swmm_link_get_offset_dn(engine, linkIdx, &offsetDn);
        swmm_node_get_invert_elev(engine, dnNode, &invertDn);
    }
    if (haveUp) {
        // "100.00 ft" when the two ends agree, "100.00 / 98.00 ft" when they
        // don't — a flat run shouldn't pay for the slash.
        const auto endText = [&](double up, double dn) {
            if (!haveDn || std::abs(up - dn) < 5.0e-3) return lenText(up, units, 2);
            return tr_("%1 / %2").arg(num(up, 2), lenText(dn, units, 2));
        };
        const double invUpEl = invertUp + offsetUp;
        const double invDnEl = invertDn + offsetDn;

        m.leaders << DiagramLeader{
            QPointF(0.0, 0.0),
            tr_("Invert El. %1").arg(endText(invUpEl, invDnEl)),
            QPointF(-70.0, 34.0) };
        m.leaders << DiagramLeader{
            QPointF(0.0, fp.yFull),
            tr_("Crown El. %1").arg(endText(invUpEl + fp.yFull, invDnEl + fp.yFull)),
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
    // A 120 m reach against 4 m of depth cannot be drawn at true scale in a
    // dock and still show the pipe, so the vertical IS exaggerated — but the
    // exaggeration is capped and stated rather than being whatever the pane's
    // aspect ratio implies. Uncapped, a 0.25 % pipe was being drawn at 15:1
    // and read as a 4 % one.
    m.uniformScale            = false;
    m.maxVerticalExaggeration = kProfileMaxExaggeration;
    m.targetDrawnAspect       = kProfileTargetAspect;
    m.annotateExaggeration    = true;   // both axes are real lengths here

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

    int linkType = SWMM_LINK_CONDUIT;
    swmm_link_get_type(engine, linkIdx, &linkType);

    // Orifices / weirs / outlets have no length; give them a nominal run so
    // the structures don't collapse onto each other. A pump gets a run scaled to
    // its LIFT: at the flat nominal, a wet well and a discharge 15 ft apart
    // vertically collapsed into a vertical sliver with the pump symbol on top of
    // both structures.
    if (!(length > 0.0)) {
        const double lift = std::abs((invDn + offDn) - (invUp + offUp));
        length = (linkType == SWMM_LINK_PUMP && lift > 0.0)
                     ? std::max(1.0, lift * 1.6) : 1.0;
        // With a synthetic x axis there is no V:H ratio to report — annotating
        // one would be arithmetic on a drawing width.
        m.annotateExaggeration = false;
    }

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
    // between their inner faces so the drawing reads like a real profile. Each
    // end is sized and coloured for ITS node type, so a reach into a tank or out
    // of an outfall says so without the reader opening the node profile.
    const double mw = std::max(length * 0.035, 1.0e-6);
    const double x0 = 0.0, x1 = length;
    const double structTop = std::max(rimUp, rimDn);
    const double structBot = std::min({ invUp, invDn, pipeInvUp, pipeInvDn });

    int upType = SWMM_NODE_JUNCTION, dnType = SWMM_NODE_JUNCTION;
    swmm_node_get_type(engine, upNode, &upType);
    swmm_node_get_type(engine, dnNode, &dnType);
    const NodeShellStyle upStyle = shellStyleFor(upType);
    const NodeShellStyle dnStyle = shellStyleFor(dnType);
    const double mwUp = mw * upStyle.halfWidthMult;
    const double mwDn = mw * dnStyle.halfWidthMult;

    auto structure = [&](double xc, double w, double rim, double inv,
                         const NodeShellStyle &st) {
        DiagramPoly s;
        s.role = st.role;
        s.pts << QPointF(xc - w, rim) << QPointF(xc + w, rim)
              << QPointF(xc + w, inv) << QPointF(xc - w, inv);
        return s;
    };
    const double sink = (structTop - structBot) * 0.02;
    m.polys << structure(x0, mwUp, rimUp, invUp - sink, upStyle);
    m.polys << structure(x1, mwDn, rimDn, invDn - sink, dnStyle);
    if (upStyle.cover)
        m.symbols << DiagramSymbol{ QPointF(x0, rimUp),
                                    DiagramSymbolKind::ManholeCover, 26.0, false,
                                    DiagramRole::Structure };
    if (dnStyle.cover)
        m.symbols << DiagramSymbol{ QPointF(x1, rimDn),
                                    DiagramSymbolKind::ManholeCover, 26.0, false,
                                    DiagramRole::Structure };

    // Ground line: flat at each rim, sloping between them. Reach is measured
    // OUT from the structure face — measuring it from the centreline left a
    // wide storage shell with only a stub of ground beside it.
    m.grounds << DiagramGround{ x0 - mwUp - length * 0.12, x0 - mwUp, rimUp };
    m.grounds << DiagramGround{ x1 + mwDn, x1 + mwDn + length * 0.12, rimDn };
    m.polylines << DiagramPolyline{
        QPolygonF({ QPointF(x0 + mwUp, rimUp), QPointF(x1 - mwDn, rimDn) }),
        DiagramRole::Muted, true, QString() };

    if (linkType == SWMM_LINK_PUMP) {
        // A pump has no barrel — a zero-height rectangle drawn between the wet
        // well and the discharge is a line pretending to be a conduit. Draw the
        // pressure main and put the machine on it.
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(x0 + mwUp, pipeInvUp),
                        QPointF(x1 - mwDn, pipeInvDn) }),
            DiagramRole::Conduit, false, QString(), false };
        m.symbols << DiagramSymbol{
            QPointF(0.5 * (x0 + mwUp + x1 - mwDn), 0.5 * (pipeInvUp + pipeInvDn)),
            DiagramSymbolKind::Pump, 30.0, false, DiagramRole::Accent };
    } else {
        DiagramPoly barrel;
        barrel.role = DiagramRole::Conduit;
        barrel.pts << QPointF(x0 + mwUp, pipeInvUp + yFull)
                   << QPointF(x1 - mwDn, pipeInvDn + yFull)
                   << QPointF(x1 - mwDn, pipeInvDn)
                   << QPointF(x0 + mwUp, pipeInvUp);
        m.polys << barrel;
    }

    // Elevations.
    m.leaders << DiagramLeader{ QPointF(x0, rimUp),
                                tr_("Rim %1").arg(num(rimUp, 2)),
                                QPointF(44.0, -18.0) };
    m.leaders << DiagramLeader{ QPointF(x1, rimDn),
                                tr_("Rim %1").arg(num(rimDn, 2)),
                                QPointF(-52.0, -18.0) };
    m.leaders << DiagramLeader{ QPointF(x0 + mwUp, pipeInvUp),
                                tr_("Inv %1").arg(num(pipeInvUp, 2)),
                                QPointF(60.0, 30.0) };
    m.leaders << DiagramLeader{ QPointF(x1 - mwDn, pipeInvDn),
                                tr_("Inv %1").arg(num(pipeInvDn, 2)),
                                QPointF(-64.0, 26.0) };
    if (yFull > 0.0) {
        // Anchored at the barrel-end soffit corners, so each label reports a
        // crown elevation that exists on a plan set rather than an interpolated
        // mid-run value. These two point OUTWARD, against this function's
        // inward convention for rim/invert: the run dimension writes
        // "L … S … %" along this very crown line, so anything landing over the
        // barrel is written on top of that text.
        m.leaders << DiagramLeader{ QPointF(x0 + mwUp, pipeInvUp + yFull),
                                    tr_("Crown %1").arg(num(pipeInvUp + yFull, 2)),
                                    QPointF(-26.0, -48.0) };
        m.leaders << DiagramLeader{ QPointF(x1 - mwDn, pipeInvDn + yFull),
                                    tr_("Crown %1").arg(num(pipeInvDn + yFull, 2)),
                                    QPointF(26.0, -48.0) };
    }

    // Upstream offset — the number engineers actually check on a profile.
    if (std::abs(offUp) > 1.0e-9) {
        DiagramDim d;
        d.from        = QPointF(x0 - mwUp, invUp);
        d.to          = QPointF(x0 - mwUp, pipeInvUp);
        d.text        = tr_("Offset %1").arg(lenText(offUp, units, 2));
        d.pixelOffset = -22.0;  // left of the upstream structure
        m.dims << d;
    }

    if (linkType == SWMM_LINK_PUMP) {
        // A pump has neither a length nor a slope — the run above is a drawing
        // width. Its lift is the number that exists, so dimension that instead of
        // annotating "L 1.0 ft   S -1500 %".
        DiagramDim lift;
        lift.from        = QPointF(x1 - mwDn, pipeInvUp);
        lift.to          = QPointF(x1 - mwDn, pipeInvDn);
        lift.text        = tr_("Lift %1").arg(lenText(pipeInvDn - pipeInvUp, units, 2));
        lift.pixelOffset = -24.0;
        m.dims << lift;
    } else {
        // Length + slope along the barrel axis.
        // `length` is already floored to a positive nominal run above.
        const double slopePct = 100.0 * (pipeInvUp - pipeInvDn) / length;
        DiagramDim run;
        run.from        = QPointF(x0 + mwUp, pipeInvUp + yFull);
        run.to          = QPointF(x1 - mwDn, pipeInvDn + yFull);
        run.text        = tr_("L %1   S %2 %")
                              .arg(lenText(length, units, 1), num(slopePct, 2));
        run.pixelOffset = -20.0;
        m.dims << run;
    }

    m.footer = (linkType == SWMM_LINK_PUMP)
                   ? tr_("PUMP   lift %1 %2   inverts %3 → %4")
                         .arg(num(pipeInvDn - pipeInvUp, 2), units.lengthLabel,
                              num(pipeInvUp, 2), num(pipeInvDn, 2))
                   : tr_("%1   %2   inverts %3 → %4 %5")
                         .arg(shapeDisplayName(shape),
                              yFull > 0.0 ? lenText(yFull, units) : tr_("no section"),
                              num(pipeInvUp, 2), num(pipeInvDn, 2),
                              units.lengthLabel);
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
    int     type     = SWMM_LINK_CONDUIT;
    /*! Orifice SIDE/BOTTOM, weir type, or outlet rating type; -1 when the link
     *  kind has no sub-type. */
    int     subtype  = -1;
    bool    flapGate = false;
};

/*! Outfall boundary-condition codes. The engine documents these inline on
 *  swmm_node_set_outfall_type() but exports no enum for them. */
enum { kOutfallFree = 0, kOutfallNormal = 1, kOutfallFixed = 2,
       kOutfallTidal = 3, kOutfallTimeSeries = 4 };

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

        swmm_link_get_type(engine, i, &c.type);
        switch (c.type) {
        case SWMM_LINK_ORIFICE: swmm_link_get_orifice_type(engine, i, &c.subtype); break;
        case SWMM_LINK_WEIR:    swmm_link_get_weir_type(engine, i, &c.subtype);    break;
        case SWMM_LINK_OUTLET:  swmm_link_get_outlet_rating_type(engine, i, &c.subtype); break;
        default: break;
        }
        int flap = 0;
        if (swmm_link_get_flap_gate(engine, i, &flap) == SWMM_OK) c.flapGate = (flap != 0);

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

    // ---- Structure ---------------------------------------------------------
    // Model x is arbitrary here (nothing horizontal is to scale), so use a
    // normalized frame: structure centred, stubs to either side. The four node
    // types differ in width, material and fittings — see shellStyleFor().
    // Wider and with shorter stubs than the original 0.10 / 0.34: leader text
    // reserves up to 40 % of the pane on each side, which left a 12 ft manhole
    // 25 px wide — too narrow for a silhouette or a cover to be visible at all.
    constexpr double kBaseHalf = 0.14;
    constexpr double kBaseWall = 0.028;
    constexpr double kStubLen  = 0.30;

    // Two different spans. `depthSpan` maps an elevation onto the structure's
    // own depth and so must be the node's max depth. `vSpan` scales the drawing
    // minimums (floor slab, apron, jets) and must NOT be, because an outfall's
    // max depth is legitimately zero — the engine raises a JUNCTION's full depth
    // to its highest connecting crown but leaves outfalls alone — and a zero
    // scale collapsed the whole outboard drawing onto one line.
    const double depthSpan = std::max(maxDepth, 1.0e-6);
    double vSpan = maxDepth;
    for (const NodeConnection &c : conns)
        vSpan = std::max(vSpan, (c.invert + std::max(c.height, 0.0)) - invert);
    vSpan = std::max(vSpan, 1.0e-6);

    const double floorY = invert - vSpan * 0.03;

    NodeShellStyle style = shellStyleFor(nodeType);
    const double half = kBaseHalf * style.halfWidthMult;
    const double wall = kBaseWall * style.wallMult;

    // A storage unit's silhouette comes from its own shape data; a junction's
    // corbel is decoration, so it is suppressed when a pipe would land on the
    // neck — a manhole with a pipe hanging off its shaft is worse than a box.
    QVector<QPointF> sil;
    if (nodeType == SWMM_NODE_STORAGE) {
        sil = storageSilhouette(engine, nodeIdx, maxDepth);
    } else if (style.corbel && maxDepth > 0.0) {
        double topConn = invert;
        for (const NodeConnection &c : conns)
            topConn = std::max(topConn, c.invert + std::max(c.height, 0.0));
        if ((topConn - invert) < 0.74 * depthSpan) sil = corbelSilhouette();
    }

    //! Half-width of the VOID at an elevation; the shell face is this + wall.
    const auto halfAt = [&](double elev) {
        const double f = std::clamp((elev - invert) / depthSpan, 0.0, 1.0);
        return half * silhouetteWidth(sil, f);
    };
    const auto faceX = [&](double elev, double sign) {
        return sign * (halfAt(elev) + wall);
    };

    /*! Ring following the silhouette between two elevations, padded outward by
     *  \p pad (0 for the void / the water body, `wall` for the shell). Emitted
     *  right side bottom→top then left side top→bottom, so the first and last
     *  points are the two bottom corners.
     *
     *  Sampled AT the silhouette's own breakpoints rather than on a uniform
     *  ladder: a fixed ladder rounded a manhole's corbel into a bottle neck and
     *  smeared a storage curve's knee, because it interpolates across the very
     *  vertices that carry the shape. */
    const auto ring = [&](double yBot, double yTop, double pad) {
        QVector<double> ys{ yBot, yTop };
        for (const QPointF &s : sil) {
            const double y = invert + s.x() * depthSpan;
            if (y > yBot + 1.0e-9 && y < yTop - 1.0e-9) ys << y;
        }
        std::sort(ys.begin(), ys.end());

        QPolygonF r;
        for (double y : ys) r << QPointF(halfAt(y) + pad, y);
        for (int i = ys.size() - 1; i >= 0; --i)
            r << QPointF(-(halfAt(ys.at(i)) + pad), ys.at(i));
        return r;
    };

    DiagramPoly shell;
    shell.role = style.role;
    shell.pts  = ring(invert, rim, wall);
    // Floor slab: the shell closes across the bottom, so pushing the two bottom
    // corners down gives a base without a separate polygon.
    if (!shell.pts.isEmpty()) {
        shell.pts.prepend(QPointF(shell.pts.first().x(), floorY));
        shell.pts.append(QPointF(shell.pts.last().x(), floorY));
    }
    m.polys << shell;

    DiagramPoly voidSpace;
    voidSpace.role = DiagramRole::Conduit;
    voidSpace.pts  = ring(invert, rim, 0.0);
    m.polys << voidSpace;

    if (style.cover)
        m.symbols << DiagramSymbol{ QPointF(0.0, rim),
                                    DiagramSymbolKind::ManholeCover,
                                    std::max(28.0, 34.0 * halfAt(rim) / kBaseHalf),
                                    false, DiagramRole::Structure };

    // Ground on both sides, except where an outfall discharges — there the
    // right-hand side is water or open air, and hatching it would bury the
    // outlet.
    const double groundReach = half + wall + kStubLen + 0.08;
    m.grounds << DiagramGround{ -groundReach, faceX(rim, -1.0), rim };
    if (nodeType != SWMM_NODE_OUTFALL)
        m.grounds << DiagramGround{ faceX(rim, 1.0), groundReach, rim };

    // ---- Connecting links --------------------------------------------------
    int nPumps = 0, nOrifices = 0, nWeirs = 0, nOutlets = 0;

    // Lanes for devices. Several devices commonly hang off ONE wall at the SAME
    // elevation — fv_structures.inp has a pump, an orifice and a weir all on
    // J4's upstream face at its invert — and sharing the stub band merged them
    // into a single hatched slab in which none of the three was identifiable.
    // Each device therefore gets its own slice of the stub; conduits keep the
    // full stub, since a pipe has to reach the wall to read as one.
    int devLeft = 0, devRight = 0;
    for (const NodeConnection &c : conns)
        if (c.type != SWMM_LINK_CONDUIT) (c.inbound ? devLeft : devRight)++;
    int laneLeft = 0, laneRight = 0;

    for (const NodeConnection &c : conns) {
        const double sign    = c.inbound ? -1.0 : 1.0;
        const bool   device  = (c.type != SWMM_LINK_CONDUIT);
        const int    nDev    = std::max(c.inbound ? devLeft : devRight, 1);
        const int    lane    = device ? (c.inbound ? laneLeft++ : laneRight++) : 0;
        const double laneLen = device ? kStubLen / nDev : kStubLen;

        const double xIn  = faceX(c.invert, sign) + sign * lane * laneLen;
        const double xOut = xIn + sign * laneLen;
        const double xMid = 0.5 * (xIn + xOut);
        // Glyphs are a fixed pixel size, so they have to shrink as the lanes
        // divide or three of them overlap into a blot.
        const double glyphPx = (nDev >= 3) ? 15.0 : (nDev == 2) ? 18.0 : 22.0;
        // Zero-height sections (pump / dummy) still need a visible stub.
        const double h = (c.height > 0.0) ? c.height : vSpan * 0.06;

        // A lane past the first does not touch the structure, and a wall panel
        // floating in space reads as a mistake. Tie it back with a spine at its
        // invert — which is also the truth: the device IS connected there.
        if (lane > 0)
            m.polylines << DiagramPolyline{
                QPolygonF({ QPointF(faceX(c.invert, sign), c.invert),
                            QPointF(xIn, c.invert) }),
                DiagramRole::Muted, false, QString(), false };
        // Flow runs toward the node on an inbound link, away on an outbound one,
        // so every device arrow is drawn between the same two x's in the order
        // the water travels.
        const QPointF jetFrom(c.inbound ? xOut : xIn, c.invert + h * 0.5);
        const QPointF jetTo  (c.inbound ? xIn  : xOut, c.invert + h * 0.5);

        switch (c.type) {
        case SWMM_LINK_PUMP: {
            ++nPumps;
            // A pump is a pressure line with a machine on it, not a gravity
            // barrel: draw the line thin and let the glyph carry the meaning.
            m.polylines << DiagramPolyline{
                QPolygonF({ QPointF(xIn, c.invert), QPointF(xOut, c.invert) }),
                DiagramRole::Conduit, false, QString(), false };
            m.symbols << DiagramSymbol{ QPointF(xMid, c.invert),
                                        DiagramSymbolKind::Pump, glyphPx,
                                        /*mirrored=*/c.inbound,
                                        DiagramRole::Accent };
            m.arrows << DiagramArrow{ jetFrom, jetTo, QString(),
                                      DiagramRole::Accent };
            break;
        }
        case SWMM_LINK_ORIFICE: {
            ++nOrifices;
            const bool bottom = (c.subtype == SWMM_ORIFICE_BOTTOM);
            // An orifice is a hole in a wall, so the wall is what gets drawn:
            // hatched fill above the opening and below it, with the opening left
            // as a void that the jet passes through.
            //
            // The fill is a BAND around the opening, not the whole wall from
            // floor to rim: several devices commonly share one side of a
            // structure, and full-height panels merged into a single grey column
            // that hid every one of them.
            const double xa = std::min(xIn, xOut), xb = std::max(xIn, xOut);
            const auto panel = [&](double yBot, double yTop) {
                DiagramPoly p;
                p.role    = DiagramRole::Structure;
                p.texture = DiagramTexture::Hatch;
                p.pts << QPointF(xa, yTop) << QPointF(xb, yTop)
                      << QPointF(xb, yBot) << QPointF(xa, yBot);
                return p;
            };
            const double capUp = std::min(h * 1.3,
                                          std::max(rim - (c.invert + h), 0.0));
            const double capDn = std::min(h * 0.9,
                                          std::max(c.invert - floorY, 0.0));
            if (capUp > 0.0) m.polys << panel(c.invert + h, c.invert + h + capUp);
            if (capDn > 0.0) m.polys << panel(c.invert - capDn, c.invert);
            if (bottom) {
                // A bottom orifice discharges through the floor, so the jet
                // leaves downward instead of through the wall.
                m.arrows << DiagramArrow{ QPointF(xMid, c.invert),
                                          QPointF(xMid, c.invert - vSpan * 0.12),
                                          QString(), DiagramRole::Accent };
            } else {
                m.arrows << DiagramArrow{ jetFrom, jetTo, QString(),
                                          DiagramRole::Accent };
            }
            break;
        }
        case SWMM_LINK_WEIR: {
            ++nWeirs;
            // The weir's offset IS its crest, so the plate is real geometry:
            // solid below the crest, open above it, with a nappe spilling over
            // the edge. Like the orifice's fill, the plate is a band rather than
            // a full-height wall so it can share a side with other devices.
            const double xa = std::min(xIn, xOut), xb = std::max(xIn, xOut);
            const double plateBot =
                std::max(floorY, c.invert - std::max(h * 1.2, vSpan * 0.06));
            if (c.invert > plateBot) {
                DiagramPoly plate;
                plate.role = DiagramRole::Structure;
                plate.pts << QPointF(xa, c.invert)  << QPointF(xb, c.invert)
                          << QPointF(xb, plateBot)  << QPointF(xa, plateBot);
                m.polys << plate;
            }
            // Nappe: starts just inside the crest and lands beyond it, which is
            // the one picture that says "this flow goes OVER, not through".
            m.arrows << DiagramArrow{
                QPointF(xIn - sign * laneLen * 0.35, c.invert + h * 0.55),
                QPointF(xIn + sign * laneLen * 1.35, c.invert - vSpan * 0.10),
                QString(), DiagramRole::Accent };
            break;
        }
        case SWMM_LINK_OUTLET: {
            ++nOutlets;
            m.polylines << DiagramPolyline{
                QPolygonF({ QPointF(xIn, c.invert), QPointF(xOut, c.invert) }),
                DiagramRole::Conduit, false, QString(), false };
            m.symbols << DiagramSymbol{ QPointF(xMid, c.invert),
                                        DiagramSymbolKind::RatingBox, glyphPx,
                                        c.inbound, DiagramRole::Accent };
            m.arrows << DiagramArrow{ jetFrom, jetTo, QString(),
                                      DiagramRole::Accent };
            break;
        }
        default: {
            DiagramPoly stub;
            stub.role = DiagramRole::Conduit;
            stub.pts << QPointF(xIn,  c.invert + h) << QPointF(xOut, c.invert + h)
                     << QPointF(xOut, c.invert)     << QPointF(xIn,  c.invert);
            m.polys << stub;
            break;
        }
        }

        // A flap gate lives on the downstream face of whatever it guards, so it
        // hangs at the outboard end of an outbound link and at the node-side end
        // of an inbound one — which is the same point either way: where the flow
        // leaves the drawing element.
        if (c.flapGate)
            m.symbols << DiagramSymbol{ QPointF(c.inbound ? xIn : xOut,
                                                c.invert + h),
                                        DiagramSymbolKind::FlapGate, 20.0,
                                        c.inbound, DiagramRole::Structure };

        m.leaders << DiagramLeader{
            QPointF(xOut, c.invert),
            tr_("%1  Inv %2").arg(c.name, num(c.invert, 2)),
            QPointF(c.inbound ? -18.0 : 18.0, 16.0) };

        // Only a real section has a crown. `h` above falls back to a fraction
        // of the chamber depth so pumps and DUMMY links still draw a visible
        // stub — that number is a drawing minimum, not an elevation.
        //
        // -22 against the invert's +16 leaves the pair 38 px apart even when
        // the stub collapses to nothing on screen — two full de-confliction
        // steps, so several shallow pipes on one side stack as crowns above
        // inverts instead of interleaving. Emitted after the invert so the
        // invert keeps its natural slot (de-confliction is first-come).
        if (c.height > 0.0) {
            m.leaders << DiagramLeader{
                QPointF(xOut, c.invert + c.height),
                tr_("%1  Crown %2").arg(c.name, num(c.invert + c.height, 2)),
                QPointF(c.inbound ? -18.0 : 18.0, -22.0) };
        }

        if (c.hasHeading)
            m.plan << PlanSpoke{ c.headingDeg, c.name, c.inbound };
    }

    // ---- Node-type specifics ----------------------------------------------
    QString typeNote;

    if (nodeType == SWMM_NODE_STORAGE) {
        int shape = -1;
        swmm_node_get_storage_shape(engine, nodeIdx, &shape);
        typeNote = storageShapeName(shape);

        // Water at the initial depth: a tank that is drawn empty when the model
        // says it starts half full is telling the user the wrong thing.
        double initDepth = 0.0;
        swmm_node_get_initial_depth(engine, nodeIdx, &initDepth);
        const double wsel = invert + std::clamp(initDepth, 0.0, maxDepth);
        if (wsel > invert) {
            DiagramPoly water;
            water.role = DiagramRole::Water;
            water.pts  = ring(invert, wsel, 0.0);
            m.polys << water;
            m.polylines << DiagramPolyline{
                QPolygonF({ QPointF(-halfAt(wsel), wsel),
                            QPointF( halfAt(wsel), wsel) }),
                DiagramRole::Water, false,
                tr_("init %1").arg(num(wsel, 2)), /*wavy=*/true };
        }

        // Seepage out of the bottom — the reason a storage unit's continuity can
        // differ from a junction's.
        double seep = 0.0;
        if (swmm_node_get_storage_seep_rate(engine, nodeIdx, &seep) == SWMM_OK
            && seep > 0.0)
        {
            for (double f : { -0.45, 0.0, 0.45 })
                m.arrows << DiagramArrow{ QPointF(half * f, floorY),
                                          QPointF(half * f, floorY - vSpan * 0.10),
                                          QString(), DiagramRole::Muted };
            // On a leader, not on the arrow: an arrow's label is written back at
            // its tail, which for a vertical arrow lands on the structure.
            m.leaders << DiagramLeader{ QPointF(0.0, floorY - vSpan * 0.10),
                                        tr_("seepage %1").arg(num(seep, 3)),
                                        QPointF(30.0, 20.0) };
        }
    }
    else if (nodeType == SWMM_NODE_OUTFALL) {
        int oType = kOutfallFree;
        swmm_node_get_outfall_type(engine, nodeIdx, &oType);
        int flap = 0;
        swmm_node_get_outfall_flap_gate(engine, nodeIdx, &flap);

        const double xFace  = faceX(invert, 1.0);
        const double xReach = xFace + kStubLen + 0.10;
        const double bedDrop = vSpan * 0.08;
        const double apronThk = vSpan * 0.10;

        // Riprap apron: the bed falls away from the headwall and is armoured.
        // Aggregate texture rather than a plain fill, because a smooth wedge
        // below an outlet reads as water.
        DiagramPoly apron;
        apron.role    = DiagramRole::Soil;
        apron.texture = DiagramTexture::Aggregate;
        apron.pts << QPointF(xFace,  invert)
                  << QPointF(xReach, invert - bedDrop)
                  << QPointF(xReach, invert - bedDrop - apronThk)
                  << QPointF(xFace,  invert - apronThk);
        m.polys << apron;

        // Carry the lowest inbound pipe THROUGH the headwall so the outfall
        // reads as a pipe discharging rather than as a blank wall.
        double outletInv = invert, outletH = vSpan * 0.10;
        for (const NodeConnection &c : conns) {
            if (!c.inbound) continue;
            outletInv = c.invert;
            outletH   = (c.height > 0.0) ? c.height : vSpan * 0.10;
            break;      // conns are sorted deepest-first
        }
        DiagramPoly through;
        through.role = DiagramRole::Conduit;
        through.pts << QPointF(-xFace, outletInv + outletH)
                    << QPointF( xFace, outletInv + outletH)
                    << QPointF( xFace, outletInv)
                    << QPointF(-xFace, outletInv);
        m.polys << through;

        // The tailwater. FIXED is a number we have; a tidal curve or a time
        // series is a number that changes, so it is drawn dashed and labelled
        // "varies" with NO elevation — inventing one would be worse than the
        // omission. FREE / NORMAL have no boundary stage at all: the pipe
        // discharges to air, and a free-fall jet is the honest picture.
        double stage = 0.0;
        swmm_node_get_outfall_param(engine, nodeIdx, &stage);

        if (oType == kOutfallFixed && stage > invert - bedDrop) {
            const double wsel = std::min(stage, rim + vSpan * 0.25);
            DiagramPoly water;
            water.role = DiagramRole::Water;
            water.pts << QPointF(xFace,  wsel)
                      << QPointF(xReach, wsel)
                      << QPointF(xReach, invert - bedDrop)
                      << QPointF(xFace,  invert);
            m.polys << water;
            // Emitted right→left: a polyline's label is drawn off its LAST
            // point, and the tailwater's right end is the drawing's right edge,
            // where the text was being clipped.
            m.polylines << DiagramPolyline{
                QPolygonF({ QPointF(xReach, wsel), QPointF(xFace, wsel) }),
                DiagramRole::Water, false,
                tr_("Fixed stage %1").arg(num(stage, 2)), /*wavy=*/true };
            typeNote = tr_("FIXED %1").arg(num(stage, 2));
        }
        else if (oType == kOutfallTidal || oType == kOutfallTimeSeries) {
            const double wsel = invert + vSpan * 0.45;
            DiagramPoly water;
            water.role = DiagramRole::Water;
            water.pts << QPointF(xFace,  wsel)
                      << QPointF(xReach, wsel)
                      << QPointF(xReach, invert - bedDrop)
                      << QPointF(xFace,  invert);
            m.polys << water;
            // Dashed: the surface shown is schematic, not the stage.
            m.polylines << DiagramPolyline{
                QPolygonF({ QPointF(xReach, wsel), QPointF(xFace, wsel) }),
                DiagramRole::Water, true, tr_("stage varies"), /*wavy=*/true };
            typeNote = (oType == kOutfallTidal) ? tr_("TIDAL") : tr_("TIMESERIES");
        }
        else {
            // Free discharge: a jet out of the pipe onto the apron. The caption
            // rides a leader rather than the arrow, because an arrow's label is
            // written back at its tail — which here is inside the pipe.
            const QPointF land(xFace + kStubLen * 0.55, invert - bedDrop * 0.75);
            m.arrows << DiagramArrow{
                QPointF(xFace, outletInv + outletH * 0.4), land,
                QString(), DiagramRole::Accent };
            m.leaders << DiagramLeader{ land, tr_("free discharge"),
                                        QPointF(30.0, 22.0) };
            typeNote = (oType == kOutfallNormal) ? tr_("NORMAL") : tr_("FREE");
        }

        if (flap)
            m.symbols << DiagramSymbol{ QPointF(xFace, outletInv + outletH),
                                        DiagramSymbolKind::FlapGate, 22.0, false,
                                        DiagramRole::Structure };
    }
    else if (nodeType == SWMM_NODE_DIVIDER) {
        int dType = -1;
        swmm_node_get_divider_type(engine, nodeIdx, &dType);
        typeNote = dividerTypeName(dType);

        // The split itself: one stream carries on, one is diverted upward and
        // out. Which link is which is not exposed by the engine, so the arrows
        // are drawn from the chamber outward without naming a destination.
        const double xR   = faceX(invert, 1.0);
        const double xTip = xR + kStubLen * 0.22;
        m.arrows << DiagramArrow{ QPointF(-halfAt(invert) * 0.4, invert + vSpan * 0.06),
                                  QPointF(xTip, invert + vSpan * 0.06),
                                  QString(), DiagramRole::Accent };
        m.arrows << DiagramArrow{ QPointF(0.0, invert + vSpan * 0.10),
                                  QPointF(xTip, invert + vSpan * 0.40),
                                  QString(), DiagramRole::Accent };
        m.leaders << DiagramLeader{ QPointF(xTip, invert + vSpan * 0.40),
                                    tr_("flow split"), QPointF(26.0, -18.0) };
    }

    // ---- Rim / invert / depth annotations ---------------------------------
    // The structure's own annotations go on whichever side carries fewer
    // connections. They used to be pinned right, where a stack of outbound
    // devices is now drawn — the depth dimension ran straight through it.
    // An outfall is the exception: its right side is the receiving water, which
    // has to stay clear whatever the connection count says.
    int nLeft = 0;
    for (const NodeConnection &c : conns) nLeft += c.inbound ? 1 : 0;
    const double dimSide =
        (nodeType == SWMM_NODE_OUTFALL) ? -1.0
        : (nLeft <= static_cast<int>(conns.size()) - nLeft) ? -1.0 : 1.0;

    m.leaders << DiagramLeader{ QPointF(faceX(rim, -1.0), rim),
                                tr_("Rim El. %1").arg(lenText(rim, units, 2)),
                                QPointF(-40.0, -20.0) };
    m.leaders << DiagramLeader{ QPointF(faceX(invert, dimSide), invert),
                                tr_("Invert El. %1").arg(lenText(invert, units, 2)),
                                QPointF(34.0 * dimSide, 26.0) };

    // Both ends at the WIDEST face, so a battered shell gets a vertical
    // dimension line: anchoring each end to its own face drew "Max depth" along
    // the tank's batter, which reads as a slope measurement. Placed OUTBOARD of
    // the stubs, because a 30 px offset off the shell lands the text on top of
    // whatever connects on that side.
    const double xDim = dimSide * (half + wall + kStubLen + 0.05);
    DiagramDim depthDim;
    depthDim.from        = QPointF(xDim, invert);
    depthDim.to          = QPointF(xDim, rim);
    depthDim.text        = tr_("Max depth %1").arg(lenText(maxDepth, units));
    depthDim.pixelOffset = 16.0 * dimSide;
    m.dims << depthDim;

    if (surcharge > 0.0) {
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-halfAt(rim), rim + surcharge),
                        QPointF( halfAt(rim), rim + surcharge) }),
            DiagramRole::Accent, true,
            tr_("surcharge +%1").arg(num(surcharge, 2)), /*wavy=*/false };
    }

    // ---- Headers -----------------------------------------------------------
    // The subtitle names the type, its sub-kind (storage shape, outfall BC,
    // divider method) and any devices on the connections — the three things
    // that decide what the drawing looks like.
    QStringList devices;
    if (nPumps)    devices << tr_("%1 pump(s)").arg(nPumps);
    if (nOrifices) devices << tr_("%1 orifice(s)").arg(nOrifices);
    if (nWeirs)    devices << tr_("%1 weir(s)").arg(nWeirs);
    if (nOutlets)  devices << tr_("%1 outlet(s)").arg(nOutlets);

    m.subtitle = typeNote.isEmpty() ? kindName
                                    : tr_("%1 · %2").arg(kindName, typeNote);
    m.subtitle += tr_(" · %1 connecting link(s)").arg(total);
    if (!devices.isEmpty())
        m.subtitle += QStringLiteral(" · ") + devices.join(QStringLiteral(", "));

    QString footer = tr_("Invert %1   Rim %2   Max depth %3 %4")
                         .arg(num(invert, 2), num(rim, 2),
                              num(maxDepth, 2), units.lengthLabel);
    if (total > static_cast<int>(conns.size()))
        footer += tr_("   (+%1 more link(s) not drawn)")
                      .arg(total - static_cast<int>(conns.size()));
    if (nodeType == SWMM_NODE_STORAGE) {
        double p1 = 0.0, p2 = 0.0, p3 = 0.0;
        if (swmm_node_get_storage_geometry(engine, nodeIdx, &p1, &p2, &p3) == SWMM_OK
            && (p1 > 0.0 || p2 > 0.0))
        {
            footer += tr_("   Plan %1 × %2 %3")
                          .arg(num(p1, 1), num(p2, 1), units.lengthLabel);
        }
        // The horizontal is schematic in this view, so say so where the shape is
        // now being drawn from real data — otherwise the silhouette invites the
        // reader to scale widths off it.
        footer += tr_("   (widths schematic)");
    }
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
