/**
 * @file      chartproperties.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice AT.3 — QObject wrapper around a QChart exposing its
 *         visual configuration as Q_PROPERTYs so a QPropertyModel can
 *         drive the per-chart "Chart Properties…" editor.
 *
 * Chart-backed setters apply their values immediately. The
 * chart is held via QPointer so the wrapper survives a chart deletion
 * (chart-backed getters use fallbacks and cached formatting/policies remain available).
 *
 * Property layout (kept tight; the dialog hosts a single QTreeView):
 *   - Title       — titleText, titleFont
 *   - Y axis      — yAutoRange, yMin, yMax, yGridVisible
 *   - X axis      — xGridVisible
 *   - Fonts       — axisLabelFont, tickLabelFont
 *   - Colours     — backgroundColor, plotAreaColor, gridColor
 *   - Theme       — chartTheme (enum)
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_CHARTPROPERTIES_H
#define OPENSWMMVIS_PLOT_CHARTPROPERTIES_H

#include "plot/numberformat.h"

#include <QChart>
#include <QColor>
#include <QFont>
#include <QObject>
#include <QPointer>
#include <QString>

namespace openswmmvis::plot
{

class ChartProperties : public QObject
{
    Q_OBJECT

public:
    /*! \brief Axis label number mode; values mirror
     *  openswmmvis::plot::NumberFormatMode (0=Decimals, 1=SignificantFigures,
     *  2=Scientific, 3=Engineering, 4=Thousands). */
    enum LabelFormatMode {
        Decimals = 0,
        SignificantFigures = 1,
        Scientific = 2,
        Engineering = 3,
        Thousands = 4
    };
    Q_ENUM(LabelFormatMode)

    /*! Combined number format offered as ONE dropdown, replacing a mode enum
     *  plus a free integer count. Mirrors openswmmvis::plot::
     *  AxisNumberFormatPreset value-for-value; QPropertyModel needs the
     *  enumerator list on the declaring class and labels each row with the
     *  enumerator name. numberformat.h owns the mapping to mode + digits. */
    enum AxisNumberFormat {
        Integer = 0,
        Decimals1 = 1,
        Decimals2 = 2,
        Decimals3 = 3,
        Decimals4 = 4,
        Decimals6 = 5,
        SigFigs3 = 6,
        SigFigs4 = 7,
        SigFigs6 = 8,
        Scientific2 = 9,
        Scientific3 = 10,
        Scientific4 = 11,
        Engineering2 = 12,
        Engineering3 = 13,
        ThousandsInteger = 14,
        Thousands1 = 15,
        Thousands2 = 16
    };
    Q_ENUM(AxisNumberFormat)

    enum LegendPosition { Top = 0, Bottom, Left, Right };
    Q_ENUM(LegendPosition)

    Q_PROPERTY(QString titleText READ titleText WRITE setTitleText NOTIFY titleTextChanged)
    Q_PROPERTY(QFont titleFont READ titleFont WRITE setTitleFont NOTIFY titleFontChanged)

    Q_PROPERTY(bool yAutoRange READ yAutoRange WRITE setYAutoRange NOTIFY yAutoRangeChanged)
    Q_PROPERTY(qreal yMin READ yMin WRITE setYMin NOTIFY yMinChanged)
    Q_PROPERTY(qreal yMax READ yMax WRITE setYMax NOTIFY yMaxChanged)
    Q_PROPERTY(bool yGridVisible READ yGridVisible WRITE setYGridVisible NOTIFY yGridVisibleChanged)
    Q_PROPERTY(bool xGridVisible READ xGridVisible WRITE setXGridVisible NOTIFY xGridVisibleChanged)

    Q_PROPERTY(
        QFont axisLabelFont READ axisLabelFont WRITE setAxisLabelFont NOTIFY axisLabelFontChanged)
    Q_PROPERTY(
        QFont tickLabelFont READ tickLabelFont WRITE setTickLabelFont NOTIFY tickLabelFontChanged)

    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY
                   backgroundColorChanged)
    Q_PROPERTY(
        QColor plotAreaColor READ plotAreaColor WRITE setPlotAreaColor NOTIFY plotAreaColorChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)

    Q_PROPERTY(int chartTheme READ chartTheme WRITE setChartTheme NOTIFY chartThemeChanged)
    Q_PROPERTY(ChartProperties::LegendPosition legendPosition READ legendPosition WRITE
                   setLegendPosition NOTIFY legendPositionChanged)

    Q_PROPERTY(ChartProperties::AxisNumberFormat xAxisNumberFormat READ xAxisNumberFormat WRITE
                   setXAxisNumberFormat NOTIFY xAxisNumberFormatChanged)
    Q_PROPERTY(
        QString xLabelFormat READ xLabelFormat WRITE setXLabelFormat NOTIFY xLabelFormatChanged)
    Q_PROPERTY(ChartProperties::AxisNumberFormat yAxisNumberFormat READ yAxisNumberFormat WRITE
                   setYAxisNumberFormat NOTIFY yAxisNumberFormatChanged)
    Q_PROPERTY(
        QString yLabelFormat READ yLabelFormat WRITE setYLabelFormat NOTIFY yLabelFormatChanged)
    Q_PROPERTY(ChartProperties::AxisNumberFormat statisticsFormatPreset READ statisticsFormatPreset
                   WRITE setStatisticsFormatPreset NOTIFY statisticsFormatPresetChanged)
    Q_PROPERTY(QString statisticsFormat READ statisticsFormat WRITE setStatisticsFormat NOTIFY
                   statisticsFormatChanged)

public:
    /**
     * @brief Bind a chart weakly and initialize numeric formatting from application preferences.
     * @param[in,out] chart Optional borrowed chart; construction immediately applies X/Y numeric formats to its supported axes. No chart reparenting/ownership transfer occurs.
     * @param[in] parent Optional QObject owner for this wrapper. Statistics format starts from the Y-axis preference.
     * @note Use on the GUI thread. Chart deletion clears the QPointer; chart-backed getters then return their documented fallbacks while cached format/policy setters can still operate.
     */
    explicit ChartProperties(QChart *chart, QObject *parent = nullptr);
    /**
     * @brief Release the property wrapper without deleting the borrowed chart.
     */
    ~ChartProperties() override = default;

    /**
     * @brief Inspect the guarded chart binding.
     * @return Borrowed chart pointer, null after deletion or if constructed unbound.
     */
    QChart *chart() const noexcept { return m_chart.data(); }

    /*! \brief AT.3 polish — QPropertyModel hook for human-friendly,
     *  group-prefixed display labels in the Chart Properties dialog.
     *  Returns labels like "Title — Text", "Y Axis — Min", etc.
     * @param[in] propertyName Exact meta-property key.
     * @return Human-readable group label, or empty for the property view's normal fallback.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

signals:
    /**
     * @brief Property-view label invalidation hook; this implementation does not emit it automatically.
     */
    void displayLabelsChanged();

public:
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return Live chart title, or empty when unbound.
     */
    QString titleText() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return Live chart title font, or default QFont when unbound.
     */
    QFont titleFont() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return Cached host refit policy, initially true; does not inspect or continuously fit the chart.
     */
    bool yAutoRange() const noexcept { return m_yAutoRange; }
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return First vertical axis minimum when castable to QValueAxis, otherwise zero. Units follow that axis.
     */
    qreal yMin() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return First vertical axis maximum when castable to QValueAxis, otherwise one. Units follow that axis.
     */
    qreal yMax() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return First vertical QValueAxis grid visibility, or true when absent/nonmatching.
     */
    bool yGridVisible() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return First horizontal axis grid visibility, or true when absent.
     */
    bool xGridVisible() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return First vertical QValueAxis title font, or default QFont. Does not merge fonts from other axes.
     */
    QFont axisLabelFont() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return First vertical QValueAxis tick-label font, or default QFont.
     */
    QFont tickLabelFont() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return Live background brush colour, invalid when no chart exists.
     */
    QColor backgroundColor() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return Live plot-area brush colour, invalid when no chart exists; visibility is not encoded in this value.
     */
    QColor plotAreaColor() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return Cached last set grid colour, initially RGB 204/204/204; not a scan of current axis pens.
     */
    QColor gridColor() const noexcept { return m_gridColor; }
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return Live QChart theme integer, or zero when no chart exists.
     */
    int chartTheme() const;
    /**
     * @brief Inspect chart configuration or its specified fallback.
     * @return Live legend alignment mapped to Top/Bottom/Left/Right; unknown alignments map Top, and an absent chart uses the last cached position.
     */
    LegendPosition legendPosition() const;

    /*! Combined format per axis, derived from the mode + digit count that
     *  remain the internal representation.
     * @brief Represent the cached X-axis mode/precision as a combined preset.
     * @return Closest matching preset via presetForNumberFormat(); custom overrides are ignored by this approximation.
     */
    AxisNumberFormat xAxisNumberFormat() const;
    /**
     * @brief Represent the cached Y-axis mode/precision as a combined preset.
     * @return Closest matching preset via presetForNumberFormat(); custom overrides are ignored by this approximation.
     */
    AxisNumberFormat yAxisNumberFormat() const;
    /**
     * @brief Represent the cached statistics mode/precision as a combined preset.
     * @return Closest matching preset via presetForNumberFormat(); custom overrides are ignored by this approximation.
     */
    AxisNumberFormat statisticsFormatPreset() const;

    /**
     * @brief Inspect the X-axis format cache.
     * @return Raw cached numeric family; not inferred from a live printf string.
     */
    LabelFormatMode xLabelFormatMode() const noexcept { return m_xLabelMode; }
    /**
     * @brief Inspect the X-axis format cache.
     * @return Raw cached digit count. Typed precision setters clamp [0,10]; formatting imposes its mode-specific lower bound.
     */
    int xLabelPrecision() const noexcept { return m_xLabelPrecision; }
    /**
     * @brief Inspect the X-axis format cache.
     * @return Stored optional custom printf text, including invalid syntax that NumberFormat ignores.
     */
    QString xLabelFormat() const { return m_xLabelFormatStr; }
    /**
     * @brief Inspect the Y-axis format cache.
     * @return Raw cached numeric family; not inferred from a live printf string.
     */
    LabelFormatMode yLabelFormatMode() const noexcept { return m_yLabelMode; }
    /**
     * @brief Inspect the Y-axis format cache.
     * @return Raw cached digit count. Typed precision setters clamp [0,10]; formatting imposes its mode-specific lower bound.
     */
    int yLabelPrecision() const noexcept { return m_yLabelPrecision; }
    /**
     * @brief Inspect the Y-axis format cache.
     * @return Stored optional custom printf text, including invalid syntax that NumberFormat ignores.
     */
    QString yLabelFormat() const { return m_yLabelFormatStr; }
    /**
     * @brief Inspect the statistics format cache.
     * @return Raw cached numeric family; not inferred from a live printf string.
     */
    LabelFormatMode statisticsFormatMode() const noexcept { return m_statisticsMode; }
    /**
     * @brief Inspect the statistics format cache.
     * @return Raw cached digit count. Typed precision setters clamp [0,10]; formatting imposes its mode-specific lower bound.
     */
    int statisticsPrecision() const noexcept { return m_statisticsPrecision; }
    /**
     * @brief Inspect the statistics format cache.
     * @return Stored optional custom printf text, including invalid syntax that NumberFormat ignores.
     */
    QString statisticsFormat() const { return m_statisticsFormatStr; }

    /*! \brief Current X/Y/statistics number format as the shared value type.
     * @brief Copy the X-axis formatting policy as a shared value type.
     * @return Raw mode/count/custom snapshot, without validating custom or changing values.
     */
    NumberFormat xFormat() const noexcept;
    /**
     * @brief Copy the Y-axis formatting policy as a shared value type.
     * @return Raw mode/count/custom snapshot, without validating custom or changing values.
     */
    NumberFormat yFormat() const noexcept;
    /**
     * @brief Copy the statistics formatting policy as a shared value type.
     * @return Raw mode/count/custom snapshot, without validating custom or changing values.
     */
    NumberFormat statisticsNumberFormat() const noexcept;

public slots:
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] text Chart title text, copied verbatim.
     * @note No-op without a chart or for an identical current title; otherwise applies and emits titleTextChanged().
     */
    void setTitleText(const QString &text);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] font Chart title font.
     * @note No-op without a chart or for the same font; otherwise applies and emits titleFontChanged().
     */
    void setTitleFont(const QFont &font);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] on Host-level automatic-refit policy.
     * @note Updates only the cached flag and emits yAutoRangeChanged() if changed, even without a chart. Does not refit immediately.
     */
    void setYAutoRange(bool on);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] v Minimum in the first vertical QValueAxis's data units; must satisfy Qt range rules.
     * @note No-op without a matching first vertical axis or for equality. Calls its setMin(), clears the cached auto-range flag and emits yMinChanged(v); no yAutoRangeChanged() is emitted and the other bound may move under Qt rules.
     */
    void setYMin(qreal v);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] v Maximum in the first vertical QValueAxis's data units; must satisfy Qt range rules.
     * @note No-op without a matching first vertical axis or for equality. Calls its setMax(), clears auto-range and emits yMaxChanged(v) without a yAutoRangeChanged() notification.
     */
    void setYMax(qreal v);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] on Requested grid visibility for the first vertical QValueAxis.
     * @note Missing/nonmatching axis or equality is a no-op; applies and emits yGridVisibleChanged() otherwise.
     */
    void setYGridVisible(bool on);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] on Requested grid visibility for the first horizontal axis.
     * @note Missing axis or equality is a no-op; applies and emits xGridVisibleChanged() otherwise.
     */
    void setXGridVisible(bool on);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] font Title font to apply across all chart axes.
     * @note No-op without a chart; otherwise updates every axis and emits axisLabelFontChanged(), even if unchanged.
     */
    void setAxisLabelFont(const QFont &font);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] font Tick-label font for every chart axis.
     * @note No-op without a chart; otherwise applies and emits tickLabelFontChanged(), even if unchanged.
     */
    void setTickLabelFont(const QFont &font);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] c Background brush colour; no validity checks.
     * @note No-op without a chart; otherwise replaces the brush and emits backgroundColorChanged(), even if unchanged.
     */
    void setBackgroundColor(const QColor &c);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] c Plot-area colour; invalid colour hides plot-area background.
     * @note No-op without a chart; otherwise sets brush/visibility and emits plotAreaColorChanged(), even if unchanged.
     */
    void setPlotAreaColor(const QColor &c);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] c Colour copied into every existing chart axis grid pen; other pen fields are preserved.
     * @note No-op without a chart, including the cache. Otherwise caches/applies and emits gridColorChanged(), even if unchanged. Later-added axes are not initialized here.
     */
    void setGridColor(const QColor &c);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] theme QChart::ChartTheme integer expected valid; no range validation occurs.
     * @note Missing chart or equality is a no-op. Qt applies the theme, which can reset visual defaults, then chartThemeChanged() is emitted.
     */
    void setChartTheme(int theme);
    /**
     * @brief Edit the chart configuration through its property adapter.
     * @param[in] position Top/Bottom/Left/Right; other enum values are ignored.
     * @note An identical effective alignment is a no-op. Caches the position, changes the live legend if present and emits legendPositionChanged(); also works while unbound.
     */
    void setLegendPosition(ChartProperties::LegendPosition position);

    /**
     * @brief Apply a combined x preset through the separate mode/count setters.
     * @param[in] f Preset value; out-of-range input falls back to two decimals.
     * @note Custom printf text is retained and can still override the preset. Changed mode and count may each emit their own notifications; this is not a single batched update.
     */
    void setXAxisNumberFormat(ChartProperties::AxisNumberFormat f);
    /**
     * @brief Apply a combined y preset through the separate mode/count setters.
     * @param[in] f Preset value; out-of-range input falls back to two decimals.
     * @note Custom printf text is retained and can still override the preset. Changed mode and count may each emit their own notifications; this is not a single batched update.
     */
    void setYAxisNumberFormat(ChartProperties::AxisNumberFormat f);
    /**
     * @brief Apply a combined statistics preset through the separate mode/count setters.
     * @param[in] f Preset value; out-of-range input falls back to two decimals.
     * @note Custom printf text is retained and can still override the preset. Changed mode and count may each emit their own notifications; this is not a single batched update.
     */
    void setStatisticsFormatPreset(ChartProperties::AxisNumberFormat f);
    /**
     * @brief Store the x numeric-format family.
     * @param[in] mode Raw enum value, copied without enum validation; equality is a no-op.
     * @note Reapplies numeric X/Y axis formats, then emits mode and combined-preset notifications.
     */
    void setXLabelFormatMode(ChartProperties::LabelFormatMode mode);
    /**
     * @brief Store x numeric precision.
     * @param[in] count Requested digits, clamped to [0,10]; equality after clamping is a no-op. SignificantFigures renders at least one digit.
     * @note Reapplies numeric axis formats, then emits precision and combined-preset notifications.
     */
    void setXLabelPrecision(int count);
    /**
     * @brief Store a x optional custom printf override.
     * @param[in] spec Verbatim text; empty/invalid strings fall back to mode/count at formatting time, with no edit rejection. Equality is a no-op.
     * @note Reapplies numeric formats and emits the custom-format signal, without combined-preset notification.
     */
    void setXLabelFormat(const QString &spec);
    /**
     * @brief Store the y numeric-format family.
     * @param[in] mode Raw enum value, copied without enum validation; equality is a no-op.
     * @note Reapplies numeric X/Y axis formats, then emits mode and combined-preset notifications.
     */
    void setYLabelFormatMode(ChartProperties::LabelFormatMode mode);
    /**
     * @brief Store y numeric precision.
     * @param[in] count Requested digits, clamped to [0,10]; equality after clamping is a no-op. SignificantFigures renders at least one digit.
     * @note Reapplies numeric axis formats, then emits precision and combined-preset notifications.
     */
    void setYLabelPrecision(int count);
    /**
     * @brief Store a y optional custom printf override.
     * @param[in] spec Verbatim text; empty/invalid strings fall back to mode/count at formatting time, with no edit rejection. Equality is a no-op.
     * @note Reapplies numeric formats and emits the custom-format signal, without combined-preset notification.
     */
    void setYLabelFormat(const QString &spec);
    /**
     * @brief Store the statistics numeric-format family.
     * @param[in] mode Raw enum value, copied without enum validation; equality is a no-op.
     * @note Emits statisticsFormatModeChanged() then statisticsFormatPresetChanged(); no chart axis changes occur.
     */
    void setStatisticsFormatMode(ChartProperties::LabelFormatMode mode);
    /**
     * @brief Store statistics numeric precision.
     * @param[in] count Requested digits, clamped to [0,10]; equality after clamping is a no-op. SignificantFigures renders at least one digit.
     * @note Emits statisticsPrecisionChanged() then statisticsFormatPresetChanged(); no axes are modified.
     */
    void setStatisticsPrecision(int count);
    /**
     * @brief Store a statistics optional custom printf override.
     * @param[in] spec Verbatim text; empty/invalid strings fall back to mode/count at formatting time, with no edit rejection. Equality is a no-op.
     * @note Emits statisticsFormatChanged() only; custom text is independent of the combined preset.
     */
    void setStatisticsFormat(const QString &spec);
    /**
     * @brief Replace the three statistics-format fields together.
     * @param[in] format Raw mode/custom copied verbatim and count clamped to [0,10].
     * @note Emits each changed mode/precision/custom signal after all fields have been set, but does not emit statisticsFormatPresetChanged(). No persistence or chart mutation occurs.
     */
    void setStatisticsNumberFormat(const NumberFormat &format);

signals:
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void titleTextChanged(const QString &value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void titleFontChanged(const QFont &value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void yAutoRangeChanged(bool value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void yMinChanged(qreal value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void yMaxChanged(qreal value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void yGridVisibleChanged(bool value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void xGridVisibleChanged(bool value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void axisLabelFontChanged(const QFont &value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void tickLabelFontChanged(const QFont &value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void backgroundColorChanged(const QColor &value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void plotAreaColorChanged(const QColor &value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void gridColorChanged(const QColor &value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void chartThemeChanged(int value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void legendPositionChanged(ChartProperties::LegendPosition value);

    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void xLabelFormatModeChanged(ChartProperties::LabelFormatMode value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void xLabelPrecisionChanged(int value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void xLabelFormatChanged(const QString &value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void yLabelFormatModeChanged(ChartProperties::LabelFormatMode value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void yLabelPrecisionChanged(int value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void yLabelFormatChanged(const QString &value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void statisticsFormatModeChanged(ChartProperties::LabelFormatMode value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void statisticsPrecisionChanged(int value);
    // Fired alongside the mode/precision signals above, so the combined
    // property notifies no matter which path changed the underlying pair.
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void xAxisNumberFormatChanged(ChartProperties::AxisNumberFormat value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void yAxisNumberFormatChanged(ChartProperties::AxisNumberFormat value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void statisticsFormatPresetChanged(ChartProperties::AxisNumberFormat value);
    /**
     * @brief Notify the corresponding property adapter edit.
     * @param[in] value Value emitted by the matching setter; see its contract for normalization, affected axes and equality behavior. No external chart edits are monitored automatically.
     */
    void statisticsFormatChanged(const QString &value);

private:
    // Push the cached X/Y label formats onto the chart's value axes
    // (UtcTimeAxis is explicitly skipped; other QCategoryAxis subclasses match QValueAxis).
    /**
     * @brief Apply cached X/Y printf formats to every matching QValueAxis.
     * @note UtcTimeAxis is explicitly skipped; QDateTimeAxis fails the cast. Other QCategoryAxis subclasses inherit QValueAxis and receive the numeric format. Missing chart is a no-op.
     */
    void applyLabelFormats_();

    QPointer<QChart> m_chart;

    // Cached values for QChart configuration that's awkward to read back
    // (grid colour isn't a first-class QChart attribute; it's per-axis pen).
    bool m_yAutoRange = true;
    QColor m_gridColor = QColor("#cccccc");
    LegendPosition m_legendPosition = Top;

    // Axis label number formats. Seeded from the global Preferences default
    // in the constructor; per-chart edits override them (cached because a
    // printf format string can't be reliably parsed back to mode+count).
    LabelFormatMode m_xLabelMode = Decimals;
    int m_xLabelPrecision = 0;
    QString m_xLabelFormatStr; // optional printf override; empty = use mode+precision
    LabelFormatMode m_yLabelMode = Decimals;
    int m_yLabelPrecision = 2;
    QString m_yLabelFormatStr; // optional printf override; empty = use mode+precision

    // Statistics summary value format. Exposed through the same properties
    // dialog so the stats panel does not need its own formatting controls.
    LabelFormatMode m_statisticsMode = Decimals;
    int m_statisticsPrecision = 2;
    QString m_statisticsFormatStr;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_CHARTPROPERTIES_H
