/*!
 * \file   test_qsg2d_lodpolicy.cpp
 * \brief  Unit tests for Qsg2DLodPolicy (QSG-2D-1M Phase 3).
 *
 * Phase 3 acceptance contract:
 *   - 1M cells at full extent  -> Far; edges / vertices / labels off.
 *   - 1M cells at near zoom    -> Near; exact edges allowed.
 *   - selected cells at Far    -> selected overlay stays enabled.
 *   - LOD bucket is stable under tiny zoom jitter.
 *   - changing viewport size can change the bucket predictably.
 */

#include <gtest/gtest.h>

#include "render/qsg2dlodpolicy.h"

#include <cmath>

using OpenSWMM::Render::Qsg2DLodDecision;
using OpenSWMM::Render::Qsg2DLodInputs;
using OpenSWMM::Render::Qsg2DLodPolicy;

namespace {

/*! 1M-cell mesh whose bbox fills a 1600×900 viewport at full extent:
 *  10 km × 10 km domain, extent == domain. Mean cell area on screen is
 *  well below the Far threshold. */
Qsg2DLodInputs millionCellFullExtent()
{
    Qsg2DLodInputs in;
    in.viewportWidthPx  = 1600.0;
    in.viewportHeightPx = 900.0;
    in.extentWidth      = 10'000.0;
    in.extentHeight     = 10'000.0 * (900.0 / 1600.0);
    in.cellCount        = 1'000'000;
    in.meshBBoxArea     = 10'000.0 * 10'000.0;
    in.visibleFraction  = 1.0;
    in.wantFill          = true;
    in.wantEdges         = true;    // user wants them — LOD must veto
    in.wantVertexMarkers = true;
    in.wantContours      = true;
    in.wantContourLabels = true;
    in.wantVelocity      = true;
    return in;
}

/*! Same mesh zoomed to a 40 m × 22.5 m window — cells are huge on screen. */
Qsg2DLodInputs millionCellNearZoom()
{
    Qsg2DLodInputs in = millionCellFullExtent();
    in.extentWidth     = 40.0;
    in.extentHeight    = 22.5;
    in.visibleFraction = (40.0 * 22.5) / in.meshBBoxArea;
    return in;
}

} // namespace

// ── Bucket selection ───────────────────────────────────────────────────

TEST(Qsg2DLodPolicyTest, MillionCellsAtFullExtentIsFarWithDensePassesOff)
{
    const auto d = Qsg2DLodPolicy::decide(millionCellFullExtent());
    EXPECT_EQ(d.bucket, Qsg2DLodDecision::Far);
    EXPECT_TRUE(d.drawFill);
    EXPECT_TRUE(d.useAggregateFill);
    EXPECT_FALSE(d.drawEdges);
    EXPECT_FALSE(d.drawVertexMarkers);
    EXPECT_FALSE(d.drawContourLabels);
    // The velocity overlay is screen-space SAMPLED (bounded glyph count),
    // so it survives Far — only the dense per-cell mode is banned.
    EXPECT_TRUE(d.drawVelocityVectors);
    EXPECT_FALSE(d.denseVelocityAllowed);
    EXPECT_FALSE(d.exactContourBands);
    EXPECT_EQ(d.maxContourLabels, 0);
}

TEST(Qsg2DLodPolicyTest, MillionCellsNearZoomIsNearWithExactEdges)
{
    const auto d = Qsg2DLodPolicy::decide(millionCellNearZoom());
    EXPECT_EQ(d.bucket, Qsg2DLodDecision::Near);
    EXPECT_TRUE(d.drawFill);
    EXPECT_FALSE(d.useAggregateFill);
    EXPECT_TRUE(d.drawEdges);            // exact edges allowed at Near
    EXPECT_TRUE(d.drawContours);
    EXPECT_TRUE(d.exactContourBands);
    EXPECT_TRUE(d.drawVelocityVectors);
    EXPECT_TRUE(d.denseVelocityAllowed); // few visible cells -> under cap
}

TEST(Qsg2DLodPolicyTest, UserDisabledPassesStayOffAtNear)
{
    Qsg2DLodInputs in = millionCellNearZoom();
    in.wantEdges    = false;
    in.wantVelocity = false;
    const auto d = Qsg2DLodPolicy::decide(in);
    EXPECT_EQ(d.bucket, Qsg2DLodDecision::Near);
    EXPECT_FALSE(d.drawEdges);
    EXPECT_FALSE(d.drawVelocityVectors);
}

TEST(Qsg2DLodPolicyTest, SelectedOverlayStaysEnabledAtFar)
{
    Qsg2DLodInputs in = millionCellFullExtent();
    in.haveSelection = true;
    const auto d = Qsg2DLodPolicy::decide(in);
    EXPECT_EQ(d.bucket, Qsg2DLodDecision::Far);
    EXPECT_TRUE(d.drawSelectedOverlays);

    in.haveSelection = false;
    EXPECT_FALSE(Qsg2DLodPolicy::decide(in).drawSelectedOverlays);
}

// ── Stability / hysteresis ─────────────────────────────────────────────

TEST(Qsg2DLodPolicyTest, BucketStableUnderTinyZoomJitter)
{
    // Park the view roughly AT the Far/Mid threshold, then jitter the
    // extent by ±1%. With the previous decision fed back in, neither the
    // bucket nor the zoom step may oscillate.
    Qsg2DLodInputs in = millionCellFullExtent();
    // avgCellAreaPx == kFarMaxCellAreaPx at this extent width:
    //   cellArea(scene)=100 m²; need ppu² == 8/100 → extent = 1600/ppu.
    in.extentWidth  = 1600.0 / std::sqrt(Qsg2DLodPolicy::kFarMaxCellAreaPx / 100.0);
    in.extentHeight = in.extentWidth * (900.0 / 1600.0);

    const auto base = Qsg2DLodPolicy::decide(in);

    Qsg2DLodInputs jitter = in;
    jitter.previousBucket   = base.bucket;
    jitter.previousZoomStep = base.zoomStep;

    for (double f : {1.01, 0.99, 1.005, 0.995, 1.0}) {
        Qsg2DLodInputs j = jitter;
        j.extentWidth  = in.extentWidth * f;
        j.extentHeight = in.extentHeight * f;
        const auto d = Qsg2DLodPolicy::decide(j);
        EXPECT_EQ(d.bucket, base.bucket)  << "jitter factor " << f;
        EXPECT_EQ(d.zoomStep, base.zoomStep) << "jitter factor " << f;
        EXPECT_EQ(d.contentKey(), base.contentKey()) << "jitter factor " << f;
    }
}

TEST(Qsg2DLodPolicyTest, LargeZoomChangeStillCrossesBuckets)
{
    // Hysteresis must damp jitter, not real zooms: full extent (Far) to
    // near zoom (Near) flips even with the previous decision anchored.
    const auto far = Qsg2DLodPolicy::decide(millionCellFullExtent());
    Qsg2DLodInputs in = millionCellNearZoom();
    in.previousBucket   = far.bucket;
    in.previousZoomStep = far.zoomStep;
    const auto d = Qsg2DLodPolicy::decide(in);
    EXPECT_EQ(d.bucket, Qsg2DLodDecision::Near);
    EXPECT_NE(d.contentKey(), far.contentKey());
}

TEST(Qsg2DLodPolicyTest, ViewportSizeChangesBucketPredictably)
{
    // A mid-density view on a small viewport…
    Qsg2DLodInputs in = millionCellFullExtent();
    in.viewportWidthPx  = 400.0;
    in.viewportHeightPx = 225.0;
    in.extentWidth      = 800.0;    // ppu = 0.5 → cellAreaPx = 25 → Mid
    in.extentHeight     = 450.0;
    const auto small = Qsg2DLodPolicy::decide(in);
    EXPECT_EQ(small.bucket, Qsg2DLodDecision::Mid);

    // …becomes Near when the window grows 4× linearly (16× the pixels):
    // ppu = 2 → cellAreaPx = 400 ≥ kNearMinCellAreaPx.
    in.viewportWidthPx  = 1600.0;
    in.viewportHeightPx = 900.0;
    const auto big = Qsg2DLodPolicy::decide(in);
    EXPECT_EQ(big.bucket, Qsg2DLodDecision::Near);
}

// ── Mid-bucket caps ────────────────────────────────────────────────────

TEST(Qsg2DLodPolicyTest, MidBucketCapsLabelsAndForcesSampledVelocity)
{
    Qsg2DLodInputs in = millionCellFullExtent();
    // ppu = 1600/2000 = 0.8 → cellAreaPx = 64: Mid, above the edge gate.
    in.extentWidth  = 2000.0;
    in.extentHeight = 1125.0;
    const auto d = Qsg2DLodPolicy::decide(in);
    ASSERT_EQ(d.bucket, Qsg2DLodDecision::Mid);
    EXPECT_TRUE(d.drawEdges);                 // 64 px² ≥ 32 px² edge gate
    EXPECT_FALSE(d.drawVertexMarkers);        // never at Mid
    EXPECT_TRUE(d.drawContours);
    EXPECT_TRUE(d.exactContourBands);
    EXPECT_FALSE(d.denseVelocityAllowed);     // sampled glyphs only
    EXPECT_EQ(d.maxContourLabels, Qsg2DLodPolicy::kMidMaxContourLabels);
}

TEST(Qsg2DLodPolicyTest, DenseVelocityGatedByVisibleCellCap)
{
    Qsg2DLodInputs in = millionCellNearZoom();
    in.visibleFraction = 0.5;   // ~500k visible cells — over the cap
    const auto d = Qsg2DLodPolicy::decide(in);
    ASSERT_EQ(d.bucket, Qsg2DLodDecision::Near);
    EXPECT_FALSE(d.denseVelocityAllowed);
}

// ── Degenerate inputs ──────────────────────────────────────────────────

TEST(Qsg2DLodPolicyTest, DegenerateViewportDrawsAggregateAndSelectionOnly)
{
    Qsg2DLodInputs in;
    in.haveSelection = true;
    const auto d = Qsg2DLodPolicy::decide(in);
    EXPECT_EQ(d.bucket, Qsg2DLodDecision::Far);
    EXPECT_FALSE(d.drawEdges);
    EXPECT_TRUE(d.drawSelectedOverlays);
}

TEST(Qsg2DLodPolicyTest, UnknownDensityFallsBackToNear)
{
    Qsg2DLodInputs in = millionCellFullExtent();
    in.cellCount    = 0;
    in.meshBBoxArea = 0.0;
    EXPECT_EQ(Qsg2DLodPolicy::decide(in).bucket, Qsg2DLodDecision::Near);
}
