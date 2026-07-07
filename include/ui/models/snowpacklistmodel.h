/*!
 * \file   snowpacklistmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QAbstractListModel over a SnowpackRegistry (identity-only).
 */
#ifndef OPENSWMMVIS_UI_MODELS_SNOWPACKLISTMODEL_H
#define OPENSWMMVIS_UI_MODELS_SNOWPACKLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>

namespace openswmmvis::snowpack {
class SnowpackProvider;
class SnowpackRegistry;
}

namespace openswmmvis::ui {

class SnowpackListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit SnowpackListModel(QObject *parent = nullptr);
    ~SnowpackListModel() override;

    void setRegistry(openswmmvis::snowpack::SnowpackRegistry *registry);
    openswmmvis::snowpack::SnowpackRegistry *registry() const noexcept;

    openswmmvis::snowpack::SnowpackProvider *providerAt(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

private slots:
    void onProviderAdded_(openswmmvis::snowpack::SnowpackProvider *p);
    void onProviderAboutToBeRemoved_(openswmmvis::snowpack::SnowpackProvider *p);
    void onProviderRenamed_(openswmmvis::snowpack::SnowpackProvider *p,
                              const QString &prev, const QString &now);

private:
    int indexOf_(openswmmvis::snowpack::SnowpackProvider *p) const;

    QPointer<openswmmvis::snowpack::SnowpackRegistry> m_registry;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_SNOWPACKLISTMODEL_H
