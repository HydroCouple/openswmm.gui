/**
 * @file      transectstationtablemodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.4 — QAbstractTableModel over one TransectProvider.
 *
 * Schema: 2 columns (Station / Elevation) by N rows. Edits route through
 * TransectProvider::setElevationAt (col 1) or setPointAt (col 0). The
 * provider's mutationRejected signal surfaces UI errors. Subscribes to
 * the provider's pointsChanged / pointsInserted / pointsRemoved.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_MODELS_TRANSECTSTATIONTABLEMODEL_H
#define OPENSWMMVIS_UI_MODELS_TRANSECTSTATIONTABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>

namespace openswmmvis::transect
{
class TransectProvider;
}

namespace openswmmvis::ui
{

class TransectStationTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /**
     * @brief Create an unbound two-column transect point adapter.
     * @param[in] parent Optional QObject owner; access on the Qt/GUI thread.
     */
    explicit TransectStationTableModel(QObject *parent = nullptr);
    /**
     * @brief Destroy the adapter without deleting its borrowed provider.
     */
    ~TransectStationTableModel() override;

    /**
     * @brief Reset rows and replace point-change subscriptions.
     * @param[in] p Borrowed provider tracked by QPointer; null unbinds and identical pointers are ignored.
     */
    void setProvider(openswmmvis::transect::TransectProvider *p);
    /**
     * @brief Access the currently observed transect.
     * @return Borrowed provider or null when unbound/destroyed.
     */
    openswmmvis::transect::TransectProvider *provider() const noexcept;

    /*! \brief Override the header units (e.g. "(ft)" / "(m)").
     * @param[in] suffix Display-only unit text, including empty; no data conversion occurs.
     * @note A changed value emits headerDataChanged() for both horizontal columns.
     */
    void setUnitsSuffix(const QString &suffix);
    /**
     * @brief Read the display-only suffix appended to Station/Elevation headings.
     * @return Stored unit label; empty suppresses the suffix.
     */
    QString unitsSuffix() const noexcept { return m_unitsSuffix; }

    /**
     * @brief Count currently bound transect points.
     * @param[in] parent Root is invalid; valid parents have no children.
     * @return Point count at root when bound, otherwise zero.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Expose Station and Elevation columns.
     * @param[in] parent Root is invalid; valid parents have no columns.
     * @return Two at root, otherwise zero, even when no provider is bound.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Read point coordinates or numeric alignment.
     * @param[in] index Valid row/column from this model; both bounds are checked.
     * @param[in] role DisplayRole/EditRole gives numeric station/elevation; TextAlignmentRole gives right/vertical-centre alignment.
     * @return Provider-unit numeric value or alignment; invalid QVariant for unavailable data/roles.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @brief Supply coordinate headings and one-based row numbers.
     * @param[in] section Column 0 is Station; other horizontal sections use Elevation.
     * @param[in] orientation Vertical selects row numbering.
     * @param[in] role Only DisplayRole is supported.
     * @return Display label with optional units suffix, or invalid for another role.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @brief Advertise editing when a provider and a valid index are available.
     * @param[in] index Point index; ownership/bounds are not independently checked here.
     * @return Enabled/selectable/editable, or no flags when unbound/invalid.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @brief Forward a numeric coordinate edit to the provider's validated point operations.
     * @param[in] index Existing row in Station column 0 or Elevation column 1.
     * @param[in] value Number convertible to double in provider units; no unit conversion occurs.
     * @param[in] role Only EditRole is accepted.
     * @return Provider mutation result, or false for invalid conversion/index/role/column. Provider signals update views and report rejected mutations.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

private slots:
    /**
     * @brief Announce updated numeric cells across both coordinate columns.
     * @param[in] first First zero-based changed row supplied by the provider.
     * @param[in] count Changed row count; nonpositive counts are ignored.
     */
    void onPointsChanged_(int first, int count);
    /**
     * @brief Announce a provider point insertion to Qt views.
     * @param[in] at First inserted zero-based row.
     * @param[in] count Positive inserted count supplied by the provider.
     */
    void onPointsInserted_(int at, int count);
    /**
     * @brief Announce a provider point removal to Qt views.
     * @param[in] at First removed zero-based row.
     * @param[in] count Positive removed count supplied by the provider.
     */
    void onPointsRemoved_(int at, int count);

private:
    QPointer<openswmmvis::transect::TransectProvider> m_provider;
    QString m_unitsSuffix;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_TRANSECTSTATIONTABLEMODEL_H
