/**
 * @file utctimeaxis.h
 * @brief A chart time axis whose tick labels are rendered in UTC.
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
 */

#ifndef OPENSWMMVIS_PLOT_UTCTIMEAXIS_H
#define OPENSWMMVIS_PLOT_UTCTIMEAXIS_H

#include <QCategoryAxis>
#include <QDateTime>
#include <QString>

namespace openswmmvis::plot {

class UtcTimeAxis : public QCategoryAxis
{
    Q_OBJECT

public:
    explicit UtcTimeAxis(QObject *parent = nullptr);
    ~UtcTimeAxis() override;

    /// QDateTime format string for the tick labels, as QDateTimeAxis::setFormat.
    void setFormat(const QString &format);
    QString format() const { return m_format; }

    /// Number of tick labels to place across the range (>= 2), both edges
    /// included, as QDateTimeAxis::setTickCount.
    void setTickCount(int count);
    int tickCount() const { return m_tickCount; }

    /// Range as instants. Both are read as absolute times; their timeSpec is
    /// irrelevant because only the epoch value is used.
    void setRange(const QDateTime &min, const QDateTime &max);
    void setRangeMs(qint64 minMs, qint64 maxMs);
    /// Move one edge, as QDateTimeAxis::setMin / setMax.
    void setMin(const QDateTime &min);
    void setMax(const QDateTime &max);

    /// Range edges as UTC instants.
    QDateTime min() const;
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
    void rangeChangedUtc(const QDateTime &min, const QDateTime &max);

private:
    void rebuildLabels();
    /// The numeric range moved underneath us (chart zoom / pan / zoomReset,
    /// or a caller using the qreal overloads): adopt it.
    void onValueRangeChanged(qreal min, qreal max);

    QString m_format = QStringLiteral("MM/dd/yyyy HH:mm");
    int     m_tickCount = 5;
    qint64  m_minMs = 0;
    qint64  m_maxMs = 0;
    bool    m_rebuilding = false;   ///< rebuildLabels() sets the numeric range itself
};

}  // namespace openswmmvis::plot

#endif  // OPENSWMMVIS_PLOT_UTCTIMEAXIS_H
