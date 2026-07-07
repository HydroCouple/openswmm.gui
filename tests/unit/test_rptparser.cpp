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

#include <QFile>
#include <QString>
#include <QStringList>

#include "io/rptparser.h"

using openswmmvis::io::RptParser;
using openswmmvis::io::RptSection;

namespace {

// CTest runs with WORKING_DIRECTORY tests/unit/data.
const QString kFixture = QStringLiteral("weir_culvert.rpt");
const QString kTwoDFixture =
    QStringLiteral("../../gui/data/output_simstatus2derr/mini_2d.rpt");

QStringList titlesOf(const QVector<RptSection> &sections)
{
    QStringList t;
    for (const auto &s : sections) t << s.title;
    return t;
}

QString joinedBodies(const QVector<RptSection> &sections)
{
    QString text;
    for (const auto &s : sections) text += s.body;
    return text;
}

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
    const auto sections =
        RptParser::parse(QStringLiteral("does_not_exist.rpt"), &err);
    EXPECT_TRUE(sections.isEmpty());
    EXPECT_FALSE(err.isEmpty());
}
