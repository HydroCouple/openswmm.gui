/**
 * @file      scalarfillsublayer.h
 * @brief     Per-cell (flat) and per-vertex (Gouraud) scalar-fill sublayers over a
 *         2D mesh. Unlike ContourBandSublayer (which marching-squares the field
 *         into discrete band polygons), these fill the existing triangle mesh
 *         directly:
 *           - CellDepthFillSublayer    — one flat colour per cell (cell-centre
 *             value), classified or continuous.
 *           - SmoothDepthFillSublayer  — a colour per mesh vertex, interpolated
 *             across each triangle by the GPU (Gouraud). Continuous gives a
 *             seam-free gradient; classified bins each vertex.
 *         Both reuse the shared ScalarFillStyle (attribute + ClassificationScheme
 *         whose ClassMode is the Continuous/Classified toggle).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_SCALARFILLSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_SCALARFILLSUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

/*! Shared style for the two scalar-fill sublayers. The Continuous/Classified
 *  toggle is the embedded ClassificationScheme's ClassMode; in Continuous mode
 *  the renderer samples the ramp by raw value (colorForValue), in Classified
 *  mode it bins by class (colorForClass). */
class ScalarFillStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QString attribute READ attribute WRITE setAttribute NOTIFY styleChanged)
    Q_PROPERTY(bool classified READ classified WRITE setClassified NOTIFY styleChanged)
    Q_PROPERTY(int bandCount READ bandCount WRITE setBandCount NOTIFY styleChanged)
    Q_PROPERTY(QString colorRampName READ colorRampName WRITE setColorRampName NOTIFY styleChanged)
    Q_PROPERTY(bool invertRamp READ invertRamp WRITE setInvertRamp NOTIFY styleChanged)
    Q_PROPERTY(QColor lowColor READ lowColor WRITE setLowColor NOTIFY styleChanged)
    Q_PROPERTY(QColor highColor READ highColor WRITE setHighColor NOTIFY styleChanged)
    Q_PROPERTY(bool useCustomRange READ useCustomRange WRITE setUseCustomRange NOTIFY styleChanged)
    Q_PROPERTY(double rangeMin READ rangeMin WRITE setRangeMin NOTIFY styleChanged)
    Q_PROPERTY(double rangeMax READ rangeMax WRITE setRangeMax NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute", "Classification")
    Q_CLASSINFO("group:classified", "Classification")
    Q_CLASSINFO("group:bandCount", "Classification")
    Q_CLASSINFO("group:useCustomRange", "Classification")
    Q_CLASSINFO("group:rangeMin", "Classification")
    Q_CLASSINFO("group:rangeMax", "Classification")
    Q_CLASSINFO("group:colorRampName", "Ramp")
    Q_CLASSINFO("group:invertRamp", "Ramp")
    Q_CLASSINFO("group:lowColor", "Ramp")
    Q_CLASSINFO("group:highColor", "Ramp")

public:
    /** @brief Construct the style and its default settings.
     * @param parent Optional QObject owner; its destruction destroys this style.
     * Seeds Continuous EqualInterval classification with eight bands and Water Depth ramp. All reads/edits occur on its owning Qt thread. */
    explicit ScalarFillStyle(QObject *parent = nullptr);

    /** @brief Read the scalar attribute key.
     * @return Stored/forwarded value; initially "depth". */
    [[nodiscard]] QString attribute() const { return m_attribute; }
    /** @brief Read the Classified-versus-Continuous mode switch.
     * @return Stored/forwarded value; initially false. */
    [[nodiscard]] bool classified() const
    {
        return m_scheme.mode() == ClassificationScheme::ClassMode::Classified;
    }
    /** @brief Read the classification band count.
     * @return Stored/forwarded value; initially 8. */
    [[nodiscard]] int bandCount() const { return m_scheme.classCount(); }
    /** @brief Read the scheme ramp-name token.
     * @return Stored/forwarded value; initially "Water Depth". */
    [[nodiscard]] QString colorRampName() const { return m_scheme.rampName(); }
    /** @brief Read the ramp inversion switch.
     * @return Stored/forwarded value; initially false. */
    [[nodiscard]] bool invertRamp() const { return m_scheme.invertRamp(); }
    /** @brief Read the two-color ramp lower endpoint.
     * @return Stored/forwarded value; initially RGBA(247,251,255,100). */
    [[nodiscard]] QColor lowColor() const { return m_scheme.lowColor(); }
    /** @brief Read the two-color ramp upper endpoint.
     * @return Stored/forwarded value; initially RGBA(8,48,107,255). */
    [[nodiscard]] QColor highColor() const { return m_scheme.highColor(); }
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
    /** @brief Set the Classified-versus-Continuous mode switch and notify on a change.
     * @param v New value. Selects the corresponding scheme mode without changing method, count or overrides.
     * Unchanged values according to mode equality are ignored; changed values
     * emit styleChanged through setDirty(). No synchronous drawing occurs. */
    void setClassified(bool v);
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

    /** @brief Borrow the complete classification settings.
     * @return Scheme owned by this style, replaced by setScheme()/fromJson();
     * callers must not mutate it through a cast or retain elements across replacement. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    /** @brief Copy a complete classification configuration and notify on inequality.
     * @param s Source copied without validation. ClassificationScheme equality
     * decides whether to replace it; other style fields are retained. */
    void setScheme(const ClassificationScheme &s);

    /*! Continuous colour for a raw value over [dataMin, dataMax]. */
    /** @param v Scalar in the selected attribute unit. Nonfinite or below-range values
     * are transparent under the scheme's continuous sampling contract.
     * @param dataMin Automatic data lower bound, overridden by a custom range if enabled.
     * @param dataMax Automatic upper bound in the same unit.
     * @return Scheme's continuous color including ramp alpha/inversion, without sublayer
     * opacity; this helper samples continuously even when classified() is true. */
    [[nodiscard]] QColor colorForValue(double v, double dataMin, double dataMax) const
    {
        return m_scheme.colorForValue(v, dataMin, dataMax);
    }
    /*! Classified colour at the midpoint of class \p classIndex of \p count. */
    /** @param classIndex Zero-based class; negative yields transparent.
     * @param count Nominal class count, normalized to at least one before forwarding.
     * @return Scheme override when valid, otherwise midpoint ramp color with high
     * indices clamped to the last band. Includes ramp alpha but no sublayer opacity. */
    [[nodiscard]] QColor colorForClass(int classIndex, int count) const
    {
        return m_scheme.colorForClass(classIndex, std::max(1, count));
    }

    /** @brief Serialize the current style settings.
     * @return Independent object. Writes the attribute and full embedded classification object without validation. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Read a partial style object and notify listeners.
     * @param j Borrowed JSON; unknown keys are ignored.
     * Absent/mistyped attribute retains the current key; a present classification key
     * replaces the scheme via its reader (nonobject becomes empty object), while an
     * absent key retains it. Other top-level keys are ignored. Emits styleChanged once
     * unconditionally, without calling the individual property setters. */
    void fromJson(const QJsonObject &j) override;

private:
    QString m_attribute = QStringLiteral("depth");
    ClassificationScheme m_scheme;
};

/*! One flat colour per mesh cell, from the cell-centre value. */
class CellDepthFillSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a sublayer with a QObject-owned style and invalidation forwarding.
     * @param id_ Stable host ID moved into storage, without checking uniqueness.
     * @param parent Optional QObject owner; destroys this sublayer and style with itself.
     * All access is on the owning Qt thread; styleChanged forwards to invalidated(). */
    explicit CellDepthFillSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify this rendering pass.
     * @return FillKind. */
    Kind kind() const override { return FillKind; }
    /** @brief Copy the immutable host identifier.
     * @return Construction-time id without validation. */
    QString id() const override { return m_id; }
    /** @brief Describe this pass for the UI.
     * @return Translated Cell Elevation Fill when the attribute equals elevation ignoring case,
     * otherwise Cell Depth Fill. */
    QString displayName() const override
    {
        const QString a = m_style ? m_style->attribute() : QString();
        if (a.compare(QLatin1String("elevation"), Qt::CaseInsensitive) == 0)
            return tr("Cell Elevation Fill");
        return tr("Cell Depth Fill");
    }

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
     * @return Shared buildFillLegend() output: six continuous previews, method-aware numeric
     * classified rows when a valid custom range needs no samples, placeholder class
     * rows for data-dependent/unknown ranges, or one invalid-classification message.
     * Rows carry this id and opacity, independent of visibility. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Preserve the existing scene-graph node without building geometry here.
     * @param existing Borrowed node returned unchanged, including null; no ownership transfer.
     * @param ctx Unused context; the host renderer owns actual geometry construction.
     * @return existing unconditionally. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Borrow the concrete owned style.
     * @return Child pointer valid until sublayer destruction; do not delete or reparent. */
    [[nodiscard]] ScalarFillStyle *fillStyle() const { return m_style; }

private:
    QString m_id;
    bool m_visible = false;
    qreal m_opacity = 1.0;
    ScalarFillStyle *m_style;
};

/*! A colour per mesh vertex, interpolated across each triangle by the GPU
 *  (Gouraud). Continuous mode gives a smooth seam-free gradient — the
 *  artifact-free alternative to marching-squares contour bands. */
class SmoothDepthFillSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a sublayer with a QObject-owned style and invalidation forwarding.
     * @param id_ Stable host ID moved into storage, without checking uniqueness.
     * @param parent Optional QObject owner; destroys this sublayer and style with itself.
     * All access is on the owning Qt thread; styleChanged forwards to invalidated(). */
    explicit SmoothDepthFillSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify this rendering pass.
     * @return ColorRampFillKind. */
    Kind kind() const override { return ColorRampFillKind; }
    /** @brief Copy the immutable host identifier.
     * @return Construction-time id without validation. */
    QString id() const override { return m_id; }
    /** @brief Describe this pass for the UI.
     * @return Translated Smooth Elevation Fill when the attribute equals elevation ignoring case,
     * otherwise Smooth Depth Fill. */
    QString displayName() const override
    {
        const QString a = m_style ? m_style->attribute() : QString();
        if (a.compare(QLatin1String("elevation"), Qt::CaseInsensitive) == 0)
            return tr("Smooth Elevation Fill");
        return tr("Smooth Depth Fill");
    }

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
     * @return Shared buildFillLegend() output with this id and opacity; same range-validation
     * and data-unavailable placeholders as CellDepthFillSublayer. Visibility is ignored. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Preserve the existing scene-graph node without building geometry here.
     * @param existing Borrowed node returned unchanged, including null; no ownership transfer.
     * @param ctx Unused context; the host renderer owns actual geometry construction.
     * @return existing unconditionally. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Borrow the concrete owned style.
     * @return Child pointer valid until sublayer destruction; do not delete or reparent. */
    [[nodiscard]] ScalarFillStyle *fillStyle() const { return m_style; }

private:
    QString m_id;
    bool m_visible = false;
    qreal m_opacity = 0.60; // basemap stays visible under the fill
    ScalarFillStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
