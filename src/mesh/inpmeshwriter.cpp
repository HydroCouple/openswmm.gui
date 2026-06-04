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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <QtCore/QChar>

namespace mesh {

// Out-of-line default ctor (declared in the header) — see UnitInfo's comment.
InpMeshWriter::UnitInfo::UnitInfo() = default;

namespace {

constexpr const char *kSecVertices       = "[2D_VERTICES]";
constexpr const char *kSecTriangles      = "[2D_TRIANGLES]";
constexpr const char *kSecVertexNodeMap  = "[2D_VERTEX_NODE_MAP]";
constexpr const char *kSecTriangleNodeMap= "[2D_TRIANGLE_NODE_MAP]";
constexpr const char *kSecMeshFile       = "[2D_MESH_FILE]";

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
    s << ";; V1   V2   V3   MANNINGS_N   TAG\n";
    for (int i = 0; i < mesh.triangles.size(); ++i)
    {
        const MeshTriangle &t = mesh.triangles[i];
        const double n = coupling.triangleMannings.value(i, defaultMannings);
        s << t.v0 << "  " << t.v1 << "  " << t.v2 << "  " << n;
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
    s << ";; VERTEX_INDEX_OR_TAG    SWMM_NODE_NAME\n";
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
        const QString &tag = mesh.vertices[vIdx].tag;
        if (!tag.isEmpty())
            s << tag;
        else
            s << vIdx;
        s << "        " << node << "\n";
    }
    s << "\n";
    return out;
}

QString formatTriangleNodeMap(const MeshResult &mesh,
                              const CouplingMap &coupling)
{
    if (coupling.triangleToNode.isEmpty()) return {};

    QString out;
    QTextStream s(&out);
    s << kSecTriangleNodeMap << "\n";
    s << ";; TRIANGLE_INDEX_OR_TAG  SWMM_NODE_NAME\n";
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
    s << "\n";
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
    };
    if (alsoMeshFileRef)
        ours.append(QStringLiteral("[2D_MESH_FILE]"));

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

    // Path relative to the .inp directory when both files share a parent;
    // absolute otherwise. Keeps the project portable when copied as a
    // unit, falls back to absolute if the user pointed at a shared mesh
    // directory elsewhere.
    QString refPath;
    {
        const QString meshAbs = QFileInfo(meshPath).absoluteFilePath();
        const QString inpDir  = inpFi.absoluteDir().absolutePath();
        if (meshAbs.startsWith(inpDir + QChar('/')))
            refPath = meshAbs.mid(inpDir.size() + 1);
        else
            refPath = meshAbs;
    }

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

    // Path relative to the .inp directory when both share a parent;
    // absolute otherwise. Mirrors writeExternal so generation and
    // retargeting produce identical reference forms.
    const QFileInfo inpFi(inpPath);
    QString refPath;
    {
        const QString meshAbs = QFileInfo(meshFilePath).absoluteFilePath();
        const QString inpDir  = inpFi.absoluteDir().absolutePath();
        if (meshAbs.startsWith(inpDir + QChar('/')))
            refPath = meshAbs.mid(inpDir.size() + 1);
        else
            refPath = meshAbs;
    }

    if (!patched.endsWith(QChar('\n')))
        patched.append(QChar('\n'));
    patched.append(QStringLiteral("\n[2D_MESH_FILE]\n;; FILE  <path relative to this .inp, or absolute>\nFILE  %1\n\n")
                       .arg(refPath));

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

} // namespace mesh
