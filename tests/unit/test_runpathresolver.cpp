/**
 * @file      test_runpathresolver.cpp
 * @brief     Unit tests for openswmmvis::resolveRunOutputPath (Slice QB.5).
 *
 * The pure resolver function is exercised here; the QSettings-driven
 * wrapper (resolveRunOutputPathFromSettings) is a thin lookup-then-dispatch
 * adapter and is left for higher-level GUI tests once a QSettings sandbox
 * fixture is available.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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

/**
 *
 * @brief Require missing report override to resolve to model.rpt beside the input.
 *
 */
TEST(RunPathResolver, SiblingDefaultRptWhenNoOverride)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString expected = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.rpt"));
    EXPECT_EQ(resolveRunOutputPath(inp, QString(), RunOutputKind::Rpt), expected);
}

/**
 *
 * @brief Require missing output override to resolve to model.out beside the input.
 *
 */
TEST(RunPathResolver, SiblingDefaultOutWhenNoOverride)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString expected = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.out"));
    EXPECT_EQ(resolveRunOutputPath(inp, QString(), RunOutputKind::Out), expected);
}

/**
 *
 * @brief Require empty model input to yield empty output path even with an override.
 *
 */
TEST(RunPathResolver, EmptyInputReturnsEmpty)
{
    EXPECT_TRUE(resolveRunOutputPath(QString(), QStringLiteral("/whatever.rpt"), RunOutputKind::Rpt)
                    .isEmpty());
}

/**
 *
 * @brief Require whitespace-only override to select the sibling report default.
 *
 */
TEST(RunPathResolver, WhitespaceOverrideTreatedAsEmpty)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString expected = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.rpt"));
    EXPECT_EQ(resolveRunOutputPath(inp, QStringLiteral("   "), RunOutputKind::Rpt), expected);
}

// ---------------------------------------------------------------------------
// Override precedence
// ---------------------------------------------------------------------------

/**
 *
 * @brief Require an absolute override to pass through QDir path cleaning.
 *
 */
TEST(RunPathResolver, AbsoluteOverridePassesThrough)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString override_ = QStringLiteral("/some/absolute/results.out");
    EXPECT_EQ(resolveRunOutputPath(inp, override_, RunOutputKind::Out), QDir::cleanPath(override_));
}

/**
 *
 * @brief Resolve relative override beneath the input directory rather than process working
 * directory.
 *
 */
TEST(RunPathResolver, RelativeOverrideResolvedAgainstInpDir)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString override_ = QStringLiteral("results/custom.out");
    const QString expected =
        QDir::cleanPath(QDir(tmp.path()).absoluteFilePath(QStringLiteral("results/custom.out")));
    EXPECT_EQ(resolveRunOutputPath(inp, override_, RunOutputKind::Out), expected);
}

/**
 *
 * @brief Resolve parent-directory components against a nested input directory and clean the
 * result.
 *
 */
TEST(RunPathResolver, DotRelativeOverrideResolved)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("sub/model.inp"));
    QDir(tmp.path()).mkpath(QStringLiteral("sub"));
    const QString override_ = QStringLiteral("../out/custom.out");
    const QString expected =
        QDir::cleanPath(QDir(tmp.path()).absoluteFilePath(QStringLiteral("out/custom.out")));
    EXPECT_EQ(resolveRunOutputPath(inp, override_, RunOutputKind::Out), expected);
}

/**
 *
 * @brief Apply explicit report destination while preserving independent sibling output fallback.
 *
 */
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

// ---------------------------------------------------------------------------
// moveProjectSettings — tutorial bug report (2026-10-17): a new project saved
// over an old project's path inherited the old project's per-path settings
// (2D module intent, report/output overrides).
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QSettings>

namespace
{

/**
 *
 * @brief Select and clear the compile-time test QSettings sandbox.
 * Requires SWMMVIS_TEST_SETTINGS_DIR to name an isolated writable test directory.
 * Creates it (ignoring mkdir status), sets process-wide INI/UserScope search path
 * and test organization/application names, then clears that store. Does not restore
 * previous settings defaults; callers serialize and never point it at user data.
 *
 */
void useSettingsSandbox()
{
    QDir().mkpath(QStringLiteral(SWMMVIS_TEST_SETTINGS_DIR));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral(SWMMVIS_TEST_SETTINGS_DIR));
    QCoreApplication::setOrganizationName(QStringLiteral("SWMMVisUnitTest"));
    QCoreApplication::setApplicationName(QStringLiteral("test_runpathresolver"));
    QSettings().clear();
}

/**
 *
 * @brief Read one project-key value from the current test settings store.
 * @param path Borrowed project path inserted verbatim into SWMMVis/Project/path/key.
 * @param key Borrowed suffix, no normalization/escaping.
 * @return Copied QVariant or invalid if absent. Requires useSettingsSandbox before
 * use to avoid real user settings; no store write, ownership transfer or validation.
 *
 */
QVariant projectValue(const QString &path, const QString &key)
{
    return QSettings().value(QStringLiteral("SWMMVis/Project/%1/%2").arg(path, key));
}

} // namespace

/**
 *
 * @brief Clear stale module/output keys at a reused path when saving an untitled project.
 * Uses and clears the isolated process-wide settings sandbox.
 *
 */
TEST(MoveProjectSettings, UntitledSaveClearsAnOlderProjectsKeys)
{
    useSettingsSandbox();
    const QString target = QStringLiteral("/p/model.inp");
    {
        QSettings s;
        s.setValue(QStringLiteral("SWMMVis/Project/%1/Module2DEnabled").arg(target), true);
        s.setValue(QStringLiteral("SWMMVis/Project/%1/OutputFilePath").arg(target),
                   QStringLiteral("/old/run.out"));
    }
    openswmmvis::moveProjectSettings(QString(), target);
    EXPECT_FALSE(projectValue(target, QStringLiteral("Module2DEnabled")).isValid());
    EXPECT_FALSE(projectValue(target, QStringLiteral("OutputFilePath")).isValid());
}

/**
 *
 * @brief Require rename to copy current keys, replace stale destination keys and retain source
 * settings.
 * Uses the isolated test QSettings sandbox and does not restore its global selection.
 *
 */
TEST(MoveProjectSettings, RenameCarriesCurrentKeysAndReplacesStaleOnes)
{
    useSettingsSandbox();
    const QString from = QStringLiteral("/p/current.inp");
    const QString to = QStringLiteral("/p/model.inp");
    {
        QSettings s;
        s.setValue(QStringLiteral("SWMMVis/Project/%1/ReportFilePath").arg(from),
                   QStringLiteral("/p/current.rpt"));
        s.setValue(QStringLiteral("SWMMVis/Project/%1/Module2DEnabled").arg(to), true);
    }
    openswmmvis::moveProjectSettings(from, to);
    EXPECT_EQ(projectValue(to, QStringLiteral("ReportFilePath")).toString(),
              QStringLiteral("/p/current.rpt"));
    EXPECT_FALSE(projectValue(to, QStringLiteral("Module2DEnabled")).isValid());
    // The source project may still exist on disk; its keys stay.
    EXPECT_EQ(projectValue(from, QStringLiteral("ReportFilePath")).toString(),
              QStringLiteral("/p/current.rpt"));
}
