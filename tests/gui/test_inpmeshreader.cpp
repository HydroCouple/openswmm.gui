/*!
 * \file   test_inpmeshreader.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  LOAD_PERF plan Phase 2 — InpMeshReader parse contract.
 *
 *         The reader had no test of its own, yet Phase 2 replaces its
 *         tokenizer and its coupling-map tag resolution — both load-bearing
 *         for every `.2dm`. These cases pin the behaviour that must survive:
 *
 *           - index-form and tag-form [2D_VERTEX_NODE_MAP] resolve identically
 *             (the writer PREFERS tag form, so this is the common path)
 *           - an unresolvable tag is skipped, not fatal
 *           - first-occurrence-wins on duplicate tags (the old forward scan's
 *             semantics, which the lazy hash must reproduce)
 *           - [2D_TRIANGLE_NODE_MAP] tag form behaves the same way
 *           - tokenizer equivalence: tabs, runs of spaces, inline ';'
 *             comments, CRLF, leading/trailing whitespace
 *           - ';; UNITS:' / ';; SOURCE_CRS:' headers still parse
 *
 *         Test artifacts are written under a reviewable path, never a temp
 *         directory (CLAUDE.md §4.1).
 */
#include "mesh/inpmeshreader.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTextStream>

namespace {

/*! Directory holding this test's generated .inp fixtures. Reviewable, and
 *  left in place after the run so a failure can be inspected by hand. */
QString outDir()
{
    const QString d = QStringLiteral("tests/output/inpmeshreader");
    QDir().mkpath(d);
    return d;
}

/*! Write \p text to <outDir>/<name> and return the full path. */
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

/*! Four vertices in a unit square + two triangles. Vertex tags V0..V3 so the
 *  tag-form coupling map has something to resolve against. */
QString baseMesh()
{
    return QStringLiteral(
        ";; UNITS: SI (m)\n"
        ";; SOURCE_CRS: EPSG:32617\n"
        "[2D_VERTICES]\n"
        "0.0 0.0 10.0 V0\n"
        "1.0 0.0 11.0 V1\n"
        "1.0 1.0 12.0 V2\n"
        "0.0 1.0 13.0 V3\n"
        "\n"
        "[2D_TRIANGLES]\n"
        "0 1 2 0.013\n"
        "0 2 3 0.013\n"
        "\n");
}

} // namespace

class TestInpMeshReader : public QObject
{
    Q_OBJECT

private slots:
    void parsesBaseMesh();
    void unitsAndCrsHeaders();
    void unitsHeaderIsSI_matchesEngineKeywords();
    void vertexNodeMap_indexForm();
    void vertexNodeMap_tagForm();
    void vertexNodeMap_indexAndTagAgree();
    void vertexNodeMap_unresolvableTagSkipped();
    void vertexNodeMap_duplicateTagFirstWins();
    void triangleNodeMap_tagForm();
    void tokenizer_whitespaceAndComments_data();
    void tokenizer_whitespaceAndComments();
    void tokenizer_crlfLineEndings();
    void profileExternalMesh();
};

void TestInpMeshReader::parsesBaseMesh()
{
    const QString p = writeFixture(QStringLiteral("base.inp"), baseMesh());
    QVERIFY(!p.isEmpty());

    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(p);
    QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
    QVERIFY(r.hasMesh);
    QCOMPARE(r.mesh.vertices.size(), 4);
    QCOMPARE(r.mesh.triangles.size(), 2);
    QCOMPARE(r.mesh.vertices[0].tag, QStringLiteral("V0"));
    QCOMPARE(r.mesh.vertices[3].z, 13.0);
}

void TestInpMeshReader::unitsAndCrsHeaders()
{
    // Phase 2 folds scanUnitsHeader into the single parse pass; both header
    // lines must still be picked up.
    const QString p = writeFixture(QStringLiteral("headers.inp"), baseMesh());
    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(p);
    QVERIFY(r.hasMesh);
    QCOMPARE(r.unitsHeader, QStringLiteral("SI (m)"));
    QCOMPARE(r.sourceCrsTag, QStringLiteral("EPSG:32617"));
}

// Issue #155 — whether the engine rescaled the mesh to SI turns on this
// keyword set. It is duplicated from the engine's prescan2DUnitsHeader
// (openswmm.engine SectionHandlers2D.cpp); if the two drift, 2D results land
// in the wrong place with no error anywhere. Pin the whole set.
void TestInpMeshReader::unitsHeaderIsSI_matchesEngineKeywords()
{
    for (const QString &si : {QStringLiteral("SI (m)"), QStringLiteral("m"),
                              QStringLiteral("metre"), QStringLiteral("metres"),
                              QStringLiteral("meter"), QStringLiteral("meters"),
                              QStringLiteral("si (M)"), QStringLiteral("  m  ")})
        QVERIFY2(mesh::unitsHeaderIsSI(si), qPrintable(si));

    for (const QString &notSi : {QString(), QStringLiteral("ft"),
                                 QStringLiteral("US survey foot"),
                                 QStringLiteral("feet"),
                                 QStringLiteral("millimetres"),
                                 QStringLiteral("SI")})
        QVERIFY2(!mesh::unitsHeaderIsSI(notSi), qPrintable(notSi));
}

void TestInpMeshReader::vertexNodeMap_indexForm()
{
    const QString p = writeFixture(
        QStringLiteral("vnm_index.inp"),
        baseMesh() + QStringLiteral("[2D_VERTEX_NODE_MAP]\n"
                                    "0 J1\n"
                                    "2 J2 0.7 3.0\n"));
    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(p);
    QVERIFY(r.hasMesh);
    QCOMPARE(r.mesh.vertices[0].coupledNode, QStringLiteral("J1"));
    QCOMPARE(r.mesh.vertices[2].coupledNode, QStringLiteral("J2"));
    QCOMPARE(r.mesh.vertices[2].couplingCd, 0.7);
    QCOMPARE(r.mesh.vertices[2].couplingArea, 3.0);
    QVERIFY(r.mesh.vertices[1].coupledNode.isEmpty());
}

void TestInpMeshReader::vertexNodeMap_tagForm()
{
    // This is what the writer actually emits, and the path that used to be
    // O(nCoupled x nVertices).
    const QString p = writeFixture(
        QStringLiteral("vnm_tag.inp"),
        baseMesh() + QStringLiteral("[2D_VERTEX_NODE_MAP]\n"
                                    "V0 J1\n"
                                    "V2 J2 0.7 3.0\n"));
    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(p);
    QVERIFY(r.hasMesh);
    QCOMPARE(r.mesh.vertices[0].coupledNode, QStringLiteral("J1"));
    QCOMPARE(r.mesh.vertices[2].coupledNode, QStringLiteral("J2"));
    QCOMPARE(r.mesh.vertices[2].couplingCd, 0.7);
    QCOMPARE(r.mesh.vertices[2].couplingArea, 3.0);
}

void TestInpMeshReader::vertexNodeMap_indexAndTagAgree()
{
    const QString idx = writeFixture(
        QStringLiteral("vnm_cmp_index.inp"),
        baseMesh() + QStringLiteral("[2D_VERTEX_NODE_MAP]\n0 J1\n1 J2\n3 J4\n"));
    const QString tag = writeFixture(
        QStringLiteral("vnm_cmp_tag.inp"),
        baseMesh() + QStringLiteral("[2D_VERTEX_NODE_MAP]\nV0 J1\nV1 J2\nV3 J4\n"));

    const mesh::InpMeshReadResult a = mesh::InpMeshReader::read(idx);
    const mesh::InpMeshReadResult b = mesh::InpMeshReader::read(tag);
    QCOMPARE(a.mesh.vertices.size(), b.mesh.vertices.size());
    for (int i = 0; i < a.mesh.vertices.size(); ++i) {
        QCOMPARE(a.mesh.vertices[i].coupledNode, b.mesh.vertices[i].coupledNode);
        QCOMPARE(a.mesh.vertices[i].couplingCd,  b.mesh.vertices[i].couplingCd);
        QCOMPARE(a.mesh.vertices[i].couplingArea, b.mesh.vertices[i].couplingArea);
    }
}

void TestInpMeshReader::vertexNodeMap_unresolvableTagSkipped()
{
    // A tag matching no vertex is skipped; the rest of the section still
    // applies. (An out-of-range NUMERIC index falls through to the tag path
    // and is likewise skipped.)
    const QString p = writeFixture(
        QStringLiteral("vnm_miss.inp"),
        baseMesh() + QStringLiteral("[2D_VERTEX_NODE_MAP]\n"
                                    "NOPE J9\n"
                                    "999 J8\n"
                                    "V1 J2\n"));
    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(p);
    QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
    QVERIFY(r.hasMesh);
    QCOMPARE(r.mesh.vertices[1].coupledNode, QStringLiteral("J2"));
    QVERIFY(r.mesh.vertices[0].coupledNode.isEmpty());
    QVERIFY(r.mesh.vertices[2].coupledNode.isEmpty());
    QVERIFY(r.mesh.vertices[3].coupledNode.isEmpty());
}

void TestInpMeshReader::vertexNodeMap_duplicateTagFirstWins()
{
    // The replaced linear scan broke on the FIRST match; the lazy hash must
    // reproduce that, not last-wins.
    const QString dup =
        ";; UNITS: SI (m)\n"
        "[2D_VERTICES]\n"
        "0.0 0.0 1.0 DUP\n"
        "1.0 0.0 2.0 DUP\n"
        "1.0 1.0 3.0 OTHER\n"
        "\n"
        "[2D_TRIANGLES]\n"
        "0 1 2 0.013\n"
        "\n"
        "[2D_VERTEX_NODE_MAP]\n"
        "DUP J1\n";
    const QString p = writeFixture(QStringLiteral("vnm_dup.inp"), dup);
    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(p);
    QVERIFY(r.hasMesh);
    QCOMPARE(r.mesh.vertices[0].coupledNode, QStringLiteral("J1"));
    QVERIFY(r.mesh.vertices[1].coupledNode.isEmpty());
}

void TestInpMeshReader::triangleNodeMap_tagForm()
{
    const QString src =
        ";; UNITS: SI (m)\n"
        "[2D_VERTICES]\n"
        "0.0 0.0 1.0\n"
        "1.0 0.0 2.0\n"
        "1.0 1.0 3.0\n"
        "0.0 1.0 4.0\n"
        "\n"
        "[2D_TRIANGLES]\n"
        "0 1 2 0.013 0.0 TA\n"
        "0 2 3 0.013 0.0 TB\n"
        "\n"
        "[2D_TRIANGLE_NODE_MAP]\n"
        "TB J7 0.6 2.0\n";
    const QString p = writeFixture(QStringLiteral("tnm_tag.inp"), src);
    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(p);
    QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
    QVERIFY(r.hasMesh);
    QCOMPARE(r.mesh.triangles[1].tag, QStringLiteral("TB"));
    QCOMPARE(r.mesh.cellCouplings.size(), 1);
    QCOMPARE(r.mesh.cellCouplings[0].tri, 1);
    QCOMPARE(r.mesh.cellCouplings[0].nodeId, QStringLiteral("J7"));
    QCOMPARE(r.mesh.cellCouplings[0].cd, 0.6);
    QCOMPARE(r.mesh.cellCouplings[0].area, 2.0);
}

void TestInpMeshReader::tokenizer_whitespaceAndComments_data()
{
    QTest::addColumn<QString>("vertexLine");
    QTest::addColumn<QString>("expectTag");

    QTest::newRow("single spaces")   << "2.0 3.0 4.0 T"            << "T";
    QTest::newRow("multiple spaces") << "2.0    3.0     4.0    T"  << "T";
    QTest::newRow("tabs")            << "2.0\t3.0\t4.0\tT"         << "T";
    QTest::newRow("mixed")           << "2.0 \t 3.0\t  4.0 \tT"    << "T";
    QTest::newRow("leading ws")      << "   2.0 3.0 4.0 T"         << "T";
    QTest::newRow("trailing ws")     << "2.0 3.0 4.0 T   "         << "T";
    QTest::newRow("inline comment")  << "2.0 3.0 4.0 T ; a note"   << "T";
    QTest::newRow("comment no space")<< "2.0 3.0 4.0 T;note"       << "T";
}

void TestInpMeshReader::tokenizer_whitespaceAndComments()
{
    QFETCH(QString, vertexLine);
    QFETCH(QString, expectTag);

    const QString src =
        QStringLiteral(";; UNITS: SI (m)\n"
                       "[2D_VERTICES]\n"
                       "0.0 0.0 0.0 A\n"
                       "1.0 0.0 1.0 B\n")
        + vertexLine + QStringLiteral("\n"
                       "\n"
                       "[2D_TRIANGLES]\n"
                       "0 1 2 0.013\n");
    const QString p = writeFixture(QStringLiteral("tok.inp"), src);
    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(p);

    QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
    QVERIFY(r.hasMesh);
    QCOMPARE(r.mesh.vertices.size(), 3);
    QCOMPARE(r.mesh.vertices[2].xy.x(), 2.0);
    QCOMPARE(r.mesh.vertices[2].xy.y(), 3.0);
    QCOMPARE(r.mesh.vertices[2].z, 4.0);
    QCOMPARE(r.mesh.vertices[2].tag, expectTag);
}

void TestInpMeshReader::tokenizer_crlfLineEndings()
{
    // A .2dm authored on Windows carries \r\n. The \r must not survive into
    // the last token of every line (which would corrupt every tag).
    QString src = baseMesh() + QStringLiteral("[2D_VERTEX_NODE_MAP]\nV1 J2\n");
    src.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));

    const QString p = writeFixture(QStringLiteral("crlf.inp"), src);
    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(p);

    QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
    QVERIFY(r.hasMesh);
    QCOMPARE(r.mesh.vertices.size(), 4);
    QCOMPARE(r.mesh.vertices[0].tag, QStringLiteral("V0"));
    QCOMPARE(r.mesh.vertices[1].coupledNode, QStringLiteral("J2"));
    QCOMPARE(r.unitsHeader, QStringLiteral("SI (m)"));
}

void TestInpMeshReader::profileExternalMesh()
{
    // LOAD_PERF Phase 2 gate harness. Skipped unless pointed at a fixture, so
    // it is a no-op in CI:
    //
    //   SWMM_PROFILE_MESH=<path.inp|path.2dm> \
    //     ./build/tests/gui/test_inpmeshreader profileExternalMesh
    //
    // Generate a repro fixture with:
    //   mesh_perf_generator --coupled=50000 <dir> 1500000
    const QString path = qEnvironmentVariable("SWMM_PROFILE_MESH");
    if (path.isEmpty())
        QSKIP("set SWMM_PROFILE_MESH=<path> to profile a real mesh");
    if (!QFileInfo::exists(path))
        QFAIL(qPrintable(QStringLiteral("no such file: %1").arg(path)));

    QElapsedTimer t;
    t.start();
    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(path);
    const qint64 ms = t.elapsed();

    qInfo().noquote()
        << QStringLiteral("[mesh-parse] %1: %2 verts, %3 tris, %4 coupled, "
                          "read=%5 ms")
               .arg(QFileInfo(path).fileName())
               .arg(r.mesh.vertices.size())
               .arg(r.mesh.triangles.size())
               .arg([&r] {
                   int n = 0;
                   for (const auto &v : r.mesh.vertices)
                       if (!v.coupledNode.isEmpty()) ++n;
                   return n;
               }())
               .arg(ms);

    QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
    QVERIFY(r.hasMesh);
}

QTEST_MAIN(TestInpMeshReader)
#include "test_inpmeshreader.moc"
