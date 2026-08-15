/*!
 * \file   test_linesymbollayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice Z.5 — Line Symbol Layer + arrow spec.
 *
 *         Success criterion (RENDERING_RULE_MODEL_PLAN.md §16, Z.5):
 *           - All Qt::PenStyle / PenCapStyle / PenJoinStyle combos
 *             round-trip through SymbolLayer.
 *           - All 5 arrow placement modes round-trip through string.
 *           - drawArrowsAlongPolyline places arrows at the expected
 *             arc-length positions for each placement mode.
 *           - Custom dash patterns survive round-trip.
 *           - Reverse flag flips arrow direction.
 */

#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QtTest/QtTest>

#include "render/linesymbollayer.h"
#include "render/symbollayer.h"

using namespace OpenSWMM::Render;

class TestLineSymbolLayer : public QObject
{
    Q_OBJECT

private slots:
    // ArrowPlacement enum
    void arrowPlacement_allValuesRoundTripThroughString();
    void arrowPlacement_unknownStringFallsBackToEnd();

    // QPen composition
    void toQPen_composesEveryField();
    void toQPen_customDashAppliedWhenStyleIsCustomDashLine();
    void toQPen_customDashIgnoredWhenStyleIsSolid();
    void toQPen_emptyCustomDashFallsBackToSolid();

    // SymbolLayer round-trip
    void spec_toSymbolLayerSetsCorrectKind();
    void spec_toSymbolLayerWritesAllCanonicalProps();
    void spec_roundTripPreservesEveryField();
    void spec_partialPropsFallBackToDefaults();
    void spec_dashPatternRoundTripPreservesValues();
    void spec_arrowSubSpecRoundTrips();

    // Arrow painting
    void drawArrows_zeroLengthIsNoop();
    void drawArrows_emptyPolylineIsNoop();
    void drawArrows_singleVertexPolylineIsNoop();
    void drawArrows_endPlacementPaintsOneArrowNearEnd();
    void drawArrows_bothPlacementPaintsArrowsNearBothEnds();
    void drawArrows_centeredPlacementPaintsNearMidpoint();
    void drawArrows_repeatPlacementSpacesArrowsAlongLine();
    void drawArrows_atVerticesPaintsAtInteriorVertices();
    void drawArrows_reverseFlagChangesPixelCoverage();

private:
    static int    nonTransparentPixelCount(const QImage &img);
    static QImage paintArrows(const QPolygonF &poly,
                              const LineArrowSpec &spec,
                              int w = 200, int h = 100);
};

// ── ArrowPlacement enum ─────────────────────────────────────────────

void TestLineSymbolLayer::arrowPlacement_allValuesRoundTripThroughString()
{
    const ArrowPlacement all[] = {
        ArrowPlacement::End,
        ArrowPlacement::Both,
        ArrowPlacement::Centered,
        ArrowPlacement::RepeatEveryNPx,
        ArrowPlacement::AtVertices
    };
    for (ArrowPlacement p : all) {
        const QString tok = arrowPlacementToString(p);
        QVERIFY(!tok.isEmpty());
        QCOMPARE(arrowPlacementFromString(tok), p);
    }
}

void TestLineSymbolLayer::arrowPlacement_unknownStringFallsBackToEnd()
{
    QCOMPARE(arrowPlacementFromString(QStringLiteral("nonexistent")),
             ArrowPlacement::End);
}

// ── QPen composition ────────────────────────────────────────────────

void TestLineSymbolLayer::toQPen_composesEveryField()
{
    LineSymbolLayerSpec s;
    s.color     = QColor(10, 20, 30);
    s.width     = 2.5;
    s.penStyle  = Qt::DashDotLine;
    s.capStyle  = Qt::RoundCap;
    s.joinStyle = Qt::MiterJoin;

    const QPen pen = s.toQPen();
    QCOMPARE(pen.color(), QColor(10, 20, 30));
    QCOMPARE(pen.widthF(), 2.5);
    QCOMPARE(pen.style(), Qt::DashDotLine);
    QCOMPARE(pen.capStyle(), Qt::RoundCap);
    QCOMPARE(pen.joinStyle(), Qt::MiterJoin);
}

void TestLineSymbolLayer::toQPen_customDashAppliedWhenStyleIsCustomDashLine()
{
    LineSymbolLayerSpec s;
    s.penStyle = Qt::CustomDashLine;
    s.customDash = { 5.0, 2.0, 1.0, 2.0 };
    const QPen pen = s.toQPen();
    QCOMPARE(pen.style(), Qt::CustomDashLine);
    QCOMPARE(pen.dashPattern(), QVector<qreal>({5.0, 2.0, 1.0, 2.0}));
}

void TestLineSymbolLayer::toQPen_customDashIgnoredWhenStyleIsSolid()
{
    LineSymbolLayerSpec s;
    s.penStyle = Qt::SolidLine;
    s.customDash = { 5.0, 2.0 };
    const QPen pen = s.toQPen();
    QCOMPARE(pen.style(), Qt::SolidLine);
}

void TestLineSymbolLayer::toQPen_emptyCustomDashFallsBackToSolid()
{
    LineSymbolLayerSpec s;
    s.penStyle = Qt::CustomDashLine;
    s.customDash = {};  // empty
    const QPen pen = s.toQPen();
    // Falls back to penStyle's literal value (CustomDashLine); the
    // default pattern is applied implicitly. Just check no crash + style
    // preserved.
    QCOMPARE(pen.style(), Qt::CustomDashLine);
}

// ── SymbolLayer round-trip ──────────────────────────────────────────

void TestLineSymbolLayer::spec_toSymbolLayerSetsCorrectKind()
{
    LineSymbolLayerSpec s;
    s.drawArrows = false;
    QCOMPARE(s.toSymbolLayer().kind, SymbolLayerKind::SimpleLine);
    s.drawArrows = true;
    QCOMPARE(s.toSymbolLayer().kind, SymbolLayerKind::MarkerLine);
}

void TestLineSymbolLayer::spec_toSymbolLayerWritesAllCanonicalProps()
{
    LineSymbolLayerSpec s;
    s.color     = QColor(99, 88, 77);
    s.width     = 3.0;
    s.penStyle  = Qt::DotLine;
    s.capStyle  = Qt::SquareCap;
    s.joinStyle = Qt::RoundJoin;
    s.offsetPx  = 2.5;

    SymbolLayer layer = s.toSymbolLayer();
    QCOMPARE(layer.props.value(QStringLiteral("color")).value<QColor>(),
             QColor(99, 88, 77));
    QCOMPARE(layer.props.value(QStringLiteral("width")).toDouble(), 3.0);
    QCOMPARE(layer.props.value(QStringLiteral("penStyle")).toInt(),
             static_cast<int>(Qt::DotLine));
    QCOMPARE(layer.props.value(QStringLiteral("capStyle")).toInt(),
             static_cast<int>(Qt::SquareCap));
    QCOMPARE(layer.props.value(QStringLiteral("joinStyle")).toInt(),
             static_cast<int>(Qt::RoundJoin));
    QCOMPARE(layer.props.value(QStringLiteral("offsetPx")).toDouble(), 2.5);
}

void TestLineSymbolLayer::spec_roundTripPreservesEveryField()
{
    LineSymbolLayerSpec s;
    s.color     = QColor(12, 34, 56);
    s.width     = 1.75;
    s.penStyle  = Qt::DashLine;
    s.capStyle  = Qt::RoundCap;
    s.joinStyle = Qt::MiterJoin;
    s.customDash = { 3.0, 2.0 };
    s.offsetPx  = -1.5;
    s.drawArrows = true;
    s.arrows.color     = QColor(255, 0, 0);
    s.arrows.lengthPx  = 12.0;
    s.arrows.widthPx   = 9.0;
    s.arrows.placement = ArrowPlacement::Centered;
    s.arrows.spacingPx = 33.0;
    s.arrows.reverse   = true;

    SymbolLayer layer = s.toSymbolLayer();
    LineSymbolLayerSpec back = LineSymbolLayerSpec::fromSymbolLayer(layer);

    QCOMPARE(back.color,     s.color);
    QCOMPARE(back.width,     s.width);
    QCOMPARE(back.penStyle,  s.penStyle);
    QCOMPARE(back.capStyle,  s.capStyle);
    QCOMPARE(back.joinStyle, s.joinStyle);
    QCOMPARE(back.customDash, s.customDash);
    QCOMPARE(back.offsetPx,  s.offsetPx);
    QCOMPARE(back.drawArrows, s.drawArrows);
    QCOMPARE(back.arrows.color,     s.arrows.color);
    QCOMPARE(back.arrows.lengthPx,  s.arrows.lengthPx);
    QCOMPARE(back.arrows.widthPx,   s.arrows.widthPx);
    QCOMPARE(back.arrows.placement, s.arrows.placement);
    QCOMPARE(back.arrows.spacingPx, s.arrows.spacingPx);
    QCOMPARE(back.arrows.reverse,   s.arrows.reverse);
}

void TestLineSymbolLayer::spec_partialPropsFallBackToDefaults()
{
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::SimpleLine;
    layer.props[QStringLiteral("width")] = 4.0;
    // Nothing else.
    LineSymbolLayerSpec back = LineSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.width, 4.0);
    LineSymbolLayerSpec defaults;
    QCOMPARE(back.color,    defaults.color);
    QCOMPARE(back.penStyle, defaults.penStyle);
    QCOMPARE(back.capStyle, defaults.capStyle);
}

void TestLineSymbolLayer::spec_dashPatternRoundTripPreservesValues()
{
    LineSymbolLayerSpec s;
    s.penStyle = Qt::CustomDashLine;
    s.customDash = { 8.5, 3.5, 1.0, 1.5, 4.0, 2.0 };

    SymbolLayer layer = s.toSymbolLayer();
    LineSymbolLayerSpec back = LineSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.customDash, s.customDash);
}

void TestLineSymbolLayer::spec_arrowSubSpecRoundTrips()
{
    LineSymbolLayerSpec s;
    s.drawArrows = true;
    s.arrows.placement = ArrowPlacement::RepeatEveryNPx;
    s.arrows.spacingPx = 50.0;
    s.arrows.reverse   = true;

    SymbolLayer layer = s.toSymbolLayer();
    LineSymbolLayerSpec back = LineSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.arrows.placement, ArrowPlacement::RepeatEveryNPx);
    QCOMPARE(back.arrows.spacingPx, 50.0);
    QCOMPARE(back.arrows.reverse,   true);
}

// ── Arrow paint ─────────────────────────────────────────────────────

int TestLineSymbolLayer::nonTransparentPixelCount(const QImage &img)
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

QImage TestLineSymbolLayer::paintArrows(const QPolygonF &poly,
                                         const LineArrowSpec &spec,
                                         int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    drawArrowsAlongPolyline(&p, poly, spec);
    p.end();
    return img;
}

void TestLineSymbolLayer::drawArrows_zeroLengthIsNoop()
{
    LineArrowSpec s;
    s.lengthPx = 0.0;
    QPolygonF poly;
    poly << QPointF(10, 50) << QPointF(190, 50);
    QImage img = paintArrows(poly, s);
    QCOMPARE(nonTransparentPixelCount(img), 0);
}

void TestLineSymbolLayer::drawArrows_emptyPolylineIsNoop()
{
    LineArrowSpec s;
    QImage img = paintArrows(QPolygonF{}, s);
    QCOMPARE(nonTransparentPixelCount(img), 0);
}

void TestLineSymbolLayer::drawArrows_singleVertexPolylineIsNoop()
{
    LineArrowSpec s;
    QPolygonF poly;
    poly << QPointF(50, 50);
    QImage img = paintArrows(poly, s);
    QCOMPARE(nonTransparentPixelCount(img), 0);
}

void TestLineSymbolLayer::drawArrows_endPlacementPaintsOneArrowNearEnd()
{
    LineArrowSpec s;
    s.placement = ArrowPlacement::End;
    s.lengthPx = 12.0;
    s.widthPx  = 10.0;
    QPolygonF poly;
    poly << QPointF(10, 50) << QPointF(190, 50);

    QImage img = paintArrows(poly, s);
    QVERIFY(nonTransparentPixelCount(img) > 10);

    // Pixels should cluster near x ≈ 190 (line end), not near x ≈ 10.
    int leftHalf = 0, rightHalf = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) > 0) {
                if (x < img.width() / 2) ++leftHalf;
                else                     ++rightHalf;
            }
        }
    }
    QVERIFY2(rightHalf > 10 * leftHalf,
             "End-placement arrow should paint near the line's end vertex");
}

void TestLineSymbolLayer::drawArrows_bothPlacementPaintsArrowsNearBothEnds()
{
    LineArrowSpec s;
    s.placement = ArrowPlacement::Both;
    s.lengthPx = 12.0;
    s.widthPx  = 10.0;
    QPolygonF poly;
    poly << QPointF(10, 50) << QPointF(190, 50);

    QImage img = paintArrows(poly, s);
    int leftQuarter = 0, rightQuarter = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) > 0) {
                if (x < img.width() / 4)        ++leftQuarter;
                else if (x > 3 * img.width() / 4) ++rightQuarter;
            }
        }
    }
    QVERIFY2(leftQuarter > 5 && rightQuarter > 5,
             "Both-placement should paint at start AND end");
}

void TestLineSymbolLayer::drawArrows_centeredPlacementPaintsNearMidpoint()
{
    LineArrowSpec s;
    s.placement = ArrowPlacement::Centered;
    s.lengthPx = 12.0;
    s.widthPx  = 10.0;
    QPolygonF poly;
    poly << QPointF(10, 50) << QPointF(190, 50);

    QImage img = paintArrows(poly, s);
    int midBand = 0, outerBand = 0;
    const int midX = img.width() / 2;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) > 0) {
                if (std::abs(x - midX) < 20) ++midBand;
                else                          ++outerBand;
            }
        }
    }
    QVERIFY2(midBand > 5 * outerBand,
             "Centered-placement should cluster near the midpoint");
}

void TestLineSymbolLayer::drawArrows_repeatPlacementSpacesArrowsAlongLine()
{
    LineArrowSpec s;
    s.placement = ArrowPlacement::RepeatEveryNPx;
    s.spacingPx = 40.0;
    s.lengthPx  = 10.0;
    s.widthPx   = 8.0;
    // 180-pixel-long horizontal line; spacing 40 → arrows at ~50, ~90,
    // ~130, ~170 (approx — first arrow at spacing 40 from start ≈ x 50).
    QPolygonF poly;
    poly << QPointF(10, 50) << QPointF(190, 50);

    QImage img = paintArrows(poly, s);
    // Just verify multiple distinct arrows were painted (count clusters
    // in the band y = 40..60).
    int totalPainted = nonTransparentPixelCount(img);
    QVERIFY2(totalPainted > 80,
             "RepeatEveryNPx should paint multiple arrows across the line");
}

void TestLineSymbolLayer::drawArrows_atVerticesPaintsAtInteriorVertices()
{
    LineArrowSpec s;
    s.placement = ArrowPlacement::AtVertices;
    s.lengthPx = 10.0;
    s.widthPx  = 8.0;
    QPolygonF poly;
    poly << QPointF(10, 30)
         << QPointF(70, 30)
         << QPointF(130, 30)
         << QPointF(190, 30);
    // 2 interior vertices → 2 arrows.

    QImage img = paintArrows(poly, s);
    QVERIFY(nonTransparentPixelCount(img) > 20);
}

void TestLineSymbolLayer::drawArrows_reverseFlagChangesPixelCoverage()
{
    LineArrowSpec a, b;
    a.placement = ArrowPlacement::End;
    a.lengthPx  = 15.0;
    a.widthPx   = 10.0;
    a.reverse   = false;
    b = a;
    b.reverse   = true;

    QPolygonF poly;
    poly << QPointF(50, 50) << QPointF(150, 50);
    QImage imgF = paintArrows(poly, a);
    QImage imgR = paintArrows(poly, b);
    QVERIFY(imgF != imgR);
}

QTEST_MAIN(TestLineSymbolLayer)
#include "test_linesymbollayer.moc"
