/*!
 * \file   test_inlet_junction_layer.cpp
 * \brief  Engine-level contract behind the GUI's inlet-junction commands
 *         (INLET_EDITOR_AND_INLET_JUNCTION_GUI_PLAN_2026-09-05.md, phase G5).
 *
 * Scope note: SWMMModelLayer cannot be linked headlessly (nanoflann, GDAL, the
 * whole Qt Widgets scene graph), so — like tests/gui/test_vjsourcesummary.cpp,
 * the closest precedent — this test exercises the engine calls the layer and
 * its MapCommands make, in the same order and with the same arguments:
 *
 *   applyInsertInletJunction  → swmm_conduit_split_inlet
 *   applyFuseInletJunction    → swmm_inlet_junction_fuse
 *   SetInletUsageCommand      → swmm_inlet_usage_find_node / _get / _set /
 *                               _remove   (snapshot → apply → restore)
 *
 * What that buys: if the engine's split/fuse stops being an exact inverse, or
 * the usage row stops round-tripping, the GUI's undo is broken and this test
 * says so — which is the assertion the phase actually needs.
 *
 * The fixture is a real .inp (tests/data/inlets/street_inlet.inp) and every
 * artefact is written to tests/output/inlets/ so it can be reviewed
 * (CLAUDE.md §4.1), never to a temp dir.
 */
#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>

namespace {

#ifndef SWMMVIS_INLET_FIXTURE_DIR
#  define SWMMVIS_INLET_FIXTURE_DIR "."
#endif
#ifndef SWMMVIS_INLET_OUTPUT_DIR
#  define SWMMVIS_INLET_OUTPUT_DIR "."
#endif

QString fixturePath(const char *name)
{
    return QDir(QStringLiteral(SWMMVIS_INLET_FIXTURE_DIR)).filePath(
        QString::fromLatin1(name));
}

QString outputPath(const char *name)
{
    QDir dir(QStringLiteral(SWMMVIS_INLET_OUTPUT_DIR));
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
 *  initialized: swmm_conduit_split_inlet and the usage setters are
 *  BUILDING/OPENED-only, and initialize() bakes the node-count invariant. */
SWMM_Engine openFixture()
{
    SWMM_Engine e = swmm_engine_create();
    if (!e) return nullptr;
    const QString inp = fixturePath("street_inlet.inp");
    const QString rpt = outputPath("street_inlet.rpt");
    const QString out = outputPath("street_inlet.out");
    if (swmm_engine_open(e, inp.toUtf8().constData(),
                         rpt.toUtf8().constData(),
                         out.toUtf8().constData(), nullptr) != SWMM_OK) {
        swmm_engine_destroy(e);
        return nullptr;
    }
    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// Insert on a street conduit → the node is an inlet junction with a usage row.
// This is what InsertInletJunctionCommand::redo() produces.
// ---------------------------------------------------------------------------
TEST(InletJunctionLayer, SplitStreetConduitMakesInletJunction)
{
    SWMM_Engine e = openFixture();
    ASSERT_NE(e, nullptr) << "fixture street_inlet.inp did not open";

    const int c1 = swmm_link_index(e, "C1");
    ASSERT_GE(c1, 0);

    int newNode = -1, newLink = -1;
    ASSERT_EQ(swmm_conduit_split_inlet(e, c1, 0.5, "IJ1", "C1_B",
                                       "GRATE1", "SEWER",
                                       &newNode, &newLink), SWMM_OK);
    ASSERT_GE(newNode, 0);
    ASSERT_GE(newLink, 0);

    // The node carries BOTH flags — every renderer and adapter switch in the
    // GUI depends on that (inlet is probed before virtual).
    int isInlet = 0, isVirtual = 0;
    ASSERT_EQ(swmm_node_is_inlet(e, newNode, &isInlet), SWMM_OK);
    ASSERT_EQ(swmm_node_is_virtual(e, newNode, &isVirtual), SWMM_OK);
    EXPECT_EQ(isInlet, 1);
    EXPECT_EQ(isVirtual, 1) << "an inlet junction must also be a virtual junction";

    // …and owns exactly one usage row naming the design and the capture node.
    const int row = swmm_inlet_usage_find_node(e, newNode);
    ASSERT_GE(row, 0) << "no [INLET_USAGE] row created for the inlet junction";
    SWMM_InletUsage u{};
    ASSERT_EQ(swmm_inlet_usage_get(e, row, &u), SWMM_OK);
    EXPECT_EQ(u.host_kind, SWMM_INLET_HOST_NODE);
    EXPECT_EQ(u.host_idx, newNode);
    EXPECT_EQ(u.design_idx, swmm_inlet_index(e, "GRATE1"));
    EXPECT_EQ(u.capture_node_idx, swmm_node_index(e, "SEWER"));
    EXPECT_EQ(u.num_inlets, 1);

    swmm_engine_destroy(e);
}

// ---------------------------------------------------------------------------
// Undo = fuse. InsertInletJunctionCommand::undo() relies on the split→fuse
// round trip being exact; assert it on the written .inp text.
// ---------------------------------------------------------------------------
TEST(InletJunctionLayer, FuseUndoesSplitByteIdentically)
{
    SWMM_Engine e = openFixture();
    ASSERT_NE(e, nullptr);

    const QString beforePath = outputPath("street_inlet_before.inp");
    ASSERT_EQ(swmm_model_write(e, beforePath.toUtf8().constData()), SWMM_OK);
    const QString before = readAll(beforePath);
    ASSERT_FALSE(before.isEmpty());

    const int c1 = swmm_link_index(e, "C1");
    ASSERT_GE(c1, 0);
    int newNode = -1, newLink = -1;
    ASSERT_EQ(swmm_conduit_split_inlet(e, c1, 0.5, "IJ1", "C1_B",
                                       "GRATE1", "SEWER",
                                       &newNode, &newLink), SWMM_OK);

    int surviving = -1;
    ASSERT_EQ(swmm_inlet_junction_fuse(e, newNode, &surviving), SWMM_OK);
    EXPECT_LT(swmm_node_index(e, "IJ1"), 0) << "the fuse must delete the node";
    EXPECT_LT(swmm_link_index(e, "C1_B"), 0) << "the fuse must retire the new conduit";

    const QString afterPath = outputPath("street_inlet_after_fuse.inp");
    ASSERT_EQ(swmm_model_write(e, afterPath.toUtf8().constData()), SWMM_OK);
    EXPECT_EQ(readAll(afterPath), before)
        << "split → fuse is not an exact inverse; undo of an inlet-junction "
           "insertion would not restore the model";

    swmm_engine_destroy(e);
}

// ---------------------------------------------------------------------------
// SetInletUsageCommand's undo/redo contract: snapshot the prior row (or its
// absence), apply the new one, restore the snapshot. Exercised here on a
// CONDUIT host, which is the §2.4 usage-page path.
// ---------------------------------------------------------------------------
TEST(InletJunctionLayer, UsageRowSetAndRestoreRoundTrips)
{
    SWMM_Engine e = openFixture();
    ASSERT_NE(e, nullptr);

    const int c1     = swmm_link_index(e, "C1");
    const int design = swmm_inlet_index(e, "GRATE1");
    const int sewer  = swmm_node_index(e, "SEWER");
    ASSERT_GE(c1, 0);
    ASSERT_GE(design, 0);
    ASSERT_GE(sewer, 0);

    // --- ctor: no prior row, so undo must REMOVE ---------------------------
    EXPECT_LT(swmm_inlet_usage_find_link(e, c1), 0);

    // --- redo: install the row ---------------------------------------------
    SWMM_InletUsage first{};
    first.host_kind        = SWMM_INLET_HOST_LINK;
    first.host_idx         = c1;
    first.design_idx       = design;
    first.capture_node_idx = sewer;
    first.num_inlets       = 2;
    first.pct_clogged      = 10.0;
    first.flow_limit       = 1.5;
    first.local_depress    = 0.25;
    first.local_width      = 3.0;
    first.placement        = SWMM_INLET_ON_SAG;
    int rowIdx = -1;
    ASSERT_EQ(swmm_inlet_usage_set(e, &first, &rowIdx), SWMM_OK);
    ASSERT_GE(rowIdx, 0);

    // --- a second edit snapshots the first ---------------------------------
    SWMM_InletUsage snapshot{};
    const int found = swmm_inlet_usage_find_link(e, c1);
    ASSERT_GE(found, 0);
    ASSERT_EQ(swmm_inlet_usage_get(e, found, &snapshot), SWMM_OK);

    SWMM_InletUsage second = first;
    second.num_inlets  = 4;
    second.pct_clogged = 40.0;
    second.placement   = SWMM_INLET_ON_GRADE;
    ASSERT_EQ(swmm_inlet_usage_set(e, &second, nullptr), SWMM_OK);

    SWMM_InletUsage now{};
    ASSERT_EQ(swmm_inlet_usage_get(e, swmm_inlet_usage_find_link(e, c1), &now),
              SWMM_OK);
    EXPECT_EQ(now.num_inlets, 4);
    EXPECT_DOUBLE_EQ(now.pct_clogged, 40.0);
    EXPECT_EQ(now.placement, SWMM_INLET_ON_GRADE);

    // --- undo of the second edit: restore the snapshot ---------------------
    ASSERT_EQ(swmm_inlet_usage_set(e, &snapshot, nullptr), SWMM_OK);
    ASSERT_EQ(swmm_inlet_usage_get(e, swmm_inlet_usage_find_link(e, c1), &now),
              SWMM_OK);
    EXPECT_EQ(now.num_inlets, 2);
    EXPECT_DOUBLE_EQ(now.pct_clogged, 10.0);
    EXPECT_DOUBLE_EQ(now.flow_limit, 1.5);
    EXPECT_DOUBLE_EQ(now.local_depress, 0.25);
    EXPECT_DOUBLE_EQ(now.local_width, 3.0);
    EXPECT_EQ(now.placement, SWMM_INLET_ON_SAG);
    EXPECT_EQ(now.design_idx, design);
    EXPECT_EQ(now.capture_node_idx, sewer);

    // --- undo of the FIRST edit: there was no row, so remove ---------------
    ASSERT_EQ(swmm_inlet_usage_remove(e, swmm_inlet_usage_find_link(e, c1)),
              SWMM_OK);
    EXPECT_LT(swmm_inlet_usage_find_link(e, c1), 0);

    swmm_engine_destroy(e);
}

// ---------------------------------------------------------------------------
// A capture node that is itself virtual is refused (rule 627) — the GUI's
// capture-node pickers filter on exactly this, so the filter must match the
// engine's rule rather than merely look tidy.
// ---------------------------------------------------------------------------
TEST(InletJunctionLayer, VirtualCaptureNodeIsRefused)
{
    SWMM_Engine e = openFixture();
    ASSERT_NE(e, nullptr);

    const int c1 = swmm_link_index(e, "C1");
    ASSERT_GE(c1, 0);
    int newNode = -1, newLink = -1;
    ASSERT_EQ(swmm_conduit_split_inlet(e, c1, 0.5, "IJ1", "C1_B",
                                       "GRATE1", "SEWER",
                                       &newNode, &newLink), SWMM_OK);

    // Point a conduit-hosted inlet at the inlet junction just created.
    SWMM_InletUsage bad{};
    bad.host_kind        = SWMM_INLET_HOST_LINK;
    bad.host_idx         = swmm_link_index(e, "C1_B");
    bad.design_idx       = swmm_inlet_index(e, "GRATE1");
    bad.capture_node_idx = newNode;
    bad.num_inlets       = 1;
    EXPECT_NE(swmm_inlet_usage_set(e, &bad, nullptr), SWMM_OK)
        << "a virtual / inlet junction must not be accepted as a capture node";

    swmm_engine_destroy(e);
}
