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

#include "mesh/inpmeshreader.h"
#include "mesh/inpmeshwriter.h"
#include "mesh/meshbctype.h"
#include "mesh/meshcellgeom.h"
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
        // v3 sits between v2 and tag since the tri-quad model landed —
        // designate it (-1 = triangle) rather than relying on position.
        m.triangles.append({0, 1, 2, -1, "subcatch_S1"});
        m.triangles.append({0, 2, 3, -1, "" });
        return m;
    }

    /*! Mixed mesh — a 2x1 strip of unit squares: the left square split
     *  into two triangles, the right square a single quad (cell 2, the
     *  engine's triangles-first order). Quad (1,2,5,4): edge 0 = (2,5),
     *  edge 1 = (5,4), edge 2 = (4,1) shared with triangle 0's edge 0,
     *  edge 3 = (1,2) on the bottom boundary. */
    static MeshResult mixedMesh()
    {
        MeshResult m;
        m.ok = true;
        m.vertices.append({QPointF(0, 0), 1.0, 0, ""});
        m.vertices.append({QPointF(1, 0), 1.1, 0, ""});
        m.vertices.append({QPointF(2, 0), 1.2, 0, ""});
        m.vertices.append({QPointF(0, 1), 1.3, 0, ""});
        m.vertices.append({QPointF(1, 1), 1.4, 0, ""});
        m.vertices.append({QPointF(2, 1), 1.5, 0, ""});
        m.triangles.append({0, 1, 4, -1, "left"});
        m.triangles.append({0, 4, 3, -1, ""});
        m.triangles.append({1, 2, 5, 4, "street"});
        m.triangles[2].initDepth = 0.25;
        return m;
    }

    /*! Two quads (0,1,4,3) and (1,2,5,4) sharing edge (1,4): quad 0's edge 0
     *  and quad 1's edge 2. */
    static MeshResult quadPairMesh()
    {
        MeshResult m = mixedMesh();
        m.triangles.clear();
        m.triangles.append({0, 1, 4, 3, ""});
        m.triangles.append({1, 2, 5, 4, ""});
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

    /*! INIT_DEPTH sits between MANNINGS_N and TAG; the column is emitted for
     *  EVERY row whenever any triangle carries a depth or a tag, so a numeric
     *  5th token always means INIT_DEPTH on re-read. */
    void buildSectionText_initDepthPrecedesTag()
    {
        MeshResult m = sampleMesh();
        m.triangles[0].initDepth = 0.25;
        const QString text = InpMeshWriter::buildSectionText(m, sampleCoupling());

        const int sec = text.indexOf("[2D_TRIANGLES]");
        QVERIFY(sec >= 0);
        int end = text.indexOf("\n[", sec + 1);
        if (end < 0) end = text.size();
        const QString block = text.mid(sec, end - sec);
        QVERIFY(block.contains("INIT_DEPTH"));

        bool sawDepthThenTag = false, allRowsHaveDepth = true;
        for (const QString &line : block.split('\n')) {
            const QString t = line.trimmed();
            if (t.isEmpty() || t.startsWith(";;") || t.startsWith('[')) continue;
            const QStringList tok =
                t.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (tok.size() < 5) { allRowsHaveDepth = false; continue; }
            bool okd = false;
            tok[4].toDouble(&okd);
            if (!okd) allRowsHaveDepth = false;
            if (okd && tok[4].toDouble() == 0.25 && tok.size() >= 6
                && tok[5] == QStringLiteral("subcatch_S1"))
                sawDepthThenTag = true;
        }
        QVERIFY2(allRowsHaveDepth, qPrintable(block));
        QVERIFY2(sawDepthThenTag, qPrintable(block));
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

    /*! Save-path regression: the post-save external-mesh restore rolls the
     *  sidecar back to its pre-edit snapshot, then patchAttributeSections
     *  re-emits the layer's attribute state. Vertex Z / tag / coupling and
     *  triangle Manning / tag / cell couplings must all survive; a triangle
     *  whose layer Manning is unset (NaN) must keep the generation-time
     *  token from the file. */
    void patchAttributeSections_reemitsEditedState()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath  = dir.filePath("project.inp");
        const QString meshPath = dir.filePath("project.2dm");
        {
            QFile inp(inpPath);
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
            // CMS keeps every unit factor at 1 so values round-trip verbatim.
            inp.write("[TITLE]\nDemo\n\n[OPTIONS]\nFLOW_UNITS  CMS\n\n");
            inp.close();
        }

        // Generation-time write — triangle 0 has an explicit Manning
        // (0.025), triangle 1 gets the writer default (0.035).
        QVERIFY(InpMeshWriter::writeExternal(inpPath, meshPath, sampleMesh(),
                                             sampleCoupling(), 0.035));

        // The layer's editable state after the user edits: Z + vertex tag +
        // vertex coupling + triangle tag + one cell coupling. Triangle
        // Mannings stay NaN (the layer only fills them on an explicit edit).
        // The layer's coupledNode fields carry the generation-time coupling —
        // both real flows fold it in (meshgenerationdialog folds the
        // CouplingMap; InpMeshReader parses [2D_VERTEX_NODE_MAP]).
        MeshResult edited = sampleMesh();
        edited.vertices[0].coupledNode = QStringLiteral("J1");
        edited.vertices[1].coupledNode = QStringLiteral("J2");
        edited.vertices[1].z   = 9.75;
        edited.vertices[2].tag = QStringLiteral("VTAG");
        edited.vertices[3].coupledNode  = QStringLiteral("J9");
        edited.vertices[3].couplingCd   = 0.5;
        edited.vertices[3].couplingArea = 2.5;
        edited.triangles[1].tag = QStringLiteral("subcatch_S2");
        CellCoupling cc;
        cc.tri = 1; cc.nodeId = QStringLiteral("J7"); cc.cd = 0.6; cc.area = 3.0;
        edited.cellCouplings.append(cc);

        QString err;
        QVERIFY2(InpMeshWriter::patchAttributeSections(meshPath, edited, &err),
                 qPrintable(err));

        const InpMeshReadResult r = InpMeshReader::read(inpPath);
        QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
        QVERIFY(r.hasMesh);
        QCOMPARE(r.mesh.vertices.size(), 4);
        QCOMPARE(r.mesh.triangles.size(), 2);
        QCOMPARE(r.mesh.vertices[1].z, 9.75);
        QCOMPARE(r.mesh.vertices[2].tag, QStringLiteral("VTAG"));
        QCOMPARE(r.mesh.vertices[3].coupledNode, QStringLiteral("J9"));
        QCOMPARE(r.mesh.vertices[3].couplingCd, 0.5);
        QCOMPARE(r.mesh.vertices[3].couplingArea, 2.5);
        // Untouched fields survive the rewrite.
        QCOMPARE(r.mesh.vertices[0].z, 1.0);
        QCOMPARE(r.mesh.vertices[0].tag, QStringLiteral("J1"));
        QCOMPARE(r.mesh.vertices[0].coupledNode, QStringLiteral("J1"));
        // Layer NaN Mannings keep the file's generation-time tokens.
        QCOMPARE(r.mesh.triangles[0].mannings, 0.025);
        QCOMPARE(r.mesh.triangles[1].mannings, 0.035);
        QCOMPARE(r.mesh.triangles[0].tag, QStringLiteral("subcatch_S1"));
        QCOMPARE(r.mesh.triangles[1].tag, QStringLiteral("subcatch_S2"));
        QCOMPARE(r.mesh.cellCouplings.size(), 1);
        QCOMPARE(r.mesh.cellCouplings[0].tri, 1);
        QCOMPARE(r.mesh.cellCouplings[0].nodeId, QStringLiteral("J7"));

        // An explicit layer-side Manning edit wins over the file token.
        MeshResult edited2 = r.mesh;
        edited2.triangles[1].mannings = 0.077;
        QVERIFY2(InpMeshWriter::patchAttributeSections(meshPath, edited2, &err),
                 qPrintable(err));
        const InpMeshReadResult r2 = InpMeshReader::read(inpPath);
        QVERIFY(r2.hasMesh);
        QCOMPARE(r2.mesh.triangles[1].mannings, 0.077);
        QCOMPARE(r2.mesh.triangles[1].tag, QStringLiteral("subcatch_S2"));
    }

    /*! Topology guard: a row-count mismatch means the file holds a
     *  different mesh — the patch must fail and leave the file untouched. */
    void patchAttributeSections_countMismatchFailsUntouched()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath  = dir.filePath("project.inp");
        const QString meshPath = dir.filePath("project.2dm");
        {
            QFile inp(inpPath);
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
            inp.write("[TITLE]\nDemo\n\n[OPTIONS]\nFLOW_UNITS  CMS\n\n");
            inp.close();
        }
        QVERIFY(InpMeshWriter::writeExternal(inpPath, meshPath, sampleMesh(),
                                             sampleCoupling(), 0.035));
        QFile before(meshPath);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray snapshot = before.readAll();
        before.close();

        MeshResult bigger = sampleMesh();
        bigger.vertices.append({QPointF(50, 50), 3.0, 0, ""});
        QString err;
        QVERIFY(!InpMeshWriter::patchAttributeSections(meshPath, bigger, &err));
        QVERIFY(!err.isEmpty());

        QFile after(meshPath);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), snapshot);
    }

    /*! An edited INIT_DEPTH must survive the patch even when the row carries
     *  no Manning's value on either side. Columns are positional, so the
     *  patcher materializes the default n rather than dropping the later
     *  columns — before the fix the depth (and tag) vanished silently. */
    void patchAttributeSections_depthSurvivesMissingMannings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString meshPath = dir.filePath("bare.2dm");

        // Hand-authored 3-token rows: connectivity only, no MANNINGS_N.
        {
            QFile f(meshPath);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write("[2D_VERTICES]\n"
                    "0   0   1.0\n"
                    "10  0   1.5\n"
                    "10  10  2.0\n"
                    "0   10  2.5\n"
                    "\n"
                    "[2D_TRIANGLES]\n"
                    "0  1  2\n"
                    "0  2  3\n");
            f.close();
        }

        MeshResult edited = sampleMesh();
        edited.triangles[0].initDepth = 0.4;
        edited.triangles[0].tag       = QStringLiteral("pond");
        // triangles[*].mannings stay NaN — the layer only fills them on an
        // explicit edit, and the file has no token to preserve either.

        QString err;
        QVERIFY2(InpMeshWriter::patchAttributeSections(meshPath, edited, &err),
                 qPrintable(err));

        const InpMeshReadResult r = InpMeshReader::read(meshPath);
        QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
        QVERIFY(r.hasMesh);
        QCOMPARE(r.mesh.triangles.size(), 2);
        QCOMPARE(r.mesh.triangles[0].initDepth, 0.4);
        QCOMPARE(r.mesh.triangles[0].tag, QStringLiteral("pond"));
        // The materialized Manning's keeps the columns positional.
        QVERIFY(std::isfinite(r.mesh.triangles[0].mannings));
        QVERIFY(r.mesh.triangles[0].mannings > 0.0);
    }

    /*! A depth edit must not disturb a Manning's value the file already
     *  carried — the patcher preserves the original token when the layer's
     *  own value is unset. */
    void patchAttributeSections_depthEditKeepsFileMannings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath  = dir.filePath("project.inp");
        const QString meshPath = dir.filePath("project.2dm");
        {
            QFile inp(inpPath);
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
            inp.write("[TITLE]\nDemo\n\n[OPTIONS]\nFLOW_UNITS  CMS\n\n");
            inp.close();
        }
        // Triangle 0 carries an explicit 0.025; triangle 1 the 0.035 default.
        QVERIFY(InpMeshWriter::writeExternal(inpPath, meshPath, sampleMesh(),
                                             sampleCoupling(), 0.035));

        MeshResult edited = sampleMesh();
        edited.triangles[1].initDepth = 0.15;   // depth only, mannings NaN

        QString err;
        QVERIFY2(InpMeshWriter::patchAttributeSections(meshPath, edited, &err),
                 qPrintable(err));

        const InpMeshReadResult r = InpMeshReader::read(inpPath);
        QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
        QCOMPARE(r.mesh.triangles.size(), 2);
        QCOMPARE(r.mesh.triangles[1].initDepth, 0.15);
        QCOMPARE(r.mesh.triangles[0].mannings, 0.025);
        QCOMPARE(r.mesh.triangles[1].mannings, 0.035);
    }

    // ── TRI_QUAD_MESHING_PLAN phase G1 — [2D_QUADS] ─────────────────────────

    /*! All-triangle output is pinned byte-for-byte: no [2D_QUADS] section,
     *  the historical column layout. Any change here breaks the R3 gate
     *  (all-triangle projects round-trip .inp byte-identically). */
    void buildSectionText_allTrianglePinnedText()
    {
        CouplingMap none;
        const QString text = InpMeshWriter::buildSectionText(sampleMesh(), none);
        const QString expected = QStringLiteral(
            "[2D_VERTICES]\n"
            ";; X            Y            Z            TAG\n"
            "0.000000  0.000000  1.000000  J1\n"
            "100.000000  0.000000  1.500000\n"
            "100.000000  100.000000  2.000000\n"
            "0.000000  100.000000  1.200000\n"
            "\n"
            "[2D_TRIANGLES]\n"
            ";; V1   V2   V3   MANNINGS_N   INIT_DEPTH   TAG\n"
            "0  1  2  0.0350  0.0000  subcatch_S1\n"
            "0  2  3  0.0350  0.0000\n"
            "\n");
        QCOMPARE(text, expected);
        QVERIFY(!text.contains("[2D_QUADS]"));
    }

    /*! A mixed mesh writes [2D_TRIANGLES] (triangle cells only) then
     *  [2D_QUADS] (`V1 V2 V3 V4 MANNINGS_N INIT_DEPTH TAG`), and the text
     *  survives reader -> writer byte-for-byte. */
    void quadRow_roundTripsByteIdentical()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath = dir.filePath("mixed.inp");
        {
            QFile inp(inpPath);
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
            inp.write(sampleInpText().toUtf8());
            inp.close();
        }
        CouplingMap c;
        c.triangleMannings.insert(0, 0.025);
        c.triangleMannings.insert(2, 0.018);   // the quad
        const QString first = InpMeshWriter::buildSectionText(mixedMesh(), c);

        const int tSec = first.indexOf("[2D_TRIANGLES]");
        const int qSec = first.indexOf("[2D_QUADS]");
        QVERIFY(tSec >= 0 && qSec > tSec);
        const QString triBlock  = first.mid(tSec, qSec - tSec);
        QVERIFY(triBlock.contains("0  1  4  0.0250  0.0000  left\n"));
        QVERIFY(triBlock.contains("0  4  3  0.0350  0.0000\n"));
        QVERIFY(!triBlock.contains("1  2  5  4"));
        QVERIFY(first.contains(";; V1   V2   V3   V4   MANNINGS_N   INIT_DEPTH   TAG\n"
                               "1  2  5  4  0.0180  0.2500  street\n"));

        QString err;
        QVERIFY2(InpMeshWriter::writeInline(inpPath, mixedMesh(), c, 0.035, &err),
                 qPrintable(err));
        const InpMeshReadResult r = InpMeshReader::read(inpPath);
        QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
        QVERIFY(r.hasMesh);
        QCOMPARE(r.mesh.triangles.size(), 3);
        QCOMPARE(r.mesh.quadCount(), 1);
        QVERIFY(r.mesh.triangles[2].isQuad());
        QCOMPARE(r.mesh.triangles[2].v3, 4);
        QCOMPARE(r.mesh.triangles[2].mannings, 0.018);
        QCOMPARE(r.mesh.triangles[2].initDepth, 0.25);
        QCOMPARE(r.mesh.triangles[2].tag, QStringLiteral("street"));

        // Re-emit from the parsed mesh with its own Manning values.
        CouplingMap c2;
        for (int i = 0; i < r.mesh.triangles.size(); ++i)
            c2.triangleMannings.insert(i, r.mesh.triangles[i].mannings);
        const QString second = InpMeshWriter::buildSectionText(r.mesh, c2);
        QCOMPARE(second, first);
    }

    /*! A BC on a quad's edge 3 (its (V1,V2) side) is written as `TRI EDGE`
     *  = (cell, 3) and read back into the same stride-4 slot. */
    void bcRow_onQuadEdge3()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath = dir.filePath("bc.inp");
        {
            QFile inp(inpPath);
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
            inp.write(sampleInpText().toUtf8());
            inp.close();
        }
        const MeshResult m = mixedMesh();
        QVector<MeshEdgeBC> bcs(mesh::edgeSlotCount(m.triangles.size()));
        MeshEdgeBC nf;
        nf.type  = MeshBCTypes::Type::NormalFlow;
        nf.slope = 0.002;
        bcs[mesh::edgeSlot(2, 3)] = nf;

        const QString bcText = InpMeshWriter::buildBCSectionText(bcs);
        QVERIFY2(bcText.contains("     2    3 NORMAL_FLOW"), qPrintable(bcText));

        QString err;
        QVERIFY2(InpMeshWriter::writeInline(inpPath, m, CouplingMap{}, bcs, 0.035, &err),
                 qPrintable(err));
        const InpMeshReadResult r = InpMeshReader::read(inpPath);
        QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
        QCOMPARE(r.edgeBCs.size(), mesh::edgeSlotCount(3));
        QCOMPARE(r.edgeBCs[mesh::edgeSlot(2, 3)].type, MeshBCTypes::Type::NormalFlow);
        QCOMPARE(r.edgeBCs[mesh::edgeSlot(2, 3)].slope, 0.002);
        // Every other slot (padding included) stays Wall.
        int nonWall = 0;
        for (const auto &b : r.edgeBCs) if (b.type != MeshBCTypes::Type::Wall) ++nonWall;
        QCOMPARE(nonWall, 1);
    }

    /*! Conveyance on a quad-quad edge: the two neighbour slots collapse to
     *  ONE vertex-pair row and both slots are repopulated on read. */
    void conveyanceRow_onQuadQuadEdge()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath = dir.filePath("conv.inp");
        {
            QFile inp(inpPath);
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
            inp.write(sampleInpText().toUtf8());
            inp.close();
        }
        const MeshResult m = quadPairMesh();
        QVector<MeshEdgeBC> bcs(mesh::edgeSlotCount(m.triangles.size()));
        bcs[mesh::edgeSlot(0, 0)].conveyance = 0.5;   // (1,4) seen from quad 0
        bcs[mesh::edgeSlot(1, 2)].conveyance = 0.5;   // (4,1) seen from quad 1

        const QString convText = InpMeshWriter::buildConveyanceSectionText(m, bcs);
        QCOMPARE(convText.count(QStringLiteral(" 0.5\n")), 1);
        QVERIFY2(convText.contains("     1      4 0.5\n"), qPrintable(convText));

        QString err;
        QVERIFY2(InpMeshWriter::writeInline(inpPath, m, CouplingMap{}, bcs, 0.035, &err),
                 qPrintable(err));
        const InpMeshReadResult r = InpMeshReader::read(inpPath);
        QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
        QCOMPARE(r.mesh.quadCount(), 2);
        QCOMPARE(r.edgeBCs[mesh::edgeSlot(0, 0)].conveyance, 0.5);
        QCOMPARE(r.edgeBCs[mesh::edgeSlot(1, 2)].conveyance, 0.5);
        int nonDefault = 0;
        for (const auto &b : r.edgeBCs) if (b.conveyance != 1.0) ++nonDefault;
        QCOMPARE(nonDefault, 2);
    }

    /*! patchAttributeSections on a mixed mesh: [2D_QUADS] rows are rebuilt
     *  from the quad cells (row j <-> cell n_tri + j), a file-side Manning
     *  token survives a NaN layer value, and the count guard sees quads. */
    void patchAttributeSections_mixedMesh()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inpPath  = dir.filePath("project.inp");
        const QString meshPath = dir.filePath("project.2dm");
        {
            QFile inp(inpPath);
            QVERIFY(inp.open(QIODevice::WriteOnly | QIODevice::Text));
            inp.write("[TITLE]\nDemo\n\n[OPTIONS]\nFLOW_UNITS  CMS\n\n");
            inp.close();
        }
        CouplingMap c;
        c.triangleMannings.insert(2, 0.018);
        QVERIFY(InpMeshWriter::writeExternal(inpPath, meshPath, mixedMesh(), c, 0.035));

        MeshResult edited = mixedMesh();
        edited.triangles[2].tag = QStringLiteral("avenue");   // mannings stay NaN
        QString err;
        QVERIFY2(InpMeshWriter::patchAttributeSections(meshPath, edited, &err),
                 qPrintable(err));
        const InpMeshReadResult r = InpMeshReader::read(inpPath);
        QVERIFY2(r.errorMsg.isEmpty(), qPrintable(r.errorMsg));
        QCOMPARE(r.mesh.triangles.size(), 3);
        QVERIFY(r.mesh.triangles[2].isQuad());
        QCOMPARE(r.mesh.triangles[2].v3, 4);
        QCOMPARE(r.mesh.triangles[2].mannings, 0.018);
        QCOMPARE(r.mesh.triangles[2].initDepth, 0.25);
        QCOMPARE(r.mesh.triangles[2].tag, QStringLiteral("avenue"));
        QCOMPARE(r.mesh.triangles[0].mannings, 0.035);

        // A quad-count mismatch is a different mesh: fail, file untouched.
        QFile before(meshPath);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray snapshot = before.readAll();
        before.close();
        QVERIFY(!InpMeshWriter::patchAttributeSections(meshPath, quadPairMesh(), &err));
        QFile after(meshPath);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), snapshot);
    }
};

QTEST_MAIN(TestInpMeshWriter)
#include "test_inpmeshwriter.moc"
