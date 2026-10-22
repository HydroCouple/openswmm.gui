/**
 * @file      contourbandsublayer.h
 * @brief     Dynamic marching-squares filled-contour-bands sublayer over a 2D mesh.
 *         Plan §3 — fifth sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.5.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_CONTOURBANDSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_CONTOURBANDSUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class ContourBandStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QString attribute READ attribute WRITE setAttribute NOTIFY styleChanged)
    Q_PROPERTY(int bandCount READ bandCount WRITE setBandCount NOTIFY styleChanged)
    // VS.8 — colour source: a named built-in/custom ramp ("" = use the
    // two-colour lowColor→highColor gradient), optionally inverted.
    Q_PROPERTY(QString colorRampName READ colorRampName WRITE setColorRampName NOTIFY styleChanged)
    Q_PROPERTY(bool invertRamp READ invertRamp WRITE setInvertRamp NOTIFY styleChanged)
    Q_PROPERTY(QColor lowColor READ lowColor WRITE setLowColor NOTIFY styleChanged)
    Q_PROPERTY(QColor highColor READ highColor WRITE setHighColor NOTIFY styleChanged)
    Q_PROPERTY(QColor belowMinColor READ belowMinColor WRITE setBelowMinColor NOTIFY styleChanged)
    Q_PROPERTY(QColor aboveMaxColor READ aboveMaxColor WRITE setAboveMaxColor NOTIFY styleChanged)
    Q_PROPERTY(bool smoothBands READ smoothBands WRITE setSmoothBands NOTIFY styleChanged)
    // VS.8 — fixed classification range. Off = auto-track the layer's
    // [dryDepth, maxDepth] range (legacy behaviour).
    Q_PROPERTY(bool useCustomRange READ useCustomRange WRITE setUseCustomRange NOTIFY styleChanged)
    Q_PROPERTY(double rangeMin READ rangeMin WRITE setRangeMin NOTIFY styleChanged)
    Q_PROPERTY(double rangeMax READ rangeMax WRITE setRangeMax NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute", "Classification")
    Q_CLASSINFO("group:bandCount", "Classification")
    Q_CLASSINFO("group:useCustomRange", "Classification")
    Q_CLASSINFO("group:rangeMin", "Classification")
    Q_CLASSINFO("group:rangeMax", "Classification")
    Q_CLASSINFO("group:colorRampName", "Ramp")
    Q_CLASSINFO("group:invertRamp", "Ramp")
    Q_CLASSINFO("group:lowColor", "Ramp")
    Q_CLASSINFO("group:highColor", "Ramp")
    Q_CLASSINFO("group:belowMinColor", "Out of range")
    Q_CLASSINFO("group:aboveMaxColor", "Out of range")
    Q_CLASSINFO("group:smoothBands", "Rendering")

public:
    // Slice US.2 — the classification knobs (band count, ramp, invert,
    // custom range) now live in a shared ClassificationScheme; the legacy
    // Q_PROPERTY accessors are pure forwards into it so the property grid +
    // every QSG / legend call site keep working while the scheme drives the
    // method-aware level edges.
    /** @brief Construct the style and its default settings.
     * @param parent Optional QObject owner; its destruction destroys this style.
     * Seeds Classified EqualInterval classification with eight viridis bands. All reads/edits occur on its owning Qt thread. */
    explicit ContourBandStyle(QObject *parent = nullptr);

    /** @brief Read the scalar attribute key.
     * @return Stored/forwarded value; initially "depth". */
    [[nodiscard]] QString attribute() const { return m_attribute; }
    /** @brief Read the classification band count.
     * @return Stored/forwarded value; initially 8. */
    [[nodiscard]] int bandCount() const { return m_scheme.classCount(); }
    /** @brief Read the scheme ramp-name token.
     * @return Stored/forwarded value; initially "viridis". */
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
    /** @brief Read the within-band gradient switch.
     * @return Stored/forwarded value; initially true. */
    [[nodiscard]] bool smoothBands() const { return m_smoothBands; }
    /** @brief Read the fixed-versus-data range switch.
     * @return Stored/forwarded value; initially false. */
    [[nodiscard]] bool useCustomRange() const { return m_scheme.useCustomRange(); }
    /** @brief Read the custom lower bound in the selected attribute unit.
     * @return Stored/forwarded value; initially scheme default. */
    [[nodiscard]] double rangeMin() const { return m_scheme.rangeMin(); }
    /** @brief Read the custom upper bound in the selected attribute unit.
     * @return Stored/forwarded value; initially scheme default. */
    [[nodiscard]] double rangeMax() const { return m_scheme.rangeMax(); }

    /** @brief Set the scalar attribute key and notify on a change.
     * @param v New value. Stores the exact string without checking availability.
     * Unchanged values according to exact equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setAttribute(const QString &v);
    /** @brief Set the classification band count and notify on a change.
     * @param v New value. Clamps to at least one and forwards to scheme.setClassCount(); does not switch mode.
     * Unchanged values according to integer equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setBandCount(int v);
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
    /** @brief Set the within-band gradient switch and notify on a change.
     * @param v New value. Stores the rendering hint without altering level edges or the classification scheme.
     * Unchanged values according to Boolean equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setSmoothBands(bool v);
    /** @brief Set the fixed-versus-data range switch and notify on a change.
     * @param v New value. Changes only the range-selection flag, retaining endpoints.
     * Unchanged values according to Boolean equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setUseCustomRange(bool v);
    /** @brief Set the custom lower bound in the selected attribute unit and notify on a change.
     * @param v New value. Stores the raw bound without validating order/finiteness or enabling custom range.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setRangeMin(double v);
    /** @brief Set the custom upper bound in the selected attribute unit and notify on a change.
     * @param v New value. Stores the raw bound without validating order/finiteness or enabling custom range.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setRangeMax(double v);

    /*! Slice US.2 — the embedded classification scheme (band classification
     *  method, class count, ramp, range, per-class overrides). */
    /** @brief Borrow the complete classification settings.
     * @return Scheme owned by this style, replaced by setScheme()/fromJson();
     * callers must not mutate it through a cast or retain elements across replacement. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    /** @brief Copy a complete classification configuration and notify on inequality.
     * @param s Source copied without validation. ClassificationScheme equality
     * decides whether to replace it; other style fields are retained. */
    void setScheme(const ClassificationScheme &s);

    /*! VS.8 — band colour at the midpoint of band \p bandIndex out of
     *  \p bandCount. Forwards to scheme().colorForClass — samples the named
     *  ramp (inverted when requested) or lerps lowColor→highColor when the
     *  ramp name is empty. Authored alpha is retained; callers apply the sublayer opacity. */
    /** @param bandIndex Zero-based band; negative yields transparent.
     * @param bandCount Nominal number of bands, normalized to at least one.
     * @return Scheme override when valid, otherwise midpoint ramp color with high
     * indices clamped to the final band. Authored alpha is retained; sublayer opacity
     * is applied separately. smoothBands does not affect this discrete helper. */
    [[nodiscard]] QColor colorForBand(int bandIndex, int bandCount) const;

    /** @brief Serialize the current style settings.
     * @return Independent object. Writes attribute, ARGB below/above colors, smoothBands and full classification JSON. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Read a partial style object and notify listeners.
     * @param j Borrowed JSON; unknown keys are ignored.
     * Absent/mistyped attribute/smoothBands retain current values. Invalid or empty
     * below/above color text is ignored. A present classification key replaces the
     * scheme via its reader (nonobject becomes empty object); absent classification
     * retains it. Legacy range/ramp keys are ignored. Emits styleChanged unconditionally. */
    void fromJson(const QJsonObject &j) override;

private:
    QString m_attribute = QStringLiteral("depth");
    QColor m_belowMinColor = QColor(0, 0, 0, 0);
    QColor m_aboveMaxColor = QColor(255, 255, 255, 200);
    bool m_smoothBands = true;
    ClassificationScheme m_scheme;
};

class ContourBandSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a sublayer with a QObject-owned style and invalidation forwarding.
     * @param id_ Stable host ID moved into storage, without checking uniqueness.
     * @param parent Optional QObject owner; destroys this sublayer and style with itself.
     * All access is on the owning Qt thread; styleChanged forwards to invalidated(). */
    explicit ContourBandSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify this rendering pass.
     * @return ContourBandKind. */
    Kind kind() const override { return ContourBandKind; }
    /** @brief Copy the immutable host identifier.
     * @return Construction-time id without validation. */
    QString id() const override { return m_id; }
    // Label tracks the classified attribute: the terrain mesh classifies bed
    // ELEVATION, the results layer classifies water DEPTH. Hardcoding "Depth"
    // mislabelled the terrain sublayer.
    /** @brief Describe this pass for the UI.
     * @return Translated Elevation Contours when the attribute equals elevation ignoring case,
     * otherwise Depth Contours. */
    QString displayName() const override
    {
        const QString a = m_style ? m_style->attribute() : QString();
        if (a.compare(QLatin1String("elevation"), Qt::CaseInsensitive) == 0)
            return tr("Elevation Contours");
        return tr("Depth Contours");
    }

    /** @brief Read the local visibility flag.
     * @return Stored flag, initially false; parent/host visibility is not consulted. */
    bool isVisible() const override { return m_visible; }
    /** @brief Replace local visibility and invalidate on a Boolean change.
     * @param v New flag; no synchronous node construction occurs. */
    void setVisible(bool v) override;
    /** @brief Read the local opacity multiplier.
     * @return Stored multiplier, initially 0.60. */
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
     * @return One row per max(1,bandCount), with midpoint scheme colors and sublayer opacity.
     * A custom range with max>min produces equal-interval labels/ranges; otherwise
     * labels are Band N. This fallback does not compute method-aware level edges or
     * apply label overrides; visibility does not suppress rows. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Preserve the existing scene-graph node without building geometry here.
     * @param existing Borrowed node returned unchanged, including null; no ownership transfer.
     * @param ctx Unused context; the host renderer owns actual geometry construction.
     * @return existing unconditionally. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Borrow the concrete owned style.
     * @return Child pointer valid until sublayer destruction; do not delete or reparent. */
    [[nodiscard]] ContourBandStyle *bandStyle() const { return m_style; }

private:
    QString m_id;
    bool m_visible = false; // off by default per plan §3
    qreal m_opacity = 0.60; // basemap stays visible under bands
    ContourBandStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
