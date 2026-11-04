#ifndef SUBCATCHQUALITYTABLEMODEL_H
#define SUBCATCHQUALITYTABLEMODEL_H

#include "ui/properties/subcatchcompoundeditref.h"
#include <QAbstractTableModel>
#include <QPointer>
#include <functional>

// A land-use or pollutant row changes only that named matrix member.
class SubcatchQualityTableModel : public QAbstractTableModel
{
public:
    explicit SubcatchQualityTableModel(SubcatchCompoundEditRef ref, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QString lastError() const { return m_error; }
    QString summary() const;
    QStringList targetNames() const { return m_names; }
    std::function<void(const QString &)> feedback;

private:
    void refresh();
    bool available() const;
    QPointer<SWMMModelLayer> m_layer;
    QPointer<QUndoStack> m_undo;
    SWMM_Engine m_engine = nullptr;
    SubcatchCompoundEditRef::Kind m_kind;
    QStringList m_names, m_keys;
    QList<QVector<double>> m_values;
    QStringList m_errors;
    QString m_error;
    int m_count = 0;
    bool m_valid = true, m_pending = false;
};

class QWidget;
void buildSubcatchQualityEditor(QWidget *host, const SubcatchCompoundEditRef &ref);

#endif
