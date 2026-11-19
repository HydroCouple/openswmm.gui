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
#include <QHash>
#include <QPalette>
#include <QTimer>

#include <utility>

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
    bool hiddenWithOthers = false; ///< a row left out with its heading or color bar
    bool layerShown = true;        ///< the item's layer is shown on the map
    bool isSublayer = false;       ///< a sublayer without a heading row, over its rows
    bool hasNumbers = false;       ///< Layer: the layer has a number format
    OpenSWMM::Render::NumberLabelFormat numbers; ///< Layer: its number format
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

void build(const LegendOverlayStyle *style, const QList<LegendTreeItem> &items, auto *parent,
           const QString &layerKey)
{
    for (const LegendTreeItem &item : items) {
        auto node = std::make_unique<std::remove_pointer_t<decltype(parent)>>();
        node->item = item;
        node->item.children.clear();
        node->layerShown = !item.layer || item.layer->isVisible();
        if (item.node.type == Type::Layer && item.layer)
            node->hasNumbers = item.layer->legendNumberFormat(&node->numbers);
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
        if (item.node.type != Type::Layer) {
            build(style, item.children, raw, raw->layerKey);
            continue;
        }
        // A layer's rows as a tree: sub-headings over their sections, sublayers (and SWMM
        // element types) over their rows, other rows on their own.
        using NodeT = std::remove_pointer_t<decltype(parent)>;
        const QList<bool> hidden = openswmmvis::map::LegendContent::rowsHiddenWithOthers(item, style);
        const auto rowNode = [&](int r, NodeT *under) {
            const LegendTreeItem &child = item.children[r];
            auto n = std::make_unique<NodeT>();
            n->item = child;
            n->item.children.clear();
            n->layerShown = !child.layer || child.layer->isVisible();
            n->parent = under;
            n->layerKey = raw->layerKey;
            n->key = QStringLiteral("I:") + raw->layerKey + QLatin1Char('\n') + child.node.id;
            n->hiddenWithOthers = hidden[r];
            NodeT *at = n.get();
            under->children.push_back(std::move(n));
            return at;
        };
        QHash<QString, int> seen; // a sublayer split in two places keeps distinct keys
        for (const auto &g : openswmmvis::map::LegendContent::rowGroups(item)) {
            NodeT *owner = nullptr;
            if (g.row >= 0) {
                owner = rowNode(g.row, raw);
            } else {
                auto n = std::make_unique<NodeT>();
                n->isSublayer = true;
                n->item.layer = item.layer;
                n->item.sourceLabel = g.label;
                n->layerShown = raw->layerShown;
                n->parent = raw;
                n->layerKey = raw->layerKey;
                n->key = QStringLiteral("S:") + raw->layerKey + QLatin1Char('\n') + g.key +
                         QLatin1Char('\n') + QString::number(seen[g.key]++);
                owner = n.get();
                raw->children.push_back(std::move(n));
            }
            for (const int c : g.children)
                rowNode(c, owner);
        }
    }
}

/** Whether two built trees list the same items in the same places. */
bool sameShape(const auto &a, const auto &b)
{
    if (a.key != b.key || a.children.size() != b.children.size())
        return false;
    for (size_t i = 0; i < a.children.size(); ++i)
        if (!sameShape(*a.children[i], *b.children[i]))
            return false;
    return true;
}

/** Whether two items at the same place show the same (text, check state, gray). */
bool sameContent(const auto &a, const auto &b)
{
    return a.item.node == b.item.node && a.item.sourceLabel == b.item.sourceLabel &&
           a.item.row.visible == b.item.row.visible && a.hiddenWithOthers == b.hiddenWithOthers &&
           a.layerShown == b.layerShown && a.hasNumbers == b.hasNumbers && a.numbers == b.numbers;
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
    m_forceRefresh = true;
    scheduleLayerRefresh();
}

void LegendItemTreeModel::scheduleLayerRefresh()
{
    if (m_rebuildPending)
        return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, &LegendItemTreeModel::rebuild);
}

void LegendItemTreeModel::rebuild()
{
    m_rebuildPending = false;
    const bool force = std::exchange(m_forceRefresh, false);
    auto fresh = std::make_unique<Node>();
    if (m_canvas) {
        build(m_style.data(), openswmmvis::map::LegendContent::resolveTree(m_canvas, m_style),
              fresh.get(), QString());
        // A renamed layer or changed renderer shows at once.
        for (OpenSWMMVisLayer *layer : m_canvas->layers()) {
            if (!layer)
                continue;
            disconnect(layer, nullptr, this, nullptr);
            connect(layer, &OpenSWMMVisLayer::nameChanged, this,
                    &LegendItemTreeModel::scheduleLayerRefresh);
            connect(layer, &OpenSWMMVisLayer::visibilityChanged, this,
                    &LegendItemTreeModel::scheduleLayerRefresh);
            connect(layer, &OpenSWMMVisLayer::repaintRequested, this,
                    &LegendItemTreeModel::scheduleLayerRefresh);
        }
    }
    // A layer that only repainted (an animated one does all the time) changes nothing here: the
    // model is left alone, so a click or an edit in progress is not lost to a reset. Changed
    // items are updated in place; only a different set of items resets the model.
    if (sameShape(*m_root, *fresh)) {
        if (adopt(m_root.get(), fresh.get()) || force)
            emit rebuilt();
        return;
    }
    beginResetModel();
    m_root = std::move(fresh);
    endResetModel();
    emit rebuilt();
}

bool LegendItemTreeModel::adopt(Node *current, const Node *fresh)
{
    bool changed = false;
    for (size_t i = 0; i < current->children.size(); ++i) {
        Node *c = current->children[i].get();
        const Node *f = fresh->children[i].get();
        if (!sameContent(*c, *f)) {
            c->item = f->item;
            c->hiddenWithOthers = f->hiddenWithOthers;
            c->layerShown = f->layerShown;
            c->hasNumbers = f->hasNumbers;
            c->numbers = f->numbers;
            const QModelIndex at = indexOfNode(c);
            emit dataChanged(at, at);
            changed = true;
        }
        // A sublayer's check box sums up its rows.
        if (adopt(c, f)) {
            changed = true;
            if (c->isSublayer) {
                const QModelIndex at = indexOfNode(c);
                emit dataChanged(at, at);
            }
        }
    }
    return changed;
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

bool LegendItemTreeModel::isSublayer(const QModelIndex &index) const
{
    return index.isValid() && nodeAt(index)->isSublayer;
}

bool LegendItemTreeModel::isManual() const
{
    return m_style && !m_style->autoUpdate();
}

bool LegendItemTreeModel::editSettings(const QModelIndex &index,
                                       const std::function<void(LegendNode &)> &edit)
{
    if (!m_style || !index.isValid() || nodeAt(index)->isSublayer)
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
        const Node *layerNode = node->parent;
        while (layerNode && layerNode->item.node.type != Type::Layer)
            layerNode = layerNode->parent;
        if (!layer || !layerNode)
            return false;
        // The layer's rows in blocks, as shown: each entry under the layer with the rows under
        // it. An entry under the layer moves with its rows; a row under a heading or a
        // sublayer moves among that entry's rows.
        QList<QList<const Node *>> blocks;
        for (const auto &top : layerNode->children) {
            QList<const Node *> block;
            if (!top->isSublayer)
                block.append(top.get());
            for (const auto &c : top->children)
                block.append(c.get());
            blocks.append(block);
        }
        if (node->parent == layerNode) {
            const int to = node->row() + delta;
            if (to < 0 || to >= blocks.size())
                return false;
            blocks.move(node->row(), to);
        } else {
            const Node *owner = node->parent;
            QList<const Node *> &block = blocks[owner->row()];
            const int first = owner->isSublayer ? 0 : 1; // a heading leads its block
            const int at = first + node->row();
            const int to = at + delta;
            if (to < first || to >= block.size())
                return false;
            block.move(at, to);
        }
        // The stored rows in the new order (rows the renderer added since included).
        QList<LegendNode> rows;
        for (const auto &block : std::as_const(blocks))
            for (const Node *n : block) {
                LegendNode row = n->item.node;
                for (const LegendNode &c : layer->children)
                    if (c.id == row.id)
                        row = c;
                rows.append(row);
            }
        layer->children = rows;
        m_style->setItemTree(tree);
        return true;
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
        if (node->isSublayer) { // all, some or none of its rows shown
            int shown = 0;
            for (const auto &c : node->children)
                shown += c->item.node.visible ? 1 : 0;
            return shown == 0                             ? Qt::Unchecked
                   : shown == int(node->children.size()) ? Qt::Checked
                                                          : Qt::PartiallyChecked;
        }
        return item.node.visible ? Qt::Checked : Qt::Unchecked;
    case Qt::ToolTipRole:
        if (node->isSublayer)
            return tr("Sublayer: its check box shows or hides all its rows");
        switch (item.node.type) {
        case Type::Layer:
            return item.layer && !item.layer->isVisible()
                       ? tr("Layer (hidden on the map, so not in the legend)")
                       : tr("Layer");
        case Type::Item:
            if (!item.row.visible)
                return tr("Row (hidden by the layer's style)");
            return node->hiddenWithOthers
                       ? tr("Row (left out of the legend with its heading or color bar)")
                       : tr("Row");
        case Type::Group:
            return tr("Group");
        case Type::Text:
            return tr("Text line");
        }
        return {};
    case Qt::ForegroundRole:
        // Layers hidden on the map, rows their style hides, and rows left out with their
        // heading or color bar do not draw.
        if ((item.node.type == Type::Layer && item.layer && !item.layer->isVisible()) ||
            (item.node.type == Type::Item && (!item.row.visible || node->hiddenWithOthers)))
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
        const Node *node = nodeAt(index);
        if (node->isSublayer) {
            // All its rows, as one edit.
            if (!m_style)
                return false;
            LegendItemTree tree = m_style->itemTree();
            for (const auto &c : node->children) {
                LegendNode &stored = tree.ensureItemNode(c->layerKey, c->item.node.id);
                if (stored.hasDefaultSettings()) // a row setting from an older file
                    stored.userLabel = c->item.node.userLabel;
                stored.visible = on;
            }
            m_style->setItemTree(tree);
            return true;
        }
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
    if (nodeAt(index)->isSublayer) // a check box, no name of its own
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsUserCheckable;
}

} // namespace openswmmvis::ui
