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
#include <QObject>
#include <QTest>

class TestSimulationOptionsDialog : public QObject
{
    Q_OBJECT
private slots:
    void parseEngineBoolKnownValues();
    void parseEngineBoolUnknownIsPartial();
    void engineBoolStringRoundTrip();
    void formatEngineDateTimeMatchesInpFormat();
    void parseEngineDateTimeRoundTrips();
    void parseEngineDateTimeRejectsMalformed();
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

QTEST_MAIN(TestSimulationOptionsDialog)
#include "test_simulationoptionsdialog.moc"
