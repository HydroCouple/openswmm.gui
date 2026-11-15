/**
 * @file      legenditemtreemodel.cpp
 * @brief     The map legend's items as a tree for the Legend Properties dialog.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/models/legenditemtreemodel.h"

#include "layers/openswmmvislayer.h"
#include "map/mapcanvas.h"
#include "render/legenditemtree.h"
#include "render/legendoverlaystyle.h"

#include <QBrush>
#include <QPalette>
#include <QTimer>

namespace openswmmvis::ui
{

using openswmmvis::map::LegendContent::LegendTreeItem;
using OpenSWMM::Render::LegendItemTree;
using OpenSWMM::Render::LegendNode;
using OpenSWMM::Render::LegendOverlayStyle;
using Type = LegendNode::Type;

struct LegendItemTreeModel::Node {
    LegendTreeItem item; ///< children unused; see Node::children
    QString key;
    QString layerKey;
    Node *parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;

    int row() const
    {
        if (!parent)
            return 0;
        for (size_t i = 0; i < parent->children.size(); ++i)
            if (parent->children[i].get() == this)
                return int(i);
        return 0;
    }
};

namespace
{

/** Where a node is in the stored tree: its sibling list and position. */
struct Spot {
    QList<LegendNode> *list = nullptr;
    int index = -1;
    explicit operator bool() const { return list && index >= 0; }
};

Spot locate(QList<LegendNode> &list, Type type, const QString &id)
{
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].type == type && list[i].id == id)
            return {&list, i};
        if (list[i].type == Type::Group)
            if (Spot inner = locate(list[i].children, type, id))
                return inner;
    }
    return {};
}

void build(LegendItemTreeModel *, const QList<LegendTreeItem> &items, auto *parent,
           const QString &layerKey)
{
    for (const LegendTreeItem &item : items) {
        auto node = std::make_unique<std::remove_pointer_t<decltype(parent)>>();
        node->item = item;
        node->item.children.clear();
        node->parent = parent;
        switch (item.node.type) {
        case Type::Layer:
            node->layerKey = item.node.id;
            node->key = QStringLiteral("L:") + item.node.id;
            break;
        case Type::Item:
            node->layerKey = layerKey;
            node->key = QStringLiteral("I:") + layerKey + QLatin1Char('\n') + item.node.id;
            break;
        case Type::Group:
        case Type::Text:
            node->key = QStringLiteral("N:") + item.node.id;
            break;
        }
        auto *raw = node.get();
        parent->children.push_back(std::move(node));
        build(nullptr, item.children, raw, raw->layerKey);
    }
}

} // namespace

LegendItemTreeModel::LegendItemTreeModel(MapCanvas *canvas, LegendOverlayStyle *style,
                                         QObject *parent)
    : QAbstractItemModel(parent), m_canvas(canvas), m_style(style), m_root(std::make_unique<Node>())
{
    if (m_style) {
        connect(m_style, &LegendOverlayStyle::itemTreeChanged, this,
                &LegendItemTreeModel::scheduleRebuild);
        connect(m_style, &LegendOverlayStyle::autoUpdateChanged, this,
                &LegendItemTreeModel::scheduleRebuild);
        connect(m_style, &LegendOverlayStyle::itemOverrideChanged, this,
                &LegendItemTreeModel::scheduleRebuild);
        connect(m_style, &LegendOverlayStyle::rampDisplayChanged, this,
                &LegendItemTreeModel::scheduleRebuild);
    }
    if (m_canvas) {
        connect(m_canvas, &MapCanvas::layerAdded, this, &LegendItemTreeModel::scheduleRebuild);
        connect(m_canvas, &MapCanvas::layerRemoved, this, &LegendItemTreeModel::scheduleRebuild);
        connect(m_canvas, &MapCanvas::layerOrderChanged, this,
                &LegendItemTreeModel::scheduleRebuild);
    }
    rebuild();
}

LegendItemTreeModel::~LegendItemTreeModel() = default;

void LegendItemTreeModel::scheduleRebuild()
{
    if (m_rebuildPending)
        return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, &LegendItemTreeModel::rebuild);
}

void LegendItemTreeModel::rebuild()
{
    m_rebuildPending = false;
    beginResetModel();
    m_root = std::make_unique<Node>();
    if (m_canvas) {
        build(this, openswmmvis::map::LegendContent::resolveTree(m_canvas, m_style), m_root.get(),
              QString());
        // A renamed layer or changed renderer shows at once.
        for (OpenSWMMVisLayer *layer : m_canvas->layers()) {
            if (!layer)
                continue;
            disconnect(layer, nullptr, this, nullptr);
            connect(layer, &OpenSWMMVisLayer::nameChanged, this,
                    &LegendItemTreeModel::scheduleRebuild);
            connect(layer, &OpenSWMMVisLayer::visibilityChanged, this,
                    &LegendItemTreeModel::scheduleRebuild);
            connect(layer, &OpenSWMMVisLayer::repaintRequested, this,
                    &LegendItemTreeModel::scheduleRebuild);
        }
    }
    endResetModel();
    emit rebuilt();
}

LegendItemTreeModel::Node *LegendItemTreeModel::nodeAt(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<Node *>(index.internalPointer()) : m_root.get();
}

QModelIndex LegendItemTreeModel::indexOfNode(const Node *node) const
{
    if (!node || node == m_root.get())
        return {};
    return createIndex(node->row(), 0, const_cast<Node *>(node));
}

QString LegendItemTreeModel::keyOf(const QModelIndex &index) const
{
    return index.isValid() ? nodeAt(index)->key : QString();
}

QModelIndex LegendItemTreeModel::indexOfKey(const QString &key) const
{
    if (key.isEmpty())
        return {};
    const std::function<const Node *(const Node *)> find = [&](const Node *n) -> const Node * {
        for (const auto &c : n->children) {
            if (c->key == key)
                return c.get();
            if (const Node *inner = find(c.get()))
                return inner;
        }
        return nullptr;
    };
    return indexOfNode(find(m_root.get()));
}

const LegendTreeItem *LegendItemTreeModel::itemAt(const QModelIndex &index) const
{
    return index.isValid() ? &nodeAt(index)->item : nullptr;
}

QString LegendItemTreeModel::layerKeyOf(const QModelIndex &index) const
{
    return index.isValid() ? nodeAt(index)->layerKey : QString();
}

bool LegendItemTreeModel::isManual() const
{
    return m_style && !m_style->autoUpdate();
}

bool LegendItemTreeModel::editSettings(const QModelIndex &index,
                                       const std::function<void(LegendNode &)> &edit)
{
    if (!m_style || !index.isValid())
        return false;
    const Node *node = nodeAt(index);
    LegendItemTree tree = m_style->itemTree();
    LegendNode *stored = nullptr;
    switch (node->item.node.type) {
    case Type::Layer:
        stored = &tree.ensureLayerNode(node->layerKey);
        break;
    case Type::Item:
        stored = &tree.ensureItemNode(node->layerKey, node->item.node.id);
        // A row setting read from an older file becomes the row's own.
        if (stored->hasDefaultSettings()) {
            stored->visible = node->item.node.visible;
            stored->userLabel = node->item.node.userLabel;
        }
        break;
    case Type::Group:
    case Type::Text:
        stored = tree.nodeById(node->item.node.id);
        break;
    }
    if (!stored)
        return false;
    edit(*stored);
    const LegendItemTree before = m_style->itemTree();
    m_style->setItemTree(tree);
    return !(before == m_style->itemTree());
}

bool LegendItemTreeModel::moveBy(const QModelIndex &index, int delta)
{
    if (!isManual() || !index.isValid() || delta == 0)
        return false;
    const Node *node = nodeAt(index);
    LegendItemTree tree = m_style->itemTree();
    Spot spot;
    if (node->item.node.type == Type::Item) {
        LegendNode *layer = tree.layerNode(node->layerKey);
        if (!layer)
            return false;
        // The stored rows in the order shown (rows the renderer added since included).
        QList<LegendNode> rows;
        for (const auto &sibling : node->parent->children) {
            LegendNode row = sibling->item.node;
            for (const LegendNode &c : layer->children)
                if (c.id == row.id)
                    row = c;
            rows.append(row);
        }
        layer->children = rows;
        spot = {&layer->children, node->row()};
    } else {
        spot = locate(tree.roots(), node->item.node.type, node->item.node.id);
    }
    if (!spot)
        return false;
    const int to = spot.index + delta;
    if (to < 0 || to >= spot.list->size())
        return false;
    spot.list->move(spot.index, to);
    m_style->setItemTree(tree);
    return true;
}

bool LegendItemTreeModel::moveIntoGroup(const QModelIndex &index)
{
    if (!isManual() || !index.isValid())
        return false;
    const Node *node = nodeAt(index);
    if (node->item.node.type == Type::Item)
        return false;
    LegendItemTree tree = m_style->itemTree();
    const Spot spot = locate(tree.roots(), node->item.node.type, node->item.node.id);
    if (!spot || spot.index == 0 || (*spot.list)[spot.index - 1].type != Type::Group)
        return false;
    LegendNode moved = spot.list->takeAt(spot.index);
    (*spot.list)[spot.index - 1].children.append(moved);
    m_style->setItemTree(tree);
    return true;
}

bool LegendItemTreeModel::moveOutOfGroup(const QModelIndex &index)
{
    if (!isManual() || !index.isValid())
        return false;
    const Node *node = nodeAt(index);
    const Node *group = node->parent;
    if (node->item.node.type == Type::Item || !group || group == m_root.get() ||
        group->item.node.type != Type::Group)
        return false;
    LegendItemTree tree = m_style->itemTree();
    const Spot groupSpot = locate(tree.roots(), Type::Group, group->item.node.id);
    if (!groupSpot)
        return false;
    LegendNode &g = (*groupSpot.list)[groupSpot.index];
    const Spot spot = locate(g.children, node->item.node.type, node->item.node.id);
    if (!spot || spot.list != &g.children)
        return false;
    LegendNode moved = g.children.takeAt(spot.index);
    groupSpot.list->insert(groupSpot.index + 1, moved);
    m_style->setItemTree(tree);
    return true;
}

namespace
{
/** Insert @p node after the top-level-kind item at @p after (or at the end). */
void insertAfter(LegendItemTree &tree, const LegendTreeItem *after, const QString &afterLayerKey,
                 const LegendNode &node)
{
    if (after) {
        Type type = after->node.type;
        QString id = after->node.id;
        if (type == Type::Item) { // after a row: after its layer
            type = Type::Layer;
            id = afterLayerKey;
        }
        if (const Spot spot = locate(tree.roots(), type, id)) {
            spot.list->insert(spot.index + 1, node);
            return;
        }
    }
    tree.roots().append(node);
}
} // namespace

QString LegendItemTreeModel::addGroup(const QModelIndex &after, const QString &name)
{
    if (!isManual())
        return {};
    LegendNode group;
    group.type = Type::Group;
    group.id = LegendItemTree::newNodeId();
    group.userLabel = name;
    LegendItemTree tree = m_style->itemTree();
    insertAfter(tree, itemAt(after), layerKeyOf(after), group);
    m_style->setItemTree(tree);
    return QStringLiteral("N:") + group.id;
}

QString LegendItemTreeModel::addText(const QModelIndex &after, const QString &text)
{
    if (!isManual())
        return {};
    LegendNode line;
    line.type = Type::Text;
    line.id = LegendItemTree::newNodeId();
    line.userLabel = text;
    LegendItemTree tree = m_style->itemTree();
    insertAfter(tree, itemAt(after), layerKeyOf(after), line);
    m_style->setItemTree(tree);
    return QStringLiteral("N:") + line.id;
}

bool LegendItemTreeModel::removeItem(const QModelIndex &index)
{
    if (!isManual() || !index.isValid())
        return false;
    const Node *node = nodeAt(index);
    if (node->item.node.type == Type::Item)
        return false;
    LegendItemTree tree = m_style->itemTree();
    const Spot spot = locate(tree.roots(), node->item.node.type, node->item.node.id);
    if (!spot)
        return false;
    const LegendNode removed = spot.list->takeAt(spot.index);
    if (removed.type == Type::Group) // its contents move up to its place
        for (int i = 0; i < removed.children.size(); ++i)
            spot.list->insert(spot.index + i, removed.children[i]);
    m_style->setItemTree(tree);
    return true;
}

QList<OpenSWMMVisLayer *> LegendItemTreeModel::missingLayers() const
{
    QList<OpenSWMMVisLayer *> out;
    if (!m_canvas || !isManual())
        return out;
    const auto layers = m_canvas->layers();
    for (int i = layers.size() - 1; i >= 0; --i) // top of the map first
        if (layers[i] && !m_style->itemTree().layerNode(LegendOverlayStyle::itemKey(layers[i], {})))
            out.append(layers[i]);
    return out;
}

void LegendItemTreeModel::addLayers(const QList<OpenSWMMVisLayer *> &layers)
{
    if (!isManual() || layers.isEmpty())
        return;
    LegendItemTree tree = m_style->itemTree();
    int at = 0;
    for (OpenSWMMVisLayer *layer : layers) {
        const QString key = LegendOverlayStyle::itemKey(layer, {});
        if (!layer || tree.layerNode(key))
            continue;
        LegendNode node;
        node.type = Type::Layer;
        node.id = key;
        tree.roots().insert(at++, node);
    }
    m_style->setItemTree(tree);
}

QModelIndex LegendItemTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    const Node *p = nodeAt(parent);
    if (column != 0 || row < 0 || row >= int(p->children.size()))
        return {};
    return createIndex(row, 0, p->children[size_t(row)].get());
}

QModelIndex LegendItemTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    return indexOfNode(nodeAt(child)->parent);
}

int LegendItemTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column() != 0)
        return 0;
    return int(nodeAt(parent)->children.size());
}

int LegendItemTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant LegendItemTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const Node *node = nodeAt(index);
    const LegendTreeItem &item = node->item;
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole: {
        const QString label = item.label();
        if (!label.isEmpty() || role == Qt::EditRole)
            return label;
        return item.node.type == Type::Group ? tr("(group)") : tr("(empty)");
    }
    case Qt::CheckStateRole:
        return item.node.visible ? Qt::Checked : Qt::Unchecked;
    case Qt::ToolTipRole:
        switch (item.node.type) {
        case Type::Layer:
            return item.layer && !item.layer->isVisible()
                       ? tr("Layer (hidden on the map, so not in the legend)")
                       : tr("Layer");
        case Type::Item:
            return item.row.visible ? tr("Row") : tr("Row (hidden by the layer's style)");
        case Type::Group:
            return tr("Group");
        case Type::Text:
            return tr("Text line");
        }
        return {};
    case Qt::ForegroundRole:
        // Layers hidden on the map, and rows their style hides, do not draw.
        if ((item.node.type == Type::Layer && item.layer && !item.layer->isVisible()) ||
            (item.node.type == Type::Item && !item.row.visible))
            return QBrush(QPalette().color(QPalette::Disabled, QPalette::Text));
        return {};
    default:
        return {};
    }
}

bool LegendItemTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;
    if (role == Qt::CheckStateRole) {
        const bool on = value.toInt() == Qt::Checked;
        return editSettings(index, [on](LegendNode &n) { n.visible = on; });
    }
    if (role == Qt::EditRole) {
        const QString text = value.toString();
        // An empty label restores the source name (a text line keeps its text).
        if (text.isEmpty() && nodeAt(index)->item.node.type == Type::Text)
            return false;
        return editSettings(index, [text](LegendNode &n) { n.userLabel = text; });
    }
    return false;
}

Qt::ItemFlags LegendItemTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsUserCheckable;
}

} // namespace openswmmvis::ui
