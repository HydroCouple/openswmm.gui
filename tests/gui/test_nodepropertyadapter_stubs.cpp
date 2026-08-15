/*!
 * \file  test_nodepropertyadapter_stubs.cpp
 * \brief Link-only stubs so test_nodepropertyadapter can build without the full
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
 */

#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "output/outputstatsregistry.h"
#include "ui/properties/userflagseditref.h"

double SWMMResultsLayer::nodeStatMaxDepth(const QString &) const { return 0.0; }
double SWMMResultsLayer::nodeStatMaxOverflow(const QString &) const { return 0.0; }
double SWMMResultsLayer::nodeStatVolFlooded(const QString &) const { return 0.0; }
double SWMMResultsLayer::nodeStatTimeFlooded(const QString &) const { return 0.0; }

openswmmvis::ui::UserFlagsModel *SWMMModelLayer::ensureUserFlagsModel() { return nullptr; }

openswmmvis::OutputIdentity
openswmmvis::OutputStatsRegistry::identityFor(const QUuid &) const { return {}; }

// Free function used by the adapter's row builder; real definition lives in
// userflagseditref.cpp, which the test does not link.
QString userFlagsSummaryFor(openswmmvis::ui::UserFlagsModel *,
                            const QString &, const QString &) { return {}; }
