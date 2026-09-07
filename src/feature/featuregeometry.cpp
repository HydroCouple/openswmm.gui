/*!
 * \file   featuregeometry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "feature/featuregeometry.h"
#include "mesh/meshquadregion.h"

#include <ogr_core.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

#include <QCoreApplication>
#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::feature {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/*! Squared distance — comparisons only, so the sqrt is avoided. */
inline double dist2(const QPointF &a, const QPointF &b)
{
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

/*! True when two rings' polygons properly overlap: any vertex of one strictly
 *  inside the other. Shared edges are tolerated (same tolerance philosophy as
 *  mesh::validateQuadRegionsDisjoint). */
bool ringsOverlap(const Ring &a, const Ring &b)
{
    const QPolygonF pa = a.toPolygon();
    const QPolygonF pb = b.toPolygon();
    for (const QPointF &p : pa)
        if (mesh::pointInRing(pb, p)) return true;
    for (const QPointF &p : pb)
        if (mesh::pointInRing(pa, p)) return true;
    return false;
}

}   // namespace

// ---------------------------------------------------------------------------
// GeometryType helpers
// ---------------------------------------------------------------------------

QString geometryTypeLabel(GeometryType t)
{
    switch (t) {
    case GeometryType::Point:           return QCoreApplication::translate("FeatureGeometry", "Point");
    case GeometryType::LineString:      return QCoreApplication::translate("FeatureGeometry", "Line");
    case GeometryType::Polygon:         return QCoreApplication::translate("FeatureGeometry", "Polygon");
    case GeometryType::MultiPoint:      return QCoreApplication::translate("FeatureGeometry", "Multi-point");
    case GeometryType::MultiLineString: return QCoreApplication::translate("FeatureGeometry", "Multi-line");
    case GeometryType::MultiPolygon:    return QCoreApplication::translate("FeatureGeometry", "Multi-polygon");
    case GeometryType::None:            break;
    }
    return QCoreApplication::translate("FeatureGeometry", "None");
}

QString geometryTypeToken(GeometryType t)
{
    switch (t) {
    case GeometryType::Point:           return QStringLiteral("point");
    case GeometryType::LineString:      return QStringLiteral("line");
    case GeometryType::Polygon:         return QStringLiteral("polygon");
    case GeometryType::MultiPoint:      return QStringLiteral("multipoint");
    case GeometryType::MultiLineString: return QStringLiteral("multiline");
    case GeometryType::MultiPolygon:    return QStringLiteral("multipolygon");
    case GeometryType::None:            break;
    }
    return QStringLiteral("none");
}

GeometryType geometryTypeFromToken(const QString &token)
{
    const QString t = token.trimmed().toLower();
    if (t == QLatin1String("point"))         return GeometryType::Point;
    if (t == QLatin1String("line"))          return GeometryType::LineString;
    if (t == QLatin1String("linestring"))    return GeometryType::LineString;
    if (t == QLatin1String("polygon"))       return GeometryType::Polygon;
    if (t == QLatin1String("multipoint"))    return GeometryType::MultiPoint;
    if (t == QLatin1String("multiline"))     return GeometryType::MultiLineString;
    if (t == QLatin1String("multilinestring")) return GeometryType::MultiLineString;
    if (t == QLatin1String("multipolygon"))  return GeometryType::MultiPolygon;
    return GeometryType::None;
}

bool isMultiType(GeometryType t)
{
    return t == GeometryType::MultiPoint
        || t == GeometryType::MultiLineString
        || t == GeometryType::MultiPolygon;
}

GeometryType toMultiType(GeometryType t)
{
    switch (t) {
    case GeometryType::Point:      return GeometryType::MultiPoint;
    case GeometryType::LineString: return GeometryType::MultiLineString;
    case GeometryType::Polygon:    return GeometryType::MultiPolygon;
    default:                       return t;
    }
}

// ---------------------------------------------------------------------------
// Ring
// ---------------------------------------------------------------------------

double Ring::zAt(int i) const
{
    if (!hasZ() || i < 0 || i >= z.size()) return kNaN;
    return z.at(i);
}

void Ring::syncZLength()
{
    if (z.isEmpty()) return;              // 2D ring stays 2D
    while (z.size() < pts.size()) z.append(kNaN);
    while (z.size() > pts.size()) z.removeLast();
}

void Ring::ensureZ()
{
    if (hasZ()) return;
    z.assign(pts.size(), kNaN);
}

QPolygonF Ring::toPolygon() const
{
    QPolygonF p;
    p.reserve(pts.size());
    for (const QPointF &v : pts) p << v;
    return p;
}

QRectF Ring::boundingRect() const
{
    return toPolygon().boundingRect();
}

double Ring::signedArea() const
{
    if (pts.size() < 3) return 0.0;
    return mesh::ringSignedArea(toPolygon());
}

double Ring::length2D() const
{
    double len = 0.0;
    for (int i = 1; i < pts.size(); ++i)
        len += std::sqrt(dist2(pts.at(i - 1), pts.at(i)));
    return len;
}

bool Ring::isSimpleRing() const
{
    if (pts.size() < 3) return false;
    return mesh::ringIsSimple(toPolygon());
}

void Ring::normalizeCCW()
{
    if (pts.size() < 3) return;
    if (signedArea() >= 0.0) return;      // already CCW

    // Reverse in place, keeping Z aligned with its vertex. std::reverse on
    // both arrays is correct because they are index-parallel.
    std::reverse(pts.begin(), pts.end());
    if (hasZ()) std::reverse(z.begin(), z.end());
}

bool Ring::containsPoint(const QPointF &p) const
{
    if (pts.size() < 3) return false;
    return mesh::pointInRing(toPolygon(), p);
}

void Ring::insertVertex(int index, const QPointF &p, double zVal)
{
    const qsizetype i = std::clamp<qsizetype>(index, 0, pts.size());
    pts.insert(i, p);
    if (!z.isEmpty()) z.insert(std::clamp<qsizetype>(i, 0, z.size()), zVal);
}

void Ring::removeVertex(int index)
{
    if (index < 0 || index >= pts.size()) return;
    pts.removeAt(index);
    if (index < z.size()) z.removeAt(index);
}

void Ring::moveVertex(int index, const QPointF &p)
{
    if (index < 0 || index >= pts.size()) return;
    pts[index] = p;
}

int Ring::densify(double spacing)
{
    if (spacing <= 0.0 || pts.size() < 2) return 0;

    const bool z3d = hasZ();
    QVector<QPointF> out;
    QVector<double>  outZ;
    out.reserve(pts.size() * 2);
    if (z3d) outZ.reserve(pts.size() * 2);

    int inserted = 0;
    for (int i = 0; i < pts.size() - 1; ++i) {
        const QPointF a = pts.at(i);
        const QPointF b = pts.at(i + 1);
        out.append(a);
        if (z3d) outZ.append(z.at(i));

        const double segLen = std::sqrt(dist2(a, b));
        if (segLen <= spacing) continue;

        // ceil so no sub-segment exceeds `spacing`; the -1 counts only the
        // interior points actually inserted.
        const int n = static_cast<int>(std::ceil(segLen / spacing));
        for (int k = 1; k < n; ++k) {
            const double t = static_cast<double>(k) / static_cast<double>(n);
            out.append(QPointF(a.x() + t * (b.x() - a.x()),
                               a.y() + t * (b.y() - a.y())));
            // Inserted vertices are unsampled by construction; the caller
            // re-samples Z afterwards (plan §4.3).
            if (z3d) outZ.append(kNaN);
            ++inserted;
        }
    }
    out.append(pts.last());
    if (z3d) outZ.append(z.last());

    pts = out;
    if (z3d) z = outZ;
    return inserted;
}

bool Ring::operator==(const Ring &o) const
{
    if (pts != o.pts) return false;
    if (z.size() != o.z.size()) return false;
    for (int i = 0; i < z.size(); ++i) {
        const double a = z.at(i), b = o.z.at(i);
        const bool aNan = std::isnan(a), bNan = std::isnan(b);
        if (aNan != bNan) return false;
        if (!aNan && !qFuzzyCompare(a, b)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Part
// ---------------------------------------------------------------------------

QRectF Part::boundingRect() const
{
    return exterior.boundingRect();       // holes are inside by construction
}

double Part::netArea() const
{
    if (exterior.size() < 3) return 0.0;
    double a = std::fabs(exterior.signedArea());
    for (const Ring &h : holes) a -= std::fabs(h.signedArea());
    return std::max(0.0, a);
}

bool Part::containsPoint(const QPointF &p) const
{
    if (!exterior.containsPoint(p)) return false;
    for (const Ring &h : holes)
        if (h.containsPoint(p)) return false;
    return true;
}

void Part::normalizeOrientation()
{
    exterior.normalizeCCW();
    for (Ring &h : holes) {
        h.normalizeCCW();                 // to CCW first, then flip
        std::reverse(h.pts.begin(), h.pts.end());
        if (h.hasZ()) std::reverse(h.z.begin(), h.z.end());
    }
}

bool Part::operator==(const Part &o) const
{
    return exterior == o.exterior && holes == o.holes;
}

// ---------------------------------------------------------------------------
// FeatureGeometry
// ---------------------------------------------------------------------------

bool FeatureGeometry::isEmpty() const
{
    if (m_parts.isEmpty()) return true;
    for (const Part &p : m_parts)
        if (!p.isEmpty()) return false;
    return true;
}

int FeatureGeometry::vertexCount() const
{
    int n = 0;
    for (const Part &p : m_parts) {
        n += p.exterior.size();
        for (const Ring &h : p.holes) n += h.size();
    }
    return n;
}

bool FeatureGeometry::hasZ() const
{
    bool sawRing = false;
    for (const Part &p : m_parts) {
        if (!p.exterior.isEmpty()) {
            sawRing = true;
            if (!p.exterior.hasZ()) return false;
        }
        for (const Ring &h : p.holes) {
            if (h.isEmpty()) continue;
            sawRing = true;
            if (!h.hasZ()) return false;
        }
    }
    return sawRing;
}

int FeatureGeometry::unsampledZCount() const
{
    int n = 0;
    const auto countRing = [&n](const Ring &r) {
        if (!r.hasZ()) return;
        for (double v : r.z) if (std::isnan(v)) ++n;
    };
    for (const Part &p : m_parts) {
        countRing(p.exterior);
        for (const Ring &h : p.holes) countRing(h);
    }
    return n;
}

bool FeatureGeometry::zRange(double &minZ, double &maxZ) const
{
    bool any = false;
    double lo =  std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();
    const auto scan = [&](const Ring &r) {
        if (!r.hasZ()) return;
        for (double v : r.z) {
            if (std::isnan(v)) continue;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            any = true;
        }
    };
    for (const Part &p : m_parts) {
        scan(p.exterior);
        for (const Ring &h : p.holes) scan(h);
    }
    if (!any) return false;
    minZ = lo;
    maxZ = hi;
    return true;
}

QRectF FeatureGeometry::boundingRect() const
{
    QRectF r;
    for (const Part &p : m_parts) {
        const QRectF pr = p.boundingRect();
        if (pr.isNull()) continue;
        r = r.isNull() ? pr : r.united(pr);
    }
    return r;
}

double FeatureGeometry::area() const
{
    double a = 0.0;
    for (const Part &p : m_parts) a += p.netArea();
    return a;
}

double FeatureGeometry::length() const
{
    double l = 0.0;
    for (const Part &p : m_parts) l += p.exterior.length2D();
    return l;
}

void FeatureGeometry::setHasZ(bool on)
{
    for (Part &p : m_parts) {
        if (on) p.exterior.ensureZ(); else p.exterior.clearZ();
        for (Ring &h : p.holes) { if (on) h.ensureZ(); else h.clearZ(); }
    }
}

int FeatureGeometry::densify(double spacing)
{
    int n = 0;
    for (Part &p : m_parts) {
        n += p.exterior.densify(spacing);
        for (Ring &h : p.holes) n += h.densify(spacing);
    }
    return n;
}

void FeatureGeometry::normalizeOrientation()
{
    for (Part &p : m_parts) p.normalizeOrientation();
}

bool FeatureGeometry::validate(GeometryType expected, QString *reason) const
{
    const auto fail = [reason](const QString &msg) {
        if (reason) *reason = msg;
        return false;
    };

    if (expected == GeometryType::None)
        return fail(QCoreApplication::translate("FeatureGeometry",
                                                "The layer has no geometry type."));
    if (m_parts.isEmpty())
        return fail(QCoreApplication::translate("FeatureGeometry",
                                                "The geometry has no parts."));
    if (!isMultiType(expected) && m_parts.size() != 1)
        return fail(QCoreApplication::translate("FeatureGeometry",
                    "A %1 geometry must have exactly one part (found %2). "
                    "Create the layer as a multi-part type to hold more.")
                    .arg(geometryTypeLabel(expected)).arg(m_parts.size()));

    const bool isPoly  = expected == GeometryType::Polygon
                      || expected == GeometryType::MultiPolygon;
    const bool isPoint = expected == GeometryType::Point
                      || expected == GeometryType::MultiPoint;
    const int  minVerts = isPoly ? 3 : (isPoint ? 1 : 2);

    for (int i = 0; i < m_parts.size(); ++i) {
        const Part &p = m_parts.at(i);
        if (p.exterior.size() < minVerts)
            return fail(QCoreApplication::translate("FeatureGeometry",
                        "Part %1 has %2 vertices; %3 requires at least %4.")
                        .arg(i + 1).arg(p.exterior.size())
                        .arg(geometryTypeLabel(expected)).arg(minVerts));

        if (!isPoly) {
            if (!p.holes.isEmpty())
                return fail(QCoreApplication::translate("FeatureGeometry",
                            "Only polygons can have holes."));
            continue;
        }

        if (!p.exterior.isSimpleRing())
            return fail(QCoreApplication::translate("FeatureGeometry",
                        "Part %1 self-intersects. Rings must be simple.").arg(i + 1));

        for (int h = 0; h < p.holes.size(); ++h) {
            const Ring &hole = p.holes.at(h);
            if (hole.size() < 3)
                return fail(QCoreApplication::translate("FeatureGeometry",
                            "Hole %1 of part %2 has fewer than three vertices.")
                            .arg(h + 1).arg(i + 1));
            if (!hole.isSimpleRing())
                return fail(QCoreApplication::translate("FeatureGeometry",
                            "Hole %1 of part %2 self-intersects.").arg(h + 1).arg(i + 1));
            // Every hole vertex inside the exterior — catches both a hole
            // drawn outside and one that crosses the boundary.
            for (const QPointF &v : hole.pts)
                if (!p.exterior.containsPoint(v))
                    return fail(QCoreApplication::translate("FeatureGeometry",
                                "Hole %1 of part %2 is not inside the polygon.")
                                .arg(h + 1).arg(i + 1));
            for (int k = 0; k < h; ++k)
                if (ringsOverlap(hole, p.holes.at(k)))
                    return fail(QCoreApplication::translate("FeatureGeometry",
                                "Holes %1 and %2 of part %3 overlap.")
                                .arg(k + 1).arg(h + 1).arg(i + 1));
        }
    }

    if (reason) reason->clear();
    return true;
}

// ---------------------------------------------------------------------------
// OGR bridge
// ---------------------------------------------------------------------------

namespace {

/*! Read one OGRLineString / OGRLinearRing into a Ring, dropping the closing
 *  duplicate vertex (FeatureGeometry stores rings open). */
Ring ringFromOgrLine(const OGRLineString *ls, bool closedRing)
{
    Ring r;
    if (!ls) return r;
    const int n = ls->getNumPoints();
    if (n <= 0) return r;

    const bool withZ = (ls->getCoordinateDimension() >= 3) || ls->Is3D();
    int count = n;
    if (closedRing && n >= 2) {
        const QPointF first(ls->getX(0), ls->getY(0));
        const QPointF last(ls->getX(n - 1), ls->getY(n - 1));
        // Compare the DIFFERENCE, not the values: qFuzzyCompare is a relative
        // test and is false for (0.0, 0.0), so a ring closing at the origin
        // would keep its duplicate closing vertex. qFuzzyIsNull(0.0) is true,
        // so this still recognises the exact close a GeoPackage round trip
        // produces.
        if (qFuzzyIsNull(first.x() - last.x()) && qFuzzyIsNull(first.y() - last.y()))
            --count;
    }

    r.pts.reserve(count);
    if (withZ) r.z.reserve(count);
    for (int i = 0; i < count; ++i) {
        r.pts.append(QPointF(ls->getX(i), ls->getY(i)));
        if (withZ) r.z.append(ls->getZ(i));
    }
    return r;
}

Part partFromOgrPolygon(const OGRPolygon *poly)
{
    Part p;
    if (!poly) return p;
    p.exterior = ringFromOgrLine(poly->getExteriorRing(), /*closedRing=*/true);
    const int nh = poly->getNumInteriorRings();
    p.holes.reserve(nh);
    for (int i = 0; i < nh; ++i)
        p.holes.append(ringFromOgrLine(poly->getInteriorRing(i), /*closedRing=*/true));
    return p;
}

/*! Build an OGRLinearRing/OGRLineString from a Ring. \p close appends the
 *  first vertex again, which OGR requires for rings. */
template <typename T>
T *ogrLineFromRing(const Ring &r, bool withZ, bool close)
{
    auto *ls = new T();
    const int n = r.pts.size();
    for (int i = 0; i < n; ++i) {
        const QPointF &p = r.pts.at(i);
        if (withZ) {
            const double zv = r.zAt(i);
            // OGR has no NaN convention; write 0 for unsampled vertices so the
            // file stays readable, and rely on the layer's unsampled counter
            // to tell the user which features still need a Z pass.
            ls->addPoint(p.x(), p.y(), std::isnan(zv) ? 0.0 : zv);
        } else {
            ls->addPoint(p.x(), p.y());
        }
    }
    if (close && n >= 3) {
        const QPointF &p = r.pts.first();
        if (withZ) {
            const double zv = r.zAt(0);
            ls->addPoint(p.x(), p.y(), std::isnan(zv) ? 0.0 : zv);
        } else {
            ls->addPoint(p.x(), p.y());
        }
    }
    return ls;
}

OGRPolygon *ogrPolygonFromPart(const Part &p, bool withZ)
{
    auto *poly = new OGRPolygon();
    poly->addRingDirectly(ogrLineFromRing<OGRLinearRing>(p.exterior, withZ, true));
    for (const Ring &h : p.holes)
        poly->addRingDirectly(ogrLineFromRing<OGRLinearRing>(h, withZ, true));
    return poly;
}

}   // namespace

FeatureGeometry FeatureGeometry::fromOGR(const OGRGeometry *g)
{
    FeatureGeometry out;
    if (!g) return out;

    const OGRwkbGeometryType flat = wkbFlatten(g->getGeometryType());
    const bool withZ = g->Is3D();

    switch (flat) {
    case wkbPoint: {
        const auto *pt = g->toPoint();
        if (!pt) break;
        out.m_type = GeometryType::Point;
        Part p;
        p.exterior.pts.append(QPointF(pt->getX(), pt->getY()));
        if (withZ) p.exterior.z.append(pt->getZ());
        out.m_parts.append(p);
        break;
    }
    case wkbLineString: {
        const auto *ls = g->toLineString();
        if (!ls) break;
        out.m_type = GeometryType::LineString;
        Part p;
        p.exterior = ringFromOgrLine(ls, /*closedRing=*/false);
        out.m_parts.append(p);
        break;
    }
    case wkbPolygon: {
        const auto *poly = g->toPolygon();
        if (!poly) break;
        out.m_type = GeometryType::Polygon;
        out.m_parts.append(partFromOgrPolygon(poly));
        break;
    }
    case wkbMultiPoint: {
        const auto *mp = g->toMultiPoint();
        if (!mp) break;
        out.m_type = GeometryType::MultiPoint;
        for (int i = 0; i < mp->getNumGeometries(); ++i) {
            const auto *pt = mp->getGeometryRef(i)->toPoint();
            if (!pt) continue;
            Part p;
            p.exterior.pts.append(QPointF(pt->getX(), pt->getY()));
            if (withZ) p.exterior.z.append(pt->getZ());
            out.m_parts.append(p);
        }
        break;
    }
    case wkbMultiLineString: {
        const auto *ml = g->toMultiLineString();
        if (!ml) break;
        out.m_type = GeometryType::MultiLineString;
        for (int i = 0; i < ml->getNumGeometries(); ++i) {
            Part p;
            p.exterior = ringFromOgrLine(ml->getGeometryRef(i)->toLineString(), false);
            out.m_parts.append(p);
        }
        break;
    }
    case wkbMultiPolygon: {
        const auto *mp = g->toMultiPolygon();
        if (!mp) break;
        out.m_type = GeometryType::MultiPolygon;
        for (int i = 0; i < mp->getNumGeometries(); ++i)
            out.m_parts.append(partFromOgrPolygon(mp->getGeometryRef(i)->toPolygon()));
        break;
    }
    default:
        // Curves, geometry collections and anything else are outside the
        // editable subset. A None result is the caller's signal to skip the
        // feature rather than an error.
        break;
    }

    return out;
}

OGRGeometry *FeatureGeometry::toOGR(OGRSpatialReference *srs) const
{
    if (m_type == GeometryType::None || m_parts.isEmpty())
        return nullptr;

    const bool withZ = hasZ();
    OGRGeometry *g = nullptr;

    switch (m_type) {
    case GeometryType::Point: {
        const Part &p = m_parts.first();
        if (p.exterior.pts.isEmpty()) return nullptr;
        auto *pt = new OGRPoint(p.exterior.pts.first().x(),
                                p.exterior.pts.first().y());
        if (withZ) {
            const double zv = p.exterior.zAt(0);
            pt->setZ(std::isnan(zv) ? 0.0 : zv);
        }
        g = pt;
        break;
    }
    case GeometryType::LineString:
        g = ogrLineFromRing<OGRLineString>(m_parts.first().exterior, withZ, false);
        break;
    case GeometryType::Polygon:
        g = ogrPolygonFromPart(m_parts.first(), withZ);
        break;
    case GeometryType::MultiPoint: {
        auto *mp = new OGRMultiPoint();
        for (const Part &p : m_parts) {
            if (p.exterior.pts.isEmpty()) continue;
            auto *pt = new OGRPoint(p.exterior.pts.first().x(),
                                    p.exterior.pts.first().y());
            if (withZ) {
                const double zv = p.exterior.zAt(0);
                pt->setZ(std::isnan(zv) ? 0.0 : zv);
            }
            mp->addGeometryDirectly(pt);
        }
        g = mp;
        break;
    }
    case GeometryType::MultiLineString: {
        auto *ml = new OGRMultiLineString();
        for (const Part &p : m_parts)
            ml->addGeometryDirectly(ogrLineFromRing<OGRLineString>(p.exterior, withZ, false));
        g = ml;
        break;
    }
    case GeometryType::MultiPolygon: {
        auto *mp = new OGRMultiPolygon();
        for (const Part &p : m_parts)
            mp->addGeometryDirectly(ogrPolygonFromPart(p, withZ));
        g = mp;
        break;
    }
    case GeometryType::None:
        return nullptr;
    }

    if (g && srs) g->assignSpatialReference(srs);
    return g;
}

int FeatureGeometry::ogrTypeFor(GeometryType t, bool withZ)
{
    OGRwkbGeometryType base = wkbUnknown;
    switch (t) {
    case GeometryType::Point:           base = wkbPoint;           break;
    case GeometryType::LineString:      base = wkbLineString;      break;
    case GeometryType::Polygon:         base = wkbPolygon;         break;
    case GeometryType::MultiPoint:      base = wkbMultiPoint;      break;
    case GeometryType::MultiLineString: base = wkbMultiLineString; break;
    case GeometryType::MultiPolygon:    base = wkbMultiPolygon;    break;
    case GeometryType::None:            return wkbUnknown;
    }
    return withZ ? static_cast<int>(wkbSetZ(base)) : static_cast<int>(base);
}

bool FeatureGeometry::operator==(const FeatureGeometry &o) const
{
    return m_type == o.m_type && m_parts == o.m_parts;
}

}   // namespace openswmmvis::feature
