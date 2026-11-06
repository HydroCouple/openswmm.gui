/**
 * @file      transectlistmodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.4 — QAbstractListModel over a TransectRegistry.
 *
 * Mirrors RuleListModel (Phase 6.8.1). Subscribes to providerAdded /
 * providerAboutToBeRemoved / providerRenamed / providerMetadataChanged and
 * emits the matching begin/end signals so the editor list pane refreshes
 * lock-step with any other UI that mutates the registry.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_MODELS_TRANSECTLISTMODEL_H
#define OPENSWMMVIS_UI_MODELS_TRANSECTLISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>

namespace openswmmvis::transect
{
class TransectProvider;
class TransectRegistry;
}

namespace openswmmvis::ui
{

class TransectListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    /**
     * @brief Create a registry-backed flat identity list with no registry bound.
     * @param[in] parent Optional QObject owner; use the model on its Qt/GUI thread.
     */
    explicit TransectListModel(QObject *parent = nullptr);
    /**
     * @brief Release the list adapter without deleting its borrowed registry or providers.
     */
    ~TransectListModel() override;

    /**
     * @brief Reset the visible rows and replace registry signal subscriptions.
     * @param[in] registry Borrowed registry tracked by QPointer; null unbinds. An identical pointer is a no-op.
     * @note Registry insertion/removal/rename notifications drive row and data updates; no provider ownership transfers.
     */
    void setRegistry(openswmmvis::transect::TransectRegistry *registry);
    /**
     * @brief Access the currently bound registry.
     * @return Borrowed pointer, or null when unbound or the registry has been destroyed.
     */
    openswmmvis::transect::TransectRegistry *registry() const noexcept;

    /*! \brief Return the provider at \p row, or nullptr.
     * @brief Resolve a row in current registry insertion order.
     * @param[in] row Zero-based row.
     * @return Borrowed provider, or null for no registry or an out-of-range row.
     */
    openswmmvis::transect::TransectProvider *providerAt(int row) const;

    /**
     * @brief Report the number of root registry entries.
     * @param[in] parent Root is an invalid index; valid parents have no children.
     * @return Provider count when bound at root, otherwise zero.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Read provider identity and its compact tooltip.
     * @param[in] index Model index identifying the provider row; obtain indexes from this model.
     * @param[in] role DisplayRole/EditRole returns the provider name. ToolTipRole returns station count and bank locations in provider/model length units.
     * @return Requested value, or an invalid QVariant for missing/invalid rows or unsupported roles.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @brief Supply the list title or one-based row number.
     * @param[in] section Header section; vertical labels are section plus one.
     * @param[in] orientation Vertical selects numbering, horizontal the translated list title.
     * @param[in] role Only DisplayRole is supported.
     * @return Header value or an invalid QVariant for another role.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @brief Advertise inline name editing for valid list indexes.
     * @param[in] index Index whose validity is tested; row ownership/bounds are not independently checked here.
     * @return Enabled, selectable and editable for a valid index, otherwise no flags.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @brief Request a registry rename from an inline editor.
     * @param[in] index Index resolving to an existing provider row.
     * @param[in] value New name converted to a string and trimmed; empty names are rejected.
     * @param[in] role Only EditRole is accepted.
     * @return Registry rename result; false for invalid arguments or registry rejection. Registry notifications refresh views; no undo command is created here.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

private slots:
    /**
     * @brief Announce a newly appended registry row to Qt views.
     * @param[in] p Borrowed provider already present in the registry; null/unresolved pointers are ignored.
     */
    void onProviderAdded_(openswmmvis::transect::TransectProvider *p);
    /**
     * @brief Announce removal of the provider's current row; the registry owns the mutation.
     * @param[in] p Provider still present during the about-to-remove notification; missing pointers are ignored.
     */
    void onProviderAboutToBeRemoved_(openswmmvis::transect::TransectProvider *p);
    /**
     * @brief Refresh DisplayRole and EditRole for the renamed provider.
     * @param[in] p Borrowed provider whose current row is looked up.
     * @param[in] prev Previous identity, unused by this adapter.
     * @param[in] now New identity, unused because data() reads the provider directly.
     */
    void onProviderRenamed_(openswmmvis::transect::TransectProvider *p, const QString &prev,
                            const QString &now);
    /**
     * @brief Refresh the provider row's ToolTipRole after its descriptive data changes.
     * @param[in] p Borrowed changed provider; an unregistered/null pointer is ignored.
     */
    void onProviderMetadataChanged_(openswmmvis::transect::TransectProvider *p);
    /**
     * @brief Refresh the provider row's ToolTipRole after its descriptive data changes.
     * @param[in] p Borrowed changed provider; an unregistered/null pointer is ignored.
     */
    void onProviderPointsChanged_(openswmmvis::transect::TransectProvider *p);

private:
    /**
     * @brief Find a provider pointer in current registry order.
     * @param[in] p Borrowed identity pointer; null is not a match.
     * @return Zero-based row, or -1 for a missing registry/provider.
     */
    int indexOf_(openswmmvis::transect::TransectProvider *p) const;

    QPointer<openswmmvis::transect::TransectRegistry> m_registry;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_TRANSECTLISTMODEL_H
