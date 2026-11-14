/**
 * @file      test_diagramspec.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Slice Z.12-data — DiagramSpec value type.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QColor>
#include <QtTest/QtTest>

#include "render/diagramspec.h"

using namespace OpenSWMM::Render;

class TestDiagramSpec : public QObject
{
    Q_OBJECT
private slots:
    // Enum
    /**
     *
     * @brief Round-trip Pie, Bar, TimeSeries and Histogram diagram types through string tokens.
     *
     */
    void type_allValuesRoundTripThroughString();
    /**
     *
     * @brief Require an unknown diagram token to resolve to Pie.
     *
     */
    void type_unknownStringFallsBackToPie();

    // Defaults
    /**
     *
     * @brief Pin disabled Pie defaults, empty fields/expression/palette, 40x40-pixel size, zero
     * offset and zero range.
     *
     */
    void defaults_areSensible();
    /**
     *
     * @brief Require a default diagram specification to serialize with no fields.
     *
     */
    void defaults_jsonIsEmpty();

    // Round-trip
    /**
     *
     * @brief Round-trip the assigned static diagram fields, checking attributes/size/offset,
     * palette count/first color and maximum range.
     *
     */
    void roundTrip_preservesEveryStaticField();
    /**
     *
     * @brief Require TimeSeries type and series-expression text to survive JSON round-trip.
     *
     */
    void roundTrip_preservesTimeSeriesExpression();
    /**
     *
     * @brief Require three explicit RGB palette colors to retain their order.
     *
     */
    void roundTrip_palettePreservedInOrder();
    /**
     *
     * @brief Require attribute names c,a,b to retain order through JSON.
     *
     */
    void roundTrip_attributesPreservedInOrder();

    // Default-elision
    /**
     *
     * @brief Require the default Pie type to be omitted from enabled JSON.
     *
     */
    void defaultType_omittedFromJson();
    /**
     *
     * @brief Require default width/height fields to be absent from enabled JSON.
     *
     */
    void defaultSize_omittedFromJson();
    /**
     *
     * @brief Require default offset x/y fields to be absent from enabled JSON.
     *
     */
    void defaultOffset_omittedFromJson();

    // Equality
    /**
     *
     * @brief Require matching attributes and Bar type to compare equal.
     *
     */
    void equality_identicalSpecsAreEqual();
    /**
     *
     * @brief Require differing diagram types to compare unequal.
     *
     */
    void equality_differentTypeNotEqual();
    /**
     *
     * @brief Require differing attribute lists to compare unequal.
     *
     */
    void equality_differentAttributesNotEqual();
};

void TestDiagramSpec::type_allValuesRoundTripThroughString()
{
    for (DiagramType t :
         {DiagramType::Pie, DiagramType::Bar, DiagramType::TimeSeries, DiagramType::Histogram}) {
        QCOMPARE(diagramTypeFromString(diagramTypeToString(t)), t);
    }
}

void TestDiagramSpec::type_unknownStringFallsBackToPie()
{
    QCOMPARE(diagramTypeFromString(QStringLiteral("nope")), DiagramType::Pie);
}

void TestDiagramSpec::defaults_areSensible()
{
    DiagramSpec s;
    QCOMPARE(s.enabled, false);
    QCOMPARE(s.type, DiagramType::Pie);
    QVERIFY(s.attributes.isEmpty());
    QCOMPARE(s.seriesExpression, QString());
    QCOMPARE(s.sizePx, QSizeF(40.0, 40.0));
    QCOMPARE(s.offsetPx, QPointF(0.0, 0.0));
    QVERIFY(s.palette.isEmpty());
    QCOMPARE(s.rangeMin, 0.0);
    QCOMPARE(s.rangeMax, 0.0);
}

void TestDiagramSpec::defaults_jsonIsEmpty()
{
    QVERIFY(DiagramSpec().toJson().isEmpty());
}

void TestDiagramSpec::roundTrip_preservesEveryStaticField()
{
    DiagramSpec s;
    s.enabled = true;
    s.type = DiagramType::Bar;
    s.attributes = {QStringLiteral("runoff"), QStringLiteral("infil")};
    s.sizePx = QSizeF(60.0, 30.0);
    s.offsetPx = QPointF(5.0, -10.0);
    s.palette = {QColor(Qt::red), QColor(Qt::blue)};
    s.rangeMin = 0.0;
    s.rangeMax = 100.0;

    DiagramSpec back = DiagramSpec::fromJson(s.toJson());
    QCOMPARE(back.enabled, s.enabled);
    QCOMPARE(back.type, s.type);
    QCOMPARE(back.attributes, s.attributes);
    QCOMPARE(back.sizePx, s.sizePx);
    QCOMPARE(back.offsetPx, s.offsetPx);
    QCOMPARE(back.palette.size(), s.palette.size());
    QCOMPARE(back.palette[0].rgb(), QColor(Qt::red).rgb());
    QCOMPARE(back.rangeMax, s.rangeMax);
}

void TestDiagramSpec::roundTrip_preservesTimeSeriesExpression()
{
    DiagramSpec s;
    s.enabled = true;
    s.type = DiagramType::TimeSeries;
    s.seriesExpression = QStringLiteral("depth_series");

    DiagramSpec back = DiagramSpec::fromJson(s.toJson());
    QCOMPARE(back.type, s.type);
    QCOMPARE(back.seriesExpression, s.seriesExpression);
}

void TestDiagramSpec::roundTrip_palettePreservedInOrder()
{
    DiagramSpec s;
    s.palette = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255)};
    DiagramSpec back = DiagramSpec::fromJson(s.toJson());
    QCOMPARE(back.palette.size(), 3);
    QCOMPARE(back.palette[0].rgb(), QColor(255, 0, 0).rgb());
    QCOMPARE(back.palette[1].rgb(), QColor(0, 255, 0).rgb());
    QCOMPARE(back.palette[2].rgb(), QColor(0, 0, 255).rgb());
}

void TestDiagramSpec::roundTrip_attributesPreservedInOrder()
{
    DiagramSpec s;
    s.attributes = {QStringLiteral("z"), QStringLiteral("a"), QStringLiteral("m")};
    DiagramSpec back = DiagramSpec::fromJson(s.toJson());
    QCOMPARE(back.attributes,
             QStringList({QStringLiteral("z"), QStringLiteral("a"), QStringLiteral("m")}));
}

void TestDiagramSpec::defaultType_omittedFromJson()
{
    DiagramSpec s;
    s.enabled = true; // make JSON non-empty
    QVERIFY(!s.toJson().contains(QStringLiteral("type")));
}

void TestDiagramSpec::defaultSize_omittedFromJson()
{
    DiagramSpec s;
    s.enabled = true;
    const QJsonObject j = s.toJson();
    QVERIFY(!j.contains(QStringLiteral("sizeW")));
    QVERIFY(!j.contains(QStringLiteral("sizeH")));
}

void TestDiagramSpec::defaultOffset_omittedFromJson()
{
    DiagramSpec s;
    s.enabled = true;
    const QJsonObject j = s.toJson();
    QVERIFY(!j.contains(QStringLiteral("offsetX")));
    QVERIFY(!j.contains(QStringLiteral("offsetY")));
}

void TestDiagramSpec::equality_identicalSpecsAreEqual()
{
    DiagramSpec a, b;
    a.attributes = b.attributes = {QStringLiteral("x")};
    a.type = b.type = DiagramType::Bar;
    QCOMPARE(a, b);
}

void TestDiagramSpec::equality_differentTypeNotEqual()
{
    DiagramSpec a, b;
    b.type = DiagramType::Histogram;
    QVERIFY(a != b);
}

void TestDiagramSpec::equality_differentAttributesNotEqual()
{
    DiagramSpec a, b;
    b.attributes = {QStringLiteral("z")};
    QVERIFY(a != b);
}

QTEST_MAIN(TestDiagramSpec)
#include "test_diagramspec.moc"
