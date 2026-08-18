/*!
 * \file   inp_pslg.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Shared by the two minimum-cell-size diagnostics in this folder: read a real
 * SWMM .inp and assemble the same PSLG shape the mesh-generation worker builds
 * from it — nodes as tagged Steiner points, conduits as tagged constraint
 * polylines, and the convex hull of the nodes standing in for the boundary
 * layer.  Deliberately minimal: no engine, no GDAL, no project window.
 */
#ifndef OPENSWMMVIS_TESTS_MANUAL_INP_PSLG_H
#define OPENSWMMVIS_TESTS_MANUAL_INP_PSLG_H

#include "mesh/meshgenerator.h"

#include <QFile>
#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace probe {

struct Pslg
{
    QVector<QPolygonF>                domains;
    QVector<mesh::ConstraintSegment>  segs;
    QVector<mesh::SteinerPoint>       pts;
};

namespace detail {

struct Model
{
    QHash<QString, QPointF>          nodeXY;
    QVector<QString>                 nodeOrder;
    QVector<QPair<QString, QString>> linkEnds;
    QVector<QString>                 linkOrder;
    QHash<QString, QVector<QPointF>> linkVerts;
};

inline bool readInp(const QString &path, Model *m, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    { *err = QStringLiteral("cannot open %1").arg(path); return false; }

    QTextStream in(&f);
    QString section;
    while (!in.atEnd())
    {
        QString line = in.readLine();
        const int semi = line.indexOf(QLatin1Char(';'));
        if (semi >= 0) line = line.left(semi);
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('[')))
        { section = line.mid(1, line.indexOf(QLatin1Char(']')) - 1).toUpper(); continue; }

        const QStringList tok = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                           Qt::SkipEmptyParts);
        if (section == QLatin1String("COORDINATES"))
        {
            if (tok.size() < 3) continue;
            bool okx = false, oky = false;
            const double x = tok[1].toDouble(&okx), y = tok[2].toDouble(&oky);
            if (!okx || !oky) continue;
            if (!m->nodeXY.contains(tok[0])) m->nodeOrder.append(tok[0]);
            m->nodeXY.insert(tok[0], QPointF(x, y));
        }
        else if (section == QLatin1String("VERTICES"))
        {
            if (tok.size() < 3) continue;
            bool okx = false, oky = false;
            const double x = tok[1].toDouble(&okx), y = tok[2].toDouble(&oky);
            if (!okx || !oky) continue;
            m->linkVerts[tok[0]].append(QPointF(x, y));
        }
        else if (section == QLatin1String("CONDUITS"))
        {
            if (tok.size() < 3) continue;
            m->linkOrder.append(tok[0]);
            m->linkEnds.append(qMakePair(tok[1], tok[2]));
        }
    }
    if (m->nodeXY.isEmpty()) { *err = QStringLiteral("no [COORDINATES]"); return false; }
    return true;
}

/*! Monotone-chain convex hull; the domain stand-in for the boundary layer. */
inline QPolygonF convexHull(QVector<QPointF> p)
{
    std::sort(p.begin(), p.end(), [](const QPointF &a, const QPointF &b) {
        return a.x() != b.x() ? a.x() < b.x() : a.y() < b.y();
    });
    p.erase(std::unique(p.begin(), p.end()), p.end());
    if (p.size() < 3) return QPolygonF(p);

    auto cross = [](const QPointF &o, const QPointF &a, const QPointF &b) {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };
    QVector<QPointF> h(2 * p.size());
    int k = 0;
    for (const QPointF &q : std::as_const(p))
    {
        while (k >= 2 && cross(h[k - 2], h[k - 1], q) <= 0) --k;
        h[k++] = q;
    }
    const int lower = k + 1;
    for (int i = p.size() - 2; i >= 0; --i)
    {
        while (k >= lower && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k;
        h[k++] = p[i];
    }
    h.resize(k - 1);
    return QPolygonF(h);
}

/*! Expand a ring outward from its centroid, so no conduit endpoint sits ON the
 *  domain edge (which the worker's in-domain filter would drop). */
inline QPolygonF inflate(const QPolygonF &ring, double f)
{
    QPointF c(0, 0);
    for (const QPointF &q : ring) c += q;
    c /= double(ring.size());
    QPolygonF out;
    out.reserve(ring.size());
    for (const QPointF &q : ring) out.append(c + (q - c) * f);
    return out;
}

} // namespace detail

/*! Read \p inpPath and assemble the PSLG.  Returns false with \p err set. */
inline bool loadPslg(const QString &inpPath, Pslg *g, QString *err)
{
    detail::Model m;
    if (!detail::readInp(inpPath, &m, err)) return false;

    QVector<QPointF> all;
    for (const QString &n : std::as_const(m.nodeOrder)) all.append(m.nodeXY[n]);
    g->domains = {detail::inflate(detail::convexHull(all), 1.02)};

    int marker = 1;
    for (const QString &n : std::as_const(m.nodeOrder))
    {
        mesh::SteinerPoint sp;
        sp.xy = m.nodeXY[n]; sp.marker = marker++; sp.tag = n;
        g->pts.append(sp);
    }
    for (int i = 0; i < m.linkOrder.size(); ++i)
    {
        const QString &id = m.linkOrder[i];
        const auto it0 = m.nodeXY.constFind(m.linkEnds[i].first);
        const auto it1 = m.nodeXY.constFind(m.linkEnds[i].second);
        if (it0 == m.nodeXY.constEnd() || it1 == m.nodeXY.constEnd()) continue;
        QVector<QPointF> path;
        path.append(*it0);
        path += m.linkVerts.value(id);
        path.append(*it1);
        QVector<QPointF> dedup;                    // as dedupeSegPath does upstream
        for (const QPointF &q : std::as_const(path))
            if (dedup.isEmpty() || dedup.last() != q) dedup.append(q);
        if (dedup.size() < 2) continue;
        mesh::ConstraintSegment cs;
        cs.path = dedup; cs.marker = marker++; cs.tag = id;
        g->segs.append(cs);
    }
    return true;
}

} // namespace probe

#endif // OPENSWMMVIS_TESTS_MANUAL_INP_PSLG_H
