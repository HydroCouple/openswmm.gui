/**
 * @file      legendcontent.cpp
 * @brief     Historically three near-copies — LegendOverlay, LegendLayerTreeModel and
 *            legendclasseditcommands — each resolved "which renderer / which rows / which swatch
 *            color" for a layer and had to stay in lockstep manually.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "map/legendcontent.h"

#include "layers/gisrasterlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "render/legendoverlaystyle.h"
#include "render/ifeaturerenderer.h"
#include "render/irasterrenderer.h"
#include "render/symbolstyle.h"

namespace openswmmvis::map::LegendContent
{

OpenSWMM::Render::IFeatureRenderer *featureRendererFor(OpenSWMMVisLayer *layer)
{
    if (!layer)
        return nullptr;
    // NOTE: SWMMResultsLayer is intentionally NOT routed through its
    // layer-level renderer() — its rows and edits go through the
    // kind-qualified facade (gap B1), same as SWMMModelLayer.
    if (auto *l = qobject_cast<SWMM2DResultsLayer *>(layer))
        return l->renderer();
    if (auto *l = qobject_cast<SWMM2DMeshLayer *>(layer))
        return l->renderer();
    if (auto *l = qobject_cast<GISVectorLayer *>(layer))
        return l->renderer();
    return nullptr;
}

QList<OpenSWMM::Render::LegendSymbolItem> legendItemsFor(OpenSWMMVisLayer *layer)
{
    using namespace OpenSWMM::Render;
    if (!layer)
        return {};

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

QList<OpenSWMM::Render::LegendEntry> buildEntries(const MapCanvas *canvas,
                                                  const OpenSWMM::Render::LegendOverlayStyle *style)
{
    using OpenSWMM::Render::LegendEntry;
    QList<LegendEntry> entries;
    if (!canvas)
        return entries;
    const auto layers = canvas->layers();
    int block = 0;
    // Top of the map first.
    for (int i = layers.size() - 1; i >= 0; --i) {
        OpenSWMMVisLayer *layer = layers.at(i);
        if (!layer || !layer->isVisible())
            continue;
        const QString layerKey = OpenSWMM::Render::LegendOverlayStyle::itemKey(layer, {});
        ++block;
        LegendEntry heading;
        heading.kind = LegendEntry::Kind::GroupHeading;
        heading.label = layer->name();
        heading.layer = layer;
        heading.layerKey = layerKey;
        heading.blockId = block;
        entries.append(heading);
        for (const auto &row : legendItemsFor(layer)) {
            bool visible = row.visible;
            QString label = row.effectiveLabel();
            // The legend's own settings hide or rename rows; a row the renderer hides stays
            // hidden.
            if (style && !row.classKey.isEmpty()) {
                const auto override = style->itemOverride(layerKey, row.classKey);
                if (!override.visible)
                    visible = false;
                if (!override.userLabel.isEmpty())
                    label = override.userLabel;
            }
            if (!visible)
                continue;
            LegendEntry entry;
            entry.kind = row.symbol.layers.isEmpty() ? LegendEntry::Kind::Subheading
                                                     : LegendEntry::Kind::Symbol;
            entry.label = label;
            entry.symbol = row.symbol;
            entry.layer = layer;
            entry.layerKey = layerKey;
            entry.classKey = row.classKey;
            entry.sublayerId = row.sublayerId;
            entry.blockId = block;
            entries.append(entry);
        }
    }
    return entries;
}

QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style)
{
    return OpenSWMM::Render::SymbolProps::firstColor(style, QColor(Qt::gray));
}

bool supportsClassEdit(OpenSWMMVisLayer *layer, OpenSWMM::Render::ClassEditKind kind)
{
    if (auto *r = featureRendererFor(layer))
        return r->supportsClassEdit(kind);
    if (auto *m = qobject_cast<SWMMModelLayer *>(layer))
        return m->supportsClassEdit(kind);
    if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer))
        return rl->supportsClassEdit(kind);
    return false;
}

QColor colorForClass(OpenSWMMVisLayer *layer, const QString &classKey)
{
    if (auto *r = featureRendererFor(layer))
        return r->colorForClass(classKey);
    if (auto *m = qobject_cast<SWMMModelLayer *>(layer))
        return m->colorForClass(classKey);
    if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer))
        return rl->colorForClass(classKey);
    return {};
}

} // namespace openswmmvis::map::LegendContent
