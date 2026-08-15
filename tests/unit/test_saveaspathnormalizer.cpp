/*!
 * \file   test_saveaspathnormalizer.cpp
 * \brief  Unit tests for openswmmvis::normalizeSaveAsPath (Slice RA, Phase RA.4).
 *
 * The normalizer is a pure function over (dialogPath, writableExtensions),
 * so every edge case is exercised without spinning up QFileDialog or the
 * FileFilterRegistry. See include/project/saveaspathnormalizer.h.
 */

#include <gtest/gtest.h>

#include <QSet>
#include <QString>

#include "project/saveaspathnormalizer.h"

namespace {

// Writable-extensions set the production code populates from
// FileFilterRegistry (entries with canWrite=true). Tests use the same
// {inp, oswp, gpkg} set the GUI ships today.
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

TEST(SaveAsPathNormalizer, BareInpPassesThrough)
{
    const auto r = openswmmvis::normalizeSaveAsPath(
        QStringLiteral("/p/m.inp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.inp"));
    EXPECT_FALSE(r.isProject);
    EXPECT_FALSE(r.wasNormalized);
}

TEST(SaveAsPathNormalizer, DoubleInpCollapses)
{
    const auto r = openswmmvis::normalizeSaveAsPath(
        QStringLiteral("/p/m.inp.inp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.inp"));
    EXPECT_FALSE(r.isProject);
    EXPECT_TRUE(r.wasNormalized);
}

TEST(SaveAsPathNormalizer, InpThenOswpProducesProject)
{
    const auto r = openswmmvis::normalizeSaveAsPath(
        QStringLiteral("/p/m.inp.oswp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.inp"));
    EXPECT_TRUE(r.isProject);
    EXPECT_TRUE(r.wasNormalized);
}

TEST(SaveAsPathNormalizer, OswpThenInpRoundTrip)
{
    const auto r = openswmmvis::normalizeSaveAsPath(
        QStringLiteral("/p/m.oswp.inp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.inp"));
    EXPECT_FALSE(r.isProject);
    EXPECT_TRUE(r.wasNormalized);
}

TEST(SaveAsPathNormalizer, DoubleGpkgCollapses)
{
    const auto r = openswmmvis::normalizeSaveAsPath(
        QStringLiteral("/p/m.gpkg.gpkg"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.gpkg"));
    EXPECT_FALSE(r.isProject);
    EXPECT_TRUE(r.wasNormalized);
}

TEST(SaveAsPathNormalizer, NonWritableExtensionPassesThrough)
{
    // .bak isn't in the writable set — normalizer leaves it alone even
    // though "<ext>.<ext>" duplication is visually present. Defense
    // against false-positives on user-coined names.
    const auto r = openswmmvis::normalizeSaveAsPath(
        QStringLiteral("/p/m.bak.bak"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.bak.bak"));
    EXPECT_FALSE(r.isProject);
    EXPECT_FALSE(r.wasNormalized);
}

TEST(SaveAsPathNormalizer, EmptyPathReturnsEmpty)
{
    const auto r = openswmmvis::normalizeSaveAsPath(QString(), defaultWritable());
    EXPECT_TRUE(r.inpPath.isEmpty());
    EXPECT_FALSE(r.isProject);
    EXPECT_FALSE(r.wasNormalized);
}

TEST(SaveAsPathNormalizer, DeeplyDuplicatedCollapses)
{
    const auto r = openswmmvis::normalizeSaveAsPath(
        QStringLiteral("/p/foo.inp.inp.inp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/foo.inp"));
    EXPECT_FALSE(r.isProject);
    EXPECT_TRUE(r.wasNormalized);
}

// ---------------------------------------------------------------------------
// Extra coverage — bare .oswp + isProject derivation
// ---------------------------------------------------------------------------

TEST(SaveAsPathNormalizer, BareOswpProducesInpSibling)
{
    const auto r = openswmmvis::normalizeSaveAsPath(
        QStringLiteral("/p/m.oswp"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m.inp"));
    EXPECT_TRUE(r.isProject);
    EXPECT_FALSE(r.wasNormalized);
}

TEST(SaveAsPathNormalizer, NoExtensionPassesThrough)
{
    const auto r = openswmmvis::normalizeSaveAsPath(
        QStringLiteral("/p/m"), defaultWritable());
    EXPECT_EQ(r.inpPath, QStringLiteral("/p/m"));
    EXPECT_FALSE(r.isProject);
    EXPECT_FALSE(r.wasNormalized);
}
