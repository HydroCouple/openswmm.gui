/*!
 * \file   test_rptparser.cpp
 * \brief  Unit tests for openswmmvis::io::RptParser.
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
 */

#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include "io/rptparser.h"

using openswmmvis::io::RptParser;
using openswmmvis::io::RptSection;

namespace {

// CTest runs with WORKING_DIRECTORY tests/unit/data.
const QString kFixture = QStringLiteral("weir_culvert.rpt");

QStringList titlesOf(const QVector<RptSection> &sections)
{
    QStringList t;
    for (const auto &s : sections) t << s.title;
    return t;
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
    // ("Volume / Depth", "acre-feet / inches") — the title must be cut
    // to the star-rule columns.
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
    const auto sections =
        RptParser::parse(QStringLiteral("does_not_exist.rpt"), &err);
    EXPECT_TRUE(sections.isEmpty());
    EXPECT_FALSE(err.isEmpty());
}
