/*!
 * \file   test_seriesstyleobject.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Pins the QObject-wrapped SeriesStyle contract:
 *   - Constructors expose every SeriesStyle field via Q_PROPERTY getters
 *   - Typed setters fire per-field NOTIFY + aggregate styleChanged()
 *   - setStyle() emits exactly one aggregate signal
 *   - JSON round-trip preserves every field including the new
 *     cap/join/marker-border/point-label/area-fill ones
 *   - applySeriesStyle() drives a real QLineSeries (pen, opacity,
 *     point-label format, area-fill brush)
 *   - The effective* fallbacks derive sensible colours from `color`
 *     when the per-aspect colours are invalid
 */
#include "plot/seriesstyle.h"
#include "plot/seriesstyleobject.h"
#include "ui/widgets/seriesstyleeditor.h"

#include <qpropertyitemdelegate.h>
#include <qpropertymodel.h>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QLineSeries>
#include <QObject>
#include <QPen>
#include <QSignalSpy>
#include <QTest>
#include <QTreeView>

using openswmmvis::plot::MarkerShape;
using openswmmvis::plot::SeriesStyle;
using openswmmvis::plot::SeriesStyleObject;

class TestSeriesStyleObject : public QObject
{
    Q_OBJECT
private slots:
    void gettersExposeInitialStyle();
    void typedSettersFireBothSignals();
    void setStyleEmitsOneAggregateSignal();
    void jsonRoundTripsAllFields();
    void applyToLineSeriesSetsPenAndLabels();
    void effectiveColorsFallBackToMainColor();
    void displayLabelsAreGroupPrefixed();
    void shapeEnumMirrorsMarkerShape();
    void legendOverrideIsSpecLevelNotStyle();
    void editorUsesQPropertyModelDelegate();
};

// ---------------------------------------------------------------------------
void TestSeriesStyleObject::gettersExposeInitialStyle()
{
    SeriesStyle s;
    s.color             = QColor(10, 20, 30);
    s.opacity           = 0.5;
    s.legendName        = QStringLiteral("alpha");
    s.lineWidth         = 2.4;
    s.dash              = Qt::DashLine;
    s.capStyle          = Qt::RoundCap;
    s.joinStyle         = Qt::MiterJoin;
    s.showMarkers       = true;
    s.shape             = MarkerShape::Triangle;
    s.markerSize        = 8.0;
    s.markerBorderWidth = 1.5;
    s.showPointLabels   = true;
    s.pointLabelPrecision = 3;
    s.showAreaFill      = true;

    SeriesStyleObject obj(s);
    QCOMPARE(obj.color(),             QColor(10, 20, 30));
    QCOMPARE(obj.opacity(),           0.5);
    QCOMPARE(obj.legendName(),        QStringLiteral("alpha"));
    QCOMPARE(obj.lineWidth(),         2.4);
    QCOMPARE(obj.dash(),              Qt::DashLine);
    QCOMPARE(obj.capStyle(),          Qt::RoundCap);
    QCOMPARE(obj.joinStyle(),         Qt::MiterJoin);
    QVERIFY(obj.showMarkers());
    QCOMPARE(static_cast<MarkerShape>(obj.shape()), MarkerShape::Triangle);
    QCOMPARE(obj.markerSize(),        8.0);
    QCOMPARE(obj.markerBorderWidth(), 1.5);
    QVERIFY(obj.showPointLabels());
    QCOMPARE(obj.pointLabelPrecision(), 3);
    QVERIFY(obj.showAreaFill());

    // The aggregate snapshot must mirror the held struct.
    const SeriesStyle out = obj.style();
    QCOMPARE(out.color, s.color);
    QCOMPARE(out.dash,  s.dash);
    QCOMPARE(out.shape, s.shape);
}

// ---------------------------------------------------------------------------
void TestSeriesStyleObject::typedSettersFireBothSignals()
{
    SeriesStyleObject obj;
    QSignalSpy widthSpy  (&obj, &SeriesStyleObject::lineWidthChanged);
    QSignalSpy capSpy    (&obj, &SeriesStyleObject::capStyleChanged);
    QSignalSpy shapeSpy  (&obj, &SeriesStyleObject::shapeChanged);
    QSignalSpy aggSpy    (&obj, &SeriesStyleObject::styleChanged);

    obj.setLineWidth(3.0);
    obj.setCapStyle(Qt::SquareCap);
    obj.setShape(SeriesStyleObject::MarkerShapeQ::Diamond);

    QCOMPARE(widthSpy.count(), 1);
    QCOMPARE(capSpy.count(),   1);
    QCOMPARE(shapeSpy.count(), 1);
    QCOMPARE(aggSpy.count(),   3);   // one aggregate per typed setter

    // No-op setters must not re-fire either signal.
    obj.setLineWidth(3.0);
    QCOMPARE(widthSpy.count(), 1);
    QCOMPARE(aggSpy.count(),   3);
}

// ---------------------------------------------------------------------------
void TestSeriesStyleObject::setStyleEmitsOneAggregateSignal()
{
    SeriesStyleObject obj;
    QSignalSpy aggSpy(&obj, &SeriesStyleObject::styleChanged);

    SeriesStyle next;
    next.color      = QColor(99, 0, 99);
    next.opacity    = 0.42;
    next.lineWidth  = 5.0;
    next.dash       = Qt::DotLine;
    obj.setStyle(next);

    // setStyle() temporarily blocks per-field signals and emits the
    // aggregate exactly once at the end — even though many fields changed.
    QCOMPARE(aggSpy.count(), 1);
    QCOMPARE(obj.color(),     QColor(99, 0, 99));
    QCOMPARE(obj.opacity(),   0.42);
    QCOMPARE(obj.lineWidth(), 5.0);
    QCOMPARE(obj.dash(),      Qt::DotLine);
}

// ---------------------------------------------------------------------------
void TestSeriesStyleObject::jsonRoundTripsAllFields()
{
    SeriesStyle s;
    s.color             = QColor::fromRgb(11, 22, 33);
    s.opacity           = 0.77;
    s.legendName        = QStringLiteral("legend X");
    s.showLine          = false;
    s.lineWidth         = 4.25;
    s.dash              = Qt::DashDotDotLine;
    s.capStyle          = Qt::SquareCap;
    s.joinStyle         = Qt::RoundJoin;
    s.showMarkers       = true;
    s.shape             = MarkerShape::Plus;
    s.markerSize        = 11.0;
    s.markerFillColor   = QColor(200, 100, 50);
    s.markerBorderColor = QColor(40, 80, 120);
    s.markerBorderWidth = 2.5;
    s.showPointLabels   = true;
    s.pointLabelFont    = QFont(QStringLiteral("Courier"), 14);
    s.pointLabelColor   = QColor(50, 50, 50);
    s.pointLabelPrecision = 4;
    s.pointLabelFormat  = QStringLiteral("%.1f m");
    s.showAreaFill      = true;
    s.areaFillColor     = QColor(0, 128, 255, 60);

    const QJsonObject j = s.toJson();
    const SeriesStyle back = SeriesStyle::fromJson(j);

    QCOMPARE(back.color.rgba(),             s.color.rgba());
    QCOMPARE(back.opacity,                  s.opacity);
    QCOMPARE(back.legendName,               s.legendName);
    QCOMPARE(back.showLine,                 s.showLine);
    QCOMPARE(back.lineWidth,                s.lineWidth);
    QCOMPARE(back.dash,                     s.dash);
    QCOMPARE(back.capStyle,                 s.capStyle);
    QCOMPARE(back.joinStyle,                s.joinStyle);
    QCOMPARE(back.showMarkers,              s.showMarkers);
    QCOMPARE(back.shape,                    s.shape);
    QCOMPARE(back.markerSize,               s.markerSize);
    QCOMPARE(back.markerFillColor.rgba(),   s.markerFillColor.rgba());
    QCOMPARE(back.markerBorderColor.rgba(), s.markerBorderColor.rgba());
    QCOMPARE(back.markerBorderWidth,        s.markerBorderWidth);
    QCOMPARE(back.showPointLabels,          s.showPointLabels);
    QCOMPARE(back.pointLabelFont.toString(), s.pointLabelFont.toString());
    QCOMPARE(back.pointLabelColor.rgba(),   s.pointLabelColor.rgba());
    QCOMPARE(back.pointLabelPrecision,      s.pointLabelPrecision);
    QCOMPARE(back.pointLabelFormat,         s.pointLabelFormat);
    QCOMPARE(back.showAreaFill,             s.showAreaFill);
    QCOMPARE(back.areaFillColor.rgba(),     s.areaFillColor.rgba());

    // Invalid per-aspect colours must NOT serialise (we use absence to mean
    // "derive from main colour"). Confirm the JSON omits them when invalid.
    SeriesStyle minimal;
    const QJsonObject jm = minimal.toJson();
    QVERIFY(!jm.contains(QStringLiteral("markerFillColor")));
    QVERIFY(!jm.contains(QStringLiteral("markerBorderColor")));
    QVERIFY(!jm.contains(QStringLiteral("pointLabelColor")));
    QVERIFY(!jm.contains(QStringLiteral("areaFillColor")));
}

// ---------------------------------------------------------------------------
void TestSeriesStyleObject::applyToLineSeriesSetsPenAndLabels()
{
    SeriesStyle s;
    s.color             = QColor(120, 30, 200);
    s.opacity           = 0.8;
    s.lineWidth         = 2.75;
    s.dash              = Qt::DashLine;
    s.capStyle          = Qt::RoundCap;
    s.joinStyle         = Qt::MiterJoin;
    s.showLine          = true;
    s.legendName        = QStringLiteral("my series");
    s.showPointLabels   = true;
    s.pointLabelPrecision = 3;
    s.pointLabelColor   = QColor(0, 0, 0);
    s.showAreaFill      = true;
    s.areaFillColor     = QColor(120, 30, 200, 80);

    QLineSeries series;
    openswmmvis::plot::applySeriesStyle(s, &series);

    const QPen pen = series.pen();
    QCOMPARE(pen.style(),     Qt::DashLine);
    QCOMPARE(pen.capStyle(),  Qt::RoundCap);
    QCOMPARE(pen.joinStyle(), Qt::MiterJoin);
    QCOMPARE(pen.widthF(),    2.75);

    // Opacity baked into the pen colour AND on the series.
    QCOMPARE(series.opacity(), 0.8);
    // The pen colour alpha is base color alpha (255) * opacity (0.8) ≈ 204.
    QCOMPARE(pen.color().alpha(), 204);

    QCOMPARE(series.name(), QStringLiteral("my series"));

    // Point labels propagated.
    QVERIFY(series.pointLabelsVisible());
    QCOMPARE(series.pointLabelsFormat(), QStringLiteral("%.3f"));

    // Area fill brush set.
    QCOMPARE(series.brush().color().alpha(), 80);
}

// ---------------------------------------------------------------------------
void TestSeriesStyleObject::effectiveColorsFallBackToMainColor()
{
    SeriesStyle s;
    s.color = QColor(100, 150, 200);
    // All per-aspect colours left invalid → all four effective* getters
    // must return colours derived from `color`.

    QVERIFY(s.effectiveMarkerFillColor().isValid());
    QCOMPARE(s.effectiveMarkerFillColor(), s.color);  // fill defaults to main

    QVERIFY(s.effectiveMarkerBorderColor().isValid());
    QVERIFY(s.effectiveMarkerBorderColor() != s.color);  // border defaults to darker

    QVERIFY(s.effectivePointLabelColor().isValid());

    const QColor area = s.effectiveAreaFillColor();
    QVERIFY(area.isValid());
    QVERIFY(area.alpha() < 255);   // semi-transparent fallback

    // When explicit colours are set, they win.
    s.markerFillColor   = QColor(1, 2, 3);
    s.markerBorderColor = QColor(4, 5, 6);
    s.pointLabelColor   = QColor(7, 8, 9);
    s.areaFillColor     = QColor(10, 11, 12, 200);
    QCOMPARE(s.effectiveMarkerFillColor(),   QColor(1, 2, 3));
    QCOMPARE(s.effectiveMarkerBorderColor(), QColor(4, 5, 6));
    QCOMPARE(s.effectivePointLabelColor(),   QColor(7, 8, 9));
    QCOMPARE(s.effectiveAreaFillColor(),     QColor(10, 11, 12, 200));
}

// ---------------------------------------------------------------------------
void TestSeriesStyleObject::displayLabelsAreGroupPrefixed()
{
    SeriesStyleObject obj;
    QVERIFY(obj.displayLabelFor(QStringLiteral("color")).startsWith(QStringLiteral("Identity")));
    QVERIFY(obj.displayLabelFor(QStringLiteral("lineWidth")).startsWith(QStringLiteral("Line")));
    QVERIFY(obj.displayLabelFor(QStringLiteral("shape")).startsWith(QStringLiteral("Marker")));
    QVERIFY(obj.displayLabelFor(QStringLiteral("pointLabelFont")).startsWith(QStringLiteral("Labels")));
    QVERIFY(obj.displayLabelFor(QStringLiteral("areaFillColor")).startsWith(QStringLiteral("Area")));

    // Unknown property → empty string so QPropertyModel falls back to
    // the auto-generated name.
    QVERIFY(obj.displayLabelFor(QStringLiteral("notAProperty")).isEmpty());
}

// ---------------------------------------------------------------------------
void TestSeriesStyleObject::shapeEnumMirrorsMarkerShape()
{
    using MS  = MarkerShape;
    using MSQ = SeriesStyleObject::MarkerShapeQ;
    QCOMPARE(static_cast<int>(MSQ::Circle),   static_cast<int>(MS::Circle));
    QCOMPARE(static_cast<int>(MSQ::Square),   static_cast<int>(MS::Square));
    QCOMPARE(static_cast<int>(MSQ::Triangle), static_cast<int>(MS::Triangle));
    QCOMPARE(static_cast<int>(MSQ::Diamond),  static_cast<int>(MS::Diamond));
    QCOMPARE(static_cast<int>(MSQ::Cross),    static_cast<int>(MS::Cross));
    QCOMPARE(static_cast<int>(MSQ::Plus),     static_cast<int>(MS::Plus));
}

// ---------------------------------------------------------------------------
void TestSeriesStyleObject::legendOverrideIsSpecLevelNotStyle()
{
    // legendOverride mirrors SeriesSpec::legendOverride — it lives on the
    // editor's QObject so the QPropertyModel can edit it, but it is NOT
    // part of SeriesStyle and must NOT fire the aggregate styleChanged().
    SeriesStyleObject obj;
    QSignalSpy aggSpy(&obj, &SeriesStyleObject::styleChanged);
    QSignalSpy overSpy(&obj, &SeriesStyleObject::legendOverrideChanged);

    QCOMPARE(obj.legendOverride(), QString());

    obj.setLegendOverride(QStringLiteral("My custom label"));
    QCOMPARE(obj.legendOverride(), QStringLiteral("My custom label"));
    QCOMPARE(overSpy.count(), 1);
    QCOMPARE(aggSpy.count(),  0);    // style untouched

    // No-op setter is a no-op.
    obj.setLegendOverride(QStringLiteral("My custom label"));
    QCOMPARE(overSpy.count(), 1);

    // setStyle() leaves legendOverride alone — it only walks SeriesStyle
    // fields. Pre-existing override survives a full style swap.
    SeriesStyle next;
    next.color     = QColor(99, 99, 99);
    next.lineWidth = 3.5;
    obj.setStyle(next);
    QCOMPARE(obj.legendOverride(), QStringLiteral("My custom label"));

    // Display label is grouped under Identity.
    QVERIFY(obj.displayLabelFor(QStringLiteral("legendOverride"))
                .startsWith(QStringLiteral("Identity")));
}

// ---------------------------------------------------------------------------
void TestSeriesStyleObject::editorUsesQPropertyModelDelegate()
{
    openswmmvis::ui::SeriesStyleEditor editor;

    auto *tree = editor.findChild<QTreeView *>();
    QVERIFY(tree);
    QVERIFY(qobject_cast<QPropertyModel *>(tree->model()));
    QVERIFY(qobject_cast<QPropertyItemDelegate *>(tree->itemDelegate()));
}

QTEST_MAIN(TestSeriesStyleObject)
#include "test_seriesstyleobject.moc"
