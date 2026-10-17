/*!
 * \file   test_sizechannel.cpp
 * \brief  SizeChannel math + JSON round-trip, and its ownership by the
 *         point / line sublayer styles.
 */
#include "render/sizechannel.h"
#include "render/sublayers/feature/featuresublayerstyle.h"

#include <QSignalSpy>
#include <QtTest>

#include <cmath>

using OpenSWMM::Render::LineFeatureSublayerStyle;
using OpenSWMM::Render::PointFeatureSublayerStyle;
using OpenSWMM::Render::RangeMode;
using OpenSWMM::Render::SizeChannel;

class TestSizeChannel : public QObject
{
    Q_OBJECT
private slots:
    void linearWidth()
    {
        SizeChannel c;
        c.zeroBased = false;
        c.minPx = 2; c.maxPx = 12;
        QCOMPARE(c.pixels(0, 0, 10), 2.0);
        QCOMPARE(c.pixels(5, 0, 10), 7.0);
        QCOMPARE(c.pixels(10, 0, 10), 12.0);
        QCOMPARE(c.pixels(20, 0, 10), 12.0);   // clamped
        QCOMPARE(c.pixels(-5, 0, 10), 2.0);
    }
    void areaScaledNode()
    {
        SizeChannel c;
        c.zeroBased = false;
        c.areaScaled = true;
        c.minPx = 2; c.maxPx = 14;
        // Half way in value = half way in AREA, not diameter.
        QCOMPARE(c.pixels(5, 0, 10), std::sqrt(4.0 + 0.5 * (196.0 - 4.0)));
        QCOMPARE(c.pixels(10, 0, 10), 14.0);
    }
    void transforms()
    {
        SizeChannel c;
        c.zeroBased = false;
        c.transform = SizeChannel::Transform::Sqrt;
        QCOMPARE(c.position(25, 0, 100), 0.5);
        c.transform = SizeChannel::Transform::Log;
        QCOMPARE(c.position(std::exp(1.0) - 1.0, 0, std::exp(2.0) - 1.0), 0.5);
    }
    void zeroBasedRange()
    {
        SizeChannel c;
        c.minPx = 0; c.maxPx = 10;
        c.zeroBased = true;
        QCOMPARE(c.pixels(5, 4, 10), 5.0);     // range is [0, 10]
        c.zeroBased = false;
        QCOMPARE(c.pixels(5, 4, 10), 10.0 / 6.0);
        // Negative minima are not raised to zero.
        c.zeroBased = true;
        QCOMPARE(c.position(0, -10, 10), 0.5);
    }
    void nonFiniteAndDegenerate()
    {
        SizeChannel c;
        QVERIFY(std::isnan(c.pixels(std::numeric_limits<double>::quiet_NaN(), 0, 1)));
        QVERIFY(std::isnan(c.pixels(std::numeric_limits<double>::infinity(), 0, 1)));
        c.zeroBased = false;
        QCOMPARE(c.position(3, 3, 3), 0.5);
    }
    void jsonRoundTrip()
    {
        SizeChannel c;
        c.enabled = true;
        c.attribute = QStringLiteral("LinkFlow");
        c.transform = SizeChannel::Transform::Log;
        c.rangeMode = RangeMode::FixedUser;
        c.userMin = -1.5; c.userMax = 7.25;
        c.zeroBased = false;
        c.minPx = 1.5; c.maxPx = 9;
        const SizeChannel back = SizeChannel::fromJson(c.toJson(), false);
        QCOMPARE(back, c);
        QCOMPARE(SizeChannel::fromJson({}, true).areaScaled, true);
    }
    void styleOwnership()
    {
        PointFeatureSublayerStyle point;
        QVERIFY(point.sizeChannel().areaScaled);
        QSignalSpy spy(&point, &PointFeatureSublayerStyle::styleChanged);
        SizeChannel c = point.sizeChannel();
        c.enabled = true;
        c.attribute = QStringLiteral("NodeDepth");
        c.areaScaled = false;                 // archetype owns this flag
        point.setSizeChannel(c);
        QCOMPARE(spy.count(), 1);
        QVERIFY(point.sizeChannel().areaScaled);
        point.setSizeChannel(c);              // unchanged → no signal
        QCOMPARE(spy.count(), 1);

        PointFeatureSublayerStyle copy;
        copy.fromJson(point.toJson());
        QCOMPARE(copy.sizeChannel(), point.sizeChannel());

        LineFeatureSublayerStyle line;
        QVERIFY(!line.widthChannel().areaScaled);
        SizeChannel w;
        w.enabled = true;
        w.attribute = QStringLiteral("LinkFlow");
        line.setWidthChannel(w);
        LineFeatureSublayerStyle lineCopy;
        lineCopy.fromJson(line.toJson());
        QCOMPARE(lineCopy.widthChannel(), line.widthChannel());

        // A disabled channel still serializes, so a snapshot taken before the
        // user enabled it restores it to disabled on Cancel.
        LineFeatureSublayerStyle fresh;
        QVERIFY(fresh.toJson().contains(QStringLiteral("sizeChannel")));
        lineCopy.fromJson(fresh.toJson());
        QVERIFY(!lineCopy.widthChannel().enabled);
    }
};

QTEST_GUILESS_MAIN(TestSizeChannel)
#include "test_sizechannel.moc"
