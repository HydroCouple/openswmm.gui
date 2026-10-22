/**
 * @file      timeserieslistmodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.3.3-CRUD — QAbstractListModel over a
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_TIMESERIESLISTMODEL_H
#define OPENSWMMVIS_UI_PANELS_TIMESERIESLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>
#include <QVector>

namespace openswmmvis::timeseries
{
class TimeseriesProvider;
class TimeseriesRegistry;
}

namespace openswmmvis::ui
{

class TimeseriesListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner, caller owns parentless model.
     * @brief Create unbound GUI-thread model, without loading files or mutating engine/providers.
     * @note Qt model notifications and all bound-provider operations must be serialized in owner
     *   thread; no locking. Binding and editor/undo ownership configured separately.
     */
    explicit TimeseriesListModel(QObject *parent = nullptr);
    /**
     * @brief Destroy model-owned connections/cache; borrowed providers/registry and undo stack
     *   remain externally owned. No automatic save/undo commit or engine mutation.
     */
    ~TimeseriesListModel() override;

    /*! \brief Bind to a registry. Pass nullptr to clear. Subscribes to the
     *  registry's lifecycle + rename signals so the list stays in sync with
     *  external CRUD.
     * @param[in] registry Optional borrowed registry guarded by QPointer, null clears.
     * @note Equality no-op. Full reset around disconnect old hooks/rebuild raw provider row
     *   cache/connect lifecycle/rename hooks. No provider ownership, destroyed→cache clear hook or
     *   undo; registry must remove rows normally before provider destruction or host explicitly
     *   unbind.
     */
    void setRegistry(openswmmvis::timeseries::TimeseriesRegistry *registry);
    /**
     * @return Borrowed guarded registry, null after destruction; does not validate raw cached
     *   provider lifetimes.
     */
    openswmmvis::timeseries::TimeseriesRegistry *registry() const noexcept;

    /*! \brief The provider at a given row, or nullptr.
     * @param[in] row Zero-based cached row.
     * @return Borrowed RAW provider or null out-of-range, no deletion guard/ownership; host
     *   preserves provider lifetime via registry lifecycle.
     */
    openswmmvis::timeseries::TimeseriesProvider *providerAt(int row) const;

    /*! \brief Row index for a provider, or -1.
     * @param[in] p Provider pointer identity, not dereferenced; null can match a null cache entry.
     * @return First matching cached row or -1, linear scan, no registry query.
     */
    int rowOf(const openswmmvis::timeseries::TimeseriesProvider *p) const;

    // QAbstractListModel ----------------------------------------------------

    /**
     * @param[in] parent Valid parent yields 0in flat list.
     * @return Cached row size independent of current registry pointer; can remain stale after
     *   destruction if not explicitly cleared.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] index Expected current list index; validates row/index but not column/model
     *   identity.
     * @param[in] role Only Display/Edit supported.
     * @return Copied cached provider name or invalid for null/bad row/unsupported role. Raw
     *   provider must still live; no engine read or rename.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Expected existing list row; index validity/column not independently checked
     *   beyond row range.
     * @param[in] value Converted string then trimmed, empty rejects.
     * @param[in] role Only EditRole accepted.
     * @return Registry rename result, false for absent registry/provider/bad row/empty name or
     *   uniqueness/validation rejection. No undo command here, unlike editor-level wrapped CRUD.
     *   Registry signals refresh name; no local engine-flush guarantee.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @param[in] index Supplied Qt index; valid adds Editable, no provider/row bounds check.
     * @return Base QAbstractListModel flags plus editability for any valid index. Does not
     *   guarantee rename success or live registry/provider.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private slots:
    /**
     * @param[in] p Borrowed newly registry-owned provider, null ignored.
     * @note Append one RAW row in insert notification, no sorting/duplicate check; provider must
     *   live until matching removal. No engine/registry mutation.
     */
    void onProviderAdded_(openswmmvis::timeseries::TimeseriesProvider *p);
    /**
     * @param[in] p Borrowed provider identity, still live until caller finishes notification.
     * @note Remove first cached row via begin/endRemoveRows; unknown no-op. Does not delete
     *   provider or change registry itself.
     */
    void onProviderAboutToBeRemoved_(openswmmvis::timeseries::TimeseriesProvider *p);
    /**
     * @param[in] p Borrowed renamed provider identity.
     * @param[in] prev Ignored previous name, implementation unnamed.
     * @param[in] now Ignored new name, implementation unnamed; data() rereads provider.
     * @note Known row emits Display/Edit dataChanged only; does not sort/cache copied name or
     *   rename again.
     */
    void onProviderRenamed_(openswmmvis::timeseries::TimeseriesProvider *p, const QString &prev,
                            const QString &now);

private:
    QPointer<openswmmvis::timeseries::TimeseriesRegistry> m_registry;
    /*! \brief Cached row order. Mirrors registry->providers() so the model
     *  can answer rowCount/data without re-querying the registry on each
     *  call and so beginInsertRows/beginRemoveRows can use stable indices. */
    QVector<openswmmvis::timeseries::TimeseriesProvider *> m_rows;

    /**
     * @brief Clear raw row cache and copy registry providers in registry order when live.
     * @note Caller surrounds model reset; helper emits no notifications itself, owns no providers
     *   and performs no sorting/lifetime guards.
     */
    void rebuildCache_();
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_TIMESERIESLISTMODEL_H
