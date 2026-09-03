/*!
 * \file   chartproperties.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/chartproperties.h"

#include "plot/numberformat.h"

#include "core/preferencesmanager.h"

#include <QAbstractAxis>
#include <QBrush>
#include <QValueAxis>

#include <algorithm>

namespace openswmmvis::plot {

namespace {
// Helper: first Vertical axis on the chart (if any). Casts to QValueAxis
// when present — that's what we need for setRange / setGridLineVisible.
QValueAxis *firstYAxis(QChart *chart)
{
    if (!chart) return nullptr;
    const auto axes = chart->axes(Qt::Vertical);
    return axes.isEmpty() ? nullptr : qobject_cast<QValueAxis*>(axes.first());
}
QAbstractAxis *firstXAxis(QChart *chart)
{
    if (!chart) return nullptr;
    const auto axes = chart->axes(Qt::Horizontal);
    return axes.isEmpty() ? nullptr : axes.first();
}
} // namespace

ChartProperties::ChartProperties(QChart *chart, QObject *parent)
    : QObject(parent), m_chart(chart)
{
    // Seed the axis label formats from the global Preferences default, then
    // push them onto the chart so it opens with the user's chosen precision.
    auto *prefs = PreferencesManager::instance();
    m_xLabelMode      = static_cast<LabelFormatMode>(prefs->plotXAxisFormatMode());
    m_xLabelPrecision = prefs->plotXAxisPrecision();
    m_yLabelMode      = static_cast<LabelFormatMode>(prefs->plotYAxisFormatMode());
    m_yLabelPrecision = prefs->plotYAxisPrecision();
    m_statisticsMode      = m_yLabelMode;
    m_statisticsPrecision = m_yLabelPrecision;
    applyLabelFormats_();
}

NumberFormat ChartProperties::xFormat() const noexcept
{
    return { static_cast<NumberFormatMode>(m_xLabelMode), m_xLabelPrecision, m_xLabelFormatStr };
}

NumberFormat ChartProperties::yFormat() const noexcept
{
    return { static_cast<NumberFormatMode>(m_yLabelMode), m_yLabelPrecision, m_yLabelFormatStr };
}

NumberFormat ChartProperties::statisticsNumberFormat() const noexcept
{
    return {
        static_cast<NumberFormatMode>(m_statisticsMode),
        m_statisticsPrecision,
        m_statisticsFormatStr
    };
}

void ChartProperties::applyLabelFormats_()
{
    if (!m_chart) return;
    const QString xSpec = xFormat().printfSpec();
    const QString ySpec = yFormat().printfSpec();
    // Only QValueAxis honours a printf label format; QDateTimeAxis (the
    // comparison-plot time axis) and QCategoryAxis are left untouched.
    for (auto *ax : m_chart->axes(Qt::Horizontal))
        if (auto *va = qobject_cast<QValueAxis*>(ax)) va->setLabelFormat(xSpec);
    for (auto *ax : m_chart->axes(Qt::Vertical))
        if (auto *va = qobject_cast<QValueAxis*>(ax)) va->setLabelFormat(ySpec);
}

QString ChartProperties::displayLabelFor(const QString &name) const
{
    // AT.3 polish — visual grouping in the flat QPropertyModel view.
    if (name == QStringLiteral("titleText"))       return QStringLiteral("Title — Text");
    if (name == QStringLiteral("titleFont"))       return QStringLiteral("Title — Font");
    if (name == QStringLiteral("yAutoRange"))      return QStringLiteral("Y Axis — Auto range");
    if (name == QStringLiteral("yMin"))            return QStringLiteral("Y Axis — Min");
    if (name == QStringLiteral("yMax"))            return QStringLiteral("Y Axis — Max");
    if (name == QStringLiteral("yGridVisible"))    return QStringLiteral("Y Axis — Grid lines");
    if (name == QStringLiteral("xGridVisible"))    return QStringLiteral("X Axis — Grid lines");
    if (name == QStringLiteral("axisLabelFont"))   return QStringLiteral("Fonts — Axis title");
    if (name == QStringLiteral("tickLabelFont"))   return QStringLiteral("Fonts — Tick labels");
    if (name == QStringLiteral("backgroundColor")) return QStringLiteral("Colours — Background");
    if (name == QStringLiteral("plotAreaColor"))   return QStringLiteral("Colours — Plot area");
    if (name == QStringLiteral("gridColor"))       return QStringLiteral("Colours — Grid");
    if (name == QStringLiteral("chartTheme"))      return QStringLiteral("Theme");
    if (name == QStringLiteral("xAxisNumberFormat")) return QStringLiteral("X Axis — Number format");
    if (name == QStringLiteral("xLabelFormat"))     return QStringLiteral("X Axis — Custom format");
    if (name == QStringLiteral("yAxisNumberFormat")) return QStringLiteral("Y Axis — Number format");
    if (name == QStringLiteral("yLabelFormat"))     return QStringLiteral("Y Axis — Custom format");
    if (name == QStringLiteral("statisticsFormatPreset")) return QStringLiteral("Statistics — Number format");
    if (name == QStringLiteral("statisticsFormat"))       return QStringLiteral("Statistics — Custom format");
    return {};   // empty → fall back to default name
}

// ----- Getters --------------------------------------------------------------

QString ChartProperties::titleText() const
{
    return m_chart ? m_chart->title() : QString();
}

QFont ChartProperties::titleFont() const
{
    return m_chart ? m_chart->titleFont() : QFont();
}

qreal ChartProperties::yMin() const
{
    auto *ax = firstYAxis(m_chart.data());
    return ax ? ax->min() : 0.0;
}

qreal ChartProperties::yMax() const
{
    auto *ax = firstYAxis(m_chart.data());
    return ax ? ax->max() : 1.0;
}

bool ChartProperties::yGridVisible() const
{
    auto *ax = firstYAxis(m_chart.data());
    return ax ? ax->isGridLineVisible() : true;
}

bool ChartProperties::xGridVisible() const
{
    auto *ax = firstXAxis(m_chart.data());
    return ax ? ax->isGridLineVisible() : true;
}

QFont ChartProperties::axisLabelFont() const
{
    auto *ax = firstYAxis(m_chart.data());
    return ax ? ax->titleFont() : QFont();
}

QFont ChartProperties::tickLabelFont() const
{
    auto *ax = firstYAxis(m_chart.data());
    return ax ? ax->labelsFont() : QFont();
}

QColor ChartProperties::backgroundColor() const
{
    if (!m_chart) return QColor();
    return m_chart->backgroundBrush().color();
}

QColor ChartProperties::plotAreaColor() const
{
    if (!m_chart) return QColor();
    return m_chart->plotAreaBackgroundBrush().color();
}

int ChartProperties::chartTheme() const
{
    return m_chart ? static_cast<int>(m_chart->theme()) : 0;
}

// ----- Setters --------------------------------------------------------------

void ChartProperties::setTitleText(const QString &text)
{
    if (!m_chart || m_chart->title() == text) return;
    m_chart->setTitle(text);
    emit titleTextChanged(text);
}

void ChartProperties::setTitleFont(const QFont &font)
{
    if (!m_chart || m_chart->titleFont() == font) return;
    m_chart->setTitleFont(font);
    emit titleFontChanged(font);
}

void ChartProperties::setYAutoRange(bool on)
{
    if (m_yAutoRange == on) return;
    m_yAutoRange = on;
    // When auto re-enabled, leave the current range as-is; the next
    // chart rebuild re-fits naturally. The dialog can pass a "refit
    // now" button if needed.
    emit yAutoRangeChanged(on);
}

void ChartProperties::setYMin(qreal v)
{
    auto *ax = firstYAxis(m_chart.data());
    if (!ax || ax->min() == v) return;
    ax->setMin(v);
    m_yAutoRange = false;
    emit yMinChanged(v);
}

void ChartProperties::setYMax(qreal v)
{
    auto *ax = firstYAxis(m_chart.data());
    if (!ax || ax->max() == v) return;
    ax->setMax(v);
    m_yAutoRange = false;
    emit yMaxChanged(v);
}

void ChartProperties::setYGridVisible(bool on)
{
    auto *ax = firstYAxis(m_chart.data());
    if (!ax || ax->isGridLineVisible() == on) return;
    ax->setGridLineVisible(on);
    emit yGridVisibleChanged(on);
}

void ChartProperties::setXGridVisible(bool on)
{
    auto *ax = firstXAxis(m_chart.data());
    if (!ax || ax->isGridLineVisible() == on) return;
    ax->setGridLineVisible(on);
    emit xGridVisibleChanged(on);
}

void ChartProperties::setAxisLabelFont(const QFont &font)
{
    if (!m_chart) return;
    for (auto *ax : m_chart->axes())
        ax->setTitleFont(font);
    emit axisLabelFontChanged(font);
}

void ChartProperties::setTickLabelFont(const QFont &font)
{
    if (!m_chart) return;
    for (auto *ax : m_chart->axes())
        ax->setLabelsFont(font);
    emit tickLabelFontChanged(font);
}

void ChartProperties::setBackgroundColor(const QColor &c)
{
    if (!m_chart) return;
    m_chart->setBackgroundBrush(QBrush(c));
    emit backgroundColorChanged(c);
}

void ChartProperties::setPlotAreaColor(const QColor &c)
{
    if (!m_chart) return;
    m_chart->setPlotAreaBackgroundBrush(QBrush(c));
    m_chart->setPlotAreaBackgroundVisible(c.isValid());
    emit plotAreaColorChanged(c);
}

void ChartProperties::setGridColor(const QColor &c)
{
    if (!m_chart) return;
    m_gridColor = c;
    for (auto *ax : m_chart->axes()) {
        QPen gridPen = ax->gridLinePen();
        gridPen.setColor(c);
        ax->setGridLinePen(gridPen);
    }
    emit gridColorChanged(c);
}

void ChartProperties::setChartTheme(int theme)
{
    if (!m_chart) return;
    const auto t = static_cast<QChart::ChartTheme>(theme);
    if (m_chart->theme() == t) return;
    m_chart->setTheme(t);
    emit chartThemeChanged(theme);
}


// ---------------------------------------------------------------------------
// Combined number-format presets (one dropdown per axis)
// ---------------------------------------------------------------------------

namespace {
ChartProperties::AxisNumberFormat presetOf(ChartProperties::LabelFormatMode m, int count)
{
    return static_cast<ChartProperties::AxisNumberFormat>(
        openswmmvis::plot::presetForNumberFormat(
            static_cast<openswmmvis::plot::NumberFormatMode>(m), count));
}
} // namespace

ChartProperties::AxisNumberFormat ChartProperties::xAxisNumberFormat() const
{ return presetOf(m_xLabelMode, m_xLabelPrecision); }

ChartProperties::AxisNumberFormat ChartProperties::yAxisNumberFormat() const
{ return presetOf(m_yLabelMode, m_yLabelPrecision); }

ChartProperties::AxisNumberFormat ChartProperties::statisticsFormatPreset() const
{ return presetOf(m_statisticsMode, m_statisticsPrecision); }

void ChartProperties::setXAxisNumberFormat(AxisNumberFormat f)
{
    // One user choice drives both stored fields; mode + count stay the
    // internal representation every formatter already reads.
    const auto nf = openswmmvis::plot::numberFormatForPreset(static_cast<int>(f));
    setXLabelFormatMode(static_cast<LabelFormatMode>(nf.mode));
    setXLabelPrecision(nf.count);
}

void ChartProperties::setYAxisNumberFormat(AxisNumberFormat f)
{
    const auto nf = openswmmvis::plot::numberFormatForPreset(static_cast<int>(f));
    setYLabelFormatMode(static_cast<LabelFormatMode>(nf.mode));
    setYLabelPrecision(nf.count);
}

void ChartProperties::setStatisticsFormatPreset(AxisNumberFormat f)
{
    const auto nf = openswmmvis::plot::numberFormatForPreset(static_cast<int>(f));
    setStatisticsFormatMode(static_cast<LabelFormatMode>(nf.mode));
    setStatisticsPrecision(nf.count);
}

void ChartProperties::setXLabelFormatMode(LabelFormatMode mode)
{
    if (m_xLabelMode == mode) return;
    m_xLabelMode = mode;
    applyLabelFormats_();
    emit xLabelFormatModeChanged(mode);
    emit xAxisNumberFormatChanged(xAxisNumberFormat());
}

void ChartProperties::setXLabelPrecision(int count)
{
    const int c = std::clamp(count, 0, 10);
    if (m_xLabelPrecision == c) return;
    m_xLabelPrecision = c;
    applyLabelFormats_();
    emit xLabelPrecisionChanged(c);
    emit xAxisNumberFormatChanged(xAxisNumberFormat());
}

void ChartProperties::setXLabelFormat(const QString &spec)
{
    if (m_xLabelFormatStr == spec) return;
    m_xLabelFormatStr = spec;
    applyLabelFormats_();
    emit xLabelFormatChanged(spec);
}

void ChartProperties::setYLabelFormatMode(LabelFormatMode mode)
{
    if (m_yLabelMode == mode) return;
    m_yLabelMode = mode;
    applyLabelFormats_();
    emit yLabelFormatModeChanged(mode);
    emit yAxisNumberFormatChanged(yAxisNumberFormat());
}

void ChartProperties::setYLabelPrecision(int count)
{
    const int c = std::clamp(count, 0, 10);
    if (m_yLabelPrecision == c) return;
    m_yLabelPrecision = c;
    applyLabelFormats_();
    emit yLabelPrecisionChanged(c);
    emit yAxisNumberFormatChanged(yAxisNumberFormat());
}

void ChartProperties::setYLabelFormat(const QString &spec)
{
    if (m_yLabelFormatStr == spec) return;
    m_yLabelFormatStr = spec;
    applyLabelFormats_();
    emit yLabelFormatChanged(spec);
}

void ChartProperties::setStatisticsFormatMode(LabelFormatMode mode)
{
    if (m_statisticsMode == mode) return;
    m_statisticsMode = mode;
    emit statisticsFormatModeChanged(mode);
    emit statisticsFormatPresetChanged(statisticsFormatPreset());
}

void ChartProperties::setStatisticsPrecision(int count)
{
    const int c = std::clamp(count, 0, 10);
    if (m_statisticsPrecision == c) return;
    m_statisticsPrecision = c;
    emit statisticsPrecisionChanged(c);
    emit statisticsFormatPresetChanged(statisticsFormatPreset());
}

void ChartProperties::setStatisticsFormat(const QString &spec)
{
    if (m_statisticsFormatStr == spec) return;
    m_statisticsFormatStr = spec;
    emit statisticsFormatChanged(spec);
}

void ChartProperties::setStatisticsNumberFormat(const NumberFormat &format)
{
    // LabelFormatMode mirrors NumberFormatMode value-for-value.
    const auto mode = static_cast<LabelFormatMode>(format.mode);
    const int precision = std::clamp(format.count, 0, 10);

    const bool modeChanged = m_statisticsMode != mode;
    const bool precisionChanged = m_statisticsPrecision != precision;
    const bool formatChanged = m_statisticsFormatStr != format.custom;

    m_statisticsMode = mode;
    m_statisticsPrecision = precision;
    m_statisticsFormatStr = format.custom;

    if (modeChanged) emit statisticsFormatModeChanged(mode);
    if (precisionChanged) emit statisticsPrecisionChanged(precision);
    if (formatChanged) emit statisticsFormatChanged(format.custom);
}

} // namespace openswmmvis::plot
