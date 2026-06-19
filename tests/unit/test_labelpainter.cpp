/*!
 * \file   test_labelpainter.cpp
 * \brief  Slice US.B3 — unit coverage for the shared LabelPainter's pure
 *         logic: scale-window gating + placement offsets. drawLabel itself is
 *         exercised by the GUI paint paths.
 */
#include <gtest/gtest.h>

#include "render/labelconfig.h"
#include "render/labelpainter.h"

using OpenSWMM::Render::LabelConfig;
using OpenSWMM::Render::LabelPainter;

// ── scaleVisible ────────────────────────────────────────────────────────

TEST(LabelPainter, ScaleVisibleUnboundedByDefault)
{
    LabelConfig cfg;                       // minScale = maxScale = 0
    EXPECT_TRUE(LabelPainter::scaleVisible(cfg, 1000.0));
    EXPECT_TRUE(LabelPainter::scaleVisible(cfg, 1.0));
}

TEST(LabelPainter, ScaleVisibleHidesWhenZoomedOutPastMinScale)
{
    LabelConfig cfg;
    cfg.minScale = 5000.0;                  // hide further out than 1:5000
    EXPECT_TRUE(LabelPainter::scaleVisible(cfg, 4000.0));   // zoomed in → visible
    EXPECT_FALSE(LabelPainter::scaleVisible(cfg, 6000.0));  // zoomed out → hidden
}

TEST(LabelPainter, ScaleVisibleHidesWhenZoomedInPastMaxScale)
{
    LabelConfig cfg;
    cfg.maxScale = 500.0;                   // hide further in than 1:500
    EXPECT_TRUE(LabelPainter::scaleVisible(cfg, 800.0));    // zoomed out → visible
    EXPECT_FALSE(LabelPainter::scaleVisible(cfg, 200.0));   // zoomed in → hidden
}

TEST(LabelPainter, ScaleVisibleWindowBothBounds)
{
    LabelConfig cfg;
    cfg.minScale = 5000.0;
    cfg.maxScale = 500.0;
    EXPECT_TRUE(LabelPainter::scaleVisible(cfg, 1000.0));   // inside window
    EXPECT_FALSE(LabelPainter::scaleVisible(cfg, 6000.0));  // too far out
    EXPECT_FALSE(LabelPainter::scaleVisible(cfg, 200.0));   // too far in
}

TEST(LabelPainter, ScaleVisibleNonPositiveDenominatorDisablesGating)
{
    LabelConfig cfg;
    cfg.minScale = 5000.0;
    cfg.maxScale = 500.0;
    EXPECT_TRUE(LabelPainter::scaleVisible(cfg, 0.0));
    EXPECT_TRUE(LabelPainter::scaleVisible(cfg, -1.0));
}

// ── placementOffset ─────────────────────────────────────────────────────

TEST(LabelPainter, PlacementOffsetDirections)
{
    LabelConfig cfg;
    const QSizeF sz(40.0, 10.0);

    cfg.placement = LabelConfig::Above;
    EXPECT_LT(LabelPainter::placementOffset(cfg, sz).y(), 0.0);   // moves up

    cfg.placement = LabelConfig::Below;
    EXPECT_GT(LabelPainter::placementOffset(cfg, sz).y(), 0.0);   // moves down

    cfg.placement = LabelConfig::Left;
    EXPECT_LT(LabelPainter::placementOffset(cfg, sz).x(), 0.0);   // moves left

    cfg.placement = LabelConfig::Right;
    EXPECT_GT(LabelPainter::placementOffset(cfg, sz).x(), 0.0);   // moves right

    cfg.placement = LabelConfig::Centre;
    const QPointF c = LabelPainter::placementOffset(cfg, sz);
    EXPECT_DOUBLE_EQ(c.x(), -20.0);
    EXPECT_DOUBLE_EQ(c.y(), -5.0);
}

TEST(LabelPainter, LabelRectAnchoredByPlacement)
{
    LabelConfig cfg;
    cfg.placement = LabelConfig::Centre;
    const QSizeF sz(40.0, 10.0);
    const QRectF r = LabelPainter::labelRect(cfg, QPointF(100.0, 100.0), sz);
    // Centre placement puts the box centred on the anchor.
    EXPECT_DOUBLE_EQ(r.center().x(), 100.0);
    EXPECT_DOUBLE_EQ(r.center().y(), 100.0);
    EXPECT_DOUBLE_EQ(r.width(), 40.0);
    EXPECT_DOUBLE_EQ(r.height(), 10.0);
}
