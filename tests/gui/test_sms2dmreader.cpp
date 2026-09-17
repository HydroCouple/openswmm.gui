/*!
 * \file   test_sms2dmreader.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  TRI_QUAD_MESHING_PLAN phase G5 — Sms2dmReader parse contract.
 *
 *           - ND / E3T / E4Q cards land as vertices, triangle cells and quad
 *             cells (v3 set), triangles first then quads (engine order)
 *           - 1-based, sparse node ids are remapped; elements before nodes
 *           - MAT column becomes the tag `mat<n>`; other cards are ignored
 *           - splitQuads turns each E4Q into two triangles along the
 *             mesh::cellGeom diagonal, covering the quad exactly once
 *           - malformed cards fail with the line number
 *
 *         Test artifacts are written under a reviewable path, never a temp
 *         directory (CLAUDE.md §4.1).
 */
#include "mesh/sms2dmreader.h"
#include "mesh/meshcellgeom.h"

#include <QDir>
#include <QFile>
#include <QTest>

#include <cmath>

namespace {

QString outDir()
{
    const QString d = QStringLiteral("tests/output/sms2dmreader");
    QDir().mkpath(d);
    return d;
}

QString writeFixture(const QString &name, const QString &text)
{
    const QString path = outDir() + QStringLiteral("/") + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    f.write(text.toUtf8());
    f.close();
    return path;
}

/*! A unit square split into a triangle pair on the left half and one quad
 *  on the right half: nodes 1..6 (ids sparse: 10 replaces 6), elements
 *  listed BEFORE nodes as SMS writes them. */
QString mixedMesh()
{
    return QStringLiteral(
        "MESH2D\n"
        "MESHNAME \"demo\"\n"
        "NUM_MATERIALS_PER_ELEM 1\n"
        "E4Q 1 2 3 5 4 7\n"
        "E3T 2 1 2 4 3\n"
        "E3T 3 1 4 10 3\n"
        "ND 1 0.0 0.0 10.0\n"
        "ND 2 1.0 0.0 10.5\n"
        "ND 3 2.0 0.0 11.0\n"
        "ND 4 1.0 1.0 12.0\n"
        "ND 5 2.0 1.0 11.5\n"
        "ND 10 0.0 1.0 10.2\n"
        "NS 1 2 -3\n"
        "BEGPARAMDEF\n"
        "ENDPARAMDEF\n");
}

} // namespace

class TestSms2dmReader : public QObject
{
    Q_OBJECT

private slots:
    void parsesMixedMesh();
    void readsFromFile();
    void splitQuadsUsesCellGeomDiagonal();
    void malformedCardReportsLine();
    void unknownNodeIdFails();
};

void TestSms2dmReader::parsesMixedMesh()
{
    const mesh::MeshResult m = mesh::Sms2dmReader::parse(mixedMesh());
    QVERIFY2(m.ok, qPrintable(m.errorMsg));
    QCOMPARE(m.vertices.size(), 6);
    QCOMPARE(m.vertices[5].xy, QPointF(0.0, 1.0));   // id 10 → index 5
    QCOMPARE(m.vertices[3].z, 12.0);

    // Triangles first (file order), then the quad.
    QCOMPARE(m.triangles.size(), 3);
    QCOMPARE(m.quadCount(), 1);
    QVERIFY(!m.triangles[0].isQuad());
    QCOMPARE(m.triangles[0].v0, 0); QCOMPARE(m.triangles[0].v1, 1); QCOMPARE(m.triangles[0].v2, 3);
    QCOMPARE(m.triangles[0].tag, QStringLiteral("mat3"));
    QCOMPARE(m.triangles[1].v2, 5);                    // node id 10 remapped
    QVERIFY(m.triangles[2].isQuad());
    QCOMPARE(m.triangles[2].vertexCount(), 4);
    QCOMPARE(m.triangles[2].v0, 1); QCOMPARE(m.triangles[2].v1, 2);
    QCOMPARE(m.triangles[2].v2, 4); QCOMPARE(m.triangles[2].v3, 3);
    QCOMPARE(m.triangles[2].tag, QStringLiteral("mat7"));
    QVERIFY(std::isnan(m.triangles[2].mannings));
}

void TestSms2dmReader::readsFromFile()
{
    const QString p = writeFixture(QStringLiteral("mixed.2dm"), mixedMesh());
    QVERIFY(!p.isEmpty());
    const mesh::MeshResult m = mesh::Sms2dmReader::read(p);
    QVERIFY2(m.ok, qPrintable(m.errorMsg));
    QCOMPARE(m.triangles.size(), 3);

    const mesh::MeshResult missing =
        mesh::Sms2dmReader::read(outDir() + QStringLiteral("/nope.2dm"));
    QVERIFY(!missing.ok);
    QVERIFY(!missing.errorMsg.isEmpty());
}

void TestSms2dmReader::splitQuadsUsesCellGeomDiagonal()
{
    const mesh::MeshResult whole = mesh::Sms2dmReader::parse(mixedMesh());
    const mesh::MeshResult split = mesh::Sms2dmReader::parse(mixedMesh(), /*splitQuads=*/true);
    QVERIFY2(split.ok, qPrintable(split.errorMsg));
    QCOMPARE(split.triangles.size(), 4);
    QCOMPARE(split.quadCount(), 0);

    // The two halves are exactly cellGeom's sub-triangles of the quad, in
    // order, and carry the quad's tag.
    const mesh::CellGeom g = mesh::cellGeom(whole.vertices, whole.triangles[2]);
    QCOMPARE(g.nSub, 2);
    for (int s = 0; s < 2; ++s) {
        const mesh::MeshTriangle &t = split.triangles[2 + s];
        QCOMPARE(t.v0, g.sub[s][0]);
        QCOMPARE(t.v1, g.sub[s][1]);
        QCOMPARE(t.v2, g.sub[s][2]);
        QCOMPARE(t.tag, QStringLiteral("mat7"));
    }
    // Area is conserved by the split.
    const double a0 = mesh::cellGeom(split.vertices, split.triangles[2]).area;
    const double a1 = mesh::cellGeom(split.vertices, split.triangles[3]).area;
    QVERIFY(qFuzzyCompare(a0 + a1, g.area));
}

void TestSms2dmReader::malformedCardReportsLine()
{
    const mesh::MeshResult m = mesh::Sms2dmReader::parse(
        QStringLiteral("MESH2D\nND 1 0 0 0\nND 2 1 0 0\nND 3 0 1 0\nE3T 1 1 2\n"));
    QVERIFY(!m.ok);
    QVERIFY2(m.errorMsg.contains(QStringLiteral("line 5")), qPrintable(m.errorMsg));
}

void TestSms2dmReader::unknownNodeIdFails()
{
    const mesh::MeshResult m = mesh::Sms2dmReader::parse(
        QStringLiteral("ND 1 0 0 0\nND 2 1 0 0\nND 3 0 1 0\nE3T 1 1 2 9 1\n"));
    QVERIFY(!m.ok);
    QVERIFY2(m.errorMsg.contains(QStringLiteral("line 4")), qPrintable(m.errorMsg));
    QVERIFY2(m.errorMsg.contains(QStringLiteral("9")), qPrintable(m.errorMsg));
}

QTEST_MAIN(TestSms2dmReader)
#include "test_sms2dmreader.moc"
