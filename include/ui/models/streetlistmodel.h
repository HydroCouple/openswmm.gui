/*!
 * \file   streetlistmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QAbstractListModel over a StreetRegistry.
 *
 * Mirrors TransectListModel. Subscribes to providerAdded /
 * providerAboutToBeRemoved / providerRenamed / providerParamsChanged and
 * emits the matching begin/end signals so the editor list pane refreshes
 * lock-step with any other UI that mutates the registry.
 */
#ifndef OPENSWMMVIS_UI_MODELS_STREETLISTMODEL_H
#define OPENSWMMVIS_UI_MODELS_STREETLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>

namespace openswmmvis::street {
class StreetProvider;
class StreetRegistry;
}

namespace openswmmvis::ui {

class StreetListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit StreetListModel(QObject *parent = nullptr);
    ~StreetListModel() override;

    void setRegistry(openswmmvis::street::StreetRegistry *registry);
    openswmmvis::street::StreetRegistry *registry() const noexcept;

    openswmmvis::street::StreetProvider *providerAt(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

private slots:
    void onProviderAdded_(openswmmvis::street::StreetProvider *p);
    void onProviderAboutToBeRemoved_(openswmmvis::street::StreetProvider *p);
    void onProviderRenamed_(openswmmvis::street::StreetProvider *p,
                              const QString &prev, const QString &now);
    void onProviderParamsChanged_(openswmmvis::street::StreetProvider *p);

private:
    int indexOf_(openswmmvis::street::StreetProvider *p) const;

    QPointer<openswmmvis::street::StreetRegistry> m_registry;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_STREETLISTMODEL_H
