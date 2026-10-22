/**
 * @file      featuresublayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Granular per-Category sublayer that replaces the legacy
 *         NodeMarker / ConduitLine / ConduitArrow / SubcatchmentFill mix.
 *
 *         Slice U-0 — one FeatureSublayer instance per OpenSWMMVis::SwmmCategory
 *         (Junction / Outfall / Storage / Divider / Conduit / Pump / Orifice /
 *         Weir / Outlet / Subcatchment / RainGage). Each instance carries an
 *         archetype-appropriate FeatureSublayerStyle (Point / Line / Polygon)
 *         so every visual object type can be styled independently.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_FEATURE_FEATURESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_FEATURE_FEATURESUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayers/feature/featuresublayerstyle.h"

#include "layers/swmm_category.h"

#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \class FeatureSublayer
 * \brief One paint pass for a single OpenSWMMVis::SwmmCategory.
 *
 *        Archetype is derived from the category:
 *          - CatJunctions/Outfalls/Storage/Dividers/RainGages → Point
 *          - CatConduits/Pumps/Orifices/Weirs/Outlets        → Line
 *          - CatSubcatchments                                → Polygon
 *
 *        isDynamic() == true for every category except CatRainGages,
 *        which has no per-period output and therefore needs no animation
 *        invalidation.
 */
class FeatureSublayer : public ISublayer
{
    Q_OBJECT
public:
    enum class Archetype { Point, Line, Polygon };

    /** @brief Construct one category pass and its owned archetype-specific style.
     * @param category Immutable category used by archetypeFor(); unknown values use Point.
     * Rain gages start without a color attribute/ramp; nonconduit links start as midpoint glyphs.
     * @param id_ Stable host identifier moved into storage; not checked for uniqueness.
     * @param displayName_ UI/legend label moved into storage.
     * @param parent Optional QObject owner. The child style is owned by this sublayer
     * and its styleChanged signal forwards to invalidated(). Access on the owning Qt thread. */
    FeatureSublayer(OpenSWMMVis::SwmmCategory category, QString id_, QString displayName_,
                    QObject *parent = nullptr);
    /** @brief Destroy the pass and its QObject-owned style. */
    ~FeatureSublayer() override = default;

    // ── Identity ──────────────────────────────────────────────────────
    /** @brief Read the immutable source category.
     * @return Category supplied at construction. */
    [[nodiscard]] OpenSWMMVis::SwmmCategory category() const noexcept { return m_category; }
    /** @brief Read the category-derived geometry archetype.
     * @return Point, Line or Polygon selected at construction. */
    [[nodiscard]] Archetype archetype() const noexcept { return m_archetype; }

    // ── ISublayer ─────────────────────────────────────────────────────
    /** @brief Map the archetype to the corresponding rendering kind.
     * @return MarkerKind, LineKind or FillKind; the defensive fallback is MarkerKind. */
    Kind kind() const override
    {
        switch (m_archetype) {
        case Archetype::Point:
            return MarkerKind;
        case Archetype::Line:
            return LineKind;
        case Archetype::Polygon:
            return FillKind;
        }
        return MarkerKind;
    }
    /** @brief Copy the immutable host identifier.
     * @return Construction-time id without validation. */
    QString id() const override { return m_id; }
    /** @brief Copy the immutable display label.
     * @return Construction-time name used by the fallback legend. */
    QString displayName() const override { return m_displayName; }

    /** @brief Read visibility without consulting the host or parent layer.
     * @return Stored flag, initially true. */
    bool isVisible() const override { return m_visible; }
    /** @brief Replace visibility and emit invalidated() on an exact Boolean change.
     * @param v New visibility; no geometry is built synchronously. */
    void setVisible(bool v) override;
    /** @brief Read the raw opacity multiplier.
     * @return Stored value, initially one; this class does not enforce [0,1]. */
    qreal opacity() const override { return m_opacity; }
    /** @brief Replace opacity and emit invalidated() unless qFuzzyCompare reports equality.
     * @param o Raw multiplier; negative, above-one and nonfinite inputs are not clamped. */
    void setOpacity(qreal o) override;

    /** @brief Report whether time changes can affect this category.
     * @return False only for CatRainGages, true for all other category values. */
    bool isDynamic() const override { return m_category != OpenSWMMVis::CatRainGages; }

    /** @brief Borrow the owned archetype-specific property bag through its base type.
     * @return Style owned by this sublayer; caller must not delete it. */
    SublayerStyle *style() override { return m_style; }

    /*! Convenience accessor with the derived type already cast. */
    /** @return Borrowed owned style, valid until the sublayer is destroyed. */
    [[nodiscard]] FeatureSublayerStyle *featureStyle() const { return m_style; }
    /** @brief Borrow the point style when this pass has Point archetype.
     * @return Owned child pointer, or null for another archetype; do not delete. */
    [[nodiscard]] PointFeatureSublayerStyle *pointStyle() const;
    /** @brief Borrow the line style when this pass has Line archetype.
     * @return Owned child pointer, or null for another archetype; do not delete. */
    [[nodiscard]] LineFeatureSublayerStyle *lineStyle() const;
    /** @brief Borrow the polygon style when this pass has Polygon archetype.
     * @return Owned child pointer, or null for another archetype; do not delete. */
    [[nodiscard]] PolygonFeatureSublayerStyle *polygonStyle() const;

    /** @brief Produce a one-row fallback preview for this category.
     * @return Independent row labelled with displayName/id, using a basic marker/line/fill
     * kind, fallback color and point size or line width. Visibility, ramp classes, labels,
     * opacity, arrow details and most archetype properties are not applied here. */
    QList<LegendSymbolItem> legendSymbolItems() const override;

    /*! Not used by the existing scene-graphics paint pipeline (which
     *  drives painting from SWMMResultsLayer::populateScene). Wired so
     *  the QSG renderer adoption in S5+ has a place to hang geometry. */
    /** @param existing Borrowed existing node, returned unchanged with no ownership transfer.
     * @param ctx Unused rendering context.
     * @return existing; this hook currently builds no geometry. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

private:
    OpenSWMMVis::SwmmCategory m_category;
    Archetype m_archetype;
    QString m_id;
    QString m_displayName;
    bool m_visible = true;
    qreal m_opacity = 1.0;

    /*! Heap-owned via QObject parent-child (this). Concrete derived class
     *  picked at construction based on archetype. */
    FeatureSublayerStyle *m_style = nullptr;

public:
    /*! Convert a Category to its Archetype.  Public so renderer-class
     *  editor panels (CategorizedRendererPanel's per-class symbol
     *  dialog) can ask "what archetype am I editing for?" without
     *  needing a live sublayer instance.  Inline (gap A4.1) so headers
     *  that only need the mapping (RendererPanelContext::resolve,
     *  RendererFactory) carry no link dependency on this TU. */
    /** @param c Category to map; junction/outfall/storage/divider/raingage use Point,
     * link categories use Line and subcatchments use Polygon.
     * @return Mapped archetype; unknown categories default to Point. */
    static Archetype archetypeFor(OpenSWMMVis::SwmmCategory c)
    {
        switch (c) {
        case OpenSWMMVis::CatJunctions:
        case OpenSWMMVis::CatOutfalls:
        case OpenSWMMVis::CatStorage:
        case OpenSWMMVis::CatDividers:
        case OpenSWMMVis::CatRainGages:
            return Archetype::Point;
        case OpenSWMMVis::CatConduits:
        case OpenSWMMVis::CatPumps:
        case OpenSWMMVis::CatOrifices:
        case OpenSWMMVis::CatWeirs:
        case OpenSWMMVis::CatOutlets:
            return Archetype::Line;
        case OpenSWMMVis::CatSubcatchments:
            return Archetype::Polygon;
        default:
            return Archetype::Point;
        }
    }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SUBLAYERS_FEATURE_FEATURESUBLAYER_H
