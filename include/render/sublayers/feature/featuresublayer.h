/*!
 * \file   featuresublayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Granular per-Category sublayer that replaces the legacy
 *         NodeMarker / ConduitLine / ConduitArrow / SubcatchmentFill mix.
 *
 *         Slice U-0 — one FeatureSublayer instance per OpenSWMMVis::SwmmCategory
 *         (Junction / Outfall / Storage / Divider / Conduit / Pump / Orifice /
 *         Weir / Outlet / Subcatchment / RainGage). Each instance carries an
 *         archetype-appropriate FeatureSublayerStyle (Point / Line / Polygon)
 *         so every visual object type can be styled independently.
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

    FeatureSublayer(OpenSWMMVis::SwmmCategory category,
                    QString id_,
                    QString displayName_,
                    QObject *parent = nullptr);
    ~FeatureSublayer() override = default;

    // ── Identity ──────────────────────────────────────────────────────
    [[nodiscard]] OpenSWMMVis::SwmmCategory category() const noexcept { return m_category; }
    [[nodiscard]] Archetype                archetype() const noexcept { return m_archetype; }

    // ── ISublayer ─────────────────────────────────────────────────────
    Kind    kind() const override
    {
        switch (m_archetype) {
            case Archetype::Point:   return MarkerKind;
            case Archetype::Line:    return LineKind;
            case Archetype::Polygon: return FillKind;
        }
        return MarkerKind;
    }
    QString id() const override          { return m_id; }
    QString displayName() const override { return m_displayName; }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override
    {
        return m_category != OpenSWMMVis::CatRainGages;
    }

    SublayerStyle *style() override      { return m_style; }

    /*! Convenience accessor with the derived type already cast. */
    [[nodiscard]] FeatureSublayerStyle        *featureStyle()    const { return m_style; }
    [[nodiscard]] PointFeatureSublayerStyle   *pointStyle()      const;
    [[nodiscard]] LineFeatureSublayerStyle    *lineStyle()       const;
    [[nodiscard]] PolygonFeatureSublayerStyle *polygonStyle()    const;

    QList<LegendSymbolItem> legendSymbolItems() const override;

    /*! Not used by the existing scene-graphics paint pipeline (which
     *  drives painting from SWMMResultsLayer::populateScene). Wired so
     *  the QSG renderer adoption in S5+ has a place to hang geometry. */
    QSGNode *buildOrUpdateNode(QSGNode *existing,
                               const SublayerContext &ctx) override;

private:
    OpenSWMMVis::SwmmCategory m_category;
    Archetype                m_archetype;
    QString                  m_id;
    QString                  m_displayName;
    bool                     m_visible = true;
    qreal                    m_opacity = 1.0;

    /*! Heap-owned via QObject parent-child (this). Concrete derived class
     *  picked at construction based on archetype. */
    FeatureSublayerStyle    *m_style   = nullptr;

public:
    /*! Convert a Category to its Archetype.  Public so renderer-class
     *  editor panels (CategorizedRendererPanel's per-class symbol
     *  dialog) can ask "what archetype am I editing for?" without
     *  needing a live sublayer instance. */
    static Archetype archetypeFor(OpenSWMMVis::SwmmCategory c);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SUBLAYERS_FEATURE_FEATURESUBLAYER_H
