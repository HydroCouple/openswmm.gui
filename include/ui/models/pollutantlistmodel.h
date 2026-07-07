/*!
 * \file   pollutantlistmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QAbstractListModel over a PollutantRegistry. Mirrors StreetListModel.
 */
#ifndef OPENSWMMVIS_UI_MODELS_POLLUTANTLISTMODEL_H
#define OPENSWMMVIS_UI_MODELS_POLLUTANTLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>

namespace openswmmvis::pollutant {
class PollutantProvider;
class PollutantRegistry;
}

namespace openswmmvis::ui {

class PollutantListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit PollutantListModel(QObject *parent = nullptr);
    ~PollutantListModel() override;

    void setRegistry(openswmmvis::pollutant::PollutantRegistry *registry);
    openswmmvis::pollutant::PollutantRegistry *registry() const noexcept;

    openswmmvis::pollutant::PollutantProvider *providerAt(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

private slots:
    void onProviderAdded_(openswmmvis::pollutant::PollutantProvider *p);
    void onProviderAboutToBeRemoved_(openswmmvis::pollutant::PollutantProvider *p);
    void onProviderRenamed_(openswmmvis::pollutant::PollutantProvider *p,
                              const QString &prev, const QString &now);
    void onProviderParamsChanged_(openswmmvis::pollutant::PollutantProvider *p);

private:
    int indexOf_(openswmmvis::pollutant::PollutantProvider *p) const;

    QPointer<openswmmvis::pollutant::PollutantRegistry> m_registry;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_POLLUTANTLISTMODEL_H
