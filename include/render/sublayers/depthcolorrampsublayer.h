/**
 * @file      depthcolorrampsublayer.h
 * @brief     Dynamic graduated colour-ramp fill over a 2D mesh.
 *         Plan §3 — second sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.2.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_DEPTHCOLORRAMPSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_DEPTHCOLORRAMPSUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class DepthColorRampStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QString attribute READ attribute WRITE setAttribute NOTIFY styleChanged)
    Q_PROPERTY(double minValue READ minValue WRITE setMinValue NOTIFY styleChanged)
    Q_PROPERTY(double maxValue READ maxValue WRITE setMaxValue NOTIFY styleChanged)
    // VS.8 — named built-in/custom ramp ("" = two-colour low→high
    // gradient, the legacy default), optionally inverted.
    Q_PROPERTY(QString colorRampName READ colorRampName WRITE setColorRampName NOTIFY styleChanged)
    Q_PROPERTY(bool invertRamp READ invertRamp WRITE setInvertRamp NOTIFY styleChanged)
    Q_PROPERTY(QColor lowColor READ lowColor WRITE setLowColor NOTIFY styleChanged)
    Q_PROPERTY(QColor highColor READ highColor WRITE setHighColor NOTIFY styleChanged)
    Q_PROPERTY(QColor belowMinColor READ belowMinColor WRITE setBelowMinColor NOTIFY styleChanged)
    Q_PROPERTY(QColor aboveMaxColor READ aboveMaxColor WRITE setAboveMaxColor NOTIFY styleChanged)
    Q_PROPERTY(bool useLogScale READ useLogScale WRITE setUseLogScale NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute", "Classification")
    Q_CLASSINFO("group:minValue", "Range")
    Q_CLASSINFO("group:maxValue", "Range")
    Q_CLASSINFO("group:colorRampName", "Ramp")
    Q_CLASSINFO("group:invertRamp", "Ramp")
    Q_CLASSINFO("group:lowColor", "Ramp")
    Q_CLASSINFO("group:highColor", "Ramp")
    Q_CLASSINFO("group:belowMinColor", "Out of range")
    Q_CLASSINFO("group:aboveMaxColor", "Out of range")
    Q_CLASSINFO("group:useLogScale", "Range")

public:
    // Slice US.2 — the ramp/range/class knobs live in a shared
    // ClassificationScheme (mode Continuous = smooth Gouraud, Classified =
    // graduated). The legacy accessors are pure forwards so the property grid
    // and every renderer / legend call site keep working.
    /** @brief Construct the style and its default settings.
     * @param parent Optional QObject owner; its destruction destroys this style.
     * Seeds a Continuous two-color blue scheme over [0,1] with five stored classes. All reads/edits occur on its owning Qt thread. */
    explicit DepthColorRampStyle(QObject *parent = nullptr);

    /** @brief Read the scalar attribute key.
     * @return Stored/forwarded value; initially "depth". */
    [[nodiscard]] QString attribute() const { return m_attribute; }
    /** @brief Read the scheme lower bound in the selected attribute unit.
     * @return Stored/forwarded value; initially 0. */
    [[nodiscard]] double minValue() const { return m_scheme.rangeMin(); }
    /** @brief Read the scheme upper bound in the selected attribute unit.
     * @return Stored/forwarded value; initially 1. */
    [[nodiscard]] double maxValue() const { return m_scheme.rangeMax(); }
    /** @brief Read the scheme ramp-name token.
     * @return Stored/forwarded value; initially empty (two-color blue gradient). */
    [[nodiscard]] QString colorRampName() const { return m_scheme.rampName(); }
    /** @brief Read the ramp inversion switch.
     * @return Stored/forwarded value; initially false. */
    [[nodiscard]] bool invertRamp() const { return m_scheme.invertRamp(); }
    /** @brief Read the two-color ramp lower endpoint.
     * @return Stored/forwarded value; initially RGBA(60,100,200,200). */
    [[nodiscard]] QColor lowColor() const { return m_scheme.lowColor(); }
    /** @brief Read the two-color ramp upper endpoint.
     * @return Stored/forwarded value; initially RGBA(200,220,255,200). */
    [[nodiscard]] QColor highColor() const { return m_scheme.highColor(); }
    /** @brief Read the below-range fallback color.
     * @return Stored/forwarded value; initially transparent. */
    [[nodiscard]] QColor belowMinColor() const { return m_belowMinColor; }
    /** @brief Read the above-range fallback color.
     * @return Stored/forwarded value; initially RGBA(255,255,255,200). */
    [[nodiscard]] QColor aboveMaxColor() const { return m_aboveMaxColor; }
    /** @brief Read the logarithmic-range rendering hint.
     * @return Stored/forwarded value; initially false. */
    [[nodiscard]] bool useLogScale() const { return m_useLogScale; }

    /** @brief Set the scalar attribute key and notify on a change.
     * @param v New value. Stores the exact string without checking availability.
     * Unchanged values according to exact equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setAttribute(const QString &v);
    /** @brief Set the scheme lower bound in the selected attribute unit and notify on a change.
     * @param v New value. Stores the raw bound without validating order/finiteness or enabling custom range.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setMinValue(double v);
    /** @brief Set the scheme upper bound in the selected attribute unit and notify on a change.
     * @param v New value. Stores the raw bound without validating order/finiteness or enabling custom range.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setMaxValue(double v);
    /** @brief Set the scheme ramp-name token and notify on a change.
     * @param v New value. Stores the token verbatim; empty selects the two-color path. Lookup occurs when sampled.
     * Unchanged values according to exact string equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setColorRampName(const QString &v);
    /** @brief Set the ramp inversion switch and notify on a change.
     * @param v New value. Changes inversion without reversing stored endpoints or overrides.
     * Unchanged values according to Boolean equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setInvertRamp(bool v);
    /** @brief Set the two-color ramp lower endpoint and notify on a change.
     * @param v New value. Accepts invalid QColor values without validation.
     * Unchanged values according to QColor equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setLowColor(const QColor &v);
    /** @brief Set the two-color ramp upper endpoint and notify on a change.
     * @param v New value. Accepts invalid QColor values without validation.
     * Unchanged values according to QColor equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setHighColor(const QColor &v);
    /** @brief Set the below-range fallback color and notify on a change.
     * @param v New value. Accepts invalid QColor values; does not change the classification scheme.
     * Unchanged values according to QColor equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setBelowMinColor(const QColor &v);
    /** @brief Set the above-range fallback color and notify on a change.
     * @param v New value. Accepts invalid QColor values; does not change the classification scheme.
     * Unchanged values according to QColor equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setAboveMaxColor(const QColor &v);
    /** @brief Set the logarithmic-range rendering hint and notify on a change.
     * @param v New value. Stores the switch; colorAtF() itself does not apply logarithmic normalization.
     * Unchanged values according to Boolean equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setUseLogScale(bool v);

    /*! Slice US.2 — the embedded classification scheme (display mode, ramp,
     *  class count, range, per-class overrides). */
    /** @brief Borrow the complete classification settings.
     * @return Scheme owned by this style, replaced by setScheme()/fromJson();
     * callers must not mutate it through a cast or retain elements across replacement. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    /** @brief Copy a complete classification configuration and notify on inequality.
     * @param s Source copied without validation. ClassificationScheme equality
     * decides whether to replace it; other style fields are retained. */
    void setScheme(const ClassificationScheme &s);

    /*! VS.8 / US.2 — colour at normalised ramp position \p f in [0,1]:
     *  forwards to scheme().colorAtF — samples the named ramp (inverted when
     *  requested) or lerps lowColor→highColor when the ramp name is empty. */
    /** @param f Finite normalized ramp position; scheme clamps to [0,1] and applies inversion.
     * @return Scheme color including authored alpha, without log normalization,
     * below/above fallback colors, class overrides or sublayer opacity. */
    [[nodiscard]] QColor colorAtF(double f) const;

    /** @brief Serialize the current style settings.
     * @return Independent object. Writes attribute, ARGB below/above colors, log hint and full classification JSON.
     * Legacy top-level range/ramp keys are not written. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Read a partial style object and notify listeners.
     * @param j Borrowed JSON; unknown keys are ignored.
     * Absent/mistyped attribute/log fields retain current values. Invalid or empty
     * below/above color text is ignored. A present classification key replaces the
     * scheme via its reader (nonobject becomes empty object); absent classification
     * retains it. Legacy range/ramp keys are ignored. Emits styleChanged unconditionally. */
    void fromJson(const QJsonObject &j) override;

private:
    QString m_attribute = QStringLiteral("depth");
    QColor m_belowMinColor = QColor(0, 0, 0, 0); // transparent (dry)
    QColor m_aboveMaxColor = QColor(255, 255, 255, 200);
    bool m_useLogScale = false;
    ClassificationScheme m_scheme;
};

class DepthColorRampSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a sublayer with a QObject-owned style and invalidation forwarding.
     * @param id_ Stable host ID moved into storage, without checking uniqueness.
     * @param parent Optional QObject owner; destroys this sublayer and style with itself.
     * All access is on the owning Qt thread; styleChanged forwards to invalidated(). */
    explicit DepthColorRampSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify this rendering pass.
     * @return ColorRampFillKind. */
    Kind kind() const override { return ColorRampFillKind; }
    /** @brief Copy the immutable host identifier.
     * @return Construction-time id without validation. */
    QString id() const override { return m_id; }
    /** @brief Describe this pass for the UI.
     * @return Translated Depth color ramp label. */
    QString displayName() const override { return tr("Depth color ramp"); }

    /** @brief Read the local visibility flag.
     * @return Stored flag, initially true; parent/host visibility is not consulted. */
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
     * @return Two bookend fill rows with raw attribute/min/max labels, endpoint colors and
     * sublayer opacity. Named/inverted ramps, classification mode, log hint and out-of-
     * range colors are not sampled; hidden sublayers still return rows. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Preserve the existing scene-graph node without building geometry here.
     * @param existing Borrowed node returned unchanged, including null; no ownership transfer.
     * @param ctx Unused context; the host renderer owns actual geometry construction.
     * @return existing unconditionally. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Borrow the concrete owned style.
     * @return Child pointer valid until sublayer destruction; do not delete or reparent. */
    [[nodiscard]] DepthColorRampStyle *rampStyle() const { return m_style; }

private:
    QString m_id;
    bool m_visible = true;
    qreal m_opacity = 1.0;
    DepthColorRampStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
