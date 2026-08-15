/*!
 * \file   test_contourjob.cpp
 * \brief  Unit tests for computeContourJob (QSG-2D-1M Phase 7).
 *
 * The async contour worker must be a pure function of its snapshot:
 *   - identical output to calling the marching templates directly on the
 *     same data (the renderer's synchronous fallback path),
 *   - empty level sets skip the respective pass,
 *   - empty / mismatched snapshots yield empty output instead of UB.
 */

#include <gtest/gtest.h>

#include "render/contourjob.h"

#include <QPointF>

#include <array>
#include <memory>
#include <vector>

using OpenSWMM::Render::ContourJobInput;
using OpenSWMM::Render::ContourJobOutput;
using OpenSWMM::Render::computeContourJob;

namespace {

/*! Two triangles forming a unit square, scalar rising with x+y. */
ContourJobInput makeSquareInput()
{
    auto pos = std::make_shared<std::vector<ContourJobInput::TriPos>>();
    pos->push_back({0.f, 0.f, 1.f, 0.f, 0.f, 1.f});
    pos->push_back({1.f, 0.f, 1.f, 1.f, 0.f, 1.f});

    ContourJobInput in;
    in.positions = pos;
    in.scalars   = std::make_shared<std::vector<std::array<float, 3>>>(
        std::vector<std::array<float, 3>>{{0.f, 1.f, 1.f}, {1.f, 2.f, 1.f}});
    in.bandLevels = {0.0, 1.0, 2.0};
    in.isoLevels  = {0.5, 1.5};
    return in;
}

} // namespace

TEST(ContourJobTest, MatchesDirectMarchingOnSameSnapshot)
{
    const ContourJobInput in = makeSquareInput();
    const ContourJobOutput out = computeContourJob(in);

    // Reference: run the marching templates directly over the same arrays
    // (this is exactly what the renderer's synchronous path does).
    struct Tri { ContourJobInput::TriPos p; std::array<float, 3> s; };
    std::vector<Tri> tris;
    for (size_t i = 0; i < in.positions->size(); ++i)
        tris.push_back({(*in.positions)[i], (*in.scalars)[i]});
    const auto extract = [](const Tri &t,
                            QPointF &p0, QPointF &p1, QPointF &p2,
                            double &v0, double &v1, double &v2) {
        p0 = QPointF(t.p.ax, t.p.ay);
        p1 = QPointF(t.p.bx, t.p.by);
        p2 = QPointF(t.p.cx, t.p.cy);
        v0 = t.s[0]; v1 = t.s[1]; v2 = t.s[2];
    };
    const auto refBands = OpenSWMM::Contour::marchingTrianglesIsobands(
        tris, in.bandLevels, extract);
    const auto refSegs = OpenSWMM::Contour::marchingTriangles(
        tris, in.isoLevels, extract);

    ASSERT_EQ(out.bands.size(), refBands.size());
    for (size_t i = 0; i < refBands.size(); ++i) {
        EXPECT_EQ(out.bands[i].bandIndex, refBands[i].bandIndex);
        ASSERT_EQ(out.bands[i].verts.size(), refBands[i].verts.size());
        for (int v = 0; v < refBands[i].verts.size(); ++v)
            EXPECT_EQ(out.bands[i].verts[v], refBands[i].verts[v]);
    }
    ASSERT_EQ(out.segs.size(), refSegs.size());
    for (size_t i = 0; i < refSegs.size(); ++i) {
        EXPECT_EQ(out.segs[i].a,     refSegs[i].a);
        EXPECT_EQ(out.segs[i].b,     refSegs[i].b);
        EXPECT_EQ(out.segs[i].level, refSegs[i].level);
    }
    EXPECT_FALSE(out.bands.empty());
    EXPECT_FALSE(out.segs.empty());
}

TEST(ContourJobTest, EmptyLevelSetsSkipPasses)
{
    ContourJobInput in = makeSquareInput();
    in.bandLevels.clear();
    ContourJobOutput out = computeContourJob(in);
    EXPECT_TRUE(out.bands.empty());
    EXPECT_FALSE(out.segs.empty());

    in = makeSquareInput();
    in.isoLevels.clear();
    out = computeContourJob(in);
    EXPECT_FALSE(out.bands.empty());
    EXPECT_TRUE(out.segs.empty());

    // A single band edge cannot form a band either.
    in = makeSquareInput();
    in.bandLevels = {0.5};
    in.isoLevels.clear();
    out = computeContourJob(in);
    EXPECT_TRUE(out.bands.empty());
}

TEST(ContourJobTest, DegenerateSnapshotsAreSafe)
{
    ContourJobInput in;   // null positions
    EXPECT_TRUE(computeContourJob(in).bands.empty());

    in.positions = std::make_shared<std::vector<ContourJobInput::TriPos>>();
    EXPECT_TRUE(computeContourJob(in).segs.empty());

    // Positions without scalars are equally safe.
    in = makeSquareInput();
    in.scalars.reset();
    EXPECT_TRUE(computeContourJob(in).bands.empty());

    // Scalars shorter than positions: only the common prefix is marched.
    in = makeSquareInput();
    in.scalars = std::make_shared<std::vector<std::array<float, 3>>>(
        std::vector<std::array<float, 3>>{(*in.scalars)[0]});
    const ContourJobOutput out = computeContourJob(in);
    for (const auto &s : out.segs) {
        // Only the first triangle (x+y in [0,1] region) can contribute.
        EXPECT_LE(s.a.x() + s.a.y(), 1.0 + 1e-9);
    }
}
