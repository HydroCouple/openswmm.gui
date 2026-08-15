/*!
 * \file   test_maskspec.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice Z.14-data — MaskSpec value type.
 */

#include <QtTest/QtTest>

#include "render/maskspec.h"

using namespace OpenSWMM::Render;

class TestMaskSpec : public QObject
{
    Q_OBJECT
private slots:
    void mode_bothValuesRoundTripThroughString();
    void mode_unknownStringFallsBackToClipInside();

    void defaults_areSensible();
    void defaults_jsonIsEmpty();

    void roundTrip_preservesEveryField();
    void roundTrip_disabledOmitted();
    void roundTrip_emptySourceLayerIdOmitted();
    void roundTrip_clipInsideModeOmitted();
    void roundTrip_clipOutsideModeIncluded();

    void equality_identicalSpecsAreEqual();
    void equality_differentEnabledNotEqual();
    void equality_differentSourceNotEqual();
    void equality_differentModeNotEqual();
};

void TestMaskSpec::mode_bothValuesRoundTripThroughString()
{
    for (MaskMode m : { MaskMode::ClipInside, MaskMode::ClipOutside }) {
        const QString s = maskModeToString(m);
        QVERIFY(!s.isEmpty());
        QCOMPARE(maskModeFromString(s), m);
    }
}

void TestMaskSpec::mode_unknownStringFallsBackToClipInside()
{
    QCOMPARE(maskModeFromString(QStringLiteral("nope")), MaskMode::ClipInside);
    QCOMPARE(maskModeFromString(QString()),              MaskMode::ClipInside);
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
    QCOMPARE(back.enabled,       s.enabled);
    QCOMPARE(back.sourceLayerId, s.sourceLayerId);
    QCOMPARE(back.mode,          s.mode);
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
    s.enabled = true;  // make JSON non-empty
    QVERIFY(!s.toJson().contains(QStringLiteral("mode")));
}

void TestMaskSpec::roundTrip_clipOutsideModeIncluded()
{
    MaskSpec s;
    s.mode = MaskMode::ClipOutside;
    QCOMPARE(s.toJson().value(QStringLiteral("mode")).toString(),
             QStringLiteral("clipOutside"));
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
