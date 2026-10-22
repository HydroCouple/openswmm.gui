/**
 * @file      groupedpropertymodel.h
 * @brief     Presentation grouping of existing property model items.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_GROUPED_PROPERTY_MODEL_H
#define OPENSWMM_GROUPED_PROPERTY_MODEL_H

#include "ui/properties/attributegroups.h"
#include <QAbstractProxyModel>
#include <QFont>
#include <QPersistentModelIndex>
#include <qpropertyitem.h>
#include <qvariantpropertyItem.h>
#include <algorithm>
#include <memory>
#include <vector>

// Presentation-only grouping: the original property items still own all
// editing, notification, unit-label and compound-editor behavior.
class GroupedPropertyModel : public QAbstractProxyModel
{
    struct Row {
        QPersistentModelIndex source;
        std::unique_ptr<QPropertyItem> heading;
        Row *parent = nullptr;
        std::vector<std::unique_ptr<Row>> children;
        /**
         * @brief Choose the stable internal identity used for this proxy row.
         * @return Borrowed synthetic heading pointer or underlying source item pointer; invalidated by a rebuild.
         */
        void *pointer() const
        {
            return heading ? heading.get() : QModelIndex(source).internalPointer();
        }
    };
    Row m_root;
    QHash<void *, Row *> m_rows;

    /**
     * @brief Resolve a proxy index to the stored grouping node.
     * @param[in] index Proxy index from this model, or invalid for the root.
     * @return Borrowed row, root for invalid input, or nullptr when an indexed pointer is not registered.
     */
    Row *row(const QModelIndex &index) const
    {
        return index.isValid() ? m_rows.value(index.internalPointer()) : const_cast<Row *>(&m_root);
    }
    /**
     * @brief Find a node's current sibling row and construct its proxy index.
     * @param[in] item Borrowed node belonging to this model; nullptr/root produce invalid indexes.
     * @param[in] column Column number to encode, normally zero or one.
     * @return Index for a found nonroot node, otherwise invalid.
     */
    QModelIndex indexFor(Row *item, int column = 0) const
    {
        if (!item || !item->parent)
            return {};
        const auto &siblings = item->parent->children;
        for (int i = 0; i < int(siblings.size()); ++i)
            if (siblings[i].get() == item)
                return createIndex(i, column, item->pointer());
        return {};
    }
    /**
     * @brief Reuse or append a noneditable synthetic group heading.
     * @param[in,out] parent Non-null parent row belonging to this model.
     * @param[in] name Exact heading text used for sibling reuse.
     * @return Borrowed node owned by parent children; valid until rebuild/reset.
     */
    Row *group(Row *parent, const QString &name)
    {
        for (const auto &child : parent->children)
            if (child->heading && child->heading->name() == name)
                return child.get();
        auto item = std::make_unique<Row>();
        item->parent = parent;
        item->heading = std::make_unique<QPropertyItem>(QVariant(), name, nullptr);
        item->heading->setFlags(Qt::ItemIsEnabled);
        auto *result = item.get();
        m_rows.insert(item->pointer(), result);
        parent->children.push_back(std::move(item));
        return result;
    }
    /**
     * @brief Mirror one source property and all its children below a grouping node.
     * @param[in] source Valid source index in column zero; sourceModel() must exist.
     * @param[in,out] parent Non-null destination row owning the appended subtree.
     * @details Stores persistent source indexes while retaining source item internal pointers for delegate compatibility.
     */
    void append(const QModelIndex &source, Row *parent)
    {
        auto item = std::make_unique<Row>();
        item->source = source;
        item->parent = parent;
        auto *result = item.get();
        m_rows.insert(item->pointer(), result);
        parent->children.push_back(std::move(item));
        for (int i = 0; i < sourceModel()->rowCount(source); ++i)
            append(sourceModel()->index(i, 0, source), result);
    }
    /**
     * @brief Flatten source class headings into attribute groups while retaining compound property children.
     * @param[in] parent Source-model parent to traverse; invalid starts at root.
     * @details Mutates this proxy's row map/tree and requires a valid property source model.
     */
    void collect(const QModelIndex &parent)
    {
        for (int i = 0; i < sourceModel()->rowCount(parent); ++i) {
            const auto source = sourceModel()->index(i, 0, parent);
            auto *property = dynamic_cast<QVariantPropertyItem *>(
                static_cast<QPropertyItem *>(source.internalPointer()));
            if (!property) {
                // Class headings are implementation details; keep compound
                // property children intact once a real property is reached.
                if (sourceModel()->hasChildren(source))
                    collect(source);
                else
                    append(source, group(&m_root, tr("Properties")));
                continue;
            }
            QString path =
                openswmmvis::attributeGroup(QString::fromLatin1(property->metaProperty().name()));
            if (path.isEmpty())
                path = tr("Properties");
            Row *destination = &m_root;
            for (const auto &part : path.split(QStringLiteral(" / ")))
                destination = group(destination, part);
            append(source, destination);
        }
    }
    /**
     * @brief Check source property metadata for any recognized grouping key.
     * @param[in] parent Source parent to inspect recursively; invalid means root.
     * @return True if a QVariantPropertyItem is classified by attributeGroup(); requires a source model.
     */
    bool hasGroups(const QModelIndex &parent = {}) const
    {
        for (int i = 0; i < sourceModel()->rowCount(parent); ++i) {
            auto source = sourceModel()->index(i, 0, parent);
            auto *property = dynamic_cast<QVariantPropertyItem *>(
                static_cast<QPropertyItem *>(source.internalPointer()));
            if (property &&
                !openswmmvis::attributeGroup(QString::fromLatin1(property->metaProperty().name()))
                     .isEmpty())
                return true;
            if (!property && hasGroups(source))
                return true;
        }
        return false;
    }
    /**
     * @brief Replace proxy rows from the current source tree and move output-summary groups after editable inputs.
     * @details Invalidates stored row pointers and proxy indexes; caller must bracket public resets appropriately. A null source yields an empty tree.
     */
    void rebuild()
    {
        m_rows.clear();
        m_root.children.clear();
        if (sourceModel()) {
            if (hasGroups())
                collect({});
            else
                for (int i = 0; i < sourceModel()->rowCount(); ++i)
                    append(sourceModel()->index(i, 0), &m_root);
        }
        // Always leave simulation summaries after the editable model.
        const QString output =
            QCoreApplication::translate("AttributeGroups", "Output Summary / Node Depth")
                .section(" / ", 0, 0);
        std::stable_partition(
            m_root.children.begin(), m_root.children.end(),
            [&](const auto &item) { return !item->heading || item->heading->name() != output; });
    }

public:
    /**
     * @brief Construct an empty two-column grouping proxy.
     * @param[in] parent Optional Qt owner; use on the GUI thread with the source model.
     */
    explicit GroupedPropertyModel(QObject *parent = nullptr) : QAbstractProxyModel(parent) {}
    /**
     * @brief Bind a property model and rebuild the grouped presentation.
     * @param[in] source Borrowed source model, or nullptr to detach; normal Qt model lifetime applies.
     * @details Disconnects the previous source, resets proxy indexes, and forwards source reset/dataChanged updates. Structural insert/remove changes require a source reset to rebuild grouping.
     */
    void setSourceModel(QAbstractItemModel *source) override
    {
        beginResetModel();
        if (sourceModel())
            disconnect(sourceModel(), nullptr, this, nullptr);
        QAbstractProxyModel::setSourceModel(source);
        rebuild();
        if (source) {
            connect(source, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
                beginResetModel();
                m_rows.clear();
                m_root.children.clear();
            });
            connect(source, &QAbstractItemModel::modelReset, this, [this] {
                rebuild();
                endResetModel();
            });
            connect(
                source, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
                    for (int r = first.row(); r <= last.row(); ++r) {
                        const auto left = mapFromSource(sourceModel()->index(r, 0, first.parent()));
                        if (left.isValid())
                            emit dataChanged(left, left.siblingAtColumn(1), roles);
                    }
                });
        }
        endResetModel();
    }
    /**
     * @brief Resolve a mirrored property index to its source column.
     * @param[in] proxy Index belonging to this proxy.
     * @return Corresponding source index, or invalid for headings, invalid input or missing rows.
     */
    QModelIndex mapToSource(const QModelIndex &proxy) const override
    {
        auto *item = row(proxy);
        return proxy.isValid() && item && item->source.isValid()
                   ? QModelIndex(item->source).siblingAtColumn(proxy.column())
                   : QModelIndex();
    }
    /**
     * @brief Resolve a source item identity to its grouped proxy row.
     * @param[in] source Index from the bound source model.
     * @return Corresponding proxy index retaining the column; invalid when not mirrored.
     */
    QModelIndex mapFromSource(const QModelIndex &source) const override
    {
        return source.isValid() ? indexFor(m_rows.value(source.internalPointer()), source.column())
                                : QModelIndex();
    }
    /**
     * @brief Address a child in the two-column grouped tree.
     * @param[in] r Zero-based child row.
     * @param[in] c Column zero (name) or one (value).
     * @param[in] parent Proxy parent; invalid selects root. Nonzero-column parents have no children.
     * @return Invalid for missing rows or out-of-range row/column, otherwise the child index.
     */
    QModelIndex index(int r, int c, const QModelIndex &parent = {}) const override
    {
        auto *item = row(parent);
        if (!item || r < 0 || r >= int(item->children.size()) || c < 0 || c >= 2 ||
            parent.column() > 0)
            return {};
        return createIndex(r, c, item->children[r]->pointer());
    }
    /**
     * @brief Return a grouped row's parent in column zero.
     * @param[in] child Proxy child index.
     * @return Parent index or invalid for root-level/invalid children.
     */
    QModelIndex parent(const QModelIndex &child) const override
    {
        auto *item = row(child);
        return child.isValid() && item ? indexFor(item->parent) : QModelIndex();
    }
    /**
     * @brief Count grouped children below a column-zero parent.
     * @param[in] parent Proxy parent or invalid root.
     * @return Child count; zero for missing rows or parents in a nonzero column.
     */
    int rowCount(const QModelIndex &parent = {}) const override
    {
        auto *item = row(parent);
        return item && parent.column() <= 0 ? int(item->children.size()) : 0;
    }
    /**
     * @brief Report the name/value column pair at every level.
     * @return Always two; the optional parent argument is ignored.
     */
    int columnCount(const QModelIndex & = {}) const override { return 2; }
    /**
     * @brief Forward property data or render a synthetic bold group heading.
     * @param[in] index Proxy cell to inspect.
     * @param[in] role Qt role forwarded unchanged for real properties.
     * @return Source role value; headings supply a column-zero display name and bold FontRole; unsupported/invalid cells yield invalid QVariant.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid())
            return {};
        const auto source = mapToSource(index);
        if (source.isValid())
            return source.data(role);
        auto *item = row(index);
        if (item && item->heading) {
            if (role == Qt::DisplayRole && index.column() == 0)
                return item->heading->name();
            if (role == Qt::FontRole) {
                QFont font;
                font.setBold(true);
                return font;
            }
        }
        return {};
    }
    /**
     * @brief Preserve real property editability while making headings enabled-only.
     * @param[in] index Proxy cell to inspect.
     * @return Source flags for real rows, ItemIsEnabled for headings, or NoItemFlags for invalid input.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::NoItemFlags;
        const auto source = mapToSource(index);
        return source.isValid() ? sourceModel()->flags(source) : Qt::ItemIsEnabled;
    }
    /**
     * @brief Forward an edit to the real property model.
     * @param[in] index Real property cell; group headings cannot be edited.
     * @param[in] value Value forwarded unchanged to the source.
     * @param[in] role Source edit role, default EditRole.
     * @return Source setData result, or false for synthetic/unmapped cells.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override
    {
        const auto source = mapToSource(index);
        return source.isValid() && sourceModel()->setData(source, value, role);
    }
    /**
     * @brief Forward headers to the bound property model.
     * @param[in] section Requested row/column section.
     * @param[in] orientation Horizontal or vertical header orientation.
     * @param[in] role Qt header-data role.
     * @return Source header value, or invalid QVariant without a source.
     */
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        return sourceModel() ? sourceModel()->headerData(section, orientation, role) : QVariant();
    }
};
#endif
