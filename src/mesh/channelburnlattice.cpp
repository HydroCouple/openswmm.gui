/*!
 * \file   channelburnlattice.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The channel corridor as mesh input (CHANNEL_BURN_IN_PLAN_2026-09-21.md
 * §4.6, §4.7, §16.2 — phases P2 and P3).
 */
#include "mesh/channelburnlattice.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {
double cross(const QPointF &a,const QPointF &b) { return a.x()*b.y()-a.y()*b.x(); }
bool barycentric(const QPointF *p,const QPointF &q,double *u,double *v)
{
    const double d=cross(p[1]-p[0],p[2]-p[0]);
    if(std::abs(d)<1e-18) return false;
    *u=cross(q-p[0],p[2]-p[0])/d;
    *v=cross(p[1]-p[0],q-p[0])/d;
    return *u>=-1e-9 && *v>=-1e-9 && *u+*v<=1+1e-9;
}
QRectF triangleBounds(const QPointF *p) {
    return QRectF(QPointF(std::min({p[0].x(),p[1].x(),p[2].x()}),std::min({p[0].y(),p[1].y(),p[2].y()})),
                  QPointF(std::max({p[0].x(),p[1].x(),p[2].x()}),std::max({p[0].y(),p[1].y(),p[2].y()})));
}
}

void BurnSurface::build(const QVector<BurnLattice> &lattices)
{
    faces.clear(); grid.clear();
    double sum=0;
    for(int pi=0;pi<lattices.size();++pi) {
        const auto &lat=lattices[pi];
        for(int i=0;i+1<lat.nAlong;++i) for(int k=0;k+1<lat.nAcross;++k) {
            const int ids[4]={lat.at(i,k),lat.at(i+1,k),lat.at(i+1,k+1),lat.at(i,k+1)};
            for(int half=0;half<2;++half) {
                Face f; f.profile=pi;
                const int c[3]={0,half?2:1,half?3:2};
                for(int j=0;j<3;++j) {
                    f.p[j]=lat.xy[ids[c[j]]]; f.z[j]=lat.z[ids[c[j]]];
                    f.offset[j]=lat.offsets[k+(c[j]>=2?1:0)];
                }
                if(std::abs(cross(f.p[1]-f.p[0],f.p[2]-f.p[0]))<1e-18) continue;
                f.bounds=triangleBounds(f.p); sum+=std::max(f.bounds.width(),f.bounds.height());
                faces.append(f);
            }
        }
    }
    QRectF bounds;
    for(const auto &face:faces) bounds=bounds.united(face.bounds);
    surfaceBounds=bounds.adjusted(-1e-9,-1e-9,1e-9,1e-9);
    origin=bounds.topLeft();
    pitch=faces.isEmpty()?1:std::max({1e-6,sum/faces.size(),bounds.width()/1e8,bounds.height()/1e8});
    // A rare very large face amongst small reaches must not allocate a
    // rectangle containing billions of empty buckets.
    const double bucketBudget=std::max(4096.0,16.0*faces.size());
    for(;;) {
        double entries=0;
        for(const auto &face:faces) {
            entries+=(std::ceil(face.bounds.width()/pitch)+2)*(std::ceil(face.bounds.height()/pitch)+2);
            if(entries>bucketBudget) break;
        }
        if(entries<=bucketBudget) break;
        pitch*=2;
    }
    for(int id=0;id<faces.size();++id) {
        const auto &b=faces[id].bounds;
        for(int x=int(std::floor((b.left()-origin.x())/pitch));x<=int(std::floor((b.right()-origin.x())/pitch));++x)
            for(int y=int(std::floor((b.top()-origin.y())/pitch));y<=int(std::floor((b.bottom()-origin.y())/pitch));++y)
                grid[{x,y}].append(id);
    }
}

QVector<int> BurnSurface::candidates(const QRectF &b) const
{
    QSet<int> ids;
    if(faces.isEmpty() || !surfaceBounds.intersects(b)) return {};
    const QRectF clipped=b.intersected(surfaceBounds);
    const int x0=int(std::floor((clipped.left()-origin.x())/pitch)),x1=int(std::floor((clipped.right()-origin.x())/pitch));
    const int y0=int(std::floor((clipped.top()-origin.y())/pitch)),y1=int(std::floor((clipped.bottom()-origin.y())/pitch));
    if(qint64(x1-x0+1)*(y1-y0+1)>grid.size()) {
        for(int i=0;i<faces.size();++i) if(faces[i].bounds.intersects(b)) ids.insert(i);
    } else for(int x=x0;x<=x1;++x) for(int y=y0;y<=y1;++y)
        for(int id:grid.value({x,y})) ids.insert(id);
    return ids.values();
}

bool BurnSurface::intersects(const QRectF &bounds) const
{
    for(int id:candidates(bounds)) if(faces[id].bounds.intersects(bounds)) return true;
    return false;
}

BurnSurface::Hit BurnSurface::sample(const QPointF &p) const
{
    Hit hit;
    if(faces.isEmpty() || !surfaceBounds.contains(p)) return hit;
    const auto it=grid.constFind({int(std::floor((p.x()-origin.x())/pitch)),int(std::floor((p.y()-origin.y())/pitch))});
    if(it==grid.cend()) return hit;
    for(int id:*it) {
        const auto &f=faces[id]; double u=0,v=0;
        if(!barycentric(f.p,p,&u,&v)) continue;
        const double z=f.z[0]+u*(f.z[1]-f.z[0])+v*(f.z[2]-f.z[0]);
        if(!std::isfinite(hit.z) || z<hit.z || (z==hit.z && f.profile<hit.profile))
            hit={z,f.profile,f.offset[0]+u*(f.offset[1]-f.offset[0])+v*(f.offset[2]-f.offset[0])};
    }
    return hit;
}

QVector<BurnSurface::Hit> BurnSurface::hitsAt(const QPointF &p) const
{
    QVector<Hit> out;
    if(faces.isEmpty() || !surfaceBounds.contains(p)) return out;
    const auto it=grid.constFind({int(std::floor((p.x()-origin.x())/pitch)),int(std::floor((p.y()-origin.y())/pitch))});
    if(it==grid.cend()) return out;
    for(int id:*it) {
        const auto &f=faces[id]; double u=0,v=0;
        if(!barycentric(f.p,p,&u,&v)) continue;
        out.append({f.z[0]+u*(f.z[1]-f.z[0])+v*(f.z[2]-f.z[0]),f.profile,f.offset[0]+u*(f.offset[1]-f.offset[0])+v*(f.offset[2]-f.offset[0])});
    }
    return out;
}

BurnSurface::Hit BurnSurface::sampleNear(const QPointF &p, double radius) const
{
    Hit hit = sample(p);
    if (hit.profile >= 0 || !(radius > 0.0)) return hit;
    double best = radius * radius;
    for (int id : candidates(QRectF(p.x() - radius, p.y() - radius, 2 * radius, 2 * radius))) {
        const auto &f = faces[id];
        // Closest point on the triangle: the nearest point of its three edges
        // (p is outside it, or sample() would have hit).
        for (int e = 0; e < 3; ++e) {
            const QPointF a = f.p[e], d = f.p[(e + 1) % 3] - a;
            const double len2 = QPointF::dotProduct(d, d);
            const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, d) / len2, 0.0, 1.0) : 0.0;
            const QPointF q = a + d * t;
            const double dist2 = QPointF::dotProduct(p - q, p - q);
            if (dist2 > best) continue;
            const double z = f.z[e] + t * (f.z[(e + 1) % 3] - f.z[e]);
            const double off = f.offset[e] + t * (f.offset[(e + 1) % 3] - f.offset[e]);
            if (dist2 < best || !std::isfinite(hit.z) || z < hit.z) { best = dist2; hit = {z, f.profile, off}; }
        }
    }
    return hit;
}

BurnSurface::Error BurnSurface::error(const QPointF *xy,const double *z,
                                     const std::function<bool(const QPointF &)> &inside) const
{
    Error out;
    const double orientation=cross(xy[1]-xy[0],xy[2]-xy[0])>=0?1:-1;
    for(int id:candidates(triangleBounds(xy))) {
        const auto &f=faces[id];
        QVector<QPointF> poly{f.p[0],f.p[1],f.p[2]};
        for(int e=0;e<3 && !poly.isEmpty();++e) {
            QVector<QPointF> next;
            const QPointF a=xy[e],d=xy[(e+1)%3]-a;
            for(int j=0;j<poly.size();++j) {
                const QPointF p=poly[j],q=poly[(j+1)%poly.size()];
                const double dp=orientation*cross(d,p-a),dq=orientation*cross(d,q-a);
                if(dp>=-1e-10) next.append(p);
                if((dp>0 && dq<0)||(dp<0 && dq>0)) next.append(p+(q-p)*(dp/(dp-dq)));
            }
            poly=std::move(next);
        }
        for(const auto &p:poly) {
            if(inside && !inside(p)) continue;
            double u=0,v=0; if(!barycentric(xy,p,&u,&v)) continue;
            // Check every authored face, including overlaps. Sampling only
            // their lower envelope at polygon corners can hide an interior
            // ridge where two conflicting channel planes intersect.
            double fu=0,fv=0;if(!barycentric(f.p,p,&fu,&fv)) continue;
            const double reference=f.z[0]+fu*(f.z[1]-f.z[0])+fv*(f.z[2]-f.z[0]);
            out.touched=true;
            const double delta=std::abs(reference-(z[0]+u*(z[1]-z[0])+v*(z[2]-z[0])));
            if(delta>out.maximum) {out.maximum=delta;out.point=p;}
        }
    }
    return out;
}

namespace {

/*! Point at chainage \p t along a polyline with cumulative \p chain. */
QPointF pointAtChainage(const QVector<QPointF> &path, const QVector<double> &chain, double t)
{
    const int n = path.size();
    if (n == 0) return {};
    if (n == 1 || t <= chain.first()) return path.first();
    if (t >= chain.last()) return path.last();

    const auto it = std::upper_bound(chain.cbegin(), chain.cend(), t);
    const int hi = int(it - chain.cbegin());
    const int lo = hi - 1;
    const double d = chain[hi] - chain[lo];
    const double f = (d > 0.0) ? (t - chain[lo]) / d : 0.0;
    return path[lo] + (path[hi] - path[lo]) * f;
}

/*! Unit normal pointing RIGHT of the downstream direction at chainage \p t.
 *
 *  Taken from a central difference half a step either side rather than from the
 *  containing segment: at a bend vertex the segment normal jumps, and offset
 *  rows built on it fold. The averaged direction is the same thing a mitre
 *  joint approximates. */
QPointF rightNormalAt(const QVector<QPointF> &path, const QVector<double> &chain,
                      double t, double h)
{
    const double L = chain.isEmpty() ? 0.0 : chain.last();
    const double a = std::max(0.0, t - h);
    const double b = std::min(L,   t + h);
    QPointF p0 = pointAtChainage(path, chain, a);
    QPointF p1 = pointAtChainage(path, chain, b);
    double dx = p1.x() - p0.x(), dy = p1.y() - p0.y();
    double len = std::hypot(dx, dy);
    if (!(len > 0.0))
    {
        dx = path.last().x() - path.first().x();
        dy = path.last().y() - path.first().y();
        len = std::hypot(dx, dy);
        if (!(len > 0.0)) return QPointF(0.0, -1.0);
    }
    return QPointF(dy / len, -dx / len);   // right of (dx, dy)
}

/*! Manning's n and the sub-region suffix for a cell centred at offset \p m. */
void roughnessFor(const NormalizedSection &s, double m, double *n, QString *suffix)
{
    if (std::isfinite(s.leftBank) && m < s.leftBank)
    { *n = s.nLeft;  *suffix = QStringLiteral(":left");  return; }
    if (std::isfinite(s.rightBank) && m > s.rightBank)
    { *n = s.nRight; *suffix = QStringLiteral(":right"); return; }
    *n = s.nChannel; *suffix = QStringLiteral(":chan");
}

} // namespace

// Douglas-Peucker over the section offsets, measured at every station at
// once: offset k between kept a and b may go only if the straight line from
// a to b stays within tol of relZ[i][k] at every station i.
static QVector<double> thinOffsets(const BurnProfile &p, double tol, double maxGap)
{
    const int n = int(p.offsets.size());
    QVector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    int centre = 0;
    for (int k = 1; k < n; ++k)
        if (std::abs(p.offsets[k]) < std::abs(p.offsets[centre])) centre = k;
    keep[centre] = 1;
    QVector<QPair<int, int>> stack{{0, centre}, {centre, n - 1}};
    while (!stack.isEmpty()) {
        const auto [a, b] = stack.takeLast();
        if (b - a < 2) continue;
        double worst = 0; int at = -1;
        const double sa = p.offsets[a], sb = p.offsets[b];
        for (int k = a + 1; k < b; ++k) {
            const double t = (p.offsets[k] - sa) / (sb - sa);
            for (const auto &rel : p.relZ) {
                const double e = std::abs(rel[k] - (rel[a] + t * (rel[b] - rel[a])));
                if (e > worst) { worst = e; at = k; }
            }
        }
        if (at >= 0 && worst > tol) { keep[at] = 1; stack.append({a, at}); stack.append({at, b}); }
    }
    // Keep lateral resolution: no kept gap may exceed maxGap, so structural
    // offset strings (collinear by design) survive where they are spaced out.
    if (maxGap > 0.0) {
        int last = 0;
        for (int k = 1; k < n; ++k) {
            if (!keep[k]) continue;
            int prev = last;
            while (p.offsets[k] - p.offsets[prev] > maxGap) {
                int pick = -1;
                for (int j = prev + 1; j < k; ++j)
                    if (p.offsets[j] - p.offsets[prev] <= maxGap) pick = j;
                if (pick < 0) break;
                keep[pick] = 1; prev = pick;
            }
            last = k;
        }
    }
    QVector<double> out;
    for (int k = 0; k < n; ++k) if (keep[k]) out.append(p.offsets[k]);
    return out;
}

/*! Fraction of the mesh minimum cell below which lattice offsets or
 *  stations merge. */
constexpr double kMergeFraction = 0.05;

/*! Drop values of the ascending \p v closer than \p gap to the previous kept
 *  one. The first and last always stay; with \p keepNearestZero so does the
 *  value nearest zero (the thalweg), and a dropped neighbour yields to it. */
static QVector<double> mergeCloseValues(const QVector<double> &v, double gap, bool keepNearestZero)
{
    const int n = int(v.size());
    if (n < 3 || !(gap > 0.0)) return v;
    int centre = -1;
    if (keepNearestZero) {
        centre = 0;
        for (int k = 1; k < n; ++k) if (std::abs(v[k]) < std::abs(v[centre])) centre = k;
    }
    const auto pinned = [&](int k) { return k == 0 || k == n - 1 || k == centre; };
    QVector<int> kept{0};
    for (int k = 1; k < n; ++k) {
        if (v[k] - v[kept.last()] >= gap) { kept.append(k); continue; }
        if (!pinned(k)) continue;
        // A pinned value always stays and displaces unpinned ones crowding it.
        while (!pinned(kept.last()) && v[k] - v[kept.last()] < gap) kept.removeLast();
        kept.append(k);
    }
    QVector<double> out;
    out.reserve(kept.size());
    for (int k : kept) out.append(v[k]);
    return out;
}

BurnLattice buildCorridorLattice(const BurnProfile &p,
                                 double alongStep, double minCellSize,
                                 QStringList *warnings, QString *err,
                                 double acrossTolerance)
{
    BurnLattice lat;
    if (!p.isValid())
    {
        if (err) *err = QStringLiteral("conduit '%1': profile is not valid").arg(p.conduitId);
        return lat;
    }
    lat.conduitId = p.conduitId;
    // Thin only sections whose offsets are denser than the mesh floor (dense
    // survey or lidar sections); authored analytic sections stay as built.
    const double width = p.offsets.last() - p.offsets.first();
    const bool dense = acrossTolerance > 0.0 && minCellSize > 0.0 && p.offsets.size() > 2
                       && p.offsets.size() > width / minCellSize + 1.0;
    lat.offsets = dense ? thinOffsets(p, acrossTolerance, alongStep > 0.0 ? alongStep : 4.0 * minCellSize)
                        : p.offsets;
    // Offsets closer than a small fraction of the mesh floor (a transect's
    // vertical wall is authored as two stations 1e-5 apart) cannot be told
    // apart by any cell, and the slivers they leave shrink the constraint
    // join tolerance for the whole mesh. Merge them, keeping the banks and
    // the thalweg.
    if (minCellSize > 0.0)
        lat.offsets = mergeCloseValues(lat.offsets, kMergeFraction * minCellSize, true);
    lat.nAcross   = int(lat.offsets.size());
    if (lat.nAcross < 2)
    {
        if (err) *err = QStringLiteral("conduit '%1': corridor has fewer than 2 offsets")
                            .arg(p.conduitId);
        return lat;
    }

    // Preserve authored bends and section-change stations, then subdivide.
    const double L = p.length();
    if (alongStep > 0.0)
    {
        const double estimated = std::ceil(L/alongStep)+p.chainage.size();
        if (!std::isfinite(estimated) || estimated*lat.nAcross > 2000000) {
            if(err) *err=QStringLiteral("corridor exceeds the preparation vertex budget; increase spacing");
            return {};
        }
        lat.chainage.append(0.0);
        for (int j=1;j<p.chainage.size();++j) {
            const double a=p.chainage[j-1], b=p.chainage[j];
            const int n=std::max(1,int(std::ceil((b-a)/alongStep)));
            for(int i=1;i<=n;++i) lat.chainage.append(a+(b-a)*i/n);
        }
    }
    else
    {
        lat.chainage = p.chainage;
    }
    // Section-change stations a hair from a bend vertex: same reasoning.
    if (minCellSize > 0.0)
        lat.chainage = mergeCloseValues(lat.chainage, kMergeFraction * minCellSize, false);
    lat.nAlong = int(lat.chainage.size());
    if (qint64(lat.nAlong)*lat.nAcross > 2000000) {
        if(err) *err=QStringLiteral("corridor exceeds the preparation vertex budget");
        return {};
    }
    if (lat.nAlong < 2)
    {
        if (err) *err = QStringLiteral("conduit '%1': corridor has fewer than 2 stations")
                            .arg(p.conduitId);
        return lat;
    }

    // Dense along-channel sampling must not concentrate the entire rotation
    // of a bank into one tiny interval at an authored bend. Use a physical
    // section-width neighbourhood for directions, independent of DEM pixels.
    const double h = std::max(std::min(lat.offsets.last()-lat.offsets.first(),L*0.05),
                              0.5 * (L / double(lat.nAlong - 1)));
    lat.xy.reserve(qsizetype(lat.nAlong) * lat.nAcross);
    lat.z .reserve(qsizetype(lat.nAlong) * lat.nAcross);

    for (int i = 0; i < lat.nAlong; ++i)
    {
        const double  t = lat.chainage[i];
        const QPointF c = pointAtChainage(p.centerline, p.chainage, t);
        const QPointF nrm = rightNormalAt(p.centerline, p.chainage, t, h);
        for (int k = 0; k < lat.nAcross; ++k)
        {
            const double s = lat.offsets[k];
            lat.xy.append(c + nrm * s);
            bool inExtent = false;
            double zv = sectionZAt(p, t, s, &inExtent);
            if (!std::isfinite(zv))
            {
                // Only reachable at a clipped extent's own endpoint through
                // rounding; the bed is always defined, so fall back to it.
                zv = bedZAt(p, t);
            }
            lat.z.append(zv);
        }
    }

    // The reference surface must be valid even when the final corridor uses
    // triangles. Reject folded cells before burning pixels or retiring links.
    double winding=0;
    for(int i=0;i+1<lat.nAlong;++i) for(int k=0;k+1<lat.nAcross;++k) {
        const QPointF corners[4]={lat.xy[lat.at(i,k)],lat.xy[lat.at(i+1,k)],
            lat.xy[lat.at(i+1,k+1)],lat.xy[lat.at(i,k+1)]};
        for(int j=0;j<4;++j) {
            const double turn=cross(corners[(j+1)%4]-corners[j],corners[(j+2)%4]-corners[(j+1)%4]);
            if(winding==0) winding=turn;
            if(std::abs(turn)<1e-18 || turn*winding<=0) {
                if(err) *err=QStringLiteral("conduit '%1': corridor folds at a bend; reduce its width or correct the centreline").arg(p.conduitId);
                return {};
            }
        }
    }

    // Spacings, for the densification guard (§4.6).
    lat.minAlongSpacing = std::numeric_limits<double>::infinity();
    for (int i = 1; i < lat.nAlong; ++i)
        lat.minAlongSpacing = std::min(lat.minAlongSpacing, lat.chainage[i] - lat.chainage[i - 1]);
    lat.minAcrossSpacing = std::numeric_limits<double>::infinity();
    for (int k = 1; k < lat.nAcross; ++k)
        lat.minAcrossSpacing = std::min(lat.minAcrossSpacing, lat.offsets[k] - lat.offsets[k - 1]);

    if (minCellSize > 0.0 && warnings)
    {
        const double worst = std::min(lat.minAlongSpacing, lat.minAcrossSpacing);
        if (worst < minCellSize)
            warnings->append(
                QStringLiteral("conduit '%1': corridor lattice spacing %2 is below the mesh "
                               "minimum cell size %3 — channel edges are retained and the "
                               "completed mesh must pass the channel accuracy check")
                    .arg(p.conduitId).arg(worst).arg(minCellSize));
    }
    return lat;
}

namespace {

PatchMesh corridorPatchGeometry(const BurnLattice &lat, QString *err)
{
    PatchMesh pm;
    if (!lat.isValid())
    {
        if (err) *err = QStringLiteral("conduit '%1': lattice is not valid").arg(lat.conduitId);
        return pm;
    }

    pm.tag = QStringLiteral("channel:%1").arg(lat.conduitId);
    pm.xy  = lat.xy;

    // Cells. The lattice is conformal, so each (i, k) cell is simply its four
    // lattice corners; winding is fixed per cell rather than assumed, because a
    // reversed centreline would otherwise emit the whole patch clockwise.
    pm.quads.reserve(qsizetype(lat.nAlong - 1) * (lat.nAcross - 1));
    for (int i = 0; i + 1 < lat.nAlong; ++i)
    {
        for (int k = 0; k + 1 < lat.nAcross; ++k)
        {
            MeshTriangle q;
            q.v0 = lat.at(i,     k);
            q.v1 = lat.at(i,     k + 1);
            q.v2 = lat.at(i + 1, k + 1);
            q.v3 = lat.at(i + 1, k);
            // Subtract a local origin before multiplying: small cells in a
            // projected CRS otherwise lose their winding to cancellation.
            const QPointF origin = pm.xy[q.v0];
            double twiceArea = 0.0;
            for (int edge = 0; edge < 4; ++edge) {
                const QPointF a = pm.xy[q.vertex(edge)] - origin;
                const QPointF b = pm.xy[q.vertex((edge + 1) % 4)] - origin;
                twiceArea += a.x() * b.y() - b.x() * a.y();
            }
            if (twiceArea < 0.0) std::swap(q.v1, q.v3);
            pm.quads.append(q);
        }
    }

    // Boundary loop: down the left edge, across the tail, up the right edge,
    // back across the head — the ring Triangle will hold as constraints.
    auto pushSeg = [&pm](int a, int b) { if (a != b) pm.boundarySegments.append(qMakePair(a, b)); };
    for (int i = 0; i + 1 < lat.nAlong;  ++i) pushSeg(lat.at(i, 0), lat.at(i + 1, 0));
    for (int k = 0; k + 1 < lat.nAcross; ++k)
        pushSeg(lat.at(lat.nAlong - 1, k), lat.at(lat.nAlong - 1, k + 1));
    for (int i = lat.nAlong - 1; i > 0; --i)
        pushSeg(lat.at(i, lat.nAcross - 1), lat.at(i - 1, lat.nAcross - 1));
    for (int k = lat.nAcross - 1; k > 0; --k) pushSeg(lat.at(0, k), lat.at(0, k - 1));

    const QString bad = validate(pm);
    if (!bad.isEmpty())
    {
        if (err) *err = QStringLiteral("conduit '%1': %2 (the corridor folds — smooth the "
                                       "centreline or reduce the corridor width)")
                            .arg(lat.conduitId, bad);
        return PatchMesh();
    }

    return pm;
}

} // namespace

PatchMesh corridorPatch(const BurnLattice &lat, const BurnProfile &p, const BurnOptions &opt,
                        QString *err)
{
    PatchMesh pm = corridorPatchGeometry(lat, err);
    if (pm.quads.isEmpty() || !opt.roughnessFromTransect) return pm;

    int c = 0;
    for (int i = 0; i + 1 < lat.nAlong; ++i)
        for (int k = 0; k + 1 < lat.nAcross; ++k, ++c)
        {
            const double m = 0.5 * (lat.offsets[k] + lat.offsets[k + 1]);
            double n = qQNaN();
            QString suffix;
            roughnessFor(p.section, m, &n, &suffix);
            pm.quads[c].tag = QStringLiteral("channel:%1%2").arg(lat.conduitId, suffix);
            if (std::isfinite(n) && n > 0.0) pm.quads[c].mannings = n;
        }
    return pm;
}

QVector<ConstraintSegment> corridorStrings(const BurnLattice &lat, int markerBase,
                                           QHash<int, QString> *markerToTag)
{
    QVector<ConstraintSegment> out;
    if (!lat.isValid()) return out;

    out.reserve(lat.nAcross);
    for (int k = 0; k < lat.nAcross; ++k)
    {
        ConstraintSegment cs;
        cs.path.reserve(lat.nAlong);
        for (int i = 0; i < lat.nAlong; ++i) cs.path.append(lat.xy[lat.at(i, k)]);
        cs.marker = markerBase + k;
        cs.tag    = QStringLiteral("burn:%1:%2").arg(lat.conduitId).arg(k);
        if (markerToTag) markerToTag->insert(cs.marker, cs.tag);
        out.append(cs);
    }
    return out;
}

QVector<SteinerPoint> corridorPoints(const BurnLattice &lat, int marker)
{
    QVector<SteinerPoint> out;
    if (!lat.isValid()) return out;

    out.reserve(lat.xy.size());
    for (int idx = 0; idx < lat.xy.size(); ++idx)
    {
        SteinerPoint sp;
        sp.xy   = lat.xy[idx];
        sp.z    = lat.z[idx];
        sp.hasZ = true;
        sp.marker = marker;
        sp.tag  = QStringLiteral("burn:%1").arg(lat.conduitId);
        out.append(sp);
    }
    return out;
}

QPolygonF corridorRing(const BurnLattice &lat)
{
    QPolygonF ring;
    if (!lat.isValid()) return ring;

    ring.reserve(2 * (lat.nAlong + lat.nAcross));
    for (int k = 0; k < lat.nAcross; ++k)            ring << lat.xy[lat.at(0, k)];
    for (int i = 1; i < lat.nAlong; ++i)             ring << lat.xy[lat.at(i, lat.nAcross - 1)];
    for (int k = lat.nAcross - 2; k >= 0; --k)       ring << lat.xy[lat.at(lat.nAlong - 1, k)];
    for (int i = lat.nAlong - 2; i >= 1; --i)        ring << lat.xy[lat.at(i, 0)];
    return normalizeRingCCW(ring);
}

void corridorZSeeds(const BurnLattice &lat, QVector<QPointF> *xy, QVector<double> *z)
{
    if (!lat.isValid() || !xy || !z) return;
    xy->reserve(xy->size() + lat.xy.size());
    z ->reserve(z->size()  + lat.z.size());
    for (int i = 0; i < lat.xy.size(); ++i)
    {
        if (!std::isfinite(lat.z[i])) continue;
        xy->append(lat.xy[i]);
        z ->append(lat.z[i]);
    }
}

} // namespace mesh
