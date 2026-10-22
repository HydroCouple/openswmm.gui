/**
 * @file      test_nodepropertyadapter_stubs.cpp
 * @brief     Link-only stubs so test_nodepropertyadapter can build without the full
 *        results-layer chain.
 *
 * swmmnodepropertyadapter.cpp references four SWMMResultsLayer::nodeStat*
 * getters, SWMMModelLayer::ensureUserFlagsModel() and
 * OutputStatsRegistry::identityFor(). Their real definitions
 * (swmmresultslayer.cpp / swmmmodellayer.cpp / outputstatsregistry.cpp) pull in
 * SWMMResultsLayer's entire transitive surface, which is why the adapter test was
 * disabled. But those paths are unreachable in the unit test: it never sets a
 * stats source or a layer, so the stat getters take the engine path
 * (m_statsSourceId.isNull()) and ensureUserFlagsModel()/identityFor() are never
 * called. Providing trivial definitions here satisfies the linker without the
 * chain — the same link-stub technique test_typeconversionflow uses for
 * SWMMModelLayer::apply*Convert.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "output/outputstatsregistry.h"
#include "ui/properties/userflagseditref.h"

/**
 *
 * @brief Provide an unreachable link-only maximum-depth stub for adapter tests.
 * @param name Unused borrowed node identifier.
 * @return Zero placeholder, with no result lookup or physical-statistic guarantee.
 *
 */
double SWMMResultsLayer::nodeStatMaxDepth(const QString &name) const
{
    return 0.0;
}
/**
 *
 * @brief Provide an unreachable link-only overflow stub for adapter tests.
 * @param name Unused borrowed node identifier.
 * @return Zero placeholder without result IO or a computed statistic.
 *
 */
double SWMMResultsLayer::nodeStatMaxOverflow(const QString &name) const
{
    return 0.0;
}
/**
 *
 * @brief Provide an unreachable link-only flooded-volume stub for adapter tests.
 * @param name Unused borrowed node identifier.
 * @return Zero placeholder; no result source is accessed.
 *
 */
double SWMMResultsLayer::nodeStatVolFlooded(const QString &name) const
{
    return 0.0;
}
/**
 *
 * @brief Provide an unreachable link-only flooded-time stub for adapter tests.
 * @param name Unused borrowed node identifier.
 * @return Zero placeholder; does not measure a duration.
 *
 */
double SWMMResultsLayer::nodeStatTimeFlooded(const QString &name) const
{
    return 0.0;
}

/**
 *
 * @brief Provide an unreachable link stub that creates no user-flag model.
 * @return nullptr; caller receives no ownership and no layer state changes.
 *
 */
openswmmvis::ui::UserFlagsModel *SWMMModelLayer::ensureUserFlagsModel()
{
    return nullptr;
}

// The inlet-junction setters route their one undoable write through the layer
// (SetInletUsageCommand). Unreachable here for the same reason as the rest of
// this file: the test never binds a layer, and writeInletUsage() returns early
// without one.
/**
 *
 * @brief Refuse every inlet edit in the layer-free adapter link fixture.
 * @param usage Unused borrowed usage request, never read/copied.
 * @return Always false without engine changes, undo commands or signals.
 *
 */
bool SWMMModelLayer::pushInletUsageEdit(const SWMM_InletUsage &usage)
{
    return false;
}

// The 2D-coupling setters push through the layer when one is bound; without
// one (this test) they write the engine directly via nodecoupling2d::write.
/**
 *
 * @brief Refuse every coupling edit in the layer-free adapter link fixture.
 * @param states Unused borrowed proposed coupling states.
 * @param text Unused borrowed command label.
 * @param error Optional unused output pointer, not written even on refusal.
 * @return Always false; no ownership transfer, engine write or undo command.
 *
 */
bool SWMMModelLayer::pushNodeCoupling2DEdits(const QVector<nodecoupling2d::State> &states,
                                             const QString &text, QString *error)
{
    return false;
}

/**
 *
 * @brief Return empty output identity in the adapter-only link fixture.
 * @param id Unused borrowed output UUID.
 * @return Default OutputIdentity; no registry lookup or retained reference.
 *
 */
openswmmvis::OutputIdentity openswmmvis::OutputStatsRegistry::identityFor(const QUuid &id) const
{
    return {};
}

// Free function used by the adapter's row builder; real definition lives in
// userflagseditref.cpp, which the test does not link.
/**
 *
 * @brief Return no user-flag summary in the adapter-only link fixture.
 * @param model Unused borrowed model pointer, may be null and is never dereferenced.
 * @param kind Unused borrowed object-kind token.
 * @param id Unused borrowed object identifier.
 * @return Empty owned string, with no lookup, signals or state mutation.
 *
 */
QString userFlagsSummaryFor(openswmmvis::ui::UserFlagsModel *model, const QString &kind,
                            const QString &id)
{
    return {};
}
