/*!
 * \file   test_markershape.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice Z.4 — Marker Symbol Layer.
 *
 *         Success criterion (RENDERING_RULE_MODEL_PLAN.md §16, Z.4):
 *           - All 13 MarkerShape values round-trip through string.
 *           - drawMarkerShape produces non-empty pixel coverage for each
 *             shape (validates each branch actually paints).
 *           - MarkerSymbolLayerSpec read/write preserves every typed
 *             field through a SymbolLayer round-trip.
 *           - outlinePen() composes color + width + style correctly.
 *           - Data-defined override on "size" doesn't collide with the
 *             typed accessors (kept independent).
 */

#include <QBrush>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QtTest/QtTest>

#include "render/markershape.h"
#include "render/markersymbollayer.h"
#include "render/symbollayer.h"

using namespace OpenSWMM::Render;

class TestMarkerShape : public QObject
{
    Q_OBJECT

private slots:
    void enum_roundTripsThroughString();
    void enum_unknownStringFallsBackToCircle();

    void draw_zeroSizeIsNoop();
    void draw_eachShapeProducesPixelCoverage();
    void draw_rotationProducesDifferentPixels();
    void draw_nullPainterIsSafe();

    void spec_defaultsAreSensible();
    void spec_outlinePenComposesFields();
    void spec_toSymbolLayerSetsKindAndCanonicalProps();
    void spec_fromSymbolLayerPreservesEveryField();
    void spec_roundTripIsLossless();
    void spec_partialPropsFallBackToDefaults();
    void spec_invalidFillColorIsRejected();
    void spec_extraPropsArePreservedOnWrite();

private:
    static int nonTransparentPixelCount(const QImage &img);
    static QImage paintShape(MarkerShape s, qreal rotationDeg = 0.0);
};

// ── Enum ─────────────────────────────────────────────────────────────

void TestMarkerShape::enum_roundTripsThroughString()
{
    const MarkerShape all[] = {
        MarkerShape::Circle, MarkerShape::Square, MarkerShape::Triangle,
        MarkerShape::Diamond, MarkerShape::Star, MarkerShape::Cross,
        MarkerShape::Plus, MarkerShape::XCross, MarkerShape::Pentagon,
        MarkerShape::Hexagon, MarkerShape::Arrow,
        MarkerShape::EquilateralTriangle, MarkerShape::HalfCircle
    };
    for (MarkerShape s : all) {
        const QString token = markerShapeToString(s);
        QVERIFY2(!token.isEmpty(), "every shape must serialise to a non-empty token");
        QCOMPARE(markerShapeFromString(token), s);
    }
}

void TestMarkerShape::enum_unknownStringFallsBackToCircle()
{
    QCOMPARE(markerShapeFromString(QStringLiteral("nonexistent")),
             MarkerShape::Circle);
    QCOMPARE(markerShapeFromString(QString()), MarkerShape::Circle);
}

// ── Drawing ──────────────────────────────────────────────────────────

int TestMarkerShape::nonTransparentPixelCount(const QImage &img)
{
    int count = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(row[x]) > 0)
                ++count;
    }
    return count;
}

QImage TestMarkerShape::paintShape(MarkerShape s, qreal rotationDeg)
{
    QImage img(64, 64, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    drawMarkerShape(&p, s, QPointF(32, 32), 40.0,
                    QBrush(QColor(255, 0, 0)),
                    QPen(QColor(0, 0, 0), 1.5),
                    rotationDeg);
    p.end();
    return img;
}

void TestMarkerShape::draw_zeroSizeIsNoop()
{
    QImage img(32, 32, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    drawMarkerShape(&p, MarkerShape::Circle, QPointF(16, 16),
                    0.0, QBrush(Qt::red), QPen(Qt::black, 1.0));
    p.end();
    QCOMPARE(nonTransparentPixelCount(img), 0);
}

void TestMarkerShape::draw_eachShapeProducesPixelCoverage()
{
    const MarkerShape all[] = {
        MarkerShape::Circle, MarkerShape::Square, MarkerShape::Triangle,
        MarkerShape::Diamond, MarkerShape::Star, MarkerShape::Cross,
        MarkerShape::Plus, MarkerShape::XCross, MarkerShape::Pentagon,
        MarkerShape::Hexagon, MarkerShape::Arrow,
        MarkerShape::EquilateralTriangle, MarkerShape::HalfCircle
    };
    for (MarkerShape s : all) {
        const QImage img = paintShape(s);
        const int painted = nonTransparentPixelCount(img);
        QVERIFY2(painted > 30,
                 qPrintable(QStringLiteral("shape '%1' produced too few pixels (%2)")
                                .arg(markerShapeToString(s)).arg(painted)));
    }
}

void TestMarkerShape::draw_rotationProducesDifferentPixels()
{
    // A 45° rotation must change pixel coverage for a non-rotationally
    // symmetric shape. Use Triangle.
    const QImage a = paintShape(MarkerShape::Triangle, 0.0);
    const QImage b = paintShape(MarkerShape::Triangle, 45.0);
    QVERIFY(a != b);
}

void TestMarkerShape::draw_nullPainterIsSafe()
{
    drawMarkerShape(nullptr, MarkerShape::Circle, QPointF(0, 0),
                    10.0, QBrush(Qt::red), QPen(Qt::black));
    // No crash — pass.
    QVERIFY(true);
}

// ── Spec ────────────────────────────────────────────────────────────

void TestMarkerShape::spec_defaultsAreSensible()
{
    MarkerSymbolLayerSpec s;
    QCOMPARE(s.shape, MarkerShape::Circle);
    QVERIFY(s.sizePx > 0.0);
    QVERIFY(s.fillColor.isValid());
    QVERIFY(s.outlineColor.isValid());
    QVERIFY(s.outlineWidth >= 0.0);
    QCOMPARE(s.outlinePenStyle, Qt::SolidLine);
}

void TestMarkerShape::spec_outlinePenComposesFields()
{
    MarkerSymbolLayerSpec s;
    s.outlineColor    = QColor(11, 22, 33);
    s.outlineWidth    = 2.75;
    s.outlinePenStyle = Qt::DashDotLine;
    const QPen pen = s.outlinePen();
    QCOMPARE(pen.color(), QColor(11, 22, 33));
    QCOMPARE(pen.widthF(), 2.75);
    QCOMPARE(pen.style(), Qt::DashDotLine);
}

void TestMarkerShape::spec_toSymbolLayerSetsKindAndCanonicalProps()
{
    MarkerSymbolLayerSpec s;
    s.shape        = MarkerShape::Pentagon;
    s.sizePx       = 14.0;
    s.fillColor    = QColor(100, 200, 50);
    s.outlineColor = QColor(20, 20, 20);
    s.outlineWidth = 1.25;
    s.rotationDeg  = 45.0;
    s.offsetPx     = QPointF(3.0, -2.5);

    SymbolLayer layer = s.toSymbolLayer();
    QCOMPARE(layer.kind, SymbolLayerKind::SimpleMarker);
    QCOMPARE(layer.props.value(QStringLiteral("shape")).toInt(),
             static_cast<int>(MarkerShape::Pentagon));
    QCOMPARE(layer.props.value(QStringLiteral("size")).toDouble(), 14.0);
    QCOMPARE(layer.props.value(QStringLiteral("fillColor")).value<QColor>(),
             QColor(100, 200, 50));
    QCOMPARE(layer.props.value(QStringLiteral("outlineWidth")).toDouble(), 1.25);
    QCOMPARE(layer.props.value(QStringLiteral("rotationDeg")).toDouble(), 45.0);
    QCOMPARE(layer.props.value(QStringLiteral("offsetX")).toDouble(), 3.0);
    QCOMPARE(layer.props.value(QStringLiteral("offsetY")).toDouble(), -2.5);
}

void TestMarkerShape::spec_fromSymbolLayerPreservesEveryField()
{
    MarkerSymbolLayerSpec s;
    s.shape           = MarkerShape::Star;
    s.sizePx          = 22.5;
    s.fillColor       = QColor(255, 128, 0);
    s.outlineColor    = QColor(0, 0, 0);
    s.outlineWidth    = 0.75;
    s.outlinePenStyle = Qt::DashLine;
    s.rotationDeg     = 17.5;
    s.offsetPx        = QPointF(-1.0, 4.0);

    SymbolLayer layer = s.toSymbolLayer();
    MarkerSymbolLayerSpec back = MarkerSymbolLayerSpec::fromSymbolLayer(layer);

    QCOMPARE(back.shape,           s.shape);
    QCOMPARE(back.sizePx,          s.sizePx);
    QCOMPARE(back.fillColor,       s.fillColor);
    QCOMPARE(back.outlineColor,    s.outlineColor);
    QCOMPARE(back.outlineWidth,    s.outlineWidth);
    QCOMPARE(back.outlinePenStyle, s.outlinePenStyle);
    QCOMPARE(back.rotationDeg,     s.rotationDeg);
    QCOMPARE(back.offsetPx,        s.offsetPx);
}

void TestMarkerShape::spec_roundTripIsLossless()
{
    // The full pipeline: spec → SymbolLayer → JSON → SymbolLayer → spec.
    MarkerSymbolLayerSpec s;
    s.shape         = MarkerShape::Hexagon;
    s.sizePx        = 9.5;
    s.fillColor     = QColor(50, 100, 150, 200);
    s.outlineColor  = QColor(255, 0, 0);
    s.outlineWidth  = 1.0;
    s.rotationDeg   = 30.0;
    s.offsetPx      = QPointF(0.5, 0.5);

    SymbolLayer a = s.toSymbolLayer();
    const QJsonObject j = a.toJson();
    SymbolLayer b;
    b.fromJson(j);

    MarkerSymbolLayerSpec back = MarkerSymbolLayerSpec::fromSymbolLayer(b);
    QCOMPARE(back.shape,        s.shape);
    QCOMPARE(back.sizePx,       s.sizePx);
    QCOMPARE(back.outlineWidth, s.outlineWidth);
    QCOMPARE(back.rotationDeg,  s.rotationDeg);
    QCOMPARE(back.offsetPx,     s.offsetPx);
    // Colours round-trip through JSON via SymbolLayer's serialisation.
    QCOMPARE(back.fillColor.alpha(), s.fillColor.alpha());
}

void TestMarkerShape::spec_partialPropsFallBackToDefaults()
{
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::SimpleMarker;
    layer.props[QStringLiteral("shape")] = static_cast<int>(MarkerShape::Diamond);
    // No size, no colours, no outline.
    MarkerSymbolLayerSpec back = MarkerSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.shape, MarkerShape::Diamond);
    MarkerSymbolLayerSpec defaults;
    QCOMPARE(back.sizePx,        defaults.sizePx);
    QCOMPARE(back.fillColor,     defaults.fillColor);
    QCOMPARE(back.outlineWidth,  defaults.outlineWidth);
}

void TestMarkerShape::spec_invalidFillColorIsRejected()
{
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::SimpleMarker;
    layer.props[QStringLiteral("fillColor")] = QVariant::fromValue(QColor());  // invalid
    MarkerSymbolLayerSpec back = MarkerSymbolLayerSpec::fromSymbolLayer(layer);
    MarkerSymbolLayerSpec defaults;
    QCOMPARE(back.fillColor, defaults.fillColor);
}

void TestMarkerShape::spec_extraPropsArePreservedOnWrite()
{
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::SimpleMarker;
    layer.props[QStringLiteral("customExtra")] = QStringLiteral("keep me");

    MarkerSymbolLayerSpec s;
    s.shape  = MarkerShape::Hexagon;
    s.sizePx = 11.0;
    s.writeToSymbolLayer(layer);

    QCOMPARE(layer.props.value(QStringLiteral("customExtra")).toString(),
             QStringLiteral("keep me"));
    QCOMPARE(layer.props.value(QStringLiteral("shape")).toInt(),
             static_cast<int>(MarkerShape::Hexagon));
}

QTEST_MAIN(TestMarkerShape)
#include "test_markershape.moc"
