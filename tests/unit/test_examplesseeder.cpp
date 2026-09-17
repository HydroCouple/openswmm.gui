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
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

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

TEST(ExamplesSeeder, SyncAlwaysWalksEvenWhenMarkerMatches)
{
    // Contract change (2026-09-13): there is no marker-equals-version fast
    // path any more. A same-version sync re-walks the payload, so a deleted
    // mirror file IS restored and the marker still records the version.
    QTemporaryDir tmp;
    const QString src = tmp.filePath("install");
    const QString dst = tmp.filePath("appdata");
    ASSERT_TRUE(writeFile(src + "/model.inp", "M"));
    ASSERT_TRUE(syncFromInstall(src, dst, "6.0.0", nullptr));

    ASSERT_TRUE(QFile::remove(dst + "/model.inp"));
    ASSERT_TRUE(syncFromInstall(src, dst, "6.0.0", nullptr));
    EXPECT_EQ(readFile(dst + "/model.inp"), QByteArray("M"));
    EXPECT_EQ(readFile(dst + "/" + seedMarkerFileName()), QByteArray("6.0.0"));
}

// The reported bug: the payload gained example directories without a version
// bump, and the mirror the Welcome page scans never received them because the
// marker already matched the version. Seed at V, add a directory to the
// payload, sync again at the SAME V — the new directory must appear.
TEST(ExamplesSeeder, SyncPicksUpPayloadAddedWithinSameVersion)
{
    QTemporaryDir tmp;
    const QString src = tmp.filePath("install");
    const QString dst = tmp.filePath("appdata");
    ASSERT_TRUE(writeFile(src + "/site_drainage_model.inp", "OLD"));
    ASSERT_TRUE(syncFromInstall(src, dst, "6.0.0", nullptr));
    ASSERT_FALSE(QDir(dst + "/swashes_bump_shock").exists());

    ASSERT_TRUE(writeFile(src + "/swashes_bump_shock/1d_dynwave.inp", "NEW"));
    ASSERT_TRUE(writeFile(src + "/swashes_bump_shock/example.json",
                          R"({"name":"Bump","category":"SWASHES"})"));

    QString err;
    ASSERT_TRUE(syncFromInstall(src, dst, "6.0.0", &err)) << err.toStdString();
    EXPECT_EQ(readFile(dst + "/swashes_bump_shock/1d_dynwave.inp"), QByteArray("NEW"));
    EXPECT_EQ(readFile(dst + "/site_drainage_model.inp"), QByteArray("OLD"));

    // And the Welcome page's discovery over the mirror now lists it.
    const QVector<ExampleInfo> found = discoverExamples(dst);
    bool listed = false;
    for (const ExampleInfo &info : found)
        listed = listed || (info.isDirectory && info.category == QStringLiteral("SWASHES"));
    EXPECT_TRUE(listed);
}

// Startup-cost guard for dropping the fast path (GUI_LOAD_PERF_REVIEW treats
// Welcome-page startup as a live concern): a steady-state sync of the REAL
// bundled payload — every file already mirrored — must stay cheap. Written
// to a reviewable dir under tests/output, never a temp dir.
TEST(ExamplesSeeder, SteadyStateSyncOfRealPayloadIsCheap)
{
    const QString src = QStringLiteral(EXAMPLES_SOURCE_DIR);
    ASSERT_TRUE(QDir(src).exists()) << EXAMPLES_SOURCE_DIR;
    QDir out(src); out.cdUp();                                  // <repo>
    const QString dst = out.filePath(
        QStringLiteral("tests/output/examplesseeder/steady_state_mirror"));
    QDir(dst).removeRecursively();

    QString err;
    ASSERT_TRUE(syncFromInstall(src, dst, "test", &err)) << err.toStdString();  // cold: copies

    QElapsedTimer t; t.start();
    ASSERT_TRUE(syncFromInstall(src, dst, "test", &err)) << err.toStdString();  // warm: stats only
    const qint64 warmMs = t.elapsed();
    RecordProperty("steady_state_sync_ms", static_cast<int>(warmMs));
    std::printf("[  INFO    ] steady-state sync of %s: %lld ms\n",
                EXAMPLES_SOURCE_DIR, static_cast<long long>(warmMs));
    EXPECT_LT(warmMs, 250) << "a no-op re-seed should be one stat per file";

    // The warm walk touched nothing: a mirrored file keeps the payload mtime.
    // (EXAMPLES_SOURCE_DIR is the uncurated repo tree; anchor on a SWASHES
    // deck, which is in both it and the bundle.)
    const QString rel = QStringLiteral("/swashes_bump_shock/1d_dynwave.inp");
    const QFileInfo a(src + rel), b(dst + rel);
    ASSERT_TRUE(a.exists() && b.exists()) << rel.toStdString();
    EXPECT_EQ(a.lastModified(), b.lastModified());
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
                          R"({"name":"Big Model","description":"Desc.",)"
                          R"("category":"Tutorials"})"));
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
    EXPECT_EQ(big.category, QStringLiteral("Tutorials"));
    EXPECT_TRUE(big.openPath.endsWith(QStringLiteral("model.oswp")));
    EXPECT_EQ(big.sourceRoot, QDir(root + "/big_model").absolutePath());

    // No manifest → no category, so the Welcome panel's default group owns it.
    const ExampleInfo &inpOnly = found[1];
    EXPECT_TRUE(inpOnly.isDirectory);
    EXPECT_EQ(inpOnly.displayName, QStringLiteral("inp only case"));
    EXPECT_TRUE(inpOnly.category.isEmpty());
    EXPECT_TRUE(inpOnly.openPath.endsWith(QStringLiteral("only.inp")));

    const ExampleInfo &flat = found[2];
    EXPECT_FALSE(flat.isDirectory);
    EXPECT_EQ(flat.displayName, QStringLiteral("road culvert"));
    EXPECT_TRUE(flat.category.isEmpty());
    EXPECT_EQ(flat.sourceRoot, flat.openPath);
}

TEST(ExamplesSeeder, DiscoveryOpensFirstInpByName)
{
    // The SWASHES payloads ship several solver decks side by side and rely on
    // this ordering so 1d_dynwave.inp is the default open target.
    QTemporaryDir tmp;
    const QString root = tmp.filePath("examples");
    ASSERT_TRUE(writeFile(root + "/case/2d_explicit.inp", "I"));
    ASSERT_TRUE(writeFile(root + "/case/1d_fv.inp", "I"));
    ASSERT_TRUE(writeFile(root + "/case/1d_dynwave.inp", "I"));

    const QVector<ExampleInfo> found = discoverExamples(root);
    ASSERT_EQ(found.size(), 1);
    EXPECT_TRUE(found[0].openPath.endsWith(QStringLiteral("1d_dynwave.inp")));
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

// ── Curated SWASHES payload guard ───────────────────────────────────────────
// The 10 analytical-verification cases imported by
// scripts/import_swashes_examples.py. Input files only: the QA suite that
// generates these decks writes .rpt/.out/surface.h5/extracted.csv beside them,
// and none of that may ride along into the shipped payload.

TEST(SwashesExamples, CuratedPayloadIsInputOnly)
{
    const QDir examples(QStringLiteral(EXAMPLES_SOURCE_DIR));
    ASSERT_TRUE(examples.exists()) << EXAMPLES_SOURCE_DIR;

    const QStringList cases =
        examples.entryList({QStringLiteral("swashes_*")}, QDir::Dirs, QDir::Name);
    ASSERT_EQ(cases.size(), 10) << "expected the 10 curated SWASHES cases";

    for (const QString &name : cases) {
        const QDir d(examples.absoluteFilePath(name));
        SCOPED_TRACE(name.toStdString());

        // Every shipped file is an input the GUI or the user reads.
        const QStringList allowed{
            QStringLiteral("1d_dynwave.inp"), QStringLiteral("1d_fv.inp"),
            QStringLiteral("2d_explicit.inp"), QStringLiteral("reference.csv"),
            QStringLiteral("example.json"), QStringLiteral("README.md")};
        const QStringList files =
            d.entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
        for (const QString &f : files)
            EXPECT_TRUE(allowed.contains(f)) << f.toStdString();
        EXPECT_TRUE(d.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());

        // At least one deck, and the analytic reference it is compared against.
        EXPECT_FALSE(d.entryList({QStringLiteral("*.inp")}, QDir::Files).isEmpty());
        EXPECT_TRUE(QFile::exists(d.absoluteFilePath(QStringLiteral("reference.csv"))));

        // Decks must stay self-contained — the Welcome page copies the folder
        // elsewhere before opening, so an absolute path or an unshipped
        // sidecar reference would break the copy.
        for (const QString &deck : d.entryList({QStringLiteral("*.inp")}, QDir::Files)) {
            QFile f(d.absoluteFilePath(deck));
            ASSERT_TRUE(f.open(QIODevice::ReadOnly)) << deck.toStdString();
            const QString text = QString::fromUtf8(f.readAll());
            EXPECT_FALSE(text.contains(QStringLiteral("[FILES]")))
                << deck.toStdString();
            EXPECT_FALSE(text.contains(QStringLiteral("/Users/")))
                << deck.toStdString();
        }

        // Manifest carries the display name and the shared category that
        // groups these under their own Welcome-page header.
        QFile manifest(d.absoluteFilePath(QStringLiteral("example.json")));
        ASSERT_TRUE(manifest.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(manifest.readAll()).object();
        EXPECT_FALSE(o.value(QStringLiteral("name")).toString().isEmpty());
        EXPECT_FALSE(o.value(QStringLiteral("description")).toString().isEmpty());
        EXPECT_EQ(o.value(QStringLiteral("category")).toString(),
                  QStringLiteral("Analytical Verification (SWASHES)"));
    }
}

// ── One coordinate frame per case ───────────────────────────────────────────
//
// Every deck of a SWASHES case shares one plan frame so that opening the 1D
// and 2D decks shows the channel in the same place: the centreline is y = 0,
// nodes sit at their chainage x_i = i*dx from 0, the 2D strip is centred on
// y = 0, and anything sacrificial (the closed-basin spillway OUTF, the dry
// dummy pair JD1/OD1 in the 2D decks) sits past the outlet on the centreline
// with conduit Length equal to plan distance. Before this gate the 1D chains
// ran along the 2D strip's bottom wall (y = 0 vs a strip on [0, W]).

namespace {

struct XY { double x = 0, y = 0; };

//! Parses `[SECTION]` rows of an .inp into token lists (comments/blank skipped).
QVector<QStringList> sectionRows(const QString &text, const QString &section)
{
    QVector<QStringList> rows;
    bool in = false;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) { in = (line == section); continue; }
        if (!in || line.isEmpty() || line.startsWith(QLatin1Char(';'))) continue;
        rows.append(line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts));
    }
    return rows;
}

} // namespace

TEST(SwashesExamples, DecksShareOneFrame)
{
    const QDir examples(QStringLiteral(EXAMPLES_SOURCE_DIR));
    ASSERT_TRUE(examples.exists()) << EXAMPLES_SOURCE_DIR;
    int checked1d = 0, checked2d = 0;

    for (const QString &name : examples.entryList({QStringLiteral("swashes_*")}, QDir::Dirs, QDir::Name)) {
        const QDir d(examples.absoluteFilePath(name));
        SCOPED_TRACE(name.toStdString());

        // 1D decks: a straight chain along +x on y = 0, starting at 0.
        for (const QString &deck : {QStringLiteral("1d_dynwave.inp"), QStringLiteral("1d_fv.inp")}) {
            if (!QFile::exists(d.absoluteFilePath(deck))) continue;
            SCOPED_TRACE(deck.toStdString());
            const QString text = QString::fromUtf8(readFile(d.absoluteFilePath(deck)));
            const auto coords = sectionRows(text, QStringLiteral("[COORDINATES]"));
            ASSERT_GE(coords.size(), 3);
            double prevX = -1.0;
            for (const QStringList &r : coords) {
                ASSERT_EQ(r.size(), 3) << r.join(' ').toStdString();
                const double x = r[1].toDouble(), y = r[2].toDouble();
                EXPECT_DOUBLE_EQ(y, 0.0) << r[0].toStdString();
                EXPECT_GE(x, prevX) << r[0].toStdString();          // written in chainage order
                prevX = x;
            }
            EXPECT_DOUBLE_EQ(coords.first()[1].toDouble(), 0.0);   // starts at the origin
            // Every conduit's Length equals its plan distance (straight chain).
            QMap<QString, XY> at;
            for (const QStringList &r : coords) at[r[0]] = {r[1].toDouble(), r[2].toDouble()};
            for (const QStringList &c : sectionRows(text, QStringLiteral("[CONDUITS]"))) {
                ASSERT_TRUE(at.contains(c[1]) && at.contains(c[2])) << c[0].toStdString();
                const double plan = std::hypot(at[c[2]].x - at[c[1]].x, at[c[2]].y - at[c[1]].y);
                EXPECT_NEAR(c[3].toDouble(), plan, 1e-6) << c[0].toStdString();
            }
            ++checked1d;
        }

        // 2D deck: strip centred on y = 0, x from 0; dummy pair past the outlet.
        if (QFile::exists(d.absoluteFilePath(QStringLiteral("2d_explicit.inp")))) {
            const QString text = QString::fromUtf8(readFile(d.absoluteFilePath(QStringLiteral("2d_explicit.inp"))));
            const auto verts = sectionRows(text, QStringLiteral("[2D_VERTICES]"));
            ASSERT_GE(verts.size(), 4);
            double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
            for (const QStringList &v : verts) {
                ASSERT_EQ(v.size(), 3) << v.join(' ').toStdString();
                const double x = v[0].toDouble(), y = v[1].toDouble();
                xmin = std::min(xmin, x); xmax = std::max(xmax, x);
                ymin = std::min(ymin, y); ymax = std::max(ymax, y);
            }
            EXPECT_DOUBLE_EQ(xmin, 0.0);
            EXPECT_NEAR(ymin, -ymax, 1e-9) << "strip not centred on y = 0";
            EXPECT_GT(ymax, 0.0);

            QMap<QString, XY> at;
            for (const QStringList &r : sectionRows(text, QStringLiteral("[COORDINATES]")))
                at[r[0]] = {r[1].toDouble(), r[2].toDouble()};
            ASSERT_TRUE(at.contains(QStringLiteral("JD1")) && at.contains(QStringLiteral("OD1")));
            for (const char *n : {"JD1", "OD1"}) {
                EXPECT_DOUBLE_EQ(at[QLatin1String(n)].y, 0.0) << n;
                EXPECT_GT(at[QLatin1String(n)].x, xmax) << n << " must sit past the outlet";
            }
            bool sawCd1 = false;
            for (const QStringList &c : sectionRows(text, QStringLiteral("[CONDUITS]"))) {
                if (c[0] != QStringLiteral("CD1")) continue;
                sawCd1 = true;
                EXPECT_NEAR(c[3].toDouble(), at[QStringLiteral("OD1")].x - at[QStringLiteral("JD1")].x, 1e-6)
                    << "CD1 Length must equal its plan distance";
            }
            EXPECT_TRUE(sawCd1);
            ++checked2d;
        }
    }
    // Guard against a silently empty loop.
    EXPECT_GE(checked1d, 2);
    EXPECT_GE(checked2d, 1);
}
