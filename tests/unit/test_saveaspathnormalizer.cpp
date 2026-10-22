/**
 * @file      test_saveaspathnormalizer.cpp
 * @brief     Unit tests for openswmmvis::normalizeSaveAsPath (Slice RA, Phase RA.4).
 *
 * The normalizer is a pure function over (dialogPath, writableExtensions),
 * so every edge case is exercised without spinning up QFileDialog or the
 * FileFilterRegistry. See include/project/saveaspathnormalizer.h.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <QSet>
#include <QString>

#include "project/saveaspathnormalizer.h"

namespace
{

// Writable-extensions set the production code populates from
// FileFilterRegistry (entries with canWrite=true). Tests use the same
// {inp, oswp, gpkg} set the GUI ships today.
/**
 *
 * @brief Build the explicit writable-extension fixture used by Save As normalization cases.
 * @return Owned lowercase inp/oswp/gpkg set, without registry lookup or file I/O.
 * This fixed fixture does not automatically track newly installed plugin formats.
 *
 */
QSet<QString> defaultWritable()
{
    return QSet<QString>{
        QStringLiteral("inp"),
        QStringLiteral("oswp"),
        QStringLiteral("gpkg"),
    };
}

} // anonymous

// ---------------------------------------------------------------------------
// 8 cases per §R.1 Phase RA.4
// ---------------------------------------------------------------------------

/**
 *
 * @brief Keep a single INP suffix without project/normalization flags.
 *
 */
TEST(SaveAsPathNormalizer, BareInpPassesThrough)
{
    const auto r = openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/m.inp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.inp"));
    EXPECT_FALSE(r.isProject);
    EXPECT_FALSE(r.wasNormalized);
}

/**
 *
 * @brief Collapse duplicated INP suffix and set only the normalization flag.
 *
 */
TEST(SaveAsPathNormalizer, DoubleInpCollapses)
{
    const auto r =
        openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/m.inp.inp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.inp"));
    EXPECT_FALSE(r.isProject);
    EXPECT_TRUE(r.wasNormalized);
}

/**
 *
 * @brief Resolve INP.OSWP dialog suffix to INP sibling with project and normalization flags.
 *
 */
TEST(SaveAsPathNormalizer, InpThenOswpProducesProject)
{
    const auto r =
        openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/m.inp.oswp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.inp"));
    EXPECT_TRUE(r.isProject);
    EXPECT_TRUE(r.wasNormalized);
}

/**
 *
 * @brief Collapse OSWP.INP to INP while retaining ordinary model-save intent.
 *
 */
TEST(SaveAsPathNormalizer, OswpThenInpRoundTrip)
{
    const auto r =
        openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/m.oswp.inp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.inp"));
    EXPECT_FALSE(r.isProject);
    EXPECT_TRUE(r.wasNormalized);
}

/**
 *
 * @brief Collapse duplicated writable GeoPackage suffix without project intent.
 *
 */
TEST(SaveAsPathNormalizer, DoubleGpkgCollapses)
{
    const auto r =
        openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/m.gpkg.gpkg"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.gpkg"));
    EXPECT_FALSE(r.isProject);
    EXPECT_TRUE(r.wasNormalized);
}

/**
 *
 * @brief Leave repeated unadvertised BAK suffix unchanged without normalization flags.
 *
 */
TEST(SaveAsPathNormalizer, NonWritableExtensionPassesThrough)
{
    // .bak isn't in the writable set — normalizer leaves it alone even
    // though "<ext>.<ext>" duplication is visually present. Defense
    // against false-positives on user-coined names.
    const auto r =
        openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/m.bak.bak"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.bak.bak"));
    EXPECT_FALSE(r.isProject);
    EXPECT_FALSE(r.wasNormalized);
}

/**
 *
 * @brief Require empty dialog path to yield empty destination and false flags.
 *
 */
TEST(SaveAsPathNormalizer, EmptyPathReturnsEmpty)
{
    const auto r = openswmmvis::normalizeSaveAsPath(QString(), defaultWritable());
    EXPECT_TRUE(r.inpPath.isEmpty());
    EXPECT_FALSE(r.isProject);
    EXPECT_FALSE(r.wasNormalized);
}

/**
 *
 * @brief Collapse three repeated INP suffixes to a single model suffix.
 *
 */
TEST(SaveAsPathNormalizer, DeeplyDuplicatedCollapses)
{
    const auto r =
        openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/foo.inp.inp.inp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/foo.inp"));
    EXPECT_FALSE(r.isProject);
    EXPECT_TRUE(r.wasNormalized);
}

// ---------------------------------------------------------------------------
// Extra coverage — bare .oswp + isProject derivation
// ---------------------------------------------------------------------------

/**
 *
 * @brief Require a bare OSWP path to select INP sibling and project intent without duplicate
 * normalization.
 *
 */
TEST(SaveAsPathNormalizer, BareOswpProducesInpSibling)
{
    const auto r = openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/m.oswp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.inp"));
    EXPECT_TRUE(r.isProject);
    EXPECT_FALSE(r.wasNormalized);
}

/**
 *
 * @brief Leave an extensionless path unchanged without project/normalization flags.
 *
 */
TEST(SaveAsPathNormalizer, NoExtensionPassesThrough)
{
    const auto r = openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/m"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m"));
    EXPECT_FALSE(r.isProject);
    EXPECT_FALSE(r.wasNormalized);
}
