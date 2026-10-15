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

TEST(ContourJobTest, PackedBandsPreserveExpandedGeometryAndCoverage)
{
    using namespace OpenSWMM::Render;
    ContourJobInput input;
    auto positions=std::make_shared<std::vector<ContourJobInput::TriPos>>();
    auto scalars=std::make_shared<std::vector<std::array<float,3>>>();
    for(int y=0;y<120;++y)for(int x=0;x<180;++x) {
        positions->push_back({float(x),float(y),float(x+1),float(y),float(x+1),float(y+1)});
        positions->push_back({float(x),float(y),float(x+1),float(y+1),float(x),float(y+1)});
        const float depth=(x+y)%2?.26f:.76f;
        scalars->push_back((x+y)%11?std::array<float,3>{depth,depth,depth}:std::array<float,3>{-.1f,.5f,1.1f});
        scalars->push_back((x+y)%11?std::array<float,3>{depth,depth,depth}:std::array<float,3>{-.1f,1.1f,.5f});
    }
    input.positions=positions;input.scalars=scalars;input.bandLevels={0,.25,.5,.75,1};
    input.isoLevels={.4};input.minimumVisibleValue=.02;
    const auto original=computeContourJob(input);
    ContourJobOutput packed;
    for(const QRectF coverage:{QRectF(),QRectF(30,20,60,40),QRectF(300,300,10,10),QRectF()}) {
        std::vector<PackedBandVertex> expected;
        for(const auto& polygon:original.bands) {
            double left=polygon.verts[0].x(),right=left,top=polygon.verts[0].y(),bottom=top;
            for(const auto& p:polygon.verts) {
                left=std::min(left,p.x());right=std::max(right,p.x());
                top=std::min(top,p.y());bottom=std::max(bottom,p.y());
            }
            if(coverage.isValid() && (right<coverage.left() || left>coverage.right()
                || bottom<coverage.top() || top>coverage.bottom()))continue;
            for(size_t k=1;k+1<polygon.verts.size();++k)for(size_t j:{size_t(0),k,k+1})
                expected.push_back({float(polygon.verts[j].x()),float(polygon.verts[j].y()),quint32(polygon.bandIndex)});
        }
        input.packBands=true;input.bandCoverage=coverage;
        computeContourJob(input,packed);
        EXPECT_TRUE(packed.bands.empty());EXPECT_EQ(packed.segs.size(),original.segs.size());
        size_t count=0,stored=0,bytes=0;
        for(const auto& chunk:packed.packedBands) {
            EXPECT_LE(chunk.vertices.size(),size_t(waterGeometryChunkLimit));
            EXPECT_LE(chunk.indices.size(),size_t(waterGeometryChunkLimit));
            EXPECT_EQ(chunk.indices.size()%3,0u);stored+=chunk.vertices.size();
            bytes+=chunk.vertices.size()*sizeof(PackedBandVertex)+chunk.indices.size()*sizeof(quint16);
            for(quint16 index:chunk.indices) {
                ASSERT_LT(size_t(index),chunk.vertices.size());ASSERT_LT(count,expected.size());
                const auto& vertex=chunk.vertices[index];const auto& reference=expected[count++];
                EXPECT_EQ(vertex.x,reference.x);EXPECT_EQ(vertex.y,reference.y);EXPECT_EQ(vertex.bandIndex,reference.bandIndex);
            }
        }
        EXPECT_EQ(count,expected.size());
        if(!expected.empty()) {
            EXPECT_LT(stored,expected.size());EXPECT_LT(bytes,expected.size()*sizeof(PackedBandVertex));
            if(!coverage.isValid())EXPECT_GT(packed.packedBands.size(),1u);
        }
    }
    computeContourJob({},packed);
    EXPECT_TRUE(packed.bands.empty());EXPECT_TRUE(packed.segs.empty());EXPECT_TRUE(packed.packedBands.empty());
}

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

TEST(ContourJobTest, CoverageSelectionMatchesSelectedSnapshotExactly)
{
    auto input=makeSquareInput();
    input.triangleIndices=std::make_shared<const std::vector<size_t>>(
        std::vector<size_t>{1,99,std::numeric_limits<size_t>::max()});
    const auto selected=computeContourJob(input);
    auto reference=makeSquareInput();
    reference.positions=std::make_shared<const std::vector<ContourJobInput::TriPos>>(
        std::vector<ContourJobInput::TriPos>{(*input.positions)[1]});
    reference.scalars=std::make_shared<const std::vector<std::array<float,3>>>(
        std::vector<std::array<float,3>>{(*input.scalars)[1]});
    const auto expected=computeContourJob(reference);
    ASSERT_EQ(selected.bands.size(),expected.bands.size());
    ASSERT_EQ(selected.segs.size(),expected.segs.size());
    for(size_t i=0;i<expected.bands.size();++i) {
        EXPECT_EQ(selected.bands[i].verts,expected.bands[i].verts);
        EXPECT_EQ(selected.bands[i].bandIndex,expected.bands[i].bandIndex);
    }
    for(size_t i=0;i<expected.segs.size();++i) {
        EXPECT_EQ(selected.segs[i].a,expected.segs[i].a);
        EXPECT_EQ(selected.segs[i].b,expected.segs[i].b);
        EXPECT_EQ(selected.segs[i].level,expected.segs[i].level);
    }
}

TEST(ContourJobTest, EmptyCoverageDoesNotFallBackToWholeMesh)
{
    auto input=makeSquareInput();
    input.triangleIndices=std::make_shared<const std::vector<size_t>>();
    const auto result=computeContourJob(input);
    EXPECT_TRUE(result.bands.empty());EXPECT_TRUE(result.segs.empty());
}

TEST(ContourJobTest, ConfiguredMinimumClipsGeometryAndMaximumSaturates)
{
    auto input = makeSquareInput();
    input.bandLevels = {0.5, 0.75, 1.0};
    const auto result = computeContourJob(input);
    double area = 0;
    bool saturated = false;
    for (const auto &band : result.bands) {
        for (const auto &p : band.verts) {
            EXPECT_GE(p.x() + p.y(), 0.5 - 1e-9);
            if (p.x() + p.y() > 1.0) {
                EXPECT_EQ(band.bandIndex, 1);
                saturated = true;
            }
        }
        for (size_t i = 1; i + 1 < band.verts.size(); ++i) {
            const auto a = band.verts[i] - band.verts[0];
            const auto b = band.verts[i+1] - band.verts[0];
            area += std::abs(a.x()*b.y() - a.y()*b.x())/2;
        }
    }
    EXPECT_NEAR(area, 0.875, 1e-9);
    EXPECT_TRUE(saturated);
    input.scalars = std::make_shared<const std::vector<std::array<float, 3>>>(
        std::vector<std::array<float, 3>>{{0.5f,0.5f,0.5f},{0.25f,0.25f,0.25f}});
    EXPECT_EQ(computeContourJob(input).bands.size(), 1u);
}
