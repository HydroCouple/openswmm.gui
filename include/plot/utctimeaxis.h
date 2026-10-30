/**
 * @file      utctimeaxis.h
 * @brief     A chart time axis whose tick labels are rendered in UTC.
 *
 * @details Model times are UTC end to end: `.inp` dates and times carry no
 *          zone, so `swmmDateTimeToQDateTime()` tags them `Qt::UTC` and the
 *          viewer must not shift them. Rendering them in the viewer's local
 *          zone does not reveal information, it invents it — the same model
 *          would plot differently in Denver and Berlin (issue #11).
 *
 *          Qt Charts' QDateTimeAxis cannot do this. It exposes only `format`,
 *          `min`, `max` and `tickCount`, and builds each label through
 *          `QDateTime::fromMSecsSinceEpoch(ms)` — i.e. LOCAL time — with no
 *          timezone property to override. (The UTC-defaulting `timeZone`
 *          property is Qt GRAPHS 6.11+, a different module this project does
 *          not use.) Forcing the process timezone is not an option either: Qt
 *          honours $TZ on Unix but uses system APIs on Windows.
 *
 *          So the labels are generated here instead. X values remain true UTC
 *          milliseconds-since-epoch — unchanged, uncorrected, directly
 *          comparable with the table — and only the label TEXT is formatted,
 *          with `Qt::UTC`. Shifting the x values to make a local-rendering
 *          axis show UTC was the alternative; it puts the axis in a fake
 *          coordinate space every read-back has to invert, and it breaks
 *          across a DST boundary, which any multi-month series will cross.
 *
 *          The public surface deliberately mirrors QDateTimeAxis so existing
 *          call sites (setRange with QDateTimes, min()/max(), setMin/setMax,
 *          setFormat, setTickCount) need no changes.
 *
 *          Chart zoom, pan and zoomReset move the NUMERIC range directly
 *          (`QChart::zoomIn` → `QValueAxis::setRange`), never through the
 *          QDateTime setters, so the axis listens to its own
 *          `QValueAxis::rangeChanged`, relabels for the new window and
 *          re-emits the range as instants (`rangeChangedUtc`). A drag-zoom
 *          therefore reaches panel-sync code exactly like a programmatic
 *          `setRange`.
 *
 * @warning QCategoryAxis derives from QValueAxis, so a
 *          `qobject_cast<QValueAxis*>` matches this type where it would NOT
 *          match QDateTimeAxis. Code that treats "is a QValueAxis" as "takes a
 *          printf label spec" or "edits as a number" (ChartProperties,
 *          ChartAxisFormatController, InteractiveChartView) must test for
 *          this class first.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_PLOT_UTCTIMEAXIS_H
#define OPENSWMMVIS_PLOT_UTCTIMEAXIS_H

#include <QCategoryAxis>
#include <QDateTime>
#include <QString>

namespace openswmmvis::plot
{

class UtcTimeAxis : public QCategoryAxis
{
    Q_OBJECT

public:
    /**
     * @brief Configure category labels on their UTC epoch-millisecond tick positions.
     * @param[in] parent Optional QObject owner until Qt chart ownership is assigned. Initial stored range is zero/zero and no labels are built yet.
     */
    explicit UtcTimeAxis(QObject *parent = nullptr);
    /**
     * @brief Release axis state and QObject resources through Qt's normal ownership rules.
     */
    ~UtcTimeAxis() override;

    /// QDateTime format string for the tick labels, as QDateTimeAxis::setFormat.
    /**
     * @param[in] format QDateTime pattern stored verbatim; an identical string is a no-op.
     * @note Rebuilds labels immediately without changing the UTC range or emitting rangeChangedUtc(). Duplicate formatted labels collapse.
     */
    void setFormat(const QString &format);
    /**
     * @brief Inspect the UTC tick-label pattern.
     * @return Stored QDateTime pattern, initially MM/dd/yyyy HH:mm.
     */
    QString format() const { return m_format; }

    /// Number of tick labels to place across the range (>= 2), both edges
    /// included, as QDateTimeAxis::setTickCount.
    /**
     * @param[in] count Requested label count, clamped to at least two with no upper limit; unchanged effective count is a no-op.
     * @note Rebuilds labels without changing the UTC range. Duplicate label strings can reduce the actual label count.
     */
    void setTickCount(int count);
    /**
     * @brief Inspect the requested label count rather than the deduplicated categories.
     * @return Stored count, initially five and always at least two through this API.
     */
    int tickCount() const { return m_tickCount; }

    /// Choose a tick count that fits labels of the current format across
    /// \p pixelWidth without overlapping (clamped to [2, 8]). Qt Charts hides
    /// overlapping or clipped labels, so a fixed count on a narrow chart left
    /// the time axis with no readable labels at all.
    /**
     * @param[in] pixelWidth Available logical-pixel width; nonpositive widths choose two ticks.
     * @note Estimates seven pixels per format character plus spacing, then calls setTickCount() with [2,8]. This does not measure the actual current font.
     */
    void fitTickCountToWidth(int pixelWidth);

    /// Range as instants. Both are read as absolute times; their timeSpec is
    /// irrelevant because only the epoch value is used.
    /**
     * @param[in] min First valid absolute instant; invalid input makes the entire call a no-op.
     * @param[in] max Second valid absolute instant; reversed endpoints are sorted by setRangeMs().
     * @note Time-zone representations are reduced to epoch milliseconds; no wall-clock reinterpretation occurs.
     */
    void setRange(const QDateTime &min, const QDateTime &max);
    /**
     * @brief Set a UTC range in epoch milliseconds and rebuild categories.
     * @param[in] minMs First endpoint, sorted with maxMs if reversed.
     * @param[in] maxMs Second endpoint; expected arithmetic-safe for span/label generation.
     * @note Equal stored endpoints are a no-op. A changed range emits rangeChangedUtc() after rebuilding; equal endpoints retain an empty label list with an internal numeric axis span of one millisecond.
     */
    void setRangeMs(qint64 minMs, qint64 maxMs);
    /// Move one edge, as QDateTimeAxis::setMin / setMax.
    /**
     * @param[in] min New absolute endpoint; invalid is ignored. Calls setRangeMs() with current maximum, so crossing it sorts/swaps the endpoints.
     */
    void setMin(const QDateTime &min);
    /**
     * @param[in] max New absolute endpoint; invalid is ignored. Calls setRangeMs() with current minimum, sorting/swapping crossed endpoints.
     */
    void setMax(const QDateTime &max);

    /// Range edges as UTC instants.
    /**
     * @return Stored lower endpoint represented in Qt::UTC, independent of the viewer's local zone.
     */
    QDateTime min() const;
    /**
     * @return Stored upper endpoint represented in Qt::UTC; may equal min() even when the numeric axis uses a one-millisecond fallback span.
     */
    QDateTime max() const;

    /// Keep the inherited numeric overloads reachable — declaring the
    /// QDateTime overloads above would otherwise hide them by name.
    using QValueAxis::setRange;
    using QValueAxis::setMin;
    using QValueAxis::setMax;

signals:
    /// Range changed, as instants. The QDateTimeAxis equivalent is
    /// `rangeChanged(QDateTime, QDateTime)`; the inherited QValueAxis signal of
    /// that name carries `(qreal, qreal)` instead, so panel-sync code connects
    /// to THIS one. Named distinctly rather than overloaded so a connect() to
    /// the wrong overload is a compile error rather than a silent mismatch.
    /// Also emitted after a chart zoom / pan (see the class notes).
    /**
     * @param[in] min Current stored lower endpoint as a UTC instant.
     * @param[in] max Current stored upper endpoint as a UTC instant; equal endpoints are permitted.
     * @note Emitted after changed programmatic or inherited numeric-range adoption; format/tick-count-only changes do not emit it.
     */
    void rangeChangedUtc(const QDateTime &min, const QDateTime &max);

private:
    /**
     * @brief Replace category labels with deduplicated UTC-formatted ticks across the stored range.
     * @note Changes numeric axis range while guarding against recursive adoption. Empty/equal ranges have no labels; ticks include both boundaries before duplicate-text removal.
     */
    void rebuildLabels();
    /// The numeric range moved underneath us (chart zoom / pan / zoomReset,
    /// or a caller using the qreal overloads): adopt it.
    /**
     * @param[in] min Numeric lower epoch-millisecond endpoint, rounded to qint64.
     * @param[in] max Numeric upper endpoint, likewise rounded; caller must supply finite representable values.
     * @note Ignores range changes generated by rebuildLabels(); otherwise adopts through setRangeMs(), possibly rebuilding and emitting the UTC signal.
     */
    void onValueRangeChanged(qreal min, qreal max);

    QString m_format = QStringLiteral("MM/dd/yyyy HH:mm");
    int m_tickCount = 5;
    qint64 m_minMs = 0;
    qint64 m_maxMs = 0;
    bool m_rebuilding = false; ///< rebuildLabels() sets the numeric range itself
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_UTCTIMEAXIS_H
