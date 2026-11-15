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
#include "render/isublayer.h"
#include "render/isublayerhost.h"
#include "render/irasterrenderer.h"
#include "render/symbolstyle.h"

#include <QHash>
#include <QSet>

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

QString rowKey(const OpenSWMM::Render::LegendSymbolItem &row)
{
    return row.classKey.isEmpty() ? QStringLiteral("#") + row.label : row.classKey;
}

namespace
{

using OpenSWMM::Render::LegendEntry;
using OpenSWMM::Render::LegendNode;
using OpenSWMM::Render::LegendOverlayStyle;

/** A layer with its rows, in the stored row order when @p ordered. */
LegendTreeItem resolveLayer(OpenSWMMVisLayer *layer, const LegendNode *stored,
                            const LegendOverlayStyle *style, bool ordered)
{
    LegendTreeItem item;
    const QString key = LegendOverlayStyle::itemKey(layer, {});
    if (stored) {
        item.node = stored->settingsOnly();
    } else {
        item.node.type = LegendNode::Type::Layer;
        item.node.id = key;
    }
    item.layer = layer;
    item.sourceLabel = layer->name();

    QList<OpenSWMM::Render::LegendSymbolItem> rows = legendItemsFor(layer);
    if (ordered && stored && !stored->children.isEmpty()) {
        // Stored rows first, in their order; rows the renderer added since go after them.
        QList<OpenSWMM::Render::LegendSymbolItem> sorted;
        for (const LegendNode &child : stored->children)
            for (int i = 0; i < rows.size(); ++i)
                if (rowKey(rows[i]) == child.id) {
                    sorted.append(rows.takeAt(i));
                    break;
                }
        rows = sorted + rows;
    }
    for (const auto &row : rows) {
        LegendTreeItem child;
        child.layer = layer;
        child.row = row;
        child.sourceLabel = row.effectiveLabel();
        const QString id = rowKey(row);
        const LegendNode *node = nullptr;
        if (stored)
            for (const LegendNode &c : stored->children)
                if (c.type == LegendNode::Type::Item && c.id == id)
                    node = &c;
        if (node) {
            child.node = node->settingsOnly();
        } else {
            child.node.type = LegendNode::Type::Item;
            child.node.id = id;
            // A row setting from an older file, keyed by the layer's object name.
            if (style && !row.classKey.isEmpty()) {
                const auto old = style->legacyItemOverride(layer->objectName(), row.classKey);
                child.node.visible = old.visible;
                child.node.userLabel = old.userLabel;
            }
        }
        item.children.append(child);
    }
    return item;
}

QList<LegendTreeItem> resolveNodes(const QList<LegendNode> &nodes,
                                   const QHash<QString, OpenSWMMVisLayer *> &layers,
                                   const LegendOverlayStyle *style)
{
    QList<LegendTreeItem> out;
    for (const LegendNode &n : nodes) {
        switch (n.type) {
        case LegendNode::Type::Layer:
            if (OpenSWMMVisLayer *layer = layers.value(n.id))
                out.append(resolveLayer(layer, &n, style, true));
            break;
        case LegendNode::Type::Group: {
            LegendTreeItem group;
            group.node = n.settingsOnly();
            group.children = resolveNodes(n.children, layers, style);
            out.append(group);
            break;
        }
        case LegendNode::Type::Text: {
            LegendTreeItem text;
            text.node = n.settingsOnly();
            out.append(text);
            break;
        }
        case LegendNode::Type::Item:
            break;
        }
    }
    return out;
}

/** Whether the layer node @p n shows its continuous ramps as color bars. */
bool showsRampBars(const LegendNode &n, const LegendOverlayStyle *style)
{
    return n.ramp == LegendNode::RampChoice::Inherit
               ? !style || style->rampDisplay() == LegendOverlayStyle::RampDisplay::Bar
               : n.ramp == LegendNode::RampChoice::Bar;
}

/** Add the lines of @p items at @p depth. */
void emitEntries(const QList<LegendTreeItem> &items, int depth, int &block, QList<LegendEntry> &out,
                 const LegendOverlayStyle *style, const LegendCounts *counts)
{
    for (const LegendTreeItem &item : items) {
        const LegendNode &n = item.node;
        if (!n.visible)
            continue;
        if (n.type == LegendNode::Type::Text) {
            LegendEntry e;
            e.kind = LegendEntry::Kind::Text;
            e.label = n.userLabel;
            e.nodeId = n.id;
            e.blockId = ++block;
            e.depth = depth;
            e.columnBreakBefore = n.columnBreakBefore;
            out.append(e);
            continue;
        }
        if (n.type == LegendNode::Type::Group) {
            ++block;
            bool pendingBreak = n.columnBreakBefore;
            if (n.heading != LegendNode::HeadingStyle::Hidden) {
                LegendEntry e;
                e.kind = n.heading == LegendNode::HeadingStyle::Subgroup
                             ? LegendEntry::Kind::Subheading
                             : LegendEntry::Kind::GroupHeading;
                e.label = n.userLabel;
                e.nodeId = n.id;
                e.blockId = block;
                e.depth = depth;
                e.columnBreakBefore = pendingBreak;
                e.font = n.headingFont;
                e.color = n.headingColor;
                pendingBreak = false;
                out.append(e);
            }
            const int before = out.size();
            emitEntries(item.children, depth + 1, block, out, style, counts);
            if (pendingBreak && out.size() > before)
                out[before].columnBreakBefore = true;
            continue;
        }
        // A layer: shown on the map and in the legend.
        OpenSWMMVisLayer *layer = item.layer.data();
        if (!layer || !layer->isVisible())
            continue;
        const QString layerKey = n.id;
        ++block;
        // Feature counts, for a layer that can count: only rows seen in the map view, and the
        // count after each label.
        const auto countsOf = [&](const QHash<QString, QHash<QString, qint64>> &all,
                                  bool wanted) -> const QHash<QString, qint64> * {
            if (!wanted)
                return nullptr;
            const auto it = all.constFind(layerKey);
            return it != all.constEnd() ? &it.value() : nullptr;
        };
        // The layer's own choice, else the legend's.
        const bool filter =
            n.mapExtentFilter >= 0 ? n.mapExtentFilter == 1 : style && style->filterByMapExtent();
        const bool showCounts =
            n.featureCounts >= 0 ? n.featureCounts == 1 : style && style->showFeatureCounts();
        const QHash<QString, qint64> *inView =
            counts ? countsOf(counts->inExtent, filter) : nullptr;
        const QHash<QString, qint64> *totals =
            counts ? countsOf(counts->total, showCounts) : nullptr;
        const qsizetype layerStart = out.size();
        bool anyRowInView = false;
        bool pendingBreak = n.columnBreakBefore;
        if (n.heading != LegendNode::HeadingStyle::Hidden) {
            LegendEntry heading;
            heading.kind = n.heading == LegendNode::HeadingStyle::Subgroup
                               ? LegendEntry::Kind::Subheading
                               : LegendEntry::Kind::GroupHeading;
            heading.label = item.label();
            heading.layer = layer;
            heading.layerKey = layerKey;
            heading.blockId = block;
            heading.depth = depth;
            heading.font = n.headingFont;
            heading.color = n.headingColor;
            heading.columnBreakBefore = pendingBreak;
            pendingBreak = false;
            out.append(heading);
        }
        // Continuous ramps as one color bar, unless the layer or the legend shows swatches.
        const bool rampBars = showsRampBars(n, style);
        // Rows turned off with their heading or with their color bar.
        const QList<bool> hiddenWith = rowsHiddenWithOthers(item, style);
        QSet<int> rampsDone;
        for (qsizetype i = 0; i < item.children.size(); ++i) {
            const LegendTreeItem &child = item.children[i];
            const int rampId = child.row.rampId;
            if (rampBars && rampId != 0) {
                if (rampsDone.contains(rampId))
                    continue;
                rampsDone.insert(rampId);
                // The ramp's rows: the bar shows when none is turned off in the legend and the
                // layer's style shows any; the first row carries the spec, and its legend name
                // (if renamed) titles the bar.
                if (hiddenWith[i] || !child.node.visible)
                    continue;
                std::shared_ptr<const OpenSWMM::Render::LegendRampSpec> spec;
                bool anyShown = false;
                const LegendTreeItem *first = &child;
                for (const LegendTreeItem &c : item.children)
                    if (c.row.rampId == rampId) {
                        if (c.row.ramp)
                            spec = c.row.ramp;
                        anyShown = anyShown || c.row.visible;
                    }
                if (!spec || !anyShown)
                    continue;
                LegendEntry bar;
                bar.kind = LegendEntry::Kind::RampBar;
                bar.ramp = spec;
                bar.label = first->node.userLabel.isEmpty() ? spec->title : first->node.userLabel;
                bar.layer = layer;
                bar.layerKey = layerKey;
                bar.sublayerId = first->row.sublayerId;
                bar.nodeId = first->node.id;
                bar.blockId = block;
                bar.depth = depth;
                bar.font = n.itemFont;
                bar.color = n.itemColor;
                bar.rampSettings = n.rampSettings;
                bar.columnBreakBefore = pendingBreak || first->node.columnBreakBefore;
                pendingBreak = false;
                out.append(bar);
                continue;
            }
            if (hiddenWith[i] || !child.node.visible || !child.row.visible)
                continue;
            const QString &classKey = child.row.classKey;
            if (inView && !classKey.isEmpty()) {
                if (inView->value(classKey, 0) == 0)
                    continue;
                anyRowInView = true;
            }
            LegendEntry entry;
            entry.kind = child.row.symbol.layers.isEmpty() ? LegendEntry::Kind::Subheading
                                                           : LegendEntry::Kind::Symbol;
            entry.label = child.label();
            if (totals && !classKey.isEmpty())
                entry.label += QStringLiteral(" [%1]").arg(totals->value(classKey, 0));
            entry.symbol = child.row.symbol;
            entry.layer = layer;
            entry.layerKey = layerKey;
            entry.classKey = child.row.classKey;
            entry.sublayerId = child.row.sublayerId;
            entry.nodeId = child.node.id;
            entry.blockId = block;
            entry.depth = depth;
            entry.itemColumns = n.itemColumns;
            // Rows take the layer's item font and color; section headings keep the legend's.
            if (entry.kind == LegendEntry::Kind::Symbol) {
                entry.font = n.itemFont;
                entry.color = n.itemColor;
            }
            entry.symbolWidth = child.node.symbolWidth > 0 ? child.node.symbolWidth : n.symbolWidth;
            entry.symbolHeight =
                child.node.symbolHeight > 0 ? child.node.symbolHeight : n.symbolHeight;
            entry.columnBreakBefore = pendingBreak || child.node.columnBreakBefore;
            pendingBreak = false;
            out.append(entry);
        }
        // No row of this layer in the map view: leave the layer out.
        if (inView && !anyRowInView)
            out.resize(layerStart);
    }
}

void collectTree(const QList<LegendTreeItem> &items, QList<LegendNode> &out)
{
    for (const LegendTreeItem &item : items) {
        LegendNode n = item.node;
        n.children.clear();
        if (n.type == LegendNode::Type::Layer) {
            for (const LegendTreeItem &child : item.children)
                n.children.append(child.node);
        } else if (n.type == LegendNode::Type::Group) {
            collectTree(item.children, n.children);
        }
        out.append(n);
    }
}

} // namespace

QList<LegendRowGroup> rowGroups(const LegendTreeItem &layer)
{
    const QList<LegendTreeItem> &rows = layer.children;
    const auto isHeading = [&rows](qsizetype i) { return rows[i].row.symbol.layers.isEmpty(); };
    // The sublayer a row comes from, else its SWMM element type (the class key's prefix).
    const auto groupKey = [&rows](qsizetype i) {
        const auto &row = rows[i].row;
        if (!row.sublayerId.isEmpty())
            return row.sublayerId;
        const qsizetype sep = row.classKey.indexOf(QChar(0x1F));
        return sep > 0 ? row.classKey.left(sep) : QString();
    };
    const auto groupLabel = [&layer](const QString &key) {
        if (const auto *host = dynamic_cast<const OpenSWMM::Render::ISublayerHost *>(
                static_cast<const OpenSWMMVisLayer *>(layer.layer.data())))
            for (const OpenSWMM::Render::ISublayer *sub : host->sublayers())
                if (sub && sub->id() == key)
                    return sub->displayName();
        return key;
    };
    QList<LegendRowGroup> out;
    qsizetype i = 0;
    while (i < rows.size()) {
        if (isHeading(i)) {
            // A section: up to the next heading, and while the rows are the heading's sublayer's.
            LegendRowGroup g;
            g.row = int(i);
            const QString section = rows[i].row.sublayerId;
            qsizetype j = i + 1;
            for (; j < rows.size() && !isHeading(j); ++j) {
                const QString &sub = rows[j].row.sublayerId;
                if (!section.isEmpty() && !sub.isEmpty() && sub != section)
                    break;
                g.children.append(int(j));
            }
            out.append(g);
            i = j;
            continue;
        }
        const QString key = groupKey(i);
        qsizetype j = i + 1;
        if (!key.isEmpty())
            while (j < rows.size() && !isHeading(j) && groupKey(j) == key)
                ++j;
        LegendRowGroup g;
        if (j - i >= 2) {
            g.key = key;
            g.label = groupLabel(key);
            for (qsizetype k = i; k < j; ++k)
                g.children.append(int(k));
        } else {
            g.row = int(i);
            j = i + 1;
        }
        out.append(g);
        i = j;
    }
    return out;
}

QList<bool> rowsHiddenWithOthers(const LegendTreeItem &layer, const LegendOverlayStyle *style)
{
    const QList<LegendTreeItem> &rows = layer.children;
    QList<bool> hidden(rows.size(), false);
    // Sections: a heading turned off takes its rows with it.
    for (const LegendRowGroup &g : rowGroups(layer))
        if (g.row >= 0 && !rows[g.row].node.visible)
            for (const int c : g.children)
                hidden[c] = true;
    // Color bars: any row of a ramp turned off takes the bar off.
    if (showsRampBars(layer.node, style)) {
        QSet<int> barsOff;
        for (qsizetype i = 0; i < rows.size(); ++i)
            if (rows[i].row.rampId != 0 && (!rows[i].node.visible || hidden[i]))
                barsOff.insert(rows[i].row.rampId);
        for (qsizetype i = 0; i < rows.size(); ++i)
            if (barsOff.contains(rows[i].row.rampId) && rows[i].node.visible)
                hidden[i] = true;
    }
    return hidden;
}

QList<LegendTreeItem> resolveTree(const MapCanvas *canvas, const LegendOverlayStyle *style)
{
    QList<LegendTreeItem> out;
    if (!canvas)
        return out;
    const auto layers = canvas->layers();
    if (!style || style->autoUpdate()) {
        // Top of the map first.
        for (int i = layers.size() - 1; i >= 0; --i)
            if (OpenSWMMVisLayer *layer = layers.at(i))
                out.append(resolveLayer(
                    layer,
                    style ? style->itemTree().layerNode(LegendOverlayStyle::itemKey(layer, {}))
                          : nullptr,
                    style, false));
        return out;
    }
    QHash<QString, OpenSWMMVisLayer *> byKey;
    for (OpenSWMMVisLayer *layer : layers)
        if (layer)
            byKey.insert(LegendOverlayStyle::itemKey(layer, {}), layer);
    return resolveNodes(style->itemTree().roots(), byKey, style);
}

OpenSWMM::Render::LegendItemTree currentTree(const MapCanvas *canvas,
                                             const LegendOverlayStyle *style)
{
    OpenSWMM::Render::LegendItemTree tree;
    collectTree(resolveTree(canvas, style), tree.roots());
    return tree;
}

QList<LegendEntry> buildEntries(const MapCanvas *canvas, const LegendOverlayStyle *style,
                                const LegendCounts *counts)
{
    QList<LegendEntry> entries;
    int block = 0;
    emitEntries(resolveTree(canvas, style), 0, block, entries, style, counts);
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
