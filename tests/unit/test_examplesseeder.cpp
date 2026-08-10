/*!
 * \file   test_examplesseeder.cpp
 * \brief  Unit tests for the bundled-example seeder/discovery
 *         (openswmmvis::project::examples) plus the curated Bellinge
 *         payload guard.
 *
 * Headless (Qt Core only). Filesystem fixtures live in QTemporaryDir.
 * BELLINGE_EXAMPLE_DIR is injected by tests/unit/CMakeLists.txt and points
 * at the committed examples/bellinge_2d payload so payload/curation
 * regressions fail the suite.
 */
#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "project/examplesseeder.h"
#include "project/projectserializer.h"

using namespace openswmmvis::project::examples;

namespace {

bool writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(content) == content.size();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

} // namespace

// ── copyDirectoryRecursively ────────────────────────────────────────────────

TEST(ExamplesSeeder, CopyRecursesNestedTree)
{
    QTemporaryDir tmp;
    const QString src = tmp.filePath("src");
    const QString dst = tmp.filePath("dst");
    ASSERT_TRUE(writeFile(src + "/a.inp", "A"));
    ASSERT_TRUE(writeFile(src + "/sub/deep/b.dat", "B"));

    QString err;
    ASSERT_TRUE(copyDirectoryRecursively(src, dst, &err)) << err.toStdString();
    EXPECT_EQ(readFile(dst + "/a.inp"), QByteArray("A"));
    EXPECT_EQ(readFile(dst + "/sub/deep/b.dat"), QByteArray("B"));
}

TEST(ExamplesSeeder, CopyMissingSourceFails)
{
    QTemporaryDir tmp;
    QString err;
    EXPECT_FALSE(copyDirectoryRecursively(tmp.filePath("nope"),
                                          tmp.filePath("dst"), &err));
    EXPECT_FALSE(err.isEmpty());
}

TEST(ExamplesSeeder, CopySkipsSeedMarker)
{
    QTemporaryDir tmp;
    const QString src = tmp.filePath("src");
    const QString dst = tmp.filePath("dst");
    ASSERT_TRUE(writeFile(src + "/a.inp", "A"));
    ASSERT_TRUE(writeFile(src + "/" + seedMarkerFileName(), "v1"));

    ASSERT_TRUE(copyDirectoryRecursively(src, dst, nullptr));
    EXPECT_TRUE(QFile::exists(dst + "/a.inp"));
    EXPECT_FALSE(QFile::exists(dst + "/" + seedMarkerFileName()));
}

TEST(ExamplesSeeder, CopyIsNoOpForUnchangedFiles)
{
    QTemporaryDir tmp;
    const QString src = tmp.filePath("src");
    const QString dst = tmp.filePath("dst");
    ASSERT_TRUE(writeFile(src + "/a.inp", "A"));
    ASSERT_TRUE(copyDirectoryRecursively(src, dst, nullptr));

    // Sentinel edit in the destination with SAME size + carry the source
    // mtime over — an unchanged-file no-op must leave it alone.
    ASSERT_TRUE(writeFile(dst + "/a.inp", "Z"));
    {
        QFile f(dst + "/a.inp");
        ASSERT_TRUE(f.open(QIODevice::ReadWrite));
        ASSERT_TRUE(f.setFileTime(QFileInfo(src + "/a.inp").lastModified(),
                                  QFileDevice::FileModificationTime));
    }
    ASSERT_TRUE(copyDirectoryRecursively(src, dst, nullptr));
    EXPECT_EQ(readFile(dst + "/a.inp"), QByteArray("Z"));  // untouched

    // A size change must trigger a re-copy.
    ASSERT_TRUE(writeFile(dst + "/a.inp", "ZZ"));
    ASSERT_TRUE(copyDirectoryRecursively(src, dst, nullptr));
    EXPECT_EQ(readFile(dst + "/a.inp"), QByteArray("A"));
}

// ── syncFromInstall ─────────────────────────────────────────────────────────

TEST(ExamplesSeeder, SyncSeedsAndWritesMarker)
{
    QTemporaryDir tmp;
    const QString src = tmp.filePath("install");
    const QString dst = tmp.filePath("appdata");
    ASSERT_TRUE(writeFile(src + "/ex/model.inp", "M"));

    QString err;
    ASSERT_TRUE(syncFromInstall(src, dst, "6.0.0", &err)) << err.toStdString();
    EXPECT_EQ(readFile(dst + "/ex/model.inp"), QByteArray("M"));
    EXPECT_EQ(readFile(dst + "/" + seedMarkerFileName()), QByteArray("6.0.0"));
}

TEST(ExamplesSeeder, SyncMatchingMarkerIsFastPath)
{
    QTemporaryDir tmp;
    const QString src = tmp.filePath("install");
    const QString dst = tmp.filePath("appdata");
    ASSERT_TRUE(writeFile(src + "/model.inp", "M"));
    ASSERT_TRUE(syncFromInstall(src, dst, "6.0.0", nullptr));

    // Delete a seeded file; a same-version sync must NOT restore it (the
    // marker short-circuits the walk entirely).
    ASSERT_TRUE(QFile::remove(dst + "/model.inp"));
    ASSERT_TRUE(syncFromInstall(src, dst, "6.0.0", nullptr));
    EXPECT_FALSE(QFile::exists(dst + "/model.inp"));

    // A version bump re-walks and restores it.
    ASSERT_TRUE(syncFromInstall(src, dst, "6.0.1", nullptr));
    EXPECT_EQ(readFile(dst + "/model.inp"), QByteArray("M"));
    EXPECT_EQ(readFile(dst + "/" + seedMarkerFileName()), QByteArray("6.0.1"));
}

#ifndef Q_OS_WIN
TEST(ExamplesSeeder, SyncUnwritableDestinationFails)
{
    QTemporaryDir tmp;
    const QString src = tmp.filePath("install");
    ASSERT_TRUE(writeFile(src + "/model.inp", "M"));
    const QString lockedParent = tmp.filePath("locked");
    ASSERT_TRUE(QDir().mkpath(lockedParent));
    ASSERT_TRUE(QFile::setPermissions(
        lockedParent, QFileDevice::ReadOwner | QFileDevice::ExeOwner));

    QString err;
    EXPECT_FALSE(syncFromInstall(src, lockedParent + "/examples", "6.0.0", &err));
    EXPECT_FALSE(err.isEmpty());

    QFile::setPermissions(lockedParent,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ExeOwner);  // let QTemporaryDir clean up
}
#endif

// ── discoverExamples ────────────────────────────────────────────────────────

TEST(ExamplesSeeder, DiscoveryFindsDirAndFlatExamples)
{
    QTemporaryDir tmp;
    const QString root = tmp.filePath("examples");
    // Dir example with BOTH .oswp and .inp → .oswp preferred, manifest name.
    ASSERT_TRUE(writeFile(root + "/big_model/model.inp", "I"));
    ASSERT_TRUE(writeFile(root + "/big_model/model.oswp", "{}"));
    ASSERT_TRUE(writeFile(root + "/big_model/example.json",
                          R"({"name":"Big Model","description":"Desc."})"));
    // Dir example with only .inp, no manifest → prettified dir name.
    ASSERT_TRUE(writeFile(root + "/inp_only_case/only.inp", "I"));
    // Empty subdir → ignored.
    ASSERT_TRUE(QDir().mkpath(root + "/empty_dir"));
    // Legacy flat example.
    ASSERT_TRUE(writeFile(root + "/road_culvert.inp", "I"));

    const QVector<ExampleInfo> found = discoverExamples(root);
    ASSERT_EQ(found.size(), 3);

    const ExampleInfo &big = found[0];   // subdirs sorted first
    EXPECT_TRUE(big.isDirectory);
    EXPECT_EQ(big.displayName, QStringLiteral("Big Model"));
    EXPECT_EQ(big.description, QStringLiteral("Desc."));
    EXPECT_TRUE(big.openPath.endsWith(QStringLiteral("model.oswp")));
    EXPECT_EQ(big.sourceRoot, QDir(root + "/big_model").absolutePath());

    const ExampleInfo &inpOnly = found[1];
    EXPECT_TRUE(inpOnly.isDirectory);
    EXPECT_EQ(inpOnly.displayName, QStringLiteral("inp only case"));
    EXPECT_TRUE(inpOnly.openPath.endsWith(QStringLiteral("only.inp")));

    const ExampleInfo &flat = found[2];
    EXPECT_FALSE(flat.isDirectory);
    EXPECT_EQ(flat.displayName, QStringLiteral("road culvert"));
    EXPECT_EQ(flat.sourceRoot, flat.openPath);
}

TEST(ExamplesSeeder, DiscoveryOfMissingDirIsEmpty)
{
    EXPECT_TRUE(discoverExamples(QStringLiteral("/no/such/dir")).isEmpty());
}

// ── Curated Bellinge payload guard ──────────────────────────────────────────

TEST(BellingeExample, CuratedPayloadIsSelfContained)
{
    const QString dir = QStringLiteral(BELLINGE_EXAMPLE_DIR);
    ASSERT_TRUE(QFileInfo(dir).isDir()) << dir.toStdString();

    const QString oswp =
        dir + QStringLiteral("/BellingeSWMM_v021_nopervious.oswp");
    QFile f(oswp);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QJsonParseError parseErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
    ASSERT_EQ(parseErr.error, QJsonParseError::NoError);

    const QJsonArray sessions = doc.object().value("sessions").toArray();
    ASSERT_EQ(sessions.size(), 1);
    const QJsonObject s = sessions[0].toObject();

    // The curation contract: no references to results that don't ship.
    EXPECT_FALSE(s.contains(QStringLiteral("resultLayers")));
    EXPECT_FALSE(s.contains(QStringLiteral("results2DLayers")));
    EXPECT_FALSE(s.contains(QStringLiteral("resultLayerSublayers")));

    // Every remaining stored path resolves to a file that ships.
    const QString inpPath = ProjectSerializer::resolveStoredPath(
        s.value(QStringLiteral("inpPath")).toString(), oswp);
    EXPECT_TRUE(QFile::exists(inpPath)) << inpPath.toStdString();

    for (const QJsonValue &m : s.value(QStringLiteral("meshLayers")).toArray()) {
        const QString mesh = ProjectSerializer::resolveStoredPath(
            m.toObject().value(QStringLiteral("sourcePath")).toString(), oswp);
        EXPECT_TRUE(QFile::exists(mesh)) << mesh.toStdString();
    }

    // Manifest present with a display name.
    QFile manifest(dir + QStringLiteral("/example.json"));
    ASSERT_TRUE(manifest.open(QIODevice::ReadOnly));
    EXPECT_FALSE(QJsonDocument::fromJson(manifest.readAll())
                     .object().value(QStringLiteral("name"))
                     .toString().isEmpty());

    // Excluded-by-design artifacts must never sneak into the payload.
    const QDir d(dir);
    EXPECT_TRUE(d.entryList({QStringLiteral("*.ovr"), QStringLiteral("*.out"),
                             QStringLiteral("*.rpt"), QStringLiteral("*.2d.h5")},
                            QDir::Files).isEmpty());
}
