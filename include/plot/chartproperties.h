/*!
 * \file   chartproperties.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice AT.3 — QObject wrapper around a QChart exposing its
 *         visual configuration as Q_PROPERTYs so a QPropertyModel can
 *         drive the per-chart "Chart Properties…" editor.
 *
 * Each setter applies its value to the wrapped chart immediately. The
 * chart is held via QPointer so the wrapper survives a chart deletion
 * (subsequent setters become no-ops; getters return cached values).
 *
 * Property layout (kept tight; the dialog hosts a single QTreeView):
 *   - Title       — titleText, titleFont
 *   - Y axis      — yAutoRange, yMin, yMax, yGridVisible
 *   - X axis      — xGridVisible
 *   - Fonts       — axisLabelFont, tickLabelFont
 *   - Colours     — backgroundColor, plotAreaColor, gridColor
 *   - Theme       — chartTheme (enum)
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

namespace openswmmvis::plot {

class ChartProperties : public QObject
{
    Q_OBJECT

public:
    /*! \brief Axis label number mode; values mirror
     *  openswmmvis::plot::NumberFormatMode (0=Decimals, 1=SignificantFigures,
     *  2=Scientific, 3=Engineering, 4=Thousands). */
    enum LabelFormatMode { Decimals = 0, SignificantFigures = 1,
                           Scientific = 2, Engineering = 3, Thousands = 4 };
    Q_ENUM(LabelFormatMode)

    /*! Combined number format offered as ONE dropdown, replacing a mode enum
     *  plus a free integer count. Mirrors openswmmvis::plot::
     *  AxisNumberFormatPreset value-for-value; QPropertyModel needs the
     *  enumerator list on the declaring class and labels each row with the
     *  enumerator name. numberformat.h owns the mapping to mode + digits. */
    enum AxisNumberFormat {
        Integer   = 0,
        Decimals1 = 1,
        Decimals2 = 2,
        Decimals3 = 3,
        Decimals4 = 4,
        Decimals6 = 5,
        SigFigs3  = 6,
        SigFigs4  = 7,
        SigFigs6  = 8,
        Scientific2      = 9,
        Scientific3      = 10,
        Scientific4      = 11,
        Engineering2     = 12,
        Engineering3     = 13,
        ThousandsInteger = 14,
        Thousands1       = 15,
        Thousands2       = 16
    };
    Q_ENUM(AxisNumberFormat)

    Q_PROPERTY(QString titleText        READ titleText        WRITE setTitleText        NOTIFY titleTextChanged)
    Q_PROPERTY(QFont   titleFont        READ titleFont        WRITE setTitleFont        NOTIFY titleFontChanged)

    Q_PROPERTY(bool    yAutoRange       READ yAutoRange       WRITE setYAutoRange       NOTIFY yAutoRangeChanged)
    Q_PROPERTY(qreal   yMin             READ yMin             WRITE setYMin             NOTIFY yMinChanged)
    Q_PROPERTY(qreal   yMax             READ yMax             WRITE setYMax             NOTIFY yMaxChanged)
    Q_PROPERTY(bool    yGridVisible     READ yGridVisible     WRITE setYGridVisible     NOTIFY yGridVisibleChanged)
    Q_PROPERTY(bool    xGridVisible     READ xGridVisible     WRITE setXGridVisible     NOTIFY xGridVisibleChanged)

    Q_PROPERTY(QFont   axisLabelFont    READ axisLabelFont    WRITE setAxisLabelFont    NOTIFY axisLabelFontChanged)
    Q_PROPERTY(QFont   tickLabelFont    READ tickLabelFont    WRITE setTickLabelFont    NOTIFY tickLabelFontChanged)

    Q_PROPERTY(QColor  backgroundColor  READ backgroundColor  WRITE setBackgroundColor  NOTIFY backgroundColorChanged)
    Q_PROPERTY(QColor  plotAreaColor    READ plotAreaColor    WRITE setPlotAreaColor    NOTIFY plotAreaColorChanged)
    Q_PROPERTY(QColor  gridColor        READ gridColor        WRITE setGridColor        NOTIFY gridColorChanged)

    Q_PROPERTY(int     chartTheme       READ chartTheme       WRITE setChartTheme       NOTIFY chartThemeChanged)

    Q_PROPERTY(ChartProperties::AxisNumberFormat xAxisNumberFormat READ xAxisNumberFormat WRITE setXAxisNumberFormat NOTIFY xAxisNumberFormatChanged)
    Q_PROPERTY(QString xLabelFormat    READ xLabelFormat    WRITE setXLabelFormat    NOTIFY xLabelFormatChanged)
    Q_PROPERTY(ChartProperties::AxisNumberFormat yAxisNumberFormat READ yAxisNumberFormat WRITE setYAxisNumberFormat NOTIFY yAxisNumberFormatChanged)
    Q_PROPERTY(QString yLabelFormat    READ yLabelFormat    WRITE setYLabelFormat    NOTIFY yLabelFormatChanged)
    Q_PROPERTY(ChartProperties::AxisNumberFormat statisticsFormatPreset READ statisticsFormatPreset WRITE setStatisticsFormatPreset NOTIFY statisticsFormatPresetChanged)
    Q_PROPERTY(QString statisticsFormat READ statisticsFormat WRITE setStatisticsFormat NOTIFY statisticsFormatChanged)

public:
    explicit ChartProperties(QChart *chart, QObject *parent = nullptr);
    ~ChartProperties() override = default;

    QChart *chart() const noexcept { return m_chart.data(); }

    /*! \brief AT.3 polish — QPropertyModel hook for human-friendly,
     *  group-prefixed display labels in the Chart Properties dialog.
     *  Returns labels like "Title — Text", "Y Axis — Min", etc. */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

signals:
    void displayLabelsChanged();
public:

    QString titleText()       const;
    QFont   titleFont()       const;
    bool    yAutoRange()      const noexcept { return m_yAutoRange; }
    qreal   yMin()            const;
    qreal   yMax()            const;
    bool    yGridVisible()    const;
    bool    xGridVisible()    const;
    QFont   axisLabelFont()   const;
    QFont   tickLabelFont()   const;
    QColor  backgroundColor() const;
    QColor  plotAreaColor()   const;
    QColor  gridColor()       const noexcept { return m_gridColor; }
    int     chartTheme()      const;

    /*! Combined format per axis, derived from the mode + digit count that
     *  remain the internal representation. */
    AxisNumberFormat xAxisNumberFormat() const;
    AxisNumberFormat yAxisNumberFormat() const;
    AxisNumberFormat statisticsFormatPreset() const;

    LabelFormatMode xLabelFormatMode() const noexcept { return m_xLabelMode; }
    int             xLabelPrecision()  const noexcept { return m_xLabelPrecision; }
    QString         xLabelFormat()     const          { return m_xLabelFormatStr; }
    LabelFormatMode yLabelFormatMode() const noexcept { return m_yLabelMode; }
    int             yLabelPrecision()  const noexcept { return m_yLabelPrecision; }
    QString         yLabelFormat()     const          { return m_yLabelFormatStr; }
    LabelFormatMode statisticsFormatMode() const noexcept { return m_statisticsMode; }
    int             statisticsPrecision() const noexcept { return m_statisticsPrecision; }
    QString         statisticsFormat() const { return m_statisticsFormatStr; }

    /*! \brief Current X/Y/statistics number format as the shared value type. */
    NumberFormat xFormat() const noexcept;
    NumberFormat yFormat() const noexcept;
    NumberFormat statisticsNumberFormat() const noexcept;

public slots:
    void setTitleText(const QString &text);
    void setTitleFont(const QFont &font);
    void setYAutoRange(bool on);
    void setYMin(qreal v);
    void setYMax(qreal v);
    void setYGridVisible(bool on);
    void setXGridVisible(bool on);
    void setAxisLabelFont(const QFont &font);
    void setTickLabelFont(const QFont &font);
    void setBackgroundColor(const QColor &c);
    void setPlotAreaColor(const QColor &c);
    void setGridColor(const QColor &c);
    void setChartTheme(int theme);

    void setXAxisNumberFormat(ChartProperties::AxisNumberFormat f);
    void setYAxisNumberFormat(ChartProperties::AxisNumberFormat f);
    void setStatisticsFormatPreset(ChartProperties::AxisNumberFormat f);
    void setXLabelFormatMode(ChartProperties::LabelFormatMode mode);
    void setXLabelPrecision(int count);
    void setXLabelFormat(const QString &spec);
    void setYLabelFormatMode(ChartProperties::LabelFormatMode mode);
    void setYLabelPrecision(int count);
    void setYLabelFormat(const QString &spec);
    void setStatisticsFormatMode(ChartProperties::LabelFormatMode mode);
    void setStatisticsPrecision(int count);
    void setStatisticsFormat(const QString &spec);
    void setStatisticsNumberFormat(const NumberFormat &format);

signals:
    void titleTextChanged(const QString &);
    void titleFontChanged(const QFont &);
    void yAutoRangeChanged(bool);
    void yMinChanged(qreal);
    void yMaxChanged(qreal);
    void yGridVisibleChanged(bool);
    void xGridVisibleChanged(bool);
    void axisLabelFontChanged(const QFont &);
    void tickLabelFontChanged(const QFont &);
    void backgroundColorChanged(const QColor &);
    void plotAreaColorChanged(const QColor &);
    void gridColorChanged(const QColor &);
    void chartThemeChanged(int);

    void xLabelFormatModeChanged(ChartProperties::LabelFormatMode);
    void xLabelPrecisionChanged(int);
    void xLabelFormatChanged(const QString &);
    void yLabelFormatModeChanged(ChartProperties::LabelFormatMode);
    void yLabelPrecisionChanged(int);
    void yLabelFormatChanged(const QString &);
    void statisticsFormatModeChanged(ChartProperties::LabelFormatMode);
    void statisticsPrecisionChanged(int);
    // Fired alongside the mode/precision signals above, so the combined
    // property notifies no matter which path changed the underlying pair.
    void xAxisNumberFormatChanged(ChartProperties::AxisNumberFormat);
    void yAxisNumberFormatChanged(ChartProperties::AxisNumberFormat);
    void statisticsFormatPresetChanged(ChartProperties::AxisNumberFormat);
    void statisticsFormatChanged(const QString &);

private:
    // Push the cached X/Y label formats onto the chart's value axes
    // (QDateTimeAxis/QCategoryAxis are skipped — only QValueAxis honours it).
    void applyLabelFormats_();

    QPointer<QChart> m_chart;

    // Cached values for QChart configuration that's awkward to read back
    // (grid colour isn't a first-class QChart attribute; it's per-axis pen).
    bool   m_yAutoRange = true;
    QColor m_gridColor  = QColor("#cccccc");

    // Axis label number formats. Seeded from the global Preferences default
    // in the constructor; per-chart edits override them (cached because a
    // printf format string can't be reliably parsed back to mode+count).
    LabelFormatMode m_xLabelMode      = Decimals;
    int             m_xLabelPrecision = 0;
    QString         m_xLabelFormatStr;            // optional printf override; empty = use mode+precision
    LabelFormatMode m_yLabelMode      = Decimals;
    int             m_yLabelPrecision = 2;
    QString         m_yLabelFormatStr;            // optional printf override; empty = use mode+precision

    // Statistics summary value format. Exposed through the same properties
    // dialog so the stats panel does not need its own formatting controls.
    LabelFormatMode m_statisticsMode      = Decimals;
    int             m_statisticsPrecision = 2;
    QString         m_statisticsFormatStr;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_CHARTPROPERTIES_H
