/**
 * @file      crsselectiondialog.h
 * @brief     Modal dialog for browsing and selecting a CRS from the GDAL database.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef CRSSELECTIONDIALOG_H
#define CRSSELECTIONDIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QComboBox;
class QTreeView;
class QTextBrowser;
class QDialogButtonBox;
class QSortFilterProxyModel;
class QStandardItemModel;
class QStandardItem;
class SpatialReferenceSystem;

/*!
 * \class CRSSelectionDialog
 * \brief Modal dialog for browsing and selecting a CRS from the GDAL database.
 * \details Displays CRSes in a hierarchical tree organized by:
 *   - Type (Geographic 2D, Projected, etc.)
 *   - Area of use / region (groups state planes by state, UTM zones by region, etc.)
 *   - Individual CRS entries
 *
 *  Supports filtering by keyword, authority, and type.
 */
class CRSSelectionDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QWidget owner; parentless dialog is managed by caller.
     * @brief Build the filter controls, database tree and WKT preview synchronously on the GUI
     *   thread. Initially no selection and OK disabled; does not change any model CRS or retain an
     *   SRS.
     */
    explicit CRSSelectionDialog(QWidget *parent = nullptr);
    /**
     * @brief Destroy the owned widgets and item model; returned selectedSRS objects remain
     *   caller-owned.
     */
    ~CRSSelectionDialog() override;

    /**
     * @param[in] current Optional borrowed CRS inspected only during this call; null or empty
     *   authority is a no-op.
     * @brief Select and scroll to the first exact authority-code leaf in the CURRENT filtered tree.
     * @note Missing or filtered-out authority leaves selection unchanged. Does not relax filters,
     *   create/own the supplied CRS or accept the dialog. Selection changes update preview and OK
     *   state through the connected slot.
     */
    void setCurrentCRS(const SpatialReferenceSystem *current);

    /**
     * @return Newly allocated caller-owned CRS on EACH call, or null for empty selection,
     *   malformed/noninteger database code or creation failure. LOCAL:METERS/LOCAL:FEET create
     *   local linear-unit systems without a database lookup.
     * @note Reads the stored selection without requiring accepted dialog result; caller must check
     *   result and apply/transform the model separately. No cached object or ownership transfer
     *   from the tree.
     */
    [[nodiscard]] SpatialReferenceSystem *selectedSRS() const;
    /**
     * @return Copied stored authority-code token, initially empty; database entries or
     *   LOCAL:METERS/LOCAL:FEET. Rejection does not clear an existing selection, so check QDialog
     *   result before applying.
     */
    [[nodiscard]] QString selectedAuthCode() const;

private slots:
    /**
     * @param[in] text Signal payload ignored; current widgets supply all filters.
     * @brief Rebuild the complete tree synchronously from current authority/type and trimmed search
     *   text.
     * @note Every change performs a fresh database query without debounce, discards prior
     *   items/selection and expands all resulting groups. Does not change project coordinates.
     */
    void onSearchTextChanged(const QString &text);
    /**
     * @param[in] index Signal payload ignored; current widgets supply all filters.
     * @brief Rebuild the complete tree synchronously from current authority/type and trimmed search
     *   text.
     * @note Every change performs a fresh database query without debounce, discards prior
     *   items/selection and expands all resulting groups. Does not change project coordinates.
     */
    void onAuthorityFilterChanged(int index);
    /**
     * @param[in] index Signal payload ignored; current widgets supply all filters.
     * @brief Rebuild the complete tree synchronously from current authority/type and trimmed search
     *   text.
     * @note Every change performs a fresh database query without debounce, discards prior
     *   items/selection and expands all resulting groups. Does not change project coordinates.
     */
    void onTypeFilterChanged(int index);
    /**
     * @brief Store the first selected leaf code and refresh WKT; clear code/preview and disable OK
     *   for empty or group selection.
     * @note A leaf enables OK before CRS construction, so preview creation failure does not prevent
     *   acceptance. Does not retain a constructed SRS or modify model data.
     */
    void onTreeSelectionChanged();
    /**
     * @brief Accept a nonempty stored selection and record numeric authority codes in CRSManager
     *   recent preferences.
     * @note Empty selection is a no-op; synthetic local entries are not added to numeric recents.
     *   Does not verify selectedSRS creation or transform geometry; a nonempty malformed token can
     *   still be accepted.
     */
    void onAccepted();

private:
    /**
     * @brief Create owned filter widgets, single-row-selection noneditable tree, WKT browser and
     *   connected buttons.
     * @note Authority choices snapshot the current database authorities. Named dialog/splitter
     *   allow host layout persistence. Does not populate the tree or install a global watcher
     *   itself. GUI-thread construction helper.
     */
    void setupUi();
    /**
     * @param[in] authority Database authority filter; empty permits all authorities and synthetic
     *   local entries.
     * @param[in] typeFilter Case-insensitive substring matched against database type; empty permits
     *   all. Synthetic local entries additionally require empty or exactly local, case-insensitive.
     * @param[in] searchText Database keyword filter forwarded unchanged; synthetic entries match
     *   label or literal local, case-insensitively. Slots supply trimmed editor text.
     * @brief Replace all items with type/area/CRS groups and expand them synchronously.
     * @note Groups follow first database occurrence; empty area becomes Other. Local metre/foot
     *   leaves precede database groups when eligible. All groups are enabled but not selectable.
     *   Removing rows can clear stored selection through Qt selection notifications; selection is
     *   not restored. No CRS allocation, coordinate change or undo operation.
     */
    void buildTree(const QString &authority = {}, const QString &typeFilter = {},
                   const QString &searchText = {});
    /**
     * @brief Create a temporary selected CRS, render its WKT and release it before returning.
     * @note Empty selection clears preview; creation failure shows a message. Malformed database
     *   token or noninteger code leaves existing preview unchanged. Does not disable OK or modify
     *   model/selection and does not retain the temporary SRS.
     */
    void updatePreview();

    // Data roles for tree items
    static constexpr int AuthCodeRole = Qt::UserRole + 1;
    static constexpr int IsLeafRole = Qt::UserRole + 2;

    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_authorityCombo = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QTreeView *m_treeView = nullptr;
    QTextBrowser *m_wktPreview = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
    QStandardItemModel *m_treeModel = nullptr;

    QString m_selectedAuthCode;
};

#endif // CRSSELECTIONDIALOG_H
