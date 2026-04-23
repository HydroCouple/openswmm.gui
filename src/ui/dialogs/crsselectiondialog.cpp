/*!
 * \file   crsselectiondialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/crsselectiondialog.h"
#include "map/crsmanager.h"
#include "map/spatialreferencesystem.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTextBrowser>
#include <QTreeView>
#include <QPushButton>
#include <QVBoxLayout>

#include <memory>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CRSSelectionDialog::CRSSelectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Select Coordinate Reference System"));
    resize(840, 560);
    setupUi();
    buildTree();
}

CRSSelectionDialog::~CRSSelectionDialog() = default;

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void CRSSelectionDialog::setupUi()
{
    auto *vlay = new QVBoxLayout(this);

    // --- Filter row --------------------------------------------------------
    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Search:"), this));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Type name or EPSG code …"));
    m_searchEdit->setClearButtonEnabled(true);
    filterRow->addWidget(m_searchEdit, 1);

    filterRow->addWidget(new QLabel(tr("Authority:"), this));
    m_authorityCombo = new QComboBox(this);
    m_authorityCombo->addItem(tr("All"), QString());
    for (const QString &auth : CRSManager::instance().availableAuthorities())
        m_authorityCombo->addItem(auth, auth);
    filterRow->addWidget(m_authorityCombo);

    filterRow->addWidget(new QLabel(tr("Type:"), this));
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(tr("All"), QString());
    m_typeCombo->addItem(tr("Geographic 2D"), QStringLiteral("geographic 2D"));
    m_typeCombo->addItem(tr("Geographic 3D"), QStringLiteral("geographic 3D"));
    m_typeCombo->addItem(tr("Projected"),     QStringLiteral("projected"));
    m_typeCombo->addItem(tr("Compound"),      QStringLiteral("compound"));
    m_typeCombo->addItem(tr("Geocentric"),    QStringLiteral("geocentric"));
    filterRow->addWidget(m_typeCombo);

    vlay->addLayout(filterRow);

    // --- Splitter: tree | WKT preview --------------------------------------
    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_treeModel = new QStandardItemModel(this);
    m_treeModel->setHorizontalHeaderLabels({tr("Name"), tr("Authority:Code")});

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_treeModel);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setHeaderHidden(false);
    m_treeView->setAnimated(true);
    m_treeView->setUniformRowHeights(true);
    // Interactive mode = user can drag column dividers. Stretch / ResizeToContents
    // both lock the divider; Interactive lets the user resize freely while
    // setStretchLastSection keeps the trailing column filling residual width.
    m_treeView->header()->setSectionsMovable(false);
    m_treeView->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_treeView->header()->setStretchLastSection(true);
    m_treeView->setColumnWidth(0, 480);
    m_treeView->setColumnWidth(1, 140);
    splitter->addWidget(m_treeView);

    // WKT preview
    m_wktPreview = new QTextBrowser(this);
    m_wktPreview->setPlaceholderText(tr("Select a CRS to see its WKT definition …"));
    m_wktPreview->setMaximumHeight(140);
    splitter->addWidget(m_wktPreview);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    vlay->addWidget(splitter, 1);

    // --- Button box --------------------------------------------------------
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    vlay->addWidget(m_buttonBox);

    // --- Connections -------------------------------------------------------
    connect(m_searchEdit,     &QLineEdit::textChanged,
            this,             &CRSSelectionDialog::onSearchTextChanged);
    connect(m_authorityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,             &CRSSelectionDialog::onAuthorityFilterChanged);
    connect(m_typeCombo,      QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,             &CRSSelectionDialog::onTypeFilterChanged);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this,                         &CRSSelectionDialog::onTreeSelectionChanged);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &CRSSelectionDialog::onAccepted);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CRSSelectionDialog::setCurrentCRS(const SpatialReferenceSystem *current)
{
    if (!current)
        return;

    const QString authCode = current->toAuthority();   // "EPSG:4326"
    if (authCode.isEmpty())
        return;

    // Walk every item in the tree looking for a leaf with a matching AuthCodeRole.
    std::function<QModelIndex(const QModelIndex &)> findItem;
    findItem = [&](const QModelIndex &parent) -> QModelIndex {
        const int rows = m_treeModel->rowCount(parent);
        for (int r = 0; r < rows; ++r)
        {
            QModelIndex idx = m_treeModel->index(r, 0, parent);
            if (idx.data(IsLeafRole).toBool() && idx.data(AuthCodeRole).toString() == authCode)
                return idx;

            QModelIndex found = findItem(idx);
            if (found.isValid())
                return found;
        }
        return {};
    };

    QModelIndex match = findItem(QModelIndex());
    if (match.isValid())
    {
        m_treeView->setCurrentIndex(match);
        m_treeView->scrollTo(match);
    }
}

SpatialReferenceSystem *CRSSelectionDialog::selectedSRS() const
{
    if (m_selectedAuthCode.isEmpty())
        return nullptr;

    const int sep = m_selectedAuthCode.lastIndexOf(QLatin1Char(':'));
    if (sep < 0)
        return nullptr;

    const QString auth = m_selectedAuthCode.left(sep);
    bool ok = false;
    const int code = m_selectedAuthCode.mid(sep + 1).toInt(&ok);
    if (!ok)
        return nullptr;

    return CRSManager::instance().createFromAuthCode(auth, code);
}

QString CRSSelectionDialog::selectedAuthCode() const
{
    return m_selectedAuthCode;
}

// ---------------------------------------------------------------------------
// Tree builder
// ---------------------------------------------------------------------------

void CRSSelectionDialog::buildTree(const QString &authority,
                                   const QString &typeFilter,
                                   const QString &searchText)
{
    m_treeModel->removeRows(0, m_treeModel->rowCount());

    const QList<CRSInfo> list =
        CRSManager::instance().queryDatabase(searchText, authority, false);

    // Two-level grouping: Type → Area → CRS leaf items
    //   typeNodes[type]                 → top-level QStandardItem*
    //   areaNodes[type][area]           → second-level QStandardItem*
    QMap<QString, QStandardItem *> typeNodes;
    QMap<QString, QMap<QString, QStandardItem *>> areaNodes;

    for (const CRSInfo &info : list)
    {
        // Apply type filter
        if (!typeFilter.isEmpty()
            && !info.type.contains(typeFilter, Qt::CaseInsensitive))
            continue;

        // --- Ensure type-level node ---
        QStandardItem *typeItem = typeNodes.value(info.type);
        if (!typeItem)
        {
            typeItem = new QStandardItem(info.type);
            QFont f = typeItem->font();
            f.setBold(true);
            typeItem->setFont(f);
            typeItem->setData(false, IsLeafRole);
            typeItem->setFlags(Qt::ItemIsEnabled);

            auto *col1 = new QStandardItem();
            col1->setFlags(Qt::ItemIsEnabled);
            m_treeModel->appendRow({typeItem, col1});
            typeNodes[info.type] = typeItem;
        }

        // --- Ensure area-level node ---
        const QString area = info.areaName.isEmpty() ? tr("Other") : info.areaName;
        QStandardItem *areaItem = areaNodes[info.type].value(area);
        if (!areaItem)
        {
            areaItem = new QStandardItem(area);
            areaItem->setData(false, IsLeafRole);
            areaItem->setFlags(Qt::ItemIsEnabled);

            auto *col1 = new QStandardItem();
            col1->setFlags(Qt::ItemIsEnabled);
            typeItem->appendRow({areaItem, col1});
            areaNodes[info.type][area] = areaItem;
        }

        // --- CRS leaf item ---
        const QString authCode = info.authName + QLatin1Char(':') + QString::number(info.code);
        auto *nameItem = new QStandardItem(info.name);
        nameItem->setData(authCode, AuthCodeRole);
        nameItem->setData(true, IsLeafRole);
        nameItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

        auto *codeItem = new QStandardItem(authCode);
        codeItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        areaItem->appendRow({nameItem, codeItem});
    }

    m_treeView->expandAll();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void CRSSelectionDialog::updatePreview()
{
    if (m_selectedAuthCode.isEmpty())
    {
        m_wktPreview->clear();
        return;
    }

    const int sep = m_selectedAuthCode.lastIndexOf(QLatin1Char(':'));
    if (sep < 0)
        return;

    const QString auth = m_selectedAuthCode.left(sep);
    bool ok = false;
    const int code = m_selectedAuthCode.mid(sep + 1).toInt(&ok);
    if (!ok)
        return;

    std::unique_ptr<SpatialReferenceSystem> srs(
        CRSManager::instance().createFromAuthCode(auth, code));
    if (!srs)
    {
        m_wktPreview->setPlainText(tr("Could not load CRS."));
        return;
    }

    m_wktPreview->setPlainText(srs->toWkt());
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void CRSSelectionDialog::onSearchTextChanged(const QString & /*text*/)
{
    buildTree(m_authorityCombo->currentData().toString(),
              m_typeCombo->currentData().toString(),
              m_searchEdit->text().trimmed());
}

void CRSSelectionDialog::onAuthorityFilterChanged(int /*index*/)
{
    buildTree(m_authorityCombo->currentData().toString(),
              m_typeCombo->currentData().toString(),
              m_searchEdit->text().trimmed());
}

void CRSSelectionDialog::onTypeFilterChanged(int /*index*/)
{
    buildTree(m_authorityCombo->currentData().toString(),
              m_typeCombo->currentData().toString(),
              m_searchEdit->text().trimmed());
}

void CRSSelectionDialog::onTreeSelectionChanged()
{
    const QModelIndexList sel = m_treeView->selectionModel()->selectedRows();
    if (sel.isEmpty() || !sel.first().data(IsLeafRole).toBool())
    {
        m_selectedAuthCode.clear();
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        m_wktPreview->clear();
        return;
    }

    m_selectedAuthCode = sel.first().data(AuthCodeRole).toString();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    updatePreview();
}

void CRSSelectionDialog::onAccepted()
{
    if (m_selectedAuthCode.isEmpty())
        return;

    const int sep = m_selectedAuthCode.lastIndexOf(QLatin1Char(':'));
    if (sep >= 0)
    {
        const QString auth = m_selectedAuthCode.left(sep);
        bool ok = false;
        const int code = m_selectedAuthCode.mid(sep + 1).toInt(&ok);
        if (ok)
            CRSManager::instance().addRecentCRS(auth, code);
    }

    accept();
}
