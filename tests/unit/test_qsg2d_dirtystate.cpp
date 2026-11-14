/**
 * @file      test_qsg2d_dirtystate.cpp
 * @brief     Unit tests for Qsg2DDirtyState (QSG-2D-1M Phase 2) and the
 *         Qsg2DAsyncResult stale-output guard (Phase 7 helper).
 *
 * The dirty-state transitions locked here are the Phase 2 acceptance
 * contract:
 *
 *   pan after clean frame          -> Transform only
 *   zoom within same LOD bucket    -> Transform only
 *   zoom across LOD                -> Transform | Lod
 *   highlighted cells changed      -> Selection only
 *   time changed                   -> Data only
 *   geometry revision changed      -> Geometry
 *   style changed                  -> Style
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include "render/qsg2dasyncresult.h"
#include "render/qsg2ddirtystate.h"

#include <vector>

using OpenSWMM::Render::Qsg2DAsyncResult;
using OpenSWMM::Render::Qsg2DDirtyState;
using Domain = OpenSWMM::Render::Qsg2DDirtyState::Domain;

namespace
{

/*! resolve() with "nothing else changed" defaults. */
/**
 *
 * @brief Resolve/consume pending scene dirtiness with other observable changes disabled.
 * @param[in,out] s Dirty-state fixture; resolve clears its pending event notes.
 * @param lodKeyChanged Whether LOD content key differs from the built content, default false.
 * @param insideCoverage Whether current viewport remains inside built coverage, default true.
 * @return Resolved Domain bitmask with geometry/selection/time snapshot-diff flags false;
 * existing explicitly noted events still contribute. No render work or thread synchronization.
 *
 */
quint32 resolveClean(Qsg2DDirtyState &s, bool lodKeyChanged = false, bool insideCoverage = true)
{
    return s.resolve(/*geomRevisionChanged=*/false,
                     /*selectionChanged=*/false,
                     /*timeChanged=*/false, lodKeyChanged, insideCoverage);
}

} // namespace

// ── Extent transitions ─────────────────────────────────────────────────

/**
 *
 * @brief Require a pan-only extent note to resolve solely to Transform.
 *
 */
TEST(Qsg2DDirtyStateTest, PanAfterCleanFrameIsTransformOnly)
{
    Qsg2DDirtyState s;
    s.noteExtentChanged(/*zoomChanged=*/false);
    EXPECT_EQ(resolveClean(s), quint32(Domain::Transform));
}

/**
 *
 * @brief Require zoom inside current LOD/coverage to update Transform only.
 *
 */
TEST(Qsg2DDirtyStateTest, ZoomWithinSameLodIsTransformOnly)
{
    Qsg2DDirtyState s;
    s.noteExtentChanged(/*zoomChanged=*/true);
    EXPECT_EQ(resolveClean(s, /*lodKeyChanged=*/false, /*insideCoverage=*/true),
              quint32(Domain::Transform));
}

/**
 *
 * @brief Require zoom with changed LOD key to resolve Transform and Lod.
 *
 */
TEST(Qsg2DDirtyStateTest, ZoomAcrossLodIsTransformPlusLod)
{
    Qsg2DDirtyState s;
    s.noteExtentChanged(/*zoomChanged=*/true);
    EXPECT_EQ(resolveClean(s, /*lodKeyChanged=*/true), quint32(Domain::Transform | Domain::Lod));
}

/**
 *
 * @brief Require coverage escape during pan to add Lod to Transform.
 *
 */
TEST(Qsg2DDirtyStateTest, PanLeavingCoverageForcesLodRebuild)
{
    Qsg2DDirtyState s;
    s.noteExtentChanged(/*zoomChanged=*/false);
    EXPECT_EQ(resolveClean(s, /*lodKeyChanged=*/false, /*insideCoverage=*/false),
              quint32(Domain::Transform | Domain::Lod));
}

/**
 *
 * @brief Require a changed LOD key without extent event to rebuild Lod only.
 *
 */
TEST(Qsg2DDirtyStateTest, ViewportResizeWithoutExtentChangeRekeysLod)
{
    Qsg2DDirtyState s; // no extent note at all
    EXPECT_EQ(resolveClean(s, /*lodKeyChanged=*/true), quint32(Domain::Lod));
}

/**
 *
 * @brief Require uncovered viewport without an extent note to rebuild Lod.
 *
 */
TEST(Qsg2DDirtyStateTest, CoverageMissWithoutExtentEventStillRebuilds)
{
    // A viewport/DPR resize can grow the view past the built coverage
    // without any setMapExtent call — content must repopulate.
    Qsg2DDirtyState s;
    EXPECT_EQ(resolveClean(s, /*lodKeyChanged=*/false, /*insideCoverage=*/false),
              quint32(Domain::Lod));
}

// ── Content-domain transitions ─────────────────────────────────────────

/**
 *
 * @brief Require explicit selection change to spare other content domains.
 *
 */
TEST(Qsg2DDirtyStateTest, SelectionChangeIsSelectionOnly)
{
    Qsg2DDirtyState s;
    s.noteSelectionChanged();
    EXPECT_EQ(resolveClean(s), quint32(Domain::Selection));
}

/**
 *
 * @brief Resolve a broad external note using selection snapshot difference without adding Style.
 *
 */
TEST(Qsg2DDirtyStateTest, SnapshotDiffedSelectionIsSelectionOnly)
{
    // The catch-all repaintRequested fired, but the only observable diff
    // is the highlighted set — must classify as Selection, NOT Style.
    Qsg2DDirtyState s;
    s.noteExternalChanged();
    EXPECT_EQ(s.resolve(false, /*selectionChanged=*/true, false, false, true),
              quint32(Domain::Selection));
}

/**
 *
 * @brief Require explicit time/data change to resolve solely to Data.
 *
 */
TEST(Qsg2DDirtyStateTest, TimeChangeIsDataOnly)
{
    Qsg2DDirtyState s;
    s.noteDataChanged();
    EXPECT_EQ(resolveClean(s), quint32(Domain::Data));
}

/**
 *
 * @brief Resolve broad external notification using time snapshot difference as Data only.
 *
 */
TEST(Qsg2DDirtyStateTest, SnapshotDiffedTimeIsDataOnly)
{
    Qsg2DDirtyState s;
    s.noteExternalChanged();
    EXPECT_EQ(s.resolve(false, false, /*timeChanged=*/true, false, true), quint32(Domain::Data));
}

/**
 *
 * @brief Resolve external notification plus geometry revision difference as Geometry only.
 *
 */
TEST(Qsg2DDirtyStateTest, GeometryRevisionChangeIsGeometry)
{
    Qsg2DDirtyState s;
    s.noteExternalChanged();
    EXPECT_EQ(s.resolve(/*geomRevisionChanged=*/true, false, false, false, true),
              quint32(Domain::Geometry));
}

/**
 *
 * @brief Require explicit style notification to resolve solely to Style.
 *
 */
TEST(Qsg2DDirtyStateTest, StyleChangeIsStyle)
{
    Qsg2DDirtyState s;
    s.noteStyleChanged();
    EXPECT_EQ(resolveClean(s), quint32(Domain::Style));
}

/**
 *
 * @brief Require external notification without observable snapshot differences to fall back to
 * Style.
 *
 */
TEST(Qsg2DDirtyStateTest, UnclassifiableExternalChangeFallsBackToStyle)
{
    // repaintRequested with no observable diff — the broadest safe verdict
    // that still spares static geometry, selection and transform.
    Qsg2DDirtyState s;
    s.noteExternalChanged();
    EXPECT_EQ(resolveClean(s), quint32(Domain::Style));
}

/**
 *
 * @brief Require geometry difference to suppress the broad external Style fallback.
 *
 */
TEST(Qsg2DDirtyStateTest, ExternalChangeWithGeometryDiffDoesNotAddStyle)
{
    Qsg2DDirtyState s;
    s.noteExternalChanged();
    const quint32 bits = s.resolve(true, false, false, false, true);
    EXPECT_EQ(bits, quint32(Domain::Geometry));
    EXPECT_EQ(bits & Domain::Style, 0u);
}

/**
 *
 * @brief Require layer replacement notification to invalidate every content domain.
 *
 */
TEST(Qsg2DDirtyStateTest, LayerChangeDirtiesAllContent)
{
    Qsg2DDirtyState s;
    s.noteLayerChanged();
    EXPECT_EQ(resolveClean(s), quint32(Domain::AllContent));
}

/**
 *
 * @brief Require data, selection and zoom/LOD notifications to accumulate their exact union.
 *
 */
TEST(Qsg2DDirtyStateTest, CombinedEventsAccumulate)
{
    Qsg2DDirtyState s;
    s.noteDataChanged();
    s.noteSelectionChanged();
    s.noteExtentChanged(true);
    EXPECT_EQ(resolveClean(s, /*lodKeyChanged=*/true),
              quint32(Domain::Data | Domain::Selection | Domain::Transform | Domain::Lod));
}

/**
 *
 * @brief Require one resolve to consume pending notes and a clean second resolve to return None.
 *
 */
TEST(Qsg2DDirtyStateTest, ResolveClearsPendingState)
{
    Qsg2DDirtyState s;
    s.noteDataChanged();
    s.noteExtentChanged(true);
    EXPECT_TRUE(s.hasAnythingPending());
    (void)resolveClean(s);
    EXPECT_FALSE(s.hasAnythingPending());
    EXPECT_EQ(resolveClean(s), quint32(Domain::None));
}

/**
 *
 * @brief Require extent/zoom pending flags to remain visible until resolve consumes them.
 *
 */
TEST(Qsg2DDirtyStateTest, PendingIntrospectionSurvivesUntilResolve)
{
    Qsg2DDirtyState s;
    s.noteExtentChanged(/*zoomChanged=*/true);
    EXPECT_TRUE(s.extentChangePending());
    EXPECT_TRUE(s.zoomChangePending());
    (void)resolveClean(s);
    EXPECT_FALSE(s.extentChangePending());
    EXPECT_FALSE(s.zoomChangePending());
}

// ── Qsg2DAsyncResult: cancellation-safe result replacement (Phase 7) ───

/**
 *
 * @brief Require current asynchronous generation publication to install values and mark output up
 * to date.
 *
 */
TEST(Qsg2DAsyncResultTest, PublishesCurrentGeneration)
{
    Qsg2DAsyncResult<std::vector<int>> buf;
    const quint64 gen = buf.beginJob();
    EXPECT_TRUE(buf.jobPending());
    EXPECT_TRUE(buf.tryPublish(gen, std::vector<int>{1, 2, 3}));
    EXPECT_TRUE(buf.upToDate());
    EXPECT_EQ(buf.value().size(), size_t(3));
}

/**
 *
 * @brief Reject old-generation completion and accept only the replacement generation value.
 *
 */
TEST(Qsg2DAsyncResultTest, StaleGenerationIsDropped)
{
    Qsg2DAsyncResult<std::vector<int>> buf;
    const quint64 oldGen = buf.beginJob();
    const quint64 newGen = buf.beginJob(); // inputs changed mid-flight

    // Slow first worker finishes late — must be dropped.
    EXPECT_FALSE(buf.tryPublish(oldGen, std::vector<int>{9, 9, 9}));
    EXPECT_FALSE(buf.hasValue());

    EXPECT_TRUE(buf.tryPublish(newGen, std::vector<int>{1}));
    ASSERT_TRUE(buf.hasValue());
    EXPECT_EQ(buf.value().front(), 1);
}

/**
 *
 * @brief Retain last accepted value for display while newer generation remains pending/stale.
 *
 */
TEST(Qsg2DAsyncResultTest, PreviousValueRendersWhileNewJobPending)
{
    Qsg2DAsyncResult<int> buf;
    const quint64 g1 = buf.beginJob();
    ASSERT_TRUE(buf.tryPublish(g1, 41));

    (void)buf.beginJob();        // new inputs, worker still running
    EXPECT_TRUE(buf.hasValue()); // double-buffer: old value still drawable
    EXPECT_FALSE(buf.upToDate());
    EXPECT_EQ(buf.value(), 41);
}

/**
 *
 * @brief Require invalidation to clear accepted value and refuse the formerly pending generation.
 *
 */
TEST(Qsg2DAsyncResultTest, InvalidateCancelsInFlightAndClearsValue)
{
    Qsg2DAsyncResult<int> buf;
    const quint64 g1 = buf.beginJob();
    ASSERT_TRUE(buf.tryPublish(g1, 7));

    const quint64 g2 = buf.beginJob();
    buf.invalidate(); // e.g. mesh geometry swapped
    EXPECT_FALSE(buf.hasValue());
    EXPECT_FALSE(buf.tryPublish(g2, 8));
    EXPECT_FALSE(buf.hasValue());
}
