/*!
 * \file  test_propertyadapter_stats_stubs.cpp
 * \brief Link-only stubs for the link/subcatch adapters' post-run stat
 *        dispatch, so their lean leaf tests build without the full
 *        results-layer chain.
 *
 * swmmlinkpropertyadapter.cpp / swmmsubcatchpropertyadapter.cpp reference the
 * SWMMResultsLayer::linkStat* / subcatchStat* getters and
 * OutputStatsRegistry::identityFor() (Attribute-Table-parity summary rows).
 * Those paths are unreachable in the leaf tests — no stats source is ever
 * set, so the getters take the engine path (m_statsSourceId.isNull()).
 * Same link-stub technique as test_nodepropertyadapter_stubs.cpp.
 */

#include "layers/swmmresultslayer.h"
#include "output/outputstatsregistry.h"

double SWMMResultsLayer::linkStatMaxFlow(const QString &) const       { return 0.0; }
double SWMMResultsLayer::linkStatMaxVelocity(const QString &) const   { return 0.0; }
double SWMMResultsLayer::linkStatMaxFilling(const QString &) const    { return 0.0; }
double SWMMResultsLayer::linkStatVolFlow(const QString &) const       { return 0.0; }
double SWMMResultsLayer::linkStatSurchargeTime(const QString &) const { return 0.0; }
double SWMMResultsLayer::linkStatPumpCycles(const QString &) const    { return 0.0; }
double SWMMResultsLayer::linkStatPumpOnTime(const QString &) const    { return 0.0; }
double SWMMResultsLayer::linkStatPumpVolume(const QString &) const    { return 0.0; }

double SWMMResultsLayer::subcatchStatPrecip(const QString &) const    { return 0.0; }
double SWMMResultsLayer::subcatchStatRunoffVol(const QString &) const { return 0.0; }
double SWMMResultsLayer::subcatchStatMaxRunoff(const QString &) const { return 0.0; }

openswmmvis::OutputIdentity
openswmmvis::OutputStatsRegistry::identityFor(const QUuid &) const { return {}; }
