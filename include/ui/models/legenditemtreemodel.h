/**
 * @file      legenditemtreemodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     The map legend's items as a tree for the Legend Properties dialog: layers, their
 *            rows, user groups and text lines, with a check box (shown in the legend) and an
 *            editable label. Also the edits of the legend's structure (order, groups, text).
 *
 *         The model reads LegendContent::resolveTree() and writes the legend style's tree, so
 *         every edit is one undo step and every view of the legend follows it. It rebuilds when
 *         the style or the map's layers change; use keyOf()/indexOfKey() to keep a selection.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_MODELS_LEGENDITEMTREEMODEL_H
#define OPENSWMMVIS_UI_MODELS_LEGENDITEMTREEMODEL_H

#include "map/legendcontent.h"

#include <QAbstractItemModel>
#include <QPointer>

#include <functional>
#include <memory>
#include <vector>

class MapCanvas;
class OpenSWMMVisLayer;

namespace OpenSWMM::Render
{
class LegendOverlayStyle;
struct LegendNode;
} // namespace OpenSWMM::Render

namespace openswmmvis::ui
{

/*!
 * \class LegendItemTreeModel
 * \brief One-column tree of the legend's items.
 */
class LegendItemTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    /** @brief Read the legend tree of @p canvas and @p style.
     * @param canvas Map; borrowed. @param style Legend settings; borrowed.
     * @param parent Optional owner. */
    LegendItemTreeModel(MapCanvas *canvas, OpenSWMM::Render::LegendOverlayStyle *style,
                        QObject *parent = nullptr);
    ~LegendItemTreeModel() override;

    /** @brief Stable key of an item ("L:" layer, "I:" layer row, "N:" group or text). */
    [[nodiscard]] QString keyOf(const QModelIndex &index) const;
    /** @brief The item with @p key, or an invalid index. */
    [[nodiscard]] QModelIndex indexOfKey(const QString &key) const;
    /** @brief The resolved item at @p index, or null. */
    [[nodiscard]] const openswmmvis::map::LegendContent::LegendTreeItem *
    itemAt(const QModelIndex &index) const;
    /** @brief The layer key of a layer or layer row item; empty for groups and text. */
    [[nodiscard]] QString layerKeyOf(const QModelIndex &index) const;

    /** @brief Change the settings of the item at @p index (stored node created when missing).
     * @param index Item. @param edit Change applied to its node.
     * @return Whether the style changed. */
    bool editSettings(const QModelIndex &index,
                      const std::function<void(OpenSWMM::Render::LegendNode &)> &edit);

    /** @brief Whether the legend's order is the user's (structure edits are possible). */
    [[nodiscard]] bool isManual() const;
    /** @brief Move an item among its siblings by @p delta. @return Whether it moved. */
    bool moveBy(const QModelIndex &index, int delta);
    /** @brief Move a layer, group or text line into the group just above it. */
    bool moveIntoGroup(const QModelIndex &index);
    /** @brief Move a layer, group or text line out of its group, to just after it. */
    bool moveOutOfGroup(const QModelIndex &index);
    /** @brief Add a group named @p name after @p after (at the end when invalid).
     * @return Key of the new group, or empty. */
    QString addGroup(const QModelIndex &after, const QString &name);
    /** @brief Add a text line after @p after (at the end when invalid). @return Its key. */
    QString addText(const QModelIndex &after, const QString &text);
    /** @brief Remove a layer, group or text line from the legend; a group's contents move up to
     *  its place. @return Whether it was removed. */
    bool removeItem(const QModelIndex &index);
    /** @brief Map layers the legend does not list. */
    [[nodiscard]] QList<OpenSWMMVisLayer *> missingLayers() const;
    /** @brief Add @p layers at the top of the legend, in map order. */
    void addLayers(const QList<OpenSWMMVisLayer *> &layers);

    // QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

public slots:
    /** @brief Read the legend tree again (the model is reset). */
    void rebuild();

signals:
    /** @brief The model was rebuilt; selections must be restored by key. */
    void rebuilt();

private:
    struct Node;
    Node *nodeAt(const QModelIndex &index) const;
    QModelIndex indexOfNode(const Node *node) const;
    void scheduleRebuild();

    QPointer<MapCanvas> m_canvas;
    QPointer<OpenSWMM::Render::LegendOverlayStyle> m_style;
    std::unique_ptr<Node> m_root;
    bool m_rebuildPending = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_LEGENDITEMTREEMODEL_H
