/*!
 * \file   sms2dmreader.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/sms2dmreader.h"
#include "mesh/meshcellgeom.h"

#include <QFile>
#include <QTextStream>
#include <QHash>
#include <QStringList>

namespace mesh {

namespace {

/*! Whitespace-split; empty for blank lines. 2DM has no inline comments. */
QStringList tokenize(const QString &raw)
{
    return raw.simplified().split(QChar(' '), Qt::SkipEmptyParts);
}

} // namespace

MeshResult Sms2dmReader::read(const QString &path, bool splitQuads)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        MeshResult r;
        r.errorMsg = QStringLiteral("Cannot open %1 for reading.").arg(path);
        return r;
    }
    return parse(QString::fromUtf8(f.readAll()), splitQuads);
}

MeshResult Sms2dmReader::parse(const QString &text, bool splitQuads)
{
    MeshResult out;
    auto fail = [&out](int lineNo, const QString &msg) -> MeshResult {
        out.ok = false;
        out.errorMsg = QStringLiteral("2DM line %1: %2").arg(lineNo).arg(msg);
        return out;
    };

    // Elements reference nodes by 1-based id; ids may be sparse, so map
    // them to vertex indices. Elements are resolved after every ND card has
    // been seen — SMS writes elements BEFORE nodes.
    QHash<int, int> nodeIndex;
    struct Elem { int line; int n[4]; int nv; QString tag; };
    QVector<Elem> tris, quads;

    const QStringList lines = text.split(QChar('\n'));
    for (int i = 0; i < lines.size(); ++i)
    {
        const QStringList tok = tokenize(lines[i]);
        if (tok.isEmpty()) continue;
        const QString &card = tok[0];
        const int lineNo = i + 1;

        if (card == QLatin1String("ND")) {
            if (tok.size() < 5)
                return fail(lineNo, QStringLiteral("ND needs id x y z"));
            bool oki = false, okx = false, oky = false, okz = false;
            const int id = tok[1].toInt(&oki);
            MeshVertex v;
            v.xy = QPointF(tok[2].toDouble(&okx), tok[3].toDouble(&oky));
            v.z  = tok[4].toDouble(&okz);
            if (!oki || !okx || !oky || !okz)
                return fail(lineNo, QStringLiteral("ND non-numeric value"));
            if (nodeIndex.contains(id))
                return fail(lineNo, QStringLiteral("duplicate node id %1").arg(id));
            nodeIndex.insert(id, out.vertices.size());
            out.vertices.append(v);
            continue;
        }

        const bool e3t = card == QLatin1String("E3T");
        const bool e4q = card == QLatin1String("E4Q");
        if (!e3t && !e4q) continue;   // other cards carry no geometry

        const int nv = e4q ? 4 : 3;
        if (tok.size() < 2 + nv)
            return fail(lineNo, QStringLiteral("%1 needs id and %2 node ids").arg(card).arg(nv));
        Elem el;
        el.line = lineNo;
        el.nv = nv;
        el.n[3] = -1;
        for (int k = 0; k < nv; ++k) {
            bool okn = false;
            el.n[k] = tok[2 + k].toInt(&okn);
            if (!okn)
                return fail(lineNo, QStringLiteral("%1 non-integer node id").arg(card));
        }
        if (tok.size() >= 3 + nv)
            el.tag = QStringLiteral("mat%1").arg(tok[2 + nv]);
        (e4q ? quads : tris).append(el);
    }

    // Resolve node ids and emit cells: triangles first, then quads (engine
    // order); with splitQuads every quad becomes two triangles in place.
    auto resolve = [&](Elem &el) -> QString {
        for (int k = 0; k < el.nv; ++k) {
            const auto it = nodeIndex.constFind(el.n[k]);
            if (it == nodeIndex.constEnd())
                return QStringLiteral("unknown node id %1").arg(el.n[k]);
            el.n[k] = it.value();
        }
        return {};
    };
    for (Elem &el : tris) {
        const QString err = resolve(el);
        if (!err.isEmpty()) return fail(el.line, err);
        MeshTriangle t;
        t.v0 = el.n[0]; t.v1 = el.n[1]; t.v2 = el.n[2];
        t.tag = el.tag;
        out.triangles.append(t);
    }
    for (Elem &el : quads) {
        const QString err = resolve(el);
        if (!err.isEmpty()) return fail(el.line, err);
        MeshTriangle q;
        q.v0 = el.n[0]; q.v1 = el.n[1]; q.v2 = el.n[2]; q.v3 = el.n[3];
        q.tag = el.tag;
        if (!splitQuads) {
            out.triangles.append(q);
            continue;
        }
        const CellGeom g = cellGeom(out.vertices, q);
        for (int s = 0; s < g.nSub; ++s) {
            MeshTriangle t;
            t.v0 = g.sub[s][0]; t.v1 = g.sub[s][1]; t.v2 = g.sub[s][2];
            t.tag = el.tag;
            out.triangles.append(t);
        }
    }

    if (out.vertices.isEmpty() || out.triangles.isEmpty()) {
        out.errorMsg = QStringLiteral("2DM file has no ND / E3T / E4Q cards.");
        return out;
    }
    out.ok = true;
    return out;
}


bool Sms2dmReader::looksLikeSms2dm(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    int seen = 0;
    while (!in.atEnd() && seen < 200) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        ++seen;
        if (line.startsWith(QLatin1Char('['))) return false;      // SWMMVis sections
        if (line.startsWith(QLatin1String(";;"))) continue;        // comment
        const QString card = line.section(QLatin1Char(' '), 0, 0).toUpper();
        if (card == QLatin1String("MESH2D") || card == QLatin1String("ND") ||
            card == QLatin1String("E3T")    || card == QLatin1String("E4Q") ||
            card == QLatin1String("E2L")    || card == QLatin1String("E6T") ||
            card == QLatin1String("E8Q")    || card == QLatin1String("E9Q"))
            return true;
    }
    return false;
}

} // namespace mesh
