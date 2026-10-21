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
        void *pointer() const { return heading ? heading.get() : QModelIndex(source).internalPointer(); }
    };
    Row m_root;
    QHash<void *, Row *> m_rows;

    Row *row(const QModelIndex &index) const {
        return index.isValid() ? m_rows.value(index.internalPointer()) : const_cast<Row *>(&m_root);
    }
    QModelIndex indexFor(Row *item, int column = 0) const {
        if (!item || !item->parent) return {};
        const auto &siblings = item->parent->children;
        for (int i = 0; i < int(siblings.size()); ++i)
            if (siblings[i].get() == item) return createIndex(i, column, item->pointer());
        return {};
    }
    Row *group(Row *parent, const QString &name) {
        for (const auto &child : parent->children)
            if (child->heading && child->heading->name() == name) return child.get();
        auto item = std::make_unique<Row>();
        item->parent = parent;
        item->heading = std::make_unique<QPropertyItem>(QVariant(), name, nullptr);
        item->heading->setFlags(Qt::ItemIsEnabled);
        auto *result = item.get();
        m_rows.insert(item->pointer(), result);
        parent->children.push_back(std::move(item));
        return result;
    }
    void append(const QModelIndex &source, Row *parent) {
        auto item = std::make_unique<Row>();
        item->source = source;
        item->parent = parent;
        auto *result = item.get();
        m_rows.insert(item->pointer(), result);
        parent->children.push_back(std::move(item));
        for (int i = 0; i < sourceModel()->rowCount(source); ++i)
            append(sourceModel()->index(i, 0, source), result);
    }
    void collect(const QModelIndex &parent) {
        for (int i = 0; i < sourceModel()->rowCount(parent); ++i) {
            const auto source = sourceModel()->index(i, 0, parent);
            auto *property = dynamic_cast<QVariantPropertyItem *>(
                static_cast<QPropertyItem *>(source.internalPointer()));
            if (!property) {
                // Class headings are implementation details; keep compound
                // property children intact once a real property is reached.
                if (sourceModel()->hasChildren(source)) collect(source);
                else append(source, group(&m_root, tr("Properties")));
                continue;
            }
            QString path = openswmmvis::attributeGroup(
                QString::fromLatin1(property->metaProperty().name()));
            if (path.isEmpty()) path = tr("Properties");
            Row *destination = &m_root;
            for (const auto &part : path.split(QStringLiteral(" / ")))
                destination = group(destination, part);
            append(source, destination);
        }
    }
    bool hasGroups(const QModelIndex &parent = {}) const {
        for (int i = 0; i < sourceModel()->rowCount(parent); ++i) {
            auto source = sourceModel()->index(i, 0, parent);
            auto *property = dynamic_cast<QVariantPropertyItem *>(
                static_cast<QPropertyItem *>(source.internalPointer()));
            if (property && !openswmmvis::attributeGroup(QString::fromLatin1(property->metaProperty().name())).isEmpty()) return true;
            if (!property && hasGroups(source)) return true;
        }
        return false;
    }
    void rebuild() {
        m_rows.clear();
        m_root.children.clear();
        if (sourceModel()) {
            if (hasGroups()) collect({});
            else for (int i = 0; i < sourceModel()->rowCount(); ++i)
                append(sourceModel()->index(i, 0), &m_root);
        }
        // Always leave simulation summaries after the editable model.
        const QString output = QCoreApplication::translate("AttributeGroups", "Output Summary / Node Depth").section(" / ", 0, 0);
        std::stable_partition(m_root.children.begin(), m_root.children.end(),
            [&](const auto &item) { return !item->heading || item->heading->name() != output; });
    }
public:
    explicit GroupedPropertyModel(QObject *parent = nullptr) : QAbstractProxyModel(parent) {}
    void setSourceModel(QAbstractItemModel *source) override {
        beginResetModel();
        if (sourceModel()) disconnect(sourceModel(), nullptr, this, nullptr);
        QAbstractProxyModel::setSourceModel(source);
        rebuild();
        if (source) {
            connect(source, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
                beginResetModel(); m_rows.clear(); m_root.children.clear();
            });
            connect(source, &QAbstractItemModel::modelReset, this, [this] { rebuild(); endResetModel(); });
            connect(source, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
                    for (int r = first.row(); r <= last.row(); ++r) {
                        const auto left = mapFromSource(sourceModel()->index(r, 0, first.parent()));
                        if (left.isValid()) emit dataChanged(left, left.siblingAtColumn(1), roles);
                    }
                });
        }
        endResetModel();
    }
    QModelIndex mapToSource(const QModelIndex &proxy) const override {
        auto *item = row(proxy);
        return proxy.isValid() && item && item->source.isValid()
            ? QModelIndex(item->source).siblingAtColumn(proxy.column()) : QModelIndex();
    }
    QModelIndex mapFromSource(const QModelIndex &source) const override {
        return source.isValid() ? indexFor(m_rows.value(source.internalPointer()), source.column()) : QModelIndex();
    }
    QModelIndex index(int r, int c, const QModelIndex &parent = {}) const override {
        auto *item = row(parent);
        if (!item || r < 0 || r >= int(item->children.size()) || c < 0 || c >= 2 || parent.column() > 0) return {};
        return createIndex(r, c, item->children[r]->pointer());
    }
    QModelIndex parent(const QModelIndex &child) const override {
        auto *item = row(child);
        return child.isValid() && item ? indexFor(item->parent) : QModelIndex();
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        auto *item = row(parent);
        return item && parent.column() <= 0 ? int(item->children.size()) : 0;
    }
    int columnCount(const QModelIndex & = {}) const override { return 2; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (!index.isValid()) return {};
        const auto source = mapToSource(index);
        if (source.isValid()) return source.data(role);
        auto *item = row(index);
        if (item && item->heading) {
            if (role == Qt::DisplayRole && index.column() == 0) return item->heading->name();
            if (role == Qt::FontRole) { QFont font; font.setBold(true); return font; }
        }
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex &index) const override {
        if (!index.isValid()) return Qt::NoItemFlags;
        const auto source = mapToSource(index);
        return source.isValid() ? sourceModel()->flags(source) : Qt::ItemIsEnabled;
    }
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override {
        const auto source = mapToSource(index);
        return source.isValid() && sourceModel()->setData(source, value, role);
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        return sourceModel() ? sourceModel()->headerData(section, orientation, role) : QVariant();
    }
};
#endif
