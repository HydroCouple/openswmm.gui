/*!
 * \file   meshquadtree.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quadtree core mesher — see meshquadtree.h.
 */
#include "mesh/meshquadtree.h"

#include "mesh/pslgprep.h"

#include <QQueue>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

struct Seg { QPointF a, b; };

constexpr int kMaxDepth = 28;   // key packs x/y in 29 bits

double distSqPointSeg(const QPointF &p, const QPointF &a, const QPointF &b)
{
    return pslg::distSqToSegment(p, a, b);
}

bool segmentsCross(const QPointF &p, const QPointF &q, const QPointF &r, const QPointF &s)
{
    auto orient = [](const QPointF &a, const QPointF &b, const QPointF &c) {
        return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
    };
    const double o1 = orient(p, q, r), o2 = orient(p, q, s);
    const double o3 = orient(r, s, p), o4 = orient(r, s, q);
    return ((o1 > 0) != (o2 > 0) || o1 == 0 || o2 == 0)
        && ((o3 > 0) != (o4 > 0) || o3 == 0 || o4 == 0)
        && !(o1 == 0 && o2 == 0 && o3 == 0 && o4 == 0
             && (std::max(p.x(), q.x()) < std::min(r.x(), s.x()) || std::max(r.x(), s.x()) < std::min(p.x(), q.x())
                 || std::max(p.y(), q.y()) < std::min(r.y(), s.y()) || std::max(r.y(), s.y()) < std::min(p.y(), q.y())));
}

/*! Squared distance between a segment and an axis-aligned rectangle;
 *  0 when they touch or cross. */
double segRectDist2(const Seg &s, double x0, double y0, double x1, double y1)
{
    auto inside = [&](const QPointF &p) {
        return p.x() >= x0 && p.x() <= x1 && p.y() >= y0 && p.y() <= y1;
    };
    if (inside(s.a) || inside(s.b)) return 0.0;
    const QPointF c[4] = {QPointF(x0, y0), QPointF(x1, y0), QPointF(x1, y1), QPointF(x0, y1)};
    for (int i = 0; i < 4; ++i)
        if (segmentsCross(s.a, s.b, c[i], c[(i + 1) % 4])) return 0.0;
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 4; ++i)
    {
        best = std::min(best, distSqPointSeg(c[i], s.a, s.b));
        best = std::min(best, distSqPointSeg(s.a, c[i], c[(i + 1) % 4]));
        best = std::min(best, distSqPointSeg(s.b, c[i], c[(i + 1) % 4]));
    }
    return best;
}

struct Leaf
{
    int     level = 0;
    quint32 ix = 0, iy = 0;
    quint8  kept = 0;
    quint8  clear = 0;      ///< No constraint anywhere near: children are kept without tests.
    QVector<int> segs;      ///< Nearby constraint segments (only when !clear).
};

class Builder
{
public:
    Builder(const QVector<QPolygonF> &domains, const QVector<QPolygonF> &holes,
            const QVector<QVector<QPointF>> &constraints, const QuadtreeFrame &frame,
            const QuadtreeOptions &opt, QuadtreeMesh *out)
        : m_opt(opt), m_out(out)
    {
        const double a = frame.angleDeg * M_PI / 180.0;
        m_cos = std::cos(a); m_sin = std::sin(a);
        m_origin = frame.origin;
        m_out->frame = frame;

        QVector<QPolygonF> fDomains, fHoles;
        QRectF bbox;
        for (const QPolygonF &d : domains)
        {
            QPolygonF f = toFrame(d);
            bbox = bbox.isValid() ? bbox.united(f.boundingRect()) : f.boundingRect();
            addRingSegments(f);
            fDomains.append(f);
        }
        for (const QPolygonF &h : holes)
        {
            QPolygonF f = toFrame(h);
            addRingSegments(f);
            fHoles.append(f);
        }
        for (const QVector<QPointF> &c : constraints)
        {
            for (int i = 0; i + 1 < c.size(); ++i)
            {
                const QPointF a = toFrame(c[i]), b = toFrame(c[i + 1]);
                if (a != b) m_segs.append({a, b});
            }
        }
        m_bbox = bbox;
        QVector<QVector<QPointF>> holeRings;
        for (const QPolygonF &h : fHoles) holeRings.append(QVector<QPointF>(h.begin(), h.end()));
        m_inside.build(fDomains, holeRings);
    }

    bool run()
    {
        if (!m_opt.hAt || !(m_opt.hMin > 0.0) || !std::isfinite(m_opt.hMin))
            return fail(QStringLiteral("quadtree: hAt and hMin > 0 are required"));
        if (!m_bbox.isValid() || m_bbox.isEmpty())
            return fail(QStringLiteral("quadtree: empty domain"));
        // Root: a power-of-two multiple of hMin covering the bbox with a
        // margin, centred on it.
        const double extent = std::max(m_bbox.width(), m_bbox.height()) * 1.02;
        m_depth = 0;
        double side = m_opt.hMin;
        while (side < extent && m_depth < kMaxDepth) { side *= 2.0; ++m_depth; }
        if (side < extent) return fail(QStringLiteral("quadtree: domain needs more than 2^28 floor cells across"));
        m_rootSide = side;
        m_rootX0 = m_bbox.center().x() - side * 0.5;
        m_rootY0 = m_bbox.center().y() - side * 0.5;
        m_out->rootX0 = m_rootX0; m_out->rootY0 = m_rootY0; m_out->rootSide = m_rootSide;
        m_out->depth = m_depth;
        m_unit = m_opt.hMin * 0.5;   // vertex lattice unit
        m_maxLeafLevel = m_depth;
        if (m_opt.hMax > 0.0)
            m_minLeafLevel = std::max(0, int(std::floor(std::log2(m_rootSide / m_opt.hMax))));

        if (!subdivide()) return false;
        if (!balance()) return false;
        if (!emitCells()) return false;
        return true;
    }

private:
    bool fail(const QString &msg) { m_out->errorMsg = msg; return false; }

    QPointF toFrame(const QPointF &p) const
    {
        const double dx = p.x() - m_origin.x(), dy = p.y() - m_origin.y();
        return QPointF(m_cos * dx + m_sin * dy, -m_sin * dx + m_cos * dy);
    }
    QPolygonF toFrame(const QPolygonF &poly) const
    {
        QPolygonF f; f.reserve(poly.size());
        for (const QPointF &p : poly) f.append(toFrame(p));
        return f;
    }
    QPointF toMesh(double fx, double fy) const
    {
        return QPointF(m_origin.x() + m_cos * fx - m_sin * fy,
                       m_origin.y() + m_sin * fx + m_cos * fy);
    }
    void addRingSegments(const QPolygonF &r)
    {
        const int n = r.size();
        if (n < 2) return;
        const bool closedDup = r.first() == r.last();
        const int m = closedDup ? n - 1 : n;
        for (int i = 0; i < m; ++i)
        {
            const QPointF &a = r[i], &b = r[(i + 1) % m];
            if (a != b) m_segs.append({a, b});
        }
    }

    double sideAt(int level) const { return m_rootSide / double(quint64(1) << level); }
    double hSample(double fx, double fy) const
    {
        const QPointF m = toMesh(fx, fy);
        const double h = m_opt.hAt(m.x(), m.y());
        return (std::isfinite(h) && h > 0.0) ? h : std::numeric_limits<double>::infinity();
    }
    /*! Smallest target size over the centre and corners of a cell. */
    double hCell(int level, quint32 ix, quint32 iy, double *hCentre) const
    {
        const double s = sideAt(level);
        const double x0 = m_rootX0 + ix * s, y0 = m_rootY0 + iy * s;
        const double hc = hSample(x0 + 0.5 * s, y0 + 0.5 * s);
        if (hCentre) *hCentre = hc;
        double h = hc;
        h = std::min(h, hSample(x0, y0));
        h = std::min(h, hSample(x0 + s, y0));
        h = std::min(h, hSample(x0, y0 + s));
        h = std::min(h, hSample(x0 + s, y0 + s));
        return h;
    }
    bool isLeafSize(int level, double h) const
    {
        if (level >= m_maxLeafLevel) return true;
        if (level < m_minLeafLevel) return false;
        return sideAt(level) <= 1.41421356237309515 * h;
    }
    /*! Classify a cell against a list of nearby segments. */
    void classify(Leaf &L, const QVector<int> &parentSegs, double hCentre)
    {
        const double s = sideAt(L.level);
        const double x0 = m_rootX0 + L.ix * s, y0 = m_rootY0 + L.iy * s;
        const double margin = m_opt.clearance * 3.0 * s;
        L.segs.clear();
        double nearest2 = std::numeric_limits<double>::infinity();
        for (int si : parentSegs)
        {
            const double d2 = segRectDist2(m_segs[si], x0 - margin, y0 - margin, x0 + s + margin, y0 + s + margin);
            if (d2 <= margin * margin)
            {
                L.segs.append(si);
                nearest2 = std::min(nearest2, segRectDist2(m_segs[si], x0, y0, x0 + s, y0 + s));
            }
        }
        const bool centreInside = m_inside.contains(QPointF(x0 + 0.5 * s, y0 + 0.5 * s));
        if (L.segs.isEmpty())
        {
            L.clear = 1;
            L.kept = centreInside ? 1 : 0;
            return;
        }
        L.clear = 0;
        const double clear = m_opt.clearance * (std::isfinite(hCentre) ? hCentre : s);
        L.kept = (centreInside && nearest2 > clear * clear) ? 1 : 0;
    }

    bool subdivide()
    {
        struct Node { int level; quint32 ix, iy; QVector<int> segs; bool clear; };
        QVector<Node> stack;
        {
            QVector<int> all(m_segs.size());
            for (int i = 0; i < all.size(); ++i) all[i] = i;
            stack.append({0, 0, 0, std::move(all), false});
        }
        double done = 0.0;   // fraction of the root area finished, for progress
        int progressTick = 0;
        while (!stack.isEmpty())
        {
            Node n = stack.takeLast();
            double hc = 0.0;
            const double h = hCell(n.level, n.ix, n.iy, &hc);
            Leaf L;
            L.level = n.level; L.ix = n.ix; L.iy = n.iy;
            if (n.clear)
            {
                L.clear = 1; L.kept = 1;
            }
            else
            {
                classify(L, n.segs, hc);
                if (L.clear && !L.kept)
                {
                    // Entirely outside the domain (or inside a hole): prune.
                    done += 1.0 / double(quint64(1) << (2 * n.level));
                    continue;
                }
            }
            if (isLeafSize(n.level, h))
            {
                addLeaf(std::move(L));
                if (m_leaves.size() > m_opt.maxLeaves)
                    return fail(QStringLiteral("quadtree: more than %1 leaves — raise the cell size or the floor").arg(m_opt.maxLeaves));
                done += 1.0 / double(quint64(1) << (2 * n.level));
                if (++progressTick % 4096 == 0 && m_opt.progress && !m_opt.progress(0.6 * std::min(done, 1.0)))
                    return fail(QStringLiteral("cancelled"));
                continue;
            }
            const bool childClear = L.clear && L.kept;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                    stack.append({n.level + 1, 2 * n.ix + dx, 2 * n.iy + dy,
                                  childClear ? QVector<int>() : L.segs, childClear});
        }
        return true;
    }

    int addLeaf(Leaf L)
    {
        const quint64 k = quadtreeKey(L.level, L.ix, L.iy);
        m_leaves.append(std::move(L));
        m_index.insert(k, m_leaves.size() - 1);
        return m_leaves.size() - 1;
    }

    /*! Index of the leaf that covers cell (level, ix, iy) at that level or
     *  any coarser one; -1 when none (finer leaves there, or pruned). */
    int findCovering(int level, quint32 ix, quint32 iy) const
    {
        for (int l = level; l >= 0; --l)
        {
            const auto it = m_index.constFind(quadtreeKey(l, ix, iy));
            if (it != m_index.constEnd() && m_leaves[it.value()].level >= 0) return it.value();
            ix >>= 1; iy >>= 1;
        }
        return -1;
    }

    /*! Split leaf \p li into four children; returns their indices. */
    void splitLeaf(int li, int children[4])
    {
        Leaf parent = m_leaves[li];
        m_index.remove(quadtreeKey(parent.level, parent.ix, parent.iy));
        m_leaves[li].level = -1;   // tombstone
        int c = 0;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx)
            {
                Leaf child;
                child.level = parent.level + 1;
                child.ix = 2 * parent.ix + dx; child.iy = 2 * parent.iy + dy;
                if (parent.clear) { child.clear = 1; child.kept = parent.kept; }
                else
                {
                    double hc = 0.0;
                    hCell(child.level, child.ix, child.iy, &hc);
                    classify(child, parent.segs, hc);
                }
                children[c++] = addLeaf(std::move(child));
            }
        ++m_out->balanceSplits;
    }

    bool balance()
    {
        QQueue<int> queue;
        for (int i = 0; i < m_leaves.size(); ++i) queue.enqueue(i);
        int tick = 0;
        while (!queue.isEmpty())
        {
            const int li = queue.dequeue();
            if (m_leaves[li].level < 0) continue;
            const int l = m_leaves[li].level;
            const quint32 ix = m_leaves[li].ix, iy = m_leaves[li].iy;
            const quint32 n = quint32(1) << l;
            const int dxs[4] = {1, -1, 0, 0}, dys[4] = {0, 0, 1, -1};
            bool requeue = false;
            for (int d = 0; d < 4; ++d)
            {
                const qint64 nx = qint64(ix) + dxs[d], ny = qint64(iy) + dys[d];
                if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
                const int cover = findCovering(l, quint32(nx), quint32(ny));
                if (cover < 0) continue;
                if (m_leaves[cover].level < l - 1)
                {
                    int ch[4];
                    splitLeaf(cover, ch);
                    for (int k = 0; k < 4; ++k) queue.enqueue(ch[k]);
                    requeue = true;
                }
            }
            if (requeue) queue.enqueue(li);
            if (++tick % 8192 == 0 && m_opt.progress && !m_opt.progress(0.6 + 0.2 * (1.0 - double(queue.size()) / double(m_leaves.size() + 1))))
                return fail(QStringLiteral("cancelled"));
        }
        return true;
    }

    // ── Cell emission ─────────────────────────────────────────────────

    int vertexAt(qint64 ux, qint64 uy)
    {
        const quint64 key = (quint64(ux) << 32) | quint64(uy);
        const auto it = m_vertexIndex.constFind(key);
        if (it != m_vertexIndex.constEnd()) return it.value();
        const int id = m_out->vertices.size();
        m_out->vertices.append(toMesh(m_rootX0 + ux * m_unit, m_rootY0 + uy * m_unit));
        m_vertexIndex.insert(key, id);
        return id;
    }
    bool keptAt(int level, qint64 ix, qint64 iy) const
    {
        if (ix < 0 || iy < 0) return false;
        const auto it = m_index.constFind(quadtreeKey(level, quint32(ix), quint32(iy)));
        return it != m_index.constEnd() && m_leaves[it.value()].level >= 0 && m_leaves[it.value()].kept;
    }
    void pushCell(int a, int b, int c, int d)
    {
        if (!m_opt.triangles || d < 0)
        {
            m_out->cells.append({a, b, c, d});
            return;
        }
        // Split through the widest corner.
        const int v[4] = {a, b, c, d};
        int wide = 0; double best = 2.0;
        for (int i = 0; i < 4; ++i)
        {
            const QPointF &p = m_out->vertices[v[(i + 3) % 4]], &q = m_out->vertices[v[i]], &r = m_out->vertices[v[(i + 1) % 4]];
            const QPointF e1 = p - q, e2 = r - q;
            const double cosA = (e1.x() * e2.x() + e1.y() * e2.y()) / (std::hypot(e1.x(), e1.y()) * std::hypot(e2.x(), e2.y()));
            if (cosA < best) { best = cosA; wide = i; }
        }
        m_out->cells.append({v[wide], v[(wide + 1) % 4], v[(wide + 2) % 4], -1});
        m_out->cells.append({v[wide], v[(wide + 2) % 4], v[(wide + 3) % 4], -1});
    }

    bool emitCells()
    {
        // Deterministic order: sort live leaves by key.
        QVector<int> order;
        order.reserve(m_leaves.size());
        for (int i = 0; i < m_leaves.size(); ++i)
            if (m_leaves[i].level >= 0) order.append(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const Leaf &A = m_leaves[a], &B = m_leaves[b];
            return quadtreeKey(A.level, A.ix, A.iy) < quadtreeKey(B.level, B.ix, B.iy);
        });
        m_out->leaves = order.size();

        // Canonical templates on corners A B C D (0..3), midpoints M0..M3
        // (4..7) and the centre E (8). A quad needs an even boundary, so
        // patterns with 1 or 3 hanging nodes carry one triangle.
        struct Tmpl { int pattern; int rot; int n; int cells[4][4]; };
        static const Tmpl kTemplates[] = {
            {0b0000, 0, 1, {{0, 1, 2, 3}}},
            {0b0001, 0, 3, {{0, 4, 8, 3}, {4, 1, 2, 8}, {8, 2, 3, -1}}},
            {0b0011, 0, 3, {{0, 4, 8, 3}, {4, 1, 5, 8}, {5, 2, 3, 8}}},
            {0b0101, 0, 2, {{0, 4, 6, 3}, {4, 1, 2, 6}}},
            {0b0111, 0, 4, {{0, 4, 8, 3}, {4, 1, 5, 8}, {5, 2, 6, 8}, {8, 6, 3, -1}}},
            {0b1111, 0, 4, {{0, 4, 8, 7}, {4, 1, 5, 8}, {8, 5, 2, 6}, {7, 8, 6, 3}}},
        };

        QHash<quint64, int> edgeCount;
        auto edgeKey = [](int a, int b) {
            return (quint64(std::min(a, b)) << 32) | quint64(std::max(a, b));
        };
        int tick = 0;
        for (int li : order)
        {
            const Leaf &L = m_leaves[li];
            if (!L.kept) continue;
            ++m_out->keptLeaves;
            const int firstCell = m_out->cells.size();
            const qint64 scale = qint64(1) << (m_depth - L.level);   // lattice units per half cell
            const qint64 ux = 2 * qint64(L.ix) * scale, uy = 2 * qint64(L.iy) * scale;
            const qint64 full = 2 * scale;
            // Local vertex lattice ids.
            int vid[9];
            vid[0] = vertexAt(ux, uy);                 // A
            vid[1] = vertexAt(ux + full, uy);          // B
            vid[2] = vertexAt(ux + full, uy + full);   // C
            vid[3] = vertexAt(ux, uy + full);          // D
            // Hanging nodes: a finer KEPT leaf across an edge.
            const int cl = L.level + 1;
            const qint64 cx = 2 * qint64(L.ix), cy = 2 * qint64(L.iy);
            const bool h0 = keptAt(cl, cx, cy - 1) || keptAt(cl, cx + 1, cy - 1);      // bottom
            const bool h1 = keptAt(cl, cx + 2, cy) || keptAt(cl, cx + 2, cy + 1);      // right
            const bool h2 = keptAt(cl, cx, cy + 2) || keptAt(cl, cx + 1, cy + 2);      // top
            const bool h3 = keptAt(cl, cx - 1, cy) || keptAt(cl, cx - 1, cy + 1);      // left
            const bool hang[4] = {h0, h1, h2, h3};
            int pattern = 0;
            for (int e = 0; e < 4; ++e) if (hang[e]) pattern |= 1 << e;
            // Find the rotation that maps the pattern onto a canonical one.
            const Tmpl *t = nullptr; int rot = 0;
            for (int r = 0; r < 4 && !t; ++r)
            {
                int p = 0;
                for (int e = 0; e < 4; ++e) if (hang[(e + r) % 4]) p |= 1 << e;
                for (const Tmpl &c : kTemplates) if (c.pattern == p) { t = &c; rot = r; break; }
            }
            if (!t) return fail(QStringLiteral("quadtree: unmatched hanging pattern"));
            if (pattern)
            {
                ++m_out->templateCells;
                const qint64 hs = scale;   // half side in lattice units
                vid[4] = vertexAt(ux + hs, uy);          // M0 bottom
                vid[5] = vertexAt(ux + full, uy + hs);   // M1 right
                vid[6] = vertexAt(ux + hs, uy + full);   // M2 top
                vid[7] = vertexAt(ux, uy + hs);          // M3 left
                vid[8] = vertexAt(ux + hs, uy + hs);     // E
            }
            // Canonical local id → rotated actual id.
            auto actual = [&](int c) {
                if (c < 0) return -1;
                if (c < 4) return vid[(c + rot) % 4];
                if (c < 8) return vid[4 + ((c - 4 + rot) % 4)];
                return vid[8];
            };
            if (pattern == 0 && m_opt.triangles)
            {
                // Alternate the diagonal on the lattice parity.
                if (((L.ix + L.iy) & 1) == 0) { m_out->cells.append({vid[0], vid[1], vid[2], -1}); m_out->cells.append({vid[0], vid[2], vid[3], -1}); }
                else                          { m_out->cells.append({vid[0], vid[1], vid[3], -1}); m_out->cells.append({vid[1], vid[2], vid[3], -1}); }
            }
            else
            {
                for (int k = 0; k < t->n; ++k)
                    pushCell(actual(t->cells[k][0]), actual(t->cells[k][1]), actual(t->cells[k][2]), actual(t->cells[k][3]));
            }
            m_out->leafCells.insert(quadtreeKey(L.level, L.ix, L.iy),
                                    qMakePair(firstCell, m_out->cells.size() - firstCell));
            // Front bookkeeping: the cell's outline edges (with hanging
            // midpoints) count once per kept leaf.
            const int outline[8] = {vid[0], hang[0] ? vid[4] : -1, vid[1], hang[1] ? vid[5] : -1,
                                    vid[2], hang[2] ? vid[6] : -1, vid[3], hang[3] ? vid[7] : -1};
            int prev = outline[0];
            for (int k = 1; k <= 8; ++k)
            {
                const int v = outline[k % 8];
                if (v < 0) continue;
                ++edgeCount[edgeKey(prev, v)];
                prev = v;
            }
            if (++tick % 8192 == 0 && m_opt.progress && !m_opt.progress(0.8 + 0.2 * double(tick) / double(order.size())))
                return fail(QStringLiteral("cancelled"));
        }
        for (auto it = edgeCount.constBegin(); it != edgeCount.constEnd(); ++it)
            if (it.value() == 1)
                m_out->frontEdges.append(qMakePair(int(it.key() >> 32), int(it.key() & 0xffffffffu)));
        std::sort(m_out->frontEdges.begin(), m_out->frontEdges.end());
        // Leaf status for containsPoint().
        m_out->leafStatus.reserve(order.size());
        for (int li : order)
        {
            const Leaf &L = m_leaves[li];
            m_out->leafStatus.insert(quadtreeKey(L.level, L.ix, L.iy), L.kept);
        }
        return true;
    }

    const QuadtreeOptions &m_opt;
    QuadtreeMesh *m_out;
    double m_cos = 1.0, m_sin = 0.0;
    QPointF m_origin;
    QVector<Seg> m_segs;
    pslg::PointInRingsIndex m_inside;
    QRectF m_bbox;
    double m_rootX0 = 0.0, m_rootY0 = 0.0, m_rootSide = 0.0, m_unit = 0.0;
    int m_depth = 0, m_maxLeafLevel = 0, m_minLeafLevel = 0;
    QVector<Leaf> m_leaves;
    QHash<quint64, int> m_index;
    QHash<quint64, int> m_vertexIndex;
};

} // namespace

quint64 QuadtreeMesh::leafKeyAt(const QPointF &p) const
{
    if (rootSide <= 0.0 || leafStatus.isEmpty()) return 0;
    const double a = frame.angleDeg * M_PI / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    const double dx = p.x() - frame.origin.x(), dy = p.y() - frame.origin.y();
    const double fx = c * dx + s * dy, fy = -s * dx + c * dy;
    const double n = double(quint64(1) << depth);
    const double gx = (fx - rootX0) / rootSide * n, gy = (fy - rootY0) / rootSide * n;
    if (!(gx >= 0.0) || !(gy >= 0.0) || gx >= n || gy >= n) return 0;
    quint32 ix = quint32(gx), iy = quint32(gy);
    for (int l = depth; l >= 0; --l)
    {
        const quint64 k = quadtreeKey(l, ix, iy);
        if (leafStatus.contains(k)) return k;
        ix >>= 1; iy >>= 1;
    }
    return 0;
}

bool QuadtreeMesh::containsPoint(const QPointF &p) const
{
    const quint64 k = leafKeyAt(p);
    return k != 0 && leafStatus.value(k) != 0;
}

int QuadtreeMesh::cellAt(const QPointF &p) const
{
    const quint64 k = leafKeyAt(p);
    if (k == 0) return -1;
    const auto it = leafCells.constFind(k);
    if (it == leafCells.constEnd()) return -1;
    for (int c = it.value().first; c < it.value().first + it.value().second; ++c)
    {
        const QuadtreeCell &cell = cells[c];
        const int v[4] = {cell.v0, cell.v1, cell.v2, cell.v3};
        const int n = cell.v3 < 0 ? 3 : 4;
        bool inside = true;
        for (int e = 0; e < n && inside; ++e)
        {
            const QPointF &a = vertices[v[e]], &b = vertices[v[(e + 1) % n]];
            if ((b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x()) < 0.0) inside = false;
        }
        if (inside) return c;
    }
    return -1;
}

bool buildQuadtreeCore(const QVector<QPolygonF> &domains,
                       const QVector<QPolygonF> &holes,
                       const QVector<QVector<QPointF>> &constraints,
                       const QuadtreeFrame &frame,
                       const QuadtreeOptions &opt,
                       QuadtreeMesh *out)
{
    if (!out) return false;
    *out = QuadtreeMesh();
    Builder b(domains, holes, constraints, frame, opt, out);
    return b.run();
}

} // namespace mesh
