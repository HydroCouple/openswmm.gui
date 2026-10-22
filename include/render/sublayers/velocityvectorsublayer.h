/**
 * @file      velocityvectorsublayer.h
 * @brief     Dynamic velocity-arrow glyph sublayer over a 2D mesh.
 *         Plan §3 — third sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.3.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_VELOCITYVECTORSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_VELOCITYVECTORSUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"
#include "render/colorramp.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class VelocityVectorStyle : public SublayerStyle
{
    Q_OBJECT
public:
    /*! VS.8 — how |v| maps to on-screen glyph length (before the min/max
     *  pixel clamps). Log compresses high-dynamic-range fields (legacy
     *  behaviour); Linear is the QGIS mesh-vector default; SquareRoot is
     *  the usual perceptual middle ground for quiver plots. */
    enum class LengthScaling { Linear = 0, SquareRoot = 1, Log = 2 };
    Q_ENUM(LengthScaling)

private:
    Q_PROPERTY(double glyphLengthScalePxPerMps READ glyphLengthScalePxPerMps WRITE
                   setGlyphLengthScalePxPerMps NOTIFY styleChanged)
    Q_PROPERTY(
        LengthScaling lengthScaling READ lengthScaling WRITE setLengthScaling NOTIFY styleChanged)
    Q_PROPERTY(
        double glyphLengthMinPx READ glyphLengthMinPx WRITE setGlyphLengthMinPx NOTIFY styleChanged)
    Q_PROPERTY(
        double glyphLengthMaxPx READ glyphLengthMaxPx WRITE setGlyphLengthMaxPx NOTIFY styleChanged)
    Q_PROPERTY(
        double glyphSpacingPx READ glyphSpacingPx WRITE setGlyphSpacingPx NOTIFY styleChanged)
    Q_PROPERTY(double headSizePx READ headSizePx WRITE setHeadSizePx NOTIFY styleChanged)
    Q_PROPERTY(double shaftWidthPx READ shaftWidthPx WRITE setShaftWidthPx NOTIFY styleChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(
        double dryDepthCutoff READ dryDepthCutoff WRITE setDryDepthCutoff NOTIFY styleChanged)
    // VS.7 — colour arrows by speed magnitude through a ramp.
    Q_PROPERTY(
        bool colorByMagnitude READ colorByMagnitude WRITE setColorByMagnitude NOTIFY styleChanged)
    Q_PROPERTY(QString colorRampName READ colorRampName WRITE setColorRampName NOTIFY styleChanged)
    Q_PROPERTY(double speedMinMps READ speedMinMps WRITE setSpeedMinMps NOTIFY styleChanged)
    Q_PROPERTY(double speedMaxMps READ speedMaxMps WRITE setSpeedMaxMps NOTIFY styleChanged)
    // VS.8 — 0 = continuous gradient; >= 2 = discretise the ramp into
    // colour bands (one legend row per band).
    Q_PROPERTY(
        int colorClassCount READ colorClassCount WRITE setColorClassCount NOTIFY styleChanged)

    Q_CLASSINFO("group:glyphLengthScalePxPerMps", "Glyph")
    Q_CLASSINFO("group:lengthScaling", "Glyph")
    Q_CLASSINFO("group:glyphLengthMinPx", "Glyph")
    Q_CLASSINFO("group:glyphLengthMaxPx", "Glyph")
    Q_CLASSINFO("group:glyphSpacingPx", "Placement")
    Q_CLASSINFO("group:headSizePx", "Glyph")
    Q_CLASSINFO("group:shaftWidthPx", "Glyph")
    Q_CLASSINFO("group:color", "Glyph")
    Q_CLASSINFO("group:dryDepthCutoff", "Filtering")
    Q_CLASSINFO("group:colorByMagnitude", "Colour")
    Q_CLASSINFO("group:colorRampName", "Colour")
    Q_CLASSINFO("group:speedMinMps", "Colour")
    Q_CLASSINFO("group:speedMaxMps", "Colour")
    Q_CLASSINFO("group:colorClassCount", "Colour")

public:
    /** @brief Construct the style and its default settings.
     * @param parent Optional QObject owner; its destruction destroys this style.
     * Seeds a Continuous viridis scheme over custom [0,2] m/s with five dormant classes. All reads/edits occur on its owning Qt thread. */
    explicit VelocityVectorStyle(QObject *parent = nullptr);

    /** @brief Read the glyph length-scale coefficient (pixels per m/s in Linear mode).
     * @return Stored/forwarded value; initially 20. */
    [[nodiscard]] double glyphLengthScalePxPerMps() const { return m_glyphLengthScalePxPerMps; }
    /** @brief Read the speed-to-length mode.
     * @return Stored/forwarded value; initially Log. */
    [[nodiscard]] LengthScaling lengthScaling() const { return m_lengthScaling; }
    /** @brief Read the minimum glyph length in logical screen pixels.
     * @return Stored/forwarded value; initially 20. */
    [[nodiscard]] double glyphLengthMinPx() const { return m_glyphLengthMinPx; }
    /** @brief Read the maximum glyph length in logical screen pixels.
     * @return Stored/forwarded value; initially 44. */
    [[nodiscard]] double glyphLengthMaxPx() const { return m_glyphLengthMaxPx; }
    /** @brief Read the glyph spacing in logical screen pixels.
     * @return Stored/forwarded value; initially 30. */
    [[nodiscard]] double glyphSpacingPx() const { return m_glyphSpacingPx; }
    /** @brief Read the arrowhead size in logical screen pixels.
     * @return Stored/forwarded value; initially 8. */
    [[nodiscard]] double headSizePx() const { return m_headSizePx; }
    /** @brief Read the arrow shaft width in logical screen pixels.
     * @return Stored/forwarded value; initially 2.5. */
    [[nodiscard]] double shaftWidthPx() const { return m_shaftWidthPx; }
    /** @brief Read the flat arrow color.
     * @return Stored/forwarded value; initially RGBA(20,20,20,220). */
    [[nodiscard]] QColor color() const { return m_color; }
    /** @brief Read the minimum rendered depth in metres.
     * @return Stored/forwarded value; initially 0.01. */
    [[nodiscard]] double dryDepthCutoff() const { return m_dryDepthCutoff; }
    /** @brief Read the speed-driven color switch.
     * @return Stored/forwarded value; initially true. */
    [[nodiscard]] bool colorByMagnitude() const { return m_colorByMagnitude; }
    // Slice US.2 — the ramp / speed range / class count knobs live in a shared
    // ClassificationScheme. The legacy accessors are pure forwards so
    // colorForSpeed(), the legend, the property grid, and JSON keep working.
    /** @brief Read the scheme ramp-name token.
     * @return Stored/forwarded value; initially "viridis". */
    [[nodiscard]] QString colorRampName() const { return m_scheme.rampName(); }
    /** @brief Read the scheme lower speed bound in m/s.
     * @return Stored/forwarded value; initially 0. */
    [[nodiscard]] double speedMinMps() const { return m_scheme.rangeMin(); }
    /** @brief Read the scheme upper speed bound in m/s.
     * @return Stored/forwarded value; initially 2. */
    [[nodiscard]] double speedMaxMps() const { return m_scheme.rangeMax(); }
    /** @brief Read the active color-class count.
     * @return Stored/forwarded value; initially 0 (Continuous mode; stored class count five). */
    [[nodiscard]] int colorClassCount() const
    {
        return m_scheme.mode() == ClassificationScheme::ClassMode::Classified
                   ? m_scheme.classCount()
                   : 0;
    }

    /** @brief Set the glyph length-scale coefficient (pixels per m/s in Linear mode) and notify on a change.
     * @param v New value. Negative input clamps to zero; NaN is not sanitized.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setGlyphLengthScalePxPerMps(double v);
    /** @brief Set the speed-to-length mode and notify on a change.
     * @param v New value. Stores the enum without validating its integer value.
     * Unchanged values according to enum equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setLengthScaling(LengthScaling v);
    /** @brief Set the minimum glyph length in logical screen pixels and notify on a change.
     * @param v New value. Negative input clamps to zero; does not reorder the maximum. NaN is not sanitized.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setGlyphLengthMinPx(double v);
    /** @brief Set the maximum glyph length in logical screen pixels and notify on a change.
     * @param v New value. Negative input clamps to zero; does not reorder the minimum. NaN is not sanitized.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setGlyphLengthMaxPx(double v);
    /** @brief Set the glyph spacing in logical screen pixels and notify on a change.
     * @param v New value. Values below one clamp to one; NaN is not sanitized.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setGlyphSpacingPx(double v);
    /** @brief Set the arrowhead size in logical screen pixels and notify on a change.
     * @param v New value. Negative input clamps to zero; NaN is not sanitized.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setHeadSizePx(double v);
    /** @brief Set the arrow shaft width in logical screen pixels and notify on a change.
     * @param v New value. Values below 0.1 clamp to 0.1; NaN is not sanitized.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setShaftWidthPx(double v);
    /** @brief Set the flat arrow color and notify on a change.
     * @param v New value. Accepts invalid QColor values without validation.
     * Unchanged values according to QColor equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setColor(const QColor &v);
    /** @brief Set the minimum rendered depth in metres and notify on a change.
     * @param v New value. Negative input clamps to zero; NaN is not sanitized.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setDryDepthCutoff(double v);
    /** @brief Set the speed-driven color switch and notify on a change.
     * @param v New value. Changes only color selection, retaining the flat color and scheme.
     * Unchanged values according to Boolean equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setColorByMagnitude(bool v);
    /** @brief Set the scheme ramp-name token and notify on a change.
     * @param v New value. Forwards the string to the scheme without validating catalog availability.
     * Unchanged values according to exact string equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setColorRampName(const QString &v);
    /** @brief Set the scheme lower speed bound in m/s and notify on a change.
     * @param v New value. When changed, enables custom range and stores the raw bound without order/finiteness validation. An equal bound leaves the range flag unchanged.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setSpeedMinMps(double v);
    /** @brief Set the scheme upper speed bound in m/s and notify on a change.
     * @param v New value. When changed, enables custom range and stores the raw bound without order/finiteness validation. An equal bound leaves the range flag unchanged.
     * Unchanged values according to qFuzzyCompare(current+1, input+1) are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setSpeedMaxMps(double v);
    /** @brief Set the active color-class count and notify on a change.
     * @param v New value. Values below two switch to Continuous while retaining the stored count; two or more switch to Classified and forward the count to the scheme.
     * Unchanged values according to mode and count equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setColorClassCount(int v);

    /*! Slice US.2 — the embedded classification scheme (display mode, ramp,
     *  class count, speed range, per-class overrides). */
    /** @brief Borrow the complete classification settings.
     * @return Scheme owned by this style, replaced by setScheme()/fromJson();
     * callers must not mutate it through a cast or retain elements across replacement. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    /** @brief Copy a complete classification configuration and notify on inequality.
     * @param s Source copied without validation. ClassificationScheme equality
     * decides whether to replace it; other style fields are retained. */
    void setScheme(const ClassificationScheme &s);

    /*! VS.7 — colour for a given speed magnitude (m/s). When
     *  colorByMagnitude is off, returns the flat `color()`. Otherwise samples
     *  the named ramp over [speedMinMps, speedMaxMps]; VS.8 — when
     *  colorClassCount >= 2 the sample position is quantised to the band
     *  midpoint so arrows render in discrete colour bands. */
    /** @param speedMps Speed in m/s. Flat mode returns color() even for invalid input.
     * @return In magnitude mode, delegates to scheme classification or continuous sampling;
     * classified out-of-range/nonfinite values are transparent when classIndexFor()
     * returns -1. Uses stored speed bounds, with a nominal max fallback of min+1;
     * the scheme's custom-range settings can still take precedence. No opacity applied. */
    [[nodiscard]] QColor colorForSpeed(double speedMps) const;

    /*! VS.8 — glyph length in pixels for a speed magnitude: applies the
     *  scaling mode then clamps to [glyphLengthMinPx, glyphLengthMaxPx].
     *  Returns 0 for non-positive speeds. */
    /** @param speedMps Finite speed in m/s; <=0 returns zero before applying length clamps.
     * @return Scale times speed, sqrt(speed) or log1p(speed), clamped between sorted
     * min/max pixel lengths. When both bounds are zero, the computed positive length
     * is retained. An unchecked unknown mode starts from zero before clamping.
     * No finite-value validation occurs; provide finite configuration fields. */
    [[nodiscard]] double glyphLengthPxForSpeed(double speedMps) const;

    /** @brief Serialize the current style settings.
     * @return Independent object. Writes glyph, color and filtering fields, full classification JSON and legacy
     * color-ramp/range/class-count forwarders for older readers. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Read a partial style object and notify listeners.
     * @param j Borrowed JSON; unknown keys are ignored.
     * Absent/mistyped fields retain current values. Negative scale/min/max/head/depth
     * clamp to zero, spacing to at least one, shaft width to at least 0.1. Invalid color
     * text is ignored and out-of-enum lengthScaling integers retain the current mode.
     * A present classification key replaces the scheme through its reader, including
     * nonobjects as empty objects; otherwise legacy keys go through setters and may
     * emit intermediate styleChanged signals. A final styleChanged is unconditional. */
    void fromJson(const QJsonObject &j) override;

private:
    double m_glyphLengthScalePxPerMps = 20.0;
    LengthScaling m_lengthScaling = LengthScaling::Log;
    double m_glyphLengthMinPx = 20.0;
    double m_glyphLengthMaxPx = 44.0;
    double m_glyphSpacingPx = 30.0;
    double m_headSizePx = 8.0;
    double m_shaftWidthPx = 2.5;
    QColor m_color = QColor(20, 20, 20, 220);
    double m_dryDepthCutoff = 0.01;
    // VS.7 — colour-by-magnitude. VS.8 — default ON: the legacy painter
    // always encoded |v| as colour, so a flat default would regress it.
    bool m_colorByMagnitude = true;
    // Slice US.2 — ramp / speed range / class count consolidated here. The ctor
    // seeds it to the legacy defaults (Continuous, viridis, [0,2], 5 classes).
    ClassificationScheme m_scheme;
};

class VelocityVectorSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a sublayer with a QObject-owned style and invalidation forwarding.
     * @param id_ Stable host ID moved into storage, without checking uniqueness.
     * @param parent Optional QObject owner; destroys this sublayer and style with itself.
     * All access is on the owning Qt thread; styleChanged forwards to invalidated(). */
    explicit VelocityVectorSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify this rendering pass.
     * @return VectorGlyphKind. */
    Kind kind() const override { return VectorGlyphKind; }
    /** @brief Copy the immutable host identifier.
     * @return Construction-time id without validation. */
    QString id() const override { return m_id; }
    /** @brief Describe this pass for the UI.
     * @return Translated Flow Velocity label. */
    QString displayName() const override { return tr("Flow Velocity"); }

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
     * @return Flat mode returns one Velocity arrow row. Magnitude mode returns a Velocity (m/s)
     * header plus active-class-count rows (or five continuous previews). Labels/ranges
     * are equal subdivisions of stored speed bounds even when the scheme method is
     * nonlinear/manual; midpoint colors are sampled separately. Each swatch uses head
     * size and sublayer opacity. Visibility does not suppress rows. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Preserve the existing scene-graph node without building geometry here.
     * @param existing Borrowed node returned unchanged, including null; no ownership transfer.
     * @param ctx Unused context; the host renderer owns actual geometry construction.
     * @return existing unconditionally. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Borrow the concrete owned style.
     * @return Child pointer valid until sublayer destruction; do not delete or reparent. */
    [[nodiscard]] VelocityVectorStyle *vectorStyle() const { return m_style; }

private:
    QString m_id;
    bool m_visible = false; // default off per plan §3
    qreal m_opacity = 1.0;
    VelocityVectorStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
