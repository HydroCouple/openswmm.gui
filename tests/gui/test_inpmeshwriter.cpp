/*!
 * \file   test_inpmeshwriter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AU — QtTest coverage for InpMeshWriter. Verifies external + inline
 * modes, [2D_MESH_FILE] reference patching, idempotent rewrites, and
 * tag-vs-index emission in the coupling maps.
 */
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QPointF>
#include <QString>
#include <QTemporaryDir>

#include "mesh/inpmeshwriter.h"
#include "mesh/meshresult.h"

using namespace mesh;

class TestInpMeshWriter : public QObject
{
    Q_OBJECT

private:
    /*! Build a tiny mesh — 4 vertices, 2 triangles forming a square split
     *  along one diagonal. Vertex 0 carries a tag "J1" so it can act as a
     *  coupling vertex via tag form; vertex 1 stays untagged so it tests
     *  the index form. */
    static MeshResult sampleMesh()
    {
        MeshResult m;
        m.ok = true;
        m.vertices.append({QPointF(0, 0),     1.0, 1, "J1"});
        m.vertices.append({QPointF(100, 0),   1.5, 0, ""  });
        m.vertices.append({QPointF(100, 100), 2.0, 0, ""  });
        m.vertices.append({QPointF(0, 100),   1.2, 0, ""  });
        m.triangles.append({0, 1, 2, "subcatch_S1"});
        m.triangles.append({0, 2, 3, "" });
        return m;
    }

    static CouplingMap sampleCoupling()
    {
        CouplingMap c;
        c.vertexToNode.insert(0, "J1");           // by tag (vertex 0 has tag "J1")
        c.vertexToNode.insert(1, "J2");           // by index (vertex 1 untagged)
        c.triangleToNode.insert(0, "S1");
        c.triangleMannings.insert(0, 0.025);      // override default
        // triangle 1 uses the writer's default Manning's n
        return c;
    }

    /*! Minimum-viable .inp body — just a TITLE + OPTIONS block. */
    static QString sampleInpText()
    {
        return QStringLiteral(
            "[TITLE]\nDemo\n\n[OPTIONS]\nFLOW_UNITS  CFS\n\n");
    }

private slots:

    void buildSectionText_includesAllFour()
    {
        const QString text = InpMeshWriter::buildSectionText(
            sampleMesh(), sampleCoupling());
        QVERIFY(text.contains("[2D_VERTICES]"));
        QVERIFY(text.contains("[2D_TRIANGLES]"));
        QVERIFY(text.contains("[2D_VERTEX_NODE_MAP]"));
        QVERIFY(text.contains("[2D_TRIANGLE_NODE_MAP]"));
        // Vertex 0 has tag "J1"; should appear in the vertices section.
        QVERIFY(text.contains("J1"));
        // Triangle 0 has tag "subcatch_S1"; should appear in the triangles.
        QVERIFY(text.contains("subcatch_S1"));
        // Manning's override (0.025) survived.
        QVERIFY(text.contains("0.0250") || text.contains("0.025"));
    }

    void buildSectionText_vertexMapCarriesCdArea()
    {
        MeshResult m = sampleMesh();
        m.vertices[1].couplingCd   = 0.7;
        m.vertices[1].couplingArea = 2.0;
        const QString text = InpMeshWriter::buildSectionText(m, sampleCoupling());

        // Extract the vertex-node-map block.
        const int sec = text.indexOf("[2D_VERTEX_NODE_MAP]");
        QVERIFY(sec >= 0);
        int end = text.indexOf("\n[", sec + 1);
        if (end < 0) end = text.size();
        const QString block = text.mid(sec, end - sec);

        bool v1HasCdArea = false, v0HasDefaults = false;
        const QStringList lines = block.split('\n');
        for (const QString &line : lines) {
            const QString t = line.trimmed();
            if (t.isEmpty() || t.startsWith(";;") || t.startsWith('[')) continue;
            const QStringList tok =
                t.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (tok.size() < 4) continue;
            if (tok[1] == "J2" && tok[2].toDouble() == 0.7
                && tok[3].toDouble() == 2.0)
                v1HasCdArea = true;
            if (tok[1] == "J1" && tok[2].toDouble() == 0.65
                && tok[3].toDouble() == 1.0)
                v0HasDefaults = true;
        }
        QVERIFY2(v1HasCdArea, qPrintable(block));
        QVERIFY2(v0HasDefaults, qPrintable(block));
    }

    void buildSectionText_emptyVertexMap_omitsSection()
    {
        CouplingMap c;  // no entries at all
        const QString text = InpMeshWriter::buildSectionText(sampleMesh(), c);
        QVERIFY(text.contains("[2D_VERTICES]"));
        QVERIFY(text.contains("[2D_TRIANGLES]"));
        QVERIFY(!text.contains("[2D_VERTEX_NODE_MAP]"));
        QVERIFY(!text.contains("[2D_TRIANGLE_NODE_MAP]"));
    }

    /*! External mode: writes .2dm next to .inp + injects [2D_MESH_FILE]. */
    void writeExternal_basic()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath = dir.filePath("project.inp");
        QFile inp(inpPath);
        QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
        inp.write(sampleInpText().toUtf8());
        inp.close();

        QString err;
        const bool ok = InpMeshWriter::writeExternal(
            inpPath, /*meshFilePath=*/"", sampleMesh(), sampleCoupling(),
            0.035, &err);
        QVERIFY2(ok, qPrintable(err));

        // Sibling .2dm exists and contains the four sections.
        const QString meshPath = dir.filePath("project.2dm");
        QVERIFY(QFile::exists(meshPath));
        QFile mf(meshPath);
        QVERIFY(mf.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString meshText = QString::fromUtf8(mf.readAll());
        QVERIFY(meshText.contains("[2D_VERTICES]"));
        QVERIFY(meshText.contains("[2D_TRIANGLES]"));

        // .inp now has [2D_MESH_FILE] FILE project.2dm; original [OPTIONS]
        // preserved.
        QFile in2(inpPath);
        QVERIFY(in2.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString inpText = QString::fromUtf8(in2.readAll());
        QVERIFY(inpText.contains("[OPTIONS]"));
        QVERIFY(inpText.contains("[2D_MESH_FILE]"));
        QVERIFY(inpText.contains("FILE"));
        QVERIFY(inpText.contains("project.2dm"));
        // No inlined 2D-data sections in the .inp.
        QVERIFY(!inpText.contains("[2D_VERTICES]"));
        QVERIFY(!inpText.contains("[2D_TRIANGLES]"));
    }

    /*! External writes the path relative to the .inp directory when both
     *  are siblings (portable when copied as a unit). */
    void writeExternal_relativePath()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath  = dir.filePath("project.inp");
        QFile inp(inpPath);
        QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
        inp.write(sampleInpText().toUtf8());
        inp.close();

        QString err;
        QVERIFY(InpMeshWriter::writeExternal(
            inpPath, /*meshFilePath=*/"", sampleMesh(), sampleCoupling(),
            0.035, &err));

        QFile in2(inpPath);
        QVERIFY(in2.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString inpText = QString::fromUtf8(in2.readAll());
        // No absolute path leaked.
        QVERIFY(!inpText.contains(dir.path()));
        // Plain "project.2dm" is the relative reference.
        QVERIFY(inpText.contains("FILE  project.2dm"));
    }

    /*! Re-running external write should fully replace any prior 2D data
     *  + a stale [2D_MESH_FILE] block (idempotent). */
    void writeExternal_replacesPriorBlocks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath = dir.filePath("project.inp");
        QFile inp(inpPath);
        QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
        // Pre-existing inline sections + a stale [2D_MESH_FILE] block.
        inp.write(QStringLiteral(
            "[TITLE]\nDemo\n\n[OPTIONS]\nFLOW_UNITS CFS\n\n"
            "[2D_VERTICES]\nstale\n\n"
            "[2D_TRIANGLES]\nstale\n\n"
            "[2D_MESH_FILE]\nFILE old.2dm\n\n").toUtf8());
        inp.close();

        QString err;
        QVERIFY(InpMeshWriter::writeExternal(
            inpPath, /*meshFilePath=*/"", sampleMesh(), sampleCoupling(),
            0.035, &err));

        QFile in2(inpPath);
        QVERIFY(in2.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString inpText = QString::fromUtf8(in2.readAll());
        QVERIFY(!inpText.contains("stale"));
        QVERIFY(!inpText.contains("old.2dm"));
        QVERIFY(inpText.contains("project.2dm"));
        // Exactly one [2D_MESH_FILE] block.
        QCOMPARE(inpText.count(QStringLiteral("[2D_MESH_FILE]")), 1);
    }

    /*! Inline mode: sections in-place, no [2D_MESH_FILE]. */
    void writeInline_basic()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath = dir.filePath("project.inp");
        QFile inp(inpPath);
        QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
        inp.write(sampleInpText().toUtf8());
        inp.close();

        QString err;
        QVERIFY(InpMeshWriter::writeInline(
            inpPath, sampleMesh(), sampleCoupling(), 0.035, &err));

        QFile in2(inpPath);
        QVERIFY(in2.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString inpText = QString::fromUtf8(in2.readAll());
        QVERIFY(inpText.contains("[OPTIONS]"));
        QVERIFY(inpText.contains("[2D_VERTICES]"));
        QVERIFY(inpText.contains("[2D_TRIANGLES]"));
        QVERIFY(!inpText.contains("[2D_MESH_FILE]"));  // inline mode strips it

        // No sibling .2dm should be created.
        const QString meshPath = dir.filePath("project.2dm");
        QVERIFY(!QFile::exists(meshPath));
    }

    /*! clearMeshFileRef strips an external [2D_MESH_FILE] reference while
     *  preserving inline [2D_*] mesh data — the "switch to inline" path. */
    void clearMeshFileRef_dropsRefKeepsInline()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath = dir.filePath("project.inp");
        QFile inp(inpPath);
        QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
        // Inline mesh data AND an external reference both present.
        inp.write(QStringLiteral(
            "[TITLE]\nDemo\n\n[OPTIONS]\nFLOW_UNITS CFS\n\n"
            "[2D_VERTICES]\n0 0 1.0\n\n"
            "[2D_TRIANGLES]\n0 1 2 0.03\n\n"
            "[2D_MESH_FILE]\nFILE other.2dm\n\n").toUtf8());
        inp.close();

        QString err;
        QVERIFY2(InpMeshWriter::clearMeshFileRef(inpPath, &err), qPrintable(err));

        QFile in2(inpPath);
        QVERIFY(in2.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString inpText = QString::fromUtf8(in2.readAll());
        // Reference gone; inline mesh + other sections preserved.
        QVERIFY(!inpText.contains("[2D_MESH_FILE]"));
        QVERIFY(!inpText.contains("other.2dm"));
        QVERIFY(inpText.contains("[2D_VERTICES]"));
        QVERIFY(inpText.contains("[2D_TRIANGLES]"));
        QVERIFY(inpText.contains("[OPTIONS]"));
    }

    /*! clearMeshFileRef is a no-op-safe when no [2D_MESH_FILE] is present. */
    void clearMeshFileRef_noRef_isHarmless()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath = dir.filePath("project.inp");
        QFile inp(inpPath);
        QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
        inp.write(sampleInpText().toUtf8());
        inp.close();

        QString err;
        QVERIFY2(InpMeshWriter::clearMeshFileRef(inpPath, &err), qPrintable(err));

        QFile in2(inpPath);
        QVERIFY(in2.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString inpText = QString::fromUtf8(in2.readAll());
        QVERIFY(inpText.contains("[OPTIONS]"));
        QVERIFY(!inpText.contains("[2D_MESH_FILE]"));
    }

    /*! Empty mesh → fail gracefully with errorOut set, .inp untouched. */
    void emptyMesh_fails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath = dir.filePath("project.inp");
        QFile inp(inpPath);
        QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
        inp.write(sampleInpText().toUtf8());
        inp.close();

        MeshResult empty;  // ok=false by default
        CouplingMap c;
        QString err;
        QVERIFY(!InpMeshWriter::writeExternal(inpPath, "", empty, c, 0.035, &err));
        QVERIFY(!err.isEmpty());

        // .inp not touched.
        QFile in2(inpPath);
        QVERIFY(in2.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString inpText = QString::fromUtf8(in2.readAll());
        QCOMPARE(inpText, sampleInpText());
    }
};

QTEST_MAIN(TestInpMeshWriter)
#include "test_inpmeshwriter.moc"
