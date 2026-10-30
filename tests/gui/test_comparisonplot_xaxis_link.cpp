/**
 * @file      test_comparisonplot_xaxis_link.cpp
 * @brief     Tests for comparisonplot xaxis link.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /**
     *
     * @brief Append a borrowed time axis and connect recursion-guarded synchronous range
     * mirroring.
     * @param ax Nonnull GUI-thread axis, unique in the normal fixture. All stored axes
     * must remain alive through every later signal fan-out; raw pointers are not guarded.
     * The connection receives lower/upper QDateTime values and applies them to every
     * other nonnull entry, suppressing nested fan-outs before incrementing the counter.
     * Does not reparent/delete axes or synchronize their initial ranges; no thread locks.
     *
     */
    void add(UtcTimeAxis *ax)
    {
        const int rowIdx = m_axes.size();
        m_axes.push_back(ax);
        connect(ax, &UtcTimeAxis::rangeChangedUtc, this,
                [this, rowIdx](QDateTime lo, QDateTime hi) {
                    if (m_syncing)
                        return;
                    m_syncing = true;
                    for (int r = 0; r < m_axes.size(); ++r) {
                        if (r == rowIdx)
                            continue;
                        if (auto *other = m_axes[r])
                            other->setRange(lo, hi);
                    }
                    m_syncing = false;
                    ++m_propagations;
                });
    }
    /**
     *
     * @brief Read completed outer range-change fan-outs.
     * @return Counter, initially zero; nested mirrored signals do not increment it.
     * Caller must serialize access and keep the small fixture count within int range.
     *
     */
    int propagations() const { return m_propagations; }

private:
    QVector<UtcTimeAxis *> m_axes;
    bool m_syncing = false;
    int m_propagations = 0;
};

class TestComparisonPlotXAxisLink : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require a first-row UTC six-to-eighteen range to appear exactly on all three axes.
     *
     */
    void singleSourceSetRange_mirrorsAcrossAllRows();
    /**
     *
     * @brief Require selected endpoints to mirror after edits originating on rows one and two.
     *
     */
    void rangeChangeOnAnyRow_mirrorsToOthers();
    /**
     *
     * @brief Require 200 rotating range requests to finish with equal axis ranges and one-to-200
     * outer fan-outs.
     * Some helper hours can be 24 and invalid, so this does not assert every request
     * was accepted or exactly 200 changes propagated.
     *
     */
    void recursionGuard_survivesStormOfSetRangeCalls();
    /**
     *
     * @brief Require a numeric QValueAxis millisecond range edit to mirror UTC endpoints across
     * rows with exactly one outer propagation.
     *
     */
    void numericZoomOnOneRow_mirrorsToOthers();
    /**
     *
     * @brief Delete all owned test axes and clear the per-case pointer vector after local buses
     * are destroyed.
     *
     */
    void cleanup();

private:
    QVector<UtcTimeAxis *> m_axes;

    /**
     *
     * @brief Build one UTC test instant on 22 May 2026.
     * @param hour QTime hour, normally 0..23; 24/out-of-range yields an invalid time
     * instead of rolling to the next day.
     * @return By-value UTC QDateTime with minute/second zero or invalid for invalid hour.
     *
     */
    QDateTime t(int hour) const { return QDateTime(QDate(2026, 5, 22), QTime(hour, 0), Qt::UTC); }

    /**
     *
     * @brief Delete existing fixture axes and allocate n unparented axes owned by this fixture.
     * @param n Small nonnegative axis count; nonpositive leaves the vector empty.
     * Attempts initial range t(0)..t(24), whose upper date is invalid; Qt axis rejection
     * behavior is not checked here. New allocations may leave a partial vector on
     * exception. GUI thread only; old axes must have no remaining raw-pointer consumers.
     *
     */
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
    for (auto *ax : m_axes)
        bus.add(ax);

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
    for (auto *ax : m_axes)
        bus.add(ax);

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
    for (auto *ax : m_axes)
        bus.add(ax);

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
    for (auto *ax : m_axes)
        bus.add(ax);

    static_cast<QValueAxis *>(m_axes[1])->setRange(static_cast<qreal>(t(3).toMSecsSinceEpoch()),
                                                   static_cast<qreal>(t(9).toMSecsSinceEpoch()));

    for (auto *ax : m_axes) {
        QCOMPARE(ax->min(), t(3));
        QCOMPARE(ax->max(), t(9));
    }
    QCOMPARE(bus.propagations(), 1);
}

QTEST_MAIN(TestComparisonPlotXAxisLink)
#include "test_comparisonplot_xaxis_link.moc"
