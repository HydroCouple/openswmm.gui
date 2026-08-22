/*!
 * \file   test_meshinfil.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase GG0 gates for per-cell 2D infiltration
 * (INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md §3.2a–§3.6).
 *
 * The two that actually discriminate:
 *
 *  - **GG0a / G9 — persistence.** The project save path snapshots the mesh
 *    file, lets the engine clobber it, restores the snapshot, then re-patches
 *    the GUI-owned sections. Any `[2D_INFILTRATION*]` section missing from
 *    either the strip list or `patchAttributeSections()` disappears on EVERY
 *    save. The vertex-Z precedent is the reason this gate exists.
 *  - **GG0c / G8 — undo restores INHERITANCE, not a copy.** Assigning to a
 *    cell that inherits from a tag and then undoing must leave the cell
 *    inheriting: `infilOverrides` must not contain it. A cell that comes back
 *    carrying an override with identical numbers looks correct in every UI and
 *    silently stops tracking its region.
 */
#include <QtTest>
#include <QTemporaryDir>

#include "layers/swmm2dmeshlayer.h"
#include "map/meshcommands.h"
#include "mesh/inpmeshreader.h"
#include "mesh/inpmeshwriter.h"
#include "mesh/meshinfil.h"
#include "mesh/meshresult.h"

#include <cmath>

using namespace mesh;

namespace {

/*! Four triangles in a 2x1 strip of quads. Tags give the resolution order
 *  something to resolve: LAWN, WOODS, LAWN, (untagged). */
MeshResult makeTaggedMesh()
{
    MeshResult m;
    for (int x = 0; x <= 2; ++x) {
        MeshVertex b; b.xy = QPointF(double(x) * 10.0, 0.0);  b.z = 10.0; m.vertices.append(b);
        MeshVertex t; t.xy = QPointF(double(x) * 10.0, 10.0); t.z = 10.0; m.vertices.append(t);
    }
    const char *tags[4] = {"LAWN", "WOODS", "LAWN", ""};
    for (int x = 0; x < 2; ++x) {
        const int b0 = x * 2, t0 = x * 2 + 1, b1 = (x + 1) * 2, t1 = (x + 1) * 2 + 1;
        MeshTriangle a; a.v0 = b0; a.v1 = b1; a.v2 = t1; a.mannings = 0.03;
        MeshTriangle c; c.v0 = b0; c.v1 = t1; c.v2 = t0; c.mannings = 0.03;
        m.triangles.append(a);
        m.triangles.append(c);
    }
    for (int i = 0; i < 4; ++i) m.triangles[i].tag = QString::fromLatin1(tags[i]);
    m.ok = true;
    return m;
}

InfilRow row(InfilMethod method, double p0,
             double p1 = qQNaN(), double p2 = qQNaN(),
             double p3 = qQNaN(), double p4 = qQNaN())
{
    InfilRow r;
    r.method = method;
    r.p[0] = p0; r.p[1] = p1; r.p[2] = p2; r.p[3] = p3; r.p[4] = p4;
    r.dest = InfilDest::Lost;
    return r;
}

/*! The mesh of the fixture, plus a '*' row, a LAWN tag row and one per-cell
 *  override on triangle 2 — all four resolution cases in one mesh. */
MeshResult makeConfiguredMesh()
{
    MeshResult m = makeTaggedMesh();
    m.infilDefaults.append({QStringLiteral("*"),
                            row(InfilMethod::Constant, 1.0)});
    m.infilDefaults.append({QStringLiteral("LAWN"),
                            row(InfilMethod::Horton, 3.0, 0.5, 4.14, 7.0, 0.0)});
    m.infilOverrides.insert(2, row(InfilMethod::CurveNumber, 85.0, qQNaN(), 7.0));
    m.infilOptions.infilStep = 120.0;
    return m;
}

bool sameRow(const InfilRow &a, const InfilRow &b)
{
    if (a.method != b.method || a.dest != b.dest) return false;
    for (int i = 0; i < kInfilMaxParams; ++i) {
        const bool na = std::isnan(a.p[i]), nb = std::isnan(b.p[i]);
        if (na != nb) return false;
        if (!na && std::abs(a.p[i] - b.p[i]) > 1e-9) return false;
    }
    return true;
}

} // namespace

class TestMeshInfil : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------
    // GG0a — resolution order (engine D-I3), in the GUI's own resolver.
    // -----------------------------------------------------------------

    /*! override > tag > '*' > none, on a mesh carrying all four cases. */
    void resolve_overrideBeatsTagBeatsStarBeatsNothing()
    {
        const MeshResult m = makeConfiguredMesh();

        const ResolvedInfil r0 = resolveInfil(m, 0);   // LAWN
        QCOMPARE(r0.row.method, InfilMethod::Horton);
        QCOMPARE(r0.provenance, InfilProvenance::Tag);
        QCOMPARE(r0.sourceTag, QStringLiteral("LAWN"));
        QVERIFY(r0.isInherited());
        QVERIFY(!r0.isOverride());

        const ResolvedInfil r1 = resolveInfil(m, 1);   // WOODS -> falls to '*'
        QCOMPARE(r1.row.method, InfilMethod::Constant);
        QCOMPARE(r1.provenance, InfilProvenance::Star);
        QVERIFY(r1.isInherited());

        const ResolvedInfil r2 = resolveInfil(m, 2);   // LAWN, overridden
        QCOMPARE(r2.row.method, InfilMethod::CurveNumber);
        QCOMPARE(r2.provenance, InfilProvenance::Override);
        QVERIFY(r2.isOverride());
        QVERIFY(!r2.isInherited());

        const ResolvedInfil r3 = resolveInfil(m, 3);   // untagged -> '*'
        QCOMPARE(r3.row.method, InfilMethod::Constant);
        QCOMPARE(r3.provenance, InfilProvenance::Star);
    }

    /*! A mesh with no infiltration data at all resolves to None everywhere —
     *  the state every existing project is in. */
    void resolve_unconfiguredMeshIsNoneEverywhere()
    {
        const MeshResult m = makeTaggedMesh();
        for (int t = 0; t < m.triangles.size(); ++t) {
            const ResolvedInfil r = resolveInfil(m, t);
            QCOMPARE(r.provenance, InfilProvenance::None);
            QVERIFY(r.row.isNone());
        }
    }

    /*! Per-method parameter masking — what the attribute table greys out. */
    void masks_matchTheDocumentedPositionalLayout()
    {
        QCOMPARE(infilParamCount(InfilMethod::Horton),       5);
        QCOMPARE(infilParamCount(InfilMethod::ModHorton),    5);
        QCOMPARE(infilParamCount(InfilMethod::GreenAmpt),    3);
        QCOMPARE(infilParamCount(InfilMethod::ModGreenAmpt), 3);
        QCOMPARE(infilParamCount(InfilMethod::Constant),     1);

        // Curve Number's middle column is a documented no-op.
        QVERIFY(infilUsesParam(InfilMethod::CurveNumber, 0));
        QVERIFY(!infilUsesParam(InfilMethod::CurveNumber, 1));
        QVERIFY(infilUsesParam(InfilMethod::CurveNumber, 2));

        QVERIFY(infilUsesParam(InfilMethod::Constant, 0));
        for (int s = 1; s < kInfilMaxParams; ++s)
            QVERIFY(!infilUsesParam(InfilMethod::Constant, s));

        for (int s = 0; s < kInfilMaxParams; ++s)
            QVERIFY(!infilUsesParam(InfilMethod::None, s));
    }

    /*! The tokens must be byte-identical to the engine's, or a saved model
     *  will not reload. */
    void tokens_matchTheEngineSpelling()
    {
        QCOMPARE(infilMethodToken(InfilMethod::Horton),       QStringLiteral("HORTON"));
        QCOMPARE(infilMethodToken(InfilMethod::ModHorton),    QStringLiteral("MODIFIED_HORTON"));
        QCOMPARE(infilMethodToken(InfilMethod::GreenAmpt),    QStringLiteral("GREEN_AMPT"));
        QCOMPARE(infilMethodToken(InfilMethod::ModGreenAmpt), QStringLiteral("MODIFIED_GREEN_AMPT"));
        QCOMPARE(infilMethodToken(InfilMethod::CurveNumber),  QStringLiteral("CURVE_NUMBER"));
        QCOMPARE(infilMethodToken(InfilMethod::Constant),     QStringLiteral("CONSTANT"));
        QCOMPARE(infilMethodToken(InfilMethod::None),         QStringLiteral("NONE"));

        for (auto m : {InfilMethod::Horton, InfilMethod::ModHorton,
                       InfilMethod::GreenAmpt, InfilMethod::ModGreenAmpt,
                       InfilMethod::CurveNumber, InfilMethod::Constant,
                       InfilMethod::None}) {
            bool ok = false;
            QCOMPARE(infilMethodFromToken(infilMethodToken(m), &ok), m);
            QVERIFY(ok);
        }
        bool ok = true;
        infilMethodFromToken(QStringLiteral("SPONGE"), &ok);
        QVERIFY(!ok);
    }

    /*! The stored-value encoding the enum column relies on: `enumLabels[i]`
     *  corresponds to `spec.min + i`, valid only while InfilMethod stays
     *  contiguous from None = -1. Nothing else checks this. */
    void enumEncoding_isContiguousFromNone()
    {
        QCOMPARE(int(InfilMethod::None),         -1);
        QCOMPARE(int(InfilMethod::Horton),        0);
        QCOMPARE(int(InfilMethod::ModHorton),     1);
        QCOMPARE(int(InfilMethod::GreenAmpt),     2);
        QCOMPARE(int(InfilMethod::ModGreenAmpt),  3);
        QCOMPARE(int(InfilMethod::CurveNumber),   4);
        QCOMPARE(int(InfilMethod::Constant),      5);
    }

    // -----------------------------------------------------------------
    // GG0a / G7 / G9 — round-trip and the silent-loss-on-save check.
    // -----------------------------------------------------------------

    /*! Author -> write -> read -> the same resolved model, for an INLINE mesh.
     *  Covers the '*' row, a tag row, a sparse override and INFIL_STEP. */
    void roundTrip_inlineMeshPreservesEveryInfilSection()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inp = dir.filePath(QStringLiteral("inline.inp"));

        const MeshResult src = makeConfiguredMesh();
        writeInlineModel(inp, src);

        const InpMeshReadResult back = InpMeshReader::read(inp);
        QVERIFY2(back.hasMesh, qPrintable(back.errorMsg));
        QCOMPARE(back.mesh.triangles.size(), 4);

        compareResolved(src, back.mesh);
        QCOMPARE(back.mesh.infilOptions.infilStep, 120.0);

        // Compactness (engine D-I3): a '*' row covering three cells must not
        // be exploded into three per-cell override rows.
        QCOMPARE(back.mesh.infilOverrides.size(), 1);
        QVERIFY(back.mesh.infilOverrides.contains(2));
    }

    /*! The G9 gate. patchAttributeSections() is what the project save path
     *  calls AFTER restoring the pre-engine-write snapshot. A section it does
     *  not re-emit is gone from the saved model — and gone again on the next
     *  save, which is why this runs the cycle TWICE. */
    void patchAttributeSections_survivesRepeatedSaves()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inp = dir.filePath(QStringLiteral("patched.inp"));

        // Start from a model with NO infiltration, exactly as an existing
        // project would be, then let the "GUI edit" introduce it.
        writeInlineModel(inp, makeTaggedMesh());

        const MeshResult edited = makeConfiguredMesh();
        for (int pass = 1; pass <= 2; ++pass) {
            QString err;
            QVERIFY2(InpMeshWriter::patchAttributeSections(inp, edited, &err),
                     qPrintable(QStringLiteral("pass %1: %2").arg(pass).arg(err)));

            const QString text = readAll(inp);
            QVERIFY2(text.contains(QStringLiteral("[2D_INFILTRATION_DEFAULTS]")),
                     qPrintable(QStringLiteral("pass %1 dropped "
                                "[2D_INFILTRATION_DEFAULTS] — it is missing "
                                "from the strip list or from "
                                "patchAttributeSections()").arg(pass)));
            QVERIFY2(text.contains(QStringLiteral("[2D_INFILTRATION]")),
                     qPrintable(QStringLiteral("pass %1 dropped "
                                "[2D_INFILTRATION]").arg(pass)));
            QVERIFY2(text.contains(QStringLiteral("[2D_INFILTRATION_OPTIONS]")),
                     qPrintable(QStringLiteral("pass %1 dropped "
                                "[2D_INFILTRATION_OPTIONS]").arg(pass)));

            // A duplicated section is as broken as a dropped one: the engine
            // would append the rows twice.
            QCOMPARE(text.count(QStringLiteral("[2D_INFILTRATION_DEFAULTS]")), 1);
            QCOMPARE(text.count(QStringLiteral("[2D_INFILTRATION]\n")), 1);

            const InpMeshReadResult back = InpMeshReader::read(inp);
            QVERIFY2(back.hasMesh, qPrintable(back.errorMsg));
            compareResolved(edited, back.mesh);
            QCOMPARE(back.mesh.infilOptions.infilStep, 120.0);
        }
    }

    /*! An external `.2dm` carries the sections with the mesh (§3.2a): they
     *  must land in exactly ONE file, never be copied into the `.inp` too. */
    void roundTrip_externalMeshKeepsSectionsInTheSidecarOnly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString inp  = dir.filePath(QStringLiteral("ext.inp"));
        const QString mesh = dir.filePath(QStringLiteral("ext.2dm"));

        const MeshResult src = makeConfiguredMesh();
        // Sidecar carries the mesh + infiltration; the .inp only references it.
        writeText(mesh, InpMeshWriter::buildSectionText(src, CouplingMap{}));
        writeText(inp,
                  QStringLiteral("[OPTIONS]\nFLOW_UNITS CMS\n\n"
                                 "[2D_MESH_FILE]\nFILE %1\n")
                      .arg(QFileInfo(mesh).fileName()));

        const InpMeshReadResult back = InpMeshReader::read(inp);
        QVERIFY2(back.hasMesh, qPrintable(back.errorMsg));
        QVERIFY(back.isExternal);
        compareResolved(src, back.mesh);

        // The .inp must not have acquired a copy.
        const QString inpText = readAll(inp);
        QVERIFY2(!inpText.contains(QStringLiteral("[2D_INFILTRATION")),
                 "the .inp carries a copy of the infiltration sections that "
                 "the external .2dm also carries — two sources of truth");
    }

    // -----------------------------------------------------------------
    // GG0c / G8 — undo must restore INHERITANCE, not a materialized copy.
    // -----------------------------------------------------------------
    //
    // No MapCanvas here means no undo stack, so (as the vertex/edge command
    // tests do) these drive the command objects directly. That is also the
    // sharper test: it pins down exactly what the command's own undo() does,
    // independent of how the push helper assembles it.

    /*! The subtlest correctness point in GG0. Assign to a cell that currently
     *  INHERITS from its LAWN tag, then undo. The cell must go back to
     *  inheriting — `infilOverrides` must not contain it — and a later edit to
     *  the tag default must still reach it. A cell that comes back carrying an
     *  override with identical numbers passes every visual check and is wrong. */
    void undo_restoresInheritanceNotAMaterializedCopy()
    {
        SWMM2DMeshLayer layer(makeConfiguredMesh(), QString());

        // Triangle 0 inherits from LAWN before the edit.
        QVERIFY(!layer.mesh().infilOverrides.contains(0));
        const ResolvedInfil pre = resolveInfil(layer.mesh(), 0);
        QCOMPARE(pre.provenance, InfilProvenance::Tag);

        MeshSetTriangleInfilCommand cmd(
            &layer, {0}, row(InfilMethod::Constant, 2.5),
            {pre.row}, {pre.provenance}, QStringLiteral("infil"), nullptr);

        cmd.redo();
        QVERIFY(layer.mesh().infilOverrides.contains(0));
        QCOMPARE(resolveInfil(layer.mesh(), 0).provenance, InfilProvenance::Override);

        cmd.undo();
        QVERIFY2(!layer.mesh().infilOverrides.contains(0),
                 "undo left triangle 0 carrying an override. Its numbers may "
                 "match the LAWN default exactly, but the cell has stopped "
                 "tracking its region — GUI plan §3.5(3).");
        QCOMPARE(resolveInfil(layer.mesh(), 0).provenance, InfilProvenance::Tag);

        // The proof that inheritance is live again: change the tag default and
        // watch it reach the cell.
        QVERIFY(layer.applyMeshInfilDefault(QStringLiteral("LAWN"),
                                            row(InfilMethod::GreenAmpt, 3.5, 0.5, 0.3)));
        const ResolvedInfil after = resolveInfil(layer.mesh(), 0);
        QCOMPARE(after.row.method, InfilMethod::GreenAmpt);
        QCOMPARE(after.provenance, InfilProvenance::Tag);
    }

    /*! Undoing an assignment over a cell that ALREADY had an override must
     *  restore that override, not erase it. The mirror of the case above. */
    void undo_restoresAPreExistingOverride()
    {
        SWMM2DMeshLayer layer(makeConfiguredMesh(), QString());

        const InfilRow before = layer.mesh().infilOverrides.value(2);
        QCOMPARE(before.method, InfilMethod::CurveNumber);
        const ResolvedInfil pre = resolveInfil(layer.mesh(), 2);
        QCOMPARE(pre.provenance, InfilProvenance::Override);

        MeshSetTriangleInfilCommand cmd(
            &layer, {2}, row(InfilMethod::Constant, 9.0),
            {pre.row}, {pre.provenance}, QStringLiteral("infil"), nullptr);
        cmd.redo();
        cmd.undo();

        QVERIFY2(layer.mesh().infilOverrides.contains(2),
                 "undo erased a pre-existing per-cell override");
        QVERIFY(sameRow(layer.mesh().infilOverrides.value(2), before));
    }

    /*! A multi-cell assignment must put EVERY cell back on its own former
     *  footing — the three inheriting cells inherit again, the overridden one
     *  keeps its old row. One command, four different restore paths. */
    void undo_multiCellAssignmentRestoresEachCellsOwnState()
    {
        SWMM2DMeshLayer layer(makeConfiguredMesh(), QString());
        const InfilRow before2 = layer.mesh().infilOverrides.value(2);

        QVector<int> tris{0, 1, 2, 3};
        QVector<InfilRow>        oldRows;
        QVector<InfilProvenance> oldProv;
        for (int t : tris) {
            const ResolvedInfil r = resolveInfil(layer.mesh(), t);
            oldRows.append(r.row);
            oldProv.append(r.provenance);
        }
        // Exactly one of the four was an override before the edit — otherwise
        // this would not be testing the mixed case it claims to.
        QCOMPARE(oldProv.count(InfilProvenance::Override), 1);

        MeshSetTriangleInfilCommand cmd(
            &layer, tris, row(InfilMethod::Constant, 4.0), oldRows, oldProv,
            QStringLiteral("infil"), nullptr);

        cmd.redo();
        QCOMPARE(layer.mesh().infilOverrides.size(), 4);

        cmd.undo();
        QCOMPARE(layer.mesh().infilOverrides.size(), 1);
        QVERIFY2(!layer.mesh().infilOverrides.contains(0), "cell 0 not restored to inheriting");
        QVERIFY2(!layer.mesh().infilOverrides.contains(1), "cell 1 not restored to inheriting");
        QVERIFY2(!layer.mesh().infilOverrides.contains(3), "cell 3 not restored to inheriting");
        QVERIFY(layer.mesh().infilOverrides.contains(2));
        QVERIFY(sameRow(layer.mesh().infilOverrides.value(2), before2));
    }

    /*! Command 50's half of the same rule: a tag that had NO row must come back
     *  with no row. Writing the resolved numbers back would leave a row that
     *  shadows the '*' fallback, so a later '*' edit would stop reaching the
     *  region — invisible in every UI. */
    void undo_defaultsRestoresAbsentTagsAsAbsent()
    {
        SWMM2DMeshLayer layer(makeConfiguredMesh(), QString());
        QCOMPARE(indexOfDefault(layer.mesh(), QStringLiteral("WOODS")), -1);
        QCOMPARE(resolveInfil(layer.mesh(), 1).provenance, InfilProvenance::Star);

        MeshSetInfilDefaultsCommand cmd(
            &layer, {QStringLiteral("WOODS")},
            {row(InfilMethod::Constant, 2.0)},
            {InfilRow{}}, {false},
            QStringLiteral("defaults"), nullptr);

        cmd.redo();
        QVERIFY(indexOfDefault(layer.mesh(), QStringLiteral("WOODS")) >= 0);
        QCOMPARE(resolveInfil(layer.mesh(), 1).provenance, InfilProvenance::Tag);

        cmd.undo();
        QVERIFY2(indexOfDefault(layer.mesh(), QStringLiteral("WOODS")) < 0,
                 "undo left a WOODS default row behind; the tag had none before "
                 "the edit, and the row now shadows the '*' fallback");
        QCOMPARE(resolveInfil(layer.mesh(), 1).provenance, InfilProvenance::Star);

        // And the proof it matters: a later '*' edit must still reach WOODS.
        QVERIFY(layer.applyMeshInfilDefault(QStringLiteral("*"),
                                            row(InfilMethod::Constant, 7.0)));
        const ResolvedInfil after = resolveInfil(layer.mesh(), 1);
        QCOMPARE(after.provenance, InfilProvenance::Star);
        QCOMPARE(after.row.p[0], 7.0);
    }

    /*! An existing tag row must be RESTORED, not erased — the other half of
     *  command 50's oldExisted distinction. */
    void undo_defaultsRestoresAnExistingTagRow()
    {
        SWMM2DMeshLayer layer(makeConfiguredMesh(), QString());
        const int idx = indexOfDefault(layer.mesh(), QStringLiteral("LAWN"));
        QVERIFY(idx >= 0);
        const InfilRow before = layer.mesh().infilDefaults[idx].row;

        MeshSetInfilDefaultsCommand cmd(
            &layer, {QStringLiteral("LAWN")},
            {row(InfilMethod::Constant, 3.0)},
            {before}, {true},
            QStringLiteral("defaults"), nullptr);

        cmd.redo();
        cmd.undo();

        const int back = indexOfDefault(layer.mesh(), QStringLiteral("LAWN"));
        QVERIFY2(back >= 0, "undo erased a LAWN default row that existed before");
        QVERIFY(sameRow(layer.mesh().infilDefaults[back].row, before));
        QCOMPARE(resolveInfil(layer.mesh(), 0).provenance, InfilProvenance::Tag);
    }

private:
    static void writeText(const QString &path, const QString &text)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(text.toUtf8());
    }

    static QString readAll(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        return QString::fromUtf8(f.readAll());
    }

    /*! A minimal .inp carrying the mesh (and its infiltration) inline. */
    static void writeInlineModel(const QString &path, const MeshResult &m)
    {
        writeText(path,
                  QStringLiteral("[OPTIONS]\nFLOW_UNITS CMS\n\n")
                      + InpMeshWriter::buildSectionText(m, CouplingMap{}));
    }

    /*! Every triangle must resolve to the same row AND the same provenance
     *  on both sides — comparing only the numbers would let a round-trip that
     *  materialized every inherited cell as an override pass. */
    static void compareResolved(const MeshResult &a, const MeshResult &b)
    {
        QCOMPARE(b.triangles.size(), a.triangles.size());
        for (int t = 0; t < a.triangles.size(); ++t) {
            const ResolvedInfil ra = resolveInfil(a, t);
            const ResolvedInfil rb = resolveInfil(b, t);
            QVERIFY2(sameRow(ra.row, rb.row),
                     qPrintable(QStringLiteral("triangle %1 resolved to a "
                                "different row after the round-trip").arg(t)));
            QVERIFY2(ra.provenance == rb.provenance,
                     qPrintable(QStringLiteral("triangle %1 changed provenance "
                                "%2 -> %3 across the round-trip — inheritance "
                                "was not preserved")
                                .arg(t).arg(int(ra.provenance)).arg(int(rb.provenance))));
        }
    }
};

QTEST_MAIN(TestMeshInfil)
#include "test_meshinfil.moc"
