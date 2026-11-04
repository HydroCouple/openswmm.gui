#ifndef GROUNDWATEREXCHANGETABLEMODEL_H
#define GROUNDWATEREXCHANGETABLEMODEL_H

#include "ui/properties/subcatchcompoundeditref.h"
#include <QAbstractTableModel>
#include <functional>

// Authored groundwater exchange fields; runtime state remains owned by the solver.
class GroundwaterExchangeTableModel : public QAbstractTableModel
{
public:
    enum Row { Aquifer, Node, Surface, A1, B1, A2, B2, A3, Dsw, Egwt, Lateral, Deep, Rows };
    explicit GroundwaterExchangeTableModel(SubcatchCompoundEditRef ref, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QStringList choices(const QModelIndex &index) const;
    QStringList targetNames() const { return m_names; }
    QString summary() const;
    QString lastError() const { return m_error; }
    SWMM_Engine engine() const;
    void invalidateContext();
    std::function<void(const QString &)> feedback;

private:
    void refresh();
    void checkGeometry();
    QPointer<SWMMModelLayer> m_layer;
    QPointer<QUndoStack> m_undo;
    SWMM_Engine m_engine = nullptr;
    QStringList m_names, m_aquifers, m_nodes;
    QList<QVariant> m_values;
    QString m_problem, m_error;
    int m_subCount = 0, m_nodeCount = 0;
    bool m_valid = true, m_pending = false, m_readable = false;
    bool m_haveAquifers = false, m_haveReceivers = false;
};

class QWidget;
void buildGroundwaterExchangeEditor(QWidget *host, SubcatchCompoundEditRef ref);

#endif
