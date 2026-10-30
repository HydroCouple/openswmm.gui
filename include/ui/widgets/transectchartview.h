/**
 * @file      transectchartview.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.4 — interactive cross-section preview chart.
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
 *   - Zoom-to-extent — fits both axes to the provider's bounding box with 5% axis padding and an earth-fill floor
 *   - Reset zoom — calls QChart::zoomReset
 *
 * Right-click emits `contextMenuRequestedAt(globalPos)` so the parent
 * dialog can build a menu (Chart properties… / Zoom to extent / Reset
 * zoom / Copy data… / Export PNG…).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace openswmmvis::transect
{
class TransectProvider;
}

namespace openswmmvis::ui
{

class TransectChartView : public QChartView
{
    Q_OBJECT

    // Q_PROPERTY surface so the right-click context-menu's QPropertyModel-backed
    // editor can drive the chart styling directly.
    Q_PROPERTY(
        QColor overbankColor READ overbankColor WRITE setOverbankColor NOTIFY overbankColorChanged)
    Q_PROPERTY(
        QColor channelColor READ channelColor WRITE setChannelColor NOTIFY channelColorChanged)
    Q_PROPERTY(QColor groundFillColor READ groundFillColor WRITE setGroundFillColor NOTIFY
                   groundFillColorChanged)
    Q_PROPERTY(int handleSize READ handleSize WRITE setHandleSize NOTIFY handleSizeChanged)
    Q_PROPERTY(bool handlesVisible READ handlesVisible WRITE setHandlesVisible NOTIFY
                   handlesVisibleChanged)

    // Axis number format — same surface as plot::ChartProperties: one combined
    // dropdown per axis, plus an optional printf override.
    Q_PROPERTY(TransectChartView::AxisNumberFormat xAxisNumberFormat READ xAxisNumberFormat WRITE
                   setXAxisNumberFormat NOTIFY xLabelPrecisionChanged)
    Q_PROPERTY(
        QString xLabelFormat READ xLabelFormat WRITE setXLabelFormat NOTIFY xLabelFormatChanged)
    Q_PROPERTY(TransectChartView::AxisNumberFormat yAxisNumberFormat READ yAxisNumberFormat WRITE
                   setYAxisNumberFormat NOTIFY yLabelPrecisionChanged)
    Q_PROPERTY(
        QString yLabelFormat READ yLabelFormat WRITE setYLabelFormat NOTIFY yLabelFormatChanged)

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
        Integer = 0,
        Decimals1 = 1,
        Decimals2 = 2,
        Decimals3 = 3,
        Decimals4 = 4,
        Decimals6 = 5,
        SigFigs3 = 6,
        SigFigs4 = 7,
        SigFigs6 = 8
    };
    Q_ENUM(AxisNumberFormat)

    enum class Mode {
        Select = 0,   ///< Default — middle-button pan + wheel zoom only.
        Pan,          ///< Left-button drag pans.
        EditPoints,   ///< Left-button drag on a handle moves a station.
        InsertVertex, ///< Left-button click inserts a station at click point.
        DeleteVertex, ///< Left-button click on a handle removes it.
    };
    Q_ENUM(Mode)

    /**
     * @brief Supply translated property-grid captions for the chart's exposed style/format properties.
     * @param[in] property Exact property name.
     * @return Translated caption for a supported property or empty when unknown.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

    /**
     * @brief Create an owned cross-section chart with zone fills, station handles and bank/encroachment overlays.
     * @param[in] parent Optional QWidget owner. Number formats are seeded from global plot preferences; provider is initially absent.
     */
    explicit TransectChartView(QWidget *parent = nullptr);
    /**
     * @brief Destroy the chart view and chart-owned series/axes; borrowed provider is not deleted.
     */
    ~TransectChartView() override;

    /**
     * @brief Replace guarded provider subscriptions and rebuild all plotted data/axis ranges.
     * @param[in] p Borrowed transect tracked by QPointer; null clears data. Identical pointers are ignored. Do not rebind during a live drag.
     */
    void setProvider(openswmmvis::transect::TransectProvider *p);
    /**
     * @brief Access the currently observed transect.
     * @return Borrowed live provider, or null.
     */
    openswmmvis::transect::TransectProvider *provider() const noexcept;

    /**
     * @brief Inspect the active left-button interaction mode.
     * @return Stored mode, initially Select.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Change left-button interaction/cursor and emit modeChanged() only when different.
     * @param[in] m Supported navigation/edit mode. Does not cancel a current gesture or change handle visibility.
     */
    void setMode(Mode m);

    /**
     * @brief Rebuild all series, refitting current transformed provider data with axis padding and the earth-fill floor.
     */
    void zoomToExtent();
    /**
     * @brief Request QChart::zoomReset() when the chart exists, without changing provider data.
     */
    void resetZoom();

    // Style accessors — exposed so the right-click context menu's
    // QPropertyModel-backed editor can drive them.
    /**
     * @brief Read the cached overbank line/handle colour.
     * @return Current stored preview/style value.
     */
    QColor overbankColor() const noexcept { return m_overbankColor; }
    /**
     * @brief Read the cached channel line/bank-marker colour.
     * @return Current stored preview/style value.
     */
    QColor channelColor() const noexcept { return m_channelColor; }
    /**
     * @brief Read the cached channel earth-fill colour.
     * @return Current stored preview/style value.
     */
    QColor groundFillColor() const noexcept { return m_groundFillColor; }
    /**
     * @brief Read the cached base handle diameter in logical pixels.
     * @return Current stored preview/style value.
     */
    int handleSize() const noexcept { return m_handleSize; }
    /**
     * @brief Read the cached base handle visibility flag.
     * @return Current stored preview/style value.
     */
    bool handlesVisible() const noexcept { return m_handlesVisible; }

    /**
     * @brief Change the overbank line/handle colour and emit its signal only when the QColor differs.
     * @param[in] c Colour including alpha; no validity check occurs. Only the corresponding series are updated immediately.
     */
    void setOverbankColor(const QColor &c);
    /**
     * @brief Change the channel line/bank-marker colour and emit its signal only when the QColor differs.
     * @param[in] c Colour including alpha; no validity check occurs. Only the corresponding series are updated immediately.
     */
    void setChannelColor(const QColor &c);
    /**
     * @brief Change the channel earth-fill colour and emit its signal only when the QColor differs.
     * @param[in] c Colour including alpha; no validity check occurs. Only the corresponding series are updated immediately.
     */
    void setGroundFillColor(const QColor &c);
    /**
     * @brief Change base handle diameter under a 4–32 pixel clamp.
     * @param[in] px Requested logical-pixel size; changed accepted values update m_handles and emit handleSizeChanged(). The selected overlay has its own marker size.
     */
    void setHandleSize(int px);
    /**
     * @brief Toggle base station handles independently of interaction mode.
     * @param[in] on Visibility; changed values emit handlesVisibleChanged(). Selected overlay visibility is separate.
     */
    void setHandlesVisible(bool on);

    // Axis number format accessors (driven via the QPropertyModel editor).
    /*! Combined format per axis, derived from the mode + digit count that
     *  remain the internal representation.
     * @brief Map the cached X axis mode/digits to a combined display preset.
     * @return presetForNumberFormat() selection; custom printf text is not parsed into the preset.
     */
    AxisNumberFormat xAxisNumberFormat() const;
    /**
     * @brief Map the cached Y axis mode/digits to a combined display preset.
     * @return presetForNumberFormat() selection; custom printf text is not parsed into the preset.
     */
    AxisNumberFormat yAxisNumberFormat() const;

    /**
     * @brief Read the X axis number-format state.
     * @return Cached mode value used by NumberFormat.
     */
    LabelFormatMode xLabelFormatMode() const noexcept { return m_xLabelMode; }
    /**
     * @brief Read the X axis number-format state.
     * @return Cached digit count; explicit setters clamp to 0–10.
     */
    int xLabelPrecision() const noexcept { return m_xLabelPrecision; }
    /**
     * @brief Read the X axis number-format state.
     * @return Stored custom printf override, including empty for automatic formatting.
     */
    QString xLabelFormat() const { return m_xLabelFormatStr; }
    /**
     * @brief Read the Y axis number-format state.
     * @return Cached mode value used by NumberFormat.
     */
    LabelFormatMode yLabelFormatMode() const noexcept { return m_yLabelMode; }
    /**
     * @brief Read the Y axis number-format state.
     * @return Cached digit count; explicit setters clamp to 0–10.
     */
    int yLabelPrecision() const noexcept { return m_yLabelPrecision; }
    /**
     * @brief Read the Y axis number-format state.
     * @return Stored custom printf override, including empty for automatic formatting.
     */
    QString yLabelFormat() const { return m_yLabelFormatStr; }
    /**
     * @brief Set cached X mode/digits through the shared preset mapping.
     * @param[in] f Combined preset selector passed to numberFormatForPreset(). Individual setters apply labels and emit their change signals; custom format text is preserved.
     */
    void setXAxisNumberFormat(TransectChartView::AxisNumberFormat f);
    /**
     * @brief Set cached Y mode/digits through the shared preset mapping.
     * @param[in] f Combined preset selector passed to numberFormatForPreset(). Individual setters apply labels and emit their change signals; custom format text is preserved.
     */
    void setYAxisNumberFormat(TransectChartView::AxisNumberFormat f);

    /**
     * @brief Update X axis formatting and its corresponding change signal when the accepted value differs.
     * @param[in] m Requested number mode, passed through without local enum validation.
     */
    void setXLabelFormatMode(LabelFormatMode m);
    /**
     * @brief Update X axis formatting and its corresponding change signal when the accepted value differs.
     * @param[in] n Requested digit count clamped to 0–10.
     */
    void setXLabelPrecision(int n);
    /**
     * @brief Update X axis formatting and its corresponding change signal when the accepted value differs.
     * @param[in] spec Custom printf override stored verbatim; empty restores the generated numeric format.
     */
    void setXLabelFormat(const QString &spec);
    /**
     * @brief Update Y axis formatting and its corresponding change signal when the accepted value differs.
     * @param[in] m Requested number mode, passed through without local enum validation.
     */
    void setYLabelFormatMode(LabelFormatMode m);
    /**
     * @brief Update Y axis formatting and its corresponding change signal when the accepted value differs.
     * @param[in] n Requested digit count clamped to 0–10.
     */
    void setYLabelPrecision(int n);
    /**
     * @brief Update Y axis formatting and its corresponding change signal when the accepted value differs.
     * @param[in] spec Custom printf override stored verbatim; empty restores the generated numeric format.
     */
    void setYLabelFormat(const QString &spec);

    /*! \brief Highlight the given station indices on the chart. Pass an
     *  empty vector to clear. Implemented as a second scatter overlay so
     *  the base handle series stays one-colour.
     * @param[in] indices Zero-based provider indexes stored verbatim, without sorting/deduplication. Invalid entries are omitted from drawing; a later full rebuild removes invalid stored entries.
     * @note No selection-change signal is emitted by this setter.
     */
    void setSelectedIndices(const QVector<int> &indices);
    /**
     * @brief Read the stored selection index list.
     * @return Copy in supplied order, possibly containing duplicate/invalid entries before a full series rebuild.
     */
    QVector<int> selectedIndices() const noexcept { return m_selectedIndices; }

    // Test hooks
    /**
     * @brief Expose the left shoulder line for integration/introspection.
     * @return Borrowed chart-owned series; do not remove or delete it independently while this view uses it.
     */
    QLineSeries *leftOverbankLine() const noexcept { return m_leftOverbankLine; }
    /**
     * @brief Expose the right shoulder line for integration/introspection.
     * @return Borrowed chart-owned series; do not remove or delete it independently while this view uses it.
     */
    QLineSeries *rightOverbankLine() const noexcept { return m_rightOverbankLine; }
    /*! \brief Backward-compat alias for tests — returns the LEFT overbank. */
    QLineSeries *overbankLine() const noexcept { return m_leftOverbankLine; }
    /**
     * @brief Expose the channel line for integration/introspection.
     * @return Borrowed chart-owned series; do not remove or delete it independently while this view uses it.
     */
    QLineSeries *channelLine() const noexcept { return m_channelLine; }
    /**
     * @brief Expose the base handle scatter for integration/introspection.
     * @return Borrowed chart-owned series; do not remove or delete it independently while this view uses it.
     */
    QScatterSeries *handles() const noexcept { return m_handles; }
    /**
     * @brief Expose the channel earth-fill area for integration/introspection.
     * @return Borrowed chart-owned series; do not remove or delete it independently while this view uses it.
     */
    QAreaSeries *groundFill() const noexcept { return m_groundFill; }
    /**
     * @brief Access the chart constructed and installed by this view.
     * @return Borrowed view-owned chart pointer.
     */
    QChart *chart() const noexcept;

signals:
    /**
     * @brief Announce a changed left-button interaction mode.
     * @param[in] m Current mode.
     */
    void modeChanged(Mode m);
    /**
     * @brief Delegate context-menu construction to the host dialog.
     * @param[in] globalPos Global pointer position from the accepted context event.
     */
    void contextMenuRequestedAt(const QPoint &globalPos);

    /**
     * @brief Announce changed cached overbank line/handle colour.
     * @param[in] color Current accepted value.
     */
    void overbankColorChanged(const QColor &color);
    /**
     * @brief Announce changed cached channel line/bank-marker colour.
     * @param[in] color Current accepted value.
     */
    void channelColorChanged(const QColor &color);
    /**
     * @brief Announce changed cached channel earth-fill colour.
     * @param[in] color Current accepted value.
     */
    void groundFillColorChanged(const QColor &color);
    /**
     * @brief Announce changed cached base handle diameter in logical pixels.
     * @param[in] px Current accepted value.
     */
    void handleSizeChanged(int px);
    /**
     * @brief Announce changed cached base handle visibility flag.
     * @param[in] on Current accepted value.
     */
    void handlesVisibleChanged(bool on);

    /**
     * @brief Announce changed cached X axis formatting.
     * @param[in] mode Newly stored mode/digit count/format value for this signal.
     */
    void xLabelFormatModeChanged(TransectChartView::LabelFormatMode mode);
    /**
     * @brief Announce changed cached X axis formatting.
     * @param[in] count Newly stored mode/digit count/format value for this signal.
     */
    void xLabelPrecisionChanged(int count);
    /**
     * @brief Announce changed cached X axis formatting.
     * @param[in] spec Newly stored mode/digit count/format value for this signal.
     */
    void xLabelFormatChanged(const QString &spec);
    /**
     * @brief Announce changed cached Y axis formatting.
     * @param[in] mode Newly stored mode/digit count/format value for this signal.
     */
    void yLabelFormatModeChanged(TransectChartView::LabelFormatMode mode);
    /**
     * @brief Announce changed cached Y axis formatting.
     * @param[in] count Newly stored mode/digit count/format value for this signal.
     */
    void yLabelPrecisionChanged(int count);
    /**
     * @brief Announce changed cached Y axis formatting.
     * @param[in] spec Newly stored mode/digit count/format value for this signal.
     */
    void yLabelFormatChanged(const QString &spec);

    /*! \brief Emitted on left-button release after an EditPoints drag.
     *  Carries the original (pre-drag) and final (post-drag) station/
     *  elevation. The dialog pushes a MoveStationPointCommand from this
     *  signal so a single Cmd-Z reverts the drag.
     * @param[in] index Zero-based moved station.
     * @param[in] oldStation Pre-drag raw station coordinate.
     * @param[in] oldElevation Pre-drag raw elevation.
     * @param[in] newStation Final accepted raw station coordinate.
     * @param[in] newElevation Final accepted raw elevation.
     * @note Emitted only for a net change after reverting the live provider to old coordinates. The host must apply/push a command to retain the new state.
     */
    void stationDragFinished(int index, double oldStation, double oldElevation, double newStation,
                             double newElevation);

    /*! \brief Left-click on a handle in Select/EditPoints mode. The dialog
     *  uses this to drive the table's selection model (multi-select via
     *  Shift/Ctrl). idx == -1 means click on empty chart space (deselect).
     * @param[in] index Picked station index or -1 for empty Select-mode space.
     * @param[in] mods Keyboard modifiers allowing the host to update table selection semantics.
     */
    void handleClicked(int index, Qt::KeyboardModifiers mods);

    /*! \brief Left-click on empty space in InsertVertex mode (or Shift+click
     *  on empty space in EditPoints mode). station/elevation are in the
     *  provider's raw units (already inverse-transformed from display).
     * @param[in] station Raw station after dividing display X by stationMultiplier (zero multiplier falls back to one).
     * @param[in] elevation Raw elevation after subtracting elevationOffset. The host validates/applies the insertion.
     */
    void insertVertexRequested(double station, double elevation);

    /*! \brief Left-click on a handle in DeleteVertex mode.
     * @param[in] index Requested zero-based provider row; emitted from delete mode or selected-index Delete/Backspace. Host applies/batches removal.
     */
    void deleteVertexRequested(int index);

protected:
    /**
     * @brief Start middle/left pan, station dragging or emit host selection/insert/delete requests for the current mode.
     * @param[in] e Required Qt mouse event. Insert coordinates are inverse-transformed to raw provider units; Shift-empty click also requests insertion in EditPoints.
     */
    void mousePressEvent(QMouseEvent *e) override;
    /**
     * @brief Pan live chart ranges or preview a station move through monotonicity-clamping setPointLive().
     * @param[in] e Required Qt move event; Shift preserves station and Control preserves elevation, using the current point as the locked coordinate.
     */
    void mouseMoveEvent(QMouseEvent *e) override;
    /**
     * @brief Finish pan, or rewind a changed station drag then emit old/new raw coordinates for host undo handling.
     * @param[in] e Required Qt release event. Without a host stationDragFinished connection, changed preview coordinates remain reverted.
     */
    void mouseReleaseEvent(QMouseEvent *e) override;
    /**
     * @brief Zoom about the cursor by 1.2 for positive vertical delta, otherwise by 1/1.2.
     * @param[in] e Required Qt wheel event; even zero vertical delta uses the zoom-out branch when the chart exists.
     */
    void wheelEvent(QWheelEvent *e) override;
    /**
     * @brief Emit the global context-menu request and accept the event.
     * @param[in] e Required Qt context event.
     */
    void contextMenuEvent(QContextMenuEvent *e) override;
    /**
     * @brief Request deletion of stored selected indexes in descending order for Delete/Backspace.
     * @param[in] e Required Qt key event; other keys forward to QChartView. Duplicate stored selections are not deduplicated before emission.
     */
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    /**
     * @brief Rebuild all provider data and refit axes after station insertion/removal/changes.
     */
    void onPointsChanged_();
    /**
     * @brief Recut all three ground zones and refit axes because bank stations define their boundaries.
     */
    void onBankStationsChanged_();
    /**
     * @brief Rebuild transformed coordinates and overlays after station-multiplier/elevation-offset changes.
     */
    void onModifiersChanged_();
    /**
     * @brief Refresh only bank/bankfull/encroachment marker overlays, preserving the current plot ranges.
     */
    void onEncroachmentStationsChanged_();

private:
    /**
     * @brief Transform raw points, split overbank/channel zones at interpolated bank crossings, rebuild fills/handles and refit axes.
     * @note Display X is station times stationMultiplier; Y is elevation plus elevationOffset. Earth floor is 10% of max(0.5,elevation span) below minimum; axis padding is 5%. Invalid selection indexes are discarded. Missing provider clears series and uses 0–1 axes.
     */
    void rebuildSeriesFromProvider_();
    /**
     * @brief Rebuild vertical bank/encroachment markers and interpolated horizontal bankfull shoulders in display coordinates.
     * @note Raw encroachment station zero hides that marker. Missing provider clears overlays; bankfull shoulders require ordered distinct banks and at least two points.
     */
    void rebuildBankOverlays_();
    /*! \brief Push the cached X/Y label formats onto the value axes.
     * @note Converts cached axis mode/count/custom text through NumberFormat::printfSpec(); missing axes are ignored.
     */
    void applyAxisLabelFormats_();
    /*! \brief Hit-test viewport-px against the handle scatter; returns the
     *  matching station index or -1.
     * @param[in] viewportPx Pointer coordinate mapped to scene.
     * @return Closest provider point strictly within eight logical pixels in display coordinates, or -1. Equal distances preserve the earlier index; handle visibility is not tested.
     */
    int hitTestHandle_(const QPoint &viewportPx) const;
    /**
     * @brief Set arrow/open-hand/crosshair/forbidden cursor for the current left-button mode.
     */
    void applyModeCursor_();
    /**
     * @brief Scroll/zoom/scroll around a pointer anchor rather than changing provider coordinates.
     * @param[in] viewportPx Viewport logical-pixel anchor.
     * @param[in] factor Positive finite chart zoom scalar supplied by wheel handling. Missing chart is ignored.
     */
    void zoomAroundViewportPoint_(const QPoint &viewportPx, qreal factor);

    QPointer<openswmmvis::transect::TransectProvider> m_provider;
    Mode m_mode = Mode::Select;

    QChart *m_chart = nullptr;
    // Earth fill split per-zone — left overbank / channel / right overbank.
    // m_groundFill keeps its name (and Q_PROPERTY `groundFillColor` keeps its
    // semantics) but now paints only the CHANNEL zone; the two overbank
    // shoulders are tinted with a derived `overbankColor` wash.
    QAreaSeries *m_groundFill = nullptr; ///< channel zone fill
    QLineSeries *m_groundFillUpper = nullptr;
    QLineSeries *m_groundFillLower = nullptr;
    QAreaSeries *m_leftOverbankFill = nullptr;
    QLineSeries *m_leftOverbankFillUpper = nullptr;
    QLineSeries *m_leftOverbankFillLower = nullptr;
    QAreaSeries *m_rightOverbankFill = nullptr;
    QLineSeries *m_rightOverbankFillUpper = nullptr;
    QLineSeries *m_rightOverbankFillLower = nullptr;
    QLineSeries *m_leftOverbankLine = nullptr;
    QLineSeries *m_rightOverbankLine = nullptr;
    QLineSeries *m_channelLine = nullptr;
    QScatterSeries *m_handles = nullptr;
    /*! \brief Selection overlay — repaints the selected handles in a
     *  contrasting colour without disturbing the base m_handles series. */
    QScatterSeries *m_selectedOverlay = nullptr;
    QVector<int> m_selectedIndices;
    QLineSeries *m_leftBankMark = nullptr;
    QLineSeries *m_rightBankMark = nullptr;
    // Horizontal bankfull lines: at the elevation of each bank station,
    // extended across the corresponding overbank shoulder — HEC-RAS
    // convention for visualising bankfull water surface.
    QLineSeries *m_leftBankfullLine = nullptr;
    QLineSeries *m_rightBankfullLine = nullptr;
    // Encroachment-station markers (dashed). Hidden when value == 0.
    QLineSeries *m_leftEncMark = nullptr;
    QLineSeries *m_rightEncMark = nullptr;
    QValueAxis *m_xAxis = nullptr;
    QValueAxis *m_yAxis = nullptr;

    // Drag state.
    int m_dragIndex = -1;
    bool m_dragging = false;
    bool m_middlePanning = false;
    QPoint m_lastPos;
    double m_dragStartStation = 0.0;
    double m_dragStartElev = 0.0;

    // Pan state for left-button Pan mode.
    bool m_leftPanning = false;

    // Style.
    QColor m_overbankColor = QColor(0xFF, 0x40, 0x40); // legacy clr 16512
    QColor m_channelColor = QColor(0x00, 0x00, 0xC0);  // clBlue
    QColor m_groundFillColor = QColor(0xCC, 0xA0, 0x60, 0x70);
    int m_handleSize = 6;         // smaller default since markers are
                                  // now always on (was 9 = drag handle).
    bool m_handlesVisible = true; // points are visible by default;
                                  // edit-mode adds drag affordance only.

    // Axis number format — seeded from the global Preferences default in the
    // constructor; per-chart edits override (cached because a printf format
    // string can't be reliably parsed back to mode+count).
    LabelFormatMode m_xLabelMode = Decimals;
    int m_xLabelPrecision = 0;
    QString m_xLabelFormatStr;
    LabelFormatMode m_yLabelMode = Decimals;
    int m_yLabelPrecision = 2;
    QString m_yLabelFormatStr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_TRANSECTCHARTVIEW_H
