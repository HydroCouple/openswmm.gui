// GPL-3.0-or-later

/**
 * @file      flowstreaksublayer.h
 * @brief     Styling and display of flow streak sublayers.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "render/isublayer.h"
#include "render/sublayerstyle.h"
#include "render/classificationscheme.h"
#include <QColor>
#include <limits>
namespace OpenSWMM::Render
{
class FlowStreakStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_CLASSINFO("group:color", "Appearance")
    Q_PROPERTY(double widthPx READ widthPx WRITE setWidthPx NOTIFY styleChanged)
    Q_CLASSINFO("group:widthPx", "Appearance")
    Q_PROPERTY(double headWidthPx READ headWidthPx WRITE setHeadWidthPx NOTIFY styleChanged)
    Q_CLASSINFO("group:headWidthPx", "Appearance")
    Q_PROPERTY(double trailDurationSeconds READ trailDurationSeconds WRITE setTrailDurationSeconds
                   NOTIFY styleChanged)
    Q_CLASSINFO("group:trailDurationSeconds", "Appearance")
    Q_PROPERTY(double spacingPx READ spacingPx WRITE setSpacingPx NOTIFY styleChanged)
    Q_CLASSINFO("group:spacingPx", "Distribution")
    Q_PROPERTY(bool animated READ animated WRITE setAnimated NOTIFY styleChanged)
    Q_CLASSINFO("group:animated", "Motion")
    Q_PROPERTY(
        double speedMultiplier READ speedMultiplier WRITE setSpeedMultiplier NOTIFY styleChanged)
    Q_CLASSINFO("group:speedMultiplier", "Motion")
    Q_PROPERTY(double minimumLifetimeSeconds READ minimumLifetimeSeconds WRITE
                   setMinimumLifetimeSeconds NOTIFY styleChanged)
    Q_CLASSINFO("group:minimumLifetimeSeconds", "Motion")
    Q_PROPERTY(double maximumLifetimeSeconds READ maximumLifetimeSeconds WRITE
                   setMaximumLifetimeSeconds NOTIFY styleChanged)
    Q_CLASSINFO("group:maximumLifetimeSeconds", "Motion")
    Q_PROPERTY(double lifetimeFadeSeconds READ lifetimeFadeSeconds WRITE setLifetimeFadeSeconds
                   NOTIFY styleChanged)
    Q_CLASSINFO("group:lifetimeFadeSeconds", "Motion")
    Q_PROPERTY(
        bool colorByMagnitude READ colorByMagnitude WRITE setColorByMagnitude NOTIFY styleChanged)
    Q_CLASSINFO("group:colorByMagnitude", "Colour")
public:
    /** @brief Construct flat white streaks and a Continuous viridis speed scheme over [0,2].
     * @param parent Optional QObject owner; destroys this style with itself. Ordinary
     * reads/edits occur on the owning Qt thread. */
    explicit FlowStreakStyle(QObject *parent = nullptr);
    // Rendering uniforms use floats; impose only their numeric range, not
    // arbitrary tuning ceilings. Keep editors and persisted values consistent.
    /** @brief Expose the shared rendering-uniform numeric ceiling.
     * @return Maximum finite float, represented as double; not an application tuning recommendation. */
    static constexpr double maximumNumericValue() { return std::numeric_limits<float>::max(); }
    /** @brief Read the flat streak color.
     * @return Stored value, initially RGBA(255,255,255,217). */
    QColor color() const { return m_color; }
    /** @brief Set the flat streak color and notify on a change.
     * @param value New value. Ignores invalid QColor values; exact QColor equality suppresses notification.
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setColor(QColor value);
    /** @brief Read the trail-body width in logical screen pixels.
     * @return Stored value, initially 1.5. */
    double widthPx() const { return m_widthPx; }
    /** @brief Set the trail-body width in logical screen pixels and notify on a change.
     * @param value New value. Ignores nonfinite input and clamps to [0.1,maximumNumericValue()].
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setWidthPx(double value);
    /** @brief Read the independent head width in logical screen pixels.
     * @return Stored value, initially 2.5. */
    double headWidthPx() const { return m_headWidthPx; }
    /** @brief Set the independent head width in logical screen pixels and notify on a change.
     * @param value New value. Ignores nonfinite input and clamps to [0.1,maximumNumericValue()].
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setHeadWidthPx(double value);
    /** @brief Evaluate body taper and the independent smoothed head-width transition.
     * @param tailToHead Finite normalized trail coordinate, clamped to [0,1].
     * @return Logical pixel width: body has at least a quarter-width tail; last quarter
     * blends toward headWidthPx using smoothstep. No explicit NaN check is performed. */
    double widthAtFraction(double tailToHead) const;
    /** @brief Read the visible trail duration in flow seconds.
     * @return Stored value, initially 1.5. */
    double trailDurationSeconds() const { return m_trailDurationSeconds; }
    /** @brief Set the visible trail duration in flow seconds and notify on a change.
     * @param value New value. Ignores nonfinite input and clamps to [0.1,maximumNumericValue()].
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setTrailDurationSeconds(double value);
    /** @brief Read the target seed spacing in logical screen pixels.
     * @return Stored value, initially 40. */
    double spacingPx() const { return m_spacingPx; }
    /** @brief Set the target seed spacing in logical screen pixels and notify on a change.
     * @param value New value. Ignores nonfinite input and clamps to [2,maximumNumericValue()].
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setSpacingPx(double value);
    /** @brief Read the animation switch.
     * @return Stored value, initially true. */
    bool animated() const { return m_animated; }
    /** @brief Set the animation switch and notify on a change.
     * @param value New value. Stores the Boolean without changing particle state here.
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setAnimated(bool value);
    /** @brief Read the display motion gain.
     * @return Stored value, initially 1. */
    double speedMultiplier() const { return m_speedMultiplier; }
    /** @brief Inspect whether the last JSON load applied legacy motion presets.
     * @return Transient flag, initially false; not persisted and cleared by an accepted
     * setSpeedMultiplier() call, even when that call leaves the multiplier unchanged. */
    bool motionWasMigrated() const { return m_motionWasMigrated; }
    /** @brief Set the display motion gain and notify on a change.
     * @param value New value. Ignores nonfinite input and clamps to [0,maximumNumericValue()]. Clears motionWasMigrated even when the stored value is equal, without a signal in that equal-value case.
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setSpeedMultiplier(double value);
    /** @brief Read the minimum assigned lifetime in flow seconds.
     * @return Stored value, initially 20. */
    double minimumLifetimeSeconds() const { return m_minimumLifetimeSeconds; }
    /** @brief Set the minimum assigned lifetime in flow seconds and notify on a change.
     * @param value New value. Ignores nonfinite input and clamps to [0.1,maximumNumericValue()]. On change also raises the maximum to preserve maximum>=minimum.
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setMinimumLifetimeSeconds(double value);
    /** @brief Read the maximum assigned lifetime in flow seconds.
     * @return Stored value, initially 40. */
    double maximumLifetimeSeconds() const { return m_maximumLifetimeSeconds; }
    /** @brief Set the maximum assigned lifetime in flow seconds and notify on a change.
     * @param value New value. Ignores nonfinite input and clamps to [0.1,maximumNumericValue()]. On change also lowers the minimum to preserve minimum<=maximum.
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setMaximumLifetimeSeconds(double value);
    /** @brief Read the post-stop fade duration in flow seconds.
     * @return Stored value, initially 0.6. */
    double lifetimeFadeSeconds() const { return m_lifetimeFadeSeconds; }
    /** @brief Set the post-stop fade duration in flow seconds and notify on a change.
     * @param value New value. Ignores nonfinite input and clamps to [0,maximumNumericValue()].
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setLifetimeFadeSeconds(double value);
    /** @brief Read the speed-driven color switch.
     * @return Stored value, initially false. */
    bool colorByMagnitude() const { return m_colorByMagnitude; }
    /** @brief Set the speed-driven color switch and notify on a change.
     * @param value New value. Stores the Boolean while retaining flat color and scheme.
     * Accepted unequal values emit styleChanged; no particle work is run synchronously. */
    void setColorByMagnitude(bool value);
    /** @brief Borrow the owned speed-color classification settings.
     * @return Scheme replaced by setScheme(); this accessor does not permit mutation. */
    const ClassificationScheme &scheme() const { return m_scheme; }
    /** @brief Copy speed-color settings when their serialized objects differ.
     * @param value Source copied without validation; equality is based on toJson(),
     * so serialized differences trigger styleChanged. Other appearance fields remain. */
    void setScheme(const ClassificationScheme &value);
    /** @brief Select flat or scheme-driven color for a physical speed.
     * @param speed Speed in m/s; flat mode returns the stored color for any value.
     * @return Classified scheme color when valid edges exist, otherwise continuous
     * scheme sampling over stored range endpoints. Nonfinite/below-range input follows
     * scheme transparency rules; no sublayer opacity is applied. */
    QColor colorForSpeed(double speed) const;
    /** @brief Serialize current appearance and full speed classification at schema version five.
     * @return Independent object with ARGB color; transient migration flag is omitted. */
    QJsonObject toJson() const override;
    /** @brief Load appearance, apply legacy motion migration and notify once at the end.
     * @param j Borrowed object; present properties use their validated setters while
     * intermediate signals are blocked. Missing head width adopts a present body width.
     * The current implementation calls static ClassificationScheme::fromJson() but
     * discards its returned value, so a classification key does not replace m_scheme.
     * Nonempty legacy objects (schemaVersion<4 or continuousParticles=false) snap spacing
     * to 56/40/28 and eligible trail duration to 0.75/1.5/3, otherwise 1.5, then reset
     * speed gain to one and set motionWasMigrated. Unknown keys are ignored; empty
     * objects retain appearance but clear the transient migration flag. Final styleChanged
     * is unconditional. Lifetime setter ordering can adjust both minimum and maximum. */
    void fromJson(const QJsonObject &j) override;

private:
    ClassificationScheme m_scheme;
    QColor m_color = QColor(255, 255, 255, 217);
    double m_widthPx = 1.5;
    double m_headWidthPx = 2.5;
    double m_trailDurationSeconds = 1.5;
    double m_spacingPx = 40.0;
    bool m_animated = true;
    double m_speedMultiplier = 1.0;
    bool m_motionWasMigrated = false;
    double m_minimumLifetimeSeconds = 20.0;
    double m_maximumLifetimeSeconds = 40.0;
    double m_lifetimeFadeSeconds = 0.6;
    bool m_colorByMagnitude = false;
};
class FlowStreakSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct an initially hidden, opacity-one dynamic streak pass and owned style.
     * @param parent Optional QObject owner; styleChanged forwards to invalidated().
     * Ordinary access occurs on the owning Qt thread. */
    explicit FlowStreakSublayer(QObject *parent = nullptr);
    /** @brief Identify this scene pass.
     * @return LineKind. */
    Kind kind() const override { return LineKind; }
    /** @brief Read the fixed persisted pass identifier.
     * @return Exact token results2d.flowStreaks. */
    QString id() const override { return QStringLiteral("results2d.flowStreaks"); }
    /** @brief Describe the pass for the UI.
     * @return Translated Flow Streaks label. */
    QString displayName() const override { return tr("Flow Streaks"); }
    /** @brief Read local visibility.
     * @return Stored flag, initially false. */
    bool isVisible() const override { return m_visible; }
    /** @brief Replace visibility and emit invalidated() on a Boolean change.
     * @param v New flag; no synchronous worker/geometry operation occurs. */
    void setVisible(bool v) override;
    /** @brief Read local opacity.
     * @return Finite multiplier in [0,1], initially one. */
    qreal opacity() const override { return m_opacity; }
    /** @brief Set finite clamped opacity and invalidate on exact inequality.
     * @param v Nonfinite values are ignored; finite values clamp to [0,1]. */
    void setOpacity(qreal v) override;
    /** @brief Report that streaks depend on animated hydraulics.
     * @return True; does not itself advance the particle worker. */
    bool isDynamic() const override { return true; }
    /** @brief Borrow the owned streak style through its base interface.
     * @return Child pointer valid until destruction; do not delete/reparent. */
    SublayerStyle *style() override { return m_style; }
    /** @brief Borrow the concrete owned streak style.
     * @return Child pointer valid until destruction; do not delete/reparent. */
    FlowStreakStyle *streakStyle() const { return m_style; }
    /** @brief Build one flat-color or five equally spaced speed-color line previews.
     * @return Independent rows with fixed id, body width and local opacity. Magnitude
     * labels use stored speed endpoints in m/s even for classified/nonlinear schemes;
     * head taper, lifetimes and visibility are not represented. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Preserve the host-built scene node.
     * @param existing Borrowed node returned unchanged, including null.
     * @par Context
     * The unnamed SublayerContext reference is unused.
     * @return existing without allocation or ownership transfer. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override
    {
        return existing;
    }

private:
    FlowStreakStyle *m_style;
    bool m_visible = false;
    qreal m_opacity = 1.;
};
} // namespace OpenSWMM::Render
