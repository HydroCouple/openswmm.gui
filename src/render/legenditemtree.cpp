/**
 * @file      legenditemtree.cpp
 * @brief     The map legend's own structure and per-item settings.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/legenditemtree.h"

#include <QJsonValue>
#include <QUuid>

#include <algorithm>

namespace OpenSWMM::Render
{

namespace
{

QString typeName(LegendNode::Type t)
{
    switch (t) {
    case LegendNode::Type::Layer:
        return QStringLiteral("layer");
    case LegendNode::Type::Group:
        return QStringLiteral("group");
    case LegendNode::Type::Text:
        return QStringLiteral("text");
    case LegendNode::Type::Item:
        break;
    }
    return QStringLiteral("item");
}

LegendNode::Type typeFromName(const QString &s)
{
    if (s == QLatin1String("layer"))
        return LegendNode::Type::Layer;
    if (s == QLatin1String("group"))
        return LegendNode::Type::Group;
    if (s == QLatin1String("text"))
        return LegendNode::Type::Text;
    return LegendNode::Type::Item;
}

QString headingName(LegendNode::HeadingStyle h)
{
    switch (h) {
    case LegendNode::HeadingStyle::Subgroup:
        return QStringLiteral("subgroup");
    case LegendNode::HeadingStyle::Hidden:
        return QStringLiteral("hidden");
    case LegendNode::HeadingStyle::Group:
        break;
    }
    return QStringLiteral("group");
}

LegendNode::HeadingStyle headingFromName(const QString &s)
{
    if (s == QLatin1String("subgroup"))
        return LegendNode::HeadingStyle::Subgroup;
    if (s == QLatin1String("hidden"))
        return LegendNode::HeadingStyle::Hidden;
    return LegendNode::HeadingStyle::Group;
}

QString rampName(LegendNode::RampChoice r)
{
    switch (r) {
    case LegendNode::RampChoice::Bar:
        return QStringLiteral("bar");
    case LegendNode::RampChoice::Swatches:
        return QStringLiteral("swatches");
    case LegendNode::RampChoice::Inherit:
        break;
    }
    return QStringLiteral("inherit");
}

LegendNode::RampChoice rampFromName(const QString &s)
{
    if (s == QLatin1String("bar"))
        return LegendNode::RampChoice::Bar;
    if (s == QLatin1String("swatches"))
        return LegendNode::RampChoice::Swatches;
    return LegendNode::RampChoice::Inherit;
}

template <typename Node> Node *findLayer(QList<Node> &nodes, const QString &key)
{
    for (Node &n : nodes) {
        if (n.type == LegendNode::Type::Layer && n.id == key)
            return &n;
        if (n.type == LegendNode::Type::Group)
            if (Node *inner = findLayer(n.children, key))
                return inner;
    }
    return nullptr;
}

template <typename Node> Node *findById(QList<Node> &nodes, const QString &id)
{
    for (Node &n : nodes) {
        if ((n.type == LegendNode::Type::Group || n.type == LegendNode::Type::Text) && n.id == id)
            return &n;
        if (n.type == LegendNode::Type::Group)
            if (Node *inner = findById(n.children, id))
                return inner;
    }
    return nullptr;
}

void pruneList(QList<LegendNode> &nodes)
{
    for (auto it = nodes.begin(); it != nodes.end();) {
        pruneList(it->children);
        const bool keep = !it->hasDefaultSettings() || !it->children.isEmpty() ||
                          it->type == LegendNode::Type::Group || it->type == LegendNode::Type::Text;
        it = keep ? it + 1 : nodes.erase(it);
    }
}

void collectLayers(const QList<LegendNode> &nodes, QList<LegendNode> &out)
{
    for (const LegendNode &n : nodes) {
        if (n.type == LegendNode::Type::Layer)
            out.append(n);
        else if (n.type == LegendNode::Type::Group)
            collectLayers(n.children, out);
    }
}

} // namespace

bool LegendNode::hasDefaultSettings() const
{
    const LegendNode defaults;
    return userLabel.isEmpty() && visible == defaults.visible && heading == defaults.heading &&
           columnBreakBefore == defaults.columnBreakBefore && itemColumns == defaults.itemColumns &&
           ramp == defaults.ramp && symbolWidth == defaults.symbolWidth &&
           symbolHeight == defaults.symbolHeight;
}

LegendNode LegendNode::settingsOnly() const
{
    LegendNode n = *this;
    n.children.clear();
    return n;
}

QJsonObject LegendNode::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("type")] = typeName(type);
    j[QStringLiteral("id")] = id;
    if (!userLabel.isEmpty())
        j[QStringLiteral("label")] = userLabel;
    if (!visible)
        j[QStringLiteral("visible")] = false;
    if (heading != HeadingStyle::Group)
        j[QStringLiteral("heading")] = headingName(heading);
    if (columnBreakBefore)
        j[QStringLiteral("columnBreak")] = true;
    if (itemColumns > 1)
        j[QStringLiteral("itemColumns")] = itemColumns;
    if (ramp != RampChoice::Inherit)
        j[QStringLiteral("ramp")] = rampName(ramp);
    if (symbolWidth > 0)
        j[QStringLiteral("symbolW")] = symbolWidth;
    if (symbolHeight > 0)
        j[QStringLiteral("symbolH")] = symbolHeight;
    if (!children.isEmpty()) {
        QJsonArray a;
        for (const LegendNode &c : children)
            a.append(c.toJson());
        j[QStringLiteral("children")] = a;
    }
    return j;
}

LegendNode LegendNode::fromJson(const QJsonObject &j)
{
    LegendNode n;
    n.type = typeFromName(j.value(QStringLiteral("type")).toString());
    n.id = j.value(QStringLiteral("id")).toString();
    n.userLabel = j.value(QStringLiteral("label")).toString();
    n.visible = j.value(QStringLiteral("visible")).toBool(true);
    n.heading = headingFromName(j.value(QStringLiteral("heading")).toString());
    n.columnBreakBefore = j.value(QStringLiteral("columnBreak")).toBool(false);
    n.itemColumns = std::max(0, j.value(QStringLiteral("itemColumns")).toInt(0));
    n.ramp = rampFromName(j.value(QStringLiteral("ramp")).toString());
    n.symbolWidth = std::max(0, j.value(QStringLiteral("symbolW")).toInt(0));
    n.symbolHeight = std::max(0, j.value(QStringLiteral("symbolH")).toInt(0));
    for (const QJsonValue &c : j.value(QStringLiteral("children")).toArray())
        n.children.append(fromJson(c.toObject()));
    return n;
}

const LegendNode *LegendItemTree::layerNode(const QString &layerKey) const
{
    return findLayer(const_cast<QList<LegendNode> &>(m_roots), layerKey);
}

LegendNode *LegendItemTree::layerNode(const QString &layerKey)
{
    return findLayer(m_roots, layerKey);
}

LegendNode &LegendItemTree::ensureLayerNode(const QString &layerKey)
{
    if (LegendNode *n = layerNode(layerKey))
        return *n;
    LegendNode n;
    n.type = LegendNode::Type::Layer;
    n.id = layerKey;
    m_roots.append(n);
    return m_roots.last();
}

const LegendNode *LegendItemTree::itemNode(const QString &layerKey, const QString &classKey) const
{
    const LegendNode *layer = layerNode(layerKey);
    if (!layer)
        return nullptr;
    for (const LegendNode &c : layer->children)
        if (c.type == LegendNode::Type::Item && c.id == classKey)
            return &c;
    return nullptr;
}

LegendNode &LegendItemTree::ensureItemNode(const QString &layerKey, const QString &classKey)
{
    LegendNode &layer = ensureLayerNode(layerKey);
    for (LegendNode &c : layer.children)
        if (c.type == LegendNode::Type::Item && c.id == classKey)
            return c;
    LegendNode n;
    n.type = LegendNode::Type::Item;
    n.id = classKey;
    layer.children.append(n);
    return layer.children.last();
}

LegendNode *LegendItemTree::nodeById(const QString &id)
{
    return findById(m_roots, id);
}

const LegendNode *LegendItemTree::nodeById(const QString &id) const
{
    return findById(const_cast<QList<LegendNode> &>(m_roots), id);
}

void LegendItemTree::prune()
{
    pruneList(m_roots);
}

void LegendItemTree::flattenToLayers()
{
    QList<LegendNode> layers;
    collectLayers(m_roots, layers);
    m_roots = layers;
    prune();
}

QJsonArray LegendItemTree::toJson() const
{
    QJsonArray a;
    for (const LegendNode &n : m_roots)
        a.append(n.toJson());
    return a;
}

LegendItemTree LegendItemTree::fromJson(const QJsonArray &a)
{
    LegendItemTree t;
    for (const QJsonValue &v : a)
        t.m_roots.append(LegendNode::fromJson(v.toObject()));
    return t;
}

QString LegendItemTree::newNodeId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace OpenSWMM::Render
