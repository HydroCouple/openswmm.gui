/*!
 * \file   crsselectiondialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
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

    explicit CRSSelectionDialog(QWidget *parent = nullptr);
    ~CRSSelectionDialog() override;

    void setCurrentCRS(const SpatialReferenceSystem *current);

    [[nodiscard]] SpatialReferenceSystem *selectedSRS() const;
    [[nodiscard]] QString selectedAuthCode() const;

private slots:
    void onSearchTextChanged(const QString &text);
    void onAuthorityFilterChanged(int index);
    void onTypeFilterChanged(int index);
    void onTreeSelectionChanged();
    void onAccepted();

private:
    void setupUi();
    void buildTree(const QString &authority = {},
                   const QString &typeFilter = {},
                   const QString &searchText = {});
    void updatePreview();

    // Data roles for tree items
    static constexpr int AuthCodeRole = Qt::UserRole + 1;
    static constexpr int IsLeafRole   = Qt::UserRole + 2;

    QLineEdit              *m_searchEdit      = nullptr;
    QComboBox              *m_authorityCombo  = nullptr;
    QComboBox              *m_typeCombo       = nullptr;
    QTreeView              *m_treeView        = nullptr;
    QTextBrowser           *m_wktPreview      = nullptr;
    QDialogButtonBox       *m_buttonBox       = nullptr;
    QStandardItemModel     *m_treeModel       = nullptr;

    QString                 m_selectedAuthCode;
};

#endif // CRSSELECTIONDIALOG_H
