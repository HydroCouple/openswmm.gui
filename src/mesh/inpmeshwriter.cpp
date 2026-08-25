/*!
 * \file   inpmeshwriter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AU — InpMeshWriter implementation. Section formats per
 * openswmm.engine/docs/2dModelStrategy.md §1.4–1.7.
 */
#include "mesh/inpmeshwriter.h"
#include "mesh/meshbctype.h"
#include "mesh/meshinfil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QPair>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <QtCore/QChar>

#include <algorithm>
#include <cmath>

namespace mesh {

// Out-of-line default ctor (declared in the header) — see UnitInfo's comment.
InpMeshWriter::UnitInfo::UnitInfo() = default;

namespace {

constexpr const char *kSecVertices       = "[2D_VERTICES]";
constexpr const char *kSecTriangles      = "[2D_TRIANGLES]";
constexpr const char *kSecVertexNodeMap  = "[2D_VERTEX_NODE_MAP]";
constexpr const char *kSecTriangleNodeMap= "[2D_TRIANGLE_NODE_MAP]";
constexpr const char *kSecMeshFile       = "[2D_MESH_FILE]";
// GG0a — per-cell infiltration. These are per-cell mesh attributes, so they
// follow the mesh: into the external .2dm when one is in use, inline
// otherwise (engine SectionHandlers2D.cpp §5.5.5 parses them from both).
constexpr const char *kSecInfilOptions   = "[2D_INFILTRATION_OPTIONS]";
constexpr const char *kSecInfilDefaults  = "[2D_INFILTRATION_DEFAULTS]";
constexpr const char *kSecInfil          = "[2D_INFILTRATION]";

QString formatVertices(const MeshResult &mesh)
{
    QString out;
    QTextStream s(&out);
    s.setRealNumberNotation(QTextStream::FixedNotation);
    s.setRealNumberPrecision(6);

    s << kSecVertices << "\n";
    s << ";; X            Y            Z            TAG\n";
    // No XY conversion: the engine multiplies by 0.3048 in
    // SurfaceRouter2D::initialize when SWMM FLOW_UNITS is US, so the file
    // is expected to carry project-CRS values. The unit is described in
    // the ;; UNITS: header so a future SI-aware engine can branch on it.
    for (const MeshVertex &v : mesh.vertices)
    {
        s << v.xy.x() << "  " << v.xy.y() << "  " << v.z;
        if (!v.tag.isEmpty())
            s << "  " << v.tag;
        s << "\n";
    }
    s << "\n";
    return out;
}

QString formatTriangles(const MeshResult &mesh,
                        const CouplingMap &coupling,
                        double defaultMannings)
{
    QString out;
    QTextStream s(&out);
    s.setRealNumberNotation(QTextStream::FixedNotation);
    s.setRealNumberPrecision(4);

    s << kSecTriangles << "\n";
    // INIT_DEPTH (m, engine default 0 = dry) sits between MANNINGS_N and
    // TAG. Emit the column for every row whenever any triangle carries a
    // depth or a tag, so TAG's position stays unambiguous on re-read.
    bool anyDepth = false, anyTag = false;
    for (const MeshTriangle &t : mesh.triangles) {
        if (!std::isnan(t.initDepth) && t.initDepth != 0.0) anyDepth = true;
        if (!t.tag.isEmpty()) anyTag = true;
    }
    const bool writeDepthCol = anyDepth || anyTag;
    if (writeDepthCol)
        s << ";; V1   V2   V3   MANNINGS_N   INIT_DEPTH   TAG\n";
    else
        s << ";; V1   V2   V3   MANNINGS_N   TAG\n";
    for (int i = 0; i < mesh.triangles.size(); ++i)
    {
        const MeshTriangle &t = mesh.triangles[i];
        const double n = coupling.triangleMannings.value(i, defaultMannings);
        s << t.v0 << "  " << t.v1 << "  " << t.v2 << "  " << n;
        if (writeDepthCol)
            s << "  " << (std::isnan(t.initDepth) ? 0.0 : t.initDepth);
        if (!t.tag.isEmpty())
            s << "  " << t.tag;
        s << "\n";
    }
    s << "\n";
    return out;
}

QString formatVertexNodeMap(const MeshResult &mesh,
                            const CouplingMap &coupling)
{
    if (coupling.vertexToNode.isEmpty()) return {};

    QString out;
    QTextStream s(&out);
    s << kSecVertexNodeMap << "\n";
    s << ";; VERTEX_INDEX_OR_TAG    SWMM_NODE_NAME    CD    AREA\n";
    // Walk in vertex-index order so output is deterministic + matches
    // the [2D_VERTICES] order. Prefer the vertex's TAG over the index
    // when present — the engine accepts both, but tag form is more
    // robust to edits that renumber vertices.
    for (auto it = coupling.vertexToNode.cbegin();
         it != coupling.vertexToNode.cend(); ++it)
    {
        const int   vIdx = it.key();
        const QString  &node = it.value();
        if (vIdx < 0 || vIdx >= mesh.vertices.size()) continue;
        const MeshVertex &mv = mesh.vertices[vIdx];
        if (!mv.tag.isEmpty())
            s << mv.tag;
        else
            s << vIdx;
        s << "        " << node
          << "  " << mv.couplingCd << "  " << mv.couplingArea << "\n";
    }
    s << "\n";
    return out;
}

QString formatTriangleNodeMap(const MeshResult &mesh,
                              const CouplingMap &coupling)
{
    if (coupling.triangleToNode.isEmpty() && mesh.cellCouplings.isEmpty())
        return {};

    QString out;
    QTextStream s(&out);
    s << kSecTriangleNodeMap << "\n";
    s << ";; TRIANGLE_INDEX_OR_TAG  SWMM_NODE_NAME    CD    AREA\n";
    for (auto it = coupling.triangleToNode.cbegin();
         it != coupling.triangleToNode.cend(); ++it)
    {
        const int   tIdx = it.key();
        const QString  &node = it.value();
        if (tIdx < 0 || tIdx >= mesh.triangles.size()) continue;
        const QString &tag = mesh.triangles[tIdx].tag;
        if (!tag.isEmpty())
            s << tag;
        else
            s << tIdx;
        s << "      " << node << "\n";
    }
    // Node→cell coupling rows (Plan Part C) — repeated-row form: several
    // nodes may couple to one triangle, one row each, CD + AREA explicit.
    // Written by INDEX (not tag): coupling triangles usually carry a
    // subcatchment tag that is not unique per triangle.
    for (const CellCoupling &cc : mesh.cellCouplings)
    {
        if (cc.tri < 0 || cc.tri >= mesh.triangles.size()) continue;
        if (cc.nodeId.isEmpty()) continue;
        s << cc.tri << "      " << cc.nodeId
          << "  " << cc.cd << "  " << cc.area << "\n";
    }
    s << "\n";
    return out;
}

// ---------------------------------------------------------------------------
// GG0a — [2D_INFILTRATION_OPTIONS] / _DEFAULTS / [2D_INFILTRATION]
//
// Grammar mirrors the engine's InpWriter.cpp emit2DInfilSections() /
// SectionHandlers2D.cpp parseInfil2DRowTail() byte-for-byte:
//
//     [2D_INFILTRATION_DEFAULTS]
//     ;;TAG   METHOD   P1 P2 P3 P4 P5   DEST
//     *       NONE
//     LAWN    HORTON   3  0.5  4.14  7  0   LOST
//     [2D_INFILTRATION]
//     ;;CELL  METHOD   P1 P2 P3 P4 P5   DEST
//     1043    CURVE_NUMBER  85  -  0.5  LOST
//
// - CELL is 1-BASED in the file, 0-based in MeshResult::infilOverrides.
// - `-` means "unset"; the parser leaves the slot at its default.
// - Trailing columns the method does not use are trimmed to
//   mesh::infilParamCount(); the one interior unused slot (Curve Number's
//   middle column) is emitted as `-` so the columns after it keep position.
// - DEST is recognised as the last token when it is neither numeric nor `-`.
// - A NONE row carries neither parameters nor a destination.
//
// NOT appended to [2D_TRIANGLES]: its columns are positional
// (V1 V2 V3 MANNINGS_N [INIT_DEPTH] [TAG]) and appending would break every
// existing mesh.
// ---------------------------------------------------------------------------

/*! `METHOD [P1..Pn] [DEST]` — the row tail shared by both sections. */
QString formatInfilRowTail(const InfilRow &row)
{
    QString out = infilMethodToken(row.method);
    if (row.isNone()) return out;   // no parameters, no destination
    const int n = infilParamCount(row.method);
    for (int k = 0; k < n && k < kInfilMaxParams; ++k) {
        out += QChar(' ');
        if (!infilUsesParam(row.method, k) || std::isnan(row.p[k]))
            out += QStringLiteral("-");
        else
            out += QString::number(row.p[k], 'g', 12);
    }
    out += QChar(' ');
    out += infilDestToken(row.dest);
    return out;
}

/*! `[2D_INFILTRATION_OPTIONS]`. Omitted entirely when the cadence is at the
 *  engine default (<= 0 = "use the project WET_STEP"). */
QString formatInfilOptions(const MeshResult &mesh)
{
    const double s = mesh.infilOptions.infilStep;
    if (!(s > 0.0)) return {};

    // Same clock grammar the engine's fmt_step() emits and its
    // parse_time_seconds() accepts: h:mm:ss for a whole number of seconds,
    // plain seconds otherwise.
    QString value;
    const long r = std::lround(s);
    if (std::fabs(s - double(r)) < 0.001) {
        value = QStringLiteral("%1:%2:%3")
                    .arg(r / 3600)
                    .arg((r / 60) % 60, 2, 10, QChar('0'))
                    .arg(r % 60,        2, 10, QChar('0'));
    } else {
        value = QString::number(s, 'g', 12);
    }

    QString out;
    out += QChar('\n');
    out += QLatin1String(kSecInfilOptions);
    out += QChar('\n');
    out += QStringLiteral(";;Parameter             Value\n");
    out += QStringLiteral("INFIL_STEP             %1\n").arg(value);
    return out;
}

/*! `[2D_INFILTRATION_DEFAULTS]` — the tag rows, `*` = mesh-wide fallback.
 *  Emitted in authoring order so a hand-edited file keeps its shape. */
QString formatInfilDefaults(const MeshResult &mesh)
{
    if (mesh.infilDefaults.isEmpty()) return {};

    QString out;
    out += QChar('\n');
    out += QLatin1String(kSecInfilDefaults);
    out += QChar('\n');
    out += QStringLiteral(";;TAG           METHOD               "
                          "P1           P2           P3           "
                          "P4           P5           DEST\n");
    for (const InfilDefaultRow &d : mesh.infilDefaults) {
        if (d.tag.isEmpty()) continue;   // a tagless default has no meaning
        out += QStringLiteral("%1 %2\n")
                   .arg(d.tag, -15).arg(formatInfilRowTail(d.row));
    }
    return out;
}

/*! `[2D_INFILTRATION]` — the sparse per-cell overrides. QHash iteration order
 *  is unspecified, so rows are emitted in ascending triangle order; without
 *  that a save with no edits still produces a different file every time. */
QString formatInfilOverrides(const MeshResult &mesh)
{
    if (mesh.infilOverrides.isEmpty()) return {};

    QList<int> tris = mesh.infilOverrides.keys();
    std::sort(tris.begin(), tris.end());

    QString out;
    out += QChar('\n');
    out += QLatin1String(kSecInfil);
    out += QChar('\n');
    out += QStringLiteral(";;CELL          METHOD               "
                          "P1           P2           P3           "
                          "P4           P5           DEST\n");
    for (int t : tris) {
        if (t < 0 || t >= mesh.triangles.size()) continue;   // stale index
        // CELL is 1-BASED in the file.
        out += QStringLiteral("%1 %2\n")
                   .arg(t + 1, -15)
                   .arg(formatInfilRowTail(mesh.infilOverrides.value(t)));
    }
    return out;
}

/*! All three infiltration sections, in the engine's emission order. Empty
 *  when the mesh carries no infiltration data at all. */
QString formatInfilSections(const MeshResult &mesh)
{
    return formatInfilOptions(mesh) + formatInfilDefaults(mesh)
         + formatInfilOverrides(mesh);
}

/*! Strip every section named in \p ours from \p originalText. */
QString stripSections(const QString &originalText, const QStringList &ours)
{
    const QStringList lines = originalText.split(QChar('\n'), Qt::KeepEmptyParts);
    QString out;
    out.reserve(originalText.size());
    bool inStripped = false;
    for (const QString &raw : lines)
    {
        const QString trimmed = raw.trimmed();
        // SWMM section header detection: "[NAME]" with optional trailing
        // whitespace. Comments and data lines never start with '['.
        if (trimmed.startsWith(QChar('[')) && trimmed.endsWith(QChar(']')))
        {
            inStripped = ours.contains(trimmed, Qt::CaseInsensitive);
            if (inStripped) continue;
        }
        if (inStripped) continue;
        out.append(raw);
        out.append(QChar('\n'));
    }
    // split + join with KeepEmptyParts adds one trailing newline; trim it
    // so we don't accumulate them across repeated rewrites.
    while (out.endsWith(QStringLiteral("\n\n")))
        out.chop(1);
    return out;
}

/*! Strip every `[2D_*]` mesh-data section we own from \p originalText.
 *  When \p alsoMeshFileRef is true the `[2D_MESH_FILE]` block is also
 *  stripped — used for the inline-mode write where we want to remove a
 *  stale external reference. The external-mode write keeps `[2D_MESH_FILE]`
 *  alive (and updates its FILE token if needed). `[2D_OPTIONS]` is always
 *  preserved (user-edited in the simulation-options dialog). */
QString stripExistingMeshSections(const QString &originalText,
                                  bool alsoMeshFileRef)
{
    QStringList ours = {
        QStringLiteral("[2D_VERTICES]"),
        QStringLiteral("[2D_TRIANGLES]"),
        QStringLiteral("[2D_VERTEX_NODE_MAP]"),
        QStringLiteral("[2D_TRIANGLE_NODE_MAP]"),
        QStringLiteral("[2D_BOUNDARY_CONDITIONS]"),    // §V.VD.1
        QStringLiteral("[2D_EDGE_CONVEYANCE]"),        // Engine §11A
        // GG0a — per-cell infiltration follows the mesh, so the .inp's copy
        // must go whenever the mesh moves out to a .2dm. Leaving it behind
        // would not lose data (the engine lets a sidecar section replace the
        // inline one) but it would strand a stale copy that resurrects the
        // moment the user deletes every override.
        QStringLiteral("[2D_INFILTRATION_OPTIONS]"),
        QStringLiteral("[2D_INFILTRATION_DEFAULTS]"),
        QStringLiteral("[2D_INFILTRATION]"),
    };
    if (alsoMeshFileRef)
        ours.append(QStringLiteral("[2D_MESH_FILE]"));
    return stripSections(originalText, ours);
}

/*! Collect the data rows (non-empty, non-comment, inline comments stripped)
 *  of section \p secName from \p text, in file order. */
QStringList sectionDataRows(const QString &text, const char *secName)
{
    QStringList rows;
    bool inSec = false;
    const QStringList lines = text.split(QChar('\n'));
    for (const QString &raw : lines)
    {
        QString trimmed = raw.trimmed();
        if (trimmed.startsWith(QChar('[')) && trimmed.endsWith(QChar(']')))
        {
            inSec = (trimmed.compare(QLatin1String(secName),
                                     Qt::CaseInsensitive) == 0);
            continue;
        }
        if (!inSec) continue;
        const int semi = trimmed.indexOf(QChar(';'));
        if (semi >= 0) trimmed = trimmed.left(semi).trimmed();
        if (trimmed.isEmpty()) continue;
        rows.append(trimmed);
    }
    return rows;
}

/*! `[2D_TRIANGLES]` rebuild for patchAttributeSections: connectivity and TAG
 *  come from \p mesh; MANNINGS_N (and INIT_DEPTH) come from the mesh triangle
 *  when set, else from the same row of \p origRows so values authored at
 *  generation time (or by hand) survive the rewrite (both stay NaN until the
 *  user edits them). Columns are positional (`V1 V2 V3 MANNINGS_N
 *  [INIT_DEPTH] [TAG]`; a numeric 5th token means INIT_DEPTH), so a row that
 *  must carry a depth or a tag needs a MANNINGS_N token to hold column 4 —
 *  when neither the mesh nor the original row has one, \p defaultMannings is
 *  materialized rather than dropping the later columns. */
QString formatTrianglesPreserving(const MeshResult &mesh,
                                  const QStringList &origRows,
                                  double defaultMannings)
{
    QString out;
    QTextStream s(&out);
    s.setRealNumberNotation(QTextStream::FixedNotation);
    s.setRealNumberPrecision(4);

    // Original-row INIT_DEPTH (numeric 5th token) so hand-authored depths
    // survive the rewrite even when MeshTriangle::initDepth is unset.
    auto origDepthTok = [&origRows](int i) -> QString {
        if (i >= origRows.size()) return {};
        const QStringList tok =
            origRows[i].simplified().split(QChar(' '), Qt::SkipEmptyParts);
        if (tok.size() < 5) return {};
        bool okd = false;
        tok[4].toDouble(&okd);
        return okd ? tok[4] : QString();
    };

    bool anyDepth = false, anyTag = false;
    for (int i = 0; i < mesh.triangles.size(); ++i) {
        const MeshTriangle &t = mesh.triangles[i];
        if ((std::isfinite(t.initDepth) && t.initDepth != 0.0) ||
            !origDepthTok(i).isEmpty())
            anyDepth = true;
        if (!t.tag.isEmpty()) anyTag = true;
    }
    const bool writeDepthCol = anyDepth || anyTag;

    s << kSecTriangles << "\n";
    if (writeDepthCol)
        s << ";; V1   V2   V3   MANNINGS_N   INIT_DEPTH   TAG\n";
    else
        s << ";; V1   V2   V3   MANNINGS_N   TAG\n";
    for (int i = 0; i < mesh.triangles.size(); ++i)
    {
        const MeshTriangle &t = mesh.triangles[i];
        s << t.v0 << "  " << t.v1 << "  " << t.v2;
        QString manningsTok;
        if (std::isfinite(t.mannings) && t.mannings > 0.0)
        {
            manningsTok = QString::number(t.mannings, 'f', 4);
        }
        else if (i < origRows.size())
        {
            const QStringList tok =
                origRows[i].simplified().split(QChar(' '), Qt::SkipEmptyParts);
            bool okn = false;
            if (tok.size() >= 4) tok[3].toDouble(&okn);
            if (okn) manningsTok = tok[3];
        }
        // A depth or tag can only be written behind a MANNINGS_N token
        // (columns are positional). Materialize the default rather than
        // silently dropping the edit.
        const bool needsLaterCols = writeDepthCol || !t.tag.isEmpty();
        if (manningsTok.isEmpty() && needsLaterCols)
            manningsTok = QString::number(defaultMannings, 'f', 4);

        if (!manningsTok.isEmpty())
            s << "  " << manningsTok;
        if (!manningsTok.isEmpty() && writeDepthCol)
        {
            if (std::isfinite(t.initDepth))
                s << "  " << t.initDepth;
            else
            {
                const QString od = origDepthTok(i);
                s << "  " << (od.isEmpty() ? QStringLiteral("0") : od);
            }
        }
        if (!manningsTok.isEmpty() && !t.tag.isEmpty())
            s << "  " << t.tag;
        s << "\n";
    }
    s << "\n";
    return out;
}

} // namespace

QString InpMeshWriter::buildSectionText(const MeshResult &mesh,
                                        const CouplingMap &coupling,
                                        double defaultMannings,
                                        const UnitInfo &units)
{
    QString out;
    out.reserve(64 * mesh.vertices.size() + 64 * mesh.triangles.size());

    // Descriptive header — values are emitted verbatim and not used by the
    // writer itself.  An engine that recognises `;; UNITS: SI (m)` can
    // skip its FLOW_UNITS-based mesh scaling; legacy engines ignore both
    // comment lines and treat the file as project-unit (today's behaviour).
    if (!units.linearUnitName.isEmpty())
        out += QStringLiteral(";; UNITS: %1\n").arg(units.linearUnitName);
    if (!units.sourceCrsTag.isEmpty())
        out += QStringLiteral(";; SOURCE_CRS: %1\n").arg(units.sourceCrsTag);
    if (!units.linearUnitName.isEmpty() || !units.sourceCrsTag.isEmpty())
        out += QChar('\n');

    out += formatVertices(mesh);
    out += formatTriangles(mesh, coupling, defaultMannings);
    out += formatVertexNodeMap(mesh, coupling);
    out += formatTriangleNodeMap(mesh, coupling);
    // GG0a — [2D_INFILTRATION*] ride with the mesh, so they belong in
    // whichever file this text lands in (the .2dm in external mode, the .inp
    // inline). Omitted entirely when the mesh carries no infiltration data.
    out += formatInfilSections(mesh);
    return out;
}

namespace {

// Read entire .inp text into a QString. Sets *err and returns nullopt-ish
// (empty + ok=false) on failure.
struct ReadResult { QString text; bool ok = false; QString err; };
ReadResult readInp(const QString &inpPath)
{
    ReadResult r;
    QFile in(inpPath);
    if (!in.exists()) {
        r.err = QStringLiteral("Input file does not exist: %1").arg(inpPath);
        return r;
    }
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.err = QStringLiteral("Cannot open %1 for reading.").arg(inpPath);
        return r;
    }
    r.text = QString::fromUtf8(in.readAll());
    r.ok   = true;
    return r;
}

/*! The `[2D_MESH_FILE]` FILE token for \a meshPath as seen from \a inpPath.
 *
 *  Uses the same idiom as every other relative-path site in the project
 *  (ProjectSerializer::toRelativePath, RelativePathPicker, PathBrowseDelegate):
 *  QDir::relativeFilePath, then QFileInfo::isRelative to detect the cross-volume
 *  case where Qt hands back the absolute path unchanged.
 *
 *  This replaced a prefix match (`meshAbs.startsWith(inpDir + '/')`) that could
 *  only express a mesh living AT OR BELOW the .inp directory. A mesh kept in a
 *  sibling folder — a shared mesh directory, the common reason to keep it out of
 *  the model folder — got a machine-specific absolute path instead of
 *  `../mesh/x.2dm`, so the project stopped being movable.
 */
QString meshRefToken(const QString &inpPath, const QString &meshPath)
{
    const QDir inpDir = QFileInfo(inpPath).absoluteDir();
    const QString meshAbs =
        QDir::cleanPath(QFileInfo(meshPath).absoluteFilePath());
    const QString rel = inpDir.relativeFilePath(meshAbs);
    // Cross-volume (different drive / UNC share): Qt returns the absolute path.
    return QFileInfo(rel).isRelative() ? rel : meshAbs;
}

bool atomicWrite(const QString &path, const QString &text, QString *errorOut)
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errorOut) *errorOut = QStringLiteral("Cannot open %1 for writing.").arg(path);
        return false;
    }
    out.write(text.toUtf8());
    if (!out.commit())
    {
        if (errorOut) *errorOut = QStringLiteral("Atomic save failed for %1: %2")
                                       .arg(path, out.errorString());
        return false;
    }
    return true;
}

} // namespace

bool InpMeshWriter::writeInline(const QString &inpPath,
                                const MeshResult &mesh,
                                const CouplingMap &coupling,
                                double defaultMannings,
                                QString *errorOut,
                                const UnitInfo &units)
{
    auto fail = [&](const QString &msg) -> bool {
        if (errorOut) *errorOut = msg;
        return false;
    };

    if (!mesh.ok || mesh.vertices.isEmpty() || mesh.triangles.isEmpty())
        return fail(QStringLiteral("Mesh is empty or invalid; nothing to write."));

    const ReadResult r = readInp(inpPath);
    if (!r.ok) return fail(r.err);

    // Inline mode: strip any existing [2D_*] AND [2D_MESH_FILE] block so
    // a stale external reference doesn't shadow the freshly-inlined data.
    QString patched = stripExistingMeshSections(r.text, /*alsoMeshFileRef=*/true);
    if (!patched.endsWith(QChar('\n')))
        patched.append(QChar('\n'));
    patched.append(buildSectionText(mesh, coupling, defaultMannings, units));

    return atomicWrite(inpPath, patched, errorOut);
}

bool InpMeshWriter::writeExternal(const QString &inpPath,
                                  const QString &meshFilePath,
                                  const MeshResult &mesh,
                                  const CouplingMap &coupling,
                                  double defaultMannings,
                                  QString *errorOut,
                                  const UnitInfo &units)
{
    auto fail = [&](const QString &msg) -> bool {
        if (errorOut) *errorOut = msg;
        return false;
    };

    if (!mesh.ok || mesh.vertices.isEmpty() || mesh.triangles.isEmpty())
        return fail(QStringLiteral("Mesh is empty or invalid; nothing to write."));

    // Default mesh path: <inpDir>/<basename>.2dm
    const QFileInfo inpFi(inpPath);
    QString meshPath = meshFilePath;
    if (meshPath.isEmpty())
        meshPath = inpFi.absoluteDir().filePath(inpFi.completeBaseName()
                                                 + QStringLiteral(".2dm"));

    // 1) Write the external .2dm — header + four sections.
    {
        QString meshText;
        meshText += QStringLiteral(
            ";; OpenSWMM 2D Mesh File\n"
            ";; Source project: %1\n"
            ";; Generated by openswmm.gui (Slice AU)\n\n")
                        .arg(inpFi.fileName());
        meshText += buildSectionText(mesh, coupling, defaultMannings, units);
        if (!atomicWrite(meshPath, meshText, errorOut))
            return false;
    }

    // 2) Patch the .inp: strip any inline 2D-data sections, then inject
    //    a [2D_MESH_FILE] FILE <relpath> reference. `[2D_OPTIONS]` is
    //    preserved in the .inp; the engine's external-file parser doesn't
    //    re-read options unless the .2dm carries its own [2D_OPTIONS]
    //    block (which we don't write — those live with the project).
    const ReadResult r = readInp(inpPath);
    if (!r.ok) return fail(r.err);

    // Strip existing 2D-data sections + any prior [2D_MESH_FILE] (we'll
    // emit a fresh one).
    QString patched = stripExistingMeshSections(r.text, /*alsoMeshFileRef=*/true);

    // Relative to the .inp directory, including `../` forms; absolute only
    // when no relative form exists (different volume).
    const QString refPath = meshRefToken(inpPath, meshPath);

    if (!patched.endsWith(QChar('\n')))
        patched.append(QChar('\n'));
    patched.append(QStringLiteral("\n[2D_MESH_FILE]\n;; FILE  <path relative to this .inp, or absolute>\nFILE  %1\n\n")
                       .arg(refPath));

    return atomicWrite(inpPath, patched, errorOut);
}

bool InpMeshWriter::writeMeshFileRef(const QString &inpPath,
                                     const QString &meshFilePath,
                                     QString *errorOut)
{
    auto fail = [&](const QString &msg) -> bool {
        if (errorOut) *errorOut = msg;
        return false;
    };

    if (meshFilePath.isEmpty())
        return fail(QStringLiteral("No mesh file specified."));
    if (!QFileInfo::exists(meshFilePath))
        return fail(QStringLiteral("Mesh file does not exist: %1").arg(meshFilePath));

    const ReadResult r = readInp(inpPath);
    if (!r.ok) return fail(r.err);

    // Retarget-only: strip any inline [2D_*] mesh data AND a prior
    // [2D_MESH_FILE] block so the engine reads exactly the one external
    // file we point at. No mesh geometry is written — the .2dm already
    // exists on disk; we only repoint the reference.
    QString patched = stripExistingMeshSections(r.text, /*alsoMeshFileRef=*/true);

    // Same token rule as writeExternal, so generation and retargeting produce
    // identical reference forms.
    const QString refPath = meshRefToken(inpPath, meshFilePath);

    if (!patched.endsWith(QChar('\n')))
        patched.append(QChar('\n'));
    patched.append(QStringLiteral("\n[2D_MESH_FILE]\n;; FILE  <path relative to this .inp, or absolute>\nFILE  %1\n\n")
                       .arg(refPath));

    return atomicWrite(inpPath, patched, errorOut);
}

bool InpMeshWriter::clearMeshFileRef(const QString &inpPath, QString *errorOut)
{
    auto fail = [&](const QString &msg) -> bool {
        if (errorOut) *errorOut = msg;
        return false;
    };

    const ReadResult r = readInp(inpPath);
    if (!r.ok) return fail(r.err);

    // Strip ONLY the [2D_MESH_FILE] block — inline [2D_*] mesh data is
    // deliberately preserved so the engine reads the embedded mesh.
    const QStringList lines = r.text.split(QChar('\n'), Qt::KeepEmptyParts);
    QString patched;
    patched.reserve(r.text.size());
    bool inStripped = false;
    for (const QString &raw : lines)
    {
        const QString trimmed = raw.trimmed();
        if (trimmed.startsWith(QChar('[')) && trimmed.endsWith(QChar(']')))
        {
            inStripped = (trimmed.compare(QStringLiteral("[2D_MESH_FILE]"),
                                          Qt::CaseInsensitive) == 0);
            if (inStripped) continue;
        }
        if (inStripped) continue;
        patched.append(raw);
        patched.append(QChar('\n'));
    }
    while (patched.endsWith(QStringLiteral("\n\n")))
        patched.chop(1);

    return atomicWrite(inpPath, patched, errorOut);
}

bool InpMeshWriter::write(MeshOutputMode mode,
                          const QString &inpPath,
                          const QString &meshFilePath,
                          const MeshResult &mesh,
                          const CouplingMap &coupling,
                          double defaultMannings,
                          QString *errorOut,
                          const UnitInfo &units)
{
    switch (mode)
    {
        case MeshOutputMode::External:
            return writeExternal(inpPath, meshFilePath, mesh, coupling,
                                  defaultMannings, errorOut, units);
        case MeshOutputMode::Inline:
            return writeInline(inpPath, mesh, coupling, defaultMannings,
                                errorOut, units);
    }
    return false;
}

// =============================================================================
// Slice §V.VD.1 — [2D_BOUNDARY_CONDITIONS] section emission
// =============================================================================

QString InpMeshWriter::buildBCSectionText(const QVector<MeshEdgeBC> &bcs)
{
    // Skip the whole section if every entry is the default Wall + empty
    // group + zero params. Keeps the .inp pristine for projects that
    // never edit BCs.
    bool anyNonDefault = false;
    for (const auto &bc : bcs) {
        if (bc.type != MeshBCTypes::Type::Wall ||
            !bc.group.isEmpty() ||
            bc.head != 0.0 || bc.slope != 0.0 || bc.flow != 0.0 ||
            !bc.tseries.isEmpty() || !bc.curve.isEmpty()) {
            anyNonDefault = true;
            break;
        }
    }
    if (!anyNonDefault) return QString();

    QString out;
    out.reserve(bcs.size() * 48);
    out.append(QStringLiteral(
        "\n[2D_BOUNDARY_CONDITIONS]\n"
        ";; TRI    EDGE  TYPE             PARAM_1              PARAM_2  GROUP\n"));
    for (int flat = 0; flat < bcs.size(); ++flat) {
        const auto &bc = bcs[flat];
        if (bc.type == MeshBCTypes::Type::Wall && bc.group.isEmpty())
            continue;  // skip default-Wall rows to keep section compact
        const int tri = flat / 3;
        const int e   = flat % 3;
        QString param1 = QStringLiteral("*");
        switch (bc.type) {
        case MeshBCTypes::Type::Wall:
            break;
        case MeshBCTypes::Type::NormalFlow:
            param1 = QString::number(bc.slope, 'g', 6);
            break;
        case MeshBCTypes::Type::SpecifiedStageConst:
            param1 = QString::number(bc.head, 'g', 6);
            break;
        case MeshBCTypes::Type::SpecifiedStageTS:
        case MeshBCTypes::Type::SpecifiedFlowTS:
            param1 = bc.tseries.isEmpty() ? QStringLiteral("*") : bc.tseries;
            break;
        case MeshBCTypes::Type::SpecifiedFlowConst:
            param1 = QString::number(bc.flow, 'g', 6);
            break;
        case MeshBCTypes::Type::RatingCurve:
            param1 = bc.curve.isEmpty() ? QStringLiteral("*") : bc.curve;
            break;
        }
        const QString group = bc.group.isEmpty() ? QStringLiteral("*") : bc.group;
        out.append(QStringLiteral("%1 %2 %3 %4 %5 %6\n")
                       .arg(tri,    6)
                       .arg(e,      4)
                       .arg(MeshBCTypes::inpToken(bc.type), -16)
                       .arg(param1, -20)
                       .arg(QStringLiteral("*"), -7)  // PARAM_2 reserved
                       .arg(group));
    }
    return out;
}

// =============================================================================
// Engine §11A — [2D_EDGE_CONVEYANCE] section emission
// =============================================================================

QString InpMeshWriter::buildConveyanceSectionText(const MeshResult &mesh,
                                                  const QVector<MeshEdgeBC> &bcs)
{
    constexpr double kDefault = 1.0;
    bool anyNonDefault = false;
    for (const auto &bc : bcs) {
        if (bc.conveyance != kDefault) { anyNonDefault = true; break; }
    }
    if (!anyNonDefault) return QString();

    QString out;
    out.reserve(bcs.size() * 24);
    out.append(QStringLiteral(
        "\n[2D_EDGE_CONVEYANCE]\n"
        ";; FROM_VERTEX  TO_VERTEX  CONVEYANCE\n"));

    // Dedupe interior edges (which share a value across two slots) by
    // emitting only the first encounter of each sorted vertex pair. The
    // engine accepts either orientation, so we emit the pair as-found on
    // the canonical slot.
    QSet<QPair<int,int>> emitted;
    emitted.reserve(bcs.size() / 2);
    for (int t = 0; t < mesh.triangles.size(); ++t) {
        const auto &tri = mesh.triangles[t];
        const int va[3] = {tri.v1, tri.v2, tri.v0};
        const int vb[3] = {tri.v2, tri.v0, tri.v1};
        for (int e = 0; e < 3; ++e) {
            const int flat = t * 3 + e;
            if (flat >= bcs.size())                       continue;
            if (bcs[flat].conveyance == kDefault)         continue;  // omit defaults
            const QPair<int,int> key = (va[e] < vb[e]) ? qMakePair(va[e], vb[e])
                                                       : qMakePair(vb[e], va[e]);
            if (emitted.contains(key))                    continue;  // interior dupe
            emitted.insert(key);
            out.append(QStringLiteral("%1 %2 %3\n")
                           .arg(va[e], 6)
                           .arg(vb[e], 6)
                           .arg(QString::number(bcs[flat].conveyance, 'g', 6)));
        }
    }
    return out;
}

bool InpMeshWriter::writeExternal(const QString &inpPath,
                                  const QString &meshFilePath,
                                  const MeshResult &mesh,
                                  const CouplingMap &coupling,
                                  const QVector<MeshEdgeBC> &bcs,
                                  double defaultMannings,
                                  QString *errorOut,
                                  const UnitInfo &units)
{
    // Delegate the bulk of the work to the existing overload, then patch
    // the BC section into the same .inp afterwards. Two passes keeps the
    // BC-write path orthogonal to the four-section path; if the user
    // disables BCs the original behaviour is preserved.
    if (!writeExternal(inpPath, meshFilePath, mesh, coupling,
                       defaultMannings, errorOut, units))
        return false;

    const QString bcText = buildBCSectionText(bcs);
    const QString convText = buildConveyanceSectionText(mesh, bcs);
    if (bcText.isEmpty() && convText.isEmpty()) return true;  // nothing to add

    auto fail = [&](const QString &msg) -> bool {
        if (errorOut) *errorOut = msg;
        return false;
    };
    const ReadResult r = readInp(inpPath);
    if (!r.ok) return fail(r.err);

    QString patched = r.text;
    if (!patched.endsWith(QChar('\n')))
        patched.append(QChar('\n'));
    patched.append(bcText);
    patched.append(convText);
    return atomicWrite(inpPath, patched, errorOut);
}

bool InpMeshWriter::writeInline(const QString &inpPath,
                                const MeshResult &mesh,
                                const CouplingMap &coupling,
                                const QVector<MeshEdgeBC> &bcs,
                                double defaultMannings,
                                QString *errorOut,
                                const UnitInfo &units)
{
    if (!writeInline(inpPath, mesh, coupling, defaultMannings, errorOut, units))
        return false;

    const QString bcText = buildBCSectionText(bcs);
    const QString convText = buildConveyanceSectionText(mesh, bcs);
    if (bcText.isEmpty() && convText.isEmpty()) return true;

    auto fail = [&](const QString &msg) -> bool {
        if (errorOut) *errorOut = msg;
        return false;
    };
    const ReadResult r = readInp(inpPath);
    if (!r.ok) return fail(r.err);

    QString patched = r.text;
    if (!patched.endsWith(QChar('\n')))
        patched.append(QChar('\n'));
    patched.append(bcText);
    patched.append(convText);
    return atomicWrite(inpPath, patched, errorOut);
}

bool InpMeshWriter::patchBCSections(const QString &filePath,
                                    const MeshResult &mesh,
                                    const QVector<MeshEdgeBC> &bcs,
                                    QString *errorOut)
{
    const ReadResult r = readInp(filePath);
    if (!r.ok) {
        if (errorOut) *errorOut = r.err;
        return false;
    }

    QString patched = stripSections(
        r.text, {QStringLiteral("[2D_BOUNDARY_CONDITIONS]"),
                 QStringLiteral("[2D_EDGE_CONVEYANCE]")});
    if (!patched.endsWith(QChar('\n')))
        patched.append(QChar('\n'));
    patched.append(buildBCSectionText(bcs));
    patched.append(buildConveyanceSectionText(mesh, bcs));
    return atomicWrite(filePath, patched, errorOut);
}

bool InpMeshWriter::patchAttributeSections(const QString &filePath,
                                           const MeshResult &mesh,
                                           QString *errorOut,
                                           double defaultMannings)
{
    const ReadResult r = readInp(filePath);
    if (!r.ok) {
        if (errorOut) *errorOut = r.err;
        return false;
    }

    // Rows map to mesh entries by position — a count mismatch means the file
    // holds a different mesh (e.g. regenerated with other parameters), and
    // patching would scramble it. Leave the file untouched.
    const QStringList vRows = sectionDataRows(r.text, kSecVertices);
    const QStringList tRows = sectionDataRows(r.text, kSecTriangles);
    if (vRows.size() != mesh.vertices.size()
        || tRows.size() != mesh.triangles.size()) {
        if (errorOut)
            *errorOut = QStringLiteral(
                "mesh file has %1 vertices / %2 triangles; layer has %3 / %4")
                .arg(vRows.size()).arg(tRows.size())
                .arg(mesh.vertices.size()).arg(mesh.triangles.size());
        return false;
    }

    // The layer's per-vertex coupledNode fields are the authoritative
    // 1D↔2D vertex coupling (pushMeshEditsToEngine pushes exactly these);
    // rebuild the writer's map form from them. Triangle couplings ride on
    // MeshResult::cellCouplings, which formatTriangleNodeMap reads directly.
    CouplingMap cm;
    for (int i = 0; i < mesh.vertices.size(); ++i)
        if (!mesh.vertices[i].coupledNode.isEmpty())
            cm.vertexToNode.insert(i, mesh.vertices[i].coupledNode);

    QString patched = stripSections(
        r.text, {QLatin1String(kSecVertices),
                 QLatin1String(kSecTriangles),
                 QLatin1String(kSecVertexNodeMap),
                 QLatin1String(kSecTriangleNodeMap),
                 // GG0a — a section missing from THIS list (and from the
                 // re-emit below) is discarded on every save: the save path
                 // restores a pre-engine-write snapshot of the mesh file and
                 // re-emits the GUI's state through this function. The
                 // vertex-Z comment at swmmvisprojectwindow.cpp:1414-1419
                 // documents the same failure mode.
                 QLatin1String(kSecInfilOptions),
                 QLatin1String(kSecInfilDefaults),
                 QLatin1String(kSecInfil)});
    if (!patched.endsWith(QChar('\n')))
        patched.append(QChar('\n'));
    patched.append(formatVertices(mesh));
    patched.append(formatTrianglesPreserving(mesh, tRows, defaultMannings));
    patched.append(formatVertexNodeMap(mesh, cm));
    patched.append(formatTriangleNodeMap(mesh, cm));
    patched.append(formatInfilSections(mesh));
    return atomicWrite(filePath, patched, errorOut);
}

} // namespace mesh
