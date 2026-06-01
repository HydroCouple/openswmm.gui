/*!
 * \file   timeserieslistmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.3.3-CRUD — QAbstractListModel over a
 *         TimeseriesRegistry; drives the editor dialog's left-pane list.
 *
 * Mirrors the HydrographGroupListModel convention so the new TimeseriesEditor
 * dialog can present the same three-pane CRUD UX as `HydrographGroupEditor`
 * (left = list of all series + New/Delete/Rename, right = per-series edit
 * surface). Selection in the list drives which TimeseriesProvider the grid +
 * chart are bound to.
 *
 * The model:
 *  - listens to the registry's `providerAdded` / `providerAboutToBeRemoved` /
 *    `providerRenamed` signals so external CRUD (e.g. registry.create() from
 *    the Add-New flow, or registry.remove() from a different surface) is
 *    automatically reflected in the list.
 *  - exposes the provider name via `Qt::DisplayRole` / `Qt::EditRole` so the
 *    view can offer in-place rename via QListView::setEditTriggers.
 *  - on setData(EditRole) routes through `TimeseriesRegistry::rename` so the
 *    case-insensitive uniqueness check is enforced.
 *
 * No undo support — rename / create / delete are atomic and applied
 * immediately. The undo-stack integration belongs at the editor-dialog layer,
 * which wraps each CRUD call in a QUndoCommand.
 */
#ifndef OPENSWMMVIS_UI_PANELS_TIMESERIESLISTMODEL_H
#define OPENSWMMVIS_UI_PANELS_TIMESERIESLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>
#include <QVector>

namespace openswmmvis::timeseries {
class TimeseriesProvider;
class TimeseriesRegistry;
}

namespace openswmmvis::ui {

class TimeseriesListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit TimeseriesListModel(QObject *parent = nullptr);
    ~TimeseriesListModel() override;

    /*! \brief Bind to a registry. Pass nullptr to clear. Subscribes to the
     *  registry's lifecycle + rename signals so the list stays in sync with
     *  external CRUD. */
    void setRegistry(openswmmvis::timeseries::TimeseriesRegistry *registry);
    openswmmvis::timeseries::TimeseriesRegistry *registry() const noexcept;

    /*! \brief The provider at a given row, or nullptr. */
    openswmmvis::timeseries::TimeseriesProvider *providerAt(int row) const;

    /*! \brief Row index for a provider, or -1. */
    int rowOf(const openswmmvis::timeseries::TimeseriesProvider *p) const;

    // QAbstractListModel ----------------------------------------------------

    int      rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData(const QModelIndex &index, const QVariant &value,
                     int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private slots:
    void onProviderAdded_(openswmmvis::timeseries::TimeseriesProvider *p);
    void onProviderAboutToBeRemoved_(openswmmvis::timeseries::TimeseriesProvider *p);
    void onProviderRenamed_(openswmmvis::timeseries::TimeseriesProvider *p,
                            const QString &prev, const QString &now);

private:
    QPointer<openswmmvis::timeseries::TimeseriesRegistry> m_registry;
    /*! \brief Cached row order. Mirrors registry->providers() so the model
     *  can answer rowCount/data without re-querying the registry on each
     *  call and so beginInsertRows/beginRemoveRows can use stable indices. */
    QVector<openswmmvis::timeseries::TimeseriesProvider *> m_rows;

    void rebuildCache_();
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_TIMESERIESLISTMODEL_H
