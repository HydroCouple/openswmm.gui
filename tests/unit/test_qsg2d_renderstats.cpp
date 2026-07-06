/*!
 * \file   test_qsg2d_renderstats.cpp
 * \brief  Unit tests for Qsg2DRenderStats (QSG-2D-1M Phase 1).
 *
 * Contract under test:
 *   - dirty-reason formatting is stable (fixed bit order, '|' separator,
 *     "none" for an empty bitset) — log lines are a grep contract.
 *   - per-pass byte / vertex counters sum correctly.
 *   - the disabled-logging path never formats (no sink invocation).
 */

#include <gtest/gtest.h>

#include "render/qsg2drenderstats.h"

#include <QString>

using OpenSWMM::Render::Qsg2DRenderStats;

namespace {

class RenderStatsTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        // Never leak a forced logging state into other tests.
        Qsg2DRenderStats::overrideLoggingForTest(-1);
    }
};

} // namespace

// ── Dirty-reason formatting ────────────────────────────────────────────

TEST_F(RenderStatsTest, EmptyBitsetFormatsAsNone)
{
    EXPECT_EQ(Qsg2DRenderStats::dirtyReasonsToString(0), QStringLiteral("none"));
}

TEST_F(RenderStatsTest, SingleReasonsFormatStably)
{
    using S = Qsg2DRenderStats;
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtyPan),        QStringLiteral("pan"));
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtyZoom),       QStringLiteral("zoom"));
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtyTime),       QStringLiteral("time"));
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtyStyle),      QStringLiteral("style"));
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtySelection),  QStringLiteral("selection"));
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtyGeometry),   QStringLiteral("geometry"));
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtyLayer),      QStringLiteral("layer"));
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtyVisibility), QStringLiteral("visibility"));
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtyLod),        QStringLiteral("lod"));
}

TEST_F(RenderStatsTest, CombinedReasonsKeepBitOrder)
{
    using S = Qsg2DRenderStats;
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtyZoom | S::DirtyPan),
              QStringLiteral("pan|zoom"));
    EXPECT_EQ(S::dirtyReasonsToString(S::DirtyGeometry | S::DirtyTime
                                      | S::DirtySelection),
              QStringLiteral("time|selection|geometry"));
    EXPECT_EQ(S::dirtyReasonsToString(0xFFu),
              QStringLiteral("pan|zoom|time|style|selection|geometry|layer|visibility"));
}

// ── Counter aggregation ────────────────────────────────────────────────

TEST_F(RenderStatsTest, ByteAndVertexCountersSum)
{
    Qsg2DRenderStats s;
    s.rendererName = QStringLiteral("results2d");
    s.addPass(QStringLiteral("fill"),   3'000'000, 84'000'000);
    s.addPass(QStringLiteral("edges"),          0,          0);
    s.addPass(QStringLiteral("select"),        18,        144);

    EXPECT_EQ(s.totalBuiltVertices(), 3'000'018);
    EXPECT_EQ(s.totalUploadedBytes(), 84'000'144);
    ASSERT_EQ(s.passes.size(), 3);
    EXPECT_EQ(s.passes[0].pass, QStringLiteral("fill"));
    EXPECT_EQ(s.passes[2].uploadedBytes, 144);
}

TEST_F(RenderStatsTest, EmptyStatsSumToZero)
{
    Qsg2DRenderStats s;
    EXPECT_EQ(s.totalBuiltVertices(), 0);
    EXPECT_EQ(s.totalUploadedBytes(), 0);
}

TEST_F(RenderStatsTest, ResetClearsEverything)
{
    Qsg2DRenderStats s;
    s.rendererName = QStringLiteral("mesh2d");
    s.dirtyReasons = Qsg2DRenderStats::DirtyZoom;
    s.visibleCells = 42;
    s.addPass(QStringLiteral("fill"), 9, 90);
    s.reset();
    EXPECT_TRUE(s.rendererName.isEmpty());
    EXPECT_EQ(s.dirtyReasons, 0u);
    EXPECT_EQ(s.visibleCells, -1);
    EXPECT_TRUE(s.passes.isEmpty());
}

// ── Log line + enable gating ───────────────────────────────────────────

TEST_F(RenderStatsTest, LogLineCarriesRendererDirtyAndPassData)
{
    Qsg2DRenderStats s;
    s.rendererName = QStringLiteral("mesh2d");
    s.dirtyReasons = Qsg2DRenderStats::DirtyPan | Qsg2DRenderStats::DirtyStyle;
    s.visibleCells = 1234;
    s.addPass(QStringLiteral("fill"), 30, 840);

    const QString line = s.toLogLine();
    EXPECT_TRUE(line.contains(QStringLiteral("mesh2d")));
    EXPECT_TRUE(line.contains(QStringLiteral("dirty=pan|style")));
    EXPECT_TRUE(line.contains(QStringLiteral("cells=1234")));
    EXPECT_TRUE(line.contains(QStringLiteral("fill[v=30 B=840]")));
    EXPECT_TRUE(line.contains(QStringLiteral("totalV=30")));
    EXPECT_TRUE(line.contains(QStringLiteral("totalB=840")));
    // Unmeasured fields stay out of the line entirely.
    EXPECT_FALSE(line.contains(QStringLiteral("edges=")));
    EXPECT_FALSE(line.contains(QStringLiteral("repaintMs=")));
}

TEST_F(RenderStatsTest, DisabledLoggingNeverInvokesTheSink)
{
    Qsg2DRenderStats::overrideLoggingForTest(0);
    Qsg2DRenderStats s;
    s.rendererName = QStringLiteral("results2d");
    s.addPass(QStringLiteral("fill"), 3, 84);

    int calls = 0;
    s.logIfEnabled([&calls](const QString &) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST_F(RenderStatsTest, EnabledLoggingEmitsOneFormattedLine)
{
    Qsg2DRenderStats::overrideLoggingForTest(1);
    Qsg2DRenderStats s;
    s.rendererName = QStringLiteral("results2d");
    s.dirtyReasons = Qsg2DRenderStats::DirtyTime;
    s.addPass(QStringLiteral("fill"), 3, 84);

    QStringList lines;
    s.logIfEnabled([&lines](const QString &l) { lines.append(l); });
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], s.toLogLine());
    EXPECT_TRUE(lines[0].contains(QStringLiteral("dirty=time")));
}
