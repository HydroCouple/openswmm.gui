/**
 * @file      legendlayertreemodel.cpp
 * @brief     layers as a two-level tree (Layer → LegendSymbolItem) for the LegendPropertiesDialog
 *            Layers tab.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/models/legendlayertreemodel.h"

#include "layers/gisrasterlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "map/legendclasseditcommands.h"
#include "map/legendcontent.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "render/ifeaturerenderer.h"
#include "render/irasterrenderer.h"
// Slice S4 — sublayer-aware legend (RENDERING_OUTPUT_SUBLAYERS_PLAN.md).
#include "render/isublayer.h"
#include "render/isublayerhost.h"
#include "render/legenditemtree.h"
#include "render/legendoverlaystyle.h"
#include "render/legendsymbolitem.h"

#include <QBrush>
#include <QPalette>

namespace openswmmvis::ui
{

namespace
{

// Gap B1 — both helpers delegate to the canonical LegendContent copy so
// the dock tree, the on-canvas legend and the per-class edit routing read
// the SAME rows (results layers now aggregate their per-kind renderers
// instead of showing the dormant layer-level renderer).
/**
 * @brief Delegate symbol swatch color selection to the shared legend-content implementation.
 * @param[in] style Symbol style whose first applicable layer color is inspected.
 * @return Shared LegendContent fallback/color result.
 */
QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style)
{
    return openswmmvis::map::LegendContent::firstSymbolColor(style);
}

// Encodes parent/child position in QModelIndex::internalId. Top-level
// (layer-header) rows carry id == -1. Rows below a layer carry the layer's
// index in the high bits and, in the low 16 bits, 0 for an entry directly
// under the layer or (entry + 1) for a row under that entry.
constexpr quintptr kTopLevelId = static_cast<quintptr>(-1);
constexpr int kEntryBits = 16;
constexpr quintptr kEntryMask = (quintptr(1) << kEntryBits) - 1;

/** The internal id of the rows under @p parentEntry of layer @p layerRow (-1: under the layer). */
quintptr entryId(int layerRow, int parentEntry)
{
    return (quintptr(layerRow) << kEntryBits) | quintptr(parentEntry + 1);
}

} // namespace

LegendLayerTreeModel::LegendLayerTreeModel(MapCanvas *canvas, QObject *parent)
    : QAbstractItemModel(parent), m_canvas(canvas)
{
    if (m_canvas) {
        connect(m_canvas, &MapCanvas::layerAdded, this,
                &LegendLayerTreeModel::onCanvasLayersChanged);
        connect(m_canvas, &MapCanvas::layerRemoved, this,
                &LegendLayerTreeModel::onCanvasLayersChanged);
        connect(m_canvas, &MapCanvas::layerOrderChanged, this,
                &LegendLayerTreeModel::onCanvasLayersChanged);
    }
    rebuildLayerCache();
}

LegendLayerTreeModel::~LegendLayerTreeModel()
{
    disconnectAllLayers();
}

void LegendLayerTreeModel::setOverlayStyle(OpenSWMM::Render::LegendOverlayStyle *style)
{
    if (m_style == style)
        return;
    if (m_style)
        disconnect(m_style.data(), nullptr, this, nullptr);
    m_style = style;
    if (m_style) {
        // Per-item override edits (visibility / rename) → refresh affected
        // rows. Fine-grained signal, but rebuilding the whole cache is
        // simpler and still cheap given typical row counts.
        connect(m_style.data(), &OpenSWMM::Render::LegendOverlayStyle::itemOverrideChanged, this,
                &LegendLayerTreeModel::onLayerRepaintRequested, Qt::UniqueConnection);
        // Order, groups and per-layer settings (the legend tree) change the rows too.
        connect(m_style.data(), &OpenSWMM::Render::LegendOverlayStyle::itemTreeChanged, this,
                &LegendLayerTreeModel::onLayerRepaintRequested, Qt::UniqueConnection);
        connect(m_style.data(), &OpenSWMM::Render::LegendOverlayStyle::autoUpdateChanged, this,
                &LegendLayerTreeModel::onLayerRepaintRequested, Qt::UniqueConnection);
    }
    beginResetModel();
    rebuildLayerCache();
    endResetModel();
}

void LegendLayerTreeModel::onCanvasLayersChanged()
{
    beginResetModel();
    rebuildLayerCache();
    endResetModel();
}

void LegendLayerTreeModel::onLayerRepaintRequested()
{
    // Any layer-level shift (renderer mutation, visibility, name change) may have changed legend
    // rows; read them fresh. A layer that only repainted (an animated one does all the time)
    // changes nothing: the model is left alone, so a click or an edit in progress is not lost to
    // a reset. Changed rows are updated in place; only a different set of rows resets.
    QVector<LayerNode> fresh = collectLayers();
    if (fresh == m_layers)
        return;
    bool sameRows = fresh.size() == m_layers.size();
    for (qsizetype i = 0; sameRows && i < fresh.size(); ++i)
        sameRows = fresh[i].layer == m_layers[i].layer &&
                   fresh[i].items.size() == m_layers[i].items.size() &&
                   fresh[i].entries == m_layers[i].entries;
    if (!sameRows) {
        beginResetModel();
        rebuildLayerCache();
        endResetModel();
        return;
    }
    m_layers = std::move(fresh);
    if (m_layers.isEmpty())
        return;
    emit dataChanged(index(0, 0), index(int(m_layers.size()) - 1, ColCount - 1));
    for (int r = 0; r < m_layers.size(); ++r) {
        const auto &entries = m_layers[r].entries;
        if (entries.isEmpty())
            continue;
        const QModelIndex layer = index(r, 0);
        emit dataChanged(index(0, 0, layer), index(int(entries.size()) - 1, ColCount - 1, layer));
        for (int e = 0; e < entries.size(); ++e)
            if (!entries[e].children.isEmpty()) {
                const QModelIndex entry = index(e, 0, layer);
                emit dataChanged(index(0, 0, entry),
                                 index(int(entries[e].children.size()) - 1, ColCount - 1, entry));
            }
    }
}

void LegendLayerTreeModel::disconnectAllLayers()
{
    for (auto &p : m_connectedLayers) {
        if (p)
            disconnect(p, nullptr, this, nullptr);
    }
    m_connectedLayers.clear();
    // VS.9 — drop sublayer connections too.
    for (auto &p : m_connectedSublayers) {
        if (p)
            disconnect(p, nullptr, this, nullptr);
    }
    m_connectedSublayers.clear();
}

void LegendLayerTreeModel::connectLayer(OpenSWMMVisLayer *layer)
{
    if (!layer)
        return;
    connect(layer, &OpenSWMMVisLayer::repaintRequested, this,
            &LegendLayerTreeModel::onLayerRepaintRequested, Qt::UniqueConnection);
    connect(layer, &OpenSWMMVisLayer::visibilityChanged, this,
            &LegendLayerTreeModel::onLayerRepaintRequested, Qt::UniqueConnection);
    connect(layer, &OpenSWMMVisLayer::nameChanged, this,
            &LegendLayerTreeModel::onLayerRepaintRequested, Qt::UniqueConnection);
    m_connectedLayers.append(layer);

    // Gap B1 — renderer swaps (kind-tree edits, variable retargeting) and
    // result-variable changes must refresh the legend rows; neither emits
    // repaintRequested in every path, so subscribe explicitly.
    if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer)) {
        connect(rl, &SWMMResultsLayer::rendererChanged, this,
                &LegendLayerTreeModel::onLayerRepaintRequested, Qt::UniqueConnection);
        // Signal carries the variable; the slot ignores it (Qt drops
        // trailing signal arguments). A member-function slot is required —
        // Qt::UniqueConnection asserts on lambdas.
        connect(rl, &SWMMResultsLayer::variableChanged, this,
                &LegendLayerTreeModel::onLayerRepaintRequested, Qt::UniqueConnection);
    }
    if (auto *l2d = qobject_cast<SWMM2DResultsLayer *>(layer)) {
        connect(l2d, &SWMM2DResultsLayer::rendererChanged, this,
                &LegendLayerTreeModel::onLayerRepaintRequested, Qt::UniqueConnection);
    }

    // VS.9 — a sublayer's style / visibility / opacity edit emits
    // invalidated() and repaints the canvas, but does NOT raise the parent
    // layer's repaintRequested, so the legend would otherwise go stale.
    // Route each sublayer's invalidated() through the same cheap reset.
    if (auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(layer)) {
        for (OpenSWMM::Render::ISublayer *s : host->sublayers()) {
            if (!s)
                continue;
            connect(s, &OpenSWMM::Render::ISublayer::invalidated, this,
                    &LegendLayerTreeModel::onLayerRepaintRequested, Qt::UniqueConnection);
            m_connectedSublayers.append(s);
        }
    }
}

void LegendLayerTreeModel::rebuildLayerCache()
{
    disconnectAllLayers();
    m_layers = collectLayers();
    if (!m_canvas)
        return;
    // Every map layer is watched, also those the legend does not list.
    for (OpenSWMMVisLayer *layer : m_canvas->layers())
        if (layer)
            connectLayer(layer);
}

QVector<LegendLayerTreeModel::LayerNode> LegendLayerTreeModel::collectLayers() const
{
    QVector<LayerNode> layers;
    if (!m_canvas)
        return layers;

    // The legend's own order (map order while it follows the map), groups flattened, every
    // row kept — hidden ones too, so they can be checked again.
    using openswmmvis::map::LegendContent::LegendTreeItem;
    QList<LegendTreeItem> layerItems;
    const auto collect = [&layerItems](const auto &self,
                                       const QList<LegendTreeItem> &items) -> void {
        for (const LegendTreeItem &item : items) {
            if (item.node.type == OpenSWMM::Render::LegendNode::Type::Layer && item.layer)
                layerItems.append(item);
            else if (item.node.type == OpenSWMM::Render::LegendNode::Type::Group)
                self(self, item.children);
        }
    };
    collect(collect, openswmmvis::map::LegendContent::resolveTree(m_canvas, m_style));

    for (const LegendTreeItem &layerItem : std::as_const(layerItems)) {
        OpenSWMMVisLayer *layer = layerItem.layer.data();
        LayerNode node;
        node.layer = layer;
        node.name = layer->name();
        node.key = layerItem.node.id;
        node.shown = layerItem.node.visible;
        // Gap B1 — single dispatch through LegendContent (covers the
        // single-renderer layers AND the multi-kind facades: SWMMModelLayer,
        // SWMMResultsLayer).
        node.editableColor = openswmmvis::map::LegendContent::supportsClassEdit(
            layer, OpenSWMM::Render::ClassEditKind::Color);
        node.editableSize = openswmmvis::map::LegendContent::supportsClassEdit(
            layer, OpenSWMM::Render::ClassEditKind::Size);

        auto *renderer = openswmmvis::map::LegendContent::featureRendererFor(layer);
        auto *modelLayer = qobject_cast<SWMMModelLayer *>(layer);     // X4
        auto *resultsLayer = qobject_cast<SWMMResultsLayer *>(layer); // Gap B1
        const QList<bool> hiddenWith =
            openswmmvis::map::LegendContent::rowsHiddenWithOthers(layerItem, m_style);
        for (qsizetype i = 0; i < layerItem.children.size(); ++i) {
            const LegendTreeItem &child = layerItem.children[i];
            const auto &row = child.row;
            ItemRow ir;
            ir.classKey = row.classKey;
            ir.rowKey = openswmmvis::map::LegendContent::rowKey(row);
            ir.hiddenWithOthers = hiddenWith[i];
            // The legend's label and visibility, as the on-canvas legend shows them.
            ir.label = child.label();
            // Rows with no symbol are sub-headers (kind / section titles) —
            // leave the color invalid so the delegate paints no chip
            // instead of the gray fallback patch.
            ir.color = row.symbol.layers.isEmpty() ? QColor() : firstSymbolColor(row.symbol);
            ir.visible = row.visible && child.node.visible;
            ir.sublayerId = row.sublayerId; // Slice S4 P5
            if (renderer && !row.classKey.isEmpty()) {
                const qreal s = renderer->sizeForClass(row.classKey);
                if (s > 0.0)
                    ir.size = s;
            } else if (modelLayer && !row.classKey.isEmpty()) {
                const qreal s = modelLayer->sizeForClass(row.classKey);
                if (s > 0.0)
                    ir.size = s;
            } else if (resultsLayer && !row.classKey.isEmpty()) {
                const qreal s = resultsLayer->sizeForClass(row.classKey);
                if (s > 0.0)
                    ir.size = s;
            }
            node.items.append(ir);
        }
        // The tree under the layer: sub-headings over their sections, sublayers over their rows.
        for (const auto &g : openswmmvis::map::LegendContent::rowGroups(layerItem)) {
            Entry e;
            e.row = g.row;
            e.children = QVector<int>(g.children.cbegin(), g.children.cend());
            e.label = g.label;
            e.key = g.key;
            node.entries.append(e);
        }
        layers.append(node);
    }
    return layers;
}

int LegendLayerTreeModel::columnCount(const QModelIndex & /*parent*/) const
{
    return ColCount;
}

int LegendLayerTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_layers.size();
    if (parent.column() != 0)
        return 0;
    if (parent.internalId() == kTopLevelId) {
        if (parent.row() < 0 || parent.row() >= m_layers.size())
            return 0;
        return int(m_layers.at(parent.row()).entries.size());
    }
    if ((parent.internalId() & kEntryMask) != 0)
        return 0; // rows under an entry have none
    const Target t = targetOf(parent);
    if (t.layer < 0)
        return 0;
    return int(m_layers.at(t.layer).entries.at(t.entry).children.size());
}

QModelIndex LegendLayerTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= ColCount)
        return {};
    if (!parent.isValid()) {
        if (row >= m_layers.size())
            return {};
        return createIndex(row, column, kTopLevelId);
    }
    if (parent.internalId() == kTopLevelId) {
        if (parent.row() < 0 || parent.row() >= m_layers.size() ||
            row >= m_layers.at(parent.row()).entries.size())
            return {};
        return createIndex(row, column, entryId(parent.row(), -1));
    }
    if ((parent.internalId() & kEntryMask) != 0)
        return {};
    const Target t = targetOf(parent);
    if (t.layer < 0 || row >= m_layers.at(t.layer).entries.at(t.entry).children.size())
        return {};
    return createIndex(row, column, entryId(t.layer, t.entry));
}

QModelIndex LegendLayerTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || child.internalId() == kTopLevelId)
        return {};
    const int layerRow = int(child.internalId() >> kEntryBits);
    if (layerRow < 0 || layerRow >= m_layers.size())
        return {};
    const int parentEntry = int(child.internalId() & kEntryMask) - 1;
    if (parentEntry < 0)
        return createIndex(layerRow, 0, kTopLevelId);
    return createIndex(parentEntry, 0, entryId(layerRow, -1));
}

LegendLayerTreeModel::Target LegendLayerTreeModel::targetOf(const QModelIndex &idx) const
{
    Target t;
    if (!idx.isValid() || idx.internalId() == kTopLevelId)
        return t;
    const int layerRow = int(idx.internalId() >> kEntryBits);
    const int parentEntry = int(idx.internalId() & kEntryMask) - 1;
    if (layerRow < 0 || layerRow >= m_layers.size())
        return t;
    const auto &entries = m_layers.at(layerRow).entries;
    if (parentEntry < 0) {
        if (idx.row() < 0 || idx.row() >= entries.size())
            return t;
        t.entry = idx.row();
        t.itemRow = entries.at(idx.row()).row;
        t.sublayer = t.itemRow < 0;
    } else {
        if (parentEntry >= entries.size() || idx.row() < 0 ||
            idx.row() >= entries.at(parentEntry).children.size())
            return t;
        t.entry = parentEntry;
        t.itemRow = entries.at(parentEntry).children.at(idx.row());
    }
    t.layer = layerRow;
    return t;
}

QVariant LegendLayerTreeModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid())
        return {};

    // Layer-header row.
    if (idx.internalId() == kTopLevelId) {
        if (idx.row() < 0 || idx.row() >= m_layers.size())
            return {};
        const LayerNode &n = m_layers.at(idx.row());
        switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return idx.column() == ColItem ? n.name : QVariant{};
        case LayerPtrRole:
            return QVariant::fromValue(n.layer.data());
        case EditableRole:
            return false; // header rows never editable
        case Qt::CheckStateRole:
            // The whole layer in the legend (the map still draws it).
            if (m_style && idx.column() == ColItem)
                return n.shown ? Qt::Checked : Qt::Unchecked;
            return {};
        case Qt::ToolTipRole:
            if (m_style && idx.column() == ColItem)
                return tr("Show this layer in the legend (the map still draws it)");
            return {};
        default:
            return {};
        }
    }

    const Target t = targetOf(idx);
    if (t.layer < 0)
        return {};
    const LayerNode &n = m_layers.at(t.layer);
    // A sublayer without a heading row: its name, and a check box for all its rows.
    if (t.sublayer) {
        const Entry &e = n.entries.at(t.entry);
        switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return idx.column() == ColItem ? e.label : QVariant{};
        case Qt::CheckStateRole: {
            if (!m_style || idx.column() != ColItem)
                return {};
            int shown = 0;
            for (const int r : e.children)
                shown += n.items.at(r).visible ? 1 : 0;
            return shown == 0                       ? Qt::Unchecked
                   : shown == int(e.children.size()) ? Qt::Checked
                                                     : Qt::PartiallyChecked;
        }
        case Qt::ToolTipRole:
            return idx.column() == ColItem
                       ? tr("Sublayer: its check box shows or hides all its rows")
                       : QVariant{};
        case LayerPtrRole:
            return QVariant::fromValue(n.layer.data());
        case SublayerIdRole:
            return e.key;
        case EditableRole:
            return false;
        default:
            return {};
        }
    }

    // Item row.
    if (t.itemRow < 0 || t.itemRow >= n.items.size())
        return {};
    const ItemRow &it = n.items.at(t.itemRow);

    switch (role) {
    case Qt::DisplayRole:
        if (idx.column() == ColItem)
            return it.label;
        if (idx.column() == ColSize && it.size > 0.0)
            return QString::number(it.size, 'f', 1);
        return {};
    case Qt::EditRole:
        // ColItem: editable userLabel (overrides the renderer label).
        // ColColor: the QColor value the delegate writes back through.
        // ColSize: the qreal size for the spinbox delegate.
        if (idx.column() == ColItem)
            return it.label;
        if (idx.column() == ColColor)
            return it.color;
        if (idx.column() == ColSize)
            return it.size > 0.0 ? it.size : QVariant{};
        return {};
    case Qt::CheckStateRole:
        // Slice BB Phase 8.6.10 / 8.6.16 — per-item visibility checkbox
        // lives on the Item column of every item row (when style is set), sub-headings included.
        if (m_style && idx.column() == ColItem && !it.rowKey.isEmpty())
            return it.visible ? Qt::Checked : Qt::Unchecked;
        return {};
    case Qt::ForegroundRole:
        // Rows left out with their heading or color bar.
        if (idx.column() == ColItem && it.hiddenWithOthers)
            return QBrush(QPalette().color(QPalette::Disabled, QPalette::Text));
        return {};
    case Qt::DecorationRole:
        return idx.column() == ColColor ? it.color : QVariant{};
    case ClassKeyRole:
        return it.classKey;
    case LayerPtrRole:
        return QVariant::fromValue(n.layer.data());
    case EditableRole:
        if (idx.column() == ColColor)
            return n.editableColor && !it.classKey.isEmpty();
        if (idx.column() == ColSize)
            return n.editableSize && !it.classKey.isEmpty() && it.size > 0.0;
        return false;
    case SublayerIdRole:
        // Slice S4 P5 — non-empty iff this row was contributed by a sublayer.
        return it.sublayerId;
    default:
        return {};
    }
}

bool LegendLayerTreeModel::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    if (!idx.isValid())
        return false;
    if (idx.internalId() == kTopLevelId) {
        // The whole layer in or out of the legend; the map is unchanged.
        if (role != Qt::CheckStateRole || idx.column() != ColItem || !m_style || idx.row() < 0 ||
            idx.row() >= m_layers.size() || m_layers.at(idx.row()).key.isEmpty())
            return false;
        OpenSWMM::Render::LegendItemTree tree = m_style->itemTree();
        tree.ensureLayerNode(m_layers.at(idx.row()).key).visible = value.toInt() == Qt::Checked;
        m_style->setItemTree(tree);
        return true;
    }

    const Target t = targetOf(idx);
    if (t.layer < 0)
        return false;
    LayerNode &n = m_layers[t.layer];
    if (!n.layer)
        return false;
    // A sublayer without a heading row: all its rows in or out of the legend, as one edit.
    if (t.sublayer) {
        if (role != Qt::CheckStateRole || idx.column() != ColItem || !m_style)
            return false;
        const QString layerKey = OpenSWMM::Render::LegendOverlayStyle::itemKey(n.layer, {});
        const bool on = value.toInt() == Qt::Checked;
        OpenSWMM::Render::LegendItemTree tree = m_style->itemTree();
        for (const int r : n.entries.at(t.entry).children)
            if (!n.items.at(r).rowKey.isEmpty())
                tree.ensureItemNode(layerKey, n.items.at(r).rowKey).visible = on;
        m_style->setItemTree(tree);
        return true;
    }
    if (t.itemRow < 0 || t.itemRow >= n.items.size())
        return false;
    const ItemRow &it = n.items.at(t.itemRow);

    // ── Visibility checkbox (CheckStateRole on ColItem) ───────────────
    if (role == Qt::CheckStateRole && idx.column() == ColItem && m_style && !it.rowKey.isEmpty()) {
        const QString layerKey = OpenSWMM::Render::LegendOverlayStyle::itemKey(n.layer, {});
        const bool checked = value.toInt() == Qt::Checked;
        m_style->setItemVisible(layerKey, it.rowKey, checked);
        // itemOverrideChanged → onLayerRepaintRequested triggers a reset
        // so cached rows reflect the new visibility on next read.
        return true;
    }

    // ── Label edit (EditRole on ColItem) ──────────────────────────────
    if (role == Qt::EditRole && idx.column() == ColItem && m_style && !it.classKey.isEmpty()) {
        const QString layerKey = OpenSWMM::Render::LegendOverlayStyle::itemKey(n.layer, {});
        m_style->setItemUserLabel(layerKey, it.classKey, value.toString());
        return true;
    }

    // ── Color edit (EditRole on ColColor) ─────────────────────────────
    if (role == Qt::EditRole && idx.column() == ColColor) {
        if (!n.editableColor || it.classKey.isEmpty())
            return false;
        const QColor picked = value.value<QColor>();
        if (!picked.isValid())
            return false;
        // Push through the project undo stack so the edit reverses uniformly
        // across every view of the same data.
        if (auto *stack = m_canvas ? m_canvas->undoStack() : nullptr) {
            stack->push(new openswmmvis::map::SetRendererClassColorCommand(n.layer.data(),
                                                                           it.classKey, picked));
        }
        emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole, Qt::DecorationRole});
        return true;
    }

    // ── Size edit (EditRole on ColSize) ───────────────────────────────
    if (role == Qt::EditRole && idx.column() == ColSize) {
        if (!n.editableSize || it.classKey.isEmpty())
            return false;
        bool ok = false;
        const qreal s = value.toReal(&ok);
        if (!ok || s <= 0.0)
            return false;
        if (auto *stack = m_canvas ? m_canvas->undoStack() : nullptr) {
            stack->push(
                new openswmmvis::map::SetRendererClassSizeCommand(n.layer.data(), it.classKey, s));
        }
        emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }

    return false;
}

Qt::ItemFlags LegendLayerTreeModel::flags(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (idx.internalId() == kTopLevelId) {
        // Headers are not editable; their check box shows or hides the whole layer.
        if (m_style && idx.column() == ColItem)
            f |= Qt::ItemIsUserCheckable;
        return f;
    }
    const Target t = targetOf(idx);
    if (t.layer < 0)
        return f;
    const auto &n = m_layers.at(t.layer);
    if (t.sublayer) { // a check box for all its rows
        if (m_style && idx.column() == ColItem)
            f |= Qt::ItemIsUserCheckable;
        return f;
    }
    if (t.itemRow < 0 || t.itemRow >= n.items.size())
        return f;
    const auto &it = n.items.at(t.itemRow);

    if (idx.column() == ColColor && n.editableColor && !it.classKey.isEmpty()) {
        f |= Qt::ItemIsEditable;
    }
    if (idx.column() == ColSize && n.editableSize && !it.classKey.isEmpty() && it.size > 0.0) {
        f |= Qt::ItemIsEditable;
    }
    if (idx.column() == ColItem && m_style && !it.rowKey.isEmpty())
        f |= Qt::ItemIsUserCheckable; // sub-headings too
    if (idx.column() == ColItem && m_style && !it.classKey.isEmpty()) {
        // Visibility checkbox + editable label both live on the Item col.
        f |= Qt::ItemIsEditable;
    }
    return f;
}

QVariant LegendLayerTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColItem:
        return tr("Item");
    case ColColor:
        return tr("Color");
    case ColSize:
        return tr("Size");
    default:
        return {};
    }
}

} // namespace openswmmvis::ui
