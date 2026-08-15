/*!
 * \file   transectlistmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.4 — QAbstractListModel over a TransectRegistry.
 *
 * Mirrors RuleListModel (Phase 6.8.1). Subscribes to providerAdded /
 * providerAboutToBeRemoved / providerRenamed / providerMetadataChanged and
 * emits the matching begin/end signals so the editor list pane refreshes
 * lock-step with any other UI that mutates the registry.
 */
#ifndef OPENSWMMVIS_UI_MODELS_TRANSECTLISTMODEL_H
#define OPENSWMMVIS_UI_MODELS_TRANSECTLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>

namespace openswmmvis::transect {
class TransectProvider;
class TransectRegistry;
}

namespace openswmmvis::ui {

class TransectListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit TransectListModel(QObject *parent = nullptr);
    ~TransectListModel() override;

    void setRegistry(openswmmvis::transect::TransectRegistry *registry);
    openswmmvis::transect::TransectRegistry *registry() const noexcept;

    /*! \brief Return the provider at \p row, or nullptr. */
    openswmmvis::transect::TransectProvider *providerAt(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

private slots:
    void onProviderAdded_(openswmmvis::transect::TransectProvider *p);
    void onProviderAboutToBeRemoved_(openswmmvis::transect::TransectProvider *p);
    void onProviderRenamed_(openswmmvis::transect::TransectProvider *p,
                              const QString &prev, const QString &now);
    void onProviderMetadataChanged_(openswmmvis::transect::TransectProvider *p);
    void onProviderPointsChanged_(openswmmvis::transect::TransectProvider *p);

private:
    int indexOf_(openswmmvis::transect::TransectProvider *p) const;

    QPointer<openswmmvis::transect::TransectRegistry> m_registry;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_TRANSECTLISTMODEL_H
