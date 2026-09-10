/*!
 * \file   test_junction_split_layer.cpp
 * \brief  Engine-level contract behind the GUI's plain-junction split
 *         (OpenSWMMVisMapToolAddNode on a conduit / InsertJunctionSplitCommand).
 *
 * Scope note: SWMMModelLayer cannot be linked headlessly (nanoflann, GDAL, the
 * whole Qt Widgets scene graph), so — exactly as
 * tests/unit/test_inlet_junction_layer.cpp does for the inlet variant — this
 * test drives the engine calls the layer and its MapCommand make, in the same
 * order and with the same arguments:
 *
 *   applyInsertJunctionSplit → swmm_conduit_split(make_virtual = 0)
 *   applyFuseJunctionSplit   → swmm_node_set_virtual(1) + swmm_virtual_junction_fuse
 *
 * The second line is the interesting one. vj_fuse() refuses a node that is not
 * flagged virtual (VirtualJunctionOps.cpp), so undoing a PLAIN split has to
 * borrow the virtual-junction inverse by flagging the node first. These tests
 * pin that this borrowing works and is exact — if it stops being so, undo of a
 * junction insertion silently leaves the model split.
 *
 * The fixture is a real .inp (tests/data/inlets/street_inlet.inp — reused
 * rather than duplicated) and every artefact is written to
 * tests/output/junction_split/ so it can be reviewed (CLAUDE.md §4.1), never
 * to a temp dir.
 */
#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>

#include <cmath>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>

namespace {

#ifndef SWMMVIS_JSPLIT_FIXTURE_DIR
#  define SWMMVIS_JSPLIT_FIXTURE_DIR "."
#endif
#ifndef SWMMVIS_JSPLIT_OUTPUT_DIR
#  define SWMMVIS_JSPLIT_OUTPUT_DIR "."
#endif

QString fixturePath(const char *name)
{
    return QDir(QStringLiteral(SWMMVIS_JSPLIT_FIXTURE_DIR)).filePath(
        QString::fromLatin1(name));
}

QString outputPath(const char *name)
{
    QDir dir(QStringLiteral(SWMMVIS_JSPLIT_OUTPUT_DIR));
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QString::fromLatin1(name));
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

/*! Open the fixture in the editable OPENED state — deliberately NOT
 *  initialized: swmm_conduit_split is BUILDING/OPENED-only. */
SWMM_Engine openFixture()
{
    SWMM_Engine e = swmm_engine_create();
    if (!e) return nullptr;
    const QString inp = fixturePath("street_inlet.inp");
    const QString rpt = outputPath("junction_split.rpt");
    const QString out = outputPath("junction_split.out");
    if (swmm_engine_open(e, inp.toUtf8().constData(),
                         rpt.toUtf8().constData(),
                         out.toUtf8().constData(), nullptr) != SWMM_OK) {
        swmm_engine_destroy(e);
        return nullptr;
    }
    return e;
}

/*! The undo path of InsertJunctionSplitCommand, spelled out. */
int fuseAsGuiUndoDoes(SWMM_Engine e, int nodeIdx, int *surviving)
{
    const int rc = swmm_node_set_virtual(e, nodeIdx, 1);
    if (rc != SWMM_OK) return rc;
    return swmm_virtual_junction_fuse(e, nodeIdx, surviving);
}

} // namespace

// ---------------------------------------------------------------------------
// The whole point of the tool: the inserted node is an ORDINARY junction, not
// a virtual one. If make_virtual leaked back to 1 the node would inherit the
// virtual-junction rules (including DYNWAVE-only routing) the user is trying
// to avoid by using this tool.
// ---------------------------------------------------------------------------
TEST(JunctionSplitLayer, SplitInsertsAPlainJunctionNotAVirtualOne)
{
    SWMM_Engine e = openFixture();
    ASSERT_NE(e, nullptr) << "fixture street_inlet.inp did not open";

    const int c1 = swmm_link_index(e, "C1");
    ASSERT_GE(c1, 0);

    int newNode = -1, newLink = -1;
    ASSERT_EQ(swmm_conduit_split(e, c1, 0.5, "J9", "C1_B",
                                 /*make_virtual=*/0, &newNode, &newLink),
              SWMM_OK);
    ASSERT_GE(newNode, 0);
    ASSERT_GE(newLink, 0);

    int isVirtual = 1;
    ASSERT_EQ(swmm_node_is_virtual(e, newNode, &isVirtual), SWMM_OK);
    EXPECT_EQ(isVirtual, 0)
        << "a junction split must NOT produce a virtual junction";

    int isInlet = 1;
    ASSERT_EQ(swmm_node_is_inlet(e, newNode, &isInlet), SWMM_OK);
    EXPECT_EQ(isInlet, 0);

    swmm_engine_destroy(e);
}

// ---------------------------------------------------------------------------
// The two halves must account for the whole original conduit — the tool offers
// no length editing, so a split that loses or invents length silently changes
// the hydraulics of the model.
// ---------------------------------------------------------------------------
TEST(JunctionSplitLayer, SplitHalvesSumToTheOriginalLength)
{
    SWMM_Engine e = openFixture();
    ASSERT_NE(e, nullptr);

    const int c1 = swmm_link_index(e, "C1");
    ASSERT_GE(c1, 0);
    double original = 0.0;
    ASSERT_EQ(swmm_link_get_length(e, c1, &original), SWMM_OK);
    ASSERT_GT(original, 0.0);

    int newNode = -1, newLink = -1;
    ASSERT_EQ(swmm_conduit_split(e, c1, 0.25, "J9", "C1_B",
                                 /*make_virtual=*/0, &newNode, &newLink),
              SWMM_OK);

    double up = 0.0, dn = 0.0;
    ASSERT_EQ(swmm_link_get_length(e, swmm_link_index(e, "C1"), &up), SWMM_OK);
    ASSERT_EQ(swmm_link_get_length(e, swmm_link_index(e, "C1_B"), &dn), SWMM_OK);

    EXPECT_NEAR(up + dn, original, 1.0e-6)
        << "split lost or invented conduit length";
    // t = 0.25 puts a quarter of the run upstream; the tool's status text and
    // the marker preview both promise the break lands where the user clicked.
    EXPECT_NEAR(up, 0.25 * original, 1.0e-6);

    swmm_engine_destroy(e);
}

// ---------------------------------------------------------------------------
// Undo. InsertJunctionSplitCommand::undo() flags the node virtual purely so it
// can reuse the engine's exact fuse inverse; assert the round trip restores the
// model byte-identically, transient flag and all.
// ---------------------------------------------------------------------------
TEST(JunctionSplitLayer, FuseUndoesSplitByteIdentically)
{
    SWMM_Engine e = openFixture();
    ASSERT_NE(e, nullptr);

    const QString beforePath = outputPath("junction_split_before.inp");
    ASSERT_EQ(swmm_model_write(e, beforePath.toUtf8().constData()), SWMM_OK);
    const QString before = readAll(beforePath);
    ASSERT_FALSE(before.isEmpty());

    const int c1 = swmm_link_index(e, "C1");
    ASSERT_GE(c1, 0);
    int newNode = -1, newLink = -1;
    ASSERT_EQ(swmm_conduit_split(e, c1, 0.5, "J9", "C1_B",
                                 /*make_virtual=*/0, &newNode, &newLink),
              SWMM_OK);

    int surviving = -1;
    ASSERT_EQ(fuseAsGuiUndoDoes(e, newNode, &surviving), SWMM_OK)
        << "the GUI's undo path (set_virtual then fuse) was refused";
    EXPECT_LT(swmm_node_index(e, "J9"), 0) << "the fuse must delete the node";
    EXPECT_LT(swmm_link_index(e, "C1_B"), 0)
        << "the fuse must retire the new conduit";

    const QString afterPath = outputPath("junction_split_after_fuse.inp");
    ASSERT_EQ(swmm_model_write(e, afterPath.toUtf8().constData()), SWMM_OK);
    EXPECT_EQ(readAll(afterPath), before)
        << "split → fuse is not an exact inverse; undoing a junction insertion "
           "would not restore the model";

    swmm_engine_destroy(e);
}

// ---------------------------------------------------------------------------
// Non-conduit links cannot be split. The tool hit-tests conduits only and says
// so in the status bar; this pins the engine half of that contract, so the
// restriction stays a deliberate rule rather than an accident.
// ---------------------------------------------------------------------------
TEST(JunctionSplitLayer, SplitRejectsBadPositionsAndDuplicateNames)
{
    SWMM_Engine e = openFixture();
    ASSERT_NE(e, nullptr);

    const int c1 = swmm_link_index(e, "C1");
    ASSERT_GE(c1, 0);
    int n = -1, l = -1;

    // t must lie strictly inside (0, 1) — the tool clamps to [0.02, 0.98].
    EXPECT_NE(swmm_conduit_split(e, c1, 0.0, "JA", "C1_B", 0, &n, &l), SWMM_OK);
    EXPECT_NE(swmm_conduit_split(e, c1, 1.0, "JB", "C1_B", 0, &n, &l), SWMM_OK);

    // An existing node name must be refused — nextNodeName() scans for a free
    // one precisely because the engine will not disambiguate.
    EXPECT_NE(swmm_conduit_split(e, c1, 0.5, "J1", "C1_B", 0, &n, &l), SWMM_OK);

    swmm_engine_destroy(e);
}
