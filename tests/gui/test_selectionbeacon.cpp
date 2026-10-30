/**
 * @file      test_selectionbeacon.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for the selection locator overlay.
 *
 *         The contract that matters is SCREEN-space: the beacon must stay the
 *         same size however far the view is zoomed out, because the whole
 *         reason it exists is that a selected feature's own halo goes
 *         sub-pixel there. The zoom-dependent aggregation is the subtle part
 *         — folding point-rects with QRectF::united() silently yields an
 *         empty span (a zero-size rect is null), which collapsed every
 *         selection to one beacon at every zoom.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>

#include "map/selectionbeaconoverlay.h"

class TestSelectionBeacon : public QObject
{
    Q_OBJECT

private slots:
    void emptySelectionDrawsNothing();
    void offScreenSelectionDrawsNothing();
    void beaconSizeIsIndependentOfZoom();
    void separatedFeaturesEachGetABeacon();
    void collapsedSelectionAggregatesToOne();

private:
    static constexpr int kSide = 400;

    /**
     *
     * @brief Render the overlay into an owned transparent400-pixel-square image using an affine
     * scene transform.
     * @param ov Borrowed mutable overlay passed to paint; no ownership transfer.
     * @param scale Pixels per scene unit, multiplying coordinates; positive fixtures place
     * scene origin at image center and invert y. It is not scene units per pixel.
     * @return Owned premultiplied ARGB image after ending the local painter. The transform
     * callback captures only scale by value and runs synchronously; no screen/file I/O.
     *
     */
    static QImage render(SelectionBeaconOverlay &ov, double scale)
    {
        QImage img(QSize(kSide, kSide), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        ov.paint(
            p,
            [scale](const QPointF &s) {
                return QPointF(kSide / 2.0 + s.x() * scale, kSide / 2.0 - s.y() * scale);
            },
            QSize(kSide, kSide));
        p.end();
        return img;
    }

    /**
     *
     * @brief Count pixels whose alpha is nonzero, including antialiased boundary pixels.
     * @param img Borrowed image expected in a pixel()-compatible format.
     * @return Integer ink count; does not weight opacity, inspect colors or modify data.
     *
     */
    static int ink(const QImage &img)
    {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 0)
                    ++n;
        return n;
    }
};

/**
 *
 * @brief Require a default empty overlay to paint zero nontransparent pixels.
 * Checks isEmpty and a unit-scale synthetic render; no actual canvas is constructed.
 *
 */
void TestSelectionBeacon::emptySelectionDrawsNothing()
{
    SelectionBeaconOverlay ov;
    QVERIFY(ov.isEmpty());
    QCOMPARE(ink(render(ov, 1.0)), 0);
}

/**
 *
 * @brief Require a far-offscreen nonempty anchor to contribute no ink in the synthetic viewport.
 * Uses anchor100000/100000 at unit scale; does not test edge arrows or zoom-to-selection.
 *
 */
void TestSelectionBeacon::offScreenSelectionDrawsNothing()
{
    // Off-screen is served by "Zoom to Selection", not by the beacon.
    SelectionBeaconOverlay ov;
    ov.setAnchors({QPointF(100000.0, 100000.0)});
    QVERIFY(!ov.isEmpty());
    QCOMPARE(ink(render(ov, 1.0)), 0);
}

/**
 *
 * @brief Require a centered single beacon's ink to remain nearly constant at scales10/0.1/0.001.
 * Checks positive ink and adjacent differences at most12pixels for antialiasing jitter;
 * does not compare exact geometry, color or on-screen widget rendering.
 *
 */
void TestSelectionBeacon::beaconSizeIsIndependentOfZoom()
{
    SelectionBeaconOverlay ov;
    ov.setAnchors({QPointF(0.0, 0.0)});

    // Three views three orders of magnitude apart. A single anchor sits at
    // the centre in all of them, so any change in ink is the beacon scaling
    // with the view — exactly what must not happen.
    const int a = ink(render(ov, 10.0));
    const int b = ink(render(ov, 0.1));
    const int c = ink(render(ov, 0.001));
    QVERIFY2(a > 0, "a lone selected feature must always be marked");
    // Allow a little antialiasing jitter, but nothing scale-like.
    QVERIFY2(
        std::abs(a - b) <= 12 && std::abs(b - c) <= 12,
        qPrintable(
            QStringLiteral("beacon ink drifted with zoom: %1 / %2 / %3").arg(a).arg(b).arg(c)));
}

/**
 *
 * @brief Require anchors200screen pixels apart to paint more than1.5 times one beacon's ink.
 * The local lambda builds an independent single-anchor reference and returns its ink;
 * no exact two-to-one pixel ratio or connected-line rendering is asserted.
 *
 */
void TestSelectionBeacon::separatedFeaturesEachGetABeacon()
{
    SelectionBeaconOverlay ov;
    ov.setAnchors({QPointF(-50.0, 0.0), QPointF(50.0, 0.0)});

    const int one = [&] {
        SelectionBeaconOverlay solo;
        solo.setAnchors({QPointF(0.0, 0.0)});
        return ink(render(solo, 2.0));
    }();

    // 200 px apart — far wider than a beacon, so both are drawn.
    const int two = ink(render(ov, 2.0));
    QVERIFY2(
        two > one * 3 / 2,
        qPrintable(QStringLiteral("expected two beacons, ink %1 vs one at %2").arg(two).arg(one)));
}

/**
 *
 * @brief Require zooming a two-anchor selection from200-pixel to2-pixel separation to retain ink
 * but reduce it below75percent.
 * Pins aggregation by an image-space proxy; does not count exact rings or inspect shape.
 *
 */
void TestSelectionBeacon::collapsedSelectionAggregatesToOne()
{
    SelectionBeaconOverlay ov;
    ov.setAnchors({QPointF(-50.0, 0.0), QPointF(50.0, 0.0)});

    const int spread = ink(render(ov, 2.0));     // 200 px apart
    const int collapsed = ink(render(ov, 0.02)); //   2 px apart

    // Zoomed out the pair lands inside one beacon: draw a single ring rather
    // than stacking two on the same pixels.
    QVERIFY2(collapsed > 0, "the selection must still be marked when zoomed out");
    QVERIFY2(collapsed < spread * 3 / 4,
             qPrintable(QStringLiteral("expected aggregation: %1 collapsed vs %2 spread")
                            .arg(collapsed)
                            .arg(spread)));
}

QTEST_MAIN(TestSelectionBeacon)
#include "test_selectionbeacon.moc"
