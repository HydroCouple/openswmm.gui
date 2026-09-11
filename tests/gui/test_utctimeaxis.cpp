/*!
 * \file   test_utctimeaxis.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Issue #11 — time-axis labels are UTC, whatever the process zone.
 *
 *         Runs under TZ=America/Denver (ctest ENVIRONMENT_MODIFICATION, and
 *         again in-process below) so the assertions are not vacuous on a UTC
 *         machine: the labels must be the same strings a UTC run produces.
 *         Also pins what makes UtcTimeAxis usable as a drop-in for
 *         QDateTimeAxis: both edges labelled, the numeric range stays true
 *         epoch milliseconds, a chart zoom relabels and re-emits the range as
 *         instants, duplicate labels collapse instead of colliding.
 */

#include "plot/utctimeaxis.h"

#include <QChart>
#include <QChartView>
#include <QDateTime>
#include <QDebug>
#include <QLineSeries>
#include <QObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QValueAxis>

#include <cmath>
#include <ctime>

using openswmmvis::plot::UtcTimeAxis;

namespace {

QDateTime utc(int y, int mo, int d, int h, int mi = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi), Qt::UTC);
}

const QString kFmt = QStringLiteral("yyyy-MM-dd HH:mm");

}  // namespace

class TestUtcTimeAxis : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Belt and braces with the ctest TZ: a non-UTC, DST-observing zone
        // in-process too (Qt honours $TZ on Unix; the product bug is a Windows
        // report, where it uses system APIs — hence the class, not this).
        qputenv("TZ", "America/Denver");
#ifndef Q_OS_WIN
        tzset();
#endif
        const QDateTime probe = utc(2026, 1, 1, 12);
        qInfo() << "process zone renders 2026-01-01 12:00Z as"
                << probe.toLocalTime().toString(kFmt)
                << "(offset s =" << probe.toLocalTime().offsetFromUtc() << ")";
    }

    void labelsAreUtcAtBothEdges()
    {
        UtcTimeAxis ax;
        ax.setFormat(kFmt);
        ax.setTickCount(5);
        ax.setRange(utc(2026, 1, 1, 0), utc(2026, 1, 2, 0));

        const QStringList expected{
            QStringLiteral("2026-01-01 00:00"), QStringLiteral("2026-01-01 06:00"),
            QStringLiteral("2026-01-01 12:00"), QStringLiteral("2026-01-01 18:00"),
            QStringLiteral("2026-01-02 00:00")};
        QCOMPARE(ax.categoriesLabels(), expected);

        // The label sits AT the tick instant (category end == epoch ms).
        QCOMPARE(ax.endValue(QStringLiteral("2026-01-01 06:00")),
                 static_cast<qreal>(utc(2026, 1, 1, 6).toMSecsSinceEpoch()));

        // Instants read back as UTC and the numeric range is true epoch ms.
        QCOMPARE(ax.min(), utc(2026, 1, 1, 0));
        QCOMPARE(ax.max(), utc(2026, 1, 2, 0));
        QCOMPARE(ax.min().timeSpec(), Qt::UTC);
        QCOMPARE(static_cast<QValueAxis &>(ax).min(),
                 static_cast<qreal>(utc(2026, 1, 1, 0).toMSecsSinceEpoch()));
        QCOMPARE(static_cast<QValueAxis &>(ax).max(),
                 static_cast<qreal>(utc(2026, 1, 2, 0).toMSecsSinceEpoch()));
    }

    void labelsIgnoreProcessTimeZoneAcrossDst()
    {
        // 2026-03-08 is the US spring-forward day. A local-time axis in
        // Denver would print 2026-03-07 17:00 / 2026-03-08 06:00 / 18:00 here
        // and squeeze the missing hour; the UTC labels advance uniformly.
        UtcTimeAxis ax;
        ax.setFormat(kFmt);
        ax.setTickCount(3);
        ax.setRange(utc(2026, 3, 8, 0), utc(2026, 3, 9, 0));
        const QStringList expected{
            QStringLiteral("2026-03-08 00:00"), QStringLiteral("2026-03-08 12:00"),
            QStringLiteral("2026-03-09 00:00")};
        QCOMPARE(ax.categoriesLabels(), expected);

        // The same instant rendered the QDateTimeAxis way (local) differs
        // whenever the process zone is not UTC — which it is not here.
        const QString local = QDateTime::fromMSecsSinceEpoch(
                                  utc(2026, 3, 8, 12).toMSecsSinceEpoch())
                                  .toString(kFmt);
        if (utc(2026, 3, 8, 12).toLocalTime().offsetFromUtc() != 0)
            QVERIFY2(local != QStringLiteral("2026-03-08 12:00"),
                     "process zone is UTC; the non-UTC assertion is vacuous");
    }

    void numericRangeChangeRelabelsAndEmitsUtcRange()
    {
        // What QChart::zoomIn / scroll / zoomReset do under the hood: they
        // move the QValueAxis range, never the QDateTime one.
        UtcTimeAxis ax;
        ax.setFormat(kFmt);
        ax.setTickCount(5);
        ax.setRange(utc(2026, 1, 1, 0), utc(2026, 1, 2, 0));
        QSignalSpy spy(&ax, &UtcTimeAxis::rangeChangedUtc);

        static_cast<QValueAxis &>(ax).setRange(
            static_cast<qreal>(utc(2026, 1, 1, 6).toMSecsSinceEpoch()),
            static_cast<qreal>(utc(2026, 1, 1, 18).toMSecsSinceEpoch()));

        QCOMPARE(ax.min(), utc(2026, 1, 1, 6));
        QCOMPARE(ax.max(), utc(2026, 1, 1, 18));
        const QStringList expected{
            QStringLiteral("2026-01-01 06:00"), QStringLiteral("2026-01-01 09:00"),
            QStringLiteral("2026-01-01 12:00"), QStringLiteral("2026-01-01 15:00"),
            QStringLiteral("2026-01-01 18:00")};
        QCOMPARE(ax.categoriesLabels(), expected);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toDateTime(), utc(2026, 1, 1, 6));
        QCOMPARE(spy.first().at(1).toDateTime(), utc(2026, 1, 1, 18));

        // No-op guard: the same range again is silent (panel sync connects
        // both ways).
        ax.setRange(utc(2026, 1, 1, 6), utc(2026, 1, 1, 18));
        QCOMPARE(spy.count(), 1);
    }

    void chartRubberBandZoomRelabels()
    {
        // The real path: a laid-out chart zoomed to the middle half.
        auto *chart = new QChart;   // owned, and deleted, by the view below
        auto *series = new QLineSeries;
        const QDateTime t0 = utc(2026, 1, 1, 0), t24 = utc(2026, 1, 2, 0);
        series->append(static_cast<qreal>(t0.toMSecsSinceEpoch()), 0.0);
        series->append(static_cast<qreal>(t24.toMSecsSinceEpoch()), 1.0);
        chart->addSeries(series);
        auto *ax = new UtcTimeAxis(chart);
        ax->setFormat(kFmt);
        ax->setTickCount(3);
        auto *ay = new QValueAxis(chart);
        ay->setRange(0.0, 1.0);
        chart->addAxis(ax, Qt::AlignBottom);
        chart->addAxis(ay, Qt::AlignLeft);
        series->attachAxis(ax);
        series->attachAxis(ay);
        ax->setRange(t0, t24);

        QChartView view(chart);
        view.resize(800, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(50);

        const QRectF plot = chart->plotArea();
        QVERIFY(plot.width() > 100.0);
        chart->zoomIn(QRectF(plot.left() + plot.width() * 0.25, plot.top(),
                            plot.width() * 0.5, plot.height()));
        QTest::qWait(20);

        // 25 %..75 % of a day, to pixel precision.
        const double tolMs = 2.0 * 60.0 * 1000.0;
        QVERIFY2(std::abs(double(ax->min().toMSecsSinceEpoch()
                                 - utc(2026, 1, 1, 6).toMSecsSinceEpoch())) < tolMs,
                 qPrintable(ax->min().toString(Qt::ISODate)));
        QVERIFY2(std::abs(double(ax->max().toMSecsSinceEpoch()
                                 - utc(2026, 1, 1, 18).toMSecsSinceEpoch())) < tolMs,
                 qPrintable(ax->max().toString(Qt::ISODate)));
        QCOMPARE(ax->categoriesLabels().size(), 3);
        QVERIFY(ax->categoriesLabels().first().startsWith(QStringLiteral("2026-01-01 0")));

        chart->zoomReset();
        QTest::qWait(20);
        QCOMPARE(ax->min(), t0);
        QCOMPARE(ax->max(), t24);
        QCOMPARE(ax->categoriesLabels(),
                 (QStringList{QStringLiteral("2026-01-01 00:00"),
                              QStringLiteral("2026-01-01 12:00"),
                              QStringLiteral("2026-01-02 00:00")}));
    }

    void setMinSetMaxMoveOneEdge()
    {
        UtcTimeAxis ax;
        ax.setFormat(kFmt);
        ax.setTickCount(3);
        ax.setRange(utc(2026, 1, 1, 0), utc(2026, 1, 2, 0));

        ax.setMin(utc(2026, 1, 1, 12));
        QCOMPARE(ax.min(), utc(2026, 1, 1, 12));
        QCOMPARE(ax.max(), utc(2026, 1, 2, 0));
        QCOMPARE(ax.categoriesLabels().first(), QStringLiteral("2026-01-01 12:00"));

        ax.setMax(utc(2026, 1, 1, 18));
        QCOMPARE(ax.min(), utc(2026, 1, 1, 12));
        QCOMPARE(ax.max(), utc(2026, 1, 1, 18));
        QCOMPARE(ax.categoriesLabels().last(), QStringLiteral("2026-01-01 18:00"));
    }

    void duplicateLabelsCollapse()
    {
        // A day-only format over one day: four ticks round to the same text.
        UtcTimeAxis ax;
        ax.setFormat(QStringLiteral("yyyy-MM-dd"));
        ax.setTickCount(5);
        ax.setRange(utc(2026, 1, 1, 0), utc(2026, 1, 2, 0));
        QCOMPARE(ax.categoriesLabels(),
                 (QStringList{QStringLiteral("2026-01-01"), QStringLiteral("2026-01-02")}));
        // Widening the format restores the density.
        ax.setFormat(kFmt);
        QCOMPARE(ax.categoriesLabels().size(), 5);
    }
};

QTEST_MAIN(TestUtcTimeAxis)
#include "test_utctimeaxis.moc"
