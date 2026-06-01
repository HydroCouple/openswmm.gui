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
#include "ui/widgets/interactivechartview.h"
#include "ui/widgets/patterneditchartview.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCategoryAxis>
#include <QChart>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QInputDialog>
#include <QLabel>
#include <QLegend>
#include <QLineEdit>
#include <QLineSeries>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScatterSeries>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStringList>
#include <QSvgGenerator>
#include <QTableView>
#include <QToolButton>
#include <QUndoStack>
#include <QValueAxis>
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
    // Pre-select the first pattern if any exist (use the proxy index so the
    // selection model receives the same index type currentIndex() returns).
    if (m_listProxy && m_listProxy->rowCount() > 0) {
        m_listView->setCurrentIndex(m_listProxy->index(0, 0));
    } else if (m_listModel->rowCount() > 0) {
        m_listView->setCurrentIndex(m_listModel->index(0, 0));
    } else {
        bindProvider_(nullptr);
    }

    restoreDialogSettings_();
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

QString PatternEditorDialog::pickPattern(PatternRegistry *registry,
                                          QUndoStack      *undoStack,
                                          const QString   &initialName,
                                          QWidget         *parent)
{
    if (!registry) return {};

    PatternEditorDialog dlg(registry, undoStack, parent);
    dlg.setModal(true);
    if (initialName.isEmpty()) {
        // CreateNew — mirror createNew()'s setup without WA_DeleteOnClose.
        dlg.m_mode = Mode::CreateNew;
        if (dlg.m_createCard) dlg.m_createCard->show();
        if (dlg.m_nameEdit)   dlg.m_nameEdit->setFocus();
        dlg.setWindowTitle(tr("New Time Pattern"));
    } else {
        dlg.setWindowTitle(tr("Edit Time Pattern"));
        if (auto *p = registry->findByName(initialName))
            dlg.selectProviderInList_(p);
    }
    dlg.exec();

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
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

    // ── Left pane: patterns list + search + CRUD buttons ────────────────────
    {
        auto *leftHost = new QWidget(m_splitter);
        auto *leftLay  = new QVBoxLayout(leftHost);
        leftLay->setContentsMargins(8, 8, 8, 8);
        leftLay->addWidget(new QLabel(tr("Patterns"), leftHost));

        m_searchEdit = new QLineEdit(leftHost);
        m_searchEdit->setPlaceholderText(tr("Search patterns…"));
        m_searchEdit->setClearButtonEnabled(true);
        m_searchEdit->setToolTip(tr("Filter the list by case-insensitive name substring."));
        leftLay->addWidget(m_searchEdit);

        m_listModel = new QStandardItemModel(this);
        m_listProxy = new QSortFilterProxyModel(this);
        m_listProxy->setSourceModel(m_listModel);
        m_listProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        m_listProxy->setFilterKeyColumn(0);

        m_listView = new QListView(leftHost);
        // Inline rename via SelectedClicked / F2; itemChanged routes through
        // the registry (replaces the old QInputDialog rename prompt).
        m_listView->setEditTriggers(QAbstractItemView::SelectedClicked
                                     | QAbstractItemView::EditKeyPressed);
        m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_listView->setUniformItemSizes(true);
        m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
        m_listView->setModel(m_listProxy);
        leftLay->addWidget(m_listView, /*stretch=*/1);

        connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex &, const QModelIndex &) {
                    onListSelectionChanged_();
                });
        connect(m_listView, &QListView::customContextMenuRequested,
                this, &PatternEditorDialog::onListContextMenu_);
        connect(m_searchEdit, &QLineEdit::textChanged,
                this, &PatternEditorDialog::onSearchTextChanged_);
        connect(m_listModel, &QStandardItemModel::itemChanged,
                this, &PatternEditorDialog::onListItemRenamed_);

        // CRUD buttons row.
        auto *crudRow = new QHBoxLayout();
        m_newBtn       = new QPushButton(tr("+ New"), leftHost);
        m_duplicateBtn = new QPushButton(tr("Duplicate"), leftHost);
        m_renameBtn    = new QPushButton(tr("Rename"), leftHost);
        m_deleteBtn    = new QPushButton(tr("Delete"), leftHost);
        m_newBtn->setToolTip(tr("Create a new time pattern."));
        m_duplicateBtn->setToolTip(tr("Clone the selected pattern under a new name."));
        m_renameBtn->setToolTip(tr("Rename the selected pattern."));
        m_deleteBtn->setToolTip(tr("Delete the selected pattern."));
        connect(m_newBtn,       &QPushButton::clicked,
                this, &PatternEditorDialog::onNewClicked_);
        connect(m_duplicateBtn, &QPushButton::clicked,
                this, &PatternEditorDialog::onDuplicateClicked_);
        connect(m_renameBtn,    &QPushButton::clicked,
                this, &PatternEditorDialog::onRenameClicked_);
        connect(m_deleteBtn,    &QPushButton::clicked,
                this, &PatternEditorDialog::onDeleteClicked_);
        crudRow->addWidget(m_newBtn);
        crudRow->addWidget(m_duplicateBtn);
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

    // ── Right pane: step-line preview + plot toolbar ────────────────────────
    {
        auto *rightHost = new QWidget(m_splitter);
        auto *rightLay  = new QVBoxLayout(rightHost);
        rightLay->setContentsMargins(0, 0, 0, 0);
        rightLay->setSpacing(2);

        m_chart = new QChart();
        m_chart->setBackgroundRoundness(0);
        m_chart->legend()->hide();
        m_chart->setMargins(QMargins(8, 8, 8, 8));

        m_lineSeries = new QLineSeries(m_chart);
        m_lineSeries->setName(tr("Factor"));
        m_chart->addSeries(m_lineSeries);

        m_scatterSeries = new QScatterSeries(m_chart);
        m_scatterSeries->setName(tr("Slot"));
        m_scatterSeries->setMarkerSize(7.0);
        m_chart->addSeries(m_scatterSeries);

        m_xAxis = new QCategoryAxis(m_chart);
        m_xAxis->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
        m_chart->addAxis(m_xAxis, Qt::AlignBottom);
        m_lineSeries->attachAxis(m_xAxis);
        m_scatterSeries->attachAxis(m_xAxis);

        m_yAxis = new QValueAxis(m_chart);
        m_yAxis->setLabelFormat(QStringLiteral("%.2f"));
        m_yAxis->setTitleText(tr("Factor"));
        m_chart->addAxis(m_yAxis, Qt::AlignLeft);
        m_lineSeries->attachAxis(m_yAxis);
        m_scatterSeries->attachAxis(m_yAxis);

        // PatternEditChartView extends InteractiveChartView with vertex-drag
        // editing (vertical = factor edit; horizontal = adjacent-slot swap).
        // The chart, axes, and line/scatter series are owned by the dialog;
        // the chart view adds a selection-highlight overlay and routes mouse
        // events. All mutations flow through the bound PatternProvider (MVC).
        m_chartView = new PatternEditChartView(m_chart, m_lineSeries, rightHost);
        m_chartView->setRenderHint(QPainter::Antialiasing, true);
        m_chartView->setMinimumHeight(200);
        m_chartView->setUndoStack(m_undoStack);

        // Plot toolbar — same affordance set as the Unit Hydrograph editor's
        // preview pane (zoom in/out, pan, extent, copy, export, style).
        auto *plotTools = new QHBoxLayout;
        plotTools->setContentsMargins(2, 2, 2, 0);
        plotTools->setSpacing(2);

        auto makePlotBtn = [rightHost](const QString &iconPath, const QString &tip) {
            auto *b = new QToolButton(rightHost);
            b->setIcon(QIcon(iconPath));
            b->setToolTip(tip);
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
            b->setAutoRaise(true);
            b->setIconSize({18, 18});
            return b;
        };
        auto *selectBtn = makePlotBtn(QStringLiteral(":/swmmvis/Select"),
                                       tr("Select / hover mode"));
        selectBtn->setCheckable(true);
        selectBtn->setChecked(true);
        auto *panBtn    = makePlotBtn(QStringLiteral(":/swmmvis/Move"),
                                       tr("Pan mode (drag the chart)"));
        panBtn->setCheckable(true);
        auto *zoomInBtn = makePlotBtn(QStringLiteral(":/swmmvis/ZoomIn"),
                                       tr("Zoom in (also: scroll wheel up)"));
        zoomInBtn->setCheckable(true);
        auto *zoomOutBtn = makePlotBtn(QStringLiteral(":/swmmvis/ZoomOut"),
                                        tr("Zoom out (also: scroll wheel down)"));
        zoomOutBtn->setCheckable(true);

        // Single-selection mode group so only one tool is active at a time.
        auto *modeGroup = new QButtonGroup(this);
        modeGroup->setExclusive(true);
        modeGroup->addButton(selectBtn);
        modeGroup->addButton(panBtn);
        modeGroup->addButton(zoomInBtn);
        modeGroup->addButton(zoomOutBtn);

        auto *extentBtn = makePlotBtn(QStringLiteral(":/swmmvis/Extent"),
                                       tr("Zoom to extent — reset to full range"));
        auto *copyBtn   = makePlotBtn(QStringLiteral(":/swmmvis/Copy"),
                                       tr("Copy chart to clipboard (PNG)"));
        auto *exportBtn = makePlotBtn(QStringLiteral(":/swmmvis/SaveAs"),
                                       tr("Export chart… (PNG or SVG)"));
        auto *styleBtn  = makePlotBtn(QStringLiteral(":/swmmvis/Style"),
                                       tr("Plot style — markers and step/smooth toggle"));

        using Mode = InteractiveChartView::Mode;
        connect(selectBtn,  &QToolButton::clicked, this, [this]{
            if (m_chartView) m_chartView->setMode(Mode::Select);
        });
        connect(panBtn,     &QToolButton::clicked, this, [this]{
            if (m_chartView) m_chartView->setMode(Mode::Pan);
        });
        connect(zoomInBtn,  &QToolButton::clicked, this, [this]{
            if (m_chartView) m_chartView->setMode(Mode::ZoomIn);
        });
        connect(zoomOutBtn, &QToolButton::clicked, this, [this]{
            if (m_chartView) m_chartView->setMode(Mode::ZoomOut);
        });
        // Reflect mode changes driven from the chart itself (right-click,
        // wheel, keyboard) into the toolbar buttons.
        connect(m_chartView, &InteractiveChartView::modeChanged, this,
                [selectBtn, panBtn, zoomInBtn, zoomOutBtn](Mode m) {
                    QToolButton *target = nullptr;
                    switch (m) {
                    case Mode::Select:  target = selectBtn;  break;
                    case Mode::Pan:     target = panBtn;     break;
                    case Mode::ZoomIn:  target = zoomInBtn;  break;
                    case Mode::ZoomOut: target = zoomOutBtn; break;
                    }
                    if (target) target->setChecked(true);
                });
        connect(extentBtn, &QToolButton::clicked, this, [this]{
            if (m_chartView) m_chartView->resetZoom();
            refreshChart_();
        });
        connect(copyBtn,   &QToolButton::clicked,
                this, &PatternEditorDialog::onCopyChartClicked_);
        connect(exportBtn, &QToolButton::clicked,
                this, &PatternEditorDialog::onExportChartClicked_);
        connect(styleBtn,  &QToolButton::clicked, this, [this, styleBtn]{
            onShowPlotStyleMenu_(
                styleBtn->mapToGlobal(QPoint(0, styleBtn->height())));
        });
        connect(m_chartView, &InteractiveChartView::chartContextMenuRequested,
                this, &PatternEditorDialog::onShowPlotStyleMenu_);

        // Edit-points toggle — vertical drag edits the factor; horizontal
        // drag swaps adjacent slots. Mutually exclusive with Pan/Zoom modes.
        auto *editBtn = makePlotBtn(QStringLiteral(":/swmmvis/Edit"),
                                     tr("Edit vertices — vertical drag = factor; horizontal drag = swap slots"));
        editBtn->setCheckable(true);
        connect(editBtn, &QToolButton::toggled, this,
                [this, selectBtn](bool on) {
                    if (!m_chartView) return;
                    m_chartView->setEditMode(on
                        ? PatternEditChartView::EditMode::EditPoints
                        : PatternEditChartView::EditMode::None);
                    if (on) {
                        // Park the base mode at Select so Pan/Zoom don't
                        // compete with vertex picking. selectBtn is in the
                        // mode group; setChecked auto-unchecks Pan/ZoomIn/Out.
                        m_chartView->setMode(InteractiveChartView::Mode::Select);
                        selectBtn->setChecked(true);
                    }
                });

        plotTools->addWidget(selectBtn);
        plotTools->addWidget(panBtn);
        plotTools->addWidget(zoomInBtn);
        plotTools->addWidget(zoomOutBtn);
        plotTools->addSpacing(8);
        plotTools->addWidget(editBtn);
        plotTools->addSpacing(8);
        plotTools->addWidget(extentBtn);
        plotTools->addSpacing(8);
        plotTools->addWidget(copyBtn);
        plotTools->addWidget(exportBtn);
        plotTools->addSpacing(8);
        plotTools->addWidget(styleBtn);
        plotTools->addStretch(1);
        rightLay->addLayout(plotTools);
        rightLay->addWidget(m_chartView, /*stretch=*/1);

        // MVC chart->table selection sync. Dialog mediates between the two
        // views (selection state is not part of the provider model).
        connect(m_chartView, &PatternEditChartView::selectionChanged, this,
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

        m_splitter->addWidget(rightHost);
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

    // Table->chart selection sync. Wired here so both widgets are alive.
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
        const auto srcIdx = m_listModel->index(r, 0);
        auto *item = m_listModel->itemFromIndex(srcIdx);
        if (!item) continue;
        const auto ptr = reinterpret_cast<PatternProvider *>(
            item->data(Qt::UserRole + 1).value<quintptr>());
        if (ptr == p) {
            const QModelIndex proxyIdx = m_listProxy
                ? m_listProxy->mapFromSource(srcIdx) : srcIdx;
            if (proxyIdx.isValid())
                m_listView->setCurrentIndex(proxyIdx);
            return;
        }
    }
}

void PatternEditorDialog::onListSelectionChanged_()
{
    const QModelIndex proxyIdx = m_listView->currentIndex();
    const QModelIndex srcIdx = (m_listProxy && proxyIdx.isValid())
        ? m_listProxy->mapToSource(proxyIdx) : proxyIdx;
    PatternProvider *p = nullptr;
    if (srcIdx.isValid()) {
        auto *item = m_listModel->itemFromIndex(srcIdx);
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
    if (m_chartView)  m_chartView->setProvider(p);

    if (m_typeLabel) {
        m_typeLabel->setText(p ? typeLabel(p->type()) : tr("(no pattern selected)"));
    }
    if (m_normalizeBtn)         m_normalizeBtn->setEnabled(p != nullptr);
    if (m_normalizeTargetSpin)  m_normalizeTargetSpin->setEnabled(p != nullptr);
    if (m_renameBtn)            m_renameBtn->setEnabled(p != nullptr);
    if (m_duplicateBtn)         m_duplicateBtn->setEnabled(p != nullptr);
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
    if (!m_lineSeries || !m_scatterSeries || !m_xAxis || !m_yAxis) return;

    m_lineSeries->clear();
    m_scatterSeries->clear();
    // QCategoryAxis has no clear(); rebuild by removing each existing label.
    const QStringList prevLabels = m_xAxis->categoriesLabels();
    for (const QString &lbl : prevLabels) m_xAxis->remove(lbl);

    if (!m_current || m_current->factorCount() == 0) {
        m_xAxis->setRange(0.0, 1.0);
        m_yAxis->setRange(0.0, 1.0);
        return;
    }

    const int n = m_current->factorCount();
    double maxV = 0.0;

    if (m_smoothPreview) {
        // Smooth mode: one point per slot at the slot centre.
        for (int i = 0; i < n; ++i) {
            const double v = m_current->factor(i);
            m_lineSeries->append(i + 0.5, v);
            maxV = std::max(maxV, v);
        }
    } else {
        // Step-line mode: emit (i, f_i) and (i+1, f_i) so each slot draws as
        // a unit-wide horizontal segment. 2·N points total.
        for (int i = 0; i < n; ++i) {
            const double v = m_current->factor(i);
            m_lineSeries->append(double(i),     v);
            m_lineSeries->append(double(i + 1), v);
            maxV = std::max(maxV, v);
        }
    }

    if (m_markersOn) {
        for (int i = 0; i < n; ++i)
            m_scatterSeries->append(i + 0.5, m_current->factor(i));
    }
    m_scatterSeries->setVisible(m_markersOn);

    // Categorical x labels — Jan, Sun, 00:00 etc. — anchored at slot centres.
    for (int i = 0; i < n; ++i) {
        m_xAxis->append(PatternProvider::rowLabel(m_current->type(), i),
                        i + 0.5);
    }
    m_xAxis->setRange(0.0, double(n));
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
            m_suppressListItemRename = true;
            item->setText(now);
            m_suppressListItemRename = false;
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
    // No intermediate dialog — open inline edit on the current list row.
    if (!m_listView) return;
    const QModelIndex idx = m_listView->currentIndex();
    if (!idx.isValid()) return;
    m_listView->edit(idx);
}

void PatternEditorDialog::onListItemRenamed_(QStandardItem *item)
{
    if (!item || !m_registry || m_suppressListItemRename) return;
    auto *p = reinterpret_cast<PatternProvider *>(
        item->data(Qt::UserRole + 1).value<quintptr>());
    if (!p) return;
    const QString newName = item->text().trimmed();
    if (newName.isEmpty() || newName == p->name()) {
        m_suppressListItemRename = true;
        item->setText(p->name());
        m_suppressListItemRename = false;
        return;
    }
    if (!m_registry->rename(p, newName)) {
        m_suppressListItemRename = true;
        item->setText(p->name());
        m_suppressListItemRename = false;
        if (m_status)
            m_status->showMessage(
                tr("A pattern named “%1” already exists.").arg(newName), 4000);
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
    QAction *actNew       = menu.addAction(tr("New…"));
    QAction *actDuplicate = menu.addAction(tr("Duplicate…"));
    QAction *actRename    = menu.addAction(tr("Rename…"));
    menu.addSeparator();
    QAction *actDelete    = menu.addAction(tr("Delete"));
    QAction *picked = menu.exec(m_listView->viewport()->mapToGlobal(pos));
    if      (picked == actNew)       onNewClicked_();
    else if (picked == actDuplicate) onDuplicateClicked_();
    else if (picked == actRename)    onRenameClicked_();
    else if (picked == actDelete)    onDeleteClicked_();
}

// ─────────────────────────────────────────────────────────────────────────────
// Duplicate / Search / Plot toolbar slots (Slice BR-PAT)
// ─────────────────────────────────────────────────────────────────────────────

void PatternEditorDialog::onDuplicateClicked_()
{
    if (!m_current || !m_registry) return;

    // Auto-suggest "<name>_copy" / "<name>_copy_2" / … and create directly.
    // The user can rename inline via the list-view edit affordance.
    const QString base = m_current->name() + QLatin1String("_copy");
    QString newName = base;
    int n = 2;
    while (m_registry->hasName(newName)) {
        newName = base + QStringLiteral("_%1").arg(n++);
    }

    PatternProvider *clone = duplicateCurrent(newName);
    if (!clone) {
        QMessageBox::warning(this, tr("Duplicate Pattern"),
            tr("Could not duplicate to “%1” — name already in use.").arg(newName));
        return;
    }
    // Jump the user into inline-rename on the new row.
    if (m_listView && m_listView->currentIndex().isValid())
        m_listView->edit(m_listView->currentIndex());
}

PatternProvider *PatternEditorDialog::duplicateCurrent(const QString &newName)
{
    if (!m_current || !m_registry) return nullptr;
    PatternProvider *clone = m_registry->duplicate(m_current->name(), newName);
    if (clone) selectProviderInList_(clone);
    return clone;
}

void PatternEditorDialog::onSearchTextChanged_(const QString &text)
{
    if (!m_listProxy) return;
    m_listProxy->setFilterFixedString(text);

    // If the previous selection got filtered out, clear the binding so the
    // editor doesn't appear to act on a hidden item.
    if (m_current) {
        const QModelIndex cur = m_listView->currentIndex();
        if (!cur.isValid()) bindProvider_(nullptr);
    }
}

void PatternEditorDialog::onCopyChartClicked_()
{
    if (!m_chartView) return;
    QClipboard *clip = QApplication::clipboard();
    if (clip) clip->setPixmap(m_chartView->grab());
}

void PatternEditorDialog::onExportChartClicked_()
{
    if (!m_chartView) return;
    const QString suggested =
        (m_current ? m_current->name() : QStringLiteral("pattern")) +
        QStringLiteral("_preview.png");
    const QString chosen = QFileDialog::getSaveFileName(
        this, tr("Export Chart"), suggested,
        tr("PNG Image (*.png);;Scalable Vector Graphics (*.svg)"));
    if (chosen.isEmpty()) return;

    const QString suffix = QFileInfo(chosen).suffix().toLower();
    if (suffix == QLatin1String("svg")) {
        QSvgGenerator svg;
        svg.setFileName(chosen);
        const QSize sz = m_chartView->size();
        svg.setSize(sz);
        svg.setViewBox(QRect(QPoint(0, 0), sz));
        svg.setTitle(tr("Pattern preview"));
        QPainter painter(&svg);
        painter.setRenderHint(QPainter::Antialiasing, true);
        m_chartView->render(&painter);
        painter.end();
    } else {
        QPixmap pm = m_chartView->grab();
        if (!pm.save(chosen, "PNG")) {
            QMessageBox::warning(this, tr("Export Chart"),
                tr("Failed to write “%1”.").arg(chosen));
        }
    }
}

void PatternEditorDialog::onShowPlotStyleMenu_(const QPoint &globalPos)
{
    QMenu menu(this);

    QAction *actStep = menu.addAction(tr("Step-line"));
    actStep->setCheckable(true);
    actStep->setChecked(!m_smoothPreview);

    QAction *actSmooth = menu.addAction(tr("Smooth line"));
    actSmooth->setCheckable(true);
    actSmooth->setChecked(m_smoothPreview);

    menu.addSeparator();

    QAction *actMarkers = menu.addAction(tr("Show markers"));
    actMarkers->setCheckable(true);
    actMarkers->setChecked(m_markersOn);

    QAction *picked = menu.exec(globalPos);
    if (!picked) return;

    if      (picked == actStep)    setStepLinePreview(true);
    else if (picked == actSmooth)  setStepLinePreview(false);
    else if (picked == actMarkers) setPreviewMarkersVisible(actMarkers->isChecked());
}

void PatternEditorDialog::setStepLinePreview(bool stepLine)
{
    const bool newSmooth = !stepLine;
    if (newSmooth == m_smoothPreview) return;
    m_smoothPreview = newSmooth;
    refreshChart_();
}

void PatternEditorDialog::setPreviewMarkersVisible(bool visible)
{
    if (visible == m_markersOn) return;
    m_markersOn = visible;
    refreshChart_();
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

// ─────────────────────────────────────────────────────────────────────────────
// Persistence — geometry, splitter, plot style toggles
// ─────────────────────────────────────────────────────────────────────────────

void PatternEditorDialog::closeEvent(QCloseEvent *e)
{
    saveDialogSettings_();
    QDialog::closeEvent(e);
}

void PatternEditorDialog::saveDialogSettings_() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("dialogs/patternEditor"));
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    if (m_splitter)
        s.setValue(QStringLiteral("splitterState"), m_splitter->saveState());
    s.setValue(QStringLiteral("plotStyle/smoothLine"), m_smoothPreview);
    s.setValue(QStringLiteral("plotStyle/markers"),    m_markersOn);
    s.endGroup();
}

void PatternEditorDialog::restoreDialogSettings_()
{
    QSettings s;
    s.beginGroup(QStringLiteral("dialogs/patternEditor"));
    const QByteArray geom = s.value(QStringLiteral("geometry")).toByteArray();
    if (!geom.isEmpty()) restoreGeometry(geom);

    if (m_splitter) {
        const QByteArray ss = s.value(QStringLiteral("splitterState")).toByteArray();
        if (!ss.isEmpty()) m_splitter->restoreState(ss);
    }

    m_smoothPreview = s.value(QStringLiteral("plotStyle/smoothLine"),
                                m_smoothPreview).toBool();
    m_markersOn     = s.value(QStringLiteral("plotStyle/markers"),
                                m_markersOn).toBool();
    s.endGroup();

    // Re-render with the restored toggles.
    refreshChart_();
}

} // namespace openswmmvis::ui
