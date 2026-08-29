/*!
 * \file   inpmeshreader.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/inpmeshreader.h"
#include "mesh/meshbctype.h"
#include "mesh/meshinfil.h"

#include <QChar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPair>

#include <algorithm>

namespace mesh {

namespace {

constexpr const char *kSecVertices       = "[2D_VERTICES]";
constexpr const char *kSecTriangles      = "[2D_TRIANGLES]";
constexpr const char *kSecMeshFile       = "[2D_MESH_FILE]";
constexpr const char *kSecVertexNodeMap  = "[2D_VERTEX_NODE_MAP]";      // 1D<->2D coupling
constexpr const char *kSecTriangleNodeMap = "[2D_TRIANGLE_NODE_MAP]";   // node→cell coupling (Part C)
constexpr const char *kSecBC             = "[2D_BOUNDARY_CONDITIONS]";  // §V.VD.1
constexpr const char *kSecConveyance     = "[2D_EDGE_CONVEYANCE]";       // Engine §11A
constexpr const char *kSecInfilOptions   = "[2D_INFILTRATION_OPTIONS]";  // GG0a
constexpr const char *kSecInfilDefaults  = "[2D_INFILTRATION_DEFAULTS]"; // GG0a
constexpr const char *kSecInfil          = "[2D_INFILTRATION]";          // GG0a

/*! Per-row pre-mesh BC accumulator: (flat-index, value). Resolved to a
 *  sized QVector<MeshEdgeBC> after the mesh is known. */
struct BCRow { int flat = -1; MeshEdgeBC bc; };
using BCRowList = QVector<BCRow>;

/*! Per-row pre-mesh conveyance accumulator: (FROM_VERTEX, TO_VERTEX, value).
 *  Engine §11A format — the row keys an edge by its endpoints, not by
 *  (tri, eLocal), so resolution requires walking the mesh after it loads
 *  and writing the value into every matching slot (covers the interior-
 *  edge symmetry mirror in one pass). */
struct ConveyanceRow { int va = -1; int vb = -1; double conv = 1.0; };
using ConveyanceRowList = QVector<ConveyanceRow>;

bool readTextFile(const QString &path, QString *out, QString *err)
{
    QFile f(path);
    if (!f.exists()) {
        if (err) *err = QStringLiteral("File does not exist: %1").arg(path);
        return false;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = QStringLiteral("Cannot open %1 for reading.").arg(path);
        return false;
    }
    *out = QString::fromUtf8(f.readAll());
    return true;
}

/*! Token-split a data line. Returns empty vector for comment/blank lines.
 *
 *  Hand-rolled rather than `split(QRegularExpression("\\s+"))`: this runs once
 *  per line, i.e. ~2.3M times on a 1.5M-triangle `.2dm`, and the regex path
 *  allocated a fresh QStringList plus a match context every time. Splitting on
 *  QChar::isSpace over a view of the original string is behaviourally
 *  identical (`\s` and isSpace agree on the whitespace this format uses) and
 *  allocates only the tokens themselves. */
QStringList tokenize(const QString &raw)
{
    const QStringView line = QStringView(raw).left(
        [&raw] {
            const qsizetype semi = raw.indexOf(QChar(';'));
            return semi >= 0 ? semi : raw.size();
        }());

    QStringList out;
    qsizetype i = 0;
    const qsizetype n = line.size();
    while (i < n) {
        while (i < n && line[i].isSpace()) ++i;
        if (i >= n) break;
        const qsizetype start = i;
        while (i < n && !line[i].isSpace()) ++i;
        out.append(line.mid(start, i - start).toString());
    }
    return out;
}

/*! GG0a — parse `METHOD [P1..P5] [DEST]` starting at \p first into \p row.
 *
 *  Mirrors the engine's `parseInfil2DRowTail` (SectionHandlers2D.cpp):
 *  parameter columns are POSITIONAL and in PROJECT units, `-` means unset
 *  (left as NaN here rather than 0, so the GUI can tell "not written" from
 *  "written as zero"), trailing unused columns may be omitted, and DEST is
 *  the final token when it is neither numeric nor `-`. Returns an empty
 *  string on success, an error message otherwise. */
QString parseInfilRowTail(const QStringList &tok, int first,
                          const char *secName, InfilRow &row)
{
    const QString sec = QString::fromLatin1(secName);
    if (tok.size() <= first)
        return QStringLiteral("%1 missing METHOD").arg(sec);

    bool okm = false;
    row.method = infilMethodFromToken(tok[first], &okm);
    if (!okm)
        return QStringLiteral("%1 unknown METHOD: %2").arg(sec, tok[first]);

    int end = int(tok.size());
    if (end > first + 1) {
        const QString &last = tok[end - 1];
        bool numeric = false;
        (void)last.toDouble(&numeric);
        if (!numeric && last != QLatin1String("-")) {
            bool okd = false;
            row.dest = infilDestFromToken(last, &okd);
            if (!okd)
                return QStringLiteral("%1 unknown DEST: %2").arg(sec, last);
            --end;
        }
    }

    for (int k = first + 1; k < end; ++k) {
        const int idx = k - (first + 1);
        if (idx >= kInfilMaxParams)
            return QStringLiteral("%1 too many parameter columns (max %2)")
                .arg(sec).arg(kInfilMaxParams);
        if (tok[k] == QLatin1String("-")) continue;   // unset
        bool okv = false;
        const double v = tok[k].toDouble(&okv);
        if (!okv)
            return QStringLiteral("%1 invalid parameter: %2").arg(sec, tok[k]);
        row.p[idx] = v;
    }
    return {};
}

/*! GG0a — `INFIL_STEP` value: `h:mm:ss`, `h:mm`, or plain seconds. Matches
 *  the engine's `openswmm::input::parse_time_seconds`. Returns a negative
 *  value when the token is not a duration at all. */
double parseInfilStepSeconds(const QString &token)
{
    bool ok = false;
    const double plain = token.toDouble(&ok);
    if (ok) return plain;                     // plain number = seconds

    const QStringList parts = token.split(QChar(':'));
    if (parts.isEmpty()) return -1.0;
    double secs = 0.0;
    const double scale[3] = {3600.0, 60.0, 1.0};
    for (int i = 0; i < parts.size() && i < 3; ++i) {
        bool okp = false;
        const double v = parts[i].toDouble(&okp);
        if (!okp) return -1.0;
        secs += v * scale[i];
    }
    return secs;
}

/*! Parse the body lines of a section into the mesh result.
 *
 *  Returns empty string on success, an error message otherwise. Unknown
 *  section names are silently ignored so we round-trip whatever options /
 *  coupling-map sections happen to share the file with the mesh.
 */
QString parseSection(const QString &sectionName,
                     const QStringList &bodyLines,
                     MeshResult &out)
{
    if (sectionName.compare(QLatin1String(kSecVertices), Qt::CaseInsensitive) == 0)
    {
        for (const QString &raw : bodyLines)
        {
            const QStringList tok = tokenize(raw);
            if (tok.isEmpty()) continue;
            if (tok.size() < 3)
                return QStringLiteral("[2D_VERTICES] needs X Y Z (got: %1)").arg(raw.trimmed());
            bool okx = false, oky = false, okz = false;
            const double x = tok[0].toDouble(&okx);
            const double y = tok[1].toDouble(&oky);
            const double z = tok[2].toDouble(&okz);
            if (!okx || !oky || !okz)
                return QStringLiteral("[2D_VERTICES] non-numeric value (got: %1)").arg(raw.trimmed());
            MeshVertex v;
            v.xy = QPointF(x, y);
            v.z  = z;
            if (tok.size() >= 4) v.tag = tok[3];
            out.vertices.append(v);
        }
        return {};
    }

    if (sectionName.compare(QLatin1String(kSecTriangles), Qt::CaseInsensitive) == 0)
    {
        for (const QString &raw : bodyLines)
        {
            const QStringList tok = tokenize(raw);
            if (tok.isEmpty()) continue;
            if (tok.size() < 3)
                return QStringLiteral("[2D_TRIANGLES] needs V1 V2 V3 (got: %1)").arg(raw.trimmed());
            bool ok0 = false, ok1 = false, ok2 = false;
            const int v0 = tok[0].toInt(&ok0);
            const int v1 = tok[1].toInt(&ok1);
            const int v2 = tok[2].toInt(&ok2);
            if (!ok0 || !ok1 || !ok2)
                return QStringLiteral("[2D_TRIANGLES] non-integer vertex index (got: %1)").arg(raw.trimmed());
            MeshTriangle t;
            t.v0 = v0; t.v1 = v1; t.v2 = v2;
            // tok[3] is MANNINGS_N. tok[4] is INIT_DEPTH when numeric
            // (engine format `V1 V2 V3 MANNINGS_N [INIT_DEPTH] [TAG]`),
            // otherwise the historical TAG; tok[5] is TAG after a depth.
            if (tok.size() >= 4) {
                bool okn = false;
                const double n = tok[3].toDouble(&okn);
                if (okn) t.mannings = n;
            }
            if (tok.size() >= 5) {
                bool okd = false;
                const double d = tok[4].toDouble(&okd);
                if (okd) {
                    t.initDepth = d;
                    if (tok.size() >= 6) t.tag = tok[5];
                } else {
                    t.tag = tok[4];
                }
            }
            out.triangles.append(t);
        }
        return {};
    }

    // [2D_VERTEX_NODE_MAP] — VERTEX NODE [CD AREA]. The engine's authoritative
    // 1D<->2D coupling, distinct from the [2D_VERTICES] TAG column. Lands on
    // MeshVertex::coupledNode (the descriptive tag keeps its own field); the
    // optional CD / AREA columns land on couplingCd / couplingArea, keeping
    // the engine defaults (0.65 / 1.0 m²) when omitted.
    if (sectionName.compare(QLatin1String(kSecVertexNodeMap),
                            Qt::CaseInsensitive) == 0)
    {
        // Lazily built on the first tag-form row (see below). Stays empty for
        // an all-index-form map, so index-form files pay nothing.
        QHash<QString, int> tagToVertex;
        for (const QString &raw : bodyLines)
        {
            const QStringList tok = tokenize(raw);
            if (tok.size() < 2) continue;
            // VERTEX_INDEX_OR_TAG: numeric index first; anything else — or a
            // number out of range — is resolved against the vertex TAG column.
            // The writer PREFERS the tag form (robust to renumbering), and
            // generated junction vertices always carry tag == node id, so
            // without this fallback a GUI-written map never round-tripped
            // through this reader (it only survived via the engine).
            bool okv = false;
            int v = tok[0].toInt(&okv);
            if (okv && (v < 0 || v >= out.vertices.size()))
                okv = false;   // numeric but not a valid index — try as tag
            if (!okv)
            {
                // Tag form. The old code scanned every vertex per row — and
                // did NOT break on a miss — so a saved mesh (the writer
                // PREFERS tag form) cost nCoupled x nVertices string compares:
                // ~66 s at 100k coupled rows over 760k vertices, inside the
                // worker with no progress output, i.e. indistinguishable from
                // a hang. Build the index once, lazily, on the first tag row.
                if (tagToVertex.isEmpty() && !out.vertices.isEmpty())
                {
                    tagToVertex.reserve(out.vertices.size());
                    // First occurrence wins, matching the old forward scan.
                    for (int i = out.vertices.size() - 1; i >= 0; --i)
                        if (!out.vertices[i].tag.isEmpty())
                            tagToVertex.insert(out.vertices[i].tag, i);
                }
                v = tagToVertex.value(tok[0], -1);
                if (v < 0) continue;
            }
            out.vertices[v].coupledNode = tok[1];
            if (tok.size() >= 3) {
                bool okc = false;
                const double cd = tok[2].toDouble(&okc);
                if (okc) out.vertices[v].couplingCd = cd;
            }
            if (tok.size() >= 4) {
                bool oka = false;
                const double a = tok[3].toDouble(&oka);
                if (oka) out.vertices[v].couplingArea = a;
            }
        }
        return {};
    }

    // [2D_TRIANGLE_NODE_MAP] — TRIANGLE NODE [CD AREA], repeated-row form
    // (Plan Part C): several nodes may couple to one triangle, one row each.
    // Rows land on MeshResult::cellCouplings so the layer / toolbar can show
    // and edit them. Tag-form triangle references (subcatchment tags) are
    // resolved to the FIRST triangle carrying the tag — same rule as the
    // engine parser. Legacy single-node rows parse identically.
    if (sectionName.compare(QLatin1String(kSecTriangleNodeMap),
                            Qt::CaseInsensitive) == 0)
    {
        // Same lazy tag index as [2D_VERTEX_NODE_MAP]. This scan is latent
        // today (patchAttributeSections leaves triangleToNode empty, so only
        // index-form rows get written) but fires the moment triangle coupling
        // is emitted — over 1.5M triangles.
        QHash<QString, int> tagToTriangle;
        for (const QString &raw : bodyLines)
        {
            const QStringList tok = tokenize(raw);
            if (tok.size() < 2) continue;
            bool okt = false;
            int t = tok[0].toInt(&okt);
            if (okt && (t < 0 || t >= out.triangles.size()))
                okt = false;
            if (!okt)
            {
                if (tagToTriangle.isEmpty() && !out.triangles.isEmpty())
                {
                    // First triangle carrying the tag wins — same rule as the
                    // engine parser and as the old forward scan.
                    for (int i = out.triangles.size() - 1; i >= 0; --i)
                        if (!out.triangles[i].tag.isEmpty())
                            tagToTriangle.insert(out.triangles[i].tag, i);
                }
                t = tagToTriangle.value(tok[0], -1);
                if (t < 0) continue;
            }
            CellCoupling cc;
            cc.tri    = t;
            cc.nodeId = tok[1];
            if (tok.size() >= 3) {
                bool okc = false;
                const double cd = tok[2].toDouble(&okc);
                if (okc) cc.cd = cd;
            }
            if (tok.size() >= 4) {
                bool oka = false;
                const double a = tok[3].toDouble(&oka);
                if (oka) cc.area = a;
            }
            out.cellCouplings.append(cc);
        }
        return {};
    }

    // GG0a — [2D_INFILTRATION_OPTIONS] — PARAMETER VALUE. Only INFIL_STEP is
    // defined; anything else is ignored rather than rejected, so a file
    // written by a newer engine still loads.
    if (sectionName.compare(QLatin1String(kSecInfilOptions),
                            Qt::CaseInsensitive) == 0)
    {
        for (const QString &raw : bodyLines)
        {
            const QStringList tok = tokenize(raw);
            if (tok.size() < 2) continue;
            if (tok[0].compare(QLatin1String("INFIL_STEP"),
                               Qt::CaseInsensitive) != 0)
                continue;
            const double s = parseInfilStepSeconds(tok[1]);
            if (s < 0.0)
                return QStringLiteral("[2D_INFILTRATION_OPTIONS] invalid "
                                      "INFIL_STEP (expected hh:mm:ss >= 0): %1")
                    .arg(tok[1]);
            out.infilOptions.infilStep = s;
        }
        return {};
    }

    // GG0a — [2D_INFILTRATION_DEFAULTS] — TAG METHOD [P1..P5] [DEST].
    // `*` is the mesh-wide fallback and may appear anywhere in the section;
    // rows are kept in file order so a save re-emits the same shape.
    if (sectionName.compare(QLatin1String(kSecInfilDefaults),
                            Qt::CaseInsensitive) == 0)
    {
        for (const QString &raw : bodyLines)
        {
            const QStringList tok = tokenize(raw);
            if (tok.isEmpty()) continue;
            if (tok.size() < 2)
                return QStringLiteral("[2D_INFILTRATION_DEFAULTS] needs TAG "
                                      "METHOD [P1..P5] [DEST] (got: %1)")
                    .arg(raw.trimmed());
            InfilDefaultRow d;
            d.tag = tok[0];
            const QString err =
                parseInfilRowTail(tok, 1, kSecInfilDefaults, d.row);
            if (!err.isEmpty()) return err;
            out.infilDefaults.append(d);
        }
        return {};
    }

    // GG0a — [2D_INFILTRATION] — CELL METHOD [P1..P5] [DEST]. CELL is
    // 1-BASED in the file and 0-based in MeshResult::infilOverrides. The
    // upper bound is not checked here: the section may precede
    // [2D_TRIANGLES] in the file (and, for an external mesh, live in a
    // different file altogether), so out.triangles is not populated yet.
    if (sectionName.compare(QLatin1String(kSecInfil), Qt::CaseInsensitive) == 0)
    {
        for (const QString &raw : bodyLines)
        {
            const QStringList tok = tokenize(raw);
            if (tok.isEmpty()) continue;
            if (tok.size() < 2)
                return QStringLiteral("[2D_INFILTRATION] needs CELL METHOD "
                                      "[P1..P5] [DEST] (got: %1)")
                    .arg(raw.trimmed());
            bool okc = false;
            const int cell = tok[0].toInt(&okc);
            if (!okc || cell < 1)
                return QStringLiteral("[2D_INFILTRATION] invalid CELL index "
                                      "(1-based): %1").arg(tok[0]);
            InfilRow row;
            const QString err = parseInfilRowTail(tok, 1, kSecInfil, row);
            if (!err.isEmpty()) return err;
            out.infilOverrides.insert(cell - 1, row);
        }
        return {};
    }

    // Other sections (options) carry no rendering data.
    return {};
}

/*! Parse one `[2D_BOUNDARY_CONDITIONS]` body line into a BCRow. Returns
 *  empty string on success, an error message on malformed input. */
QString parseBCLine(const QString &raw, BCRow &row)
{
    const QStringList tok = tokenize(raw);
    if (tok.isEmpty()) return {};  // comment / blank — skip silently
    if (tok.size() < 3)
        return QStringLiteral("[2D_BOUNDARY_CONDITIONS] needs TRI EDGE TYPE [PARAM_1 [PARAM_2 [GROUP]]] (got: %1)")
            .arg(raw.trimmed());

    bool okt = false, oke = false;
    const int tri = tok[0].toInt(&okt);
    const int e   = tok[1].toInt(&oke);
    if (!okt || !oke || tri < 0 || e < 0 || e > 2)
        return QStringLiteral("[2D_BOUNDARY_CONDITIONS] TRI/EDGE invalid (got: %1)")
            .arg(raw.trimmed());

    bool typeOk = false;
    const auto type = MeshBCTypes::fromInpToken(tok[2], &typeOk);
    if (!typeOk)
        return QStringLiteral("[2D_BOUNDARY_CONDITIONS] unknown TYPE '%1'").arg(tok[2]);

    row.flat = tri * 3 + e;
    row.bc.type = type;

    auto paramOrStar = [&](int idx) -> QString {
        if (tok.size() <= idx) return QString();
        const QString s = tok[idx];
        return (s == QStringLiteral("*")) ? QString() : s;
    };
    const QString p1 = paramOrStar(3);
    // tok[4] is PARAM_2 reserved — currently always "*".
    const QString grp = paramOrStar(5);
    row.bc.group = grp;

    switch (type) {
    case MeshBCTypes::Type::Wall:
        break;
    case MeshBCTypes::Type::NormalFlow:
        row.bc.slope = p1.toDouble();
        break;
    case MeshBCTypes::Type::SpecifiedStageConst:
        row.bc.head = p1.toDouble();
        break;
    case MeshBCTypes::Type::SpecifiedStageTS:
    case MeshBCTypes::Type::SpecifiedFlowTS:
        row.bc.tseries = p1;
        break;
    case MeshBCTypes::Type::SpecifiedFlowConst:
        row.bc.flow = p1.toDouble();
        break;
    case MeshBCTypes::Type::RatingCurve:
        row.bc.curve = p1;
        break;
    }
    return {};
}

/*! Engine §11A — parse one `[2D_EDGE_CONVEYANCE]` line. Format:
 *      FROM_VERTEX  TO_VERTEX  CONVEYANCE
 *  CONVEYANCE must be in [0, 1] (strict, matches engine validation). */
QString parseConveyanceLine(const QString &raw, ConveyanceRow &row)
{
    const QStringList tok = tokenize(raw);
    if (tok.isEmpty()) return {};
    if (tok.size() < 3)
        return QStringLiteral("[2D_EDGE_CONVEYANCE] needs FROM_VERTEX TO_VERTEX CONVEYANCE (got: %1)")
            .arg(raw.trimmed());

    bool okA = false, okB = false, okC = false;
    const int va = tok[0].toInt(&okA);
    const int vb = tok[1].toInt(&okB);
    const double cv = tok[2].toDouble(&okC);
    if (!okA || !okB || va < 0 || vb < 0 || va == vb)
        return QStringLiteral("[2D_EDGE_CONVEYANCE] invalid FROM/TO vertex (got: %1)")
            .arg(raw.trimmed());
    if (!okC || !(cv >= 0.0 && cv <= 1.0))
        return QStringLiteral("[2D_EDGE_CONVEYANCE] CONVEYANCE must be a number in [0, 1] (got: %1)")
            .arg(raw.trimmed());

    row.va   = va;
    row.vb   = vb;
    row.conv = cv;
    return {};
}

/*! Scan \p text for `;; UNITS: <value>` and `;; SOURCE_CRS: <value>` header
 *  comments anywhere in the file. Returns the unit value (empty when the
 *  header is absent) and fills \p crsOut with the source-CRS tag if present.
 *
 *  The scan walks the entire text rather than stopping at the first section
 *  header because the writer appends these comments BELOW the existing
 *  sections in inline (.inp) mode.  The keys (`UNITS:` and `SOURCE_CRS:`)
 *  are specific enough that incidental hits in other `;;` comments are
 *  unlikely; if both inline and external blocks define the header the last
 *  occurrence wins (matches the order in which the writer emits them). */
QString scanUnitsHeader(const QString &text, QString *crsOut)
{
    QString units;
    if (crsOut) crsOut->clear();
    const QStringList lines = text.split(QChar('\n'), Qt::KeepEmptyParts);
    for (const QString &raw : lines)
    {
        const QString t = raw.trimmed();
        if (!t.startsWith(QStringLiteral(";;"))) continue;
        // Drop the ";;" prefix and any leading whitespace.
        QString rest = t.mid(2).trimmed();
        if (rest.startsWith(QStringLiteral("UNITS:"), Qt::CaseInsensitive)) {
            units = rest.mid(QStringLiteral("UNITS:").size()).trimmed();
        } else if (crsOut && rest.startsWith(QStringLiteral("SOURCE_CRS:"), Qt::CaseInsensitive)) {
            *crsOut = rest.mid(QStringLiteral("SOURCE_CRS:").size()).trimmed();
        }
    }
    return units;
}

/*! Walk \p text section-by-section, accumulating vertex / triangle data into
 *  \p out. Records whether either section was actually seen so the caller
 *  can distinguish "no mesh in this file" from "mesh present but empty". */
QString parseSectionsFromText(const QString &text,
                              MeshResult &out,
                              bool &sawVertices,
                              bool &sawTriangles,
                              QString *meshFileRefOut,
                              BCRowList *bcRowsOut = nullptr,
                              ConveyanceRowList *conveyOut = nullptr)
{
    sawVertices = sawTriangles = false;
    if (meshFileRefOut) meshFileRefOut->clear();

    const QStringList lines = text.split(QChar('\n'), Qt::KeepEmptyParts);
    QString currentSection;
    QStringList body;

    auto flush = [&]() -> QString {
        if (currentSection.isEmpty()) {
            body.clear();
            return {};
        }
        if (currentSection.compare(QLatin1String(kSecVertices),  Qt::CaseInsensitive) == 0) sawVertices = true;
        if (currentSection.compare(QLatin1String(kSecTriangles), Qt::CaseInsensitive) == 0) sawTriangles = true;
        if (currentSection.compare(QLatin1String(kSecMeshFile),  Qt::CaseInsensitive) == 0)
        {
            // First "FILE <path>" token wins (mirrors the engine).
            if (meshFileRefOut)
            {
                for (const QString &raw : body) {
                    const QStringList tok = tokenize(raw);
                    if (tok.size() >= 2 &&
                        tok.first().compare(QLatin1String("FILE"), Qt::CaseInsensitive) == 0) {
                        *meshFileRefOut = tok.at(1);
                        break;
                    }
                }
            }
            body.clear();
            return {};
        }
        if (currentSection.compare(QLatin1String(kSecBC), Qt::CaseInsensitive) == 0)
        {
            // §V.VD.1 — accumulate BC rows. Resolution to the sized
            // edgeBCs vector happens after the mesh is known.
            if (bcRowsOut) {
                for (const QString &raw : body) {
                    BCRow row;
                    const QString err = parseBCLine(raw, row);
                    if (!err.isEmpty()) {
                        body.clear();
                        return err;
                    }
                    if (row.flat >= 0) bcRowsOut->append(row);
                }
            }
            body.clear();
            return {};
        }
        if (currentSection.compare(QLatin1String(kSecConveyance), Qt::CaseInsensitive) == 0)
        {
            // Engine §11A — accumulate conveyance rows keyed by vertex pair.
            // Mesh-side resolution (vertex pair → flat slot(s)) happens after
            // triangulation is known, so we just collect rows here.
            if (conveyOut) {
                for (const QString &raw : body) {
                    ConveyanceRow row;
                    const QString err = parseConveyanceLine(raw, row);
                    if (!err.isEmpty()) {
                        body.clear();
                        return err;
                    }
                    if (row.va >= 0) conveyOut->append(row);
                }
            }
            body.clear();
            return {};
        }
        const QString err = parseSection(currentSection, body, out);
        body.clear();
        return err;
    };

    for (const QString &raw : lines)
    {
        const QString trimmed = raw.trimmed();
        if (trimmed.startsWith(QChar('[')) && trimmed.endsWith(QChar(']')))
        {
            const QString err = flush();
            if (!err.isEmpty()) return err;
            currentSection = trimmed;
            continue;
        }
        if (!currentSection.isEmpty())
            body.append(raw);
    }
    return flush();
}

/*! Engine §11A — apply each `[2D_EDGE_CONVEYANCE]` row to every (tri, e)
 *  slot whose endpoints match the row's vertex pair. Interior edges have
 *  two such slots (one per neighbouring triangle); both receive the same
 *  value, matching the engine's symmetry invariant. Rows whose vertex pair
 *  doesn't match any edge are silently dropped (the writer canonicalises
 *  on the lower vertex pair, but the engine accepts either order). */
void applyConveyanceRows(const MeshResult &mesh,
                         const ConveyanceRowList &rows,
                         QVector<MeshEdgeBC> &edgeBCs)
{
    if (rows.isEmpty()) return;
    const int nslots = edgeBCs.size();
    // Build vertex-pair → list-of-flat-slots once. Key is the sorted pair.
    QHash<QPair<int,int>, QVector<int>> pairToSlots;
    pairToSlots.reserve(mesh.triangles.size() * 3);
    for (int t = 0; t < mesh.triangles.size(); ++t) {
        const auto &tri = mesh.triangles[t];
        const int va[3] = {tri.v1, tri.v2, tri.v0};
        const int vb[3] = {tri.v2, tri.v0, tri.v1};
        for (int e = 0; e < 3; ++e) {
            const QPair<int,int> key = (va[e] < vb[e]) ? qMakePair(va[e], vb[e])
                                                       : qMakePair(vb[e], va[e]);
            pairToSlots[key].append(t * 3 + e);
        }
    }
    for (const auto &r : rows) {
        const QPair<int,int> key = (r.va < r.vb) ? qMakePair(r.va, r.vb)
                                                 : qMakePair(r.vb, r.va);
        const auto it = pairToSlots.constFind(key);
        if (it == pairToSlots.constEnd()) continue;  // no such edge — silently skip
        for (int flat : *it) {
            if (flat >= 0 && flat < nslots)
                edgeBCs[flat].conveyance = r.conv;
        }
    }
}

} // namespace

bool unitsHeaderIsSI(const QString &unitsHeader)
{
    // Keyword set copied from the engine's prescan2DUnitsHeader
    // (openswmm.engine/src/engine/2d/input/SectionHandlers2D.cpp). Keep the
    // two in step: a divergence silently changes whether the GUI thinks the
    // engine rescaled the mesh. Issue #155.
    static const QStringList kMetric = {
        QStringLiteral("SI (m)"), QStringLiteral("m"),
        QStringLiteral("metre"),  QStringLiteral("metres"),
        QStringLiteral("meter"),  QStringLiteral("meters"),
    };
    const QString v = unitsHeader.trimmed();
    for (const QString &k : kMetric)
        if (v.compare(k, Qt::CaseInsensitive) == 0) return true;
    return false;
}

InpMeshReadResult InpMeshReader::read(const QString &inpPath)
{
    InpMeshReadResult result;

    QString inpText;
    if (!readTextFile(inpPath, &inpText, &result.errorMsg))
        return result;

    // Capture the inline `;; UNITS:` and `;; SOURCE_CRS:` headers (if any)
    // up front; the external-file path may overwrite them when a .2dm is
    // resolved further down. No rejection here — the engine decides what
    // to do with the value.
    {
        QString tag;
        const QString units = scanUnitsHeader(inpText, &tag);
        if (!units.isEmpty()) result.unitsHeader  = units;
        if (!tag.isEmpty())   result.sourceCrsTag = tag;
    }

    // First pass on the .inp: capture any [2D_MESH_FILE] reference and parse
    // inline mesh sections opportunistically. The engine prefers the external
    // file when both are present, so an external reference clears any inline
    // data we accumulated.
    MeshResult inlineMesh;
    bool sawInlineVerts = false;
    bool sawInlineTris  = false;
    QString meshFileRef;
    BCRowList inlineBCs;  // §V.VD.1 — collected from any [2D_BOUNDARY_CONDITIONS]
    ConveyanceRowList inlineConv;  // Engine §11A — [2D_EDGE_CONVEYANCE]
    const QString inpErr =
        parseSectionsFromText(inpText, inlineMesh, sawInlineVerts, sawInlineTris,
                              &meshFileRef, &inlineBCs, &inlineConv);
    if (!inpErr.isEmpty()) {
        result.errorMsg = inpErr;
        return result;
    }

    // Resolve [2D_MESH_FILE] if it was found. Relative paths are interpreted
    // against the .inp directory — same convention as the writer.
    if (!meshFileRef.isEmpty())
    {
        QString absMeshPath = meshFileRef;
        QFileInfo fi(absMeshPath);
        if (fi.isRelative())
            absMeshPath = QFileInfo(inpPath).absoluteDir().absoluteFilePath(meshFileRef);

        QString meshText;
        if (!readTextFile(absMeshPath, &meshText, &result.errorMsg))
        {
            // [2D_MESH_FILE] referenced but the file is missing — report it
            // so the user sees something is wrong rather than silently
            // dropping the mesh.
            result.errorMsg = QStringLiteral(
                "[2D_MESH_FILE] points at %1 but the file could not be read: %2")
                                  .arg(absMeshPath, result.errorMsg);
            return result;
        }

        // External file's own ;; UNITS: / ;; SOURCE_CRS: headers take
        // precedence over the inline ones for the resolved mesh.
        {
            QString tag;
            const QString units = scanUnitsHeader(meshText, &tag);
            if (!units.isEmpty()) result.unitsHeader  = units;
            if (!tag.isEmpty())   result.sourceCrsTag = tag;
        }

        MeshResult extMesh;
        bool sawExtVerts = false;
        bool sawExtTris  = false;
        // §V.VD.1 — the external .2dm may carry its own [2D_BOUNDARY_CONDITIONS].
        // The .inp's BC rows still apply as overrides per the precedence
        // documented at the top of this file (external wins on geometry,
        // .inp wins on BC overrides since the user edits them there).
        BCRowList extBCs;
        ConveyanceRowList extConv;  // Engine §11A — [2D_EDGE_CONVEYANCE]
        const QString extErr =
            parseSectionsFromText(meshText, extMesh, sawExtVerts, sawExtTris,
                                  nullptr, &extBCs, &extConv);
        if (!extErr.isEmpty()) {
            result.errorMsg = QStringLiteral("%1 (in %2)").arg(extErr, absMeshPath);
            return result;
        }

        if (sawExtVerts && sawExtTris &&
            !extMesh.vertices.isEmpty() && !extMesh.triangles.isEmpty())
        {
            extMesh.ok = true;
            result.mesh = std::move(extMesh);
            result.sourcePath = absMeshPath;
            result.isExternal = true;
            result.hasMesh    = true;

            // GG0a — [2D_INFILTRATION*] follow the mesh, so the sidecar is
            // authoritative: a section the .2dm carries REPLACES the .inp's
            // (which is what the engine's load2DMeshExternalFile does, so a
            // stale inline copy can never be counted twice), and a section
            // the .2dm omits keeps whatever the .inp supplied.
            if (result.mesh.infilDefaults.isEmpty())
                result.mesh.infilDefaults = inlineMesh.infilDefaults;
            if (result.mesh.infilOverrides.isEmpty())
                result.mesh.infilOverrides = inlineMesh.infilOverrides;
            if (!(result.mesh.infilOptions.infilStep > 0.0))
                result.mesh.infilOptions = inlineMesh.infilOptions;

            // Resolve BCs: start Wall-defaults of size n_triangles * 3,
            // then overlay external rows first, then .inp rows (so .inp
            // overrides on conflict).
            // NB: `slots` is a Qt keyword macro — use `nslots`.
            const int nslots = result.mesh.triangles.size() * 3;
            result.edgeBCs.resize(nslots);
            std::fill(result.edgeBCs.begin(), result.edgeBCs.end(), MeshEdgeBC{});
            for (const auto &r : extBCs)
                if (r.flat >= 0 && r.flat < nslots) result.edgeBCs[r.flat] = r.bc;
            for (const auto &r : inlineBCs)
                if (r.flat >= 0 && r.flat < nslots) result.edgeBCs[r.flat] = r.bc;
            // Engine §11A — overlay external conveyance first, then .inp
            // (.inp wins on conflict; matches the BC precedence above).
            applyConveyanceRows(result.mesh, extConv,    result.edgeBCs);
            applyConveyanceRows(result.mesh, inlineConv, result.edgeBCs);
        }
        return result;
    }

    if (sawInlineVerts && sawInlineTris &&
        !inlineMesh.vertices.isEmpty() && !inlineMesh.triangles.isEmpty())
    {
        inlineMesh.ok = true;
        result.mesh = std::move(inlineMesh);
        result.sourcePath = inpPath;
        result.isExternal = false;
        result.hasMesh    = true;

        const int nslots = result.mesh.triangles.size() * 3;
        result.edgeBCs.resize(nslots);
        std::fill(result.edgeBCs.begin(), result.edgeBCs.end(), MeshEdgeBC{});
        for (const auto &r : inlineBCs)
            if (r.flat >= 0 && r.flat < nslots) result.edgeBCs[r.flat] = r.bc;
        applyConveyanceRows(result.mesh, inlineConv, result.edgeBCs);
    }

    // Legacy file note — pre-header files load fine (engine still applies
    // its FLOW_UNITS-based conversion), but surface the absence so the
    // user can regenerate to get a self-describing file.
    if (result.hasMesh && result.unitsHeader.isEmpty()) {
        result.warning = QStringLiteral(
            "Mesh file %1 has no ';; UNITS:' header — engine will infer "
            "units from SWMM FLOW_UNITS. Regenerate the mesh to embed the "
            "unit descriptor.").arg(result.sourcePath);
    }
    return result;
}

} // namespace mesh
