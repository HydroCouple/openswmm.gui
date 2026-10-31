/**
 * @file      test_rptparser.cpp
 * @brief     Unit tests for openswmmvis::io::RptParser.
 *
 * Real SWMM .rpt section headers are indented and often carry table
 * column headings to the right of the star rules and the title, e.g.:
 *
 *   **************************        Volume         Depth
 *   Runoff Quantity Continuity     acre-feet        inches
 *   **************************     ---------       -------
 *
 * These tests run the parser over a real engine-written report
 * (data/weir_culvert.rpt, copied from examples/demo_weir_culvert) and
 * assert that every starred section lands in the section list.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include "io/rptparser.h"

using openswmmvis::io::RptParser;
using openswmmvis::io::RptSection;

namespace
{

// CTest runs with WORKING_DIRECTORY tests/unit/data.
// Both fixtures are bare filenames resolved against this test's
// WORKING_DIRECTORY (tests/unit/data — see tests/unit/CMakeLists.txt).
//
// kTwoDFixture previously read "../../gui/data/output_simstatus2derr/mini_2d.rpt",
// climbing out of this test's own data dir into the *gui* test tree. That
// directory is a scratch/debug dir and was never committed, so the file existed
// only on machines that had run the model locally: the test passed for devs and
// failed on every fresh clone (i.e. always, in CI). Fixture now lives next to
// weir_culvert.rpt, tracked in git, addressed the same way.
const QString kFixture = QStringLiteral("weir_culvert.rpt");
const QString kTwoDFixture = QStringLiteral("mini_2d.rpt");

/**
 *
 * @brief Collect parsed report section titles without normalization.
 * @param sections Borrowed ordered parsed sections.
 * @return Owned title copies in the same order, retaining empty/duplicate strings;
 * no parser invocation or input mutation.
 *
 */
QStringList titlesOf(const QVector<RptSection> &sections)
{
    QStringList t;
    for (const auto &s : sections)
        t << s.title;
    return t;
}

/**
 *
 * @brief Concatenate parsed report bodies for source-preservation assertions.
 * @param sections Borrowed ordered parsed sections.
 * @return Owned concatenation without extra separators; relies on each body retaining
 * its own line breaks. Titles are excluded and no parsing/normalization occurs.
 *
 */
QString joinedBodies(const QVector<RptSection> &sections)
{
    QString text;
    for (const auto &s : sections)
        text += s.body;
    return text;
}

/**
 *
 * @brief Read a reference report with canonical LF line endings.
 * @param path Borrowed UTF-8 report filename opened read-only in binary mode.
 * @return Owned decoded text with CRLF/lone CR converted to LF and a final LF added
 * if absent. Failed open returns empty; an empty successfully opened file becomes
 * one LF. Read errors are unchecked and the file is never changed.
 *
 */
QString normalizedReportText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    QString text = QString::fromUtf8(f.readAll());
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (!text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    return text;
}

} // namespace

TEST(RptParser, ParsesRealReportIntoSections)
{
    QString err;
    const auto sections = RptParser::parse(kFixture, &err);
    ASSERT_FALSE(sections.isEmpty()) << err.toStdString();

    const QStringList titles = titlesOf(sections);

    // Simple "stars / title / stars" headers.
    EXPECT_TRUE(titles.contains(QStringLiteral("Analysis Options")));
    EXPECT_TRUE(titles.contains(QStringLiteral("Routing Time Step Summary")));
    EXPECT_TRUE(titles.contains(QStringLiteral("Node Depth Summary")));
    EXPECT_TRUE(titles.contains(QStringLiteral("Link Flow Summary")));

    // Headers whose star / title lines carry trailing column headings
    // ("Volume / Depth", "acre-feet / inches") should expose the section
    // title without the unit columns.
    EXPECT_TRUE(titles.contains(QStringLiteral("Runoff Quantity Continuity")));
    EXPECT_TRUE(titles.contains(QStringLiteral("Flow Routing Continuity")));
}

TEST(RptParser, PreambleBecomesLeadingUntitledSection)
{
    const auto sections = RptParser::parse(kFixture, nullptr);
    ASSERT_FALSE(sections.isEmpty());
    // The engine banner / project notes precede the first starred header.
    EXPECT_TRUE(sections.first().title.isEmpty());
    EXPECT_TRUE(sections.first().body.contains(QStringLiteral("OPENSWMM ENGINE")));
}

TEST(RptParser, BodyKeepsColumnHeadingsFromHeaderBlock)
{
    const auto sections = RptParser::parse(kFixture, nullptr);
    for (const auto &s : sections) {
        if (s.title == QStringLiteral("Runoff Quantity Continuity")) {
            // The units columns live on the demarcation lines — they must
            // survive into the body so the viewer doesn't lose them.
            EXPECT_TRUE(s.body.contains(QStringLiteral("acre-feet")));
            EXPECT_TRUE(s.body.contains(QStringLiteral("Total Precipitation")));
            return;
        }
    }
    FAIL() << "Runoff Quantity Continuity section not found";
}

TEST(RptParser, Long2DContinuityTitleIsNotColumnTruncated)
{
    const auto sections = RptParser::parse(kTwoDFixture, nullptr);
    ASSERT_FALSE(sections.isEmpty());
    const QStringList titles = titlesOf(sections);

    EXPECT_TRUE(titles.contains(QStringLiteral("2D Surface Routing Continuity")));
    EXPECT_FALSE(titles.contains(QStringLiteral("2D Surface Routing Continui")));
}

TEST(RptParser, Long2DContinuitySectionKeepsFullBody)
{
    const auto sections = RptParser::parse(kTwoDFixture, nullptr);
    ASSERT_FALSE(sections.isEmpty());
    for (const auto &s : sections) {
        if (s.title == QStringLiteral("2D Surface Routing Continuity")) {
            EXPECT_TRUE(s.body.contains(QStringLiteral("Boundary Inflow")));
            EXPECT_TRUE(s.body.contains(QStringLiteral("Boundary Outflow")));
            EXPECT_TRUE(s.body.contains(QStringLiteral("Continuity Error (%)")));
            return;
        }
    }
    FAIL() << "2D Surface Routing Continuity section not found";
}

TEST(RptParser, ParsedBodiesPreserveComplete2DReportText)
{
    const auto sections = RptParser::parse(kTwoDFixture, nullptr);
    ASSERT_FALSE(sections.isEmpty());

    const QString expected = normalizedReportText(kTwoDFixture);
    ASSERT_FALSE(expected.isEmpty());

    EXPECT_EQ(joinedBodies(sections), expected);
}

TEST(RptParser, ParsedBodiesPreserveCompleteReportText)
{
    const auto sections = RptParser::parse(kFixture, nullptr);
    ASSERT_FALSE(sections.isEmpty());

    const QString expected = normalizedReportText(kFixture);
    ASSERT_FALSE(expected.isEmpty());

    EXPECT_EQ(joinedBodies(sections), expected);
}

TEST(RptParser, LinkAndConduitSummariesKeepDetails)
{
    const auto sections = RptParser::parse(kFixture, nullptr);
    ASSERT_FALSE(sections.isEmpty());

    bool sawLinkFlow = false;
    bool sawConduitSurcharge = false;
    for (const auto &s : sections) {
        if (s.title == QStringLiteral("Link Flow Summary")) {
            sawLinkFlow = true;
            EXPECT_TRUE(s.body.contains(QStringLiteral("C_PIPE_OUT")));
            EXPECT_TRUE(s.body.contains(QStringLiteral("Maximum  Time of Max")));
        } else if (s.title == QStringLiteral("Conduit Surcharge Summary")) {
            sawConduitSurcharge = true;
            EXPECT_TRUE(s.body.contains(QStringLiteral("No conduits were surcharged.")));
        }
    }

    EXPECT_TRUE(sawLinkFlow);
    EXPECT_TRUE(sawConduitSurcharge);
}

TEST(RptParser, ContinuityErrorScanStillWorks)
{
    const auto sections = RptParser::parse(kFixture, nullptr);
    // weir_culvert.rpt reports 0.000 % continuity errors.
    EXPECT_FALSE(RptParser::hasHighContinuityError(sections));
    EXPECT_TRUE(RptParser::hasHighContinuityError(sections, /*thresholdPct=*/-1.0));
}

TEST(RptParser, MissingFileReturnsEmptyWithError)
{
    QString err;
    const auto sections = RptParser::parse(QStringLiteral("does_not_exist.rpt"), &err);
    EXPECT_TRUE(sections.isEmpty());
    EXPECT_FALSE(err.isEmpty());
}

namespace
{
// Phase 4A R8 (2026-10-30): the engine writes one "2D Aquifer Quality
// Continuity" block per species, naming the species on the opening star rule,
// and a "Quality Routing Continuity" block with one column per species. A
// per-species "n/a" error is followed by a block whose lines carry digits.
const char *kSpeciesBlocksReport = R"RPT(
  *************************
  Analysis Options
  *************************
  Flow Units ............... CMS


  **************************        Volume         Depth
  Runoff Quantity Continuity     hectare-m            mm
  **************************     ---------       -------
  Total Precipitation ......       154.561       571.430
  Continuity Error (%) .....        -0.437


  **************************      SurfPoll
  2D Aquifer Quality Continuity       mass
  **************************    ----------
  Initial Stored Mass ......        0.0000
  Final Stored Mass ........   136338.5348
  Continuity Error (%) .....         0.000


  **************************      GWTracer
  2D Aquifer Quality Continuity       mass
  **************************    ----------
  Initial Stored Mass ......        0.0000
  Final Stored Mass ........     1437.0000
  Continuity Error (%) .....        15.200


  **************************      __WATER_AGE__
  2D Aquifer Quality Continuity       mass
  **************************    ----------
  Initial Stored Mass ......        0.0000
  Continuity Error (%) .....           n/a


  *************************
  2D Solver Statistics
  *************************
  Internal Steps ...........        139262


  **************************      SurfPoll      GWTracer
  Quality Routing Continuity            kg            kg
  **************************    ----------    ----------
  External Inflow ..........        21.853         0.000
  Continuity Error (%) .....        12.463         0.000
)RPT";

/**
 * @brief Write the per-species report fixture to the reviewable Phase 4A folder.
 * @return Absolute path of the written report, or empty when it cannot be written.
 */
QString writeSpeciesBlocksReport()
{
    const QString dir = QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath(
        QStringLiteral("../output/gw_phase4a_2026-10-30/report_viewer"));
    if (!QDir().mkpath(dir))
        return {};
    const QString path = dir + QStringLiteral("/species_blocks.rpt");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    const QByteArray bytes(kSpeciesBlocksReport);
    return f.write(bytes) == bytes.size() ? path : QString();
}
} // namespace

TEST(RptParser, PerSpeciesAquiferBlocksGetDistinctBookmarks)
{
    const QString path = writeSpeciesBlocksReport();
    ASSERT_FALSE(path.isEmpty());
    const auto sections = RptParser::parse(path, nullptr);
    const QStringList labels = RptParser::bookmarkLabels(sections);
    ASSERT_EQ(labels.size(), sections.size());
    QStringList aquifer;
    for (const QString &label : labels)
        if (label.startsWith(QStringLiteral("2D Aquifer Quality Continuity")))
            aquifer << label;
    EXPECT_EQ(aquifer.join(QStringLiteral(" | ")).toStdString(),
              "2D Aquifer Quality Continuity — SurfPoll | "
              "2D Aquifer Quality Continuity — GWTracer | "
              "2D Aquifer Quality Continuity — __WATER_AGE__");
    // Unique titles keep their plain title (no "Volume Depth" heading suffix).
    EXPECT_TRUE(labels.contains(QStringLiteral("Runoff Quantity Continuity")));
    EXPECT_TRUE(labels.contains(QStringLiteral("Quality Routing Continuity")));
}

TEST(RptParser, ContinuityBannerNamesTheExceedingBlocks)
{
    const QString path = writeSpeciesBlocksReport();
    ASSERT_FALSE(path.isEmpty());
    const auto sections = RptParser::parse(path, nullptr);
    // Exactly the GWTracer aquifer block and the SurfPoll routing column; the
    // runoff block (0.437 %) and the "n/a" block are not named.
    EXPECT_EQ(RptParser::highContinuityErrorBlocks(sections)
                  .join(QStringLiteral(" | "))
                  .toStdString(),
              "2D Aquifer Quality Continuity (GWTracer) | Quality Routing Continuity (SurfPoll)");
    EXPECT_TRUE(RptParser::hasHighContinuityError(sections));
    EXPECT_TRUE(RptParser::highContinuityErrorBlocks(sections, 20.0).isEmpty());
}
