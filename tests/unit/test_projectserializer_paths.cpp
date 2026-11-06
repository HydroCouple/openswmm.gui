/**
 * @file      test_projectserializer_paths.cpp
 * @brief     Unit tests for the ProjectSerializer relative-path helpers
 *         (Slice AA-3.2).
 *
 * The helpers are inline statics in the header so this test compiles
 * against the public surface only — no link-time dependency on the
 * full ProjectSerializer .cpp (which pulls in MapCanvas, SWMMModelLayer
 * and the rest of the GUI graph).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include "project/projectserializer.h"

namespace
{

// Sample .oswp anchor — used for tests that don't need a real file.
/**
 *
 * @brief Construct a synthetic project anchor path without creating a file.
 * @param dir Borrowed test directory; an absolute temporary directory is supplied
 * by fixtures. Relative input follows QDir's current-directory resolution.
 * @return Owned absolute path to project.oswp under dir; no existence/permission check.
 *
 */
QString fakeOswp(const QString &dir)
{
    return QDir(dir).absoluteFilePath(QStringLiteral("project.oswp"));
}

} // anonymous

// ---------------------------------------------------------------------------
// toRelativePath
// ---------------------------------------------------------------------------

/**
 *
 * @brief Require empty model path to remain empty during relative-path conversion.
 *
 */
TEST(ProjectSerializerPaths, ToRelativeHandlesEmptyInput)
{
    EXPECT_TRUE(ProjectSerializer::toRelativePath({}, QStringLiteral("/x/y.oswp")).isEmpty());
}

/**
 *
 * @brief Store a sibling model reference as its basename.
 *
 */
TEST(ProjectSerializerPaths, ToRelativeForFileNextToOswp)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString oswp = fakeOswp(tmp.path());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));

    EXPECT_EQ(ProjectSerializer::toRelativePath(inp, oswp), QStringLiteral("model.inp"));
}

/**
 *
 * @brief Store a nested model as models/base.inp relative to project directory.
 *
 */
TEST(ProjectSerializerPaths, ToRelativeForFileInSubdir)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString oswp = fakeOswp(tmp.path());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("models/base.inp"));

    EXPECT_EQ(ProjectSerializer::toRelativePath(inp, oswp), QStringLiteral("models/base.inp"));
}

/**
 *
 * @brief Store a parent-directory model using one dot-dot component.
 *
 */
TEST(ProjectSerializerPaths, ToRelativeForFileInParentDir)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    QDir(tmp.path()).mkpath(QStringLiteral("project"));
    const QString oswp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("project/proj.oswp"));
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));

    EXPECT_EQ(ProjectSerializer::toRelativePath(inp, oswp), QStringLiteral("../model.inp"));
}

// ---------------------------------------------------------------------------
// resolveStoredPath
// ---------------------------------------------------------------------------

/**
 *
 * @brief Require empty stored reference to resolve to empty.
 *
 */
TEST(ProjectSerializerPaths, ResolveHandlesEmptyInput)
{
    EXPECT_TRUE(ProjectSerializer::resolveStoredPath({}, QStringLiteral("/x/y.oswp")).isEmpty());
}

/**
 *
 * @brief Require a legacy absolute stored reference to remain absolute and unchanged for this
 * clean path.
 *
 */
TEST(ProjectSerializerPaths, ResolveAbsolutePassesThroughCleaned)
{
    // v1/v2/v3 backward-compat: stored absolute paths must come back
    // unchanged (after cleanPath canonicalisation).
    const QString stored = QStringLiteral("/abs/path/to/model.inp");
    EXPECT_EQ(ProjectSerializer::resolveStoredPath(stored, QStringLiteral("/x/y.oswp")), stored);
}

/**
 *
 * @brief Resolve a stored basename under the project anchor directory.
 *
 */
TEST(ProjectSerializerPaths, ResolveRelativeAgainstOswpDir)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString oswp = fakeOswp(tmp.path());
    const QString resolved =
        ProjectSerializer::resolveStoredPath(QStringLiteral("model.inp"), oswp);

    EXPECT_EQ(resolved, QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp")));
}

/**
 *
 * @brief Resolve parent-directory stored reference against a nested project anchor.
 *
 */
TEST(ProjectSerializerPaths, ResolveRelativeWithDotDot)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    QDir(tmp.path()).mkpath(QStringLiteral("project"));
    const QString oswp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("project/proj.oswp"));
    const QString resolved =
        ProjectSerializer::resolveStoredPath(QStringLiteral("../model.inp"), oswp);

    EXPECT_EQ(resolved, QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp")));
}

// ---------------------------------------------------------------------------
// Round-trip
// ---------------------------------------------------------------------------

/**
 *
 * @brief Round-trip sibling absolute model path through relative storage and resolution.
 *
 */
TEST(ProjectSerializerPaths, RoundTripFileNextToOswp)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString oswp = fakeOswp(tmp.path());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));

    const QString relative = ProjectSerializer::toRelativePath(inp, oswp);
    const QString resolved = ProjectSerializer::resolveStoredPath(relative, oswp);

    EXPECT_EQ(resolved, inp);
}

/**
 *
 * @brief Require nested path storage to be relative and resolve back to exact absolute model
 * path.
 *
 */
TEST(ProjectSerializerPaths, RoundTripFileInSubdir)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    QDir(tmp.path()).mkpath(QStringLiteral("models"));
    const QString oswp = fakeOswp(tmp.path());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("models/base.inp"));

    const QString relative = ProjectSerializer::toRelativePath(inp, oswp);
    EXPECT_FALSE(QDir::isAbsolutePath(relative));

    const QString resolved = ProjectSerializer::resolveStoredPath(relative, oswp);
    EXPECT_EQ(resolved, inp);
}

/**
 *
 * @brief Simulate moving a project folder and require its relative model reference to follow the
 * new anchor.
 *
 */
TEST(ProjectSerializerPaths, RoundTripPortabilityAcrossMove)
{
    // The point of relative paths: move the project folder, references
    // still resolve.  Simulate by computing the relative path against
    // one .oswp location and resolving it against another (in a
    // different parent) — the resolved file should sit in the new dir.
    QTemporaryDir before, after;
    ASSERT_TRUE(before.isValid());
    ASSERT_TRUE(after.isValid());
    const QString oswpBefore = fakeOswp(before.path());
    const QString inpBefore = QDir(before.path()).absoluteFilePath(QStringLiteral("model.inp"));

    const QString relative = ProjectSerializer::toRelativePath(inpBefore, oswpBefore);

    // After "moving" the folder, the .oswp sits at a new path; the
    // stored relative reference should resolve to the file alongside
    // the new .oswp, not the old absolute location.
    const QString oswpAfter = fakeOswp(after.path());
    const QString resolved = ProjectSerializer::resolveStoredPath(relative, oswpAfter);

    EXPECT_EQ(resolved, QDir(after.path()).absoluteFilePath(QStringLiteral("model.inp")));
}
