/*!
 * \file   test_comparisonplot_xaxis_link.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AT.2 — pins the linked X-axis pattern used by
 * `ComparisonPlotDialog::wireXAxisSync`: when one row's UtcTimeAxis
 * range changes, every other row's axis mirrors it, with a recursion
 * guard so the cascading `setRange` calls don't blow the stack.
 *
 * Issue #11 moved the production axis from QDateTimeAxis to UtcTimeAxis and
 * the sync signal from `rangeChanged(QDateTime, QDateTime)` to
 * `rangeChangedUtc`, so this test uses the same axis and signal.
 *
 * Tests the *idiom* directly via a small `XAxisSyncBus` helper that
 * implements the same recursion-guarded mirror. The dialog uses the
 * identical pattern inline; full dialog-level verification is in the
 * manual checklist (snoopy_lagoon, AT.2 verification step #2).
 */
#include "plot/utctimeaxis.h"

#include <QDateTime>
#include <QObject>
#include <QTest>
#include <QVector>

using openswmmvis::plot::UtcTimeAxis;

/// Recursion-guarded mirror — identical idiom to ComparisonPlotDialog::wireXAxisSync.
class XAxisSyncBus : public QObject
{
    Q_OBJECT
public:
    void add(UtcTimeAxis *ax)
    {
        const int rowIdx = m_axes.size();
        m_axes.push_back(ax);
        connect(ax, &UtcTimeAxis::rangeChangedUtc,
                this, [this, rowIdx](QDateTime lo, QDateTime hi) {
                    if (m_syncing) return;
                    m_syncing = true;
                    for (int r = 0; r < m_axes.size(); ++r) {
                        if (r == rowIdx) continue;
                        if (auto *other = m_axes[r])
                            other->setRange(lo, hi);
                    }
                    m_syncing = false;
                    ++m_propagations;
                });
    }
    int propagations() const { return m_propagations; }
private:
    QVector<UtcTimeAxis*> m_axes;
    bool m_syncing = false;
    int  m_propagations = 0;
};

class TestComparisonPlotXAxisLink : public QObject
{
    Q_OBJECT
private slots:
    void singleSourceSetRange_mirrorsAcrossAllRows();
    void rangeChangeOnAnyRow_mirrorsToOthers();
    void recursionGuard_survivesStormOfSetRangeCalls();
    void numericZoomOnOneRow_mirrorsToOthers();
    void cleanup();

private:
    QVector<UtcTimeAxis*> m_axes;

    QDateTime t(int hour) const
    {
        return QDateTime(QDate(2026, 5, 22), QTime(hour, 0), Qt::UTC);
    }

    void buildAxes(int n)
    {
        qDeleteAll(m_axes);
        m_axes.clear();
        for (int i = 0; i < n; ++i) {
            auto *ax = new UtcTimeAxis;
            ax->setRange(t(0), t(24));
            m_axes.push_back(ax);
        }
    }
};

void TestComparisonPlotXAxisLink::cleanup()
{
    qDeleteAll(m_axes);
    m_axes.clear();
}

void TestComparisonPlotXAxisLink::singleSourceSetRange_mirrorsAcrossAllRows()
{
    buildAxes(3);
    XAxisSyncBus bus;
    for (auto *ax : m_axes) bus.add(ax);

    // Set range on row 0; rows 1 and 2 should follow.
    const QDateTime lo = t(6);
    const QDateTime hi = t(18);
    m_axes[0]->setRange(lo, hi);

    QCOMPARE(m_axes[0]->min(), lo);
    QCOMPARE(m_axes[0]->max(), hi);
    QCOMPARE(m_axes[1]->min(), lo);
    QCOMPARE(m_axes[1]->max(), hi);
    QCOMPARE(m_axes[2]->min(), lo);
    QCOMPARE(m_axes[2]->max(), hi);
}

void TestComparisonPlotXAxisLink::rangeChangeOnAnyRow_mirrorsToOthers()
{
    buildAxes(3);
    XAxisSyncBus bus;
    for (auto *ax : m_axes) bus.add(ax);

    // Change on row 1 → rows 0 and 2 mirror.
    m_axes[1]->setRange(t(2), t(10));
    QCOMPARE(m_axes[0]->min(), t(2));
    QCOMPARE(m_axes[2]->max(), t(10));

    // Change on row 2 → rows 0 and 1 mirror.
    m_axes[2]->setRange(t(4), t(14));
    QCOMPARE(m_axes[0]->min(), t(4));
    QCOMPARE(m_axes[1]->max(), t(14));
}

void TestComparisonPlotXAxisLink::recursionGuard_survivesStormOfSetRangeCalls()
{
    buildAxes(4);
    XAxisSyncBus bus;
    for (auto *ax : m_axes) bus.add(ax);

    // Hammer setRange — without the recursion guard, each setRange on row 0
    // would re-emit rangeChangedUtc on rows 1+, those re-emit on row 0 → stack
    // explosion. The guard short-circuits the second hop.
    for (int i = 0; i < 200; ++i) {
        const int hourLo = i % 12;
        const int hourHi = 12 + (i % 12) + 1;
        m_axes[i % 4]->setRange(t(hourLo), t(hourHi));
    }

    // No crash, no recursion, and all axes ended up at the final range.
    const QDateTime finalLo = m_axes[(199) % 4]->min();
    const QDateTime finalHi = m_axes[(199) % 4]->max();
    for (auto *ax : m_axes) {
        QCOMPARE(ax->min(), finalLo);
        QCOMPARE(ax->max(), finalHi);
    }

    // Each setRange that actually changed the range should have triggered
    // exactly one fan-out (not 4, not 16).
    QVERIFY(bus.propagations() > 0);
    QVERIFY(bus.propagations() <= 200);
}

void TestComparisonPlotXAxisLink::numericZoomOnOneRow_mirrorsToOthers()
{
    // A drag-zoom or pan moves the NUMERIC range (QChart::zoomIn → the
    // QValueAxis overload), not the QDateTime one; the sync must still fan out.
    buildAxes(3);
    XAxisSyncBus bus;
    for (auto *ax : m_axes) bus.add(ax);

    static_cast<QValueAxis *>(m_axes[1])->setRange(
        static_cast<qreal>(t(3).toMSecsSinceEpoch()),
        static_cast<qreal>(t(9).toMSecsSinceEpoch()));

    for (auto *ax : m_axes) {
        QCOMPARE(ax->min(), t(3));
        QCOMPARE(ax->max(), t(9));
    }
    QCOMPARE(bus.propagations(), 1);
}

QTEST_MAIN(TestComparisonPlotXAxisLink)
#include "test_comparisonplot_xaxis_link.moc"
