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

private:
    QPointer<QChart> m_chart;

    // Cached values for QChart configuration that's awkward to read back
    // (grid colour isn't a first-class QChart attribute; it's per-axis pen).
    bool   m_yAutoRange = true;
    QColor m_gridColor  = QColor("#cccccc");
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_CHARTPROPERTIES_H
