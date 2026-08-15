/*!
 * \file   test_outputstatsregistry.cpp
 * \brief  Smoke tests for openswmmvis::OutputStatsRegistry (Slice QA tests).
 *
 * The registry holds QPointer<SWMMResultsLayer> and calls
 * `layer->resultsFilePath()` to compute short labels. Spinning up a real
 * SWMMResultsLayer requires a SWMM_Output handle from an .out file —
 * heavier than this unit-test layer wants to carry. These tests cover
 * the API surface that doesn't need a layer pointer:
 *  - Construction yields an empty registry.
 *  - identityFor(null UUID) returns a default-constructed result (the
 *    "editing-engine" sentinel contract).
 *  - identityFor(unknown UUID) returns a default-constructed result.
 *  - Passing nullptr to register/unregister is a no-op (no crash, no
 *    signal).
 *  - identitiesChanged is wired and emits the signal type the panel
 *    connects to.
 *
 * Full register/unregister/recomputeLabels coverage with real layers
 * lives in the GUI integration suite once the test fixtures for
 * SWMMResultsLayer (real .out files in tests/gui/data) are reused.
 */

#include <gtest/gtest.h>

#include <QObject>
#include <QSignalSpy>
#include <QUuid>

#include "output/outputstatsregistry.h"

using openswmmvis::OutputStatsRegistry;
using openswmmvis::OutputIdentity;

TEST(OutputStatsRegistry, FreshRegistryHasNoIdentities)
{
    OutputStatsRegistry reg;
    EXPECT_TRUE(reg.identities().isEmpty());
}

TEST(OutputStatsRegistry, IdentityForNullUuidReturnsEmpty)
{
    OutputStatsRegistry reg;
    const OutputIdentity id = reg.identityFor(QUuid());
    EXPECT_TRUE(id.stableId.isNull());
    EXPECT_TRUE(id.shortLabel.isEmpty());
    EXPECT_TRUE(id.tooltipPath.isEmpty());
    EXPECT_EQ(id.layer, nullptr);
}

TEST(OutputStatsRegistry, IdentityForUnknownUuidReturnsEmpty)
{
    OutputStatsRegistry reg;
    const QUuid bogus = QUuid::createUuid();
    const OutputIdentity id = reg.identityFor(bogus);
    EXPECT_TRUE(id.stableId.isNull());
    EXPECT_EQ(id.layer, nullptr);
}

TEST(OutputStatsRegistry, NullLayerRegisterIsNoop)
{
    OutputStatsRegistry reg;
    QSignalSpy spy(&reg, &OutputStatsRegistry::identitiesChanged);
    reg.registerLayer(nullptr, QString());
    reg.unregisterLayer(nullptr);
    EXPECT_TRUE(reg.identities().isEmpty());
    EXPECT_EQ(spy.count(), 0);
}

TEST(OutputStatsRegistry, IdentitiesChangedSignalIsConnectable)
{
    OutputStatsRegistry reg;
    QSignalSpy spy(&reg, &OutputStatsRegistry::identitiesChanged);
    // Trivial sanity: the spy is valid (compile-time check that the
    // signal exists with the documented signature).
    EXPECT_TRUE(spy.isValid());
}
