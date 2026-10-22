/**
 * @file      test_temporalspec.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Slice Z.13-data — TemporalSpec value type.
 *
 *         Success criterion: every field round-trips through JSON;
 *         default-constructed values elide from output; frame-rate
 *         clamps to the documented range; mode enum round-trips;
 *         equality operator returns sensible results for change
 *         detection.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QtTest/QtTest>

#include "render/temporalspec.h"

using namespace OpenSWMM::Render;

class TestTemporalSpec : public QObject
{
    Q_OBJECT
private slots:
    // Enum
    /**
     *
     * @brief Require Single, Range and Cumulative modes to have nonempty reversible string
     * tokens.
     *
     */
    void mode_allValuesRoundTripThroughString();
    /**
     *
     * @brief Require unknown/empty mode tokens to resolve to Single.
     *
     */
    void mode_unknownStringFallsBackToSingle();

    // Defaults
    /**
     *
     * @brief Pin disabled Single mode, positive FPS, invalid endpoints, no loop/ping-pong and
     * zero-second range window.
     *
     */
    void defaults_areSensible();
    /**
     *
     * @brief Require a default temporal specification to serialize as an exactly empty object.
     *
     */
    void defaults_jsonIsEmptyExceptDefaults();

    // Round-trip
    /**
     *
     * @brief Round-trip enabled state, time field, Range mode, 24 FPS, loop/ping-pong, endpoints
     * and 600-second window.
     *
     */
    void roundTrip_preservesEveryField();
    /**
     *
     * @brief Require invalid start/end times to be absent from enabled temporal JSON.
     *
     */
    void roundTrip_invalidDateTimesElide();
    /**
     *
     * @brief Require the default disabled flag to be omitted from JSON.
     *
     */
    void roundTrip_disabledStateDefaultElides();

    // Clamping
    /**
     *
     * @brief Require JSON frame rate 200 FPS to clamp to 60 FPS.
     *
     */
    void frameRate_clampsHighOnRead();
    /**
     *
     * @brief Require JSON frame rate -5 FPS to clamp to .1 FPS.
     *
     */
    void frameRate_clampsLowOnRead();
    /**
     *
     * @brief Require valid JSON frame rate 30 FPS to remain unchanged.
     *
     */
    void frameRate_inRangeIsUnchanged();

    // Equality
    /**
     *
     * @brief Require matching field, mode and FPS specifications to compare equal.
     *
     */
    void equality_identicalSpecsAreEqual();
    /**
     *
     * @brief Require a changed enabled flag to compare unequal.
     *
     */
    void equality_differentEnabledNotEqual();
    /**
     *
     * @brief Require a changed temporal mode to compare unequal.
     *
     */
    void equality_differentModesNotEqual();
    /**
     *
     * @brief Require a changed time-field name to compare unequal.
     *
     */
    void equality_differentTimeFieldNotEqual();

    // Specific fields
    /**
     *
     * @brief Require empty time-field text to be omitted even when temporal behavior is enabled.
     *
     */
    void timeField_emptyOmittedFromJson();
    /**
     *
     * @brief Require a zero-second range window to be omitted from enabled JSON.
     *
     */
    void rangeWindow_zeroOmittedFromJson();
    /**
     *
     * @brief Require loop and ping-pong flags to serialize independently and jointly.
     *
     */
    void loopAndPingPong_independentlySerialise();
};

// ── Enum ───────────────────────────────────────────────────────────

void TestTemporalSpec::mode_allValuesRoundTripThroughString()
{
    const TemporalMode all[] = {TemporalMode::Single, TemporalMode::Range,
                                TemporalMode::Cumulative};
    for (TemporalMode m : all) {
        const QString s = temporalModeToString(m);
        QVERIFY(!s.isEmpty());
        QCOMPARE(temporalModeFromString(s), m);
    }
}

void TestTemporalSpec::mode_unknownStringFallsBackToSingle()
{
    QCOMPARE(temporalModeFromString(QStringLiteral("nope")), TemporalMode::Single);
    QCOMPARE(temporalModeFromString(QString()), TemporalMode::Single);
}

// ── Defaults ───────────────────────────────────────────────────────

void TestTemporalSpec::defaults_areSensible()
{
    TemporalSpec s;
    QCOMPARE(s.enabled, false);
    QCOMPARE(s.timeField, QString());
    QCOMPARE(s.mode, TemporalMode::Single);
    QVERIFY(s.frameRateFps > 0.0);
    QCOMPARE(s.loop, false);
    QCOMPARE(s.pingPong, false);
    QVERIFY(!s.startTime.isValid());
    QVERIFY(!s.endTime.isValid());
    QCOMPARE(s.rangeWindowSec, 0.0);
}

void TestTemporalSpec::defaults_jsonIsEmptyExceptDefaults()
{
    // A default-constructed spec round-trips through JSON to default
    // values; toJson() omits every default field, so the JSON object
    // is empty (or near-empty).
    TemporalSpec s;
    QJsonObject j = s.toJson();
    QVERIFY(j.isEmpty());
}

// ── Round-trip ────────────────────────────────────────────────────

void TestTemporalSpec::roundTrip_preservesEveryField()
{
    TemporalSpec s;
    s.enabled = true;
    s.timeField = QStringLiteral("observed_at");
    s.mode = TemporalMode::Range;
    s.frameRateFps = 24.0;
    s.loop = true;
    s.pingPong = true;
    s.startTime = QDateTime(QDate(2024, 6, 15), QTime(8, 30, 0));
    s.endTime = QDateTime(QDate(2024, 6, 15), QTime(20, 45, 30));
    s.rangeWindowSec = 600.0;

    TemporalSpec back = TemporalSpec::fromJson(s.toJson());
    QCOMPARE(back.enabled, s.enabled);
    QCOMPARE(back.timeField, s.timeField);
    QCOMPARE(back.mode, s.mode);
    QCOMPARE(back.frameRateFps, s.frameRateFps);
    QCOMPARE(back.loop, s.loop);
    QCOMPARE(back.pingPong, s.pingPong);
    QCOMPARE(back.startTime, s.startTime);
    QCOMPARE(back.endTime, s.endTime);
    QCOMPARE(back.rangeWindowSec, s.rangeWindowSec);
}

void TestTemporalSpec::roundTrip_invalidDateTimesElide()
{
    TemporalSpec s;
    s.enabled = true; // ensure JSON isn't fully empty
    QJsonObject j = s.toJson();
    QVERIFY(!j.contains(QStringLiteral("startTime")));
    QVERIFY(!j.contains(QStringLiteral("endTime")));
}

void TestTemporalSpec::roundTrip_disabledStateDefaultElides()
{
    TemporalSpec s;
    QVERIFY(!s.toJson().contains(QStringLiteral("enabled")));
}

// ── Clamping ──────────────────────────────────────────────────────

void TestTemporalSpec::frameRate_clampsHighOnRead()
{
    QJsonObject j;
    j[QStringLiteral("frameRateFps")] = 200.0;
    TemporalSpec s = TemporalSpec::fromJson(j);
    QCOMPARE(s.frameRateFps, 60.0);
}

void TestTemporalSpec::frameRate_clampsLowOnRead()
{
    QJsonObject j;
    j[QStringLiteral("frameRateFps")] = -5.0;
    TemporalSpec s = TemporalSpec::fromJson(j);
    QCOMPARE(s.frameRateFps, 0.1);
}

void TestTemporalSpec::frameRate_inRangeIsUnchanged()
{
    QJsonObject j;
    j[QStringLiteral("frameRateFps")] = 30.0;
    TemporalSpec s = TemporalSpec::fromJson(j);
    QCOMPARE(s.frameRateFps, 30.0);
}

// ── Equality ──────────────────────────────────────────────────────

void TestTemporalSpec::equality_identicalSpecsAreEqual()
{
    TemporalSpec a, b;
    a.timeField = b.timeField = QStringLiteral("ts");
    a.mode = b.mode = TemporalMode::Range;
    a.frameRateFps = b.frameRateFps = 15.0;
    QCOMPARE(a, b);
}

void TestTemporalSpec::equality_differentEnabledNotEqual()
{
    TemporalSpec a, b;
    b.enabled = true;
    QVERIFY(a != b);
}

void TestTemporalSpec::equality_differentModesNotEqual()
{
    TemporalSpec a, b;
    b.mode = TemporalMode::Cumulative;
    QVERIFY(a != b);
}

void TestTemporalSpec::equality_differentTimeFieldNotEqual()
{
    TemporalSpec a, b;
    b.timeField = QStringLiteral("observed");
    QVERIFY(a != b);
}

// ── Specific fields ───────────────────────────────────────────────

void TestTemporalSpec::timeField_emptyOmittedFromJson()
{
    TemporalSpec s;
    s.enabled = true; // ensure JSON has something
    QVERIFY(!s.toJson().contains(QStringLiteral("timeField")));
}

void TestTemporalSpec::rangeWindow_zeroOmittedFromJson()
{
    TemporalSpec s;
    s.enabled = true;
    QVERIFY(!s.toJson().contains(QStringLiteral("rangeWindowSec")));
}

void TestTemporalSpec::loopAndPingPong_independentlySerialise()
{
    TemporalSpec a, b, c;
    a.loop = true;
    b.pingPong = true;
    c.loop = true;
    c.pingPong = true;

    QVERIFY(a.toJson().contains(QStringLiteral("loop")));
    QVERIFY(!a.toJson().contains(QStringLiteral("pingPong")));

    QVERIFY(!b.toJson().contains(QStringLiteral("loop")));
    QVERIFY(b.toJson().contains(QStringLiteral("pingPong")));

    QVERIFY(c.toJson().contains(QStringLiteral("loop")));
    QVERIFY(c.toJson().contains(QStringLiteral("pingPong")));
}

QTEST_MAIN(TestTemporalSpec)
#include "test_temporalspec.moc"
