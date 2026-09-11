/**
 * @file utctimeaxis.cpp
 * @brief Implementation of UtcTimeAxis — see the header for why this exists.
 */

#include "plot/utctimeaxis.h"

#include <algorithm>

namespace openswmmvis::plot {

UtcTimeAxis::UtcTimeAxis(QObject *parent)
    : QCategoryAxis(parent)
{
    // Labels sit ON their tick value rather than centred in the band between
    // two categories, which is what makes a category axis read as a time axis.
    // Matches the existing use in patterneditordialog.cpp.
    setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
    // The category "start" is the left edge of the range; without it the first
    // label is placed relative to 0 (1970) and every tick lands off-screen.
    setStartValue(0.0);
    // QChart::zoomIn / scroll / zoomReset and the qreal setRange overloads
    // move the numeric range without touching m_minMs/m_maxMs: follow them.
    connect(this, &QValueAxis::rangeChanged, this, &UtcTimeAxis::onValueRangeChanged);
}

UtcTimeAxis::~UtcTimeAxis() = default;

void UtcTimeAxis::setFormat(const QString &format)
{
    if (m_format == format)
        return;
    m_format = format;
    rebuildLabels();
}

void UtcTimeAxis::setTickCount(int count)
{
    const int n = std::max(2, count);
    if (m_tickCount == n)
        return;
    m_tickCount = n;
    rebuildLabels();
}

void UtcTimeAxis::setRange(const QDateTime &min, const QDateTime &max)
{
    if (!min.isValid() || !max.isValid())
        return;
    setRangeMs(min.toMSecsSinceEpoch(), max.toMSecsSinceEpoch());
}

void UtcTimeAxis::setMin(const QDateTime &min)
{
    if (!min.isValid())
        return;
    setRangeMs(min.toMSecsSinceEpoch(), m_maxMs);
}

void UtcTimeAxis::setMax(const QDateTime &max)
{
    if (!max.isValid())
        return;
    setRangeMs(m_minMs, max.toMSecsSinceEpoch());
}

void UtcTimeAxis::setRangeMs(qint64 minMs, qint64 maxMs)
{
    if (maxMs < minMs)
        std::swap(minMs, maxMs);
    if (m_minMs == minMs && m_maxMs == maxMs)
        return;                       // no-op guard: panel sync connects both ways
    m_minMs = minMs;
    m_maxMs = maxMs;
    rebuildLabels();
    emit rangeChangedUtc(min(), max());
}

void UtcTimeAxis::onValueRangeChanged(qreal min, qreal max)
{
    if (m_rebuilding)
        return;                       // our own QValueAxis::setRange in rebuildLabels()
    setRangeMs(qRound64(min), qRound64(max));
}

QDateTime UtcTimeAxis::min() const
{
    return QDateTime::fromMSecsSinceEpoch(m_minMs, Qt::UTC);
}

QDateTime UtcTimeAxis::max() const
{
    return QDateTime::fromMSecsSinceEpoch(m_maxMs, Qt::UTC);
}

void UtcTimeAxis::rebuildLabels()
{
    // QCategoryAxis has no clear(); remove by name, as patterneditordialog does.
    const QStringList existing = categoriesLabels();
    for (const QString &label : existing)
        remove(label);

    m_rebuilding = true;
    if (m_maxMs <= m_minMs) {
        // Degenerate range (a single point, or nothing plotted yet). Leave the
        // axis label-less rather than emitting one tick at an arbitrary value.
        QValueAxis::setRange(static_cast<qreal>(m_minMs),
                             static_cast<qreal>(m_minMs + 1));
        setStartValue(static_cast<qreal>(m_minMs));
        m_rebuilding = false;
        return;
    }

    // A category is the interval (previous end, end] and is labelled at its
    // end, so the range minimum can only carry a label if a category ENDS
    // there: start the first category 1 ms before the range (off-screen).
    setStartValue(static_cast<qreal>(m_minMs - 1));
    QValueAxis::setRange(static_cast<qreal>(m_minMs), static_cast<qreal>(m_maxMs));
    m_rebuilding = false;

    const qint64 span = m_maxMs - m_minMs;
    const int    n    = m_tickCount;
    for (int i = 0; i <= n - 1; ++i) {
        // Tick i sits at min + i/(n-1) of the span: both edges labelled, as
        // QDateTimeAxis does.
        const qint64 ms = m_minMs + (span * i) / (n - 1);
        // Qt::UTC is the entire point of this class.
        const QString label =
            QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC).toString(m_format);
        // Duplicate labels would collide in QCategoryAxis' name-keyed map — a
        // short format over a short span can round two ticks to the same text.
        if (categoriesLabels().contains(label))
            continue;
        append(label, static_cast<qreal>(ms));
    }
}

}  // namespace openswmmvis::plot
