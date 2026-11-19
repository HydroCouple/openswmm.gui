/**
 * @file      transecteditordialog.cpp
 * @brief     Layout (left → right).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/transecteditordialog.h"
#include "core/swmmidentifier.h"
#include "ui/theme/iconfactory.h"
#include "ui/dialogs/dialoglayoutpersistence.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/editors/dataobjectstructureedit.h"
#include "ui/models/draftmarkproxymodel.h"

#include "layers/swmmmodellayer.h" // complete type required by QPointer<SWMMModelLayer>
#include "transect/transectprovider.h"
#include "transect/transectregistry.h"
#include "transect/transectundocommands.h"
#include "ui/dialogs/transectpropertybag.h"
#include "ui/models/transectlistmodel.h"
#include "ui/models/transectstationtablemodel.h"
#include "ui/widgets/transectchartview.h"
#include "ui/util/tableclipboard.h"

#include <qpropertyitem.h>
#include <qpropertyitemdelegate.h>
#include <qpropertymodel.h>

#include <QAction>
#include <QApplication>
#include <QChart>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QTextEdit>
#include <QToolBar>
#include <QTreeView>
#include <QUndoStack>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_infrastructure.h>

namespace openswmmvis::ui
{

namespace
{
/*! Replace a transect draft's whole content (a pasted station block) as one
 *  step on the editor's own undo stack. */
class SetTransectDraftCommand : public QUndoCommand
{
public:
    SetTransectDraftCommand(openswmmvis::transect::TransectProvider *draft,
                            openswmmvis::transect::TransectProvider::Params after,
                            const QString &text)
        : QUndoCommand(text), m_draft(draft), m_before(draft->params()), m_after(std::move(after))
    {
    }
    void redo() override { m_draft->setParams(m_after); }
    void undo() override { m_draft->setParams(m_before); }

private:
    openswmmvis::transect::TransectProvider *m_draft;
    openswmmvis::transect::TransectProvider::Params m_before, m_after;
};

/*! Put \p target into \p live and its engine transect, read the engine back
 *  and say whether the model kept it. Used by Apply, its undo / redo, and to
 *  restore a deleted transect's settings when Undo brings it back. */
bool writeTransect(openswmmvis::transect::TransectRegistry *registry,
                   openswmmvis::transect::TransectProvider *live,
                   const openswmmvis::transect::TransectProvider::Params &target)
{
    live->setParams(target); // views and the project's edited mark follow its signals
    if (void *engine = registry->engineHandle()) {
        registry->saveToEngine(engine, live->name());
        registry->loadFromEngine(engine, live->name()); // what the model holds now
    }
    return live->params().sameAs(target, 1e-9);
}
} // namespace

using openswmmvis::transect::TransectProvider;
using openswmmvis::transect::TransectRegistry;

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

TransectEditorDialog::TransectEditorDialog(TransectRegistry *registry, SWMMModelLayer *layer,
                                           QUndoStack *undoStack, QWidget *parent)
    : QDialog(parent, openswmmvis::ui::floatingPanelFlags()), m_registry(registry), m_layer(layer),
      m_undoStack(undoStack)
{
    setWindowTitle(tr("Transect Editor"));
    setModal(false);
    // Tall default so the middle pane's 10-row property tree + station table
    // are both visible without forcing the user to resize.
    resize(1180, 820);

    buildUi_();
    buildToolbar_();

    if (m_registry) {
        connect(m_registry, &TransectRegistry::providerAdded, this,
                &TransectEditorDialog::onProviderAdded_);
        connect(m_registry, &TransectRegistry::providerAboutToBeRemoved, this,
                &TransectEditorDialog::onProviderRemoved_);
        connect(m_registry, &TransectRegistry::providerRenamed, this,
                &TransectEditorDialog::onProviderRenamed_);
    }
    if (m_listModel)
        m_listModel->setRegistry(m_registry);

    // Initial selection.
    if (m_registry && m_registry->providerCount() > 0 && m_listView)
        m_listView->setCurrentIndex(viewIndexOf(m_listView, m_listModel->index(0)));
    else
        bindProvider_(nullptr);

    // Iteration 2 (D2) — geometry/splitter persistence via the app-wide
    // DialogLayoutWatcher (save on Hide/Close instead of the old
    // destructor-time write): naming is the wiring.
    setObjectName(QStringLiteral("TransectEditorDialog"));
    if (m_splitter)
        m_splitter->setObjectName(QStringLiteral("main"));
    setAccessibleDescription(
        tr("Changes are held until you choose Apply, which writes them to the model as one "
           "undoable step. Undo and Redo in this editor step through unapplied changes. Adding, "
           "deleting and renaming take effect at once."));
    if (m_table)
        m_table->setAccessibleName(tr("Transect values"));
    if (m_listView)
        m_listView->setAccessibleName(tr("Transect objects"));
    if (m_chartView)
        m_chartView->setAccessibleName(tr("Transect preview"));
    if (registry)
        connect(registry, &QObject::destroyed, this, &TransectEditorDialog::invalidateContext);
    if (undoStack)
        connect(undoStack, &QObject::destroyed, this, &TransectEditorDialog::invalidateContext);
    if (layer) {
        connect(layer, &QObject::destroyed, this, &TransectEditorDialog::invalidateContext);
        connect(layer, SIGNAL(engineAboutToClose()), this, SLOT(invalidateContext()));
    }
}

TransectEditorDialog::~TransectEditorDialog() = default;

void TransectEditorDialog::invalidateContext()
{
    if (!m_contextValid)
        return;
    m_contextValid = false;
    setEnabled(false);
    if (m_registry)
        m_registry->disconnect(this);
    m_registry.clear();
    m_undoStack.clear();
    m_layer.clear();
    for (const auto &d : std::as_const(m_drafts)) // the model is going away
        if (d.live)
            d.live->disconnect(this);
    m_drafts.clear();
    bindProvider_(nullptr);
    if (m_listModel)
        m_listModel->setRegistry(nullptr);
    reject();
}

TransectEditorDialog *TransectEditorDialog::createNew(TransectRegistry *registry,
                                                      SWMMModelLayer *layer, QUndoStack *undoStack,
                                                      QWidget *parent)
{
    auto *dlg = new TransectEditorDialog(registry, layer, undoStack, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Transect"));
    dlg->invokeNew();
    return dlg;
}

void TransectEditorDialog::openForTransect(const QString &name)
{
    show();
    raise();
    activateWindow();
    if (!m_registry || name.isEmpty())
        return;
    auto *p = m_registry->findByName(name);
    if (p)
        selectProviderInList_(p);
}

TransectProvider *TransectEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

QString TransectEditorDialog::pickTransect(TransectRegistry *registry, SWMMModelLayer *layer,
                                           QUndoStack *undoStack, const QString &initialName,
                                           QWidget *parent)
{
    if (!registry)
        return {};

    TransectEditorDialog dlg(registry, layer, undoStack, parent);
    dlg.setModal(true);
    if (auto *provider = registry->findByName(initialName)) {
        dlg.setWindowTitle(tr("Edit Transect"));
        dlg.selectProviderInList_(provider);
    } else {
        dlg.setWindowTitle(tr("Select Transect"));
        dlg.m_listView->selectionModel()->clear();
        dlg.bindProvider_(nullptr);
    }
    dlg.exec(); // closing with a draft asks Apply / Discard first

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI assembly
// ─────────────────────────────────────────────────────────────────────────────

void TransectEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    outer->addWidget(m_splitter, 1);

    // ── Left pane: transect list + Add/Delete ───────────────────────────────
    {
        auto *host = new QWidget(m_splitter);
        auto *lay = new QVBoxLayout(host);
        lay->setContentsMargins(8, 8, 8, 8);
        lay->addWidget(new QLabel(tr("Transects"), host));

        m_listView = new QListView(host);
        m_listView->setEditTriggers(QAbstractItemView::SelectedClicked |
                                    QAbstractItemView::EditKeyPressed);
        m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_listView->setUniformItemSizes(true);
        m_listModel = new TransectListModel(this);
        // Rows whose object holds an unapplied draft read in italics.
        m_draftMarks = new DraftMarkProxyModel(this);
        m_draftMarks->setSourceModel(m_listModel);
        m_draftMarks->setHasDraft([this](int row) {
            TransectProvider *live = m_listModel->providerAt(row);
            for (const auto &d : std::as_const(m_drafts))
                if (live && d.live == live)
                    return !(d.copy->params() == live->params());
            return false;
        });
        m_listView->setModel(m_draftMarks);
        lay->addWidget(m_listView, 1);

        connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this](const QModelIndex &, const QModelIndex &) { onListSelectionChanged_(); });

        auto *btnRow = new QHBoxLayout();
        m_addBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")),
                                   tr("Add"), host);
        m_delBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")),
                                   tr("Delete"), host);
        m_addBtn->setToolTip(tr("Create a new transect"));
        m_delBtn->setToolTip(tr("Delete the selected transect"));
        connect(m_addBtn, &QPushButton::clicked, this,
                &TransectEditorDialog::onAddTransectClicked_);
        connect(m_delBtn, &QPushButton::clicked, this,
                &TransectEditorDialog::onDeleteTransectClicked_);
        btnRow->addWidget(m_addBtn);
        btnRow->addStretch(1);
        btnRow->addWidget(m_delBtn);
        lay->addLayout(btnRow);

        m_splitter->addWidget(host);
    }

    // ── Middle pane: name + comments + property tree + station table ────────
    {
        auto *host = new QWidget(m_splitter);
        auto *lay = new QVBoxLayout(host);
        lay->setContentsMargins(8, 8, 8, 8);

        // Name row.
        {
            auto *r = new QHBoxLayout();
            r->addWidget(new QLabel(tr("Name:"), host));
            m_nameEdit = new QLineEdit(host);
            m_nameEdit->setPlaceholderText(tr("Transect name"));
            r->addWidget(m_nameEdit, 1);
            connect(m_nameEdit, &QLineEdit::editingFinished, this,
                    &TransectEditorDialog::onNameEdited_);
            lay->addLayout(r);
        }

        // Comments.
        lay->addWidget(new QLabel(tr("Description / comments:"), host));
        m_commentsEdit = new QTextEdit(host);
        m_commentsEdit->setAcceptRichText(true);
        m_commentsEdit->setMaximumHeight(96);
        connect(m_commentsEdit, &QTextEdit::textChanged, this,
                &TransectEditorDialog::onCommentsEdited_);
        lay->addWidget(m_commentsEdit);

        // Property tree (QPropertyModel-backed). The bag exposes 10 properties
        // across four groups (Roughness, Bank Stations, Encroachment Stations,
        // Modifiers). At default dialog height the tree must be tall enough
        // to show every row without scrolling — otherwise the user can't see
        // the bank/encroachment rows that live further down the list.
        lay->addWidget(new QLabel(tr("Properties:"), host));
        m_propertyBag = new TransectPropertyBag(this);
        m_propertyTree = new QTreeView(host);
        m_propertyTree->setAlternatingRowColors(true);
        m_propertyTree->setEditTriggers(QAbstractItemView::AllEditTriggers);
        // QPropertyModel groups Q_PROPERTYs under a class-header row.
        // `rebuildPropertyTree_` reparents the tree at that class index so
        // the 10 property rows show flat — but we still need the tree to
        // *allow* expansion because future nested Q_PROPERTYs (e.g. an
        // encroachment sub-group) should be drillable.
        m_propertyTree->setRootIsDecorated(true);
        m_propertyTree->setItemsExpandable(true);
        // 10 rows × ~24 px per row + header ≈ 260 px. Give a bit of slack.
        m_propertyTree->setMinimumHeight(280);
        m_propertyTree->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        rebuildPropertyTree_();
        lay->addWidget(m_propertyTree, 2);

        // Station table.
        lay->addWidget(new QLabel(tr("Station–elevation points:"), host));
        m_table = new QTableView(host);
        m_tableModel = new TransectStationTableModel(this);
        m_table->setModel(m_tableModel);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_table->setAlternatingRowColors(true);
        // Stations must stay strictly increasing, so a pasted station block
        // replaces the points as a whole (setAllPoints validates the result);
        // elevation-only pastes run cell by cell. Rows past the end append.
        TableClipboardOptions clip;
        clip.pasteRows = [this](int firstRow, int firstColumn, const QList<QStringList> &rows,
                                QString *message) -> int {
            if (!m_edited || firstColumn != 0) // column 0 = Station
                return -1;
            QVector<openswmmvis::transect::TransectPoint> pts = m_edited->points();
            int written = 0;
            for (int r = 0; r < rows.size(); ++r) {
                bool okS = false, okE = false;
                const double s = rows[r].value(0).trimmed().toDouble(&okS);
                const double e = rows[r].value(1).trimmed().toDouble(&okE);
                const int row = firstRow + r;
                if (row < pts.size() && (okS || okE)) {
                    if (okS)
                        pts[row].station = s;
                    if (okE)
                        pts[row].elevation = e;
                } else if (row >= pts.size() && okS && okE) {
                    pts.push_back({s, e});
                } else {
                    continue;
                }
                ++written;
            }
            QString reason;
            openswmmvis::transect::TransectProvider check(QString{});
            if (written == 0) {
                *message = tr("No station / elevation values to paste.");
            } else if (!check.setAllPoints(pts, &reason)) { // same rule as the draft
                *message = tr("Paste rejected: %1").arg(reason);
                written = 0;
            } else {
                auto after = m_edited->params();
                after.points = pts;
                m_draftUndo->push(new SetTransectDraftCommand(m_edited, after, tr("Paste stations")));
                *message = tr("Pasted %n row(s).", nullptr, written);
            }
            return written;
        };
        TableClipboard::install(m_table, clip);
        // MVC: table selection drives the chart's highlight overlay so the
        // user can see which handles correspond to the selected rows.
        connect(
            m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection &, const QItemSelection &) { onTableSelectionChanged_(); });
        lay->addWidget(m_table, 2);

        auto *rowBtns = new QHBoxLayout();
        rowBtns->addStretch(1);
        m_addRowBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("AddRow")),
                                      tr("Add Row"), host);
        m_delRowBtn =
            new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("DeleteRow")),
                            tr("Delete Row(s)"), host);
        connect(m_addRowBtn, &QPushButton::clicked, this, &TransectEditorDialog::onAddRowClicked_);
        connect(m_delRowBtn, &QPushButton::clicked, this,
                &TransectEditorDialog::onDeleteRowsClicked_);
        rowBtns->addWidget(m_addRowBtn);
        rowBtns->addWidget(m_delRowBtn);
        lay->addLayout(rowBtns);

        m_splitter->addWidget(host);
    }

    // ── Right pane: toolbar + chart view ────────────────────────────────────
    {
        auto *host = new QWidget(m_splitter);
        auto *lay = new QVBoxLayout(host);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        m_toolBar = new QToolBar(host);
        m_toolBar->setIconSize(QSize(16, 16));
        lay->addWidget(m_toolBar);

        m_chartView = new TransectChartView(host);
        connect(m_chartView, &TransectChartView::contextMenuRequestedAt, this,
                &TransectEditorDialog::onChartContextMenu_);
        // Slice BQ Phase 6.7.4 — interactive chart drags become a single
        // undoable MoveStationPoint via the dialog's QUndoStack.
        connect(m_chartView, &TransectChartView::stationDragFinished, this,
                [this](int idx, double oldS, double oldE, double newS, double newE) {
                    if (!m_edited)
                        return;
                    m_draftUndo->push(new openswmmvis::transect::MoveStationPointCommand(
                        m_edited, idx, oldS, oldE, newS, newE));
                });
        connect(m_chartView, &TransectChartView::handleClicked, this,
                &TransectEditorDialog::onChartHandleClicked_);
        connect(m_chartView, &TransectChartView::insertVertexRequested, this,
                &TransectEditorDialog::onChartInsertRequested_);
        connect(m_chartView, &TransectChartView::deleteVertexRequested, this,
                &TransectEditorDialog::onChartDeleteRequested_);
        lay->addWidget(m_chartView, 1);

        m_splitter->addWidget(host);
    }

    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 3);
    m_splitter->setStretchFactor(2, 3);

    // Close row. Wired to close() rather than accept()/reject() so every exit
    // (button, Esc, title bar) takes the same path. Not the default button —
    // Enter in a station/elevation cell must never dismiss the editor.
    m_draftUndo = new QUndoStack(this);
    m_session = new DataObjectDraftSession({{}, // every edit goes to the draft at once
                                            [this] { return dirtyCount_(); },
                                            [this](QString *why) { return applyDrafts_(why); },
                                            [this] { revertDrafts_(); }},
                                           this);
    m_session->setModel(m_layer); // Save / Run / Close ask about unapplied drafts
    connect(m_draftUndo, &QUndoStack::indexChanged, m_session, &DataObjectDraftSession::noteEdited);
    if (m_draftMarks)
        connect(m_session, &DataObjectDraftSession::changed, m_draftMarks,
                &DraftMarkProxyModel::refreshMarks);
    auto *footer = new DataObjectDraftFooter(m_session, this);
    footer->setObjectName(QStringLiteral("transect_draftFooter"));
    footer->setContentsMargins(8, 6, 8, 6); // outer runs at 0 margins
    footer->closeButton()->setObjectName(QStringLiteral("transect_closeBtn"));
    connect(footer, &DataObjectDraftFooter::closeRequested, this, &QDialog::close);
    outer->addWidget(footer);

    m_status = new QStatusBar(this);
    m_countLabel = new QLabel(m_status);
    m_status->addPermanentWidget(m_countLabel);
    outer->addWidget(m_status);
}

void TransectEditorDialog::buildToolbar_()
{
    if (!m_toolBar)
        return;
    m_toolBar->setIconSize(QSize(20, 20));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto *aFit = m_toolBar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Extent")),
                                      tr("Fit"));
    aFit->setToolTip(tr("Zoom to extent (F)"));
    aFit->setShortcut(QKeySequence(Qt::Key_F));
    connect(aFit, &QAction::triggered, this, &TransectEditorDialog::onZoomToExtentClicked_);

    auto *aIn = m_toolBar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ZoomIn")),
                                     tr("Zoom in"));
    aIn->setToolTip(tr("Zoom in (Ctrl++)"));
    aIn->setShortcut(QKeySequence::ZoomIn);
    connect(aIn, &QAction::triggered, this, &TransectEditorDialog::onZoomInClicked_);

    auto *aOut = m_toolBar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ZoomOut")),
                                      tr("Zoom out"));
    aOut->setToolTip(tr("Zoom out (Ctrl+-)"));
    aOut->setShortcut(QKeySequence::ZoomOut);
    connect(aOut, &QAction::triggered, this, &TransectEditorDialog::onZoomOutClicked_);

    // Draft edits, step by step (the model changes at Apply).
    m_toolBar->addSeparator();
    QAction *aUndo = m_draftUndo->createUndoAction(this, tr("Undo"));
    aUndo->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Undo")));
    aUndo->setShortcut(QKeySequence::Undo);
    aUndo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    QAction *aRedo = m_draftUndo->createRedoAction(this, tr("Redo"));
    aRedo->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Redo")));
    aRedo->setShortcut(QKeySequence::Redo);
    aRedo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_toolBar->addAction(aUndo);
    m_toolBar->addAction(aRedo);
    addAction(aUndo);
    addAction(aRedo);

    m_toolBar->addSeparator();

    auto *aPan =
        m_toolBar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Move")), tr("Pan"));
    aPan->setCheckable(true);
    aPan->setToolTip(tr("Toggle pan mode (left-button drag)"));
    connect(aPan, &QAction::toggled, this, &TransectEditorDialog::onPanToggled_);

    auto *aEdit = m_toolBar->addAction(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("SelectEdit")), tr("Edit Points"));
    aEdit->setCheckable(true);
    aEdit->setToolTip(tr(
        "Toggle interactive point editing — drag handles, Shift+click to insert, Delete to remove"));
    connect(aEdit, &QAction::toggled, this, &TransectEditorDialog::onEditPointsToggled_);

    // Insert / Delete vertex actions arm one-shot tool modes on the chart.
    // They live next to Edit Points because they're only meaningful while
    // the user is editing geometry.
    auto *aInsert = m_toolBar->addAction(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("InsertVertex")), tr("Insert Vertex"));
    aInsert->setCheckable(true);
    aInsert->setToolTip(tr("Insert vertex on click (also: Shift+click in Edit Points mode)"));
    connect(aInsert, &QAction::toggled, this, &TransectEditorDialog::onInsertVertexToggled_);
    m_insertVertexAction = aInsert;

    auto *aDeleteV = m_toolBar->addAction(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("DeleteVertex")), tr("Delete Vertex"));
    aDeleteV->setCheckable(true);
    aDeleteV->setToolTip(tr("Delete vertex on click (also: select handle + Delete key)"));
    connect(aDeleteV, &QAction::toggled, this, &TransectEditorDialog::onDeleteVertexToggled_);
    m_deleteVertexAction = aDeleteV;

    m_panAction = aPan;
    m_editAction = aEdit;

    m_toolBar->addSeparator();

    auto *aCopy = m_toolBar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Copy")),
                                       tr("Copy"));
    aCopy->setToolTip(tr("Copy station–elevation data to clipboard"));
    connect(aCopy, &QAction::triggered, this, &TransectEditorDialog::onCopyDataClicked_);

    auto *aProps = m_toolBar->addAction(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("ChartProperties")), tr("Properties"));
    aProps->setToolTip(tr("Edit chart display properties"));
    connect(aProps, &QAction::triggered, this, &TransectEditorDialog::onChartPropertiesClicked_);

    auto *aExport = m_toolBar->addAction(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("ExportImage")), tr("Export"));
    aExport->setToolTip(tr("Export chart as PNG"));
    connect(aExport, &QAction::triggered, this, &TransectEditorDialog::onExportChartClicked_);
}

void TransectEditorDialog::rebuildPropertyTree_()
{
    if (!m_propertyTree || !m_propertyBag)
        return;

    // QPropertyModel builds the tree as:
    //   <invisible root>                                 (m_rootPropertyItem = QObjectPropertyItem)
    //     └ index(0,0)         QObjectClassPropertyItem  value = "TransectPropertyBag"
    //         ├ Q_PROPERTY #1  Roughness — left bank Manning's n
    //         ├ Q_PROPERTY #2  ...
    //         └ Q_PROPERTY #10
    //
    // With default rootIndex the user sees a single collapsed "TransectPropertyBag"
    // class-group row and has to click the disclosure triangle to reach the 10
    // fields. Hide the class header by reparenting the tree at the class
    // index itself: its 10 children then appear as flat top-level rows.
    //
    // Constructed via setData(QVariant::fromValue<QObject*>(...)) explicitly
    // because the (QObject*, QObject*) constructor's QVariant::fromValue<T>
    // can otherwise pick up the derived-type metatype and not match
    // qvariant_cast<QObject*> downstream — see QPropertyModel::setData().
    auto *pm = new QPropertyModel(m_propertyTree);
    pm->setData(QVariant::fromValue<QObject *>(m_propertyBag));
    m_propertyTree->setModel(pm);

    // QPropertyModel requires QPropertyItemDelegate to render and edit
    // typed property rows — without it the tree shows nothing because the
    // default QStyledItemDelegate doesn't know how to call into
    // QPropertyItem::data(column, role) for the value column.
    if (!m_propertyTree->itemDelegate() ||
        !qobject_cast<QPropertyItemDelegate *>(m_propertyTree->itemDelegate())) {
        m_propertyTree->setItemDelegate(new QPropertyItemDelegate(m_propertyTree));
    }

    const QModelIndex classRoot = pm->index(0, 0);
    if (classRoot.isValid())
        m_propertyTree->setRootIndex(classRoot);
    m_propertyTree->expandAll();

    // Interactive lets the user drag the column divider. Seed column 0 to
    // a reasonable width so the longest property labels ("Encroachment
    // Stations — left", "Modifiers — station multiplier", etc.) are fully
    // visible on first show; after that the user is in charge.
    // setStretchLastSection makes the value column absorb any leftover
    // width when the dialog is resized.
    auto *hdr = m_propertyTree->header();
    hdr->setSectionResizeMode(0, QHeaderView::Interactive);
    hdr->setSectionResizeMode(1, QHeaderView::Interactive);
    hdr->setStretchLastSection(true);
    hdr->resizeSection(0, 260);
}

// ─────────────────────────────────────────────────────────────────────────────
// List / provider binding
// ─────────────────────────────────────────────────────────────────────────────

void TransectEditorDialog::selectProviderInList_(TransectProvider *p)
{
    if (!p || !m_listModel || !m_listView)
        return;
    const auto provs = m_registry ? m_registry->providers() : QVector<TransectProvider *>{};
    for (int i = 0; i < provs.size(); ++i) {
        if (provs.at(i) == p) {
            m_listView->setCurrentIndex(viewIndexOf(m_listView, m_listModel->index(i)));
            return;
        }
    }
}

void TransectEditorDialog::onListSelectionChanged_()
{
    if (!m_contextValid)
        return;
    if (!m_listView || !m_listModel)
        return;
    const int row = m_listView->currentIndex().row();
    auto *p = m_listModel->providerAt(row);
    bindProvider_(p);
}

void TransectEditorDialog::bindProvider_(TransectProvider *p)
{
    if (!m_contextValid)
        p = nullptr;
    if (m_current.data() == p) {
        refreshPropertyBag_();
        updateStatusBar_();
        return;
    }
    for (const auto &c : std::as_const(m_bindConnections))
        disconnect(c);
    m_bindConnections.clear();
    m_current = QPointer<TransectProvider>(p);
    // The table, chart, property tree and comments edit the draft copy; the
    // model's transect changes at Apply.
    TransectProvider *edited = copyFor_(p);
    m_edited = QPointer<TransectProvider>(edited);

    if (m_tableModel)
        m_tableModel->setProvider(edited);
    if (m_chartView)
        m_chartView->setProvider(edited);
    if (auto *bag = qobject_cast<TransectPropertyBag *>(m_propertyBag))
        bag->bind(edited, m_layer.data());

    m_suppressPropertyRefresh = true;
    if (m_nameEdit)
        m_nameEdit->setText(p ? p->name() : QString());
    if (m_commentsEdit)
        m_commentsEdit->setPlainText(edited ? edited->comments() : QString());
    m_suppressPropertyRefresh = false;

    if (m_addRowBtn)
        m_addRowBtn->setEnabled(p != nullptr);
    if (m_delRowBtn)
        m_delRowBtn->setEnabled(p != nullptr);
    if (m_delBtn)
        m_delBtn->setEnabled(p != nullptr);

    if (m_current && m_edited) {
        m_bindConnections << connect(m_edited, &TransectProvider::mutationRejected, this,
                                     &TransectEditorDialog::onMutationRejected_);
        m_bindConnections << connect(m_current, &TransectProvider::nameChanged, this,
                                     [this](const QString &, const QString &now) {
                                         if (m_nameEdit && m_nameEdit->text() != now) {
                                             const QSignalBlocker b(m_nameEdit);
                                             m_nameEdit->setText(now);
                                         }
                                         setWindowTitle(tr("Transect Editor — %1").arg(now));
                                     });
        m_bindConnections << connect(m_edited, &TransectProvider::commentsChanged, this, [this]() {
            if (!m_commentsEdit || !m_edited)
                return;
            if (m_commentsEdit->toPlainText() != m_edited->comments()) {
                const QSignalBlocker b(m_commentsEdit);
                m_commentsEdit->setPlainText(m_edited->comments());
            }
        });
    }

    updateStatusBar_();
    if (p)
        setWindowTitle(tr("Transect Editor — %1").arg(p->name()));
    else
        setWindowTitle(tr("Transect Editor"));
}

void TransectEditorDialog::refreshPropertyBag_()
{
    if (auto *bag = qobject_cast<TransectPropertyBag *>(m_propertyBag))
        bag->bind(m_edited.data(), m_layer.data());
}

void TransectEditorDialog::updateStatusBar_()
{
    if (!m_countLabel)
        return;
    if (!m_edited) {
        m_countLabel->setText({});
        return;
    }
    m_countLabel->setText(tr("%1 stations").arg(m_edited->pointCount()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Name + comments live-edit
// ─────────────────────────────────────────────────────────────────────────────

void TransectEditorDialog::onNameEdited_()
{
    if (m_suppressPropertyRefresh || !m_current || !m_registry || !m_nameEdit)
        return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) {
        if (m_nameEdit && m_current)
            m_nameEdit->setText(m_current->name());
        return;
    }
    // Feedback C10 — refuse names that cannot survive an .inp round trip.
    if (const QString why = openswmmvis::swmmIdentifierProblem(newName); !why.isEmpty()) {
        if (m_status)
            m_status->showMessage(why, 6000);
        const QSignalBlocker b(m_nameEdit);
        m_nameEdit->setText(m_current->name());
        return;
    }
    // Pre-check uniqueness so the undo command isn't pushed onto the stack
    // for a doomed rename. The command itself also calls registry->rename()
    // which is itself idempotent on collision, but pushing then discovering
    // the failure post-hoc would leak an undo entry that does nothing.
    if (m_registry->hasName(newName)) {
        if (m_status)
            m_status->showMessage(tr("A transect named “%1” already exists.").arg(newName), 4000);
        const QSignalBlocker b(m_nameEdit);
        m_nameEdit->setText(m_current->name());
        return;
    }
    if (m_undoStack) {
        m_undoStack->push(
            new openswmmvis::transect::RenameTransectCommand(m_registry, m_current, newName));
    } else {
        m_registry->rename(m_current, newName);
    }
    m_session->noteApplied(tr("Renamed to %1").arg(newName));
}

void TransectEditorDialog::onCommentsEdited_()
{
    if (m_suppressPropertyRefresh || !m_edited || !m_commentsEdit)
        return;
    const QString newText = m_commentsEdit->toPlainText();
    if (newText == m_edited->comments())
        return;
    m_draftUndo->push(new openswmmvis::transect::SetCommentsCommand(m_edited, newText));
}

bool TransectEditorDialog::renameCurrent(const QString &newName)
{
    if (!m_current || !m_registry)
        return false;
    return m_registry->rename(m_current, newName.trimmed());
}

// ─────────────────────────────────────────────────────────────────────────────
// CRUD
// ─────────────────────────────────────────────────────────────────────────────

QString TransectEditorDialog::suggestUniqueName_() const
{
    if (!m_registry)
        return tr("Transect1");
    for (int i = 1; i < 9999; ++i) {
        const QString cand = QStringLiteral("Transect%1").arg(i);
        if (!m_registry->hasName(cand))
            return cand;
    }
    return tr("Transect");
}

void TransectEditorDialog::invokeNew()
{
    onAddTransectClicked_();
}

void TransectEditorDialog::onAddTransectClicked_()
{
    if (!m_registry)
        return;
    // Create with a default-unique name and immediately focus the name
    // field so the user can rename inline — no intermediate prompt.
    const QString name = suggestUniqueName_();
    if (!DataObjectDraftSession::commit(
            {createObjectEdit(m_registry.data(), name, [](TransectProvider *) { return true; })},
            m_undoStack, tr("Create transect %1").arg(name)))
        return;
    auto *p = m_registry->findByName(name);
    if (!p)
        return;
    m_session->noteApplied(tr("Created %1").arg(name));
    selectProviderInList_(p);
    m_mode = Mode::Edit;
    if (m_nameEdit) {
        m_nameEdit->setFocus(Qt::OtherFocusReason);
        m_nameEdit->selectAll();
    }
}

void TransectEditorDialog::onDeleteTransectClicked_()
{
    if (!m_current || !m_registry)
        return;
    const QString name = m_current->name();
    // Plan D8 — say what the engine detaches, and that Undo will not re-attach it.
    QString text = tr("Delete transect “%1”?").arg(name);
    if (void *engine = m_registry->engineHandle()) {
        const QString uses = dataObjectUses(
            engine, swmm_transect_analyze_impact,
            swmm_transect_index(static_cast<SWMM_Engine>(engine), name.toUtf8().constData()));
        if (!uses.isEmpty())
            text += QLatin1String("\n\n") + uses + QLatin1Char(' ') +
                    tr("Conduits using it lose their IRREGULAR cross-section; Undo brings back the "
                       "transect and its settings, but not those cross-sections.");
    }
    const auto reply = QMessageBox::question(this, tr("Delete Transect"), text,
                                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;
    deleteCurrentSilently();
}

void TransectEditorDialog::deleteCurrentSilently()
{
    if (!m_current || !m_registry)
        return;
    const QString name = m_current->name();
    const auto restore = [registry = m_registry, kept = m_current->params()](TransectProvider *p) {
        return registry && writeTransect(registry.data(), p, kept);
    };
    if (!DataObjectDraftSession::commit({deleteObjectEdit(m_registry.data(), name, restore)},
                                        m_undoStack, tr("Delete transect %1").arg(name)))
        return;
    m_session->noteApplied(tr("Deleted %1").arg(name));
}

void TransectEditorDialog::onAddRowClicked_()
{
    if (!m_edited)
        return;
    const int n = m_edited->pointCount();
    const double newX = (n > 0) ? (m_edited->pointAt(n - 1).station + 1.0) : 0.0;
    m_draftUndo->push(new openswmmvis::transect::InsertStationCommand(m_edited, newX, 0.0));
}

void TransectEditorDialog::onDeleteRowsClicked_()
{
    if (!m_edited || !m_table)
        return;
    const auto sel = m_table->selectionModel();
    if (!sel)
        return;
    QVector<int> rows;
    for (const auto &idx : sel->selectedRows())
        rows.push_back(idx.row());
    if (rows.isEmpty() && sel->currentIndex().isValid())
        rows.push_back(sel->currentIndex().row());
    if (rows.isEmpty())
        return;
    m_draftUndo->push(new openswmmvis::transect::DeleteStationsCommand(m_edited, rows));
}

// ─────────────────────────────────────────────────────────────────────────────
// Registry signal handlers
// ─────────────────────────────────────────────────────────────────────────────

void TransectEditorDialog::onProviderAdded_(TransectProvider *p)
{
    Q_UNUSED(p);
    // TransectListModel handles the begin/end inserts. Nothing more to do.
}

void TransectEditorDialog::onProviderRemoved_(TransectProvider *p)
{
    if (m_current.data() == p)
        bindProvider_(nullptr);
    dropDraft_(p);
}

void TransectEditorDialog::onProviderRenamed_(TransectProvider *p, const QString &,
                                              const QString &now)
{
    if (m_current.data() == p)
        setWindowTitle(tr("Transect Editor — %1").arg(now));
}

void TransectEditorDialog::onMutationRejected_(const QString &reason)
{
    if (m_status)
        m_status->showMessage(reason, 4000);
}

// ─────────────────────────────────────────────────────────────────────────────
// Chart context menu + toolbar handlers
// ─────────────────────────────────────────────────────────────────────────────

void TransectEditorDialog::onChartContextMenu_(const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *aProps = menu.addAction(tr("Chart properties…"));
    menu.addSeparator();
    QAction *aFit = menu.addAction(tr("Zoom to extent"));
    QAction *aReset = menu.addAction(tr("Reset zoom"));
    menu.addSeparator();
    QAction *aCopy = menu.addAction(tr("Copy data to clipboard"));
    QAction *aExp = menu.addAction(tr("Export chart as PNG…"));
    QAction *picked = menu.exec(globalPos);
    if (picked == aProps)
        onChartPropertiesClicked_();
    else if (picked == aFit)
        onZoomToExtentClicked_();
    else if (picked == aReset) {
        if (m_chartView)
            m_chartView->resetZoom();
    } else if (picked == aCopy)
        onCopyDataClicked_();
    else if (picked == aExp)
        onExportChartClicked_();
}

void TransectEditorDialog::onZoomToExtentClicked_()
{
    if (m_chartView)
        m_chartView->zoomToExtent();
}

void TransectEditorDialog::onZoomInClicked_()
{
    if (m_chartView && m_chartView->chart())
        m_chartView->chart()->zoom(1.25);
}

void TransectEditorDialog::onZoomOutClicked_()
{
    if (m_chartView && m_chartView->chart())
        m_chartView->chart()->zoom(1.0 / 1.25);
}

// Mode toggles are mutually exclusive — turning one on automatically clears
// the others so the toolbar always reflects a single active mode.
/**
 * @brief Uncheck nonnull competing actions while blocking their toggled signals.
 * @param self Borrowed nominal active action, deliberately unused and never changed.
 * @param others Borrowed nullable action pointers iterated synchronously; each nonnull
 * item is unchecked. Caller supplies only competitors, since self is not filtered out.
 * No ownership transfer, QActionGroup installation or chart-mode mutation by this helper.
 */
static void setExclusive_(QAction *self, std::initializer_list<QAction *> others)
{
    for (auto *a : others) {
        if (!a)
            continue;
        const QSignalBlocker b(a);
        a->setChecked(false);
    }
    Q_UNUSED(self);
}

void TransectEditorDialog::onPanToggled_(bool on)
{
    if (!m_chartView)
        return;
    if (on)
        setExclusive_(m_panAction, {m_editAction, m_insertVertexAction, m_deleteVertexAction});
    m_chartView->setMode(on ? TransectChartView::Mode::Pan : TransectChartView::Mode::Select);
}

void TransectEditorDialog::onEditPointsToggled_(bool on)
{
    if (!m_chartView)
        return;
    if (on)
        setExclusive_(m_editAction, {m_panAction, m_insertVertexAction, m_deleteVertexAction});
    m_chartView->setMode(on ? TransectChartView::Mode::EditPoints
                            : TransectChartView::Mode::Select);
}

void TransectEditorDialog::onInsertVertexToggled_(bool on)
{
    if (!m_chartView)
        return;
    if (on)
        setExclusive_(m_insertVertexAction, {m_panAction, m_editAction, m_deleteVertexAction});
    m_chartView->setMode(on ? TransectChartView::Mode::InsertVertex
                            : TransectChartView::Mode::Select);
}

void TransectEditorDialog::onDeleteVertexToggled_(bool on)
{
    if (!m_chartView)
        return;
    if (on)
        setExclusive_(m_deleteVertexAction, {m_panAction, m_editAction, m_insertVertexAction});
    m_chartView->setMode(on ? TransectChartView::Mode::DeleteVertex
                            : TransectChartView::Mode::Select);
}

void TransectEditorDialog::onChartHandleClicked_(int index, Qt::KeyboardModifiers mods)
{
    if (!m_table || !m_table->selectionModel() || !m_tableModel)
        return;
    auto *sel = m_table->selectionModel();
    if (index < 0) {
        // Click on empty chart space — clear selection unless modified.
        if (!(mods & (Qt::ShiftModifier | Qt::ControlModifier))) {
            m_suppressChartSelectionSync = true;
            sel->clearSelection();
            m_suppressChartSelectionSync = false;
        }
        return;
    }
    if (index >= m_tableModel->rowCount())
        return;

    const QModelIndex topLeft = m_tableModel->index(index, 0);
    const QModelIndex bottomRight = m_tableModel->index(index, m_tableModel->columnCount() - 1);
    QItemSelection rowSel(topLeft, bottomRight);

    m_suppressChartSelectionSync = true;
    if (mods & Qt::ShiftModifier) {
        // Range select from current anchor to clicked row.
        const QModelIndex cur = sel->currentIndex();
        if (cur.isValid()) {
            const int a = std::min(cur.row(), index);
            const int b = std::max(cur.row(), index);
            const QModelIndex rTL = m_tableModel->index(a, 0);
            const QModelIndex rBR = m_tableModel->index(b, m_tableModel->columnCount() - 1);
            sel->select(QItemSelection(rTL, rBR), QItemSelectionModel::Select);
        } else {
            sel->select(rowSel, QItemSelectionModel::Select);
        }
    } else if (mods & Qt::ControlModifier) {
        // Additive toggle for the clicked row.
        const auto flags = sel->isSelected(topLeft)
                               ? (QItemSelectionModel::Deselect | QItemSelectionModel::Rows)
                               : (QItemSelectionModel::Select | QItemSelectionModel::Rows);
        sel->select(rowSel, flags);
    } else {
        sel->select(rowSel, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    sel->setCurrentIndex(topLeft, QItemSelectionModel::NoUpdate);
    m_table->scrollTo(topLeft, QAbstractItemView::EnsureVisible);
    m_suppressChartSelectionSync = false;

    // Drive the chart overlay directly — the table's selectionChanged signal
    // is suppressed during this call.
    onTableSelectionChanged_();
}

void TransectEditorDialog::onTableSelectionChanged_()
{
    if (m_suppressTableSelectionSync || !m_chartView || !m_table || !m_table->selectionModel())
        return;
    QVector<int> rows;
    for (const auto &idx : m_table->selectionModel()->selectedRows())
        rows.push_back(idx.row());
    std::sort(rows.begin(), rows.end());
    m_chartView->setSelectedIndices(rows);
}

void TransectEditorDialog::onChartInsertRequested_(double station, double elevation)
{
    if (!m_edited)
        return;
    m_draftUndo->push(new openswmmvis::transect::InsertStationCommand(m_edited, station, elevation));
}

void TransectEditorDialog::onChartDeleteRequested_(int index)
{
    if (!m_edited || index < 0 || index >= m_edited->pointCount())
        return;
    QVector<int> rows;
    rows.push_back(index);
    m_draftUndo->push(new openswmmvis::transect::DeleteStationsCommand(m_edited, rows));
}

void TransectEditorDialog::onChartPropertiesClicked_()
{
    if (!m_chartView)
        return;
    // Modeless QPropertyModel-backed editor over the chart view's
    // Q_PROPERTY surface. Mirrors ChartPropertiesDialog's idiom but
    // we don't reuse that class directly — it's hard-wired to
    // ChartProperties, whereas we want the transect-specific style set.
    auto *dlg = new QDialog(this, Qt::Tool);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Chart Properties"));
    dlg->resize(360, 260);

    auto *tree = new QTreeView(dlg);
    tree->setAlternatingRowColors(true);
    tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    tree->setRootIsDecorated(false);
    auto *pm = new QPropertyModel(m_chartView, dlg);
    tree->setModel(pm);
    tree->header()->setStretchLastSection(false);

    auto *lay = new QVBoxLayout(dlg);
    lay->addWidget(tree);

    auto *closeBox = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    closeBox->setObjectName(QStringLiteral("transectChart_closeBox"));
    auto *closeBtn = closeBox->button(QDialogButtonBox::Close);
    closeBtn->setObjectName(QStringLiteral("transectChart_closeBtn"));
    closeBtn->setAutoDefault(false);
    closeBtn->setDefault(false);
    connect(closeBox, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    lay->addWidget(closeBox);

    dlg->show();
}

void TransectEditorDialog::onCopyDataClicked_()
{
    if (!m_edited)
        return;
    QString tsv;
    tsv += tr("Station\tElevation\n");
    for (const auto &p : m_edited->points())
        tsv += QStringLiteral("%1\t%2\n").arg(p.station).arg(p.elevation);
    QApplication::clipboard()->setText(tsv);
    if (m_status)
        m_status->showMessage(
            tr("Copied %1 station rows to clipboard.").arg(m_edited->pointCount()), 3000);
}

void TransectEditorDialog::onExportChartClicked_()
{
    if (!m_chartView)
        return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Chart as PNG"), QString(),
                                                      tr("PNG Image (*.png)"));
    if (path.isEmpty())
        return;
    QPixmap pm = m_chartView->grab();
    if (!pm.save(path)) {
        QMessageBox::warning(this, tr("Export Chart"), tr("Could not save image to %1.").arg(path));
    } else if (m_status) {
        m_status->showMessage(tr("Saved chart to %1").arg(path), 3000);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Drafts — the form edits a copy; Apply writes it to the model
// ─────────────────────────────────────────────────────────────────────────────

void TransectEditorDialog::done(int result)
{
    if (m_contextValid && m_registry && !m_session->resolveBeforeClose())
        return;
    QDialog::done(result);
}

TransectProvider *TransectEditorDialog::copyFor_(TransectProvider *live)
{
    if (!live)
        return nullptr;
    for (const auto &d : std::as_const(m_drafts))
        if (d.live == live)
            return d.copy;
    Draft d;
    d.live = live;
    d.base = live->params();
    d.copy = new TransectProvider(live->name(), this);
    d.copy->setParams(d.base);
    m_drafts.push_back(d);

    TransectProvider *copy = d.copy;
    // Edits of the copy (table cells, property tree, commands) → indicator.
    const auto edited = [this] { m_session->noteEdited(); };
    for (auto signal : {&TransectProvider::commentsChanged, &TransectProvider::roughnessChanged,
                        &TransectProvider::bankStationsChanged,
                        &TransectProvider::encroachmentStationsChanged,
                        &TransectProvider::modifiersChanged}) {
        connect(copy, signal, this, edited);
        connect(live, signal, this, [this, live] { onLiveChanged_(live); });
    }
    for (auto signal : {&TransectProvider::pointsChanged, &TransectProvider::pointsInserted,
                        &TransectProvider::pointsRemoved}) {
        connect(copy, signal, this, edited);
        connect(live, signal, this, [this, live] { onLiveChanged_(live); });
    }
    // Renames take effect at once (registry); the copy follows.
    connect(live, &TransectProvider::nameChanged, copy,
            [copy](const QString &, const QString &now) { copy->setName(now); });
    return copy;
}

void TransectEditorDialog::onLiveChanged_(TransectProvider *live)
{
    if (m_applying)
        return;
    for (auto &d : m_drafts) {
        if (d.live != live)
            continue;
        if (!(d.copy->params() == d.base)) {
            m_session->noteExternalChange(); // keep the user's draft; Apply asks
            return;
        }
        d.base = live->params();
        if (!(d.copy->params() == d.base)) {
            d.copy->setParams(d.base); // unedited: follow the model
            m_draftUndo->clear();      // its steps were recorded against the old values
        }
        return;
    }
}

void TransectEditorDialog::dropDraft_(TransectProvider *live)
{
    for (int i = 0; i < m_drafts.size(); ++i) {
        if (m_drafts.at(i).live != live)
            continue;
        TransectProvider *copy = m_drafts.at(i).copy;
        m_drafts.remove(i);
        live->disconnect(this);
        if (m_edited.data() == copy)
            bindProvider_(nullptr);
        m_draftUndo->clear(); // steps may refer to the dropped copy
        copy->deleteLater();
        m_session->noteEdited();
        return;
    }
}

int TransectEditorDialog::dirtyCount_() const
{
    int n = 0;
    for (const auto &d : m_drafts)
        if (d.live && !(d.copy->params() == d.live->params()))
            ++n;
    return n;
}

bool TransectEditorDialog::applyDrafts_(QString *reason)
{
    QVector<DataObjectDraftSession::Edit> edits;
    QStringList names;
    void *engine = m_registry ? m_registry->engineHandle() : nullptr;
    for (const auto &d : std::as_const(m_drafts)) {
        if (!d.live || d.copy->params() == d.live->params())
            continue;
        names << d.live->name();
        // Replayed by Undo/Redo after this editor may have closed: guarded
        // pointers and values only, never `this`.
        // The transect Apply saw, or, once that one was deleted and brought
        // back by Undo, the transect of the same name.
        edits.append({[registry = m_registry, layer = m_layer, hadLayer = !m_layer.isNull(), engine,
                       live = d.live, name = d.live->name(), before = d.live->params(),
                       after = d.copy->params()](bool forward) {
            if (!registry || (hadLayer && !layer) || registry->engineHandle() != engine)
                return false;
            TransectProvider *p = stepTarget(registry.data(), live, name);
            return p && writeTransect(registry.data(), p, forward ? after : before);
        }});
    }
    if (edits.isEmpty())
        return true;
    m_applying = true;
    const bool ok = DataObjectDraftSession::commit(
        edits, m_undoStack,
        names.size() == 1 ? tr("Edit transect %1").arg(names.first())
                          : tr("Edit %n transect(s)", nullptr, int(names.size())));
    m_applying = false;
    if (!ok) {
        *reason = tr("the model did not accept %1; nothing was changed")
                      .arg(names.join(QStringLiteral(", ")));
        return false;
    }
    revertDrafts_(); // the copies now take what the model kept
    return true;
}

void TransectEditorDialog::revertDrafts_()
{
    for (auto &d : m_drafts) {
        if (!d.live)
            continue;
        d.base = d.live->params();
        d.copy->setParams(d.base);
    }
    m_draftUndo->clear();
    refreshPropertyBag_();
    updateStatusBar_();
}

} // namespace openswmmvis::ui
