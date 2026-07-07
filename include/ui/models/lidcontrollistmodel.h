/*!
 * \file   lidcontrollistmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QAbstractListModel over a LidControlRegistry. Mirrors InletListModel.
 */
#ifndef OPENSWMMVIS_UI_MODELS_LIDCONTROLLISTMODEL_H
#define OPENSWMMVIS_UI_MODELS_LIDCONTROLLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>

namespace openswmmvis::lid {
class LidControlProvider;
class LidControlRegistry;
}

namespace openswmmvis::ui {

class LidControlListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit LidControlListModel(QObject *parent = nullptr);
    ~LidControlListModel() override;

    void setRegistry(openswmmvis::lid::LidControlRegistry *registry);
    openswmmvis::lid::LidControlRegistry *registry() const noexcept;

    openswmmvis::lid::LidControlProvider *providerAt(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

private slots:
    void onProviderAdded_(openswmmvis::lid::LidControlProvider *p);
    void onProviderAboutToBeRemoved_(openswmmvis::lid::LidControlProvider *p);
    void onProviderRenamed_(openswmmvis::lid::LidControlProvider *p,
                              const QString &prev, const QString &now);
    void onProviderParamsChanged_(openswmmvis::lid::LidControlProvider *p);

private:
    int indexOf_(openswmmvis::lid::LidControlProvider *p) const;

    QPointer<openswmmvis::lid::LidControlRegistry> m_registry;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_LIDCONTROLLISTMODEL_H
