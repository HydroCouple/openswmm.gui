/*!
 * \file   patterneditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/patterneditordialog.h"

#include "pattern/patternprovider.h"
#include "pattern/patternregistry.h"
#include "ui/panels/patternfactortablemodel.h"

#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QPainter>
#include <QValueAxis>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>

namespace openswmmvis::ui {

using openswmmvis::pattern::PatternProvider;
using openswmmvis::pattern::PatternRegistry;
using openswmmvis::pattern::PatternType;

namespace {

QString typeLabel(PatternType t)
{
    switch (t) {
    case PatternType::Monthly: return QObject::tr("Monthly (12 factors)");
    case PatternType::Daily:   return QObject::tr("Daily (7 factors)");
    case PatternType::Hourly:  return QObject::tr("Hourly (24 factors, weekday)");
    case PatternType::Weekend: return QObject::tr("Weekend (24 factors)");
    }
    return {};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

PatternEditorDialog::PatternEditorDialog(PatternRegistry *registry,
                                          QUndoStack *undoStack,
                                          QWidget *parent)
    : QDialog(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
    , m_registry(registry)
    , m_undoStack(undoStack)
{
    setWindowTitle(tr("Time Pattern Editor"));
    resize(900, 520);

    buildUi_();
    buildCreateCard_();
    if (m_createCard) m_createCard->hide();   // hidden by default; Edit mode.

    if (m_registry) {
        connect(m_registry, &PatternRegistry::providerAdded,
                this, &PatternEditorDialog::onProviderAdded_);
        connect(m_registry, &PatternRegistry::providerAboutToBeRemoved,
                this, &PatternEditorDialog::onProviderRemoved_);
        connect(m_registry, &PatternRegistry::providerRenamed,
                this, &PatternEditorDialog::onProviderRenamed_);
    }

    rebuildListModel_();
    // Pre-select the first pattern if any exist.
    if (m_listModel->rowCount() > 0) {
        m_listView->setCurrentIndex(m_listModel->index(0, 0));
    } else {
        bindProvider_(nullptr);
    }
}

PatternEditorDialog::~PatternEditorDialog() = default;

PatternEditorDialog *PatternEditorDialog::createNew(PatternRegistry *registry,
                                                     QUndoStack *undoStack,
                                                     QWidget *parent)
{
    auto *dlg = new PatternEditorDialog(registry, undoStack, parent);
    dlg->m_mode = Mode::CreateNew;
    if (dlg->m_createCard) dlg->m_createCard->show();
    if (dlg->m_nameEdit)   dlg->m_nameEdit->setFocus();
    dlg->setWindowTitle(tr("New Time Pattern"));
    return dlg;
}

void PatternEditorDialog::openForPattern(const QString &name)
{
    show();
    raise();
    activateWindow();
    if (!m_registry || name.isEmpty()) return;
    auto *p = m_registry->findByName(name);
    if (p) selectProviderInList_(p);
}

PatternProvider *PatternEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI assembly
// ─────────────────────────────────────────────────────────────────────────────

void PatternEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    outer->addWidget(m_splitter, /*stretch=*/1);

    // ── Left pane: patterns list + CRUD buttons ─────────────────────────────
    {
        auto *leftHost = new QWidget(m_splitter);
        auto *leftLay  = new QVBoxLayout(leftHost);
        leftLay->setContentsMargins(8, 8, 8, 8);
        leftLay->addWidget(new QLabel(tr("Patterns"), leftHost));

        m_listView = new QListView(leftHost);
        m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_listView->setUniformItemSizes(true);
        m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
        m_listModel = new QStandardItemModel(this);
        m_listView->setModel(m_listModel);
        leftLay->addWidget(m_listView, /*stretch=*/1);

        connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex &, const QModelIndex &) {
                    onListSelectionChanged_();
                });
        connect(m_listView, &QListView::customContextMenuRequested,
                this, &PatternEditorDialog::onListContextMenu_);

        // CRUD buttons row.
        auto *crudRow = new QHBoxLayout();
        m_newBtn    = new QPushButton(tr("+ New"), leftHost);
        m_renameBtn = new QPushButton(tr("Rename"), leftHost);
        m_deleteBtn = new QPushButton(tr("Delete"), leftHost);
        m_newBtn->setToolTip(tr("Create a new time pattern."));
        m_renameBtn->setToolTip(tr("Rename the selected pattern."));
        m_deleteBtn->setToolTip(tr("Delete the selected pattern."));
        connect(m_newBtn,    &QPushButton::clicked,
                this, &PatternEditorDialog::onNewClicked_);
        connect(m_renameBtn, &QPushButton::clicked,
                this, &PatternEditorDialog::onRenameClicked_);
        connect(m_deleteBtn, &QPushButton::clicked,
                this, &PatternEditorDialog::onDeleteClicked_);
        crudRow->addWidget(m_newBtn);
        crudRow->addStretch(1);
        crudRow->addWidget(m_renameBtn);
        crudRow->addWidget(m_deleteBtn);
        leftLay->addLayout(crudRow);

        m_splitter->addWidget(leftHost);
    }

    // ── Center pane: factor table + normalize controls + status ─────────────
    {
        auto *centerHost = new QWidget(m_splitter);
        auto *centerLay  = new QVBoxLayout(centerHost);
        centerLay->setContentsMargins(8, 8, 8, 8);

        m_typeLabel = new QLabel(centerHost);
        m_typeLabel->setStyleSheet(QStringLiteral("color: #555;"));
        centerLay->addWidget(m_typeLabel);

        m_table = new QTableView(centerHost);
        m_tableModel = new PatternFactorTableModel(this);
        m_table->setModel(m_tableModel);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_table->horizontalHeader()->setSectionsClickable(false);
        m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        m_table->verticalHeader()->setDefaultSectionSize(22);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setAlternatingRowColors(true);
        centerLay->addWidget(m_table, /*stretch=*/1);

        // Normalize row.
        auto *normRow = new QHBoxLayout();
        normRow->addWidget(new QLabel(tr("Normalize to sum ="), centerHost));
        m_normalizeTargetSpin = new QDoubleSpinBox(centerHost);
        m_normalizeTargetSpin->setDecimals(4);
        m_normalizeTargetSpin->setRange(0.0001, 1.0e9);
        m_normalizeTargetSpin->setValue(1.0);
        m_normalizeTargetSpin->setSingleStep(0.1);
        normRow->addWidget(m_normalizeTargetSpin);
        normRow->addStretch(1);
        m_normalizeBtn = new QPushButton(tr("Normalize"), centerHost);
        m_normalizeBtn->setToolTip(
            tr("Rescale every factor so that all factors sum to the target value."));
        connect(m_normalizeBtn, &QPushButton::clicked,
                this, &PatternEditorDialog::onNormalizeClicked_);
        normRow->addWidget(m_normalizeBtn);
        centerLay->addLayout(normRow);

        m_splitter->addWidget(centerHost);
    }

    // ── Right pane: bar chart preview ───────────────────────────────────────
    {
        m_chart = new QChart();
        m_chart->setBackgroundRoundness(0);
        m_chart->legend()->hide();
        m_chart->setMargins(QMargins(8, 8, 8, 8));

        m_barSet    = new QBarSet(tr("Factor"));
        m_barSeries = new QBarSeries(m_chart);
        m_barSeries->append(m_barSet);
        m_chart->addSeries(m_barSeries);

        m_xAxis = new QBarCategoryAxis(m_chart);
        m_chart->addAxis(m_xAxis, Qt::AlignBottom);
        m_barSeries->attachAxis(m_xAxis);

        m_yAxis = new QValueAxis(m_chart);
        m_yAxis->setLabelFormat(QStringLiteral("%.2f"));
        m_chart->addAxis(m_yAxis, Qt::AlignLeft);
        m_barSeries->attachAxis(m_yAxis);

        m_chartView = new QChartView(m_chart, m_splitter);
        m_chartView->setRenderHint(QPainter::Antialiasing, true);
        m_splitter->addWidget(m_chartView);
    }

    // Splitter weights — list narrow, table mid, chart wide.
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 3);

    // ── Status bar ──────────────────────────────────────────────────────────
    m_status = new QStatusBar(this);
    m_sumLabel = new QLabel(m_status);
    m_status->addPermanentWidget(m_sumLabel);
    outer->addWidget(m_status);
}

void PatternEditorDialog::buildCreateCard_()
{
    auto *outer = qobject_cast<QVBoxLayout *>(layout());
    if (!outer) return;

    m_createCard = new QFrame(this);
    m_createCard->setFrameShape(QFrame::StyledPanel);
    m_createCard->setObjectName(QStringLiteral("patternCreateCard"));

    auto *cardLay = new QVBoxLayout(m_createCard);
    cardLay->setContentsMargins(12, 8, 12, 8);

    auto *row1 = new QHBoxLayout();
    row1->addWidget(new QLabel(tr("Name:"), m_createCard));
    m_nameEdit = new QLineEdit(m_createCard);
    m_nameEdit->setPlaceholderText(tr("Pattern name (required)"));
    row1->addWidget(m_nameEdit, /*stretch=*/1);
    cardLay->addLayout(row1);

    m_nameValidationLabel = new QLabel(m_createCard);
    m_nameValidationLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_nameValidationLabel->hide();
    cardLay->addWidget(m_nameValidationLabel);

    auto *row2 = new QHBoxLayout();
    row2->addWidget(new QLabel(tr("Type:"), m_createCard));
    m_typeCombo = new QComboBox(m_createCard);
    m_typeCombo->addItem(tr("Monthly (12)"), int(PatternType::Monthly));
    m_typeCombo->addItem(tr("Daily (7)"),    int(PatternType::Daily));
    m_typeCombo->addItem(tr("Hourly (24)"),  int(PatternType::Hourly));
    m_typeCombo->addItem(tr("Weekend (24)"), int(PatternType::Weekend));
    row2->addWidget(m_typeCombo);
    row2->addStretch(1);
    m_cancelCreateBtn = new QPushButton(tr("Cancel"), m_createCard);
    row2->addWidget(m_cancelCreateBtn);
    m_createBtn = new QPushButton(tr("Create"), m_createCard);
    m_createBtn->setDefault(true);
    m_createBtn->setEnabled(false);
    row2->addWidget(m_createBtn);
    cardLay->addLayout(row2);

    outer->insertWidget(0, m_createCard);

    connect(m_nameEdit, &QLineEdit::textChanged,
            this, &PatternEditorDialog::onCreateNewNameChanged_);
    connect(m_createBtn, &QPushButton::clicked,
            this, &PatternEditorDialog::onCreateNewSubmit_);
    connect(m_cancelCreateBtn, &QPushButton::clicked,
            this, &PatternEditorDialog::onCancelCreateClicked_);
}

// ─────────────────────────────────────────────────────────────────────────────
// List + provider binding
// ─────────────────────────────────────────────────────────────────────────────

void PatternEditorDialog::rebuildListModel_()
{
    m_listModel->clear();
    m_listModel->setHorizontalHeaderLabels({tr("Patterns")});
    if (!m_registry) return;
    for (PatternProvider *p : m_registry->providers()) {
        auto *item = new QStandardItem(p->name());
        item->setData(QVariant::fromValue(reinterpret_cast<quintptr>(p)),
                      Qt::UserRole + 1);
        const QString sub = tr("  ·  %1").arg(typeLabel(p->type()));
        item->setToolTip(p->name() + sub);
        m_listModel->appendRow(item);
    }
}

void PatternEditorDialog::selectProviderInList_(PatternProvider *p)
{
    if (!p) return;
    for (int r = 0; r < m_listModel->rowCount(); ++r) {
        const auto idx = m_listModel->index(r, 0);
        auto *item = m_listModel->itemFromIndex(idx);
        if (!item) continue;
        const auto ptr = reinterpret_cast<PatternProvider *>(
            item->data(Qt::UserRole + 1).value<quintptr>());
        if (ptr == p) {
            m_listView->setCurrentIndex(idx);
            return;
        }
    }
}

void PatternEditorDialog::onListSelectionChanged_()
{
    const QModelIndex idx = m_listView->currentIndex();
    PatternProvider *p = nullptr;
    if (idx.isValid()) {
        auto *item = m_listModel->itemFromIndex(idx);
        if (item) {
            p = reinterpret_cast<PatternProvider *>(
                item->data(Qt::UserRole + 1).value<quintptr>());
        }
    }
    bindProvider_(p);
}

void PatternEditorDialog::bindProvider_(PatternProvider *p)
{
    if (m_current == p && p) {
        // Same provider — refresh chart from current state.
        refreshChart_();
        updateStatusBar_();
        return;
    }
    if (m_current) m_current->disconnect(this);
    m_current = QPointer<PatternProvider>(p);

    if (m_tableModel) m_tableModel->setProvider(p);

    if (m_typeLabel) {
        m_typeLabel->setText(p ? typeLabel(p->type()) : tr("(no pattern selected)"));
    }
    if (m_normalizeBtn)         m_normalizeBtn->setEnabled(p != nullptr);
    if (m_normalizeTargetSpin)  m_normalizeTargetSpin->setEnabled(p != nullptr);
    if (m_renameBtn)            m_renameBtn->setEnabled(p != nullptr);
    if (m_deleteBtn)            m_deleteBtn->setEnabled(p != nullptr);

    if (m_current) {
        connect(m_current, &PatternProvider::factorChanged,
                this, &PatternEditorDialog::onProviderFactorChanged_);
        connect(m_current, &PatternProvider::factorsChanged,
                this, &PatternEditorDialog::onProviderFactorsChanged_);
        connect(m_current, &PatternProvider::typeChanged,
                this, &PatternEditorDialog::onProviderTypeChanged_);
        connect(m_current, &PatternProvider::mutationRejected,
                this, &PatternEditorDialog::onMutationRejected_);
    }

    refreshChart_();
    updateStatusBar_();
}

// ─────────────────────────────────────────────────────────────────────────────
// Chart + status
// ─────────────────────────────────────────────────────────────────────────────

void PatternEditorDialog::refreshChart_()
{
    if (!m_barSet || !m_xAxis || !m_yAxis) return;

    // Clear the bar set + categories.
    while (m_barSet->count() > 0) m_barSet->remove(0);
    m_xAxis->clear();

    if (!m_current || m_current->factorCount() == 0) {
        m_yAxis->setRange(0.0, 1.0);
        return;
    }

    QStringList categories;
    double maxV = 0.0;
    for (int i = 0; i < m_current->factorCount(); ++i) {
        const double v = m_current->factor(i);
        m_barSet->append(v);
        categories << PatternProvider::rowLabel(m_current->type(), i);
        maxV = std::max(maxV, v);
    }
    m_xAxis->append(categories);
    m_yAxis->setRange(0.0, std::max(1.0, maxV * 1.1));
}

void PatternEditorDialog::updateStatusBar_()
{
    if (!m_sumLabel) return;
    if (!m_current) {
        m_sumLabel->setText({});
        return;
    }
    m_sumLabel->setText(
        tr("Sum: %1   ·   Mean: %2   ·   Count: %3")
            .arg(m_current->sumOfFactors(), 0, 'f', 4)
            .arg(m_current->sumOfFactors() / std::max(1, m_current->factorCount()), 0, 'f', 4)
            .arg(m_current->factorCount()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Mutations + slots
// ─────────────────────────────────────────────────────────────────────────────

void PatternEditorDialog::onNormalizeClicked_()
{
    if (!m_current) return;
    const double target = m_normalizeTargetSpin
        ? m_normalizeTargetSpin->value() : 1.0;
    QString reason;
    if (!m_current->normalize(target, &reason)) {
        if (m_status) m_status->showMessage(reason, 4000);
    }
}

void PatternEditorDialog::invokeNormalize() { onNormalizeClicked_(); }

void PatternEditorDialog::onProviderAdded_(PatternProvider *p)
{
    if (!p) return;
    auto *item = new QStandardItem(p->name());
    item->setData(QVariant::fromValue(reinterpret_cast<quintptr>(p)),
                  Qt::UserRole + 1);
    item->setToolTip(p->name() + tr("  ·  %1").arg(typeLabel(p->type())));
    m_listModel->appendRow(item);
}

void PatternEditorDialog::onProviderRemoved_(PatternProvider *p)
{
    if (!p) return;
    for (int r = 0; r < m_listModel->rowCount(); ++r) {
        auto *item = m_listModel->item(r);
        if (!item) continue;
        const auto ptr = reinterpret_cast<PatternProvider *>(
            item->data(Qt::UserRole + 1).value<quintptr>());
        if (ptr == p) {
            m_listModel->removeRow(r);
            break;
        }
    }
    if (m_current == p) {
        bindProvider_(nullptr);
    }
}

void PatternEditorDialog::onProviderRenamed_(PatternProvider *p,
                                              const QString &, const QString &now)
{
    if (!p) return;
    for (int r = 0; r < m_listModel->rowCount(); ++r) {
        auto *item = m_listModel->item(r);
        if (!item) continue;
        const auto ptr = reinterpret_cast<PatternProvider *>(
            item->data(Qt::UserRole + 1).value<quintptr>());
        if (ptr == p) {
            item->setText(now);
            break;
        }
    }
    if (m_current == p)
        setWindowTitle(tr("Time Pattern Editor — %1").arg(now));
}

void PatternEditorDialog::onProviderFactorChanged_()
{
    refreshChart_();
    updateStatusBar_();
}

void PatternEditorDialog::onProviderFactorsChanged_()
{
    refreshChart_();
    updateStatusBar_();
}

void PatternEditorDialog::onProviderTypeChanged_()
{
    if (m_typeLabel && m_current)
        m_typeLabel->setText(typeLabel(m_current->type()));
    refreshChart_();
    updateStatusBar_();
}

void PatternEditorDialog::onMutationRejected_(const QString &reason)
{
    if (m_status) m_status->showMessage(reason, 4000);
}

// ─────────────────────────────────────────────────────────────────────────────
// List CRUD: New / Rename / Delete + context menu
// ─────────────────────────────────────────────────────────────────────────────

void PatternEditorDialog::onNewClicked_()
{
    if (!m_createCard) return;
    m_mode = Mode::CreateNew;
    if (m_nameEdit) {
        m_nameEdit->clear();
        m_nameEdit->setFocus();
    }
    m_createCard->show();
}

void PatternEditorDialog::invokeNew() { onNewClicked_(); }

void PatternEditorDialog::onCancelCreateClicked_()
{
    if (m_createCard) m_createCard->hide();
    m_mode = Mode::Edit;
    if (m_nameEdit) m_nameEdit->clear();
}

void PatternEditorDialog::onRenameClicked_()
{
    if (!m_current || !m_registry) return;
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Rename Pattern"),
        tr("New name:"), QLineEdit::Normal,
        m_current->name(), &ok).trimmed();
    if (!ok || newName.isEmpty()) return;
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename Pattern"),
            tr("Could not rename to “%1” — name already in use.").arg(newName));
    }
}

bool PatternEditorDialog::renameCurrent(const QString &newName)
{
    if (!m_current || !m_registry) return false;
    return m_registry->rename(m_current, newName.trimmed());
}

void PatternEditorDialog::onDeleteClicked_()
{
    if (!m_current || !m_registry) return;
    const QString name = m_current->name();
    const auto reply = QMessageBox::question(
        this, tr("Delete Pattern"),
        tr("Delete pattern “%1”?\n\nAny model object referencing this "
           "pattern (DWF inflows, infiltration adjustments, etc.) will "
           "lose its reference.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    deleteCurrentSilently();
}

void PatternEditorDialog::deleteCurrentSilently()
{
    if (!m_current || !m_registry) return;
    m_registry->remove(m_current);   // providerAboutToBeRemoved → onProviderRemoved_ → list refresh.
}

void PatternEditorDialog::onListContextMenu_(const QPoint &pos)
{
    if (!m_listView) return;
    const QModelIndex idx = m_listView->indexAt(pos);
    if (!idx.isValid()) return;
    // Select the right-clicked item so the slots act on it.
    m_listView->setCurrentIndex(idx);

    QMenu menu(this);
    QAction *actRename = menu.addAction(tr("Rename…"));
    QAction *actDelete = menu.addAction(tr("Delete"));
    QAction *picked = menu.exec(m_listView->viewport()->mapToGlobal(pos));
    if (picked == actRename) onRenameClicked_();
    else if (picked == actDelete) onDeleteClicked_();
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateNew mode
// ─────────────────────────────────────────────────────────────────────────────

QString PatternEditorDialog::pendingName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

int PatternEditorDialog::pendingType() const
{
    return m_typeCombo ? m_typeCombo->currentData().toInt() : 0;
}

bool PatternEditorDialog::isCreateEnabled() const
{
    return m_createBtn && m_createBtn->isEnabled();
}

void PatternEditorDialog::submitCreateNew() { onCreateNewSubmit_(); }

void PatternEditorDialog::onCreateNewNameChanged_(const QString &text)
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
            tr("A pattern named “%1” already exists.").arg(trimmed));
        m_nameValidationLabel->show();
        m_nameEdit->setStyleSheet(QStringLiteral("border: 1px solid #c0392b;"));
    } else {
        m_createBtn->setEnabled(true);
        m_nameValidationLabel->hide();
        m_nameEdit->setStyleSheet(QString());
    }
}

void PatternEditorDialog::onCreateNewSubmit_()
{
    if (!m_registry || !m_nameEdit) return;
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty() || m_registry->hasName(name)) return;
    const auto type = static_cast<PatternType>(
        m_typeCombo ? m_typeCombo->currentData().toInt() : 0);

    PatternProvider *p = m_registry->create(name, type);
    if (!p) return;

    m_mode = Mode::Edit;
    if (m_createCard) m_createCard->hide();
    setWindowTitle(tr("Time Pattern Editor — %1").arg(name));

    // providerAdded slot already appended the row; just select it.
    selectProviderInList_(p);
}

} // namespace openswmmvis::ui
