/**
 * @file      flowarrowsublayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Direction-only flow-arrow sublayer for 2D results animation.
 *
 *         Sits alongside VelocityVectorSublayer in the 2D results mix.
 *         The difference is intent: VelocityVectorSublayer encodes
 *         magnitude as glyph length so the user can see how fast water
 *         moves; FlowArrowSublayer renders fixed-length arrows on a
 *         regular sample grid so the user can read flow *direction* at a
 *         glance regardless of magnitude (cleaner pattern reading at
 *         scale, particularly for shallow flow with tiny |v|).
 *
 *         Dynamic — isDynamic() == true. The renderer rebuilds the arrow
 *         field on every animation tick.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_FLOWARROWSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_FLOWARROWSUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"
#include "render/colorramp.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class FlowArrowStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(double arrowLengthPx READ arrowLengthPx WRITE setArrowLengthPx NOTIFY styleChanged)
    Q_PROPERTY(
        double arrowSpacingPx READ arrowSpacingPx WRITE setArrowSpacingPx NOTIFY styleChanged)
    Q_PROPERTY(double headSizePx READ headSizePx WRITE setHeadSizePx NOTIFY styleChanged)
    Q_PROPERTY(double shaftWidthPx READ shaftWidthPx WRITE setShaftWidthPx NOTIFY styleChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY styleChanged)
    Q_PROPERTY(
        double dryDepthCutoff READ dryDepthCutoff WRITE setDryDepthCutoff NOTIFY styleChanged)
    Q_PROPERTY(bool placeAtCellCenters READ placeAtCellCenters WRITE setPlaceAtCellCenters NOTIFY
                   styleChanged)
    // VS.7 — colour direction arrows by speed magnitude through a ramp.
    Q_PROPERTY(
        bool colorByMagnitude READ colorByMagnitude WRITE setColorByMagnitude NOTIFY styleChanged)
    Q_PROPERTY(QString colorRampName READ colorRampName WRITE setColorRampName NOTIFY styleChanged)
    Q_PROPERTY(double speedMinMps READ speedMinMps WRITE setSpeedMinMps NOTIFY styleChanged)
    Q_PROPERTY(double speedMaxMps READ speedMaxMps WRITE setSpeedMaxMps NOTIFY styleChanged)

    Q_CLASSINFO("group:arrowLengthPx", "Arrow")
    Q_CLASSINFO("group:headSizePx", "Arrow")
    Q_CLASSINFO("group:shaftWidthPx", "Arrow")
    Q_CLASSINFO("group:color", "Arrow")
    Q_CLASSINFO("group:outlineColor", "Arrow")
    Q_CLASSINFO("group:arrowSpacingPx", "Placement")
    Q_CLASSINFO("group:placeAtCellCenters", "Placement")
    Q_CLASSINFO("group:dryDepthCutoff", "Filtering")
    Q_CLASSINFO("group:colorByMagnitude", "Colour")
    Q_CLASSINFO("group:colorRampName", "Colour")
    Q_CLASSINFO("group:speedMinMps", "Colour")
    Q_CLASSINFO("group:speedMaxMps", "Colour")

public:
    /** @brief Construct the style and its default settings.
     * @param parent Optional QObject owner; its destruction destroys this style.
     * Uses the declared flat-color direction-arrow defaults; magnitude coloring is initially off. All reads/edits occur on its owning Qt thread. */
    explicit FlowArrowStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    /** @brief Read the fixed arrow length in logical screen pixels.
     * @return Stored/forwarded value; initially 20. */
    [[nodiscard]] double arrowLengthPx() const { return m_arrowLengthPx; }
    /** @brief Read the regular-grid arrow spacing in logical screen pixels.
     * @return Stored/forwarded value; initially 36. */
    [[nodiscard]] double arrowSpacingPx() const { return m_arrowSpacingPx; }
    /** @brief Read the arrowhead size in logical screen pixels.
     * @return Stored/forwarded value; initially 6. */
    [[nodiscard]] double headSizePx() const { return m_headSizePx; }
    /** @brief Read the arrow shaft width in logical screen pixels.
     * @return Stored/forwarded value; initially 1.6. */
    [[nodiscard]] double shaftWidthPx() const { return m_shaftWidthPx; }
    /** @brief Read the flat arrow color.
     * @return Stored/forwarded value; initially RGBA(20,20,20,230). */
    [[nodiscard]] QColor color() const { return m_color; }
    /** @brief Read the arrow-outline color.
     * @return Stored/forwarded value; initially RGBA(255,255,255,200). */
    [[nodiscard]] QColor outlineColor() const { return m_outlineColor; }
    /** @brief Read the minimum rendered depth in metres.
     * @return Stored/forwarded value; initially 0.01. */
    [[nodiscard]] double dryDepthCutoff() const { return m_dryDepthCutoff; }
    /** @brief Read the cell-center-versus-screen-grid placement switch.
     * @return Stored/forwarded value; initially false. */
    [[nodiscard]] bool placeAtCellCenters() const { return m_placeAtCellCenters; }
    /** @brief Read the speed-driven arrow color switch.
     * @return Stored/forwarded value; initially false. */
    [[nodiscard]] bool colorByMagnitude() const { return m_colorByMagnitude; }
    /** @brief Read the speed color-ramp token.
     * @return Stored/forwarded value; initially "viridis". */
    [[nodiscard]] QString colorRampName() const { return m_colorRampName; }
    /** @brief Read the lower color speed bound in m/s.
     * @return Stored/forwarded value; initially 0. */
    [[nodiscard]] double speedMinMps() const { return m_speedMinMps; }
    /** @brief Read the upper color speed bound in m/s.
     * @return Stored/forwarded value; initially 2. */
    [[nodiscard]] double speedMaxMps() const { return m_speedMaxMps; }

    /** @brief Set the fixed arrow length in logical screen pixels and notify on a change.
     * @param v New value. Clamps finite input to [2,200] with qBound.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setArrowLengthPx(double v);
    /** @brief Set the regular-grid arrow spacing in logical screen pixels and notify on a change.
     * @param v New value. Clamps finite input to [4,1000] with qBound.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setArrowSpacingPx(double v);
    /** @brief Set the arrowhead size in logical screen pixels and notify on a change.
     * @param v New value. Clamps finite input to [1,64] with qBound.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setHeadSizePx(double v);
    /** @brief Set the arrow shaft width in logical screen pixels and notify on a change.
     * @param v New value. Clamps finite input to [0.5,20] with qBound.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setShaftWidthPx(double v);
    /** @brief Set the flat arrow color and notify on a change.
     * @param v New value. Accepts invalid QColor values without validation.
     * Unchanged values according to QColor equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setColor(const QColor &v)
    {
        if (m_color == v)
            return;
        m_color = v;
        setDirty();
    }
    /** @brief Set the arrow-outline color and notify on a change.
     * @param v New value. Accepts invalid QColor values without validation.
     * Unchanged values according to QColor equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setOutlineColor(const QColor &v)
    {
        if (m_outlineColor == v)
            return;
        m_outlineColor = v;
        setDirty();
    }
    /** @brief Set the minimum rendered depth in metres and notify on a change.
     * @param v New value. Clamps finite input to [0,100] with qBound.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setDryDepthCutoff(double v);
    /** @brief Set the cell-center-versus-screen-grid placement switch and notify on a change.
     * @param v New value. Changes placement without rebuilding geometry synchronously.
     * Unchanged values according to Boolean equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setPlaceAtCellCenters(bool v)
    {
        if (m_placeAtCellCenters == v)
            return;
        m_placeAtCellCenters = v;
        setDirty();
    }
    /** @brief Set the speed-driven arrow color switch and notify on a change.
     * @param v New value. Retains ramp and flat-color settings when switching.
     * Unchanged values according to Boolean equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setColorByMagnitude(bool v)
    {
        if (m_colorByMagnitude == v)
            return;
        m_colorByMagnitude = v;
        setDirty();
    }
    /** @brief Set the speed color-ramp token and notify on a change.
     * @param v New value. Stores the token without validating catalog availability.
     * Unchanged values according to exact string equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setColorRampName(const QString &v)
    {
        if (m_colorRampName == v)
            return;
        m_colorRampName = v;
        setDirty();
    }
    /** @brief Set the lower color speed bound in m/s and notify on a change.
     * @param v New value. Stores the raw bound without validating order/finiteness.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setSpeedMinMps(double v)
    {
        if (qFuzzyCompare(m_speedMinMps + 1.0, v + 1.0))
            return;
        m_speedMinMps = v;
        setDirty();
    }
    /** @brief Set the upper color speed bound in m/s and notify on a change.
     * @param v New value. Stores the raw bound without validating order/finiteness.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setSpeedMaxMps(double v)
    {
        if (qFuzzyCompare(m_speedMaxMps + 1.0, v + 1.0))
            return;
        m_speedMaxMps = v;
        setDirty();
    }

    /*! VS.7 — colour for a speed magnitude (m/s). Flat color() when
     *  colorByMagnitude is off, else the named ramp over [min,max]. */
    /** @param speedMps Finite speed in m/s for ramp sampling.
     * @return Flat color() when magnitude coloring is off. Otherwise builds the named
     * builtin ramp over stored [min,max], using min+1 when max is not greater, and
     * delegates clipping/nonfinite handling to RasterColorRamp::colorForValue().
     * Includes ramp alpha without sublayer opacity; no shared scheme is consulted. */
    [[nodiscard]] QColor colorForSpeed(double speedMps) const;

    /** @brief Serialize the current style settings.
     * @return Independent object. Writes all arrow geometry, placement, filtering and color settings, using ARGB strings. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Read a partial style object and notify listeners.
     * @param j Borrowed JSON; unknown keys are ignored.
     * Absent/mistyped numeric/bool/string properties retain current values. Length,
     * spacing, head, shaft and depth apply the same finite-input clamps as their setters;
     * invalid color text is ignored. Speed bounds remain unchecked. Emits styleChanged
     * once unconditionally, without calling individual setters. */
    void fromJson(const QJsonObject &j) override;

private:
    double m_arrowLengthPx = 20.0;
    double m_arrowSpacingPx = 36.0;
    double m_headSizePx = 6.0;
    double m_shaftWidthPx = 1.6;
    QColor m_color = QColor(20, 20, 20, 230);
    QColor m_outlineColor = QColor(255, 255, 255, 200);
    double m_dryDepthCutoff = 0.01;
    bool m_placeAtCellCenters = false; // false = regular screen grid
    // VS.7 — colour-by-magnitude.
    bool m_colorByMagnitude = false;
    QString m_colorRampName = QStringLiteral("viridis");
    double m_speedMinMps = 0.0;
    double m_speedMaxMps = 2.0;
};

class FlowArrowSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a sublayer with a QObject-owned style and invalidation forwarding.
     * @param id_ Stable host ID moved into storage, without checking uniqueness.
     * @param parent Optional QObject owner; destroys this sublayer and style with itself.
     * All access is on the owning Qt thread; styleChanged forwards to invalidated(). */
    explicit FlowArrowSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify this rendering pass.
     * @return ArrowKind. */
    Kind kind() const override { return ArrowKind; }
    /** @brief Copy the immutable host identifier.
     * @return Construction-time id without validation. */
    QString id() const override { return m_id; }
    /** @brief Describe this pass for the UI.
     * @return Translated Flow direction arrows label. */
    QString displayName() const override { return tr("Flow direction arrows"); }

    /** @brief Read the local visibility flag.
     * @return Stored flag, initially false; parent/host visibility is not consulted. */
    bool isVisible() const override { return m_visible; }
    /** @brief Replace local visibility and invalidate on a Boolean change.
     * @param v New flag; no synchronous node construction occurs. */
    void setVisible(bool v) override;
    /** @brief Read the local opacity multiplier.
     * @return Stored multiplier, initially 1. */
    qreal opacity() const override { return m_opacity; }
    /** @brief Clamp and replace opacity, invalidating when it changes.
     * @param o Finite multiplier clamped to [0,1]; supply finite input because no
     * explicit NaN validation occurs. qFuzzyCompare(current+1, input+1) suppresses
     * unchanged values; otherwise emits invalidated(). */
    void setOpacity(qreal o) override;

    /** @brief Report that this pass can depend on animated result fields.
     * @return True; this does not itself start a worker or rebuild geometry. */
    bool isDynamic() const override { return true; }

    /** @brief Borrow the owned property bag through its base interface.
     * @return Child style owned by this sublayer; do not delete or reparent it. */
    SublayerStyle *style() override { return m_style; }

    /** @brief Build the pass's standalone legend preview.
     * @return One Flow direction row using MarkerLine, the flat color and fixed arrow length
     * as sizePx, with sublayer opacity. Magnitude coloring, outline/shaft settings and
     * visibility are not represented by this fallback swatch. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Preserve the existing scene-graph node without building geometry here.
     * @param existing Borrowed node returned unchanged, including null; no ownership transfer.
     * @param ctx Unused context; the host renderer owns actual geometry construction.
     * @return existing unconditionally. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Borrow the concrete owned style.
     * @return Child pointer valid until sublayer destruction; do not delete or reparent. */
    [[nodiscard]] FlowArrowStyle *flowArrowStyle() const { return m_style; }

private:
    QString m_id;
    bool m_visible = false; // off by default — opt-in alongside VelocityVector
    qreal m_opacity = 1.0;
    FlowArrowStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
