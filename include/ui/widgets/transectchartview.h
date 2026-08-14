/*!
 * \file   transectchartview.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.4 — interactive cross-section preview chart.
 *
 * Subclasses QChartView (not InteractiveChartView — the existing modes
 * Select/Pan/ZoomIn/ZoomOut don't compose with an EditPoints handle drag,
 * and stacking two competing mouse-event regimes is more friction than
 * value here). Implements the legacy Dprevplot.pas series set plus modern
 * additions:
 *
 *   - QAreaSeries `m_groundFill` — semi-transparent fill below the bank line
 *   - QLineSeries `m_leftOverbankLine`  — from leftmost station to xLeftBank
 *   - QLineSeries `m_rightOverbankLine` — from xRightBank to rightmost station
 *   - QLineSeries `m_channelLine` — slice between xLeftBank and xRightBank
 *   - QScatterSeries `m_handles` — one marker per station; the drag layer
 *   - QLineSeries dotted overlays at xLeftBank / xRightBank (bank markers)
 *
 * Always-on: mouse-wheel zoom about cursor; middle-button drag = pan.
 *
 * Toolbar / programmatic toggles:
 *   - Pan mode  — left-button drag pans
 *   - EditPoints mode — left-button hit-test on m_handles drags a station
 *     (monotonicity clamped via TransectProvider::setPointLive; Shift =
 *     Y-only; Ctrl = X-only)
 *   - Zoom-to-extent — fits both axes to the provider's bounding box +10%
 *   - Reset zoom — calls QChart::zoomReset
 *
 * Right-click emits `contextMenuRequestedAt(globalPos)` so the parent
 * dialog can build a menu (Chart properties… / Zoom to extent / Reset
 * zoom / Copy data… / Export PNG…).
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_TRANSECTCHARTVIEW_H
#define OPENSWMMVIS_UI_WIDGETS_TRANSECTCHARTVIEW_H

#include <QChartView>
#include <QPointer>
#include <QPointF>

class QChart;
class QAreaSeries;
class QLineSeries;
class QScatterSeries;
class QValueAxis;

namespace openswmmvis::transect { class TransectProvider; }

namespace openswmmvis::ui {

class TransectChartView : public QChartView
{
    Q_OBJECT

    // Q_PROPERTY surface so the right-click context-menu's QPropertyModel-backed
    // editor can drive the chart styling directly.
    Q_PROPERTY(QColor overbankColor    READ overbankColor    WRITE setOverbankColor    NOTIFY overbankColorChanged)
    Q_PROPERTY(QColor channelColor     READ channelColor     WRITE setChannelColor     NOTIFY channelColorChanged)
    Q_PROPERTY(QColor groundFillColor  READ groundFillColor  WRITE setGroundFillColor  NOTIFY groundFillColorChanged)
    Q_PROPERTY(int    handleSize       READ handleSize       WRITE setHandleSize       NOTIFY handleSizeChanged)
    Q_PROPERTY(bool   handlesVisible   READ handlesVisible   WRITE setHandlesVisible   NOTIFY handlesVisibleChanged)

    // Axis number format — same surface as plot::ChartProperties: one combined
    // dropdown per axis, plus an optional printf override.
    Q_PROPERTY(TransectChartView::AxisNumberFormat xAxisNumberFormat READ xAxisNumberFormat WRITE setXAxisNumberFormat NOTIFY xLabelPrecisionChanged)
    Q_PROPERTY(QString xLabelFormat    READ xLabelFormat    WRITE setXLabelFormat    NOTIFY xLabelFormatChanged)
    Q_PROPERTY(TransectChartView::AxisNumberFormat yAxisNumberFormat READ yAxisNumberFormat WRITE setYAxisNumberFormat NOTIFY yLabelPrecisionChanged)
    Q_PROPERTY(QString yLabelFormat    READ yLabelFormat    WRITE setYLabelFormat    NOTIFY yLabelFormatChanged)

public:
    /*! \brief Axis label number mode (mirrors plot::NumberFormatMode). */
    enum LabelFormatMode { Decimals = 0, SignificantFigures = 1 };
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
        SigFigs6  = 8
    };
    Q_ENUM(AxisNumberFormat)


    enum class Mode {
        Select = 0,    ///< Default — middle-button pan + wheel zoom only.
        Pan,           ///< Left-button drag pans.
        EditPoints,    ///< Left-button drag on a handle moves a station.
        InsertVertex,  ///< Left-button click inserts a station at click point.
        DeleteVertex,  ///< Left-button click on a handle removes it.
    };
    Q_ENUM(Mode)

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

    explicit TransectChartView(QWidget *parent = nullptr);
    ~TransectChartView() override;

    void setProvider(openswmmvis::transect::TransectProvider *p);
    openswmmvis::transect::TransectProvider *provider() const noexcept;

    Mode mode() const noexcept { return m_mode; }
    void setMode(Mode m);

    void zoomToExtent();
    void resetZoom();

    // Style accessors — exposed so the right-click context menu's
    // QPropertyModel-backed editor can drive them.
    QColor overbankColor()    const noexcept { return m_overbankColor; }
    QColor channelColor()     const noexcept { return m_channelColor; }
    QColor groundFillColor()  const noexcept { return m_groundFillColor; }
    int    handleSize()       const noexcept { return m_handleSize; }
    bool   handlesVisible()   const noexcept { return m_handlesVisible; }

    void setOverbankColor(const QColor &c);
    void setChannelColor(const QColor &c);
    void setGroundFillColor(const QColor &c);
    void setHandleSize(int px);
    void setHandlesVisible(bool on);

    // Axis number format accessors (driven via the QPropertyModel editor).
    /*! Combined format per axis, derived from the mode + digit count that
     *  remain the internal representation. */
    AxisNumberFormat xAxisNumberFormat() const;
    AxisNumberFormat yAxisNumberFormat() const;

    LabelFormatMode xLabelFormatMode() const noexcept { return m_xLabelMode; }
    int             xLabelPrecision()  const noexcept { return m_xLabelPrecision; }
    QString         xLabelFormat()     const          { return m_xLabelFormatStr; }
    LabelFormatMode yLabelFormatMode() const noexcept { return m_yLabelMode; }
    int             yLabelPrecision()  const noexcept { return m_yLabelPrecision; }
    QString         yLabelFormat()     const          { return m_yLabelFormatStr; }
    void setXAxisNumberFormat(TransectChartView::AxisNumberFormat f);
    void setYAxisNumberFormat(TransectChartView::AxisNumberFormat f);

    void setXLabelFormatMode(LabelFormatMode m);
    void setXLabelPrecision(int n);
    void setXLabelFormat(const QString &spec);
    void setYLabelFormatMode(LabelFormatMode m);
    void setYLabelPrecision(int n);
    void setYLabelFormat(const QString &spec);

    /*! \brief Highlight the given station indices on the chart. Pass an
     *  empty vector to clear. Implemented as a second scatter overlay so
     *  the base handle series stays one-colour. */
    void setSelectedIndices(const QVector<int> &indices);
    QVector<int> selectedIndices() const noexcept { return m_selectedIndices; }

    // Test hooks
    QLineSeries    *leftOverbankLine()  const noexcept { return m_leftOverbankLine; }
    QLineSeries    *rightOverbankLine() const noexcept { return m_rightOverbankLine; }
    /*! \brief Backward-compat alias for tests — returns the LEFT overbank. */
    QLineSeries    *overbankLine() const noexcept { return m_leftOverbankLine; }
    QLineSeries    *channelLine()  const noexcept { return m_channelLine; }
    QScatterSeries *handles()      const noexcept { return m_handles; }
    QAreaSeries    *groundFill()   const noexcept { return m_groundFill; }
    QChart         *chart()        const noexcept;

signals:
    void modeChanged(Mode m);
    void contextMenuRequestedAt(const QPoint &globalPos);

    void overbankColorChanged(const QColor &);
    void channelColorChanged(const QColor &);
    void groundFillColorChanged(const QColor &);
    void handleSizeChanged(int);
    void handlesVisibleChanged(bool);

    void xLabelFormatModeChanged(TransectChartView::LabelFormatMode);
    void xLabelPrecisionChanged(int);
    void xLabelFormatChanged(const QString &);
    void yLabelFormatModeChanged(TransectChartView::LabelFormatMode);
    void yLabelPrecisionChanged(int);
    void yLabelFormatChanged(const QString &);

    /*! \brief Emitted on left-button release after an EditPoints drag.
     *  Carries the original (pre-drag) and final (post-drag) station/
     *  elevation. The dialog pushes a MoveStationPointCommand from this
     *  signal so a single Cmd-Z reverts the drag. */
    void stationDragFinished(int index,
                              double oldStation, double oldElevation,
                              double newStation, double newElevation);

    /*! \brief Left-click on a handle in Select/EditPoints mode. The dialog
     *  uses this to drive the table's selection model (multi-select via
     *  Shift/Ctrl). idx == -1 means click on empty chart space (deselect). */
    void handleClicked(int index, Qt::KeyboardModifiers mods);

    /*! \brief Left-click on empty space in InsertVertex mode (or Shift+click
     *  on empty space in EditPoints mode). station/elevation are in the
     *  provider's raw units (already inverse-transformed from display). */
    void insertVertexRequested(double station, double elevation);

    /*! \brief Left-click on a handle in DeleteVertex mode. */
    void deleteVertexRequested(int index);

protected:
    void mousePressEvent(QMouseEvent *e)   override;
    void mouseMoveEvent(QMouseEvent *e)    override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e)        override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e)       override;

private slots:
    void onPointsChanged_();
    void onBankStationsChanged_();
    void onModifiersChanged_();
    void onEncroachmentStationsChanged_();

private:
    void rebuildSeriesFromProvider_();
    void rebuildBankOverlays_();
    /*! \brief Push the cached X/Y label formats onto the value axes. */
    void applyAxisLabelFormats_();
    /*! \brief Hit-test viewport-px against the handle scatter; returns the
     *  matching station index or -1. */
    int  hitTestHandle_(const QPoint &viewportPx) const;
    void applyModeCursor_();
    void zoomAroundViewportPoint_(const QPoint &viewportPx, qreal factor);

    QPointer<openswmmvis::transect::TransectProvider> m_provider;
    Mode m_mode = Mode::Select;

    QChart         *m_chart             = nullptr;
    // Earth fill split per-zone — left overbank / channel / right overbank.
    // m_groundFill keeps its name (and Q_PROPERTY `groundFillColor` keeps its
    // semantics) but now paints only the CHANNEL zone; the two overbank
    // shoulders are tinted with a derived `overbankColor` wash.
    QAreaSeries    *m_groundFill        = nullptr;   ///< channel zone fill
    QLineSeries    *m_groundFillUpper   = nullptr;
    QLineSeries    *m_groundFillLower   = nullptr;
    QAreaSeries    *m_leftOverbankFill  = nullptr;
    QLineSeries    *m_leftOverbankFillUpper = nullptr;
    QLineSeries    *m_leftOverbankFillLower = nullptr;
    QAreaSeries    *m_rightOverbankFill = nullptr;
    QLineSeries    *m_rightOverbankFillUpper = nullptr;
    QLineSeries    *m_rightOverbankFillLower = nullptr;
    QLineSeries    *m_leftOverbankLine  = nullptr;
    QLineSeries    *m_rightOverbankLine = nullptr;
    QLineSeries    *m_channelLine       = nullptr;
    QScatterSeries *m_handles           = nullptr;
    /*! \brief Selection overlay — repaints the selected handles in a
     *  contrasting colour without disturbing the base m_handles series. */
    QScatterSeries *m_selectedOverlay   = nullptr;
    QVector<int>    m_selectedIndices;
    QLineSeries    *m_leftBankMark      = nullptr;
    QLineSeries    *m_rightBankMark     = nullptr;
    // Horizontal bankfull lines: at the elevation of each bank station,
    // extended across the corresponding overbank shoulder — HEC-RAS
    // convention for visualising bankfull water surface.
    QLineSeries    *m_leftBankfullLine  = nullptr;
    QLineSeries    *m_rightBankfullLine = nullptr;
    // Encroachment-station markers (dashed). Hidden when value == 0.
    QLineSeries    *m_leftEncMark       = nullptr;
    QLineSeries    *m_rightEncMark      = nullptr;
    QValueAxis     *m_xAxis             = nullptr;
    QValueAxis     *m_yAxis             = nullptr;

    // Drag state.
    int     m_dragIndex      = -1;
    bool    m_dragging       = false;
    bool    m_middlePanning  = false;
    QPoint  m_lastPos;
    double  m_dragStartStation = 0.0;
    double  m_dragStartElev    = 0.0;

    // Pan state for left-button Pan mode.
    bool    m_leftPanning = false;

    // Style.
    QColor m_overbankColor   = QColor(0xFF, 0x40, 0x40);   // legacy clr 16512
    QColor m_channelColor    = QColor(0x00, 0x00, 0xC0);   // clBlue
    QColor m_groundFillColor = QColor(0xCC, 0xA0, 0x60, 0x70);
    int    m_handleSize      = 6;     // smaller default since markers are
                                       // now always on (was 9 = drag handle).
    bool   m_handlesVisible  = true;  // points are visible by default;
                                       // edit-mode adds drag affordance only.

    // Axis number format — seeded from the global Preferences default in the
    // constructor; per-chart edits override (cached because a printf format
    // string can't be reliably parsed back to mode+count).
    LabelFormatMode m_xLabelMode      = Decimals;
    int             m_xLabelPrecision = 0;
    QString         m_xLabelFormatStr;
    LabelFormatMode m_yLabelMode      = Decimals;
    int             m_yLabelPrecision = 2;
    QString         m_yLabelFormatStr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_TRANSECTCHARTVIEW_H
