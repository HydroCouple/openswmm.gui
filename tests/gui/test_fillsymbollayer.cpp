/**
 * @file      test_fillsymbollayer.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for VS.2 — Fill Symbol Layer (third primitive).
 *
 *         Success criterion (VISUALIZATION_STYLING_OVERHAUL_PLAN.md §8.3 VS.2):
 *           - Brush / pen composition reflects every typed field.
 *           - SimpleFill props round-trip through SymbolLayer (kind == SimpleFill).
 *           - drawFill() fills the polygon interior with the fill color.
 *           - Outline-only (Qt::NoBrush) leaves the interior untouched.
 *           - fromSymbolLayer() is tolerant of empty props (defaults).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QBrush>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QtTest/QtTest>

#include "render/fillsymbollayer.h"
#include "render/symbollayer.h"

using namespace OpenSWMM::Render;

class TestFillSymbolLayer : public QObject
{
    Q_OBJECT
private slots:

    // ---- Brush / pen composition ----------------------------------------------

    /**
     *
     * @brief Require brush composition to retain configured color and Dense4Pattern style.
     *
     */
    void toQBrush_solidUsesColorAndStyle()
    {
        FillSymbolLayerSpec s;
        s.fillColor = QColor(10, 20, 30);
        s.fillStyle = Qt::Dense4Pattern;
        const QBrush b = s.toQBrush();
        QCOMPARE(b.style(), Qt::Dense4Pattern);
        QCOMPARE(b.color(), QColor(10, 20, 30));
    }

    /**
     *
     * @brief Require an explicit NoBrush fill to remain NoBrush.
     *
     */
    void toQBrush_noBrushWhenStyleIsNoBrush()
    {
        FillSymbolLayerSpec s;
        s.fillStyle = Qt::NoBrush;
        QCOMPARE(s.toQBrush().style(), Qt::NoBrush);
    }

    /**
     *
     * @brief Require outline color, fractional width, dash style and mitre join to compose into
     * QPen.
     *
     */
    void toQPen_composesEveryField()
    {
        FillSymbolLayerSpec s;
        s.outlineColor = QColor(1, 2, 3);
        s.outlineWidth = 2.5;
        s.outlinePenStyle = Qt::DashLine;
        s.joinStyle = Qt::MiterJoin;
        const QPen pen = s.toQPen();
        QCOMPARE(pen.color(), QColor(1, 2, 3));
        QCOMPARE(pen.widthF(), 2.5);
        QCOMPARE(pen.style(), Qt::DashLine);
        QCOMPARE(pen.joinStyle(), Qt::MiterJoin);
    }

    /**
     *
     * @brief Require explicit NoPen and zero outline width to suppress outline painting.
     *
     */
    void toQPen_noPenWhenStyleIsNoPenOrZeroWidth()
    {
        FillSymbolLayerSpec s;
        s.outlinePenStyle = Qt::NoPen;
        QCOMPARE(s.toQPen().style(), Qt::NoPen);

        FillSymbolLayerSpec s2;
        s2.outlineWidth = 0.0;
        QCOMPARE(s2.toQPen().style(), Qt::NoPen);
    }

    // ---- Round-trip through SymbolLayer ---------------------------------------

    /**
     *
     * @brief Require SimpleFill kind and all assigned fill/outline fields to survive typed
     * SymbolLayer conversion.
     *
     */
    void roundTrip_preservesEveryFieldAndSetsKind()
    {
        FillSymbolLayerSpec s;
        s.fillColor = QColor(120, 130, 140, 200);
        s.fillStyle = Qt::BDiagPattern;
        s.outlineColor = QColor(5, 6, 7);
        s.outlineWidth = 1.75;
        s.outlinePenStyle = Qt::DotLine;
        s.joinStyle = Qt::MiterJoin;

        const SymbolLayer layer = s.toSymbolLayer();
        QCOMPARE(layer.kind, SymbolLayerKind::SimpleFill);

        const FillSymbolLayerSpec r = FillSymbolLayerSpec::fromSymbolLayer(layer);
        QCOMPARE(r.fillColor, QColor(120, 130, 140, 200));
        QCOMPARE(r.fillStyle, Qt::BDiagPattern);
        QCOMPARE(r.outlineColor, QColor(5, 6, 7));
        QCOMPARE(r.outlineWidth, 1.75);
        QCOMPARE(r.outlinePenStyle, Qt::DotLine);
        QCOMPARE(r.joinStyle, Qt::MiterJoin);
    }

    /**
     *
     * @brief Require absent fill style, outline style and outline width to use defaults.
     *
     */
    void fromSymbolLayer_emptyPropsYieldsDefaults()
    {
        SymbolLayer layer; // no props
        const FillSymbolLayerSpec r = FillSymbolLayerSpec::fromSymbolLayer(layer);
        const FillSymbolLayerSpec def;
        QCOMPARE(r.fillStyle, def.fillStyle);
        QCOMPARE(r.outlinePenStyle, def.outlinePenStyle);
        QCOMPARE(r.outlineWidth, def.outlineWidth);
    }

    // ---- drawFill paint --------------------------------------------------------

    /**
     *
     * @brief Require polygon centre pixel to carry the configured red fill with outline disabled.
     *
     */
    void drawFill_fillsInterior()
    {
        QImage img(20, 20, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        QPainter p(&img);

        FillSymbolLayerSpec s;
        s.fillColor = QColor(220, 10, 10);
        s.fillStyle = Qt::SolidPattern;
        s.outlinePenStyle = Qt::NoPen;

        QPolygonF poly;
        poly << QPointF(2, 2) << QPointF(18, 2) << QPointF(18, 18) << QPointF(2, 18);
        drawFill(&p, poly, s);
        p.end();

        const QColor c = img.pixelColor(10, 10);
        QVERIFY(c.red() > 200);
        QVERIFY(c.green() < 80);
        QVERIFY(c.blue() < 80);
    }

    /**
     *
     * @brief Require outline-only drawing to leave the centre of the polygon white.
     *
     */
    void drawFill_outlineOnlyLeavesInteriorUntouched()
    {
        QImage img(20, 20, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        QPainter p(&img);

        FillSymbolLayerSpec s;
        s.fillStyle = Qt::NoBrush; // no fill
        s.outlineColor = QColor(0, 0, 220);
        s.outlineWidth = 2.0;
        s.outlinePenStyle = Qt::SolidLine;

        QPolygonF poly;
        poly << QPointF(2, 2) << QPointF(18, 2) << QPointF(18, 18) << QPointF(2, 18);
        drawFill(&p, poly, s);
        p.end();

        // Interior stays white; the outline only touches the border.
        QCOMPARE(img.pixelColor(10, 10), QColor(Qt::white));
    }

    /**
     *
     * @brief Require a representative interior pixel to stay white after drawing a two-point
     * polygon.
     * This case checks one pixel, not equality of the entire image.
     *
     */
    void drawFill_noOpForDegeneratePolygon()
    {
        QImage img(10, 10, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        QPainter p(&img);
        FillSymbolLayerSpec s;
        QPolygonF poly;
        poly << QPointF(1, 1) << QPointF(5, 5); // only 2 vertices
        drawFill(&p, poly, s);
        p.end();
        QCOMPARE(img.pixelColor(3, 3), QColor(Qt::white));
    }
};

QTEST_MAIN(TestFillSymbolLayer)
#include "test_fillsymbollayer.moc"
