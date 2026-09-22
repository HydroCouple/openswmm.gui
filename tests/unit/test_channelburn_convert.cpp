/*!
 * \file   test_channelburn_convert.cpp
 * \brief  The engine contract behind ConvertNodeTypeCommand — the burn's
 *         junction-to-coupled-outfall conversion, phase P4
 *         (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §6).
 *
 * Scope note, the same one tests/unit/test_junction_split_layer.cpp carries:
 * SWMMModelLayer cannot be linked headlessly (nanoflann, GDAL, the whole Qt
 * Widgets scene graph), so this drives the engine calls the command makes, in
 * the same order and with the same arguments:
 *
 *   ctor  → swmm_node_get_type / _invert_elev / _max_depth / _tag   (snapshot)
 *   redo  → swmm_node_convert(OUTFALL)
 *           swmm_node_set_invert_elev / _max_depth
 *           swmm_node_set_outfall_type(NORMAL) / _flap_gate(0)
 *           swmm_node_set_tag("burn:outfall")
 *   undo  → swmm_node_convert(JUNCTION) + restore invert / depth / tag
 *
 * The load-bearing assertions are that the round trip is EXACT for a junction
 * (which is the burn's only conversion), and that the generated outfall is
 * UNGATED — a `Gated YES` flap gate blocks the 2D tailwater override
 * (2D_INPUT_FORMAT_SPEC §4.4), silently disabling the coupling the conversion
 * exists to create.
 */
#include <gtest/gtest.h>

#include <QDir>
#include <QString>

#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>

#include <cmath>

namespace {

#ifndef SWMMVIS_BURNCONV_FIXTURE_DIR
#  define SWMMVIS_BURNCONV_FIXTURE_DIR "."
#endif
#ifndef SWMMVIS_BURNCONV_OUTPUT_DIR
#  define SWMMVIS_BURNCONV_OUTPUT_DIR "."
#endif

constexpr int kJunction     = SWMM_NODE_JUNCTION;
constexpr int kOutfall      = SWMM_NODE_OUTFALL;
constexpr int kOutfallNormal = 1;   // 0=FREE 1=NORMAL 2=FIXED 3=TIDAL 4=TIMESERIES

QString fixturePath(const char *name)
{
    return QDir(QStringLiteral(SWMMVIS_BURNCONV_FIXTURE_DIR)).filePath(
        QString::fromLatin1(name));
}

QString outputPath(const char *name)
{
    QDir dir(QStringLiteral(SWMMVIS_BURNCONV_OUTPUT_DIR));
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QString::fromLatin1(name));
}

/*! A snapshot of what the command records in its constructor. */
struct NodeSnapshot
{
    int     type = -1;
    double  invert = 0.0;
    double  maxDepth = 0.0;
    QString tag;
};

NodeSnapshot snapshot(SWMM_Engine eng, int idx)
{
    NodeSnapshot s;
    swmm_node_get_type(eng, idx, &s.type);
    swmm_node_get_invert_elev(eng, idx, &s.invert);
    swmm_node_get_max_depth(eng, idx, &s.maxDepth);
    char buf[256] = {0};
    if (swmm_node_get_tag(eng, idx, buf, int(sizeof(buf))) == SWMM_OK)
        s.tag = QString::fromUtf8(buf);
    return s;
}

/*! An engine on the shared fixture, plus the index of a junction in it. */
class BurnConvertTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        eng = swmm_engine_create();
        ASSERT_NE(eng, nullptr);
        // OPENED, deliberately not initialized: swmm_node_convert is
        // BUILDING/OPENED-only.
        const QString inp = fixturePath("street_inlet.inp");
        const QString rpt = outputPath("channelburn_convert.rpt");
        const QString out = outputPath("channelburn_convert.out");
        ASSERT_EQ(swmm_engine_open(eng, inp.toUtf8().constData(),
                                   rpt.toUtf8().constData(),
                                   out.toUtf8().constData(), nullptr), SWMM_OK)
            << inp.toStdString();

        // First junction in the deck — the burn converts junctions.
        const int n = swmm_node_count(eng);
        ASSERT_GT(n, 0);
        for (int i = 0; i < n; ++i)
        {
            int t = -1;
            swmm_node_get_type(eng, i, &t);
            if (t == kJunction) { idx = i; break; }
        }
        ASSERT_GE(idx, 0) << "fixture has no junction";
    }
    void TearDown() override
    {
        if (eng) { swmm_engine_destroy(eng); eng = nullptr; }
    }

    SWMM_Engine eng = nullptr;
    int idx = -1;
};

} // namespace

TEST_F(BurnConvertTest, JunctionToCoupledOutfallRoundTripsExactly)
{
    const NodeSnapshot before = snapshot(eng, idx);
    ASSERT_EQ(before.type, kJunction);

    // redo(): convert, then apply the burn's outfall spec.
    SWMM_ConversionResult res{};
    ASSERT_EQ(swmm_node_convert(eng, idx, kOutfall, &res), SWMM_OK);
    swmm_conversion_result_free(&res);

    const double burnedInvert = before.invert - 1.25;    // the channel bottom
    const double burnedDepth  = 3.5;
    ASSERT_EQ(swmm_node_set_invert_elev(eng, idx, burnedInvert), SWMM_OK);
    ASSERT_EQ(swmm_node_set_max_depth(eng, idx, burnedDepth), SWMM_OK);
    ASSERT_EQ(swmm_node_set_outfall_type(eng, idx, kOutfallNormal), SWMM_OK);
    ASSERT_EQ(swmm_node_set_outfall_flap_gate(eng, idx, 0), SWMM_OK);
    ASSERT_EQ(swmm_node_set_tag(eng, idx, "burn:outfall"), SWMM_OK);

    const NodeSnapshot burned = snapshot(eng, idx);
    EXPECT_EQ(burned.type, kOutfall);
    EXPECT_DOUBLE_EQ(burned.invert, burnedInvert);
    EXPECT_DOUBLE_EQ(burned.maxDepth, burnedDepth);
    EXPECT_EQ(burned.tag, QStringLiteral("burn:outfall"));

    // undo(): back to the snapshotted type and common properties.
    SWMM_ConversionResult back{};
    ASSERT_EQ(swmm_node_convert(eng, idx, before.type, &back), SWMM_OK);
    swmm_conversion_result_free(&back);
    ASSERT_EQ(swmm_node_set_invert_elev(eng, idx, before.invert), SWMM_OK);
    ASSERT_EQ(swmm_node_set_max_depth(eng, idx, before.maxDepth), SWMM_OK);
    ASSERT_EQ(swmm_node_set_tag(eng, idx, before.tag.toUtf8().constData()), SWMM_OK);

    const NodeSnapshot after = snapshot(eng, idx);
    EXPECT_EQ(after.type, before.type);
    EXPECT_DOUBLE_EQ(after.invert, before.invert);
    EXPECT_DOUBLE_EQ(after.maxDepth, before.maxDepth);
    EXPECT_EQ(after.tag, before.tag);
}

TEST_F(BurnConvertTest, TheGeneratedOutfallIsUngated)
{
    // A gated outfall blocks the dynamic tailwater override, which would leave
    // the coupling in place but inert — the failure this asserts against.
    SWMM_ConversionResult res{};
    ASSERT_EQ(swmm_node_convert(eng, idx, kOutfall, &res), SWMM_OK);
    swmm_conversion_result_free(&res);

    ASSERT_EQ(swmm_node_set_outfall_flap_gate(eng, idx, 0), SWMM_OK);
    int gated = -1;
    ASSERT_EQ(swmm_node_get_outfall_flap_gate(eng, idx, &gated), SWMM_OK);
    EXPECT_EQ(gated, 0);

    int type = -1;
    ASSERT_EQ(swmm_node_set_outfall_type(eng, idx, kOutfallNormal), SWMM_OK);
    ASSERT_EQ(swmm_node_get_outfall_type(eng, idx, &type), SWMM_OK);
    EXPECT_EQ(type, kOutfallNormal);
}

TEST_F(BurnConvertTest, ConversionPreservesTheCoordinatesTheCouplingNeeds)
{
    // The coupling maps the node to a mesh vertex by position, so a conversion
    // that moved the node would silently couple the wrong cell.
    double x0 = 0.0, y0 = 0.0;
    ASSERT_EQ(swmm_spatial_get_node_coord(eng, idx, &x0, &y0), SWMM_OK);

    SWMM_ConversionResult res{};
    ASSERT_EQ(swmm_node_convert(eng, idx, kOutfall, &res), SWMM_OK);
    swmm_conversion_result_free(&res);

    double x1 = 0.0, y1 = 0.0;
    ASSERT_EQ(swmm_spatial_get_node_coord(eng, idx, &x1, &y1), SWMM_OK);
    EXPECT_DOUBLE_EQ(x1, x0);
    EXPECT_DOUBLE_EQ(y1, y0);
}

TEST_F(BurnConvertTest, ConvertingToTheSameTypeIsRefusedSoTheCommandMustSkipIt)
{
    // swmm_node_convert returns BADPARAM when the type is unchanged, which is
    // why ConvertNodeTypeCommand short-circuits that case instead of calling.
    SWMM_ConversionResult res{};
    EXPECT_NE(swmm_node_convert(eng, idx, kJunction, &res), SWMM_OK);
    swmm_conversion_result_free(&res);

    int t = -1;
    swmm_node_get_type(eng, idx, &t);
    EXPECT_EQ(t, kJunction);
}
