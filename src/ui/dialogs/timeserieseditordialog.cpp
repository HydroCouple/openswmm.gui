/*!
 * \file   timeserieseditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/timeserieseditordialog.h"

#include "core/swmmdatetime.h"
#include "io/timeseriesparse.h"
#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"
#include "timeseries/timeseriesundocommands.h"
#include "ui/dialogs/dialoglayoutpersistence.h"
#include "ui/panels/timeseriestablemodel.h"
#include "ui/widgets/interactivechartview.h"
#include "ui/widgets/chartaxisformatcontroller.h"
#include "ui/widgets/timeserieseditchartview.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QInputDialog>
#include <QListView>
#include <QMessageBox>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStatusBar>
#include <QTextStream>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QVector>

#include "ui/panels/timeserieslistmodel.h"

#include <algorithm>
#include <limits>

// Debug instrumentation for the .dat-file load stall investigation.
// Enable with: QT_LOGGING_RULES="openswmm.ts-load.*=true"
Q_LOGGING_CATEGORY(lcTsLoadDialog, "openswmm.ts-load.dialog")

namespace openswmmvis::ui {

using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::timeseries::TimeseriesRegistry;

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

TimeseriesEditorDialog::TimeseriesEditorDialog(TimeseriesRegistry *registry,
                                                QUndoStack *undoStack,
                                                TimeseriesProvider *initialSelection,
                                                QWidget *parent)
    : QDialog(parent, openswmmvis::ui::floatingPanelFlags())
    , m_undoStack(undoStack)
    , m_registry(registry)
{
    setWindowTitle(tr("Time Series Editor"));
    // Step E.2 — objectName drives QSettings group + helper findChild lookup.
    setObjectName(QStringLiteral("TimeseriesEditorDialog"));
    // Step H — pin above the main window so a map click doesn't hide us.
    openswmmvis::ui::applyAlwaysOnTopPolicy(this);
    // Step D — generous default so the chart pane has room to breathe;
    // splitter sizes below force the proportions explicitly. QSettings-backed
    // restoreDialogLayout below overrides geometry + splitter state on
    // subsequent opens.
    resize(1400, 720);

    // Seed the bound-provider vector with the initial selection if any. The
    // 3-pane editor always builds the list pane (Step A removed the
    // 2-pane variants), so a null initialSelection just leaves grid + chart
    // empty until the user picks a series from the left list.
    if (initialSelection)
        m_providers.push_back(QPointer<TimeseriesProvider>(initialSelection));

    buildUi_(initialSelection ? QVector<TimeseriesProvider *>{initialSelection}
                              : QVector<TimeseriesProvider *>{});
    buildListPane_();
    wireProviderSignals_();
    updateStatusBar_();

    if (initialSelection && m_listModel && m_listView) {
        const int row = m_listModel->rowOf(initialSelection);
        if (row >= 0) {
            const QModelIndex src = m_listModel->index(row, 0);
            const QModelIndex prx = m_listProxy ? m_listProxy->mapFromSource(src) : src;
            m_listView->setCurrentIndex(prx);
        }
    }

    // Step B — initial zoom-to-extent so the chart shows the full series on
    // open. Guard against empty series (degenerate axes).
    if (initialSelection && m_chartView && initialSelection->pointCount() > 0)
        m_chartView->zoomToExtent();

    // Step E.2 — restore persisted geometry + splitter state after the UI is
    // built. The hard-coded defaults above (resize + setSizes in
    // buildListPane_) only apply on first open; subsequent opens load the
    // user's last-used layout.
    openswmmvis::ui::restoreDialogLayout(this);
}

TimeseriesEditorDialog::~TimeseriesEditorDialog() = default;

void TimeseriesEditorDialog::closeEvent(QCloseEvent *e)
{
    // Step E.2 — persist dialog geometry + splitter sizes BEFORE the
    // close happens so we capture the user's last-used layout.
    openswmmvis::ui::saveDialogLayout(this);

    // Step F — dispose the currently-bound provider's cache if it's
    // ExternalFile-backed. The provider survives in the registry; only its
    // (re-readable) point vector is freed.
    if (!m_providers.isEmpty()) {
        if (auto *p = m_providers.first().data())
            p->disposePointCache();
    }
    QDialog::closeEvent(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Slice BM.0-Add-New — CreateNew factory + create-card
// ─────────────────────────────────────────────────────────────────────────────

TimeseriesEditorDialog *TimeseriesEditorDialog::createNew(TimeseriesRegistry *registry,
                                                          QUndoStack *undoStack,
                                                          QWidget *parent)
{
    auto *dlg = new TimeseriesEditorDialog(registry, undoStack,
                                           /*initialSelection=*/nullptr, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->buildCreateCard_();
    dlg->setWindowTitle(tr("New Time Series"));
    if (dlg->m_toolbar) dlg->m_toolbar->setEnabled(false);
    return dlg;
}

QString TimeseriesEditorDialog::pickTimeseries(TimeseriesRegistry *registry,
                                                QUndoStack         *undoStack,
                                                const QString      &initialName,
                                                QWidget            *parent)
{
    if (!registry) return {};

    // Stack-allocate so we can exec() modally without WA_DeleteOnClose.
    // Mirrors HydrographGroupEditor::pickGroup.
    TimeseriesProvider *initialProvider =
        initialName.isEmpty() ? nullptr : registry->findByName(initialName);

    TimeseriesEditorDialog dlg(registry, undoStack, initialProvider, parent);
    dlg.setModal(true);

    if (!initialProvider) {
        // Empty / unknown initial name — open in CreateNew mode.
        dlg.m_mode = Mode::CreateNew;
        dlg.buildCreateCard_();
        dlg.setWindowTitle(tr("New Time Series"));
        if (dlg.m_toolbar) dlg.m_toolbar->setEnabled(false);
    } else {
        dlg.setWindowTitle(tr("Time Series Editor — %1").arg(initialProvider->name()));
    }

    dlg.exec();

    // Flush any inline edits back to the engine so callers that read
    // names via the engine API see the new/edited series.
    registry->saveToEngine();

    return dlg.currentName();
}

QString TimeseriesEditorDialog::currentName() const
{
    if (m_providers.isEmpty()) return {};
    auto *p = m_providers.first().data();
    return p ? p->name() : QString();
}

void TimeseriesEditorDialog::setProjectAnchor(const QString &dir)
{
    // Slice IO-11d — caller wires this to the active project's .inp dir;
    // empty disables relative-path display (legacy behaviour).
    if (dir == m_projectAnchor) return;
    m_projectAnchor = dir;
    // Refresh the source-mode card if it's already showing; otherwise the
    // next refresh picks up the new anchor.
    if (m_sourceCard && m_sourceCard->isVisible())
        refreshSourceModeCardForProvider_();
}

void TimeseriesEditorDialog::buildCreateCard_()
{
    auto *outer = qobject_cast<QVBoxLayout *>(layout());
    if (!outer) return;

    m_createCard = new QFrame(this);
    m_createCard->setFrameShape(QFrame::StyledPanel);
    m_createCard->setObjectName(QStringLiteral("createCard"));

    auto *cardLayout = new QVBoxLayout(m_createCard);
    cardLayout->setContentsMargins(12, 8, 12, 8);

    // Row 1: Name + uniqueness validator label.
    auto *row1 = new QHBoxLayout();
    row1->addWidget(new QLabel(tr("Name:"), m_createCard));
    m_nameEdit = new QLineEdit(m_createCard);
    m_nameEdit->setPlaceholderText(tr("Time series name (required)"));
    row1->addWidget(m_nameEdit, /*stretch=*/1);
    cardLayout->addLayout(row1);

    m_nameValidationLabel = new QLabel(m_createCard);
    m_nameValidationLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_nameValidationLabel->hide();
    cardLayout->addWidget(m_nameValidationLabel);

    // Row 2: Source mode + Create button.
    auto *row2 = new QHBoxLayout();
    row2->addWidget(new QLabel(tr("Source:"), m_createCard));
    m_sourceModeCombo = new QComboBox(m_createCard);
    m_sourceModeCombo->addItem(tr("Inline (table in .inp)"),
                               int(TimeseriesProvider::SourceMode::Inline));
    m_sourceModeCombo->addItem(tr("External file (FILE \"path\")"),
                               int(TimeseriesProvider::SourceMode::ExternalFile));
    m_sourceModeCombo->addItem(tr("Geopackage observed series"),
                               int(TimeseriesProvider::SourceMode::GeopackageObserved));
    row2->addWidget(m_sourceModeCombo);
    row2->addStretch(1);
    m_createBtn = new QPushButton(tr("Create"), m_createCard);
    m_createBtn->setDefault(true);
    m_createBtn->setEnabled(false);
    row2->addWidget(m_createBtn);
    cardLayout->addLayout(row2);

    outer->insertWidget(0, m_createCard);

    connect(m_nameEdit, &QLineEdit::textChanged,
            this, &TimeseriesEditorDialog::onCreateNewNameChanged_);
    connect(m_createBtn, &QPushButton::clicked,
            this, &TimeseriesEditorDialog::onCreateNewSubmit_);

    m_nameEdit->setFocus();
}

void TimeseriesEditorDialog::onCreateNewNameChanged_(const QString &text)
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
        m_nameValidationLabel->setText(tr("A time series named “%1” already exists.").arg(trimmed));
        m_nameValidationLabel->show();
        m_nameEdit->setStyleSheet(QStringLiteral("border: 1px solid #c0392b;"));
    } else {
        m_createBtn->setEnabled(true);
        m_nameValidationLabel->hide();
        m_nameEdit->setStyleSheet(QString());
    }
}

void TimeseriesEditorDialog::onCreateNewSubmit_()
{
    if (!m_registry || !m_nameEdit) return;
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty() || m_registry->hasName(name)) return;

    TimeseriesProvider *p = m_registry->create(name);
    if (!p) return;   // registry refused (race on uniqueness)

    if (m_sourceModeCombo) {
        const auto mode = static_cast<TimeseriesProvider::SourceMode>(
            m_sourceModeCombo->currentData().toInt());
        p->setSourceMode(mode);
    }
    bindNewProvider_(p);
}

void TimeseriesEditorDialog::bindNewProvider_(TimeseriesProvider *p)
{
    if (!p) return;

    m_providers.clear();
    m_providers.push_back(QPointer<TimeseriesProvider>(p));

    if (m_tableModel) m_tableModel->setProviders({p});
    if (m_chartView)  m_chartView->setProvider(p);

    wireProviderSignals_();
    updateStatusBar_();
    // Phase 6.7.3.6 bug-fix — without this the source-mode card stays in its
    // "no provider bound → disabled" state from the initial build, so the
    // External radio + Browse button never appear after the Create flow.
    refreshSourceModeCardForProvider_();

    m_mode = Mode::Edit;
    setWindowTitle(tr("Time Series Editor — %1").arg(p->name()));
    if (m_createCard) m_createCard->hide();
    if (m_toolbar)    m_toolbar->setEnabled(true);
}

QString TimeseriesEditorDialog::pendingName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

bool TimeseriesEditorDialog::isCreateEnabled() const
{
    return m_createBtn && m_createBtn->isEnabled();
}

void TimeseriesEditorDialog::submitCreateNew()
{
    onCreateNewSubmit_();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI assembly
// ─────────────────────────────────────────────────────────────────────────────

void TimeseriesEditorDialog::buildUi_(const QVector<TimeseriesProvider *> &providers)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Toolbar ─────────────────────────────────────────────────────────────
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(20, 20));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    outer->addWidget(m_toolbar);

    auto *baseGroup = new QActionGroup(this);
    baseGroup->setExclusive(true);

    m_actSelect  = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Select")),
                                          tr("Select"));
    m_actPan     = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Move")),
                                          tr("Pan"));
    m_actZoomIn  = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/ZoomIn")),
                                          tr("Zoom In"));
    m_actZoomOut = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/ZoomOut")),
                                          tr("Zoom Out"));
    for (QAction *a : {m_actSelect, m_actPan, m_actZoomIn, m_actZoomOut}) {
        a->setCheckable(true);
        baseGroup->addAction(a);
    }
    m_actSelect->setChecked(true);

    // Zoom to Extent — non-checkable, one-shot fit-all. Not part of the
    // exclusive mode group; clicking refits axes without changing mode.
    m_actZoomExt = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                          tr("Zoom to Extent"));
    m_actZoomExt->setToolTip(tr("Fit X and Y axes to all loaded points"));

    m_toolbar->addSeparator();

    m_actEdit = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/SelectEdit")),
                                       tr("Edit Points"));
    m_actEdit->setCheckable(true);
    m_actEdit->setToolTip(tr("Y-drag to change values; Shift-drag to multi-select"));

    m_actRotate = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Ruler")),
                                         tr("Rotate"));
    m_actRotate->setCheckable(true);
    m_actRotate->setToolTip(tr("Rotate the current selection around a pivot (numeric panel)"));

    m_actScale = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Style")),
                                        tr("Scale"));
    m_actScale->setCheckable(true);
    m_actScale->setToolTip(tr("Scale the current selection around an anchor (numeric panel)"));

    // The three edit-mode toggles are mutually exclusive but all-off is also
    // valid (none = base Select/Pan/Zoom only).
    auto *editModeGroup = new QActionGroup(this);
    editModeGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    editModeGroup->addAction(m_actEdit);
    editModeGroup->addAction(m_actRotate);
    editModeGroup->addAction(m_actScale);

    m_actSnap = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/ToggleOn")),
                                       tr("Snap"));
    m_actSnap->setCheckable(true);
    m_actSnap->setToolTip(tr("Snap inserted/dragged times to the reporting step"));

    m_toolbar->addSeparator();

    if (m_undoStack) {
        m_actUndo = m_undoStack->createUndoAction(this, tr("Undo"));
        m_actRedo = m_undoStack->createRedoAction(this, tr("Redo"));
        m_toolbar->addAction(m_actUndo);
        m_toolbar->addAction(m_actRedo);
    }

    m_toolbar->addSeparator();

    // ── Row editing: Add / Delete / Copy / Paste ────────────────────────────
    // Phase 6.7.3.4-followup. Add/Delete go through Insert/DeletePointsCommand;
    // Copy serialises selected rows to TSV; Paste parses via TimeseriesParse
    // (shared with file import) so Excel- and CSV-derived clipboard text
    // round-trips with the same time-format handling.
    m_actAddRow = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/New")),
                                         tr("Add Row"));
    m_actAddRow->setShortcut(QKeySequence(Qt::Key_Insert));
    m_actAddRow->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_actAddRow->setToolTip(tr("Insert a new point (Insert key)"));

    m_actDeleteRow = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Clear")),
                                            tr("Delete Rows"));
    m_actDeleteRow->setShortcut(QKeySequence::Delete);
    m_actDeleteRow->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_actDeleteRow->setToolTip(tr("Delete selected row(s) (Delete key)"));

    m_actCopy = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Copy")),
                                       tr("Copy"));
    m_actCopy->setShortcut(QKeySequence::Copy);
    m_actCopy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_actCopy->setToolTip(tr("Copy selected row(s) as TSV (Ctrl/Cmd+C)"));

    m_actPaste = m_toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/AddDelimetered")),
                                        tr("Paste"));
    m_actPaste->setShortcut(QKeySequence::Paste);
    m_actPaste->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_actPaste->setToolTip(tr("Paste rows from Excel / CSV / TSV clipboard (Ctrl/Cmd+V)"));

    // ── Source-mode card (Phase 6.7.3.6) ────────────────────────────────────
    buildSourceModeCard_();

    // ── Transform panel (Phase 6.7.3.5 follow-up — numeric Rotate/Scale) ────
    buildTransformPanel_();

    // ── Two-pane splitter: grid (left) | chart (right) ──────────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);
    // Step E.2 — objectName lets the persistence helper round-trip
    // splitter state via QSettings (per-named-splitter under the dialog).
    m_splitter->setObjectName(QStringLiteral("main"));
    outer->addWidget(m_splitter, /*stretch=*/1);

    // Grid
    m_table = new QTableView(m_splitter);
    m_tableModel = new TimeseriesTableModel(this);
    m_tableModel->setUndoStack(m_undoStack);
    m_tableModel->setProviders(providers);
    m_table->setModel(m_tableModel);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionsClickable(true);
    m_table->horizontalHeader()->setSectionsMovable(false);

    // Right-click context menu on the grid surfaces the same Add/Delete/Copy
    // /Paste actions exposed in the toolbar + keyboard shortcuts, so the
    // affordance is discoverable via the QTableView idiom. Actions are shared
    // (not duplicates) — their enabled/disabled state stays in sync.
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested,
            this, &TimeseriesEditorDialog::onGridContextMenu_);
    // Allow in-place header rename — Qt's default QHeaderView doesn't do this
    // out of the box; the dialog phase delivers the basic edit path via
    // setHeaderData (right-click header → "Rename…" action lands in a
    // follow-up sub-phase).

    // Table->chart selection sync. Skipped while the chart is driving us.
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_splitter->addWidget(m_table);

    // Chart (binds to providers[0])
    m_chartView = new TimeseriesEditChartView(providers.value(0, nullptr), m_splitter);
    m_chartView->setUndoStack(m_undoStack);
    // Per-chart axis number format (decimals / sig figs / custom printf),
    // reachable via the chart's right-click "Chart Properties…" entry.
    m_axisFmt = new ChartAxisFormatController(m_chartView->chart(), this);
    m_chartView->setAxisFormatController(m_axisFmt);
    connect(m_chartView, &TimeseriesEditChartView::editModeChanged,
            this, [this]() { onChartEditModeChanged_(); });
    // MVC chart<->table selection sync: clicking points / rubber-band on the
    // chart mirrors into the table row selection, and vice versa. Guard with
    // recursion-suppress flags so the two views don't loop forever.
    connect(m_chartView, &TimeseriesEditChartView::selectionChanged, this,
            [this](const QVector<int> &indices) {
                if (m_suppressTableSelectionSync_ts) return;
                if (!m_table || !m_table->selectionModel() || !m_tableModel) return;
                m_suppressChartSelectionSync_ts = true;
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
                m_suppressChartSelectionSync_ts = false;
            });
    m_splitter->addWidget(m_chartView);

    // Step D — initial stretch factors are finalized in buildListPane_() after
    // the list pane is inserted at index 0. Setting them here would be
    // overwritten anyway. setSizes() in buildListPane_ overrides sizeHints so
    // the chart isn't squeezed to a sliver by the grid's larger sizeHint.

    // ── Status bar ──────────────────────────────────────────────────────────
    m_status = new QStatusBar(this);
    m_countLabel = new QLabel(m_status);
    m_rangeLabel = new QLabel(m_status);
    m_status->addPermanentWidget(m_countLabel);
    m_status->addPermanentWidget(m_rangeLabel);
    outer->addWidget(m_status);

    // ── Wire toolbar actions ────────────────────────────────────────────────
    connect(m_actSelect,  &QAction::triggered, this, &TimeseriesEditorDialog::onSelectModeTriggered_);
    connect(m_actPan,     &QAction::triggered, this, &TimeseriesEditorDialog::onPanModeTriggered_);
    connect(m_actZoomIn,  &QAction::triggered, this, &TimeseriesEditorDialog::onZoomInModeTriggered_);
    connect(m_actZoomOut, &QAction::triggered, this, &TimeseriesEditorDialog::onZoomOutModeTriggered_);
    connect(m_actZoomExt, &QAction::triggered, this, [this]() {
        if (m_chartView) m_chartView->zoomToExtent();
    });
    connect(m_actEdit,    &QAction::triggered, this, &TimeseriesEditorDialog::onEditModeTriggered_);
    connect(m_actRotate,  &QAction::triggered, this, &TimeseriesEditorDialog::onEditModeTriggered_);
    connect(m_actScale,   &QAction::triggered, this, &TimeseriesEditorDialog::onEditModeTriggered_);
    connect(m_actSnap,    &QAction::toggled,   this, &TimeseriesEditorDialog::onSnapToggled_);

    connect(m_actAddRow,    &QAction::triggered, this, &TimeseriesEditorDialog::onAddRowTriggered_);
    connect(m_actDeleteRow, &QAction::triggered, this, &TimeseriesEditorDialog::onDeleteRowsTriggered_);
    connect(m_actCopy,      &QAction::triggered, this, &TimeseriesEditorDialog::onCopyRowsTriggered_);
    connect(m_actPaste,     &QAction::triggered, this, &TimeseriesEditorDialog::onPasteRowsTriggered_);

    // Register row-editing actions on the dialog itself so the keyboard
    // shortcuts fire even when the grid doesn't have focus (WidgetWithChildren
    // covers the table view + chart view children).
    addAction(m_actAddRow);
    addAction(m_actDeleteRow);
    addAction(m_actCopy);
    addAction(m_actPaste);

    // Table->chart selection sync. Needs the table's selection model to be
    // alive, hence wired after the table is built.
    if (m_table && m_table->selectionModel()) {
        connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [this](const QItemSelection &, const QItemSelection &) {
                    if (m_suppressChartSelectionSync_ts) return;
                    if (!m_chartView) return;
                    QVector<int> rows;
                    if (m_table->selectionModel()) {
                        for (const auto &idx : m_table->selectionModel()->selectedRows())
                            rows.push_back(idx.row());
                    }
                    std::sort(rows.begin(), rows.end());
                    m_suppressTableSelectionSync_ts = true;
                    m_chartView->setSelection(rows);
                    m_suppressTableSelectionSync_ts = false;
                });
    }
}

void TimeseriesEditorDialog::wireProviderSignals_()
{
    for (const auto& p : m_providers) {
        if (!p) continue;
        connect(p, &TimeseriesProvider::mutationRejected,
                this, &TimeseriesEditorDialog::onMutationRejected_);
        connect(p, &TimeseriesProvider::pointsChanged,
                this, &TimeseriesEditorDialog::onProviderPointsChanged_);
        connect(p, &TimeseriesProvider::pointsInserted,
                this, &TimeseriesEditorDialog::onProviderPointsInserted_);
        connect(p, &TimeseriesProvider::pointsRemoved,
                this, &TimeseriesEditorDialog::onProviderPointsRemoved_);
        // Phase 6.7.3.6 — source-mode card mirrors provider mode changes
        // (e.g. when an undo flips the mode externally).
        connect(p, &TimeseriesProvider::sourceModeChanged,
                this, [this]() { refreshSourceModeCardForProvider_(); });
        connect(p, &TimeseriesProvider::metadataChanged,
                this, [this]() { refreshSourceModeCardForProvider_(); });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Status bar
// ─────────────────────────────────────────────────────────────────────────────

void TimeseriesEditorDialog::updateStatusBar_()
{
    auto *first = m_providers.isEmpty() ? nullptr : m_providers.first().data();
    if (!first || first->pointCount() == 0) {
        m_countLabel->setText(tr("0 points"));
        m_rangeLabel->clear();
        return;
    }

    const int n = first->pointCount();
    double yMin =  std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (const auto& pt : first->points()) {
        yMin = std::min(yMin, pt.value);
        yMax = std::max(yMax, pt.value);
    }
    m_countLabel->setText(tr("%1 points").arg(n));
    m_rangeLabel->setText(tr("y: [%1, %2]   t: %3 → %4")
                          .arg(yMin, 0, 'g', 6)
                          .arg(yMax, 0, 'g', 6)
                          .arg(first->pointAt(0).time.toString(Qt::ISODate))
                          .arg(first->pointAt(n - 1).time.toString(Qt::ISODate)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────────────────────────────────────

void TimeseriesEditorDialog::onSelectModeTriggered_()
{
    if (m_chartView) m_chartView->setMode(InteractiveChartView::Mode::Select);
}
void TimeseriesEditorDialog::onPanModeTriggered_()
{
    if (m_chartView) m_chartView->setMode(InteractiveChartView::Mode::Pan);
}
void TimeseriesEditorDialog::onZoomInModeTriggered_()
{
    if (m_chartView) m_chartView->setMode(InteractiveChartView::Mode::ZoomIn);
}
void TimeseriesEditorDialog::onZoomOutModeTriggered_()
{
    if (m_chartView) m_chartView->setMode(InteractiveChartView::Mode::ZoomOut);
}

void TimeseriesEditorDialog::onEditModeTriggered_()
{
    if (!m_chartView) return;
    // Three toggle actions in an ExclusiveOptional group — pick whichever is
    // checked (or None if all three are off). Same handler covers all three
    // because the group enforces at-most-one.
    auto mode = TimeseriesEditChartView::EditMode::None;
    if (m_actEdit   && m_actEdit->isChecked())   mode = TimeseriesEditChartView::EditMode::EditPoints;
    if (m_actRotate && m_actRotate->isChecked()) mode = TimeseriesEditChartView::EditMode::RotatePoints;
    if (m_actScale  && m_actScale->isChecked())  mode = TimeseriesEditChartView::EditMode::ScalePoints;
    m_chartView->setEditMode(mode);
}

void TimeseriesEditorDialog::onSnapToggled_(bool on)
{
    if (m_chartView) m_chartView->setSnapToTimeStep(on);
}

void TimeseriesEditorDialog::onMutationRejected_(const QString &reason)
{
    qCWarning(lcTsLoadDialog) << "onMutationRejected_:" << reason;
    if (m_status) m_status->showMessage(reason, 4000);
}

void TimeseriesEditorDialog::onProviderPointsChanged_()
{
    QElapsedTimer t; t.start();
    updateStatusBar_();
    qCDebug(lcTsLoadDialog) << "onProviderPointsChanged_ → updateStatusBar_ took"
                            << t.elapsed() << "ms";
}
void TimeseriesEditorDialog::onProviderPointsInserted_()
{
    QElapsedTimer t; t.start();
    updateStatusBar_();
    qCDebug(lcTsLoadDialog) << "onProviderPointsInserted_ → updateStatusBar_ took"
                            << t.elapsed() << "ms";
}
void TimeseriesEditorDialog::onProviderPointsRemoved_()
{
    QElapsedTimer t; t.start();
    updateStatusBar_();
    qCDebug(lcTsLoadDialog) << "onProviderPointsRemoved_ → updateStatusBar_ took"
                            << t.elapsed() << "ms";
}

// ─────────────────────────────────────────────────────────────────────────────
// Row editing — Add / Delete / Copy / Paste (Phase 6.7.3.4-followup)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

using openswmmvis::timeseries::DeletePointsCommand;
using openswmmvis::timeseries::InsertPointCommand;
using openswmmvis::timeseries::TimeseriesPoint;

/*! \brief Pick a sensible default timestamp for a fresh row:
 *   - empty provider                → now (UTC)
 *   - one point                     → last + 1 hour
 *   - two or more points            → last + median interval
 *  This keeps the strict-monotone-time invariant trivially satisfied without
 *  asking the user to type a date for the common "append next point" case. */
QDateTime defaultNextTime(const TimeseriesProvider *p)
{
    if (!p || p->pointCount() == 0)
        return QDateTime::currentDateTimeUtc();

    const auto &pts = p->points();
    const QDateTime last = pts.back().time;
    if (pts.size() == 1)
        return last.addSecs(3600);

    // Median interval between consecutive points (in seconds).
    QVector<qint64> deltas;
    deltas.reserve(pts.size() - 1);
    for (int i = 1; i < pts.size(); ++i)
        deltas.push_back(pts.at(i - 1).time.secsTo(pts.at(i).time));
    std::nth_element(deltas.begin(),
                     deltas.begin() + deltas.size() / 2,
                     deltas.end());
    const qint64 median = std::max<qint64>(1, deltas.at(deltas.size() / 2));
    return last.addSecs(median);
}

/*! \brief Unique sorted row indices from a QItemSelectionModel's selectedIndexes. */
QVector<int> selectedRowIndices(const QTableView *table)
{
    QVector<int> rows;
    if (!table || !table->selectionModel()) return rows;
    const auto selected = table->selectionModel()->selectedIndexes();
    for (const QModelIndex &idx : selected) {
        if (idx.isValid()) rows.push_back(idx.row());
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

} // namespace

void TimeseriesEditorDialog::onAddRowTriggered_()
{
    if (m_providers.isEmpty()) return;
    auto *first = m_providers.first().data();
    if (!first) return;
    if (first->sourceMode() == TimeseriesProvider::SourceMode::ExternalFile) {
        if (m_status) m_status->showMessage(
            tr("Add Row disabled: this series is linked to an external file. "
               "Use Convert-to-Inline first."), 4000);
        return;
    }

    // If a row is selected, the new point lands immediately after it (midpoint
    // between selected and next, or appended past the last with the median
    // interval when the last row is selected). With no selection the new
    // point is appended past the end.
    int selectedRow = -1;
    if (m_table && m_table->selectionModel()) {
        for (const auto &idx : m_table->selectionModel()->selectedRows())
            selectedRow = std::max(selectedRow, idx.row());
        if (selectedRow < 0) {
            const QModelIndex cur = m_table->selectionModel()->currentIndex();
            if (cur.isValid()) selectedRow = cur.row();
        }
    }

    const int n = first->pointCount();
    QDateTime t;
    double v = 0.0;
    if (n == 0) {
        t = QDateTime::currentDateTimeUtc();
        v = 0.0;
    } else if (selectedRow < 0 || selectedRow >= n - 1) {
        // No selection or last row → append after the last point.
        t = defaultNextTime(first);
        v = first->pointAt(n - 1).value;
    } else {
        // Insert between selectedRow and the row after; midpoint in time
        // preserves the strict-ascending-time invariant by construction.
        const auto &a = first->pointAt(selectedRow);
        const auto &b = first->pointAt(selectedRow + 1);
        const qint64 midMs = (a.time.toMSecsSinceEpoch()
                              + b.time.toMSecsSinceEpoch()) / 2;
        t = QDateTime::fromMSecsSinceEpoch(midMs, Qt::UTC);
        v = 0.5 * (a.value + b.value);
    }

    if (m_undoStack) {
        m_undoStack->beginMacro(tr("Add timeseries row"));
        for (const auto &pp : m_providers) {
            if (!pp) continue;
            m_undoStack->push(new InsertPointCommand(pp.data(), t, v));
        }
        m_undoStack->endMacro();
    } else {
        for (const auto &pp : m_providers)
            if (pp) pp->insertPoint(t, v);
    }

    // Find + select the row that landed at time `t` so the user can see it.
    if (m_table && m_tableModel) {
        int landedRow = -1;
        for (int i = 0; i < first->pointCount(); ++i) {
            if (first->pointAt(i).time == t) { landedRow = i; break; }
        }
        if (landedRow >= 0) {
            const QModelIndex idx = m_tableModel->index(landedRow, 0);
            if (idx.isValid()) {
                m_table->scrollTo(idx);
                m_table->selectRow(landedRow);
            }
        }
    }
}

void TimeseriesEditorDialog::onDeleteRowsTriggered_()
{
    if (m_providers.isEmpty()) return;
    const QVector<int> rows = selectedRowIndices(m_table);
    if (rows.isEmpty()) {
        if (m_status) m_status->showMessage(
            tr("Select one or more rows to delete."), 3000);
        return;
    }
    auto *first = m_providers.first().data();
    if (first && first->sourceMode() == TimeseriesProvider::SourceMode::ExternalFile) {
        if (m_status) m_status->showMessage(
            tr("Delete disabled: this series is linked to an external file."), 4000);
        return;
    }

    if (m_undoStack) {
        m_undoStack->beginMacro(tr("Delete timeseries row(s)"));
        for (const auto &pp : m_providers) {
            if (!pp) continue;
            m_undoStack->push(new DeletePointsCommand(pp.data(), rows));
        }
        m_undoStack->endMacro();
    } else {
        for (const auto &pp : m_providers)
            if (pp) pp->removePointsAt(rows);
    }
}

void TimeseriesEditorDialog::onCopyRowsTriggered_()
{
    if (m_providers.isEmpty()) return;
    auto *first = m_providers.first().data();
    if (!first || first->pointCount() == 0) return;

    QVector<int> rows = selectedRowIndices(m_table);
    if (rows.isEmpty()) {
        for (int i = 0; i < first->pointCount(); ++i) rows.push_back(i);
    }

    // Format: timestamp \t v0 \t v1 \t ... per row, ISO 8601 timestamps.
    QStringList lines;
    lines.reserve(rows.size());
    for (int row : rows) {
        if (row < 0 || row >= first->pointCount()) continue;
        QStringList cells;
        cells << first->pointAt(row).time.toUTC().toString(Qt::ISODate);
        for (const auto &pp : m_providers) {
            if (!pp || row >= pp->pointCount()) {
                cells << QString();
                continue;
            }
            cells << QString::number(pp->pointAt(row).value, 'g', 15);
        }
        lines << cells.join(QLatin1Char('\t'));
    }
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    if (m_status) m_status->showMessage(
        tr("Copied %1 row(s) to clipboard.").arg(rows.size()), 2500);
}

void TimeseriesEditorDialog::onPasteRowsTriggered_()
{
    if (m_providers.isEmpty()) return;
    auto *first = m_providers.first().data();
    if (!first) return;
    if (first->sourceMode() == TimeseriesProvider::SourceMode::ExternalFile) {
        if (m_status) m_status->showMessage(
            tr("Paste disabled: this series is linked to an external file."), 4000);
        return;
    }

    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) {
        if (m_status) m_status->showMessage(tr("Clipboard is empty."), 2500);
        return;
    }

    // Per-line delimiter + timestamp detection via the shared helper extracted
    // from ObservedCsvRunLayer — same parser handles Excel TSV, CSV exports,
    // SWMM .dat fragments, and ISO 8601 timestamps.
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                          Qt::SkipEmptyParts);

    int inserted = 0;
    int rejected = 0;
    if (m_undoStack) m_undoStack->beginMacro(tr("Paste timeseries rows"));

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith(';'))
            continue;

        const QChar delim = openswmmvis::io::guessDelimiter(trimmed);
        double tJulian = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> vals;
        if (!openswmmvis::io::parseRow(trimmed, delim, tJulian, vals,
                                       std::numeric_limits<double>::quiet_NaN())) {
            ++rejected;
            continue;
        }
        const QDateTime t = openswmmvis::core::swmmDateTimeToQDateTime(tJulian);
        if (!t.isValid()) { ++rejected; continue; }

        // Distribute pasted value cells across sibling providers (column-wise).
        // If the paste has fewer cells than providers, missing cells get NaN
        // → the provider rejects, which we count as a skip per provider.
        bool anyInserted = false;
        for (int c = 0; c < m_providers.size(); ++c) {
            auto *pp = m_providers.at(c).data();
            if (!pp) continue;
            const double v = (c < static_cast<int>(vals.size()))
                              ? vals[static_cast<std::size_t>(c)]
                              : std::numeric_limits<double>::quiet_NaN();
            if (!std::isfinite(v)) continue;

            bool ok = false;
            if (m_undoStack) {
                const int prev = pp->pointCount();
                m_undoStack->push(new InsertPointCommand(pp, t, v));
                ok = pp->pointCount() > prev;
            } else {
                ok = pp->insertPoint(t, v) >= 0;
            }
            if (ok) anyInserted = true;
        }
        if (anyInserted) ++inserted;
        else ++rejected;
    }

    if (m_undoStack) m_undoStack->endMacro();

    if (m_status) {
        QString msg = tr("Pasted %1 row(s)").arg(inserted);
        if (rejected > 0) msg += tr(" (%1 skipped — duplicate time or unparseable)").arg(rejected);
        msg += QStringLiteral(".");
        m_status->showMessage(msg, 4000);
    }
}

void TimeseriesEditorDialog::onGridContextMenu_(const QPoint &posInViewport)
{
    if (!m_table) return;

    QMenu menu(this);
    // Reuse the existing actions so checked/enabled state stays in sync.
    menu.addAction(m_actAddRow);
    menu.addAction(m_actDeleteRow);
    menu.addSeparator();
    menu.addAction(m_actCopy);
    menu.addAction(m_actPaste);
    menu.exec(m_table->viewport()->mapToGlobal(posInViewport));
}

// ─────────────────────────────────────────────────────────────────────────────
// Source-mode card (Phase 6.7.3.6)
// ─────────────────────────────────────────────────────────────────────────────

void TimeseriesEditorDialog::buildSourceModeCard_()
{
    auto *outer = qobject_cast<QVBoxLayout *>(layout());
    if (!outer) return;

    m_sourceCard = new QFrame(this);
    m_sourceCard->setFrameShape(QFrame::StyledPanel);
    m_sourceCard->setObjectName(QStringLiteral("sourceModeCard"));

    // ── Two-row layout ──────────────────────────────────────────────────────
    // Row 1: "Source:" label + radio set (always visible).
    // Row 2: External-mode sub-controls (path / Browse / column / Reload /
    //        Detach / status), visible only when External radio is checked.
    // Previously these were jammed into a single horizontal row which crowded
    // the Browse button off-screen at the default dialog width.
    auto *cardLayout = new QVBoxLayout(m_sourceCard);
    cardLayout->setContentsMargins(8, 4, 8, 4);
    cardLayout->setSpacing(4);

    // Row 1 — radio set.
    auto *row1 = new QHBoxLayout();
    row1->addWidget(new QLabel(tr("Source:"), m_sourceCard));

    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    m_radioInline     = new QRadioButton(tr("Inline"), m_sourceCard);
    m_radioExternal   = new QRadioButton(tr("External file"), m_sourceCard);
    m_radioGeopackage = new QRadioButton(tr("Geopackage"), m_sourceCard);
    modeGroup->addButton(m_radioInline);
    modeGroup->addButton(m_radioExternal);
    modeGroup->addButton(m_radioGeopackage);
    row1->addWidget(m_radioInline);
    row1->addWidget(m_radioExternal);
    row1->addWidget(m_radioGeopackage);
    row1->addStretch(1);

    // Geopackage placeholder sits next to the radios in row 1 (visible only
    // when Geopackage is selected).
    m_gpkgPlaceholderLbl = new QLabel(
        tr("(GeoPackage observed series — follow-up sub-phase)"),
        m_sourceCard);
    m_gpkgPlaceholderLbl->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    m_gpkgPlaceholderLbl->hide();
    row1->addWidget(m_gpkgPlaceholderLbl);

    cardLayout->addLayout(row1);

    // Row 2 — External-file sub-controls (visible only in External mode).
    auto *row2 = new QHBoxLayout();
    row2->addWidget(new QLabel(tr("File:"), m_sourceCard));

    m_extPathEdit = new QLineEdit(m_sourceCard);
    m_extPathEdit->setReadOnly(true);
    m_extPathEdit->setPlaceholderText(tr("(no file linked)"));
    row2->addWidget(m_extPathEdit, /*stretch=*/2);

    m_extBrowseBtn = new QPushButton(tr("Browse…"), m_sourceCard);
    m_extBrowseBtn->setToolTip(tr("Pick a CSV / TSV / .dat file"));
    row2->addWidget(m_extBrowseBtn);

    row2->addWidget(new QLabel(tr("Column:"), m_sourceCard));
    m_extColumnCombo = new QComboBox(m_sourceCard);
    m_extColumnCombo->setMinimumWidth(120);
    m_extColumnCombo->setToolTip(tr("Pick which column to bind to this series"));
    row2->addWidget(m_extColumnCombo);

    m_extReloadBtn = new QPushButton(tr("Reload"), m_sourceCard);
    m_extReloadBtn->setToolTip(tr("Re-read the linked file from disk"));
    row2->addWidget(m_extReloadBtn);

    m_extDetachBtn = new QPushButton(tr("Detach → Inline"), m_sourceCard);
    m_extDetachBtn->setToolTip(tr("Copy current points into the project and break the file link (one-way)"));
    row2->addWidget(m_extDetachBtn);

    m_extStatusLabel = new QLabel(m_sourceCard);
    m_extStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    row2->addWidget(m_extStatusLabel, /*stretch=*/1);

    // Wrap row 2 in a holder QWidget so we can show/hide the whole row at once.
    auto *extRowHolder = new QWidget(m_sourceCard);
    extRowHolder->setLayout(row2);
    extRowHolder->setObjectName(QStringLiteral("extRowHolder"));
    cardLayout->addWidget(extRowHolder);

    // Insert below toolbar (toolbar is at index 0).
    outer->insertWidget(1, m_sourceCard);

    // Wire signals
    connect(m_radioInline,     &QRadioButton::toggled,
            this, [this](bool on) { if (on) onSourceModeRadioToggled_(); });
    connect(m_radioExternal,   &QRadioButton::toggled,
            this, [this](bool on) { if (on) onSourceModeRadioToggled_(); });
    connect(m_radioGeopackage, &QRadioButton::toggled,
            this, [this](bool on) { if (on) onSourceModeRadioToggled_(); });
    connect(m_extBrowseBtn,    &QPushButton::clicked, this, &TimeseriesEditorDialog::onBrowseExternalFile_);
    connect(m_extColumnCombo,  qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TimeseriesEditorDialog::onColumnSelectorChanged_);
    connect(m_extReloadBtn,    &QPushButton::clicked, this, &TimeseriesEditorDialog::onReloadExternalFile_);
    connect(m_extDetachBtn,    &QPushButton::clicked, this, &TimeseriesEditorDialog::onDetachToInline_);

    refreshSourceModeCardForProvider_();
}

void TimeseriesEditorDialog::refreshSourceModeCardForProvider_()
{
    if (!m_sourceCard) return;

    if (m_providers.isEmpty() || !m_providers.first()) {
        m_sourceCard->setEnabled(false);
        return;
    }
    m_sourceCard->setEnabled(true);

    auto *p = m_providers.first().data();
    // Pick the radio without triggering our slot (block signals).
    {
        QSignalBlocker bA(m_radioInline);
        QSignalBlocker bB(m_radioExternal);
        QSignalBlocker bC(m_radioGeopackage);
        switch (p->sourceMode()) {
            case TimeseriesProvider::SourceMode::Inline:
                m_radioInline->setChecked(true); break;
            case TimeseriesProvider::SourceMode::ExternalFile:
                m_radioExternal->setChecked(true); break;
            case TimeseriesProvider::SourceMode::GeopackageObserved:
                m_radioGeopackage->setChecked(true); break;
        }
    }

    const bool isExternal = (p->sourceMode() == TimeseriesProvider::SourceMode::ExternalFile);
    const bool isGpkg     = (p->sourceMode() == TimeseriesProvider::SourceMode::GeopackageObserved);

    // Show/hide the entire External-mode row as a unit (cleaner than
    // toggling six individual widgets, and avoids the "blank gap" effect when
    // some are hidden but others stay).
    if (auto *extHolder = m_sourceCard->findChild<QWidget *>(QStringLiteral("extRowHolder")))
        extHolder->setVisible(isExternal);
    m_gpkgPlaceholderLbl->setVisible(isGpkg);

    if (isExternal) {
        const bool hasFile = !p->filePath().isEmpty();

        // Slice IO-11d — when a project anchor is set, render the path
        // relative to it; the underlying provider still owns the absolute
        // form. Tooltip surfaces the absolute for power users.
        const QString abs   = p->filePath();
        QString shown = abs;
        if (!abs.isEmpty() && !m_projectAnchor.isEmpty()) {
            QDir d(m_projectAnchor);
            const QString rel = d.relativeFilePath(abs);
            if (QFileInfo(rel).isRelative()) shown = rel;
        }
        m_extPathEdit->setText(shown);
        m_extPathEdit->setToolTip(abs.isEmpty() ? tr("(no file linked)")
                                                : tr("Resolved: %1").arg(abs));

        // Browse is ALWAYS enabled in External mode — that's the only way the
        // user can pick a file. The other sub-controls (column selector,
        // Reload, Detach) only make sense once a file is linked, so they gate
        // on hasFile.
        m_extBrowseBtn->setEnabled(true);
        m_extColumnCombo->setEnabled(hasFile && m_extColumnCombo->count() > 0);
        m_extReloadBtn->setEnabled(hasFile);
        m_extDetachBtn->setEnabled(hasFile);

        // Status: file mtime + staleness.
        if (!hasFile) {
            m_extStatusLabel->setText(tr("Pick a file with Browse… — grid + chart are read-only in this mode"));
            m_extStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
        } else {
            QFileInfo fi(p->filePath());
            if (!fi.exists()) {
                m_extStatusLabel->setText(tr("⚠ File not found"));
                m_extStatusLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
            } else {
                const QDateTime onDisk = fi.lastModified();
                const QDateTime cached = p->fileMTime();
                if (cached.isValid() && onDisk > cached) {
                    m_extStatusLabel->setText(tr("⚠ File changed on disk — Reload to pick up"));
                    m_extStatusLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
                } else {
                    m_extStatusLabel->setText(tr("Loaded at %1 (read-only — Detach to edit)").arg(onDisk.toString(Qt::ISODate)));
                    m_extStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
                }
            }
        }
    }

    // Read-only state must also be reflected in the toolbar so the user can
    // see at a glance that edits are blocked (not just silently no-op'd).
    // The chart's mouse handlers and the table model's setData already refuse
    // in External mode; this just gives them a visual cue.
    const bool editsBlocked = isExternal;
    if (m_actEdit)      m_actEdit->setEnabled(!editsBlocked);
    if (m_actRotate)    m_actRotate->setEnabled(!editsBlocked);
    if (m_actScale)     m_actScale->setEnabled(!editsBlocked);
    if (m_actAddRow)    m_actAddRow->setEnabled(!editsBlocked);
    if (m_actDeleteRow) m_actDeleteRow->setEnabled(!editsBlocked);
    if (m_actPaste)     m_actPaste->setEnabled(!editsBlocked);
    // Copy stays enabled (read-only operation).
    // Undo/Redo stay enabled (they may undo into a mutable state).
}

void TimeseriesEditorDialog::onSourceModeRadioToggled_()
{
    if (m_providers.isEmpty() || !m_providers.first()) return;
    auto *p = m_providers.first().data();

    TimeseriesProvider::SourceMode newMode = p->sourceMode();
    if (m_radioInline->isChecked())          newMode = TimeseriesProvider::SourceMode::Inline;
    else if (m_radioExternal->isChecked())   newMode = TimeseriesProvider::SourceMode::ExternalFile;
    else if (m_radioGeopackage->isChecked()) newMode = TimeseriesProvider::SourceMode::GeopackageObserved;

    if (newMode == p->sourceMode()) {
        refreshSourceModeCardForProvider_();
        return;
    }
    if (m_undoStack)
        m_undoStack->push(new openswmmvis::timeseries::ChangeSourceModeCommand(p, newMode));
    else
        p->setSourceMode(newMode);
    refreshSourceModeCardForProvider_();
}

int TimeseriesEditorDialog::linkExternalFile(const QString &path, const QString &columnSelector)
{
    if (m_providers.isEmpty() || !m_providers.first()) return 0;
    auto *p = m_providers.first().data();
    QStringList headers;
    const int n = loadExternalFileIntoProvider_(p, path, columnSelector, &headers);
    if (m_extColumnCombo) {
        QSignalBlocker b(m_extColumnCombo);
        m_extColumnCombo->clear();
        if (headers.isEmpty()) {
            m_extColumnCombo->addItem(tr("(no header — single column)"));
        } else {
            for (const QString &h : std::as_const(headers))
                m_extColumnCombo->addItem(h);
            if (!columnSelector.isEmpty()) {
                const int idx = headers.indexOf(columnSelector);
                if (idx >= 0) m_extColumnCombo->setCurrentIndex(idx);
            }
        }
    }
    refreshSourceModeCardForProvider_();
    return n;
}

void TimeseriesEditorDialog::onBrowseExternalFile_()
{
    if (m_providers.isEmpty() || !m_providers.first()) return;
    auto *p = m_providers.first().data();

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose timeseries file"),
        p->filePath().isEmpty() ? QString() : QFileInfo(p->filePath()).absolutePath(),
        tr("Timeseries (*.csv *.tsv *.dat *.txt);;All files (*)"));
    if (path.isEmpty()) return;

    QStringList headers;
    const int n = loadExternalFileIntoProvider_(p, path, /*column=*/QString(), &headers);

    // Populate the column-selector combo from the header row.
    {
        QSignalBlocker b(m_extColumnCombo);
        m_extColumnCombo->clear();
        if (headers.isEmpty()) {
            m_extColumnCombo->addItem(tr("(no header — single column)"));
        } else {
            for (const QString &h : std::as_const(headers))
                m_extColumnCombo->addItem(h);
        }
    }
    refreshSourceModeCardForProvider_();
    if (m_status) {
        if (n > 0)
            m_status->showMessage(tr("Loaded %1 point(s) from %2").arg(n).arg(QFileInfo(path).fileName()), 4000);
        else
            m_status->showMessage(tr("File parse returned 0 points — check format."), 4000);
    }
}

void TimeseriesEditorDialog::onColumnSelectorChanged_(int index)
{
    if (index < 0 || m_providers.isEmpty() || !m_providers.first()) return;
    auto *p = m_providers.first().data();
    if (p->filePath().isEmpty()) return;

    const QString col = m_extColumnCombo->itemText(index);
    loadExternalFileIntoProvider_(p, p->filePath(), col);
    refreshSourceModeCardForProvider_();
}

void TimeseriesEditorDialog::onReloadExternalFile_()
{
    if (m_providers.isEmpty() || !m_providers.first()) return;
    auto *p = m_providers.first().data();
    if (p->filePath().isEmpty()) return;
    const QString col = m_extColumnCombo ? m_extColumnCombo->currentText() : QString();
    const int n = loadExternalFileIntoProvider_(p, p->filePath(), col);
    refreshSourceModeCardForProvider_();
    if (m_status)
        m_status->showMessage(tr("Reloaded %1 point(s)").arg(n), 3000);
}

void TimeseriesEditorDialog::onDetachToInline_()
{
    if (m_providers.isEmpty() || !m_providers.first()) return;
    auto *p = m_providers.first().data();
    // Strip file metadata + flip to Inline. Points stay in the provider
    // (they were loaded into the read-through cache by loadExternalFile…).
    if (m_undoStack)
        m_undoStack->beginMacro(tr("Detach to Inline"));
    p->setFileSource(QString(), QString(), QDateTime());
    if (m_undoStack)
        m_undoStack->push(new openswmmvis::timeseries::ChangeSourceModeCommand(
            p, TimeseriesProvider::SourceMode::Inline));
    else
        p->setSourceMode(TimeseriesProvider::SourceMode::Inline);
    if (m_undoStack) m_undoStack->endMacro();
    refreshSourceModeCardForProvider_();
    if (m_status) m_status->showMessage(tr("Detached from file — points are now inline."), 4000);
}

int TimeseriesEditorDialog::loadExternalFileIntoProvider_(
    TimeseriesProvider *p, const QString &path,
    const QString &columnSelector, QStringList *columnHeadersOut)
{
    // Re-entrancy counter — a value >1 means a signal slot fired the load
    // again while we were still inside it (a feedback loop is a plausible
    // stall cause, e.g. populating the column combo refires onColumnSelectorChanged_).
    static thread_local int s_depth = 0;
    struct DepthGuard {
        DepthGuard()  { ++s_depth; }
        ~DepthGuard() { --s_depth; }
    } depth;
    QElapsedTimer tTotal; tTotal.start();
    qCDebug(lcTsLoadDialog) << "loadExternalFileIntoProvider_ ENTER depth=" << s_depth
                            << "path=" << path
                            << "column=" << columnSelector;

    if (!p || path.isEmpty()) {
        qCDebug(lcTsLoadDialog) << "  early return: null provider or empty path";
        return 0;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcTsLoadDialog) << "  QFile::open FAILED" << f.errorString();
        return 0;
    }
    qCDebug(lcTsLoadDialog) << "  file size =" << f.size() << "bytes";
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    // SWMM .dat files use a different lexical syntax (whitespace-or-comma
    // separated, no header, with cached-date semantics across 2-token vs
    // 3-token rows). Branch to the engine-faithful parser; CSV / TSV / TXT
    // continue through the header-aware path below.
    if (QFileInfo(path).suffix().compare(QStringLiteral("dat"),
                                          Qt::CaseInsensitive) == 0) {
        // .dat has no header — fabricate one placeholder column name so the
        // grid / chart / column selector all have something to bind to.
        const QStringList headers{ QStringLiteral("value") };
        if (columnHeadersOut) *columnHeadersOut = headers;

        openswmmvis::io::SwmmDatState state;
        // Anchor for "elapsed-only" .dat files (no date tokens) — the engine
        // uses the simulation start date, but the GUI has no equivalent
        // here. 2000-01-01 keeps the points in a reasonable, non-1899 range
        // so the chart's date axis renders intelligibly.
        state.lastDateJulian = openswmmvis::core::qDateTimeToSwmmDateTime(
            QDateTime(QDate(2000, 1, 1), QTime(0, 0), Qt::UTC));

        QVector<TimeseriesPoint> pts;
        QElapsedTimer tParse; tParse.start();
        qint64 linesRead = 0, linesAccepted = 0, lastLogLines = 0;
        qint64 lastLogElapsed = 0;
        while (!in.atEnd()) {
            const QString line = in.readLine();
            ++linesRead;
            double tJ = 0.0, v = 0.0;
            if (openswmmvis::io::parseSwmmDatLine(line, state, tJ, v)) {
                pts.push_back({openswmmvis::core::swmmDateTimeToQDateTime(tJ), v});
                ++linesAccepted;
            }
            // Heartbeat every ~1s so we can see *where* a hang occurs.
            const qint64 nowMs = tParse.elapsed();
            if (nowMs - lastLogElapsed >= 1000) {
                qCDebug(lcTsLoadDialog) << "  parse heartbeat: linesRead=" << linesRead
                                        << "accepted=" << linesAccepted
                                        << "lines/sec (last window)="
                                        << (linesRead - lastLogLines);
                lastLogLines = linesRead;
                lastLogElapsed = nowMs;
            }
        }
        qCDebug(lcTsLoadDialog) << "  .dat parse DONE linesRead=" << linesRead
                                << "accepted=" << linesAccepted
                                << "in" << tParse.elapsed() << "ms";

        QElapsedTimer tSet; tSet.start();
        p->setFileSource(path, /*columnSelector=*/QString(),
                          QFileInfo(path).lastModified());
        qCDebug(lcTsLoadDialog) << "  calling setAllPoints with" << pts.size() << "points";
        p->setAllPoints(pts);
        qCDebug(lcTsLoadDialog) << "  setAllPoints + setFileSource took" << tSet.elapsed() << "ms";

        QElapsedTimer tMode; tMode.start();
        p->setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);
        qCDebug(lcTsLoadDialog) << "  setSourceMode took" << tMode.elapsed() << "ms";
        qCDebug(lcTsLoadDialog) << "loadExternalFileIntoProvider_ EXIT (.dat) total"
                                << tTotal.elapsed() << "ms";
        return pts.size();
    }

    QString headerLine;
    while (!in.atEnd()) {
        headerLine = in.readLine();
        const QString tr = headerLine.trimmed();
        if (!tr.isEmpty() && !tr.startsWith('#') && !tr.startsWith(';'))
            break;
    }
    if (headerLine.isEmpty()) return 0;

    const QChar delim = openswmmvis::io::guessDelimiter(headerLine);

    // Decide whether headerLine is itself a data row by trying to parse it
    // as one. If parse succeeds, fabricate "col_N" headers and treat the
    // line as data (mirrors ObservedCsvRunLayer's heuristic).
    double probeT = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> probeVals;
    const bool headerLooksLikeData =
        openswmmvis::io::parseRow(headerLine, delim, probeT, probeVals,
                                   std::numeric_limits<double>::quiet_NaN());

    QStringList headers;
    const QStringList rawCells = headerLine.split(delim);
    if (headerLooksLikeData) {
        for (int c = 1; c < rawCells.size(); ++c)
            headers << QStringLiteral("col_%1").arg(c);
    } else {
        for (int c = 1; c < rawCells.size(); ++c)
            headers << rawCells.at(c).trimmed();
    }
    if (columnHeadersOut) *columnHeadersOut = headers;

    // Resolve the active column index from selector ("" → 0; otherwise match
    // by header text; if no match, fall back to 0).
    int activeColIdx = 0;
    if (!columnSelector.isEmpty()) {
        const int idx = headers.indexOf(columnSelector);
        if (idx >= 0) activeColIdx = idx;
    }

    QVector<TimeseriesPoint> pts;
    if (headerLooksLikeData && activeColIdx < static_cast<int>(probeVals.size())) {
        pts.push_back({openswmmvis::core::swmmDateTimeToQDateTime(probeT),
                       probeVals[static_cast<std::size_t>(activeColIdx)]});
    }

    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith(';'))
            continue;
        double tJ = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> vals;
        if (!openswmmvis::io::parseRow(trimmed, delim, tJ, vals,
                                       std::numeric_limits<double>::quiet_NaN()))
            continue;
        if (activeColIdx >= static_cast<int>(vals.size())) continue;
        pts.push_back({openswmmvis::core::swmmDateTimeToQDateTime(tJ),
                       vals[static_cast<std::size_t>(activeColIdx)]});
    }

    // Record file metadata + push points into the provider (via setAllPoints
    // so the monotone-time invariant is validated). Source mode flips to
    // ExternalFile so the grid + chart go read-only.
    p->setFileSource(path, columnSelector, QFileInfo(path).lastModified());
    p->setAllPoints(pts);
    p->setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);
    return pts.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// Transform panel — numeric Rotate / Scale (Phase 6.7.3.5 follow-up)
// ─────────────────────────────────────────────────────────────────────────────

void TimeseriesEditorDialog::buildTransformPanel_()
{
    auto *outer = qobject_cast<QVBoxLayout *>(layout());
    if (!outer) return;

    m_transformPanel = new QFrame(this);
    m_transformPanel->setFrameShape(QFrame::StyledPanel);
    m_transformPanel->setObjectName(QStringLiteral("transformPanel"));

    auto *row = new QHBoxLayout(m_transformPanel);
    row->setContentsMargins(8, 4, 8, 4);

    // ── Rotate group ────────────────────────────────────────────────────────
    m_rotateGroup = new QFrame(m_transformPanel);
    auto *rotLay = new QHBoxLayout(m_rotateGroup);
    rotLay->setContentsMargins(0, 0, 0, 0);
    rotLay->addWidget(new QLabel(tr("Rotate:"), m_rotateGroup));

    m_rotateUseCentroid = new QCheckBox(tr("centroid pivot"), m_rotateGroup);
    m_rotateUseCentroid->setChecked(true);
    rotLay->addWidget(m_rotateUseCentroid);

    rotLay->addWidget(new QLabel(tr("pivot t:"), m_rotateGroup));
    m_rotatePivotTimeEdit = new QDateTimeEdit(m_rotateGroup);
    m_rotatePivotTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_rotatePivotTimeEdit->setEnabled(false);   // centroid by default
    rotLay->addWidget(m_rotatePivotTimeEdit);

    rotLay->addWidget(new QLabel(tr("v:"), m_rotateGroup));
    m_rotatePivotValueEdit = new QDoubleSpinBox(m_rotateGroup);
    m_rotatePivotValueEdit->setRange(-1e12, 1e12);
    m_rotatePivotValueEdit->setDecimals(6);
    m_rotatePivotValueEdit->setEnabled(false);
    rotLay->addWidget(m_rotatePivotValueEdit);

    rotLay->addWidget(new QLabel(tr("angle°:"), m_rotateGroup));
    m_rotateAngleEdit = new QDoubleSpinBox(m_rotateGroup);
    m_rotateAngleEdit->setRange(-360.0, 360.0);
    m_rotateAngleEdit->setDecimals(2);
    rotLay->addWidget(m_rotateAngleEdit);

    m_rotateApplyBtn = new QPushButton(tr("Apply"), m_rotateGroup);
    rotLay->addWidget(m_rotateApplyBtn);

    row->addWidget(m_rotateGroup);

    // ── Scale group ─────────────────────────────────────────────────────────
    m_scaleGroup = new QFrame(m_transformPanel);
    auto *sclLay = new QHBoxLayout(m_scaleGroup);
    sclLay->setContentsMargins(0, 0, 0, 0);
    sclLay->addWidget(new QLabel(tr("Scale:"), m_scaleGroup));

    m_scaleUseCentroid = new QCheckBox(tr("centroid anchor"), m_scaleGroup);
    m_scaleUseCentroid->setChecked(true);
    sclLay->addWidget(m_scaleUseCentroid);

    sclLay->addWidget(new QLabel(tr("anchor t:"), m_scaleGroup));
    m_scaleAnchorTimeEdit = new QDateTimeEdit(m_scaleGroup);
    m_scaleAnchorTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_scaleAnchorTimeEdit->setEnabled(false);
    sclLay->addWidget(m_scaleAnchorTimeEdit);

    sclLay->addWidget(new QLabel(tr("v:"), m_scaleGroup));
    m_scaleAnchorValueEdit = new QDoubleSpinBox(m_scaleGroup);
    m_scaleAnchorValueEdit->setRange(-1e12, 1e12);
    m_scaleAnchorValueEdit->setDecimals(6);
    m_scaleAnchorValueEdit->setEnabled(false);
    sclLay->addWidget(m_scaleAnchorValueEdit);

    sclLay->addWidget(new QLabel(tr("scaleX:"), m_scaleGroup));
    m_scaleXEdit = new QDoubleSpinBox(m_scaleGroup);
    m_scaleXEdit->setRange(-1e6, 1e6);
    m_scaleXEdit->setDecimals(4);
    m_scaleXEdit->setValue(1.0);
    sclLay->addWidget(m_scaleXEdit);

    sclLay->addWidget(new QLabel(tr("scaleY:"), m_scaleGroup));
    m_scaleYEdit = new QDoubleSpinBox(m_scaleGroup);
    m_scaleYEdit->setRange(-1e6, 1e6);
    m_scaleYEdit->setDecimals(4);
    m_scaleYEdit->setValue(1.0);
    sclLay->addWidget(m_scaleYEdit);

    m_scaleApplyBtn = new QPushButton(tr("Apply"), m_scaleGroup);
    sclLay->addWidget(m_scaleApplyBtn);

    row->addWidget(m_scaleGroup);
    row->addStretch(1);

    // Insert below source-mode card (index 2: toolbar=0, sourceCard=1).
    outer->insertWidget(2, m_transformPanel);
    m_transformPanel->hide();

    // Connect "use centroid" toggles → pivot inputs enabled.
    connect(m_rotateUseCentroid, &QCheckBox::toggled, this, [this](bool on) {
        m_rotatePivotTimeEdit->setEnabled(!on);
        m_rotatePivotValueEdit->setEnabled(!on);
    });
    connect(m_scaleUseCentroid, &QCheckBox::toggled, this, [this](bool on) {
        m_scaleAnchorTimeEdit->setEnabled(!on);
        m_scaleAnchorValueEdit->setEnabled(!on);
    });

    connect(m_rotateApplyBtn, &QPushButton::clicked, this, &TimeseriesEditorDialog::onApplyRotateClicked_);
    connect(m_scaleApplyBtn,  &QPushButton::clicked, this, &TimeseriesEditorDialog::onApplyScaleClicked_);
}

void TimeseriesEditorDialog::onChartEditModeChanged_()
{
    if (!m_transformPanel || !m_chartView) return;
    const auto m = m_chartView->editMode();
    using EM = TimeseriesEditChartView::EditMode;
    const bool showRot = (m == EM::RotatePoints);
    const bool showScl = (m == EM::ScalePoints);
    m_rotateGroup->setVisible(showRot);
    m_scaleGroup->setVisible(showScl);
    m_transformPanel->setVisible(showRot || showScl);
}

namespace {

using openswmmvis::timeseries::BulkTransformCommand;
using openswmmvis::timeseries::TimeseriesPoint;
using openswmmvis::timeseries::TimeseriesProvider;

/*! \brief Snapshot the selected points; empty selection ⇒ all points. */
QVector<int> resolveSelectionIndices(const QVector<int>& chartSelection, int pointCount)
{
    if (!chartSelection.isEmpty()) return chartSelection;
    QVector<int> all;
    all.reserve(pointCount);
    for (int i = 0; i < pointCount; ++i) all.push_back(i);
    return all;
}

/*! \brief Compute the centroid of the selected (time-ms, value) pairs. */
QPair<double, double> centroidOf(const TimeseriesProvider *p, const QVector<int>& sel)
{
    if (sel.isEmpty()) return {0.0, 0.0};
    double tSum = 0.0, vSum = 0.0;
    for (int i : sel) {
        if (i < 0 || i >= p->pointCount()) continue;
        tSum += static_cast<double>(p->pointAt(i).time.toMSecsSinceEpoch());
        vSum += p->pointAt(i).value;
    }
    return {tSum / sel.size(), vSum / sel.size()};
}

} // namespace

void TimeseriesEditorDialog::onApplyRotateClicked_()
{
    if (m_providers.isEmpty() || !m_providers.first() || !m_chartView) return;
    auto *p = m_providers.first().data();
    if (p->sourceMode() == TimeseriesProvider::SourceMode::ExternalFile) {
        if (m_status) m_status->showMessage(
            tr("Rotate disabled: this series is linked to an external file."), 4000);
        return;
    }

    const QVector<int> sel = resolveSelectionIndices(m_chartView->selectedIndices(),
                                                      p->pointCount());
    if (sel.isEmpty()) return;

    // Pivot: centroid (default) or numeric. Times work in ms-since-epoch.
    double pivotT = 0.0, pivotV = 0.0;
    if (m_rotateUseCentroid->isChecked()) {
        const auto c = centroidOf(p, sel);
        pivotT = c.first; pivotV = c.second;
    } else {
        pivotT = static_cast<double>(m_rotatePivotTimeEdit->dateTime().toMSecsSinceEpoch());
        pivotV = m_rotatePivotValueEdit->value();
    }
    constexpr double kPi = 3.14159265358979323846;
    const double angleRad = m_rotateAngleEdit->value() * (kPi / 180.0);
    const double cosT = std::cos(angleRad);
    const double sinT = std::sin(angleRad);

    // Build the full transformed point list (selected points rotated; others unchanged).
    const auto currentPts = p->points();
    QVector<TimeseriesPoint> newPts = currentPts;
    const QSet<int> selSet(sel.begin(), sel.end());
    for (int i : sel) {
        if (i < 0 || i >= newPts.size()) continue;
        const double t = static_cast<double>(currentPts.at(i).time.toMSecsSinceEpoch());
        const double v = currentPts.at(i).value;
        const double dt = t - pivotT;
        const double dv = v - pivotV;
        const double newT = pivotT + dt * cosT - dv * sinT;
        const double newV = pivotV + dt * sinT + dv * cosT;
        newPts[i].time = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(newT), Qt::UTC);
        newPts[i].value = newV;
    }

    // Sort: rotation can reorder rows; we re-sort by time before pushing to the
    // provider so the monotone gate sees a sorted candidate. If unique-time
    // invariant is violated post-sort the provider rejects atomically.
    std::sort(newPts.begin(), newPts.end(),
              [](const TimeseriesPoint& a, const TimeseriesPoint& b) { return a.time < b.time; });

    if (m_undoStack)
        m_undoStack->push(new BulkTransformCommand(p, std::move(newPts),
                                                    tr("Rotate %1°").arg(m_rotateAngleEdit->value())));
    else
        p->setAllPoints(std::move(newPts));
    Q_UNUSED(selSet);
}

void TimeseriesEditorDialog::onApplyScaleClicked_()
{
    if (m_providers.isEmpty() || !m_providers.first() || !m_chartView) return;
    auto *p = m_providers.first().data();
    if (p->sourceMode() == TimeseriesProvider::SourceMode::ExternalFile) {
        if (m_status) m_status->showMessage(
            tr("Scale disabled: this series is linked to an external file."), 4000);
        return;
    }

    const QVector<int> sel = resolveSelectionIndices(m_chartView->selectedIndices(),
                                                      p->pointCount());
    if (sel.isEmpty()) return;

    double anchorT = 0.0, anchorV = 0.0;
    if (m_scaleUseCentroid->isChecked()) {
        const auto c = centroidOf(p, sel);
        anchorT = c.first; anchorV = c.second;
    } else {
        anchorT = static_cast<double>(m_scaleAnchorTimeEdit->dateTime().toMSecsSinceEpoch());
        anchorV = m_scaleAnchorValueEdit->value();
    }
    const double sX = m_scaleXEdit->value();
    const double sY = m_scaleYEdit->value();

    const auto currentPts = p->points();
    QVector<TimeseriesPoint> newPts = currentPts;
    for (int i : sel) {
        if (i < 0 || i >= newPts.size()) continue;
        const double t = static_cast<double>(currentPts.at(i).time.toMSecsSinceEpoch());
        const double v = currentPts.at(i).value;
        const double newT = anchorT + (t - anchorT) * sX;
        const double newV = anchorV + (v - anchorV) * sY;
        newPts[i].time = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(newT), Qt::UTC);
        newPts[i].value = newV;
    }
    std::sort(newPts.begin(), newPts.end(),
              [](const TimeseriesPoint& a, const TimeseriesPoint& b) { return a.time < b.time; });

    if (m_undoStack)
        m_undoStack->push(new BulkTransformCommand(p, std::move(newPts),
                                                    tr("Scale (×%1, ×%2)").arg(sX).arg(sY)));
    else
        p->setAllPoints(std::move(newPts));
}

// ─────────────────────────────────────────────────────────────────────────────
// List pane CRUD (Phase 6.7.3-CRUD) — mirrors HydrographGroupEditor's three-
// pane convention: left list of all series + filter + New/Delete/Rename
// buttons; selection drives which provider the grid + chart are bound to.
// ─────────────────────────────────────────────────────────────────────────────

void TimeseriesEditorDialog::buildListPane_()
{
    if (!m_splitter || !m_registry) return;

    m_listPane = new QWidget(m_splitter);
    auto *v = new QVBoxLayout(m_listPane);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    // Filter line edit (matches HydrographGroupEditor convention).
    m_listFilterEdit = new QLineEdit(m_listPane);
    m_listFilterEdit->setPlaceholderText(tr("Search series…"));
    m_listFilterEdit->setClearButtonEnabled(true);
    v->addWidget(m_listFilterEdit);

    // List view over a filter proxy that wraps the registry-backed model.
    m_listModel = new TimeseriesListModel(this);
    m_listModel->setRegistry(m_registry);
    m_listProxy = new QSortFilterProxyModel(this);
    m_listProxy->setSourceModel(m_listModel);
    m_listProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_listProxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_listProxy->sort(0, Qt::AscendingOrder);

    m_listView = new QListView(m_listPane);
    m_listView->setModel(m_listProxy);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setEditTriggers(QAbstractItemView::SelectedClicked
                                 | QAbstractItemView::EditKeyPressed);
    m_listView->setAlternatingRowColors(true);
    v->addWidget(m_listView, /*stretch=*/1);

    // Bottom button row: New / Delete / Rename.
    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(2);

    auto makeBtn = [this](const QString &text, const QString &tip) {
        auto *b = new QToolButton(m_listPane);
        b->setText(text);
        b->setToolTip(tip);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setAutoRaise(true);
        return b;
    };
    m_listNewBtn    = makeBtn(QStringLiteral("+ New"),
                                tr("Create a new time series"));
    m_listDeleteBtn = makeBtn(QStringLiteral("− Delete"),
                                tr("Delete the selected time series"));
    m_listRenameBtn = makeBtn(QStringLiteral("Rename"),
                                tr("Rename the selected time series"));
    btnRow->addWidget(m_listNewBtn);
    btnRow->addWidget(m_listDeleteBtn);
    btnRow->addWidget(m_listRenameBtn);
    btnRow->addStretch(1);
    v->addLayout(btnRow);

    // Insert as the leftmost pane in the splitter.
    m_splitter->insertWidget(0, m_listPane);

    // Step D — Explicit initial sizes (override sizeHints so the chart
    // pane doesn't collapse to a sliver) + stretch factors that grow the
    // chart preferentially on dialog resize. setSizes is honored as the
    // initial layout; stretch factors govern subsequent resize behaviour.
    // QSettings restoreState in Step E.2 overrides these on reopen.
    m_listPane->setMinimumWidth(180);
    if (m_chartView) m_chartView->setMinimumWidth(360);
    m_splitter->setSizes({200, 300, 800});
    m_splitter->setStretchFactor(0, 0);   // list — narrow, fixed-ish
    m_splitter->setStretchFactor(1, 1);   // grid
    m_splitter->setStretchFactor(2, 3);   // chart — soaks up extra width

    // Wire signals.
    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &, const QModelIndex &) {
                onListSelectionChanged_();
            });
    connect(m_listFilterEdit, &QLineEdit::textChanged,
            this, &TimeseriesEditorDialog::onListFilterChanged_);
    connect(m_listNewBtn,    &QToolButton::clicked,
            this, &TimeseriesEditorDialog::onNewSeriesClicked_);
    connect(m_listDeleteBtn, &QToolButton::clicked,
            this, &TimeseriesEditorDialog::onDeleteSeriesClicked_);
    connect(m_listRenameBtn, &QToolButton::clicked,
            this, &TimeseriesEditorDialog::onRenameSeriesClicked_);

    // Initial Delete/Rename enabled state — no selection yet.
    m_listDeleteBtn->setEnabled(false);
    m_listRenameBtn->setEnabled(false);
}

void TimeseriesEditorDialog::rebindActiveProvider_(TimeseriesProvider *p)
{
    // Step F — dispose the outgoing provider's in-memory cache if it's
    // ExternalFile-backed. The file path stays on the provider, so a future
    // re-bind (or the user clicking the Reload button) re-reads from disk.
    // Inline / Gpkg providers are unaffected (disposePointCache is a no-op).
    if (!m_providers.isEmpty()) {
        if (auto *prev = m_providers.first().data())
            if (prev != p) prev->disposePointCache();
    }

    if (!p) {
        m_providers.clear();
        if (m_tableModel) m_tableModel->setProviders({});
        if (m_chartView)  m_chartView->setProvider(nullptr);
        updateStatusBar_();
        refreshSourceModeCardForProvider_();
        return;
    }
    m_providers.clear();
    m_providers.push_back(QPointer<TimeseriesProvider>(p));

    // Step F — lazy-load the incoming provider's cache if it's ExternalFile
    // and not currently resident. Inline / Gpkg always report loaded so
    // they no-op here.
    if (p->sourceMode() == TimeseriesProvider::SourceMode::ExternalFile
        && !p->isPointCacheLoaded()
        && !p->filePath().isEmpty()) {
        loadExternalFileIntoProvider_(p, p->filePath(), p->columnSelector());
    }

    if (m_tableModel) m_tableModel->setProviders({p});
    if (m_chartView)  m_chartView->setProvider(p);
    wireProviderSignals_();
    updateStatusBar_();
    refreshSourceModeCardForProvider_();
    setWindowTitle(tr("Time Series Editor — %1").arg(p->name()));

    // Step B — fit chart axes to the newly bound series on every switch.
    // Skip when the series has no points (degenerate axis range otherwise).
    if (m_chartView && p->pointCount() > 0)
        m_chartView->zoomToExtent();
}

void TimeseriesEditorDialog::onListSelectionChanged_()
{
    if (!m_listView || !m_listModel || !m_listProxy) return;
    const QModelIndex prx = m_listView->currentIndex();
    const bool hasSelection = prx.isValid();
    if (m_listDeleteBtn) m_listDeleteBtn->setEnabled(hasSelection);
    if (m_listRenameBtn) m_listRenameBtn->setEnabled(hasSelection);
    if (!hasSelection) {
        rebindActiveProvider_(nullptr);
        return;
    }
    const QModelIndex src = m_listProxy->mapToSource(prx);
    auto *p = m_listModel->providerAt(src.row());
    rebindActiveProvider_(p);
}

void TimeseriesEditorDialog::onListFilterChanged_(const QString &text)
{
    if (m_listProxy) m_listProxy->setFilterFixedString(text);
}

void TimeseriesEditorDialog::onNewSeriesClicked_()
{
    if (!m_registry) return;
    // Create with a default-unique name and start inline-rename on the new
    // row. The list view already routes setData through the registry.
    QString name = QStringLiteral("TS1");
    for (int i = 2; i < 9999 && m_registry->hasName(name); ++i)
        name = QStringLiteral("TS%1").arg(i);
    auto *p = m_registry->create(name);
    if (!p) return;
    const int row = m_listModel ? m_listModel->rowOf(p) : -1;
    if (row >= 0 && m_listView) {
        const QModelIndex src = m_listModel->index(row, 0);
        const QModelIndex prx = m_listProxy
            ? m_listProxy->mapFromSource(src) : src;
        m_listView->setCurrentIndex(prx);
        m_listView->edit(prx);
    }
}

void TimeseriesEditorDialog::onDeleteSeriesClicked_()
{
    if (!m_registry || !m_listView) return;
    const QModelIndex prx = m_listView->currentIndex();
    if (!prx.isValid()) return;
    const QModelIndex src = m_listProxy->mapToSource(prx);
    auto *p = m_listModel->providerAt(src.row());
    if (!p) return;

    const auto choice = QMessageBox::question(this, tr("Delete time series"),
        tr("Delete time series “%1”? This cannot be undone via the editor's Undo stack.").arg(p->name()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes) return;

    m_registry->remove(p);   // also fires providerAboutToBeRemoved → list updates
    // Selection auto-clears; rebind to whatever's now-current (or null).
    onListSelectionChanged_();
}

void TimeseriesEditorDialog::onRenameSeriesClicked_()
{
    if (!m_listView) return;
    const QModelIndex prx = m_listView->currentIndex();
    if (prx.isValid()) m_listView->edit(prx);
}

} // namespace openswmmvis::ui
