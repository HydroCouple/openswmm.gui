/*!
 * \file   test_simulationoptionsdialog.cpp
 * \brief  Slice G-1 QtTest: SimulationOptionsDialog pure helpers.
 *
 * The dialog itself round-trips through the engine and is not unit-tested
 * here (would need a SWMM_Engine + opening a model file). What we DO test is
 * the small static surface used to translate between Qt widget state and
 * the engine's option-string format — the most likely place a typo would
 * silently corrupt a model file on Save.
 */

#include "ui/dialogs/simulationoptionsdialog.h"

#include <QDateTime>
#include <QDateTimeEdit>
#include <QItemSelectionModel>
#include <QObject>
#include <QTableWidget>
#include <QTest>

class TestSimulationOptionsDialog : public QObject
{
    Q_OBJECT
private slots:
    void parseEngineBoolKnownValues();
    void parseEngineBoolUnknownIsPartial();
    void engineBoolStringRoundTrip();
    void fastPresetHasBalancedRecipe();
    void formatEngineDateTimeMatchesInpFormat();
    void parseEngineDateTimeRoundTrips();
    void parseEngineDateTimeRejectsMalformed();

    // Slice CW — [EVENTS] OADate helpers
    void oaDateEpochIsZero();
    void oaDateMatchesKnownSwmmAnchor();
    void oaDateRoundTripsArbitraryDates();
    void oaDateInvalidInputReturnsZero();

    // Step-value parsing (REPORT_STEP / WET_STEP / DRY_STEP / RULE_STEP)
    void parseStepSecondsAcceptsEngineForms();
    void parseStepSecondsFallsBackOnGarbage();

    // Numeric-aware option comparison used by writeToEngine()'s
    // writeIfChanged — formatting drift must not read as an edit.
    void optionValueEqualsNumericForms();
    void optionValueEqualsNonNumeric();

    // [EVENTS] table row-selection query behind the Remove button. The table
    // populates cells exclusively with setCellWidget() editors — NO
    // QTableWidgetItems — so this must read the selection model, not
    // selectedItems() (the bug that left Remove permanently disabled).
    void selectedRowsDescendingWidgetOnlyTable();
    void selectedRowsDescendingItemFallback();
};

void TestSimulationOptionsDialog::parseEngineBoolKnownValues()
{
    QCOMPARE(SimulationOptionsDialog::parseEngineBool("YES"),   int(Qt::Checked));
    QCOMPARE(SimulationOptionsDialog::parseEngineBool("yes"),   int(Qt::Checked));
    QCOMPARE(SimulationOptionsDialog::parseEngineBool("TRUE"),  int(Qt::Checked));
    QCOMPARE(SimulationOptionsDialog::parseEngineBool("1"),     int(Qt::Checked));

    QCOMPARE(SimulationOptionsDialog::parseEngineBool("NO"),    int(Qt::Unchecked));
    QCOMPARE(SimulationOptionsDialog::parseEngineBool("no"),    int(Qt::Unchecked));
    QCOMPARE(SimulationOptionsDialog::parseEngineBool("FALSE"), int(Qt::Unchecked));
    QCOMPARE(SimulationOptionsDialog::parseEngineBool("0"),     int(Qt::Unchecked));
}

void TestSimulationOptionsDialog::parseEngineBoolUnknownIsPartial()
{
    // Anything else → Qt::PartiallyChecked so the UI surfaces the oddity
    // rather than silently coercing.
    QCOMPARE(SimulationOptionsDialog::parseEngineBool(""),       int(Qt::PartiallyChecked));
    QCOMPARE(SimulationOptionsDialog::parseEngineBool("maybe"),  int(Qt::PartiallyChecked));
    QCOMPARE(SimulationOptionsDialog::parseEngineBool("12"),     int(Qt::PartiallyChecked));
}

void TestSimulationOptionsDialog::engineBoolStringRoundTrip()
{
    // Setter side: must emit the canonical strings the engine parses.
    QCOMPARE(SimulationOptionsDialog::engineBoolString(true),  QStringLiteral("YES"));
    QCOMPARE(SimulationOptionsDialog::engineBoolString(false), QStringLiteral("NO"));

    // Round-trip through both helpers.
    QCOMPARE(SimulationOptionsDialog::parseEngineBool(
                 SimulationOptionsDialog::engineBoolString(true)),  int(Qt::Checked));
    QCOMPARE(SimulationOptionsDialog::parseEngineBool(
                 SimulationOptionsDialog::engineBoolString(false)), int(Qt::Unchecked));
}

void TestSimulationOptionsDialog::fastPresetHasBalancedRecipe()
{
    // Locks the benchmarked "conservative fast" recipe (FAST_RUN_RECIPE.md) so a
    // future edit can't silently drift it. THREADS = the machine's performance
    // cores (the benchmark's 8 M-series P-cores), else its logical CPUs;
    // MINIMUM_STEP=1.0 s floors the 1D step the 2D coupling would collapse, while
    // keeping mass balance as good as or better than the as-shipped default.
    int    threads = -1;
    double minStep = -1.0;
    SimulationOptionsDialog::fastPresetValues(threads, minStep);
    SWMM_ThreadInfo ti{};
    QCOMPARE(swmm_get_thread_info(&ti), int(SWMM_OK));
    const int expected = ti.perf_cores > 0 ? ti.perf_cores
                       : ti.logical_cpus > 0 ? ti.logical_cpus : 8;
    QCOMPARE(threads, expected);
    QVERIFY(threads >= 1);
    QCOMPARE(minStep, 1.0);
    // Must sit inside the per-project spin range [0.01, 60.0] s.
    QVERIFY(minStep >= 0.01 && minStep <= 60.0);
}

void TestSimulationOptionsDialog::formatEngineDateTimeMatchesInpFormat()
{
    QDateTime dt(QDate(2026, 4, 22), QTime(13, 45, 7));
    QString d, t;
    SimulationOptionsDialog::formatEngineDateTime(dt, d, t);
    QCOMPARE(d, QStringLiteral("04/22/2026"));   // engine wants MM/DD/YYYY
    QCOMPARE(t, QStringLiteral("13:45:07"));     // 24-hour HH:MM:SS
}

void TestSimulationOptionsDialog::parseEngineDateTimeRoundTrips()
{
    QDateTime original(QDate(2026, 1, 1), QTime(0, 0, 0));
    QString d, t;
    SimulationOptionsDialog::formatEngineDateTime(original, d, t);
    QDateTime parsed = SimulationOptionsDialog::parseEngineDateTime(d, t);
    QCOMPARE(parsed, original);

    QDateTime later(QDate(2026, 12, 31), QTime(23, 59, 59));
    SimulationOptionsDialog::formatEngineDateTime(later, d, t);
    parsed = SimulationOptionsDialog::parseEngineDateTime(d, t);
    QCOMPARE(parsed, later);
}

void TestSimulationOptionsDialog::parseEngineDateTimeRejectsMalformed()
{
    // Bad date.
    QVERIFY(!SimulationOptionsDialog::parseEngineDateTime("13/45/2026", "12:00:00").isValid());
    // Bad time.
    QVERIFY(!SimulationOptionsDialog::parseEngineDateTime("01/01/2026", "25:99:99").isValid());
    // Empty strings.
    QVERIFY(!SimulationOptionsDialog::parseEngineDateTime("",            "00:00:00").isValid());
    QVERIFY(!SimulationOptionsDialog::parseEngineDateTime("01/01/2026", "").isValid());
}

// ---------------------------------------------------------------------------
// Slice CW — [EVENTS] OADate helpers (2026-05-21)
// ---------------------------------------------------------------------------

void TestSimulationOptionsDialog::oaDateEpochIsZero()
{
    // Anchor: 1899-12-30 00:00 must round-trip to 0.0.
    const QDateTime epoch(QDate(1899, 12, 30), QTime(0, 0, 0));
    QCOMPARE(SimulationOptionsDialog::oaDateFromQDateTime(epoch), 0.0);
    QCOMPARE(SimulationOptionsDialog::qDateTimeFromOaDate(0.0), epoch);
}

void TestSimulationOptionsDialog::oaDateMatchesKnownSwmmAnchor()
{
    // Engine convention (engine src/engine/core/DateTime.hpp): integer 1
    // is 1899-12-31, and noon adds 0.5.  Test a couple of fixed anchors
    // to lock the convention against future drift.
    const QDateTime a(QDate(1899, 12, 31), QTime(0, 0, 0));
    QCOMPARE(SimulationOptionsDialog::oaDateFromQDateTime(a), 1.0);

    const QDateTime b(QDate(1900, 1, 1), QTime(12, 0, 0));
    QCOMPARE(SimulationOptionsDialog::oaDateFromQDateTime(b), 2.5);

    // Modern SWMM-typical date (2026-01-01 00:00).
    const QDateTime c(QDate(2026, 1, 1), QTime(0, 0, 0));
    const double oa = SimulationOptionsDialog::oaDateFromQDateTime(c);
    QCOMPARE(SimulationOptionsDialog::qDateTimeFromOaDate(oa), c);
}

void TestSimulationOptionsDialog::oaDateRoundTripsArbitraryDates()
{
    // HH:MM precision per legacy SWMM 5 dialog: keep test inputs on
    // whole-minute boundaries so the floating-point round-trip is exact.
    const QList<QDateTime> samples = {
        QDateTime(QDate(2000, 2, 29), QTime( 6, 15)),  // leap-day morning
        QDateTime(QDate(2024, 7,  4), QTime(23, 59)),  // last minute of day
        QDateTime(QDate(2026, 5, 21), QTime(14, 30)),  // slice-creation date
        QDateTime(QDate(2099, 1,  1), QTime( 0,  0)),  // far-future midnight
    };
    for (const auto &dt : samples) {
        const double oa = SimulationOptionsDialog::oaDateFromQDateTime(dt);
        const QDateTime back = SimulationOptionsDialog::qDateTimeFromOaDate(oa);
        QCOMPARE(back, dt);
    }
}

void TestSimulationOptionsDialog::oaDateInvalidInputReturnsZero()
{
    // An invalid QDateTime maps to 0.0 — the engine treats 0.0 as the
    // OADate epoch which is well outside any reasonable simulation
    // window.  The dialog must validate Start < End before persisting,
    // so a zero is harmless when caught upstream.
    QCOMPARE(SimulationOptionsDialog::oaDateFromQDateTime(QDateTime()), 0.0);
}

// ---------------------------------------------------------------------------
// Step values
// ---------------------------------------------------------------------------

void TestSimulationOptionsDialog::parseStepSecondsAcceptsEngineForms()
{
    constexpr qint64 kFallback = 60;
    const auto parse = &SimulationOptionsDialog::parseStepSeconds;

    // Plain seconds — swmm_options_get() form for WET_STEP / DRY_STEP /
    // RULE_STEP (std::to_string of a long long).
    QCOMPARE(parse(QStringLiteral("900"), kFallback), qint64(900));

    // Decimal seconds — swmm_options_get() form for REPORT_STEP and
    // ROUTING_STEP, which are doubles rendered with std::to_string. The
    // integer-only parser this replaced rejected these and handed back the
    // preferences default, so a changed reporting step was silently reverted
    // the next time the dialog was opened and OK'd.
    QCOMPARE(parse(QStringLiteral("900.000000"), kFallback), qint64(900));
    QCOMPARE(parse(QStringLiteral("300.000000"), kFallback), qint64(300));
    QCOMPARE(parse(QStringLiteral("0.000000"),   kFallback), qint64(0));

    // HH:MM:SS as written into the .inp — including the > 24 h hour field
    // legacy SWMM allows.
    QCOMPARE(parse(QStringLiteral("00:15:00"), kFallback), qint64(900));
    QCOMPARE(parse(QStringLiteral("48:00:00"), kFallback), qint64(172800));
    QCOMPARE(parse(QStringLiteral("15:00"),    kFallback), qint64(900));

    // Surrounding whitespace is tolerated (getOption() already trims, but the
    // preference fallbacks feed through the same helper).
    QCOMPARE(parse(QStringLiteral("  900.000000  "), kFallback), qint64(900));
}

void TestSimulationOptionsDialog::parseStepSecondsFallsBackOnGarbage()
{
    constexpr qint64 kFallback = 60;
    const auto parse = &SimulationOptionsDialog::parseStepSeconds;

    QCOMPARE(parse(QString(),                     kFallback), kFallback);
    QCOMPARE(parse(QStringLiteral(""),            kFallback), kFallback);
    QCOMPARE(parse(QStringLiteral("abc"),         kFallback), kFallback);
    QCOMPARE(parse(QStringLiteral("00:15:xx"),    kFallback), kFallback);
    QCOMPARE(parse(QStringLiteral("1:2:3:4"),     kFallback), kFallback);
    QCOMPARE(parse(QStringLiteral("-900"),        kFallback), kFallback);
}

// ---------------------------------------------------------------------------
// optionValueEquals
// ---------------------------------------------------------------------------

void TestSimulationOptionsDialog::optionValueEqualsNumericForms()
{
    const auto eq = &SimulationOptionsDialog::optionValueEquals;

    // The engine renders numerics as std::to_string(double) (six decimals)
    // while the dialog formats 'f',2 / 'f',3 / 'g',6 — those must compare
    // equal or every OK rewrites every key and dirties the project.
    QVERIFY(eq(QStringLiteral("900.000000"), QStringLiteral("900")));
    QVERIFY(eq(QStringLiteral("0.000000"),   QStringLiteral("0.00")));
    QVERIFY(eq(QStringLiteral("5"),          QStringLiteral("5.000000")));
    QVERIFY(eq(QStringLiteral("  1.5 "),     QStringLiteral("1.50")));

    // Genuinely different numbers are edits.
    QVERIFY(!eq(QStringLiteral("0.5"), QStringLiteral("0.6")));
    QVERIFY(!eq(QStringLiteral("5"),   QStringLiteral("0.05")));
}

void TestSimulationOptionsDialog::optionValueEqualsNonNumeric()
{
    const auto eq = &SimulationOptionsDialog::optionValueEquals;

    // Non-numeric tokens compare as exact strings.
    QVERIFY(eq(QStringLiteral("DYNWAVE"), QStringLiteral("DYNWAVE")));
    QVERIFY(!eq(QStringLiteral("YES"),    QStringLiteral("NO")));

    // Mixed numeric/non-numeric (and empty vs zero) are never equal — an
    // empty engine value followed by a numeric write is a real edit.
    QVERIFY(!eq(QString(),               QStringLiteral("0")));
    QVERIFY(!eq(QStringLiteral("AUTO"),  QStringLiteral("0")));
}

void TestSimulationOptionsDialog::selectedRowsDescendingWidgetOnlyTable()
{
    // Mirror the [EVENTS] table exactly: row selection, cell WIDGETS only,
    // no QTableWidgetItem anywhere.
    QTableWidget table(3, 2);
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 2; ++c)
            table.setCellWidget(r, c, new QDateTimeEdit(&table));

    // No selection → empty (Remove stays disabled).
    QVERIFY(SimulationOptionsDialog::selectedRowsDescending(&table).isEmpty());

    // Single row selected → that row, even though selectedItems() is empty.
    table.selectRow(1);
    QVERIFY(table.selectedItems().isEmpty());  // the premise of the old bug
    QCOMPARE(SimulationOptionsDialog::selectedRowsDescending(&table),
             (QList<int>{1}));

    // Multi-row selection → distinct rows, DESCENDING (removeRow-safe).
    auto *sel = table.selectionModel();
    sel->clearSelection();
    sel->select(table.model()->index(0, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    sel->select(table.model()->index(2, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(SimulationOptionsDialog::selectedRowsDescending(&table),
             (QList<int>{2, 0}));

    // Null table → empty, no crash.
    QVERIFY(SimulationOptionsDialog::selectedRowsDescending(nullptr).isEmpty());
}

void TestSimulationOptionsDialog::selectedRowsDescendingItemFallback()
{
    // A table with real items and plain cell selection (no full-row spans):
    // selectedRows() reports nothing, the selectedItems() fallback kicks in.
    QTableWidget table(3, 2);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 2; ++c)
            table.setItem(r, c, new QTableWidgetItem(QStringLiteral("x")));

    auto *sel = table.selectionModel();
    sel->select(table.model()->index(1, 0), QItemSelectionModel::Select);
    sel->select(table.model()->index(0, 1), QItemSelectionModel::Select);

    QVERIFY(sel->selectedRows().isEmpty());   // no full row selected
    QCOMPARE(SimulationOptionsDialog::selectedRowsDescending(&table),
             (QList<int>{1, 0}));
}

QTEST_MAIN(TestSimulationOptionsDialog)
#include "test_simulationoptionsdialog.moc"
