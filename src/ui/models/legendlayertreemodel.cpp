/*!
 * \file   legendlayertreemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
#include "render/legendoverlaystyle.h"
#include "render/legendsymbolitem.h"

namespace openswmmvis::ui {

namespace {

// Gap B1 — both helpers delegate to the canonical LegendContent copy so
// the dock tree, the on-canvas legend and the per-class edit routing read
// the SAME rows (results layers now aggregate their per-kind renderers
// instead of showing the dormant layer-level renderer).
QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style)
{
    return openswmmvis::map::LegendContent::firstSymbolColor(style);
}

QList<OpenSWMM::Render::LegendSymbolItem> legendItemsFor(OpenSWMMVisLayer *layer)
{
    return openswmmvis::map::LegendContent::legendItemsFor(layer);
}

// Encodes parent/child position in QModelIndex::internalId. Top-level
// (layer-header) rows carry id == -1; child rows carry the parent layer
// index. The QAbstractItemView reads this through index().internalId().
constexpr quintptr kTopLevelId = static_cast<quintptr>(-1);

} // namespace

LegendLayerTreeModel::LegendLayerTreeModel(MapCanvas *canvas, QObject *parent)
    : QAbstractItemModel(parent), m_canvas(canvas)
{
    if (m_canvas) {
        connect(m_canvas, &MapCanvas::layerAdded,        this, &LegendLayerTreeModel::onCanvasLayersChanged);
        connect(m_canvas, &MapCanvas::layerRemoved,      this, &LegendLayerTreeModel::onCanvasLayersChanged);
        connect(m_canvas, &MapCanvas::layerOrderChanged, this, &LegendLayerTreeModel::onCanvasLayersChanged);
    }
    rebuildLayerCache();
}

LegendLayerTreeModel::~LegendLayerTreeModel()
{
    disconnectAllLayers();
}

void LegendLayerTreeModel::setOverlayStyle(OpenSWMM::Render::LegendOverlayStyle *style)
{
    if (m_style == style) return;
    if (m_style)
        disconnect(m_style.data(), nullptr, this, nullptr);
    m_style = style;
    if (m_style) {
        // Per-item override edits (visibility / rename) → refresh affected
        // rows. Fine-grained signal, but rebuilding the whole cache is
        // simpler and still cheap given typical row counts.
        connect(m_style.data(), &OpenSWMM::Render::LegendOverlayStyle::itemOverrideChanged,
                this, &LegendLayerTreeModel::onLayerRepaintRequested,
                Qt::UniqueConnection);
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
    // Any layer-level shift (renderer mutation, visibility, name change)
    // may have changed legend rows. Reset is cheap given the small list
    // and matches the MVC "single source of truth" rule — we read fresh.
    beginResetModel();
    rebuildLayerCache();
    endResetModel();
}

void LegendLayerTreeModel::disconnectAllLayers()
{
    for (auto &p : m_connectedLayers) {
        if (p) disconnect(p, nullptr, this, nullptr);
    }
    m_connectedLayers.clear();
    // VS.9 — drop sublayer connections too.
    for (auto &p : m_connectedSublayers) {
        if (p) disconnect(p, nullptr, this, nullptr);
    }
    m_connectedSublayers.clear();
}

void LegendLayerTreeModel::connectLayer(OpenSWMMVisLayer *layer)
{
    if (!layer) return;
    connect(layer, &OpenSWMMVisLayer::repaintRequested,
            this, &LegendLayerTreeModel::onLayerRepaintRequested,
            Qt::UniqueConnection);
    connect(layer, &OpenSWMMVisLayer::visibilityChanged,
            this, &LegendLayerTreeModel::onLayerRepaintRequested,
            Qt::UniqueConnection);
    connect(layer, &OpenSWMMVisLayer::nameChanged,
            this, &LegendLayerTreeModel::onLayerRepaintRequested,
            Qt::UniqueConnection);
    m_connectedLayers.append(layer);

    // Gap B1 — renderer swaps (kind-tree edits, variable retargeting) and
    // result-variable changes must refresh the legend rows; neither emits
    // repaintRequested in every path, so subscribe explicitly.
    if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer)) {
        connect(rl, &SWMMResultsLayer::rendererChanged,
                this, &LegendLayerTreeModel::onLayerRepaintRequested,
                Qt::UniqueConnection);
        // Signal carries the variable; the slot ignores it (Qt drops
        // trailing signal arguments). A member-function slot is required —
        // Qt::UniqueConnection asserts on lambdas.
        connect(rl, &SWMMResultsLayer::variableChanged,
                this, &LegendLayerTreeModel::onLayerRepaintRequested,
                Qt::UniqueConnection);
    }
    if (auto *l2d = qobject_cast<SWMM2DResultsLayer *>(layer)) {
        connect(l2d, &SWMM2DResultsLayer::rendererChanged,
                this, &LegendLayerTreeModel::onLayerRepaintRequested,
                Qt::UniqueConnection);
    }

    // VS.9 — a sublayer's style / visibility / opacity edit emits
    // invalidated() and repaints the canvas, but does NOT raise the parent
    // layer's repaintRequested, so the legend would otherwise go stale.
    // Route each sublayer's invalidated() through the same cheap reset.
    if (auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(layer)) {
        for (OpenSWMM::Render::ISublayer *s : host->sublayers()) {
            if (!s) continue;
            connect(s, &OpenSWMM::Render::ISublayer::invalidated,
                    this, &LegendLayerTreeModel::onLayerRepaintRequested,
                    Qt::UniqueConnection);
            m_connectedSublayers.append(s);
        }
    }
}

void LegendLayerTreeModel::rebuildLayerCache()
{
    disconnectAllLayers();
    m_layers.clear();
    if (!m_canvas) return;

    // Walk in reverse paint order so the topmost layer appears at the
    // top of the tree, matching the on-canvas legend's reading order.
    for (int i = m_canvas->layers().size() - 1; i >= 0; --i) {
        OpenSWMMVisLayer *layer = m_canvas->layers().at(i);
        if (!layer) continue;

        LayerNode node;
        node.layer = layer;
        node.name  = layer->name();
        // Gap B1 — single dispatch through LegendContent (covers the
        // single-renderer layers AND the multi-kind facades: SWMMModelLayer,
        // SWMMResultsLayer).
        node.editableColor = openswmmvis::map::LegendContent::supportsClassEdit(
            layer, OpenSWMM::Render::ClassEditKind::Color);
        node.editableSize = openswmmvis::map::LegendContent::supportsClassEdit(
            layer, OpenSWMM::Render::ClassEditKind::Size);

        // Apply per-item overrides from the shared style so the tree shows
        // the same visibility + label state as the on-canvas overlay.
        const QString layerKey =
            m_style ? OpenSWMM::Render::LegendOverlayStyle::itemKey(layer, {})
                    : QString();
        auto *renderer = openswmmvis::map::LegendContent::featureRendererFor(layer);
        auto *modelLayer = qobject_cast<SWMMModelLayer *>(layer);    // X4
        auto *resultsLayer = qobject_cast<SWMMResultsLayer *>(layer); // Gap B1
        for (const auto &row : legendItemsFor(layer)) {
            ItemRow ir;
            ir.classKey   = row.classKey;
            ir.label      = row.effectiveLabel();
            ir.color      = firstSymbolColor(row.symbol);
            ir.visible    = row.visible;
            ir.sublayerId = row.sublayerId;   // Slice S4 P5
            if (renderer && !row.classKey.isEmpty()) {
                const qreal s = renderer->sizeForClass(row.classKey);
                if (s > 0.0) ir.size = s;
            } else if (modelLayer && !row.classKey.isEmpty()) {
                const qreal s = modelLayer->sizeForClass(row.classKey);
                if (s > 0.0) ir.size = s;
            } else if (resultsLayer && !row.classKey.isEmpty()) {
                const qreal s = resultsLayer->sizeForClass(row.classKey);
                if (s > 0.0) ir.size = s;
            }
            if (m_style && !row.classKey.isEmpty()) {
                const auto ov = m_style->itemOverride(layerKey, row.classKey);
                if (!ov.userLabel.isEmpty()) ir.label = ov.userLabel;
                if (!ov.visible)             ir.visible = false;
            }
            // Even hidden rows are kept in the tree so users can re-enable
            // them via the checkbox; the on-canvas legend filters them out.
            node.items.append(ir);
        }
        m_layers.append(node);
        connectLayer(layer);
    }
}

int LegendLayerTreeModel::columnCount(const QModelIndex & /*parent*/) const
{
    return ColCount;
}

int LegendLayerTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) return m_layers.size();
    if (parent.internalId() != kTopLevelId) return 0;   // grandchildren disallowed
    if (parent.row() < 0 || parent.row() >= m_layers.size()) return 0;
    return m_layers.at(parent.row()).items.size();
}

QModelIndex LegendLayerTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= ColCount) return {};
    if (!parent.isValid()) {
        if (row >= m_layers.size()) return {};
        return createIndex(row, column, kTopLevelId);
    }
    if (parent.internalId() != kTopLevelId) return {};
    if (parent.row() < 0 || parent.row() >= m_layers.size()) return {};
    if (row >= m_layers.at(parent.row()).items.size()) return {};
    return createIndex(row, column, static_cast<quintptr>(parent.row()));
}

QModelIndex LegendLayerTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) return {};
    if (child.internalId() == kTopLevelId) return {};
    const int layerRow = static_cast<int>(child.internalId());
    if (layerRow < 0 || layerRow >= m_layers.size()) return {};
    return createIndex(layerRow, 0, kTopLevelId);
}

QVariant LegendLayerTreeModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid()) return {};

    // Layer-header row.
    if (idx.internalId() == kTopLevelId) {
        if (idx.row() < 0 || idx.row() >= m_layers.size()) return {};
        const LayerNode &n = m_layers.at(idx.row());
        switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return idx.column() == ColItem ? n.name : QVariant{};
        case LayerPtrRole:
            return QVariant::fromValue(n.layer.data());
        case EditableRole:
            return false;   // header rows never editable
        default:
            return {};
        }
    }

    // Item row.
    const int layerRow = static_cast<int>(idx.internalId());
    if (layerRow < 0 || layerRow >= m_layers.size()) return {};
    const LayerNode &n = m_layers.at(layerRow);
    if (idx.row() < 0 || idx.row() >= n.items.size()) return {};
    const ItemRow &it = n.items.at(idx.row());

    switch (role) {
    case Qt::DisplayRole:
        if (idx.column() == ColItem) return it.label;
        if (idx.column() == ColSize && it.size > 0.0) return QString::number(it.size, 'f', 1);
        return {};
    case Qt::EditRole:
        // ColItem: editable userLabel (overrides the renderer label).
        // ColColor: the QColor value the delegate writes back through.
        // ColSize: the qreal size for the spinbox delegate.
        if (idx.column() == ColItem)  return it.label;
        if (idx.column() == ColColor) return it.color;
        if (idx.column() == ColSize)  return it.size > 0.0 ? it.size : QVariant{};
        return {};
    case Qt::CheckStateRole:
        // Slice BB Phase 8.6.10 / 8.6.16 — per-item visibility checkbox
        // lives on the Item column of every item row (when style is set).
        if (m_style && idx.column() == ColItem && !it.classKey.isEmpty())
            return it.visible ? Qt::Checked : Qt::Unchecked;
        return {};
    case Qt::DecorationRole:
        return idx.column() == ColColor ? it.color : QVariant{};
    case ClassKeyRole:
        return it.classKey;
    case LayerPtrRole:
        return QVariant::fromValue(n.layer.data());
    case EditableRole:
        if (idx.column() == ColColor) return n.editableColor && !it.classKey.isEmpty();
        if (idx.column() == ColSize)  return n.editableSize  && !it.classKey.isEmpty() && it.size > 0.0;
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
    if (!idx.isValid()) return false;
    if (idx.internalId() == kTopLevelId) return false;

    const int layerRow = static_cast<int>(idx.internalId());
    if (layerRow < 0 || layerRow >= m_layers.size()) return false;
    LayerNode &n = m_layers[layerRow];
    if (!n.layer) return false;
    if (idx.row() < 0 || idx.row() >= n.items.size()) return false;
    const ItemRow &it = n.items.at(idx.row());

    // ── Visibility checkbox (CheckStateRole on ColItem) ───────────────
    if (role == Qt::CheckStateRole && idx.column() == ColItem && m_style
        && !it.classKey.isEmpty()) {
        const QString layerKey =
            OpenSWMM::Render::LegendOverlayStyle::itemKey(n.layer, {});
        const bool checked = value.toInt() == Qt::Checked;
        m_style->setItemVisible(layerKey, it.classKey, checked);
        // itemOverrideChanged → onLayerRepaintRequested triggers a reset
        // so cached rows reflect the new visibility on next read.
        return true;
    }

    // ── Label edit (EditRole on ColItem) ──────────────────────────────
    if (role == Qt::EditRole && idx.column() == ColItem && m_style
        && !it.classKey.isEmpty()) {
        const QString layerKey =
            OpenSWMM::Render::LegendOverlayStyle::itemKey(n.layer, {});
        m_style->setItemUserLabel(layerKey, it.classKey, value.toString());
        return true;
    }

    // ── Color edit (EditRole on ColColor) ─────────────────────────────
    if (role == Qt::EditRole && idx.column() == ColColor) {
        if (!n.editableColor || it.classKey.isEmpty()) return false;
        const QColor picked = value.value<QColor>();
        if (!picked.isValid()) return false;
        // Push through the project undo stack so the edit reverses uniformly
        // across every view of the same data.
        if (auto *stack = m_canvas ? m_canvas->undoStack() : nullptr) {
            stack->push(new openswmmvis::map::SetRendererClassColorCommand(
                n.layer.data(), it.classKey, picked));
        }
        emit dataChanged(idx, idx, { Qt::DisplayRole, Qt::EditRole, Qt::DecorationRole });
        return true;
    }

    // ── Size edit (EditRole on ColSize) ───────────────────────────────
    if (role == Qt::EditRole && idx.column() == ColSize) {
        if (!n.editableSize || it.classKey.isEmpty()) return false;
        bool ok = false;
        const qreal s = value.toReal(&ok);
        if (!ok || s <= 0.0) return false;
        if (auto *stack = m_canvas ? m_canvas->undoStack() : nullptr) {
            stack->push(new openswmmvis::map::SetRendererClassSizeCommand(
                n.layer.data(), it.classKey, s));
        }
        emit dataChanged(idx, idx, { Qt::DisplayRole, Qt::EditRole });
        return true;
    }

    return false;
}

Qt::ItemFlags LegendLayerTreeModel::flags(const QModelIndex &idx) const
{
    if (!idx.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (idx.internalId() == kTopLevelId) return f;     // headers not editable
    const int layerRow = static_cast<int>(idx.internalId());
    if (layerRow < 0 || layerRow >= m_layers.size()) return f;
    const auto &n = m_layers.at(layerRow);

    if (idx.row() < 0 || idx.row() >= n.items.size()) return f;
    const auto &it = n.items.at(idx.row());

    if (idx.column() == ColColor && n.editableColor && !it.classKey.isEmpty()) {
        f |= Qt::ItemIsEditable;
    }
    if (idx.column() == ColSize && n.editableSize && !it.classKey.isEmpty()
        && it.size > 0.0) {
        f |= Qt::ItemIsEditable;
    }
    if (idx.column() == ColItem && m_style && !it.classKey.isEmpty()) {
        // Visibility checkbox + editable label both live on the Item col.
        f |= Qt::ItemIsUserCheckable | Qt::ItemIsEditable;
    }
    return f;
}

QVariant LegendLayerTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColItem:  return tr("Item");
    case ColColor: return tr("Color");
    case ColSize:  return tr("Size");
    default:       return {};
    }
}

} // namespace openswmmvis::ui
