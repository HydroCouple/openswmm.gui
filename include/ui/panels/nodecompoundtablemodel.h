#ifndef NODECOMPOUNDTABLEMODEL_H
#define NODECOMPOUNDTABLEMODEL_H

#include "ui/properties/nodecompoundeditref.h"
#include "ui/properties/dataobjectref.h"
#include <QAbstractTableModel>
#include <functional>
#include <memory>

// One row per constituent/pollutant, or the single RDII assignment slot.
// Both single-node and selection editors use the same checked writer.
class NodeCompoundTableModel : public QAbstractTableModel
{
public:
    explicit NodeCompoundTableModel(NodeCompoundEditRef ref, QObject *parent = nullptr);
    ~NodeCompoundTableModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    bool removeAssignment(int row);
    QStringList choices(const QModelIndex &index) const;
    QString fieldKey(int column) const;
    QString rowKey(int row) const;
    DataObjectRef reference(const QModelIndex &index) const;
    QString lastError() const;
    SWMM_Engine engine() const;
    std::function<void(const QString &)> feedback;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void refresh();
    bool change(int row, const QString &field, const QVariant &value);
};

class QWidget;
void buildNodeCompoundTableEditor(QWidget *host, const NodeCompoundEditRef &ref);

#endif
