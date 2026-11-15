// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      chartaccessible.cpp
 * @brief     Screen-reader interface for every QChartView in the app (review A-01).
 * @details   Qt Charts draws on a graphics scene and gives assistive technology nothing to read.
 *            This interface names the chart (the host's accessible name, else the chart title)
 *            and describes it from the live QChart: each visible series with its point count and
 *            value range, then each axis with its title and visible range, then whatever the
 *            view's own accessible description adds (InteractiveChartView: its keys). Reading
 *            single values stays with the table beside each editor's chart. Registered for the
 *            QChartView class level, so subclasses (InteractiveChartView, the editor chart
 *            views, TransectChartView) get it too. GUI thread only.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/accessibility/customaccessible.h"

#include <QAbstractAxis>
#include <QAbstractSeries>
#include <QAreaSeries>
#include <QChart>
#include <QChartView>
#include <QCoreApplication>
#include <QDateTime>
#include <QDateTimeAxis>
#include <QLineSeries>
#include <QLocale>
#include <QLogValueAxis>
#include <QStringList>
#include <QTimeZone>
#include <QValueAxis>
#include <QXYSeries>

#include <cmath>
#include <limits>

namespace openswmmvis::ui::accessibility
{

namespace
{

QString tr(const char *text)
{
    return QCoreApplication::translate("ChartAccessible", text);
}

QString number(double v)
{
    return QLocale().toString(v, 'g', 6);
}

QString dateTime(const QDateTime &t)
{
    return t.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

/** @return The axis's visible range as text, or empty for category-only axes. */
QString axisRange(const QAbstractAxis *axis)
{
    if (const auto *time = qobject_cast<const QDateTimeAxis *>(axis))
        return tr("%1 to %2").arg(dateTime(time->min()), dateTime(time->max()));
    if (const auto *value = qobject_cast<const QValueAxis *>(axis)) {
        // UtcTimeAxis (src/plot) holds UTC epoch milliseconds on a value axis; matched by name
        // so this file needs no link dependency on it.
        if (axis->inherits("openswmmvis::plot::UtcTimeAxis")) {
            const auto utc = [](double ms) {
                return QDateTime::fromMSecsSinceEpoch(qint64(ms), QTimeZone::UTC);
            };
            return tr("%1 to %2").arg(dateTime(utc(value->min())), dateTime(utc(value->max())));
        }
        return tr("%1 to %2").arg(number(value->min()), number(value->max()));
    }
    if (const auto *log = qobject_cast<const QLogValueAxis *>(axis))
        return tr("%1 to %2").arg(number(log->min()), number(log->max()));
    return {};
}

/** @return One phrase for a visible series, or empty when it has nothing to read. */
QString seriesPhrase(QAbstractSeries *series, int ordinal)
{
    const QXYSeries *xy = qobject_cast<QXYSeries *>(series);
    if (auto *area = qobject_cast<QAreaSeries *>(series))
        xy = area->upperSeries();
    const QString name = series->name().isEmpty() ? tr("Series %1").arg(ordinal) : series->name();
    if (!xy)
        return name;
    const int count = xy->count();
    if (count == 0)
        return {}; // helper overlays (selection markers) are empty most of the time
    double lo = std::numeric_limits<double>::infinity();
    double hi = -lo;
    for (const QPointF &p : xy->points()) {
        if (std::isfinite(p.y())) {
            lo = std::min(lo, p.y());
            hi = std::max(hi, p.y());
        }
    }
    const QString points = count == 1 ? tr("1 point") : tr("%1 points").arg(count);
    if (lo > hi)
        return tr("%1: %2").arg(name, points);
    return tr("%1: %2, values %3 to %4").arg(name, points, number(lo), number(hi));
}

class ChartViewAccessible : public LiveWidgetAccessible
{
public:
    explicit ChartViewAccessible(QChartView *view) : LiveWidgetAccessible(view, QAccessible::Chart)
    {
    }

    QString text(QAccessible::Text t) const override
    {
        if (t == QAccessible::Name) {
            if (!view()->accessibleName().isEmpty())
                return view()->accessibleName();
            if (view()->chart() && !view()->chart()->title().isEmpty())
                return view()->chart()->title();
            return tr("Chart");
        }
        return LiveWidgetAccessible::text(t);
    }

protected:
    QString descriptionText() const override
    {
        QStringList parts;
        if (const QChart *chart = view()->chart()) {
            int ordinal = 0;
            for (QAbstractSeries *series : chart->series()) {
                ++ordinal;
                if (!series->isVisible())
                    continue;
                const QString phrase = seriesPhrase(series, ordinal);
                if (!phrase.isEmpty())
                    parts.append(phrase);
            }
            if (parts.isEmpty())
                parts.append(tr("No data"));
            for (const QAbstractAxis *axis : chart->axes()) {
                if (!axis->isVisible())
                    continue;
                QString title = axis->titleText();
                if (title.isEmpty())
                    title = axis->orientation() == Qt::Horizontal ? tr("Horizontal axis")
                                                                  : tr("Vertical axis");
                const QString range = axisRange(axis);
                parts.append(range.isEmpty() ? title : tr("%1 from %2").arg(title, range));
            }
        }
        if (!view()->accessibleDescription().isEmpty())
            parts.append(view()->accessibleDescription());
        return parts.join(QStringLiteral("; "));
    }

private:
    QChartView *view() const { return static_cast<QChartView *>(widget()); }
};

void registerChartViewAccessible()
{
    QAccessible::installFactory(&createAccessible<QChartView, ChartViewAccessible>);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(registerChartViewAccessible)

} // namespace openswmmvis::ui::accessibility
