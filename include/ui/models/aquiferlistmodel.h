/*!
 * \file   aquiferlistmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QAbstractListModel over an AquiferRegistry. Mirrors LandUseListModel.
 */
#ifndef OPENSWMMVIS_UI_MODELS_AQUIFERLISTMODEL_H
#define OPENSWMMVIS_UI_MODELS_AQUIFERLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>

namespace openswmmvis::aquifer {
class AquiferProvider;
class AquiferRegistry;
}

namespace openswmmvis::ui {

class AquiferListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit AquiferListModel(QObject *parent = nullptr);
    ~AquiferListModel() override;

    void setRegistry(openswmmvis::aquifer::AquiferRegistry *registry);
    openswmmvis::aquifer::AquiferRegistry *registry() const noexcept;

    openswmmvis::aquifer::AquiferProvider *providerAt(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

private slots:
    void onProviderAdded_(openswmmvis::aquifer::AquiferProvider *p);
    void onProviderAboutToBeRemoved_(openswmmvis::aquifer::AquiferProvider *p);
    void onProviderRenamed_(openswmmvis::aquifer::AquiferProvider *p,
                              const QString &prev, const QString &now);
    void onProviderParamsChanged_(openswmmvis::aquifer::AquiferProvider *p);

private:
    int indexOf_(openswmmvis::aquifer::AquiferProvider *p) const;

    QPointer<openswmmvis::aquifer::AquiferRegistry> m_registry;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_AQUIFERLISTMODEL_H
