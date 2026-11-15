/**
 * @file      legenditemtree.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     The map legend's own structure and per-item settings: which layers and rows it
 *            shows, in what order, under which headings, plus groups and text lines the user
 *            adds.
 *
 *         While the legend follows the map ("auto update"), the tree only holds the nodes that
 *         carry a setting (a hidden row, a renamed layer, a column break); order comes from the
 *         map. When the user takes over the order, the tree holds the full structure: layers
 *         and user groups and text lines in the user's order, and each layer's rows in the
 *         user's order (rows the renderer adds later are appended).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LEGENDITEMTREE_H
#define OPENSWMM_RENDER_LEGENDITEMTREE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \struct LegendNode
 * \brief One node of the legend tree with its legend-only settings.
 */
struct LegendNode {
    /** @brief What the node is. Group and Text nodes are made by the user. */
    enum class Type { Layer, Item, Group, Text };
    /** @brief How a layer's or group's heading is drawn. */
    enum class HeadingStyle { Group, Subgroup, Hidden };
    /** @brief How a layer draws a continuous color ramp. */
    enum class RampChoice { Inherit, Bar, Swatches };

    Type type = Type::Item;
    QString id;        ///< Layer: layer key. Item: renderer class key. Group/Text: unique id.
    QString userLabel; ///< Empty = the source label; for Text, the text.
    bool visible = true;
    HeadingStyle heading = HeadingStyle::Group; ///< Layer and Group nodes.
    bool columnBreakBefore = false;             ///< Start a new column at this node.
    int itemColumns = 0; ///< Layer: lay its rows out in this many sub-columns (0 or 1 = off).
    RampChoice ramp = RampChoice::Inherit; ///< Layer: color ramp as a bar or as swatches.
    int symbolWidth = 0;                   ///< Symbol slot width; 0 = the style's.
    int symbolHeight = 0;                  ///< Symbol slot height; 0 = the style's.
    QList<LegendNode> children; ///< Display order. Layer: Items. Group: Layers, Groups, Text.

    /** @brief Whether every setting is at its default (children not considered). */
    [[nodiscard]] bool hasDefaultSettings() const;
    /** @brief Copy of the settings without the children. */
    [[nodiscard]] LegendNode settingsOnly() const;
    /** @brief Save the node; default settings are left out.
     * @return {type, id, label?, visible?, heading?, columnBreak?, itemColumns?, ramp?, symbolW?,
     *   symbolH?, children?}. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Read a node saved by toJson(); unknown names keep defaults.
     * @param j Saved node.
     * @return The node. */
    [[nodiscard]] static LegendNode fromJson(const QJsonObject &j);

    bool operator==(const LegendNode &other) const = default;
};

/*!
 * \class LegendItemTree
 * \brief The legend's nodes: a list of top-level Layer, Group and Text nodes.
 */
class LegendItemTree
{
public:
    /** @brief Top-level nodes in display order. */
    [[nodiscard]] const QList<LegendNode> &roots() const { return m_roots; }
    /** @brief Top-level nodes for editing. */
    [[nodiscard]] QList<LegendNode> &roots() { return m_roots; }

    /** @brief The node of a layer, anywhere in the tree, or null.
     * @param layerKey Layer key. */
    [[nodiscard]] const LegendNode *layerNode(const QString &layerKey) const;
    /** @copydoc layerNode(const QString &) const */
    [[nodiscard]] LegendNode *layerNode(const QString &layerKey);
    /** @brief The node of a layer, added at the end of the top level when missing.
     * @param layerKey Layer key.
     * @return Reference valid until the tree is next changed. */
    LegendNode &ensureLayerNode(const QString &layerKey);
    /** @brief The node of one of a layer's rows, or null.
     * @param layerKey Layer key. @param classKey Renderer class key. */
    [[nodiscard]] const LegendNode *itemNode(const QString &layerKey,
                                             const QString &classKey) const;
    /** @brief The node of one of a layer's rows, added (with its layer) when missing.
     * @param layerKey Layer key. @param classKey Renderer class key.
     * @return Reference valid until the tree is next changed. */
    LegendNode &ensureItemNode(const QString &layerKey, const QString &classKey);
    /** @brief A Group or Text node by its id, anywhere in the tree, or null.
     * @param id Node id. */
    [[nodiscard]] LegendNode *nodeById(const QString &id);
    /** @copydoc nodeById(const QString &) */
    [[nodiscard]] const LegendNode *nodeById(const QString &id) const;

    /** @brief Drop nodes that carry no setting and no children (the tree while the legend
     *  follows the map). */
    void prune();
    /** @brief Keep only the per-layer and per-row settings: Group and Text nodes go, the layers
     *  inside groups move to the top level (turning "auto update" back on). */
    void flattenToLayers();

    /** @brief Save the tree. @return Array of top-level nodes. */
    [[nodiscard]] QJsonArray toJson() const;
    /** @brief Read a tree saved by toJson(). @param a Saved array. @return The tree. */
    [[nodiscard]] static LegendItemTree fromJson(const QJsonArray &a);

    /** @brief A new unique id for a Group or Text node. */
    [[nodiscard]] static QString newNodeId();

    bool operator==(const LegendItemTree &other) const = default;

private:
    QList<LegendNode> m_roots;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDITEMTREE_H
