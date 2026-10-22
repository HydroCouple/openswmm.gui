/**
 * @file      meshprofileplotoptions.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Q_OBJECT facade for the user-configurable display options on the 2D
 *         mesh longitudinal profile plot.  Edited via a QPropertyModel-backed
 *         tree (see MeshProfilePlotDialog's "Display Options…") and read by
 *         MeshProfilePlotWidget directly — every setter propagates through the
 *         single `changed()` signal.  Mirrors ProfilePlotOptions' mechanics
 *         (Q_ENUM positions, displayLabelFor, QPen/QBrush props) but trimmed
 *         to the continuous-cross-section render passes (no node/link theming).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MESH_PROFILE_PLOT_OPTIONS_H
#define MESH_PROFILE_PLOT_OPTIONS_H

#include "plot/numberformat.h"

#include <QBrush>
#include <QJsonObject>
#include <QColor>
#include <QFont>
#include <QObject>
#include <QPen>
#include <QPointF>
#include <QString>

class MeshProfilePlotOptions : public QObject
{
    Q_OBJECT

public:
    enum LegendPosition { TopRight = 0, TopLeft = 1, BottomLeft = 2, BottomRight = 3 };
    Q_ENUM(LegendPosition)

    /*! \brief Axis label number mode; values mirror
     *  openswmmvis::plot::NumberFormatMode (0=Decimals, 1=SignificantFigures). */
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

    enum TimeLabelPosition {
        TimeTopRight = 0,
        TimeTopLeft = 1,
        TimeBottomLeft = 2,
        TimeBottomRight = 3
    };
    Q_ENUM(TimeLabelPosition)

    // ── Visibility toggles ──────────────────────────────────────────────
    Q_PROPERTY(bool showDepthFill READ showDepthFill WRITE setShowDepthFill NOTIFY changed)
    Q_PROPERTY(bool showWseLine READ showWseLine WRITE setShowWseLine NOTIFY changed)
    Q_PROPERTY(bool showMaxEnvelopeFill READ showMaxEnvelopeFill WRITE setShowMaxEnvelopeFill NOTIFY
                   changed)
    Q_PROPERTY(bool showMaxEnvelopeLine READ showMaxEnvelopeLine WRITE setShowMaxEnvelopeLine NOTIFY
                   changed)
    Q_PROPERTY(
        bool showCellBoundaries READ showCellBoundaries WRITE setShowCellBoundaries NOTIFY changed)

    // ── Terrain / water styling ─────────────────────────────────────────
    Q_PROPERTY(QBrush soilFill READ soilFill WRITE setSoilFill NOTIFY changed)
    Q_PROPERTY(QPen groundLinePen READ groundLinePen WRITE setGroundLinePen NOTIFY changed)
    Q_PROPERTY(QBrush depthFillBrush READ depthFillBrush WRITE setDepthFillBrush NOTIFY changed)
    Q_PROPERTY(QPen wseLinePen READ wseLinePen WRITE setWseLinePen NOTIFY changed)
    Q_PROPERTY(QPen maxEnvelopePen READ maxEnvelopePen WRITE setMaxEnvelopePen NOTIFY changed)
    Q_PROPERTY(
        QBrush maxEnvelopeBrush READ maxEnvelopeBrush WRITE setMaxEnvelopeBrush NOTIFY changed)
    Q_PROPERTY(
        QColor cellBoundaryColor READ cellBoundaryColor WRITE setCellBoundaryColor NOTIFY changed)

    // ── Axis number format ──────────────────────────────────────────────
    // X axis = chainage/distance; Y axis = elevation. Seeded from the global
    // Preferences default; edits here override per-plot.
    Q_PROPERTY(MeshProfilePlotOptions::AxisNumberFormat xAxisNumberFormat READ xAxisNumberFormat
                   WRITE setXAxisNumberFormat NOTIFY changed)
    Q_PROPERTY(QString xLabelFormat READ xLabelFormat WRITE setXLabelFormat NOTIFY changed)
    Q_PROPERTY(MeshProfilePlotOptions::AxisNumberFormat yAxisNumberFormat READ yAxisNumberFormat
                   WRITE setYAxisNumberFormat NOTIFY changed)
    Q_PROPERTY(QString yLabelFormat READ yLabelFormat WRITE setYLabelFormat NOTIFY changed)

    // ── Legend ──────────────────────────────────────────────────────────
    Q_PROPERTY(bool legendVisible READ legendVisible WRITE setLegendVisible NOTIFY changed)
    Q_PROPERTY(MeshProfilePlotOptions::LegendPosition legendPosition READ legendPosition WRITE
                   setLegendPosition NOTIFY changed)
    Q_PROPERTY(QFont legendFont READ legendFont WRITE setLegendFont NOTIFY changed)
    Q_PROPERTY(double legendOpacity READ legendOpacity WRITE setLegendOpacity NOTIFY changed)
    Q_PROPERTY(QPointF legendOffset READ legendOffset WRITE setLegendOffset NOTIFY changed)

    // ── Timestamp overlay ───────────────────────────────────────────────
    Q_PROPERTY(bool showTimeLabel READ showTimeLabel WRITE setShowTimeLabel NOTIFY changed)
    Q_PROPERTY(MeshProfilePlotOptions::TimeLabelPosition timeLabelPosition READ timeLabelPosition
                   WRITE setTimeLabelPosition NOTIFY changed)
    Q_PROPERTY(QColor timeLabelColor READ timeLabelColor WRITE setTimeLabelColor NOTIFY changed)
    Q_PROPERTY(QFont timeLabelFont READ timeLabelFont WRITE setTimeLabelFont NOTIFY changed)
    Q_PROPERTY(QString timeLabelFormat READ timeLabelFormat WRITE setTimeLabelFormat NOTIFY changed)
    Q_PROPERTY(QPointF timeLabelOffset READ timeLabelOffset WRITE setTimeLabelOffset NOTIFY changed)

public:
    /**
     * @brief Create per-plot display state seeded from application axis-format preferences.
     * @param[in] parent Optional QObject owner; options do not own a chart or any result source. Legend/timestamp fonts use the system general font.
     * @note Construction emits no changed() notification. Typed setters edit only this cache, emit changed() for effective changes and do not write application preferences; connected views apply rendering/resampling as needed. Use on the GUI thread.
     */
    explicit MeshProfilePlotOptions(QObject *parent = nullptr);
    /**
     * @brief Serialize declared display meta-properties into a version-one options object.
     * @return Owned object with version/values. Pens retain colour/width/style only, brushes colour/style only, fonts use QFont text and points two-number arrays; custom pen dash/cap/join and brush textures/gradients do not round-trip in this schema. No validation occurs.
     */
    QJsonObject toJson() const;
    /**
     * @brief Decode all recognized display values before applying them through property setters.
     * @param[in] json Empty is a successful no-op; otherwise requires version one and an object-valued values field. Missing recognized properties preserve current values; unknown keys are ignored.
     * @param[out] error Optional first-failure diagnostic, cleared on entry.
     * @return False for a malformed recognized property before any property is applied. True applies staged values with signals blocked, then emits changed() once even when the nonempty payload made no effective changes. Per-property write return statuses are not checked.
     * @note Validates colour/font tokens and known enum keys, limits pen width [0,100], pen style [0,5] and brush style [0,14], and requires two numeric point components. Other primitive values use QVariant conversion and typed-setter clamps. Deserialization does not restore omitted pen/brush richness or update application preferences.
     */
    bool fromJson(const QJsonObject &json, QString *error = nullptr);

    /*! \brief Human-readable label for the QPropertyModel row header.
     * @param[in] propertyName Exact meta-property identifier.
     * @return Translated cached label or the original identifier when unmapped; the static mapping is created on first call.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    // ── Getters ─────────────────────────────────────────────────────────
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to fill the current water band above ground at valid wet stations. Returns a value copy; no data-unit conversion occurs.
     */
    bool showDepthFill() const { return m_showDepthFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to stroke the current water-surface elevation independently of depth fill. Returns a value copy; no data-unit conversion occurs.
     */
    bool showWseLine() const { return m_showWseLine; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to fill the temporal maximum water band. Returns a value copy; no data-unit conversion occurs.
     */
    bool showMaxEnvelopeFill() const { return m_showMaxEnvelopeFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to stroke the temporal maximum water-surface outline. Returns a value copy; no data-unit conversion occurs.
     */
    bool showMaxEnvelopeLine() const { return m_showMaxEnvelopeLine; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to draw diagnostic dots at sampled cell-edge crossings. Returns a value copy; no data-unit conversion occurs.
     */
    bool showCellBoundaries() const { return m_showCellBoundaries; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Crossing-dot colour including alpha; no typed validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor cellBoundaryColor() const { return m_cellBoundaryColor; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Ground/soil polygon brush with alpha/pattern retained. Returns a value copy; no data-unit conversion occurs.
     */
    QBrush soilFill() const { return m_soilFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Terrain stroke pen, including logical-pixel width and full dash/cap/join. Returns a value copy; no data-unit conversion occurs.
     */
    QPen groundLinePen() const { return m_groundLinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Current wet-band brush with alpha/pattern retained. Returns a value copy; no data-unit conversion occurs.
     */
    QBrush depthFillBrush() const { return m_depthFillBrush; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Current water-surface stroke pen, in logical-pixel widths. Returns a value copy; no data-unit conversion occurs.
     */
    QPen wseLinePen() const { return m_wseLinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Temporal maximum water-surface stroke pen, in logical-pixel widths. Returns a value copy; no data-unit conversion occurs.
     */
    QPen maxEnvelopePen() const { return m_maxEnvelopePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Temporal maximum water-band brush with alpha/pattern retained. Returns a value copy; no data-unit conversion occurs.
     */
    QBrush maxEnvelopeBrush() const { return m_maxEnvelopeBrush; }
    /*! Combined format per axis, derived from the mode + digit count that
     *  remain the internal representation.
     * @brief Approximate the chainage/distance numeric policy as a preset.
     * @return Closest preset for cached mode/count; custom text is ignored and unknown modes fall back to Decimals2. The returned integer can exceed a limited local enum vocabulary when the shared mode contains additional families.
     */
    AxisNumberFormat xAxisNumberFormat() const;
    /**
     * @brief Approximate the elevation/value numeric policy as a preset.
     * @return Closest preset for cached mode/count; custom text is ignored and unknown modes fall back to Decimals2. The returned integer can exceed a limited local enum vocabulary when the shared mode contains additional families.
     */
    AxisNumberFormat yAxisNumberFormat() const;

    /**
     * @brief Inspect chainage/distance numeric label policy.
     * @return Raw cached NumberFormatMode mirror; no enum validation is applied by the typed setter.
     */
    LabelFormatMode xLabelFormatMode() const { return m_xLabelMode; }
    /**
     * @brief Inspect chainage/distance numeric label policy.
     * @return Cached digit count, with typed precision edits clamped to [0,10]; SignificantFigures rendering uses at least one.
     */
    int xLabelPrecision() const { return m_xLabelPrecision; }
    /**
     * @brief Inspect chainage/distance numeric label policy.
     * @return Optional printf override copied verbatim; invalid syntax remains stored but is ignored by NumberFormat.
     */
    QString xLabelFormat() const { return m_xLabelFormatStr; }
    /**
     * @brief Inspect elevation/value numeric label policy.
     * @return Raw cached NumberFormatMode mirror; no enum validation is applied by the typed setter.
     */
    LabelFormatMode yLabelFormatMode() const { return m_yLabelMode; }
    /**
     * @brief Inspect elevation/value numeric label policy.
     * @return Cached digit count, with typed precision edits clamped to [0,10]; SignificantFigures rendering uses at least one.
     */
    int yLabelPrecision() const { return m_yLabelPrecision; }
    /**
     * @brief Inspect elevation/value numeric label policy.
     * @return Optional printf override copied verbatim; invalid syntax remains stored but is ignored by NumberFormat.
     */
    QString yLabelFormat() const { return m_yLabelFormatStr; }
    /*! \brief Current X/Y axis label format as the shared value type.
     * @brief Copy the chainage/distance numeric formatting policy.
     * @return Independent raw mode/count/custom snapshot without validation or conversion.
     */
    openswmmvis::plot::NumberFormat xFormat() const
    {
        return {static_cast<openswmmvis::plot::NumberFormatMode>(m_xLabelMode), m_xLabelPrecision,
                m_xLabelFormatStr};
    }
    /**
     * @brief Copy the elevation/value numeric formatting policy.
     * @return Independent raw mode/count/custom snapshot without validation or conversion.
     */
    openswmmvis::plot::NumberFormat yFormat() const
    {
        return {static_cast<openswmmvis::plot::NumberFormatMode>(m_yLabelMode), m_yLabelPrecision,
                m_yLabelFormatStr};
    }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether the in-plot legend is requested; does not change source/series visibility. Returns a value copy; no data-unit conversion occurs.
     */
    bool legendVisible() const { return m_legendVisible; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Legend corner anchor; raw enum values are stored without validation by the typed setter. Returns a value copy; no data-unit conversion occurs.
     */
    LegendPosition legendPosition() const { return m_legendPosition; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Legend text font with Qt point/pixel-size semantics. Returns a value copy; no data-unit conversion occurs.
     */
    QFont legendFont() const { return m_legendFont; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Legend background opacity, with typed edits clamped to [0,1]; finite input is expected. Returns a value copy; no data-unit conversion occurs.
     */
    double legendOpacity() const { return m_legendOpacity; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Logical-pixel XY displacement from the chosen corner anchor, with positive X right and positive Y down. Returns a value copy; no data-unit conversion occurs.
     */
    QPointF legendOffset() const { return m_legendOffset; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether the timestamp overlay is requested; does not affect the animation cursor. Returns a value copy; no data-unit conversion occurs.
     */
    bool showTimeLabel() const { return m_showTimeLabel; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Timestamp corner anchor, stored as a raw enum by typed edits. Returns a value copy; no data-unit conversion occurs.
     */
    TimeLabelPosition timeLabelPosition() const { return m_timeLabelPosition; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Timestamp foreground colour including alpha; typed edits do not reject invalid colours. Returns a value copy; no data-unit conversion occurs.
     */
    QColor timeLabelColor() const { return m_timeLabelColor; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Timestamp font with Qt point/pixel-size semantics. Returns a value copy; no data-unit conversion occurs.
     */
    QFont timeLabelFont() const { return m_timeLabelFont; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return QDateTime format pattern copied verbatim, independent of numeric axis formatting. Returns a value copy; no data-unit conversion occurs.
     */
    QString timeLabelFormat() const { return m_timeLabelFormat; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Logical-pixel XY timestamp displacement from its anchor, positive right/down. Returns a value copy; no data-unit conversion occurs.
     */
    QPointF timeLabelOffset() const { return m_timeLabelOffset; }

public slots:
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to fill the current water band above ground at valid wet stations.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowDepthFill(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to stroke the current water-surface elevation independently of depth fill.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowWseLine(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to fill the temporal maximum water band.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowMaxEnvelopeFill(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to stroke the temporal maximum water-surface outline.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowMaxEnvelopeLine(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to draw diagnostic dots at sampled cell-edge crossings.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowCellBoundaries(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Crossing-dot colour including alpha; no typed validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setCellBoundaryColor(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] b Ground/soil polygon brush with alpha/pattern retained.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setSoilFill(const QBrush &b);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Terrain stroke pen, including logical-pixel width and full dash/cap/join.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setGroundLinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] b Current wet-band brush with alpha/pattern retained.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setDepthFillBrush(const QBrush &b);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Current water-surface stroke pen, in logical-pixel widths.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setWseLinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Temporal maximum water-surface stroke pen, in logical-pixel widths.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setMaxEnvelopePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] b Temporal maximum water-band brush with alpha/pattern retained.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setMaxEnvelopeBrush(const QBrush &b);
    /**
     * @brief Apply a combined chainage/distance preset through mode/precision setters.
     * @param[in] f Shared preset index; unsupported indices fall back to two decimals. Existing custom printf text is retained and can still override it.
     * @note Mode and count are updated sequentially and can each emit changed(); this is not an atomic one-notification edit.
     */
    void setXAxisNumberFormat(AxisNumberFormat f);
    /**
     * @brief Apply a combined elevation/value preset through mode/precision setters.
     * @param[in] f Shared preset index; unsupported indices fall back to two decimals. Existing custom printf text is retained and can still override it.
     * @note Mode and count are updated sequentially and can each emit changed(); this is not an atomic one-notification edit.
     */
    void setYAxisNumberFormat(AxisNumberFormat f);
    /**
     * @brief Store the chainage/distance numeric-format family.
     * @param[in] m Raw mode enum, including shared NumberFormatMode values beyond a limited local mirror; no enum validation occurs.
     * @note Equality is a no-op; otherwise caches and emits changed(). Does not clear a custom printf override.
     */
    void setXLabelFormatMode(LabelFormatMode m);
    /**
     * @brief Store the chainage/distance label digit count.
     * @param[in] count Requested precision clamped to [0,10]; SignificantFigures rendering subsequently floors zero to one.
     * @note Equality after clamping is a no-op; otherwise stores and emits changed(), without clearing custom text.
     */
    void setXLabelPrecision(int count);
    /**
     * @brief Store an optional chainage/distance printf override.
     * @param[in] spec Text copied verbatim; empty/invalid strings fall back to mode/count during NumberFormat rendering.
     * @note Equality is a no-op; otherwise emits changed(). Invalid syntax is retained rather than rejected.
     */
    void setXLabelFormat(const QString &spec);
    /**
     * @brief Store the elevation/value numeric-format family.
     * @param[in] m Raw mode enum, including shared NumberFormatMode values beyond a limited local mirror; no enum validation occurs.
     * @note Equality is a no-op; otherwise caches and emits changed(). Does not clear a custom printf override.
     */
    void setYLabelFormatMode(LabelFormatMode m);
    /**
     * @brief Store the elevation/value label digit count.
     * @param[in] count Requested precision clamped to [0,10]; SignificantFigures rendering subsequently floors zero to one.
     * @note Equality after clamping is a no-op; otherwise stores and emits changed(), without clearing custom text.
     */
    void setYLabelPrecision(int count);
    /**
     * @brief Store an optional elevation/value printf override.
     * @param[in] spec Text copied verbatim; empty/invalid strings fall back to mode/count during NumberFormat rendering.
     * @note Equality is a no-op; otherwise emits changed(). Invalid syntax is retained rather than rejected.
     */
    void setYLabelFormat(const QString &spec);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether the in-plot legend is requested; does not change source/series visibility.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLegendVisible(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Legend corner anchor; raw enum values are stored without validation by the typed setter.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLegendPosition(LegendPosition p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] f Legend text font with Qt point/pixel-size semantics.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLegendFont(const QFont &f);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] a Legend background opacity, with typed edits clamped to [0,1]; finite input is expected.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLegendOpacity(double a);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Logical-pixel XY displacement from the chosen corner anchor, with positive X right and positive Y down.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLegendOffset(const QPointF &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether the timestamp overlay is requested; does not affect the animation cursor.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowTimeLabel(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Timestamp corner anchor, stored as a raw enum by typed edits.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setTimeLabelPosition(TimeLabelPosition p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Timestamp foreground colour including alpha; typed edits do not reject invalid colours.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setTimeLabelColor(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] f Timestamp font with Qt point/pixel-size semantics.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setTimeLabelFont(const QFont &f);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] fmt QDateTime format pattern copied verbatim, independent of numeric axis formatting.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setTimeLabelFormat(const QString &fmt);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Logical-pixel XY timestamp displacement from its anchor, positive right/down.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setTimeLabelOffset(const QPointF &p);

signals:
    /**
     * @brief Notify connected views that this plot's option cache changed.
     * @note Typed effective edits emit once per field; combined presets can emit twice. No external geometry/result/palette edits are monitored by this options object.
     */
    void changed();

private:
    bool m_showDepthFill = true;
    bool m_showWseLine = true;
    bool m_showMaxEnvelopeFill = true;
    bool m_showMaxEnvelopeLine = true;
    bool m_showCellBoundaries = true;

    // Terrain earth tone (matches the 1D profile soil fill) + crisp ground
    // line; animated depth in a translucent water blue; max envelope a
    // dashed line over a faint band.
    QBrush m_soilFill{QColor(0xC6, 0xA9, 0x7A, 160)};
    QPen m_groundLinePen = QPen(QColor(0x6B, 0x52, 0x2E), 1.6, Qt::SolidLine);
    QBrush m_depthFillBrush{QColor(0x55, 0xA8, 0xE6, 120)};
    QPen m_wseLinePen = QPen(QColor(0x1F, 0x6F, 0xB7), 2.0, Qt::SolidLine);
    QPen m_maxEnvelopePen = QPen(QColor(0x1F, 0x6F, 0xB7), 1.4, Qt::DashLine);
    QBrush m_maxEnvelopeBrush{QColor(0x55, 0xA8, 0xE6, 60)};

    // Diagnostic dots where the traced line crosses a mesh-cell edge — a warm
    // accent that reads against the blue water and brown ground.
    QColor m_cellBoundaryColor{0xD9, 0x53, 0x1E};

    // Axis number format — seeded from the global Preferences default in the
    // constructor.
    LabelFormatMode m_xLabelMode = Decimals;
    int m_xLabelPrecision = 0;
    QString m_xLabelFormatStr; // optional printf override; empty = use mode+precision
    LabelFormatMode m_yLabelMode = Decimals;
    int m_yLabelPrecision = 2;
    QString m_yLabelFormatStr; // optional printf override; empty = use mode+precision

    bool m_legendVisible = true;
    LegendPosition m_legendPosition = TopRight;
    QFont m_legendFont;
    double m_legendOpacity = 0.86;
    QPointF m_legendOffset{0.0, 0.0};

    bool m_showTimeLabel = true;
    TimeLabelPosition m_timeLabelPosition = TimeTopLeft;
    QColor m_timeLabelColor{0x10, 0x10, 0x10};
    QFont m_timeLabelFont;
    QString m_timeLabelFormat = QStringLiteral("dd-MMM-yyyy HH:mm:ss");
    QPointF m_timeLabelOffset{0.0, 0.0};
};

#endif // MESH_PROFILE_PLOT_OPTIONS_H
