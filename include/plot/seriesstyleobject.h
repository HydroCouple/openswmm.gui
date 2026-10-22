/**
 * @file      seriesstyleobject.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QObject wrapper around `SeriesStyle` exposing every visual
 *         attribute as a `Q_PROPERTY` so a `QPropertyModel` can drive
 *         per-series editing in a tree view.
 *
 * Mirrors the design of `ChartProperties` for chart-level editing.
 * Each style-field setter updates the underlying `SeriesStyle` cache and
 * emits a typed Notify signal (for QPropertyModel to repaint the row)
 * and the aggregate `styleChanged(SeriesStyle)` signal on a changed value, so host dialogs
 * can live-update their chart in one place.
 *
 * Property groupings (rendered via `displayLabelFor`):
 *   - Identity:   color, opacity, legendName, legendOverride
 *   - Line:       showLine, lineWidth, dash, capStyle, joinStyle
 *   - Markers:    showMarkers, shape, markerSize,
 *                 markerFillColor, markerBorderColor, markerBorderWidth
 *   - Labels:     showPointLabels, pointLabelFont, pointLabelColor,
 *                 pointLabelPrecision
 *   - Area:       showAreaFill, areaFillColor
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_SERIESSTYLEOBJECT_H
#define OPENSWMMVIS_PLOT_SERIESSTYLEOBJECT_H

#include "plot/seriesstyle.h"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>

namespace openswmmvis::plot
{

class SeriesStyleObject : public QObject
{
    Q_OBJECT

    // ---- Identity ----------------------------------------------------------
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)
    Q_PROPERTY(QString legendName READ legendName WRITE setLegendName NOTIFY legendNameChanged)
    Q_PROPERTY(QString legendOverride READ legendOverride WRITE setLegendOverride NOTIFY
                   legendOverrideChanged)

    // ---- Line --------------------------------------------------------------
    Q_PROPERTY(bool showLine READ showLine WRITE setShowLine NOTIFY showLineChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(Qt::PenStyle dash READ dash WRITE setDash NOTIFY dashChanged)
    Q_PROPERTY(Qt::PenCapStyle capStyle READ capStyle WRITE setCapStyle NOTIFY capStyleChanged)
    Q_PROPERTY(Qt::PenJoinStyle joinStyle READ joinStyle WRITE setJoinStyle NOTIFY joinStyleChanged)

    // ---- Markers -----------------------------------------------------------
    Q_PROPERTY(bool showMarkers READ showMarkers WRITE setShowMarkers NOTIFY showMarkersChanged)
    Q_PROPERTY(MarkerShapeQ shape READ shape WRITE setShape NOTIFY shapeChanged)
    Q_PROPERTY(qreal markerSize READ markerSize WRITE setMarkerSize NOTIFY markerSizeChanged)
    Q_PROPERTY(QColor markerFillColor READ markerFillColor WRITE setMarkerFillColor NOTIFY
                   markerFillColorChanged)
    Q_PROPERTY(QColor markerBorderColor READ markerBorderColor WRITE setMarkerBorderColor NOTIFY
                   markerBorderColorChanged)
    Q_PROPERTY(qreal markerBorderWidth READ markerBorderWidth WRITE setMarkerBorderWidth NOTIFY
                   markerBorderWidthChanged)

    // ---- Point labels ------------------------------------------------------
    Q_PROPERTY(bool showPointLabels READ showPointLabels WRITE setShowPointLabels NOTIFY
                   showPointLabelsChanged)
    Q_PROPERTY(QFont pointLabelFont READ pointLabelFont WRITE setPointLabelFont NOTIFY
                   pointLabelFontChanged)
    Q_PROPERTY(QColor pointLabelColor READ pointLabelColor WRITE setPointLabelColor NOTIFY
                   pointLabelColorChanged)
    Q_PROPERTY(LabelFormatModeQ pointLabelFormatMode READ pointLabelFormatMode WRITE
                   setPointLabelFormatMode NOTIFY pointLabelFormatModeChanged)
    Q_PROPERTY(int pointLabelPrecision READ pointLabelPrecision WRITE setPointLabelPrecision NOTIFY
                   pointLabelPrecisionChanged)
    Q_PROPERTY(QString pointLabelFormat READ pointLabelFormat WRITE setPointLabelFormat NOTIFY
                   pointLabelFormatChanged)

    // ---- Area fill ---------------------------------------------------------
    Q_PROPERTY(bool showAreaFill READ showAreaFill WRITE setShowAreaFill NOTIFY showAreaFillChanged)
    Q_PROPERTY(
        QColor areaFillColor READ areaFillColor WRITE setAreaFillColor NOTIFY areaFillColorChanged)

public:
    /*! \brief A Q_ENUM-registered mirror of `MarkerShape` so QPropertyModel
     *  can render a combo-box for it. Q_ENUM requires the enum to live in
     *  a QObject, hence this duplicate. Values are kept in sync 1:1. */
    enum class MarkerShapeQ : int {
        Circle = static_cast<int>(MarkerShape::Circle),
        Square = static_cast<int>(MarkerShape::Square),
        Triangle = static_cast<int>(MarkerShape::Triangle),
        Diamond = static_cast<int>(MarkerShape::Diamond),
        Cross = static_cast<int>(MarkerShape::Cross),
        Plus = static_cast<int>(MarkerShape::Plus),
    };
    Q_ENUM(MarkerShapeQ)

    /*! \brief Q_ENUM mirror of `NumberFormatMode` so QPropertyModel can
     *  render a combo-box for the point-label number format. Values track
     *  `NumberFormatMode` 1:1. */
    enum class LabelFormatModeQ : int {
        Decimals = static_cast<int>(NumberFormatMode::Decimals),
        SignificantFigures = static_cast<int>(NumberFormatMode::SignificantFigures),
    };
    Q_ENUM(LabelFormatModeQ)

    /**
     * @brief Create a default style editor cache without attaching a chart.
     * @param[in] parent Optional QObject owner. No initial notifications are emitted.
     */
    explicit SeriesStyleObject(QObject *parent = nullptr);
    /**
     * @brief Copy a starting style without validation or notifications.
     * @param[in] initial Style cache copied verbatim.
     * @param[in] parent Optional QObject owner; no external style/chart lifetime is retained.
     */
    explicit SeriesStyleObject(const SeriesStyle &initial, QObject *parent = nullptr);
    /**
     * @brief Release the wrapper and QObject children; no chart/series objects are owned by this wrapper.
     */
    ~SeriesStyleObject() override = default;

    /*! \brief Snapshot of the current style.
     * @return Independent raw style snapshot, excluding the separate legendOverride field.
     */
    SeriesStyle style() const noexcept { return m_style; }

    /*! \brief Replace the style cache and emit one aggregate notification.
     * @param[in] s Replacement raw style, copied through typed setters without range validation. legendOverride remains unchanged.
     * @note All signals are blocked during the field updates, so no per-field NOTIFY signals escape. Signals are then unconditionally enabled and styleChanged() is emitted once even if the style did not differ. A previously blocked state is not restored.
     */
    void setStyle(const SeriesStyle &s);

    /*! \brief Spec-level legend override (mirrors `SeriesSpec::legendOverride`).
     *
     *  Distinct from the style-level `legendName` above:
     *   - `legendName`     lives in `SeriesStyle`  (per-style, JSON-persisted).
     *   - `legendOverride` lives in `SeriesSpec`   (per-series instance, used
     *                                              by the Comparison Plot
     *                                              dialog as the canonical
     *                                              "what to call this series").
     *
     *  Stored on this object so the QPropertyModel-backed editor can edit it
     *  alongside the style fields; not part of `m_style`, so it never enters
     *  `style()` and never fires `styleChanged()`. Hosts read it via
     *  `legendOverride()` and subscribe to `legendOverrideChanged()`.
     * @return Independent exact instance-level legend text, separate from style(). Empty leaves naming policy to the host.
     */
    QString legendOverride() const { return m_legendOverride; }

    /*! \brief Human-friendly, group-prefixed label for the QPropertyModel
     *  tree. Returns labels like "Line — Width", "Marker — Fill Colour".
     *  Returns an empty string for unknown property names so the default
     *  generated name is used. Mirrors `ChartProperties::displayLabelFor`.
     * @param[in] propertyName Exact meta-property identifier; comparisons are case-sensitive.
     * @return Group-prefixed display text or empty for the caller's default-label fallback.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    // ---- Getters -----------------------------------------------------------
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Main series colour; no validity enforcement. Returns the stored raw value, including any unset colour sentinel.
     */
    QColor color() const noexcept { return m_style.color; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Series opacity, expected [0,1]; copied without clamping. Returns the stored raw value, including any unset colour sentinel.
     */
    qreal opacity() const noexcept { return m_style.opacity; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Style-level legend text; empty lets rendering preserve its current name. Returns the stored raw value, including any unset colour sentinel.
     */
    QString legendName() const { return m_style.legendName; }

    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Whether line presentation is requested. Returns the stored raw value, including any unset colour sentinel.
     */
    bool showLine() const noexcept { return m_style.showLine; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Line width in logical pixels, expected nonnegative; not clamped. Returns the stored raw value, including any unset colour sentinel.
     */
    qreal lineWidth() const noexcept { return m_style.lineWidth; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Line dash enum; no enum-membership validation. Returns the stored raw value, including any unset colour sentinel.
     */
    Qt::PenStyle dash() const noexcept { return m_style.dash; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Line-end cap enum; no validation. Returns the stored raw value, including any unset colour sentinel.
     */
    Qt::PenCapStyle capStyle() const noexcept { return m_style.capStyle; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Line-join enum; no validation. Returns the stored raw value, including any unset colour sentinel.
     */
    Qt::PenJoinStyle joinStyle() const noexcept { return m_style.joinStyle; }

    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Whether point markers are requested. Returns the stored raw value, including any unset colour sentinel.
     */
    bool showMarkers() const noexcept { return m_style.showMarkers; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Marker enum, cast directly to storage without validation. Returns the stored raw value, including any unset colour sentinel.
     */
    MarkerShapeQ shape() const noexcept { return static_cast<MarkerShapeQ>(m_style.shape); }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Marker diameter in logical pixels, expected nonnegative finite; not clamped. Returns the stored raw value, including any unset colour sentinel.
     */
    qreal markerSize() const noexcept { return m_style.markerSize; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Marker fill; invalid means derive from the main colour. Returns the stored raw value, including any unset colour sentinel.
     */
    QColor markerFillColor() const noexcept { return m_style.markerFillColor; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Marker border; invalid means derive a darker main colour. Returns the stored raw value, including any unset colour sentinel.
     */
    QColor markerBorderColor() const noexcept { return m_style.markerBorderColor; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Marker border width in logical pixels; nonpositive suppresses the pixmap border. Returns the stored raw value, including any unset colour sentinel.
     */
    qreal markerBorderWidth() const noexcept { return m_style.markerBorderWidth; }

    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Whether sample labels are requested. Returns the stored raw value, including any unset colour sentinel.
     */
    bool showPointLabels() const noexcept { return m_style.showPointLabels; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Sample label font copied with its Qt font-size semantics. Returns the stored raw value, including any unset colour sentinel.
     */
    QFont pointLabelFont() const noexcept { return m_style.pointLabelFont; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Sample label colour; invalid derives a darker main colour. Returns the stored raw value, including any unset colour sentinel.
     */
    QColor pointLabelColor() const noexcept { return m_style.pointLabelColor; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Label format family, cast directly to NumberFormatMode without validation. Returns the stored raw value, including any unset colour sentinel.
     */
    LabelFormatModeQ pointLabelFormatMode() const noexcept
    {
        return static_cast<LabelFormatModeQ>(m_style.pointLabelFormatMode);
    }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Raw digits; no clamping here, with effective lower bounds applied by NumberFormat at rendering. Returns the stored raw value, including any unset colour sentinel.
     */
    int pointLabelPrecision() const noexcept { return m_style.pointLabelPrecision; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Optional printf override copied verbatim; NumberFormat later ignores invalid syntax. Returns the stored raw value, including any unset colour sentinel.
     */
    QString pointLabelFormat() const { return m_style.pointLabelFormat; }

    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Whether the host should render an area under the line. Returns the stored raw value, including any unset colour sentinel.
     */
    bool showAreaFill() const noexcept { return m_style.showAreaFill; }
    /**
     * @brief Inspect the editor cache without reading a live chart.
     * @return Area colour; invalid derives the main colour at alpha 0.25. Returns the stored raw value, including any unset colour sentinel.
     */
    QColor areaFillColor() const noexcept { return m_style.areaFillColor; }

public slots:
    /**
     * @brief Update the cached rendering preference for color.
     * @param[in] c Main series colour; no validity enforcement.
     * @note Equality is a no-op. A change emits colorChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setColor(const QColor &c);
    /**
     * @brief Update the cached rendering preference for opacity.
     * @param[in] v Series opacity, expected [0,1]; copied without clamping.
     * @note Equality is a no-op. A change emits opacityChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setOpacity(qreal v);
    /**
     * @brief Update the cached rendering preference for legendName.
     * @param[in] s Style-level legend text; empty lets rendering preserve its current name.
     * @note Equality is a no-op. A change emits legendNameChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setLegendName(const QString &s);
    /**
     * @brief Update the separate per-series instance legend override.
     * @param[in] s Exact text; unchanged text is a no-op and empty is allowed.
     * @note Emits only legendOverrideChanged(), never styleChanged(). The host must propagate this into its SeriesSpec.
     */
    void setLegendOverride(const QString &s);

    /**
     * @brief Update the cached rendering preference for showLine.
     * @param[in] on Whether line presentation is requested.
     * @note Equality is a no-op. A change emits showLineChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setShowLine(bool on);
    /**
     * @brief Update the cached rendering preference for lineWidth.
     * @param[in] v Line width in logical pixels, expected nonnegative; not clamped.
     * @note Equality is a no-op. A change emits lineWidthChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setLineWidth(qreal v);
    /**
     * @brief Update the cached rendering preference for dash.
     * @param[in] s Line dash enum; no enum-membership validation.
     * @note Equality is a no-op. A change emits dashChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setDash(Qt::PenStyle s);
    /**
     * @brief Update the cached rendering preference for capStyle.
     * @param[in] s Line-end cap enum; no validation.
     * @note Equality is a no-op. A change emits capStyleChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setCapStyle(Qt::PenCapStyle s);
    /**
     * @brief Update the cached rendering preference for joinStyle.
     * @param[in] s Line-join enum; no validation.
     * @note Equality is a no-op. A change emits joinStyleChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setJoinStyle(Qt::PenJoinStyle s);

    /**
     * @brief Update the cached rendering preference for showMarkers.
     * @param[in] on Whether point markers are requested.
     * @note Equality is a no-op. A change emits showMarkersChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setShowMarkers(bool on);
    /**
     * @brief Update the cached rendering preference for shape.
     * @param[in] s Marker enum, cast directly to storage without validation.
     * @note Equality is a no-op. A change emits shapeChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setShape(MarkerShapeQ s);
    /**
     * @brief Update the cached rendering preference for markerSize.
     * @param[in] v Marker diameter in logical pixels, expected nonnegative finite; not clamped.
     * @note Equality is a no-op. A change emits markerSizeChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setMarkerSize(qreal v);
    /**
     * @brief Update the cached rendering preference for markerFillColor.
     * @param[in] c Marker fill; invalid means derive from the main colour.
     * @note Equality is a no-op. A change emits markerFillColorChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setMarkerFillColor(const QColor &c);
    /**
     * @brief Update the cached rendering preference for markerBorderColor.
     * @param[in] c Marker border; invalid means derive a darker main colour.
     * @note Equality is a no-op. A change emits markerBorderColorChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setMarkerBorderColor(const QColor &c);
    /**
     * @brief Update the cached rendering preference for markerBorderWidth.
     * @param[in] v Marker border width in logical pixels; nonpositive suppresses the pixmap border.
     * @note Equality is a no-op. A change emits markerBorderWidthChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setMarkerBorderWidth(qreal v);

    /**
     * @brief Update the cached rendering preference for showPointLabels.
     * @param[in] on Whether sample labels are requested.
     * @note Equality is a no-op. A change emits showPointLabelsChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setShowPointLabels(bool on);
    /**
     * @brief Update the cached rendering preference for pointLabelFont.
     * @param[in] f Sample label font copied with its Qt font-size semantics.
     * @note Equality is a no-op. A change emits pointLabelFontChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setPointLabelFont(const QFont &f);
    /**
     * @brief Update the cached rendering preference for pointLabelColor.
     * @param[in] c Sample label colour; invalid derives a darker main colour.
     * @note Equality is a no-op. A change emits pointLabelColorChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setPointLabelColor(const QColor &c);
    /**
     * @brief Update the cached rendering preference for pointLabelFormatMode.
     * @param[in] m Label format family, cast directly to NumberFormatMode without validation.
     * @note Equality is a no-op. A change emits pointLabelFormatModeChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setPointLabelFormatMode(LabelFormatModeQ m);
    /**
     * @brief Update the cached rendering preference for pointLabelPrecision.
     * @param[in] n Raw digits; no clamping here, with effective lower bounds applied by NumberFormat at rendering.
     * @note Equality is a no-op. A change emits pointLabelPrecisionChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setPointLabelPrecision(int n);
    /**
     * @brief Update the cached rendering preference for pointLabelFormat.
     * @param[in] spec Optional printf override copied verbatim; NumberFormat later ignores invalid syntax.
     * @note Equality is a no-op. A change emits pointLabelFormatChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setPointLabelFormat(const QString &spec);

    /**
     * @brief Update the cached rendering preference for showAreaFill.
     * @param[in] on Whether the host should render an area under the line.
     * @note Equality is a no-op. A change emits showAreaFillChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setShowAreaFill(bool on);
    /**
     * @brief Update the cached rendering preference for areaFillColor.
     * @param[in] c Area colour; invalid derives the main colour at alpha 0.25.
     * @note Equality is a no-op. A change emits areaFillColorChanged() then styleChanged() unless signals are blocked. No chart is updated automatically and no persistence occurs.
     */
    void setAreaFillColor(const QColor &c);

signals:
    /**
     * @brief Notify a changed cached color preference.
     * @param[in] value New raw stored value. Main series colour; no validity enforcement.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void colorChanged(const QColor &value);
    /**
     * @brief Notify a changed cached opacity preference.
     * @param[in] value New raw stored value. Series opacity, expected [0,1]; copied without clamping.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void opacityChanged(qreal value);
    /**
     * @brief Notify a changed cached legendName preference.
     * @param[in] value New raw stored value. Style-level legend text; empty lets rendering preserve its current name.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void legendNameChanged(const QString &value);
    /**
     * @brief Notify an instance-level legend override edit independently of style fields.
     * @param[in] value New exact override text; empty removes the host-level override policy.
     */
    void legendOverrideChanged(const QString &value);

    /**
     * @brief Notify a changed cached showLine preference.
     * @param[in] value New raw stored value. Whether line presentation is requested.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void showLineChanged(bool value);
    /**
     * @brief Notify a changed cached lineWidth preference.
     * @param[in] value New raw stored value. Line width in logical pixels, expected nonnegative; not clamped.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void lineWidthChanged(qreal value);
    /**
     * @brief Notify a changed cached dash preference.
     * @param[in] value New raw stored value. Line dash enum; no enum-membership validation.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void dashChanged(Qt::PenStyle value);
    /**
     * @brief Notify a changed cached capStyle preference.
     * @param[in] value New raw stored value. Line-end cap enum; no validation.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void capStyleChanged(Qt::PenCapStyle value);
    /**
     * @brief Notify a changed cached joinStyle preference.
     * @param[in] value New raw stored value. Line-join enum; no validation.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void joinStyleChanged(Qt::PenJoinStyle value);

    /**
     * @brief Notify a changed cached showMarkers preference.
     * @param[in] value New raw stored value. Whether point markers are requested.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void showMarkersChanged(bool value);
    /**
     * @brief Notify a changed cached shape preference.
     * @param[in] value New raw stored value. Marker enum, cast directly to storage without validation.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void shapeChanged(MarkerShapeQ value);
    /**
     * @brief Notify a changed cached markerSize preference.
     * @param[in] value New raw stored value. Marker diameter in logical pixels, expected nonnegative finite; not clamped.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void markerSizeChanged(qreal value);
    /**
     * @brief Notify a changed cached markerFillColor preference.
     * @param[in] value New raw stored value. Marker fill; invalid means derive from the main colour.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void markerFillColorChanged(const QColor &value);
    /**
     * @brief Notify a changed cached markerBorderColor preference.
     * @param[in] value New raw stored value. Marker border; invalid means derive a darker main colour.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void markerBorderColorChanged(const QColor &value);
    /**
     * @brief Notify a changed cached markerBorderWidth preference.
     * @param[in] value New raw stored value. Marker border width in logical pixels; nonpositive suppresses the pixmap border.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void markerBorderWidthChanged(qreal value);

    /**
     * @brief Notify a changed cached showPointLabels preference.
     * @param[in] value New raw stored value. Whether sample labels are requested.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void showPointLabelsChanged(bool value);
    /**
     * @brief Notify a changed cached pointLabelFont preference.
     * @param[in] value New raw stored value. Sample label font copied with its Qt font-size semantics.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void pointLabelFontChanged(const QFont &value);
    /**
     * @brief Notify a changed cached pointLabelColor preference.
     * @param[in] value New raw stored value. Sample label colour; invalid derives a darker main colour.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void pointLabelColorChanged(const QColor &value);
    /**
     * @brief Notify a changed cached pointLabelFormatMode preference.
     * @param[in] value New raw stored value. Label format family, cast directly to NumberFormatMode without validation.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void pointLabelFormatModeChanged(LabelFormatModeQ value);
    /**
     * @brief Notify a changed cached pointLabelPrecision preference.
     * @param[in] value New raw stored value. Raw digits; no clamping here, with effective lower bounds applied by NumberFormat at rendering.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void pointLabelPrecisionChanged(int value);
    /**
     * @brief Notify a changed cached pointLabelFormat preference.
     * @param[in] value New raw stored value. Optional printf override copied verbatim; NumberFormat later ignores invalid syntax.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void pointLabelFormatChanged(const QString &value);

    /**
     * @brief Notify a changed cached showAreaFill preference.
     * @param[in] value New raw stored value. Whether the host should render an area under the line.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void showAreaFillChanged(bool value);
    /**
     * @brief Notify a changed cached areaFillColor preference.
     * @param[in] value New raw stored value. Area colour; invalid derives the main colour at alpha 0.25.
     * @note Typed setters emit before styleChanged(); setStyle() suppresses these per-field notifications.
     */
    void areaFillColorChanged(const QColor &value);

    /*! \brief Aggregate style notification after a changed style-field setter or any setStyle().
     *  Hosts subscribe to this once to live-update the chart.
     * @param[in] s Current cached style after a typed field edit or setStyle(); excludes legendOverride. Receivers needing a persistent snapshot should copy it.
     */
    void styleChanged(const openswmmvis::plot::SeriesStyle &s);

private:
    /**
     * @brief Emit styleChanged() with the current cache; QObject signal blocking still applies.
     */
    void emitAggregate_();

    SeriesStyle m_style;
    QString m_legendOverride; ///< Mirror of SeriesSpec::legendOverride.
};

} // namespace openswmmvis::plot

Q_DECLARE_METATYPE(openswmmvis::plot::SeriesStyleObject::MarkerShapeQ)
Q_DECLARE_METATYPE(openswmmvis::plot::SeriesStyleObject::LabelFormatModeQ)

#endif // OPENSWMMVIS_PLOT_SERIESSTYLEOBJECT_H
