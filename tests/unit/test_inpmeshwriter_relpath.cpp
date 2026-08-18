/*!
 * \file   test_inpmeshwriter_relpath.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  InpMeshWriter emits `[2D_MESH_FILE]` tokens relative to the `.inp`.
 *
 * The token used to be computed by prefix match
 * (`meshAbs.startsWith(inpDir + '/')`), which can only express a mesh at or
 * below the `.inp` directory. A mesh kept in a SIBLING folder — the usual
 * reason to keep it out of the model folder in the first place — fell through
 * to a machine-specific absolute path and the project stopped being movable.
 * These tests pin the `../` form, the plain-relative form, and the
 * generation/retarget agreement between writeExternal and writeMeshFileRef.
 */

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "mesh/inpmeshwriter.h"

using mesh::InpMeshWriter;

namespace {

const char *kMinimalInp = R"INP([TITLE]
mesh ref token probe

[OPTIONS]
FLOW_UNITS           CMS

[JUNCTIONS]
;;Name  Elev  MaxDepth  InitDepth  SurDepth  Aponded
J1      10    5         0          0         0
)INP";

void writeText(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream(&f) << text;
}

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

//! The FILE token from the [2D_MESH_FILE] block, or empty.
QString meshToken(const QString &inpText)
{
    const int at = inpText.indexOf(QStringLiteral("[2D_MESH_FILE]"));
    if (at < 0) return {};
    const QStringList lines = inpText.mid(at).split(QChar('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (!line.startsWith(QStringLiteral("FILE"))) continue;
        return line.mid(4).trimmed();
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// A mesh in a sibling directory must produce a `../` token, not an absolute one
// ---------------------------------------------------------------------------

TEST(InpMeshWriterRelPath, SiblingDirectoryMeshGetsParentRelativeToken)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString inpPath  = tmp.filePath(QStringLiteral("model/model.inp"));
    const QString meshPath = tmp.filePath(QStringLiteral("shared/terrain.2dm"));
    writeText(inpPath, QString::fromUtf8(kMinimalInp));
    writeText(meshPath, QStringLiteral(";; mesh\n"));

    QString err;
    ASSERT_TRUE(InpMeshWriter::writeMeshFileRef(inpPath, meshPath, &err)) << err.toStdString();

    const QString tok = meshToken(readText(inpPath));
    ASSERT_FALSE(tok.isEmpty());
    EXPECT_TRUE(QFileInfo(tok).isRelative())
        << "sibling-directory mesh leaked an absolute path: " << tok.toStdString();
    EXPECT_TRUE(tok.startsWith(QStringLiteral("../")))
        << "expected a parent-relative token, got: " << tok.toStdString();

    // The token must resolve back to the mesh from the .inp's directory.
    const QDir inpDir = QFileInfo(inpPath).absoluteDir();
    EXPECT_EQ(QFileInfo(inpDir.absoluteFilePath(tok)).canonicalFilePath(),
              QFileInfo(meshPath).canonicalFilePath());
}

// ---------------------------------------------------------------------------
// The already-working cases stay working
// ---------------------------------------------------------------------------

TEST(InpMeshWriterRelPath, MeshBesideTheInpGetsBareFilename)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString inpPath  = tmp.filePath(QStringLiteral("model/model.inp"));
    const QString meshPath = tmp.filePath(QStringLiteral("model/terrain.2dm"));
    writeText(inpPath, QString::fromUtf8(kMinimalInp));
    writeText(meshPath, QStringLiteral(";; mesh\n"));

    QString err;
    ASSERT_TRUE(InpMeshWriter::writeMeshFileRef(inpPath, meshPath, &err)) << err.toStdString();

    EXPECT_EQ(meshToken(readText(inpPath)), QStringLiteral("terrain.2dm"));
}

TEST(InpMeshWriterRelPath, MeshInSubdirectoryGetsRelativeSubpath)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString inpPath  = tmp.filePath(QStringLiteral("model/model.inp"));
    const QString meshPath = tmp.filePath(QStringLiteral("model/mesh/terrain.2dm"));
    writeText(inpPath, QString::fromUtf8(kMinimalInp));
    writeText(meshPath, QStringLiteral(";; mesh\n"));

    QString err;
    ASSERT_TRUE(InpMeshWriter::writeMeshFileRef(inpPath, meshPath, &err)) << err.toStdString();

    EXPECT_EQ(meshToken(readText(inpPath)), QStringLiteral("mesh/terrain.2dm"));
}

// ---------------------------------------------------------------------------
// Retargeting an existing reference replaces it rather than accumulating
// ---------------------------------------------------------------------------

TEST(InpMeshWriterRelPath, RetargetReplacesThePreviousReference)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    const QString inpPath = tmp.filePath(QStringLiteral("model/model.inp"));
    const QString first   = tmp.filePath(QStringLiteral("model/first.2dm"));
    const QString second  = tmp.filePath(QStringLiteral("shared/second.2dm"));
    writeText(inpPath, QString::fromUtf8(kMinimalInp));
    writeText(first,  QStringLiteral(";; mesh\n"));
    writeText(second, QStringLiteral(";; mesh\n"));

    QString err;
    ASSERT_TRUE(InpMeshWriter::writeMeshFileRef(inpPath, first, &err)) << err.toStdString();
    ASSERT_TRUE(InpMeshWriter::writeMeshFileRef(inpPath, second, &err)) << err.toStdString();

    const QString text = readText(inpPath);
    EXPECT_EQ(text.count(QStringLiteral("[2D_MESH_FILE]")), 1)
        << "retarget must replace the reference, not append a second one";
    EXPECT_EQ(meshToken(text), QStringLiteral("../shared/second.2dm"));
}
