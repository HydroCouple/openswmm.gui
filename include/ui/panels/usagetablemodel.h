#ifndef USAGETABLEMODEL_H
#define USAGETABLEMODEL_H
#include "ui/properties/usageeditref.h"
#include <openswmm/engine/openswmm_infrastructure.h>
#include <QAbstractTableModel>
#include <functional>
#include <memory>
struct UsageChoice {
    QString label;
    QVariant value;
};
class UsageTableModel : public QAbstractTableModel
{
public:
    explicit UsageTableModel(UsageEditRef ref, QObject *parent = nullptr);
    ~UsageTableModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant headerData(int column, Qt::Orientation orientation, int role) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QList<UsageChoice> choices(const QModelIndex &index) const;
    QString rowKey(int row) const;
    QString summary() const;
    QString lastError() const;
    QStringList targetNames() const;
    UsageEditRef::Family family() const;
    bool removeAssignment(int row);
    bool assignMissingInlets(const QString &design, const QString &capture);
    void invalidateContext();
    std::function<void(const QString &)> feedback;

private:
    struct Private;
    std::unique_ptr<Private> d;
    void refresh();
    bool fail(const QString &message);
};
// Shared checked writer used by existing layer/inline inlet entry points as well as the table.
bool applyInletUsageBatch(SWMMModelLayer *layer, const QList<SWMM_InletUsage> &usages,
                          bool removing, QUndoStack *undo, QString *error = nullptr);
class QWidget;
class QDialog;
void buildUsageEditor(QWidget *host, UsageEditRef ref);
void invalidateUsageEditor(QWidget *host);
QDialog *openUsageEditor(UsageEditRef ref, QWidget *parent);
#endif
