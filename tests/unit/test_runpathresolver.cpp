/*!
 * \file   test_runpathresolver.cpp
 * \brief  Unit tests for openswmmvis::resolveRunOutputPath (Slice QB.5).
 *
 * The pure resolver function is exercised here; the QSettings-driven
 * wrapper (resolveRunOutputPathFromSettings) is a thin lookup-then-dispatch
 * adapter and is left for higher-level GUI tests once a QSettings sandbox
 * fixture is available.
 */

#include <gtest/gtest.h>

#include <QDir>
#include <QString>
#include <QTemporaryDir>

#include "simulation/runpathresolver.h"

using openswmmvis::RunOutputKind;
using openswmmvis::resolveRunOutputPath;

// ---------------------------------------------------------------------------
// Sibling-default fallbacks (no override)
// ---------------------------------------------------------------------------

TEST(RunPathResolver, SiblingDefaultRptWhenNoOverride)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString expected = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.rpt"));
    EXPECT_EQ(resolveRunOutputPath(inp, QString(), RunOutputKind::Rpt), expected);
}

TEST(RunPathResolver, SiblingDefaultOutWhenNoOverride)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString expected = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.out"));
    EXPECT_EQ(resolveRunOutputPath(inp, QString(), RunOutputKind::Out), expected);
}

TEST(RunPathResolver, EmptyInputReturnsEmpty)
{
    EXPECT_TRUE(resolveRunOutputPath(QString(),
                                      QStringLiteral("/whatever.rpt"),
                                      RunOutputKind::Rpt).isEmpty());
}

TEST(RunPathResolver, WhitespaceOverrideTreatedAsEmpty)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString expected = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.rpt"));
    EXPECT_EQ(resolveRunOutputPath(inp, QStringLiteral("   "), RunOutputKind::Rpt),
              expected);
}

// ---------------------------------------------------------------------------
// Override precedence
// ---------------------------------------------------------------------------

TEST(RunPathResolver, AbsoluteOverridePassesThrough)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString override_ = QStringLiteral("/some/absolute/results.out");
    EXPECT_EQ(resolveRunOutputPath(inp, override_, RunOutputKind::Out),
              QDir::cleanPath(override_));
}

TEST(RunPathResolver, RelativeOverrideResolvedAgainstInpDir)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString override_ = QStringLiteral("results/custom.out");
    const QString expected = QDir::cleanPath(
        QDir(tmp.path()).absoluteFilePath(QStringLiteral("results/custom.out")));
    EXPECT_EQ(resolveRunOutputPath(inp, override_, RunOutputKind::Out), expected);
}

TEST(RunPathResolver, DotRelativeOverrideResolved)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("sub/model.inp"));
    QDir(tmp.path()).mkpath(QStringLiteral("sub"));
    const QString override_ = QStringLiteral("../out/custom.out");
    const QString expected = QDir::cleanPath(
        QDir(tmp.path()).absoluteFilePath(QStringLiteral("out/custom.out")));
    EXPECT_EQ(resolveRunOutputPath(inp, override_, RunOutputKind::Out), expected);
}

TEST(RunPathResolver, OverrideAppliesToRptAndOutIndependently)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));

    // Override .rpt but not .out — the .out falls back to sibling default.
    const QString rptOverride = QStringLiteral("/elsewhere/report.txt");
    EXPECT_EQ(resolveRunOutputPath(inp, rptOverride, RunOutputKind::Rpt),
              QDir::cleanPath(rptOverride));
    EXPECT_EQ(resolveRunOutputPath(inp, QString(), RunOutputKind::Out),
              QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.out")));
}
