/*!
 * \file   inpmeshreader.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/inpmeshreader.h"

#include <QChar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace mesh {

namespace {

constexpr const char *kSecVertices       = "[2D_VERTICES]";
constexpr const char *kSecTriangles      = "[2D_TRIANGLES]";
constexpr const char *kSecMeshFile       = "[2D_MESH_FILE]";

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

/*! Strip an inline trailing comment ("X Y Z ; comment" → "X Y Z"). */
QString stripComment(QString line)
{
    const int semi = line.indexOf(QChar(';'));
    if (semi >= 0) line.truncate(semi);
    return line;
}

/*! Token-split a data line. Returns empty vector for comment/blank lines. */
QStringList tokenize(const QString &raw)
{
    const QString line = stripComment(raw).trimmed();
    if (line.isEmpty()) return {};
    static const QRegularExpression ws(QStringLiteral(R"(\s+)"));
    return line.split(ws, Qt::SkipEmptyParts);
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
            // tok[3] is MANNINGS_N — ignored for rendering. tok[4] is TAG.
            if (tok.size() >= 5) t.tag = tok[4];
            out.triangles.append(t);
        }
        return {};
    }

    // Other sections (coupling maps, options) carry no rendering data.
    return {};
}

/*! Walk \p text section-by-section, accumulating vertex / triangle data into
 *  \p out. Records whether either section was actually seen so the caller
 *  can distinguish "no mesh in this file" from "mesh present but empty". */
QString parseSectionsFromText(const QString &text,
                              MeshResult &out,
                              bool &sawVertices,
                              bool &sawTriangles,
                              QString *meshFileRefOut)
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

} // namespace

InpMeshReadResult InpMeshReader::read(const QString &inpPath)
{
    InpMeshReadResult result;

    QString inpText;
    if (!readTextFile(inpPath, &inpText, &result.errorMsg))
        return result;

    // First pass on the .inp: capture any [2D_MESH_FILE] reference and parse
    // inline mesh sections opportunistically. The engine prefers the external
    // file when both are present, so an external reference clears any inline
    // data we accumulated.
    MeshResult inlineMesh;
    bool sawInlineVerts = false;
    bool sawInlineTris  = false;
    QString meshFileRef;
    const QString inpErr =
        parseSectionsFromText(inpText, inlineMesh, sawInlineVerts, sawInlineTris,
                              &meshFileRef);
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

        MeshResult extMesh;
        bool sawExtVerts = false;
        bool sawExtTris  = false;
        const QString extErr =
            parseSectionsFromText(meshText, extMesh, sawExtVerts, sawExtTris, nullptr);
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
    }
    return result;
}

} // namespace mesh
