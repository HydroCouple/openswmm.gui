/*!
 * \file   curveeditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/curveeditordialog.h"

#include "curve/curveprovider.h"
#include "curve/curveregistry.h"
#include "ui/panels/curvepointtablemodel.h"

#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScatterSeries>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>
#include <QUndoStack>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace openswmmvis::ui {

using openswmmvis::curve::CurvePoint;
using openswmmvis::curve::CurveProvider;
using openswmmvis::curve::CurveRegistry;
using openswmmvis::curve::CurveType;

namespace {

constexpr std::array<CurveType, 11> kAllTypes = {
    CurveType::Storage, CurveType::Diversion, CurveType::Rating,
    CurveType::Shape,   CurveType::Control,   CurveType::Tidal,
    CurveType::Pump1,   CurveType::Pump2,     CurveType::Pump3,
    CurveType::Pump4,   CurveType::Pump5,
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

CurveEditorDialog::CurveEditorDialog(CurveRegistry *registry,
                                      QUndoStack *undoStack,
                                      QWidget *parent)
    : QDialog(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
    , m_registry(registry)
    , m_undoStack(undoStack)
{
    setWindowTitle(tr("Curve Editor"));
    resize(960, 560);

    buildUi_();
    buildCreateCard_();
    if (m_createCard) m_createCard->hide();

    if (m_registry) {
        connect(m_registry, &CurveRegistry::providerAdded,
                this, &CurveEditorDialog::onProviderAdded_);
        connect(m_registry, &CurveRegistry::providerAboutToBeRemoved,
                this, &CurveEditorDialog::onProviderRemoved_);
        connect(m_registry, &CurveRegistry::providerRenamed,
                this, &CurveEditorDialog::onProviderRenamed_);
    }

    rebuildListModel_();
    if (m_listModel->rowCount() > 0)
        m_listView->setCurrentIndex(m_listModel->index(0, 0));
    else
        bindProvider_(nullptr);
}

CurveEditorDialog::~CurveEditorDialog() = default;

CurveEditorDialog *CurveEditorDialog::createNew(CurveRegistry *registry,
                                                 QUndoStack *undoStack,
                                                 QWidget *parent)
{
    auto *dlg = new CurveEditorDialog(registry, undoStack, parent);
    dlg->m_mode = Mode::CreateNew;
    if (dlg->m_createCard) dlg->m_createCard->show();
    if (dlg->m_nameEdit)   dlg->m_nameEdit->setFocus();
    dlg->setWindowTitle(tr("New Curve"));
    return dlg;
}

void CurveEditorDialog::openForCurve(const QString &name)
{
    show();
    raise();
    activateWindow();
    if (!m_registry || name.isEmpty()) return;
    auto *p = m_registry->findByName(name);
    if (p) selectProviderInList_(p);
}

CurveProvider *CurveEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI assembly
// ─────────────────────────────────────────────────────────────────────────────

void CurveEditorDialog::populateTypeCombo_(QComboBox *combo) const
{
    if (!combo) return;
    combo->clear();
    for (CurveType t : kAllTypes)
        combo->addItem(CurveProvider::typeLabel(t), int(t));
}

void CurveEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    outer->addWidget(m_splitter, 1);

    // ── Left pane: curves list + CRUD buttons ───────────────────────────────
    {
        auto *host = new QWidget(m_splitter);
        auto *lay  = new QVBoxLayout(host);
        lay->setContentsMargins(8, 8, 8, 8);
        lay->addWidget(new QLabel(tr("Curves"), host));

        m_listView = new QListView(host);
        m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_listView->setUniformItemSizes(true);
        m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
        m_listModel = new QStandardItemModel(this);
        m_listView->setModel(m_listModel);
        lay->addWidget(m_listView, 1);

        connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex &, const QModelIndex &) {
                    onListSelectionChanged_();
                });
        connect(m_listView, &QListView::customContextMenuRequested,
                this, &CurveEditorDialog::onListContextMenu_);

        auto *crudRow = new QHBoxLayout();
        m_newBtn    = new QPushButton(tr("+ New"), host);
        m_renameBtn = new QPushButton(tr("Rename"), host);
        m_deleteBtn = new QPushButton(tr("Delete"), host);
        m_newBtn->setToolTip(tr("Create a new curve."));
        m_renameBtn->setToolTip(tr("Rename the selected curve."));
        m_deleteBtn->setToolTip(tr("Delete the selected curve."));
        connect(m_newBtn,    &QPushButton::clicked,
                this, &CurveEditorDialog::onNewClicked_);
        connect(m_renameBtn, &QPushButton::clicked,
                this, &CurveEditorDialog::onRenameClicked_);
        connect(m_deleteBtn, &QPushButton::clicked,
                this, &CurveEditorDialog::onDeleteCurveClicked_);
        crudRow->addWidget(m_newBtn);
        crudRow->addStretch(1);
        crudRow->addWidget(m_renameBtn);
        crudRow->addWidget(m_deleteBtn);
        lay->addLayout(crudRow);

        m_splitter->addWidget(host);
    }

    // ── Center pane: type combo + point table + row controls ────────────────
    {
        auto *host = new QWidget(m_splitter);
        auto *lay  = new QVBoxLayout(host);
        lay->setContentsMargins(8, 8, 8, 8);

        auto *typeRow = new QHBoxLayout();
        typeRow->addWidget(new QLabel(tr("Type:"), host));
        m_typeCombo = new QComboBox(host);
        populateTypeCombo_(m_typeCombo);
        typeRow->addWidget(m_typeCombo, 1);
        connect(m_typeCombo, &QComboBox::currentIndexChanged,
                this, &CurveEditorDialog::onTypeComboChanged_);
        lay->addLayout(typeRow);

        m_typeHint = new QLabel(host);
        m_typeHint->setStyleSheet(QStringLiteral("color: #555;"));
        m_typeHint->setWordWrap(true);
        lay->addWidget(m_typeHint);

        m_table = new QTableView(host);
        m_tableModel = new CurvePointTableModel(this);
        m_table->setModel(m_tableModel);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setAlternatingRowColors(true);
        lay->addWidget(m_table, 1);

        auto *rowBtns = new QHBoxLayout();
        rowBtns->addStretch(1);
        m_addRowBtn = new QPushButton(tr("+ Add Row"), host);
        m_delRowBtn = new QPushButton(tr("− Delete Row(s)"), host);
        connect(m_addRowBtn, &QPushButton::clicked,
                this, &CurveEditorDialog::onAddRowClicked_);
        connect(m_delRowBtn, &QPushButton::clicked,
                this, &CurveEditorDialog::onDeleteRowsClicked_);
        rowBtns->addWidget(m_addRowBtn);
        rowBtns->addWidget(m_delRowBtn);
        lay->addLayout(rowBtns);

        m_splitter->addWidget(host);
    }

    // ── Right pane: line + scatter chart preview ────────────────────────────
    {
        m_chart = new QChart();
        m_chart->setBackgroundRoundness(0);
        m_chart->legend()->hide();
        m_chart->setMargins(QMargins(8, 8, 8, 8));

        m_line = new QLineSeries(m_chart);
        m_scatter = new QScatterSeries(m_chart);
        m_scatter->setMarkerSize(7.0);
        m_chart->addSeries(m_line);
        m_chart->addSeries(m_scatter);

        m_xAxis = new QValueAxis(m_chart);
        m_yAxis = new QValueAxis(m_chart);
        m_xAxis->setLabelFormat(QStringLiteral("%.3g"));
        m_yAxis->setLabelFormat(QStringLiteral("%.3g"));
        m_chart->addAxis(m_xAxis, Qt::AlignBottom);
        m_chart->addAxis(m_yAxis, Qt::AlignLeft);
        m_line->attachAxis(m_xAxis);    m_line->attachAxis(m_yAxis);
        m_scatter->attachAxis(m_xAxis); m_scatter->attachAxis(m_yAxis);

        m_chartView = new QChartView(m_chart, m_splitter);
        m_chartView->setRenderHint(QPainter::Antialiasing, true);
        m_splitter->addWidget(m_chartView);
    }

    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 3);

    m_status = new QStatusBar(this);
    m_countLabel = new QLabel(m_status);
    m_status->addPermanentWidget(m_countLabel);
    outer->addWidget(m_status);
}

void CurveEditorDialog::buildCreateCard_()
{
    auto *outer = qobject_cast<QVBoxLayout *>(layout());
    if (!outer) return;

    m_createCard = new QFrame(this);
    m_createCard->setFrameShape(QFrame::StyledPanel);
    m_createCard->setObjectName(QStringLiteral("curveCreateCard"));

    auto *cardLay = new QVBoxLayout(m_createCard);
    cardLay->setContentsMargins(12, 8, 12, 8);

    auto *r1 = new QHBoxLayout();
    r1->addWidget(new QLabel(tr("Name:"), m_createCard));
    m_nameEdit = new QLineEdit(m_createCard);
    m_nameEdit->setPlaceholderText(tr("Curve name (required)"));
    r1->addWidget(m_nameEdit, 1);
    cardLay->addLayout(r1);

    m_nameValidationLabel = new QLabel(m_createCard);
    m_nameValidationLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_nameValidationLabel->hide();
    cardLay->addWidget(m_nameValidationLabel);

    auto *r2 = new QHBoxLayout();
    r2->addWidget(new QLabel(tr("Type:"), m_createCard));
    m_createTypeCombo = new QComboBox(m_createCard);
    populateTypeCombo_(m_createTypeCombo);
    r2->addWidget(m_createTypeCombo);
    r2->addStretch(1);
    m_cancelCreateBtn = new QPushButton(tr("Cancel"), m_createCard);
    r2->addWidget(m_cancelCreateBtn);
    m_createBtn = new QPushButton(tr("Create"), m_createCard);
    m_createBtn->setDefault(true);
    m_createBtn->setEnabled(false);
    r2->addWidget(m_createBtn);
    cardLay->addLayout(r2);

    outer->insertWidget(0, m_createCard);

    connect(m_nameEdit, &QLineEdit::textChanged,
            this, &CurveEditorDialog::onCreateNewNameChanged_);
    connect(m_createBtn, &QPushButton::clicked,
            this, &CurveEditorDialog::onCreateNewSubmit_);
    connect(m_cancelCreateBtn, &QPushButton::clicked,
            this, &CurveEditorDialog::onCancelCreateClicked_);
}

// ─────────────────────────────────────────────────────────────────────────────
// List + provider binding
// ─────────────────────────────────────────────────────────────────────────────

void CurveEditorDialog::rebuildListModel_()
{
    m_listModel->clear();
    m_listModel->setHorizontalHeaderLabels({tr("Curves")});
    if (!m_registry) return;
    for (CurveProvider *p : m_registry->providers()) {
        auto *item = new QStandardItem(p->name());
        item->setData(QVariant::fromValue(reinterpret_cast<quintptr>(p)),
                      Qt::UserRole + 1);
        item->setToolTip(p->name() + tr("  ·  %1")
                                       .arg(CurveProvider::typeLabel(p->type())));
        m_listModel->appendRow(item);
    }
}

void CurveEditorDialog::selectProviderInList_(CurveProvider *p)
{
    if (!p) return;
    for (int r = 0; r < m_listModel->rowCount(); ++r) {
        const auto idx = m_listModel->index(r, 0);
        auto *item = m_listModel->itemFromIndex(idx);
        if (!item) continue;
        if (reinterpret_cast<CurveProvider *>(
                item->data(Qt::UserRole + 1).value<quintptr>()) == p) {
            m_listView->setCurrentIndex(idx);
            return;
        }
    }
}

void CurveEditorDialog::onListSelectionChanged_()
{
    const QModelIndex idx = m_listView->currentIndex();
    CurveProvider *p = nullptr;
    if (idx.isValid()) {
        auto *item = m_listModel->itemFromIndex(idx);
        if (item)
            p = reinterpret_cast<CurveProvider *>(
                item->data(Qt::UserRole + 1).value<quintptr>());
    }
    bindProvider_(p);
}

void CurveEditorDialog::bindProvider_(CurveProvider *p)
{
    if (m_current == p && p) {
        refreshChart_();
        updateStatusBar_();
        return;
    }
    if (m_current) m_current->disconnect(this);
    m_current = QPointer<CurveProvider>(p);

    if (m_tableModel) m_tableModel->setProvider(p);

    // Sync the type combo to the active provider's type without re-triggering
    // a write back to the provider.
    if (m_typeCombo) {
        m_suppressTypeSignal = true;
        if (p) {
            for (int i = 0; i < m_typeCombo->count(); ++i) {
                if (m_typeCombo->itemData(i).toInt() == int(p->type())) {
                    m_typeCombo->setCurrentIndex(i);
                    break;
                }
            }
        }
        m_typeCombo->setEnabled(p != nullptr);
        m_suppressTypeSignal = false;
    }
    if (m_typeHint)   m_typeHint->setText(p ? CurveProvider::typeLabel(p->type())
                                            : tr("(no curve selected)"));
    if (m_addRowBtn)  m_addRowBtn->setEnabled(p != nullptr);
    if (m_delRowBtn)  m_delRowBtn->setEnabled(p != nullptr);
    if (m_renameBtn)  m_renameBtn->setEnabled(p != nullptr);
    if (m_deleteBtn)  m_deleteBtn->setEnabled(p != nullptr);

    if (m_current) {
        connect(m_current, &CurveProvider::pointsChanged,
                this, &CurveEditorDialog::onProviderPointsChanged_);
        connect(m_current, &CurveProvider::pointsInserted,
                this, &CurveEditorDialog::onProviderPointsChanged_);
        connect(m_current, &CurveProvider::pointsRemoved,
                this, &CurveEditorDialog::onProviderPointsChanged_);
        connect(m_current, &CurveProvider::typeChanged,
                this, &CurveEditorDialog::onProviderTypeChanged_);
        connect(m_current, &CurveProvider::mutationRejected,
                this, &CurveEditorDialog::onMutationRejected_);
    }

    refreshChart_();
    updateStatusBar_();
}

// ─────────────────────────────────────────────────────────────────────────────
// Chart + status
// ─────────────────────────────────────────────────────────────────────────────

void CurveEditorDialog::refreshChart_()
{
    if (!m_line || !m_scatter || !m_xAxis || !m_yAxis) return;
    QList<QPointF> pts;
    if (m_current) {
        for (const auto &p : m_current->points())
            pts.append({p.x, p.y});
    }
    m_line->replace(pts);
    m_scatter->replace(pts);

    if (m_current && m_current->pointCount() > 0) {
        double xMin = pts.first().x(), xMax = pts.last().x();
        double yMin =  std::numeric_limits<double>::infinity();
        double yMax = -std::numeric_limits<double>::infinity();
        for (const auto &p : pts) {
            yMin = std::min(yMin, p.y());
            yMax = std::max(yMax, p.y());
        }
        if (xMin == xMax) { xMin -= 0.5; xMax += 0.5; }
        if (yMin == yMax) { yMin -= 0.5; yMax += 0.5; }
        const double xPad = 0.05 * (xMax - xMin);
        const double yPad = 0.05 * (yMax - yMin);
        m_xAxis->setRange(xMin - xPad, xMax + xPad);
        m_yAxis->setRange(yMin - yPad, yMax + yPad);
    } else {
        m_xAxis->setRange(0.0, 1.0);
        m_yAxis->setRange(0.0, 1.0);
    }

    // Axis titles track the provider's type labels.
    m_xAxis->setTitleText(m_current ? CurveProvider::xLabel(m_current->type())
                                     : tr("X"));
    m_yAxis->setTitleText(m_current ? CurveProvider::yLabel(m_current->type())
                                     : tr("Y"));
}

void CurveEditorDialog::updateStatusBar_()
{
    if (!m_countLabel) return;
    if (!m_current) {
        m_countLabel->setText({});
        return;
    }
    m_countLabel->setText(tr("%1 points").arg(m_current->pointCount()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Row controls
// ─────────────────────────────────────────────────────────────────────────────

void CurveEditorDialog::onAddRowClicked_()
{
    if (!m_current) return;
    // Append a new point with X = (last X + 1) so it lands at the end
    // monotonically; user can edit afterwards.
    const int n = m_current->pointCount();
    const double newX = (n > 0) ? (m_current->pointAt(n - 1).x + 1.0) : 0.0;
    m_current->insertPoint(newX, 0.0);
}

void CurveEditorDialog::invokeAddRow() { onAddRowClicked_(); }

void CurveEditorDialog::onDeleteRowsClicked_()
{
    if (!m_current || !m_table) return;
    const auto sel = m_table->selectionModel();
    if (!sel) return;
    QVector<int> rows;
    for (const auto &idx : sel->selectedRows()) rows.push_back(idx.row());
    if (rows.isEmpty() && sel->currentIndex().isValid())
        rows.push_back(sel->currentIndex().row());
    if (!rows.isEmpty()) m_current->removePointsAt(rows);
}

void CurveEditorDialog::invokeDeleteRows() { onDeleteRowsClicked_(); }

void CurveEditorDialog::onTypeComboChanged_(int index)
{
    if (m_suppressTypeSignal || !m_current || !m_typeCombo) return;
    const auto t = static_cast<CurveType>(m_typeCombo->itemData(index).toInt());
    if (t != m_current->type()) m_current->setType(t);
}

// ─────────────────────────────────────────────────────────────────────────────
// Registry signal handlers
// ─────────────────────────────────────────────────────────────────────────────

void CurveEditorDialog::onProviderAdded_(CurveProvider *p)
{
    if (!p) return;
    auto *item = new QStandardItem(p->name());
    item->setData(QVariant::fromValue(reinterpret_cast<quintptr>(p)),
                  Qt::UserRole + 1);
    item->setToolTip(p->name() + tr("  ·  %1")
                                   .arg(CurveProvider::typeLabel(p->type())));
    m_listModel->appendRow(item);
}

void CurveEditorDialog::onProviderRemoved_(CurveProvider *p)
{
    if (!p) return;
    for (int r = 0; r < m_listModel->rowCount(); ++r) {
        auto *item = m_listModel->item(r);
        if (!item) continue;
        if (reinterpret_cast<CurveProvider *>(
                item->data(Qt::UserRole + 1).value<quintptr>()) == p) {
            m_listModel->removeRow(r);
            break;
        }
    }
    if (m_current == p) bindProvider_(nullptr);
}

void CurveEditorDialog::onProviderRenamed_(CurveProvider *p,
                                            const QString &, const QString &now)
{
    if (!p) return;
    for (int r = 0; r < m_listModel->rowCount(); ++r) {
        auto *item = m_listModel->item(r);
        if (!item) continue;
        if (reinterpret_cast<CurveProvider *>(
                item->data(Qt::UserRole + 1).value<quintptr>()) == p) {
            item->setText(now);
            break;
        }
    }
    if (m_current == p)
        setWindowTitle(tr("Curve Editor — %1").arg(now));
}

void CurveEditorDialog::onProviderPointsChanged_()
{
    refreshChart_();
    updateStatusBar_();
}

void CurveEditorDialog::onProviderTypeChanged_()
{
    if (m_typeHint && m_current)
        m_typeHint->setText(CurveProvider::typeLabel(m_current->type()));
    // Axis titles need refresh; refreshChart_ rebuilds them.
    refreshChart_();
}

void CurveEditorDialog::onMutationRejected_(const QString &reason)
{
    if (m_status) m_status->showMessage(reason, 4000);
}

// ─────────────────────────────────────────────────────────────────────────────
// List CRUD: New / Rename / Delete + context menu
// ─────────────────────────────────────────────────────────────────────────────

void CurveEditorDialog::onNewClicked_()
{
    if (!m_createCard) return;
    m_mode = Mode::CreateNew;
    if (m_nameEdit) {
        m_nameEdit->clear();
        m_nameEdit->setFocus();
    }
    m_createCard->show();
}

void CurveEditorDialog::invokeNew() { onNewClicked_(); }

void CurveEditorDialog::onCancelCreateClicked_()
{
    if (m_createCard) m_createCard->hide();
    m_mode = Mode::Edit;
    if (m_nameEdit) m_nameEdit->clear();
}

void CurveEditorDialog::onRenameClicked_()
{
    if (!m_current || !m_registry) return;
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Rename Curve"),
        tr("New name:"), QLineEdit::Normal,
        m_current->name(), &ok).trimmed();
    if (!ok || newName.isEmpty()) return;
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename Curve"),
            tr("Could not rename to “%1” — name already in use.").arg(newName));
    }
}

bool CurveEditorDialog::renameCurrent(const QString &newName)
{
    if (!m_current || !m_registry) return false;
    return m_registry->rename(m_current, newName.trimmed());
}

void CurveEditorDialog::onDeleteCurveClicked_()
{
    if (!m_current || !m_registry) return;
    const QString name = m_current->name();
    const auto reply = QMessageBox::question(
        this, tr("Delete Curve"),
        tr("Delete curve “%1”?\n\nAny model object referencing this curve "
           "(storage units, outlets, pumps, custom cross-sections, etc.) "
           "will lose its reference.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    deleteCurrentSilently();
}

void CurveEditorDialog::deleteCurrentSilently()
{
    if (!m_current || !m_registry) return;
    m_registry->remove(m_current);
}

void CurveEditorDialog::onListContextMenu_(const QPoint &pos)
{
    if (!m_listView) return;
    const QModelIndex idx = m_listView->indexAt(pos);
    if (!idx.isValid()) return;
    m_listView->setCurrentIndex(idx);

    QMenu menu(this);
    QAction *actRename = menu.addAction(tr("Rename…"));
    QAction *actDelete = menu.addAction(tr("Delete"));
    QAction *picked = menu.exec(m_listView->viewport()->mapToGlobal(pos));
    if (picked == actRename) onRenameClicked_();
    else if (picked == actDelete) onDeleteCurveClicked_();
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateNew mode
// ─────────────────────────────────────────────────────────────────────────────

QString CurveEditorDialog::pendingName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

int CurveEditorDialog::pendingType() const
{
    return m_createTypeCombo ? m_createTypeCombo->currentData().toInt() : 0;
}

bool CurveEditorDialog::isCreateEnabled() const
{
    return m_createBtn && m_createBtn->isEnabled();
}

void CurveEditorDialog::submitCreateNew() { onCreateNewSubmit_(); }

void CurveEditorDialog::onCreateNewNameChanged_(const QString &text)
{
    if (!m_createBtn || !m_nameValidationLabel || !m_nameEdit) return;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        m_createBtn->setEnabled(false);
        m_nameValidationLabel->hide();
        m_nameEdit->setStyleSheet(QString());
        return;
    }
    const bool collides = m_registry && m_registry->hasName(trimmed);
    if (collides) {
        m_createBtn->setEnabled(false);
        m_nameValidationLabel->setText(
            tr("A curve named “%1” already exists.").arg(trimmed));
        m_nameValidationLabel->show();
        m_nameEdit->setStyleSheet(QStringLiteral("border: 1px solid #c0392b;"));
    } else {
        m_createBtn->setEnabled(true);
        m_nameValidationLabel->hide();
        m_nameEdit->setStyleSheet(QString());
    }
}

void CurveEditorDialog::onCreateNewSubmit_()
{
    if (!m_registry || !m_nameEdit) return;
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty() || m_registry->hasName(name)) return;
    const auto type = static_cast<CurveType>(
        m_createTypeCombo ? m_createTypeCombo->currentData().toInt() : int(CurveType::Storage));

    CurveProvider *p = m_registry->create(name, type);
    if (!p) return;

    m_mode = Mode::Edit;
    if (m_createCard) m_createCard->hide();
    setWindowTitle(tr("Curve Editor — %1").arg(name));
    selectProviderInList_(p);
}

} // namespace openswmmvis::ui
