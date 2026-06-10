/*!
 * \file   legendcontent.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/legendcontent.h"

#include "layers/gisrasterlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/ifeaturerenderer.h"
#include "render/irasterrenderer.h"
#include "render/symbolstyle.h"

namespace openswmmvis::map::LegendContent {

OpenSWMM::Render::IFeatureRenderer *featureRendererFor(OpenSWMMVisLayer *layer)
{
    if (!layer) return nullptr;
    // NOTE: SWMMResultsLayer is intentionally NOT routed through its
    // layer-level renderer() — its rows and edits go through the
    // kind-qualified facade (gap B1), same as SWMMModelLayer.
    if (auto *l = qobject_cast<SWMM2DResultsLayer *>(layer))  return l->renderer();
    if (auto *l = qobject_cast<SWMM2DMeshLayer *>(layer))     return l->renderer();
    if (auto *l = qobject_cast<GISVectorLayer *>(layer))      return l->renderer();
    return nullptr;
}

QList<OpenSWMM::Render::LegendSymbolItem> legendItemsFor(OpenSWMMVisLayer *layer)
{
    using namespace OpenSWMM::Render;
    if (!layer) return {};

    // Kind-aggregating facades first — these are the layers whose paint is
    // driven by per-kind renderers, so their legends must come from the
    // same aggregation (§J.5).
    if (auto *m = qobject_cast<SWMMModelLayer *>(layer))
        return m->legendSymbolItems();
    if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer))
        return rl->legendSymbolItems();
    if (auto *l = qobject_cast<SWMM2DResultsLayer *>(layer))
        return l->sublayerLegendItems();

    if (auto *r = featureRendererFor(layer))
        return r->legendSymbolItems();
    if (auto *l = qobject_cast<GISRasterLayer *>(layer); l && l->rasterRenderer())
        return l->rasterRenderer()->legendSymbolItems();
    return {};
}

QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style)
{
    return OpenSWMM::Render::SymbolProps::firstColor(style, QColor(Qt::gray));
}

bool supportsClassEdit(OpenSWMMVisLayer *layer,
                       OpenSWMM::Render::ClassEditKind kind)
{
    if (auto *r = featureRendererFor(layer)) return r->supportsClassEdit(kind);
    if (auto *m = qobject_cast<SWMMModelLayer *>(layer))
        return m->supportsClassEdit(kind);
    if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer))
        return rl->supportsClassEdit(kind);
    return false;
}

QColor colorForClass(OpenSWMMVisLayer *layer, const QString &classKey)
{
    if (auto *r = featureRendererFor(layer)) return r->colorForClass(classKey);
    if (auto *m = qobject_cast<SWMMModelLayer *>(layer))
        return m->colorForClass(classKey);
    if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer))
        return rl->colorForClass(classKey);
    return {};
}

} // namespace openswmmvis::map::LegendContent
