/**
 * @file      test_maskspec.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Slice Z.14-data — MaskSpec value type.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QtTest/QtTest>

#include "render/maskspec.h"

using namespace OpenSWMM::Render;

class TestMaskSpec : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require both mask modes to round-trip through nonempty string tokens.
     *
     */
    void mode_bothValuesRoundTripThroughString();
    /**
     *
     * @brief Require empty/unknown mask mode strings to resolve to ClipInside.
     *
     */
    void mode_unknownStringFallsBackToClipInside();

    /**
     *
     * @brief Pin disabled mask, empty source-layer ID and ClipInside default mode.
     *
     */
    void defaults_areSensible();
    /**
     *
     * @brief Require default mask JSON to contain no fields.
     *
     */
    void defaults_jsonIsEmpty();

    /**
     *
     * @brief Round-trip enabled state, source-layer ID and ClipOutside mode without resolving a
     * layer.
     *
     */
    void roundTrip_preservesEveryField();
    /**
     *
     * @brief Require disabled enabled-flag omission even when a source-layer ID is present.
     *
     */
    void roundTrip_disabledOmitted();
    /**
     *
     * @brief Require empty source-layer ID omission from enabled mask JSON.
     *
     */
    void roundTrip_emptySourceLayerIdOmitted();
    /**
     *
     * @brief Require default ClipInside mode omission from enabled mask JSON.
     *
     */
    void roundTrip_clipInsideModeOmitted();
    /**
     *
     * @brief Require ClipOutside mode to serialize as the explicit clipOutside token.
     *
     */
    void roundTrip_clipOutsideModeIncluded();

    /**
     *
     * @brief Require identical enabled/source/mode mask specifications to compare equal.
     *
     */
    void equality_identicalSpecsAreEqual();
    /**
     *
     * @brief Require differing mask enabled states to compare unequal.
     *
     */
    void equality_differentEnabledNotEqual();
    /**
     *
     * @brief Require differing source-layer IDs to compare unequal.
     *
     */
    void equality_differentSourceNotEqual();
    /**
     *
     * @brief Require differing mask modes to compare unequal.
     *
     */
    void equality_differentModeNotEqual();
};

void TestMaskSpec::mode_bothValuesRoundTripThroughString()
{
    for (MaskMode m : {MaskMode::ClipInside, MaskMode::ClipOutside}) {
        const QString s = maskModeToString(m);
        QVERIFY(!s.isEmpty());
        QCOMPARE(maskModeFromString(s), m);
    }
}

void TestMaskSpec::mode_unknownStringFallsBackToClipInside()
{
    QCOMPARE(maskModeFromString(QStringLiteral("nope")), MaskMode::ClipInside);
    QCOMPARE(maskModeFromString(QString()), MaskMode::ClipInside);
}

void TestMaskSpec::defaults_areSensible()
{
    MaskSpec s;
    QCOMPARE(s.enabled, false);
    QCOMPARE(s.sourceLayerId, QString());
    QCOMPARE(s.mode, MaskMode::ClipInside);
}

void TestMaskSpec::defaults_jsonIsEmpty()
{
    MaskSpec s;
    QVERIFY(s.toJson().isEmpty());
}

void TestMaskSpec::roundTrip_preservesEveryField()
{
    MaskSpec s;
    s.enabled = true;
    s.sourceLayerId = QStringLiteral("layer-uuid-1234");
    s.mode = MaskMode::ClipOutside;

    MaskSpec back = MaskSpec::fromJson(s.toJson());
    QCOMPARE(back.enabled, s.enabled);
    QCOMPARE(back.sourceLayerId, s.sourceLayerId);
    QCOMPARE(back.mode, s.mode);
}

void TestMaskSpec::roundTrip_disabledOmitted()
{
    MaskSpec s;
    s.sourceLayerId = QStringLiteral("x");
    QVERIFY(!s.toJson().contains(QStringLiteral("enabled")));
}

void TestMaskSpec::roundTrip_emptySourceLayerIdOmitted()
{
    MaskSpec s;
    s.enabled = true;
    QVERIFY(!s.toJson().contains(QStringLiteral("sourceLayerId")));
}

void TestMaskSpec::roundTrip_clipInsideModeOmitted()
{
    MaskSpec s;
    s.enabled = true; // make JSON non-empty
    QVERIFY(!s.toJson().contains(QStringLiteral("mode")));
}

void TestMaskSpec::roundTrip_clipOutsideModeIncluded()
{
    MaskSpec s;
    s.mode = MaskMode::ClipOutside;
    QCOMPARE(s.toJson().value(QStringLiteral("mode")).toString(), QStringLiteral("clipOutside"));
}

void TestMaskSpec::equality_identicalSpecsAreEqual()
{
    MaskSpec a, b;
    a.enabled = b.enabled = true;
    a.sourceLayerId = b.sourceLayerId = QStringLiteral("x");
    a.mode = b.mode = MaskMode::ClipOutside;
    QCOMPARE(a, b);
}

void TestMaskSpec::equality_differentEnabledNotEqual()
{
    MaskSpec a, b;
    b.enabled = true;
    QVERIFY(a != b);
}

void TestMaskSpec::equality_differentSourceNotEqual()
{
    MaskSpec a, b;
    b.sourceLayerId = QStringLiteral("y");
    QVERIFY(a != b);
}

void TestMaskSpec::equality_differentModeNotEqual()
{
    MaskSpec a, b;
    b.mode = MaskMode::ClipOutside;
    QVERIFY(a != b);
}

QTEST_MAIN(TestMaskSpec)
#include "test_maskspec.moc"
