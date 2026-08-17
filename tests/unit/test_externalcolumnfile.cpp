/*!
 * \file   test_externalcolumnfile.cpp
 * \brief  Shared external-column-file util (spec §4 task 1,
 *         workplans/MULTICOLUMN_SERIES_SINGLE_READ_2026-08-17.md) —
 *         readHeaders / readColumn over CSV, TSV and PCSWMM .tsf fixtures
 *         (tests/unit/data/extcol_*). QtCore + engine datetime only.
 */
#include <gtest/gtest.h>

#include "core/swmmdatetime.h"
#include "ui/util/externalcolumnfile.h"
#include "ui/util/externalcolumnfilecore.h"

#include <QDateTime>
#include <QStringList>

namespace eu = openswmmvis::ui;
namespace ec = openswmmvis::ui::extcol;

namespace {
double j(int y, int mo, int d, int h, int mi)
{
    return openswmmvis::core::qDateTimeToSwmmDateTime(
        QDateTime(QDate(y, mo, d), QTime(h, mi, 0), Qt::UTC));
}
} // namespace

// ── readHeaders ─────────────────────────────────────────────────────────────

TEST(ExternalColumnFile, ReadHeaders_CommaCsv)
{
    const QStringList h = eu::readHeaders(QStringLiteral("extcol_rain_multi.csv"));
    ASSERT_EQ(h.size(), 2);
    EXPECT_EQ(h[0], QStringLiteral("RG_EAST"));
    EXPECT_EQ(h[1], QStringLiteral("RG_WEST"));
}

TEST(ExternalColumnFile, ReadHeaders_TabTsv)
{
    const QStringList h = eu::readHeaders(QStringLiteral("extcol_rain_multi.tsv"));
    ASSERT_EQ(h.size(), 2);
    EXPECT_EQ(h[0], QStringLiteral("RG_EAST"));
    EXPECT_EQ(h[1], QStringLiteral("RG_WEST"));
}

TEST(ExternalColumnFile, ReadHeaders_TsfIdsRow)
{
    const QStringList h = eu::readHeaders(QStringLiteral("extcol_sample.tsf"));
    ASSERT_EQ(h.size(), 2);
    EXPECT_EQ(h[0], QStringLiteral("RG1"));
    EXPECT_EQ(h[1], QStringLiteral("RG2"));
}

TEST(ExternalColumnFile, ReadHeaders_HeaderlessFileFabricatesColN)
{
    QString err;
    bool fabricated = false;
    const QStringList h =
        eu::readHeaders(QStringLiteral("extcol_noheader.csv"), &err, &fabricated);
    ASSERT_EQ(h.size(), 2);
    EXPECT_EQ(h[0], QStringLiteral("col_1"));
    EXPECT_EQ(h[1], QStringLiteral("col_2"));
    // The names are display-only; callers must learn that from the flag (not
    // by pattern-matching "col_") so they never persist one (review B-4).
    EXPECT_TRUE(fabricated);
}

TEST(ExternalColumnFile, ReadHeaders_RealHeadersAreNotFlaggedFabricated)
{
    bool fabricated = true;
    eu::readHeaders(QStringLiteral("extcol_rain_multi.csv"), nullptr, &fabricated);
    EXPECT_FALSE(fabricated);
    eu::readHeaders(QStringLiteral("extcol_sample.tsf"), nullptr, &fabricated);
    EXPECT_FALSE(fabricated);
}

TEST(ExternalColumnFile, ReadHeaders_MissingFileReportsError)
{
    QString err;
    const QStringList h =
        eu::readHeaders(QStringLiteral("extcol_does_not_exist.csv"), &err);
    EXPECT_TRUE(h.isEmpty());
    EXPECT_FALSE(err.isEmpty());
}

// ── readColumn ──────────────────────────────────────────────────────────────

TEST(ExternalColumnFile, ReadColumn_ByName_SkipsEmptyCells)
{
    QVector<eu::ExternalSeriesPoint> pts;
    QStringList headers;
    // RG_WEST's last row has an empty cell — skipped, engine parity.
    const int n = eu::readColumn(QStringLiteral("extcol_rain_multi.csv"),
                                 QStringLiteral("RG_WEST"), pts, &headers);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(headers.size(), 2);
    EXPECT_DOUBLE_EQ(pts[0].value, 0.10);
    EXPECT_DOUBLE_EQ(pts[1].value, 0.35);
    EXPECT_DOUBLE_EQ(pts[0].timeJulian, j(2007, 1, 1, 0, 0));
    EXPECT_DOUBLE_EQ(pts[1].timeJulian, j(2007, 1, 1, 0, 15));
}

TEST(ExternalColumnFile, ReadColumn_CaseInsensitiveSelector)
{
    QVector<eu::ExternalSeriesPoint> pts;
    const int n = eu::readColumn(QStringLiteral("extcol_rain_multi.csv"),
                                 QStringLiteral("rg_east"), pts);
    ASSERT_EQ(n, 3);
    EXPECT_DOUBLE_EQ(pts[2].value, 0.50);
}

TEST(ExternalColumnFile, ReadColumn_EmptySelectorUsesFirstColumn)
{
    QVector<eu::ExternalSeriesPoint> ptsEmpty;
    ASSERT_EQ(eu::readColumn(QStringLiteral("extcol_rain_multi.tsv"),
                             QString(), ptsEmpty), 3);
    EXPECT_DOUBLE_EQ(ptsEmpty[1].value, 0.25);     // RG_EAST
}

TEST(ExternalColumnFile, ReadColumn_UnmatchedSelectorFailsInsteadOfFirstColumn)
{
    // Review B-3: the engine hard-errors on a column it cannot find
    // (ERR_TABLE_FILE_READ / ERR_RAIN_FILE_FORMAT). The GUI used to preview
    // column 1 instead, so a stale saved model charted one column while the
    // run refused to open. Headers are still reported so the picker can be
    // repopulated, but there is no data and there IS a diagnostic.
    QVector<eu::ExternalSeriesPoint> pts;
    QStringList headers;
    QString err;
    EXPECT_EQ(eu::readColumn(QStringLiteral("extcol_rain_multi.tsv"),
                             QStringLiteral("NO_SUCH_COLUMN"), pts, &headers,
                             &err),
              -1);
    EXPECT_TRUE(pts.isEmpty());
    EXPECT_EQ(headers.size(), 2);
    EXPECT_TRUE(err.contains(QStringLiteral("NO_SUCH_COLUMN")));
}

TEST(ExternalColumnFile, ReadColumn_Tsf_AmPmDatetimesAndColumns)
{
    QVector<eu::ExternalSeriesPoint> pts;
    QStringList headers;
    const int n = eu::readColumn(QStringLiteral("extcol_sample.tsf"),
                                 QStringLiteral("RG2"), pts, &headers);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(headers.size(), 2);
    EXPECT_EQ(headers[0], QStringLiteral("RG1"));
    // 12:00:00 AM → midnight; 1:30:00 PM → 13:30.
    EXPECT_DOUBLE_EQ(pts[0].timeJulian, j(2007, 1, 1, 0, 0));
    EXPECT_DOUBLE_EQ(pts[1].timeJulian, j(2007, 1, 1, 0, 15));
    EXPECT_DOUBLE_EQ(pts[2].timeJulian, j(2007, 1, 1, 13, 30));
    EXPECT_DOUBLE_EQ(pts[0].value, 0.05);
    EXPECT_DOUBLE_EQ(pts[2].value, 0.25);
}

TEST(ExternalColumnFile, ReadColumn_HeaderlessFirstLineIsSpentAsHeader)
{
    // Review B-4 off-by-one: the engine has no header-vs-data probe — it
    // always spends the first content line on the header row — so the GUI
    // must too, or the preview shows a row the run never sees. The 3-line
    // fixture therefore yields 2 points, starting at 01:00.
    // An empty selector is the ONLY way to address a headerless file — and it
    // is what the GUI persists for one.
    QVector<eu::ExternalSeriesPoint> pts;
    bool fabricated = false;
    const int n = eu::readColumn(QStringLiteral("extcol_noheader.csv"),
                                 QString(), pts, nullptr, nullptr, &fabricated);
    ASSERT_EQ(n, 2);
    EXPECT_TRUE(fabricated);
    EXPECT_DOUBLE_EQ(pts[0].timeJulian, j(2007, 1, 1, 1, 0));
    EXPECT_DOUBLE_EQ(pts[0].value, 3.5);
    EXPECT_DOUBLE_EQ(pts[1].value, 5.5);
}

TEST(ExternalColumnFile, ReadColumn_HeaderlessRejectsAnyNamedColumn)
{
    // The engine matches column names against the file's own header row; a
    // headerless file has none, so NO name resolves there — including the
    // display-only "col_N" the GUI shows. Failing here is what stops such a
    // name from being previewed as if it were bindable (review B-4 / R1).
    QVector<eu::ExternalSeriesPoint> pts;
    QString err;
    EXPECT_EQ(eu::readColumn(QStringLiteral("extcol_noheader.csv"),
                             QStringLiteral("col_2"), pts, nullptr, &err),
              -1);
    EXPECT_TRUE(pts.isEmpty());
    EXPECT_FALSE(err.isEmpty());
}

// ── pure core: path:col token handling (registry persistence, B4) ──────────

TEST(ExternalColumnFileCore, SplitPathColumn_PlainAndColumn)
{
    std::string p, c;
    ec::splitPathColumn("rain.csv", p, c);
    EXPECT_EQ(p, "rain.csv");
    EXPECT_TRUE(c.empty());

    ec::splitPathColumn("data/rain.csv:RG_2", p, c);
    EXPECT_EQ(p, "data/rain.csv");
    EXPECT_EQ(c, "RG_2");
}

TEST(ExternalColumnFileCore, SplitPathColumn_WindowsDriveLetter)
{
    std::string p, c;
    ec::splitPathColumn("C:\\data\\rain.csv:RG_2", p, c);
    EXPECT_EQ(p, "C:\\data\\rain.csv");
    EXPECT_EQ(c, "RG_2");

    ec::splitPathColumn("C:\\data\\rain.csv", p, c);
    EXPECT_EQ(p, "C:\\data\\rain.csv");
    EXPECT_TRUE(c.empty());

    // Drive-relative form: only the drive-letter exemption saves this one.
    ec::splitPathColumn("C:rain.csv", p, c);
    EXPECT_EQ(p, "C:rain.csv");
    EXPECT_TRUE(c.empty());
}

TEST(ExternalColumnFileCore, SplitPathColumn_ColonInsideADirectoryName)
{
    // Review B-2: the unified rule takes the LAST colon and only treats it as
    // a separator when the suffix has no path separator, so a directory named
    // "Data:2024" (legal on POSIX/macOS) no longer truncates the path. Both
    // engine readers now derive the same cache key from this token, which is
    // what keeps a gage and a timeseries sharing one parse of the file.
    std::string p, c;
    ec::splitPathColumn("/x/Data:2024/f.csv", p, c);
    EXPECT_EQ(p, "/x/Data:2024/f.csv");
    EXPECT_TRUE(c.empty());

    ec::splitPathColumn("/x/Data:2024/f.csv:EAST", p, c);
    EXPECT_EQ(p, "/x/Data:2024/f.csv");
    EXPECT_EQ(c, "EAST");

    ec::splitPathColumn("f.csv:COL", p, c);
    EXPECT_EQ(p, "f.csv");
    EXPECT_EQ(c, "COL");
}

TEST(ExternalColumnFileCore, SniffDelimiter_IgnoresDelimitersInsideQuotes)
{
    // Review B-8: a tab-delimited header whose quoted column name contains
    // commas used to sniff as comma and mis-split every row.
    EXPECT_EQ(ec::sniffDelimiter("Date\t\"a,b,c\""), '\t');
    EXPECT_EQ(ec::sniffDelimiter("Date,\"Rain, North\",R2"), ',');
    EXPECT_EQ(ec::sniffDelimiter("a\tb,c"), '\t');   // tie → tab (engine rule)
}

TEST(ExternalColumnFileCore, ComposePathColumn_RoundTrip)
{
    EXPECT_EQ(ec::composePathColumn("rain.csv", ""), "rain.csv");
    EXPECT_EQ(ec::composePathColumn("rain.csv", "RG_2"), "rain.csv:RG_2");

    std::string p, c;
    ec::splitPathColumn(ec::composePathColumn("C:\\r.tsf", "East Gage"), p, c);
    EXPECT_EQ(p, "C:\\r.tsf");
    EXPECT_EQ(c, "East Gage");
}

// ── reconcileColumnSelector — the "the file just changed" rule (review R3) ───

TEST(ExternalColumnFile, Reconcile_KeepsAColumnTheNewFileStillHas)
{
    // No churn when the binding survives: callers write only on a change, so
    // returning the current name is what makes a path edit free.
    EXPECT_EQ(eu::reconcileColumnSelector(QStringLiteral("extcol_rain_multi.csv"),
                                          QStringLiteral("RG_WEST")),
              QStringLiteral("RG_WEST"));
}

TEST(ExternalColumnFile, Reconcile_NormalisesToTheFilesSpelling)
{
    // Matching is case-insensitive (engine parity) but the stored token takes
    // the file's spelling, so the token reads the way the file does.
    EXPECT_EQ(eu::reconcileColumnSelector(QStringLiteral("extcol_rain_multi.csv"),
                                          QStringLiteral("rg_east")),
              QStringLiteral("RG_EAST"));
}

TEST(ExternalColumnFile, Reconcile_PicksTheFirstColumnWhenUnboundOrStale)
{
    // An unbound multi-column file is the case that made the gage write the
    // station grammar for a file with no station column (R3).
    EXPECT_EQ(eu::reconcileColumnSelector(QStringLiteral("extcol_rain_multi.csv"),
                                          QString()),
              QStringLiteral("RG_EAST"));
    // A name carried over from a different file would fail the run.
    EXPECT_EQ(eu::reconcileColumnSelector(QStringLiteral("extcol_rain_multi.csv"),
                                          QStringLiteral("GONE")),
              QStringLiteral("RG_EAST"));
}

TEST(ExternalColumnFile, Reconcile_HeaderlessAndUnreadableResolveToEmpty)
{
    // Fabricated col_N names are display-only — the engine spends line 1 as its
    // header row, so only the first column is addressable, via an EMPTY
    // selector (review B-4 / risk R1). A stale name must not survive either.
    EXPECT_EQ(eu::reconcileColumnSelector(QStringLiteral("extcol_noheader.csv"),
                                          QStringLiteral("RG_EAST")),
              QString());
    EXPECT_EQ(eu::reconcileColumnSelector(QStringLiteral("extcol_noheader.csv"),
                                          QString()),
              QString());
    EXPECT_EQ(eu::reconcileColumnSelector(QStringLiteral("extcol_does_not_exist.csv"),
                                          QStringLiteral("RG_EAST")),
              QString());
    EXPECT_EQ(eu::reconcileColumnSelector(QString(), QStringLiteral("RG_EAST")),
              QString());
}
