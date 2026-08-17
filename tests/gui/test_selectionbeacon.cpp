/*!
 * \file   test_selectionbeacon.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for the selection locator overlay.
 *
 *         The contract that matters is SCREEN-space: the beacon must stay the
 *         same size however far the view is zoomed out, because the whole
 *         reason it exists is that a selected feature's own halo goes
 *         sub-pixel there. The zoom-dependent aggregation is the subtle part
 *         — folding point-rects with QRectF::united() silently yields an
 *         empty span (a zero-size rect is null), which collapsed every
 *         selection to one beacon at every zoom.
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

    /*! Renders the overlay with `scale` scene-units-per-pixel. */
    static QImage render(SelectionBeaconOverlay &ov, double scale)
    {
        QImage img(QSize(kSide, kSide), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        ov.paint(p, [scale](const QPointF &s) {
            return QPointF(kSide / 2.0 + s.x() * scale,
                           kSide / 2.0 - s.y() * scale);
        }, QSize(kSide, kSide));
        p.end();
        return img;
    }

    static int ink(const QImage &img)
    {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 0) ++n;
        return n;
    }
};

void TestSelectionBeacon::emptySelectionDrawsNothing()
{
    SelectionBeaconOverlay ov;
    QVERIFY(ov.isEmpty());
    QCOMPARE(ink(render(ov, 1.0)), 0);
}

void TestSelectionBeacon::offScreenSelectionDrawsNothing()
{
    // Off-screen is served by "Zoom to Selection", not by the beacon.
    SelectionBeaconOverlay ov;
    ov.setAnchors({ QPointF(100000.0, 100000.0) });
    QVERIFY(!ov.isEmpty());
    QCOMPARE(ink(render(ov, 1.0)), 0);
}

void TestSelectionBeacon::beaconSizeIsIndependentOfZoom()
{
    SelectionBeaconOverlay ov;
    ov.setAnchors({ QPointF(0.0, 0.0) });

    // Three views three orders of magnitude apart. A single anchor sits at
    // the centre in all of them, so any change in ink is the beacon scaling
    // with the view — exactly what must not happen.
    const int a = ink(render(ov, 10.0));
    const int b = ink(render(ov, 0.1));
    const int c = ink(render(ov, 0.001));
    QVERIFY2(a > 0, "a lone selected feature must always be marked");
    // Allow a little antialiasing jitter, but nothing scale-like.
    QVERIFY2(std::abs(a - b) <= 12 && std::abs(b - c) <= 12,
             qPrintable(QStringLiteral("beacon ink drifted with zoom: %1 / %2 / %3")
                            .arg(a).arg(b).arg(c)));
}

void TestSelectionBeacon::separatedFeaturesEachGetABeacon()
{
    SelectionBeaconOverlay ov;
    ov.setAnchors({ QPointF(-50.0, 0.0), QPointF(50.0, 0.0) });

    const int one = [&] {
        SelectionBeaconOverlay solo;
        solo.setAnchors({ QPointF(0.0, 0.0) });
        return ink(render(solo, 2.0));
    }();

    // 200 px apart — far wider than a beacon, so both are drawn.
    const int two = ink(render(ov, 2.0));
    QVERIFY2(two > one * 3 / 2,
             qPrintable(QStringLiteral("expected two beacons, ink %1 vs one at %2")
                            .arg(two).arg(one)));
}

void TestSelectionBeacon::collapsedSelectionAggregatesToOne()
{
    SelectionBeaconOverlay ov;
    ov.setAnchors({ QPointF(-50.0, 0.0), QPointF(50.0, 0.0) });

    const int spread    = ink(render(ov, 2.0));     // 200 px apart
    const int collapsed = ink(render(ov, 0.02));    //   2 px apart

    // Zoomed out the pair lands inside one beacon: draw a single ring rather
    // than stacking two on the same pixels.
    QVERIFY2(collapsed > 0, "the selection must still be marked when zoomed out");
    QVERIFY2(collapsed < spread * 3 / 4,
             qPrintable(QStringLiteral("expected aggregation: %1 collapsed vs %2 spread")
                            .arg(collapsed).arg(spread)));
}

QTEST_MAIN(TestSelectionBeacon)
#include "test_selectionbeacon.moc"
