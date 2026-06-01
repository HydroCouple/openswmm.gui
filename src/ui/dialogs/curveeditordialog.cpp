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
#include "ui/widgets/curveeditchartview.h"
#include "ui/widgets/interactivechartview.h"

#include <QAction>
#include <QChart>
#include <QChartView>
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QToolBar>
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
#include <QRegularExpression>
#include <QScatterSeries>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStringList>
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
    if (m_listModel->rowCount() > 0) {
        const QModelIndex src = m_listModel->index(0, 0);
        const QModelIndex view = m_listProxy ? m_listProxy->mapFromSource(src) : src;
        if (view.isValid()) m_listView->setCurrentIndex(view);
        else bindProvider_(nullptr);
    } else {
        bindProvider_(nullptr);
    }
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

QString CurveEditorDialog::pickCurve(CurveRegistry *registry,
                                      QUndoStack    *undoStack,
                                      const QString &initialName,
                                      QWidget       *parent)
{
    if (!registry) return {};

    // Stack-allocated so we can exec() modally without WA_DeleteOnClose.
    // Mirrors PatternEditorDialog::pickPattern and
    // TimeseriesEditorDialog::pickTimeseries.
    CurveEditorDialog dlg(registry, undoStack, parent);
    dlg.setModal(true);
    if (initialName.isEmpty()) {
        // CreateNew — reveal the create-card without instantiating a
        // second dialog (we already own one on the stack).
        dlg.m_mode = Mode::CreateNew;
        if (dlg.m_createCard) dlg.m_createCard->show();
        if (dlg.m_nameEdit)   dlg.m_nameEdit->setFocus();
        dlg.setWindowTitle(tr("New Curve"));
    } else {
        dlg.setWindowTitle(tr("Edit Curve"));
        if (auto *p = registry->findByName(initialName))
            dlg.selectProviderInList_(p);
    }
    dlg.exec();

    // Return whatever's currently highlighted — applies to all exit paths
    // (Apply, OK, Close). All edits are already persisted through the
    // registry MVC layer regardless of which button was used.
    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
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

        // Type filter combo above the list — "All types" + one entry per
        // CurveType. Routes through a QSortFilterProxyModel on the user
        // data role so the filter survives list rebuilds.
        auto *filterRow = new QHBoxLayout();
        filterRow->addWidget(new QLabel(tr("Filter:"), host));
        m_filterCombo = new QComboBox(host);
        m_filterCombo->addItem(tr("All types"), int(-1));
        for (CurveType t : kAllTypes)
            m_filterCombo->addItem(CurveProvider::typeLabel(t), int(t));
        filterRow->addWidget(m_filterCombo, 1);
        lay->addLayout(filterRow);

        m_listView = new QListView(host);
        // Inline rename: click-and-wait on selected row or F2 enters edit
        // mode; itemChanged routes the new text through the registry.
        m_listView->setEditTriggers(QAbstractItemView::SelectedClicked
                                     | QAbstractItemView::EditKeyPressed);
        m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_listView->setUniformItemSizes(true);
        m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
        m_listModel = new QStandardItemModel(this);
        // Use a proxy so the type-filter combo can hide rows whose curve
        // type doesn't match. The proxy filters on the curve-type role
        // (Qt::UserRole + 2) populated in rebuildListModel_.
        m_listProxy = new QSortFilterProxyModel(this);
        m_listProxy->setSourceModel(m_listModel);
        m_listProxy->setFilterRole(Qt::UserRole + 2);
        m_listView->setModel(m_listProxy);
        connect(m_filterCombo, &QComboBox::currentIndexChanged, this,
                [this](int) { applyTypeFilter_(); });
        lay->addWidget(m_listView, 1);

        connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex &, const QModelIndex &) {
                    onListSelectionChanged_();
                });
        connect(m_listView, &QListView::customContextMenuRequested,
                this, &CurveEditorDialog::onListContextMenu_);
        connect(m_listModel, &QStandardItemModel::itemChanged,
                this, &CurveEditorDialog::onListItemRenamed_);

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
        m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_table->setAlternatingRowColors(true);
        m_table->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_table, &QWidget::customContextMenuRequested,
                this, &CurveEditorDialog::onTableContextMenu_);
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

        // Copy / Paste — hidden actions on the dialog so Ctrl/Cmd+C and
        // Ctrl/Cmd+V fire from either the table or chart. Surfaced as menu
        // items via onTableContextMenu_.
        m_copyAct = new QAction(tr("Copy"), this);
        m_copyAct->setShortcut(QKeySequence::Copy);
        m_copyAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(m_copyAct, &QAction::triggered,
                this, &CurveEditorDialog::onCopyRowsClicked_);
        addAction(m_copyAct);

        m_pasteAct = new QAction(tr("Paste"), this);
        m_pasteAct->setShortcut(QKeySequence::Paste);
        m_pasteAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(m_pasteAct, &QAction::triggered,
                this, &CurveEditorDialog::onPasteRowsClicked_);
        addAction(m_pasteAct);

        m_splitter->addWidget(host);
    }

    // ── Right pane: toolbar + line/scatter chart preview ────────────────────
    {
        auto *host = new QWidget(m_splitter);
        auto *lay  = new QVBoxLayout(host);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        // Standard pan/zoom toolbar mirroring TransectEditor's right pane.
        m_chartToolBar = new QToolBar(host);
        m_chartToolBar->setIconSize(QSize(20, 20));
        m_chartToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        lay->addWidget(m_chartToolBar);

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

        // CurveEditChartView extends InteractiveChartView with vertex-drag
        // editing. The chart, axes, and line/scatter series are owned by
        // this dialog; the chart view adds an internal selection-highlight
        // overlay scatter and routes mouse events for hit-testing, drag,
        // and rubber-band selection. All mutations flow through the bound
        // CurveProvider (MVC).
        m_chartView = new CurveEditChartView(m_chart, m_line, host);
        m_chartView->setRenderHint(QPainter::Antialiasing, true);
        m_chartView->setUndoStack(m_undoStack);
        lay->addWidget(m_chartView, 1);

        // Build toolbar actions.
        auto *aFit = m_chartToolBar->addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                                 tr("Fit"));
        aFit->setToolTip(tr("Zoom to extent (F)"));
        aFit->setShortcut(QKeySequence(Qt::Key_F));
        connect(aFit, &QAction::triggered, this,
                [this]() { if (m_chartView) m_chartView->resetZoom(); });

        auto *aZIn = m_chartToolBar->addAction(QIcon(QStringLiteral(":/swmmvis/ZoomIn")),
                                                 tr("Zoom in"));
        aZIn->setCheckable(true);
        aZIn->setToolTip(tr("Click-zoom in (Ctrl++)"));
        connect(aZIn, &QAction::toggled, this, [this, aZIn](bool on) {
            if (!m_chartView) return;
            m_chartView->setMode(on ? InteractiveChartView::Mode::ZoomIn
                                     : InteractiveChartView::Mode::Select);
            if (on && m_chartPanAction) {
                const QSignalBlocker b(m_chartPanAction);
                m_chartPanAction->setChecked(false);
            }
            if (on && m_chartZoomOutAction) {
                const QSignalBlocker b(m_chartZoomOutAction);
                m_chartZoomOutAction->setChecked(false);
            }
            Q_UNUSED(aZIn);
        });
        m_chartZoomInAction = aZIn;

        auto *aZOut = m_chartToolBar->addAction(QIcon(QStringLiteral(":/swmmvis/ZoomOut")),
                                                  tr("Zoom out"));
        aZOut->setCheckable(true);
        aZOut->setToolTip(tr("Click-zoom out (Ctrl+-)"));
        connect(aZOut, &QAction::toggled, this, [this](bool on) {
            if (!m_chartView) return;
            m_chartView->setMode(on ? InteractiveChartView::Mode::ZoomOut
                                     : InteractiveChartView::Mode::Select);
            if (on && m_chartPanAction) {
                const QSignalBlocker b(m_chartPanAction);
                m_chartPanAction->setChecked(false);
            }
            if (on && m_chartZoomInAction) {
                const QSignalBlocker b(m_chartZoomInAction);
                m_chartZoomInAction->setChecked(false);
            }
        });
        m_chartZoomOutAction = aZOut;

        m_chartToolBar->addSeparator();

        auto *aPan = m_chartToolBar->addAction(QIcon(QStringLiteral(":/swmmvis/Move")),
                                                 tr("Pan"));
        aPan->setCheckable(true);
        aPan->setToolTip(tr("Left-drag pans the chart"));
        connect(aPan, &QAction::toggled, this, [this](bool on) {
            if (!m_chartView) return;
            m_chartView->setMode(on ? InteractiveChartView::Mode::Pan
                                     : InteractiveChartView::Mode::Select);
            if (on && m_chartZoomInAction) {
                const QSignalBlocker b(m_chartZoomInAction);
                m_chartZoomInAction->setChecked(false);
            }
            if (on && m_chartZoomOutAction) {
                const QSignalBlocker b(m_chartZoomOutAction);
                m_chartZoomOutAction->setChecked(false);
            }
        });
        m_chartPanAction = aPan;

        // ── Edit / axis-lock toolbar (mirrors transect/timeseries editors) ──
        m_chartToolBar->addSeparator();

        auto *aEdit = m_chartToolBar->addAction(QIcon(QStringLiteral(":/swmmvis/Edit")),
                                                 tr("Edit points"));
        aEdit->setCheckable(true);
        aEdit->setToolTip(tr("Drag vertices to edit (E)"));
        aEdit->setShortcut(QKeySequence(Qt::Key_E));
        connect(aEdit, &QAction::toggled, this, [this](bool on) {
            if (!m_chartView) return;
            m_chartView->setEditMode(on ? CurveEditChartView::EditMode::EditPoints
                                         : CurveEditChartView::EditMode::None);
            // Editing is mutually exclusive with Pan/Zoom modes.
            if (on) {
                m_chartView->setMode(InteractiveChartView::Mode::Select);
                if (m_chartPanAction) {
                    const QSignalBlocker b(m_chartPanAction);
                    m_chartPanAction->setChecked(false);
                }
                if (m_chartZoomInAction) {
                    const QSignalBlocker b(m_chartZoomInAction);
                    m_chartZoomInAction->setChecked(false);
                }
                if (m_chartZoomOutAction) {
                    const QSignalBlocker b(m_chartZoomOutAction);
                    m_chartZoomOutAction->setChecked(false);
                }
            }
            if (m_chartLockXAction) m_chartLockXAction->setEnabled(on);
            if (m_chartLockYAction) m_chartLockYAction->setEnabled(on);
        });
        m_chartEditAction = aEdit;

        auto *aLockX = m_chartToolBar->addAction(tr("Lock X"));
        aLockX->setCheckable(true);
        aLockX->setToolTip(tr("Constrain vertex drag to Y only"));
        aLockX->setEnabled(false);
        connect(aLockX, &QAction::toggled, this, [this](bool on) {
            if (m_chartView) m_chartView->setLockX(on);
        });
        m_chartLockXAction = aLockX;

        auto *aLockY = m_chartToolBar->addAction(tr("Lock Y"));
        aLockY->setCheckable(true);
        aLockY->setToolTip(tr("Constrain vertex drag to X only"));
        aLockY->setEnabled(false);
        connect(aLockY, &QAction::toggled, this, [this](bool on) {
            if (m_chartView) m_chartView->setLockY(on);
        });
        m_chartLockYAction = aLockY;

        // MVC chart<->table selection sync. Dialog mediates between the two
        // views (selection state is not part of the provider model).
        connect(m_chartView, &CurveEditChartView::selectionChanged, this,
                [this](const QVector<int> &indices) {
                    if (m_suppressTableSelectionSync) return;
                    if (!m_table || !m_table->selectionModel() || !m_tableModel)
                        return;
                    m_suppressChartSelectionSync = true;
                    auto *sel = m_table->selectionModel();
                    sel->clearSelection();
                    for (int row : indices) {
                        if (row < 0 || row >= m_tableModel->rowCount()) continue;
                        const QModelIndex tl = m_tableModel->index(row, 0);
                        const QModelIndex br = m_tableModel->index(
                            row, m_tableModel->columnCount() - 1);
                        sel->select(QItemSelection(tl, br),
                                    QItemSelectionModel::Select);
                    }
                    if (!indices.isEmpty())
                        m_table->scrollTo(m_tableModel->index(indices.first(), 0),
                                           QAbstractItemView::EnsureVisible);
                    m_suppressChartSelectionSync = false;
                });

        m_splitter->addWidget(host);
    }

    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 3);

    m_status = new QStatusBar(this);
    m_countLabel = new QLabel(m_status);
    m_status->addPermanentWidget(m_countLabel);
    outer->addWidget(m_status);

    // Table->chart selection sync (other direction wired in the right pane
    // when the chart view is constructed). Needs both widgets to be alive,
    // hence wired here at the end of buildUi_.
    if (m_table && m_table->selectionModel() && m_chartView) {
        connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [this](const QItemSelection &, const QItemSelection &) {
                    if (m_suppressChartSelectionSync) return;
                    if (!m_chartView || !m_table->selectionModel()) return;
                    QVector<int> rows;
                    for (const auto &idx : m_table->selectionModel()->selectedRows())
                        rows.push_back(idx.row());
                    std::sort(rows.begin(), rows.end());
                    m_suppressTableSelectionSync = true;
                    m_chartView->setSelection(rows);
                    m_suppressTableSelectionSync = false;
                });
    }
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
        // Stash the curve type so the filter proxy can match on it.
        item->setData(int(p->type()), Qt::UserRole + 2);
        item->setToolTip(p->name() + tr("  ·  %1")
                                       .arg(CurveProvider::typeLabel(p->type())));
        m_listModel->appendRow(item);
    }
    applyTypeFilter_();
}

void CurveEditorDialog::applyTypeFilter_()
{
    if (!m_listProxy || !m_filterCombo) return;
    const int data = m_filterCombo->currentData().toInt();
    if (data < 0) {
        m_listProxy->setFilterRegularExpression(QRegularExpression{});
    } else {
        // Exact match on the curve-type integer stored in UserRole+2.
        m_listProxy->setFilterRegularExpression(
            QRegularExpression(QStringLiteral("^%1$").arg(data)));
    }
}

void CurveEditorDialog::selectProviderInList_(CurveProvider *p)
{
    if (!p) return;
    for (int r = 0; r < m_listModel->rowCount(); ++r) {
        const auto src = m_listModel->index(r, 0);
        auto *item = m_listModel->itemFromIndex(src);
        if (!item) continue;
        if (reinterpret_cast<CurveProvider *>(
                item->data(Qt::UserRole + 1).value<quintptr>()) == p) {
            const QModelIndex view = m_listProxy
                ? m_listProxy->mapFromSource(src) : src;
            if (view.isValid()) m_listView->setCurrentIndex(view);
            return;
        }
    }
}

void CurveEditorDialog::onListSelectionChanged_()
{
    const QModelIndex view = m_listView->currentIndex();
    const QModelIndex src = (m_listProxy && view.isValid())
        ? m_listProxy->mapToSource(view) : view;
    CurveProvider *p = nullptr;
    if (src.isValid()) {
        auto *item = m_listModel->itemFromIndex(src);
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
    if (m_chartView)  m_chartView->setProvider(p);

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
    const int n = m_current->pointCount();

    // If a row is selected, the new point lands immediately after it (between
    // selected and next, or appended at end when the last row is selected).
    // With no selection the new point is appended at the end.
    int selectedRow = -1;
    if (m_table && m_table->selectionModel()) {
        for (const auto &idx : m_table->selectionModel()->selectedRows())
            selectedRow = std::max(selectedRow, idx.row());
        if (selectedRow < 0) {
            const QModelIndex cur = m_table->selectionModel()->currentIndex();
            if (cur.isValid()) selectedRow = cur.row();
        }
    }

    double newX = 0.0, newY = 0.0;
    if (n == 0) {
        newX = 0.0; newY = 0.0;
    } else if (selectedRow < 0 || selectedRow >= n - 1) {
        // No selection, or last row selected → append after the last point.
        const auto &last = m_current->pointAt(n - 1);
        newX = last.x + 1.0;
        newY = last.y;
    } else {
        // Insert between selectedRow and the row after it; midpoint preserves
        // the strict-ascending-X invariant by construction.
        const auto &a = m_current->pointAt(selectedRow);
        const auto &b = m_current->pointAt(selectedRow + 1);
        newX = 0.5 * (a.x + b.x);
        newY = 0.5 * (a.y + b.y);
    }

    QString reason;
    const int newRow = m_current->insertPoint(newX, newY, &reason);
    if (newRow < 0) {
        if (m_status) m_status->showMessage(
            tr("Add row rejected: %1").arg(reason), 4000);
        return;
    }
    if (m_table && m_tableModel) {
        const QModelIndex mi = m_tableModel->index(newRow, 0);
        if (mi.isValid()) {
            m_table->selectRow(newRow);
            m_table->scrollTo(mi);
        }
    }
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

void CurveEditorDialog::onCopyRowsClicked_()
{
    if (!m_current) return;
    QVector<int> rows;
    if (m_table && m_table->selectionModel()) {
        for (const auto &idx : m_table->selectionModel()->selectedRows())
            rows.push_back(idx.row());
    }
    if (rows.isEmpty()) {
        for (int i = 0; i < m_current->pointCount(); ++i) rows.push_back(i);
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    QStringList lines;
    lines.reserve(rows.size());
    for (int r : rows) {
        if (r < 0 || r >= m_current->pointCount()) continue;
        const auto &p = m_current->pointAt(r);
        lines << QStringLiteral("%1\t%2")
                     .arg(QString::number(p.x, 'g', 15),
                          QString::number(p.y, 'g', 15));
    }
    QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    if (m_status) m_status->showMessage(
        tr("Copied %1 row(s) to clipboard.").arg(rows.size()), 2500);
}

void CurveEditorDialog::onPasteRowsClicked_()
{
    if (!m_current) return;
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
        if (m_status) m_status->showMessage(tr("Clipboard is empty."), 2500);
        return;
    }

    // Accept tab / comma / whitespace as delimiters so Excel TSV, CSV exports
    // and plain " x y" tables all round-trip.
    const QStringList rawLines = text.split(
        QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    QVector<openswmmvis::curve::CurvePoint> parsed;
    parsed.reserve(rawLines.size());
    int skipped = 0;
    for (const QString &line : rawLines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith(';'))
            continue;
        const QStringList cells = trimmed.split(
            QRegularExpression(QStringLiteral("[\\s,\\t]+")), Qt::SkipEmptyParts);
        if (cells.size() < 2) { ++skipped; continue; }
        bool okX = false, okY = false;
        const double x = cells.at(0).toDouble(&okX);
        const double y = cells.at(1).toDouble(&okY);
        if (!okX || !okY) { ++skipped; continue; }
        parsed.push_back({x, y});
    }
    if (parsed.isEmpty()) {
        if (m_status) m_status->showMessage(
            tr("No valid X,Y rows in clipboard."), 3000);
        return;
    }

    // TSV multi-row replace: with a selected row, parsed rows replace existing
    // ones starting at the lowest selected index; tail rows past the pasted
    // block are kept. With no selection, parsed rows are appended.
    int startRow = -1;
    if (m_table && m_table->selectionModel()) {
        for (const auto &idx : m_table->selectionModel()->selectedRows()) {
            if (startRow < 0 || idx.row() < startRow) startRow = idx.row();
        }
    }

    const auto curr = m_current->points();
    QVector<openswmmvis::curve::CurvePoint> newPts;
    newPts.reserve(std::max(curr.size(), parsed.size()));
    if (startRow < 0) {
        newPts = curr;
        for (const auto &p : parsed) newPts.push_back(p);
    } else {
        for (int i = 0; i < startRow && i < curr.size(); ++i)
            newPts.push_back(curr.at(i));
        for (const auto &p : parsed) newPts.push_back(p);
        const int tailStart = startRow + parsed.size();
        for (int i = tailStart; i < curr.size(); ++i)
            newPts.push_back(curr.at(i));
    }

    // Provider requires strictly ascending X. Sort first so an out-of-order
    // paste still has a chance; duplicate-X collisions remain a rejection.
    std::sort(newPts.begin(), newPts.end(),
              [](const openswmmvis::curve::CurvePoint &a,
                 const openswmmvis::curve::CurvePoint &b) { return a.x < b.x; });

    QString reason;
    if (!m_current->setAllPoints(newPts, &reason)) {
        if (m_status) m_status->showMessage(
            tr("Paste rejected: %1").arg(reason), 4000);
        return;
    }
    if (m_status) {
        QString msg = tr("Pasted %1 row(s).").arg(parsed.size());
        if (skipped > 0) msg += tr(" %1 line(s) skipped.").arg(skipped);
        m_status->showMessage(msg, 3000);
    }
}

void CurveEditorDialog::onTableContextMenu_(const QPoint &pos)
{
    if (!m_table || !m_current) return;

    const QModelIndex idx = m_table->indexAt(pos);
    const bool hasRowAtCursor = idx.isValid();

    QMenu menu(this);
    QAction *actAdd   = menu.addAction(tr("Add Row"));
    QAction *actDel   = menu.addAction(tr("Delete Row(s)"));
    actDel->setEnabled(hasRowAtCursor
        || (m_table->selectionModel()
            && !m_table->selectionModel()->selectedRows().isEmpty()));
    menu.addSeparator();
    QAction *actCopy  = menu.addAction(tr("Copy"));
    actCopy->setShortcut(QKeySequence::Copy);
    QAction *actPaste = menu.addAction(tr("Paste"));
    actPaste->setShortcut(QKeySequence::Paste);

    QAction *picked = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (picked == actAdd)        onAddRowClicked_();
    else if (picked == actDel)   onDeleteRowsClicked_();
    else if (picked == actCopy)  onCopyRowsClicked_();
    else if (picked == actPaste) onPasteRowsClicked_();
}

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
    item->setData(int(p->type()), Qt::UserRole + 2);
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
            m_suppressListItemRename = true;
            item->setText(now);
            m_suppressListItemRename = false;
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
    // Keep the filter-role role in sync so the type filter combo keeps
    // the row visible (or hides it) after the type changes.
    if (m_current) {
        for (int r = 0; r < m_listModel->rowCount(); ++r) {
            auto *item = m_listModel->item(r);
            if (!item) continue;
            if (reinterpret_cast<CurveProvider *>(
                    item->data(Qt::UserRole + 1).value<quintptr>()) == m_current) {
                m_suppressListItemRename = true;
                item->setData(int(m_current->type()), Qt::UserRole + 2);
                m_suppressListItemRename = false;
                break;
            }
        }
    }
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
    // No intermediate dialog — start inline edit on the current list row.
    // The itemChanged handler routes the new text through the registry.
    if (!m_listView) return;
    const QModelIndex idx = m_listView->currentIndex();
    if (!idx.isValid()) return;
    m_listView->edit(idx);
}

void CurveEditorDialog::onListItemRenamed_(QStandardItem *item)
{
    if (!item || !m_registry || m_suppressListItemRename) return;
    auto *p = reinterpret_cast<CurveProvider *>(
        item->data(Qt::UserRole + 1).value<quintptr>());
    if (!p) return;
    const QString newName = item->text().trimmed();
    if (newName.isEmpty() || newName == p->name()) {
        // Revert any stray whitespace edits.
        m_suppressListItemRename = true;
        item->setText(p->name());
        m_suppressListItemRename = false;
        return;
    }
    if (!m_registry->rename(p, newName)) {
        // Revert and notify; do not pop a modal — show inline status.
        m_suppressListItemRename = true;
        item->setText(p->name());
        m_suppressListItemRename = false;
        if (m_status)
            m_status->showMessage(
                tr("A curve named “%1” already exists.").arg(newName), 4000);
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
