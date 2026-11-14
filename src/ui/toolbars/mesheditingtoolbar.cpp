/**
 * @file      mesheditingtoolbar.cpp
 * @brief     The Mesh 2D ribbon tab: active mesh, process picker, selection tools and the
 *            active process's configurators (process-aware editing plan, Phase 2).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#include "ui/toolbars/mesheditingtoolbar.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "mesh/meshautocouple.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshhoverprobe.h"
#include "mesh/meshnodemapper.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include "ui/dialogs/remap1d2ddialog.h"
#include "ui/toolbars/ribbongroup.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QShowEvent>
#include <QSizePolicy>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>

namespace
{
constexpr int kNoneIndex = 0;

using Process = MeshEditingToolbar::Process;

/// Picker order; every process has a group, shown one at a time.
constexpr Process kProcesses[] = {Process::SurfaceFlow,    Process::Infiltration,
                                  Process::Groundwater,    Process::SurfaceQuality,
                                  Process::Snow,           Process::Heat};

QString processName(Process process)
{
    switch (process) {
    case Process::SurfaceFlow:
        return MeshEditingToolbar::tr("Surface Flow");
    case Process::Infiltration:
        return MeshEditingToolbar::tr("Infiltration");
    case Process::Groundwater:
        return MeshEditingToolbar::tr("Groundwater");
    case Process::SurfaceQuality:
        return MeshEditingToolbar::tr("Surface Quality");
    case Process::Snow:
        return MeshEditingToolbar::tr("Snow");
    case Process::Heat:
        return MeshEditingToolbar::tr("Heat");
    }
    return {};
}

/// objectName suffix of a process's menu action (actionMeshProcess<Key>).
QString processKey(Process process)
{
    switch (process) {
    case Process::SurfaceFlow:
        return QStringLiteral("SurfaceFlow");
    case Process::Infiltration:
        return QStringLiteral("Infiltration");
    case Process::Groundwater:
        return QStringLiteral("Groundwater");
    case Process::SurfaceQuality:
        return QStringLiteral("SurfaceQuality");
    case Process::Snow:
        return QStringLiteral("Snow");
    case Process::Heat:
        return QStringLiteral("Heat");
    }
    return {};
}

/*! The process picker re-reads the status callback each time it opens, so a
 *  process switched on in Simulation Options shows as such without a refresh
 *  hook into the options dialog. */
class ProcessCombo final : public QComboBox
{
public:
    ProcessCombo(std::function<void()> beforePopup, QWidget *parent)
        : QComboBox(parent), mBeforePopup(std::move(beforePopup))
    {
    }
    void showPopup() override
    {
        if (mBeforePopup)
            mBeforePopup();
        QComboBox::showPopup();
    }

private:
    std::function<void()> mBeforePopup;
};
} // namespace

MeshEditingToolbar::MeshEditingToolbar(const QString &title, QWidget *parent)
    : QToolBar(title, parent)
{
    setObjectName(QStringLiteral("toolBarMeshEditing"));
    setMovable(true);

    // Process-aware editing plan §4.2: Mesh → Process → Select → the active
    // process's group → Cell Data → Profile. Groups are action-only except
    // the two pickers, so the compactor can shed labels trailing-first; the
    // former inline value editors live in Properties and the attribute table.
    using openswmmvis::ui::RibbonGroup;

    // ── Mesh: active-mesh picker + mesh-wide coupling commands ─────────────
    m_groupMesh = new RibbonGroup(tr("Mesh"), this);
    {
        auto *picker = new QWidget(m_groupMesh);
        auto *row = new QHBoxLayout(picker);
        row->setContentsMargins(4, 0, 4, 0);
        row->addWidget(new QLabel(tr("Mesh:"), picker));
        m_meshCombo = new QComboBox(picker);
        m_meshCombo->setObjectName(QStringLiteral("meshActiveCombo"));
        m_meshCombo->setMinimumWidth(170);
        m_meshCombo->setToolTip(tr("Active 2D mesh. Selecting a mesh marks it as the active "
                                   "mesh for editing and (where applicable) result coupling."));
        m_meshCombo->addItem(tr("(none)"), QVariant::fromValue<quintptr>(0));
        m_meshCombo->setEnabled(false);
        row->addWidget(m_meshCombo);
        m_groupMesh->addWidget(picker);
        connect(m_meshCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                &MeshEditingToolbar::onActiveMeshComboChanged);
    }
    // Auto-couple: couple vertices to the SWMM node sitting at the same map
    // coordinate. Operates on the selection, or the whole mesh when nothing
    // is selected, via the node locator SWMMVis installs.
    m_actAutoCouple = new QAction(QIcon(QStringLiteral(":/swmmvis/Snap")), tr("Auto-couple"), this);
    m_actAutoCouple->setObjectName(QStringLiteral("actionMeshAutoCouple"));
    m_actAutoCouple->setIconText(tr("Auto-couple Nodes"));
    m_actAutoCouple->setToolTip(
        tr("Couple mesh vertices to coincident SWMM nodes.\n"
           "Applies to the selected vertices, or scans the whole mesh when\n"
           "nothing is selected. Already-coupled vertices are left unchanged."));
    connect(m_actAutoCouple, &QAction::triggered, this, &MeshEditingToolbar::onAutoCoupleClicked);
    // Remap 1D↔2D (Plan Part C.4): the superset of Auto-couple. Coincident
    // nodes → vertex coupling; non-coincident nodes inside the mesh → cell
    // coupling (several nodes may share one cell); outside → reported.
    m_actRemap = new QAction(QIcon(QStringLiteral(":/swmmvis/Remap1D2D")), tr("Remap 1D↔2D"), this);
    m_actRemap->setObjectName(QStringLiteral("actionMeshRemap"));
    m_actRemap->setToolTip(tr("Map SWMM model nodes onto the active mesh.\n"
                              "Nodes coincident with a mesh vertex use vertex coupling; other\n"
                              "nodes inside the mesh couple to their containing cell (several\n"
                              "nodes may share one cell). All existing couplings are cleared\n"
                              "first (you will be asked to confirm)."));
    connect(m_actRemap, &QAction::triggered, this, &MeshEditingToolbar::onRemapClicked);
    m_groupMesh->addStack({m_actAutoCouple, m_actRemap});
    mountGroup(m_groupMesh);

    // ── Process: which process's configurators the tab shows ──────────────
    m_groupProcess = new RibbonGroup(tr("Process"), this);
    m_processCombo = new ProcessCombo([this] { refreshProcessStatus(); }, m_groupProcess);
    m_processCombo->setObjectName(QStringLiteral("meshProcessCombo"));
    m_processCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_processCombo->setToolTip(tr("Process whose settings and assignments the next group shows. "
                                  "Selection and values are kept when you switch."));
    for (Process process : kProcesses)
        m_processCombo->addItem(processName(process), static_cast<int>(process));
    m_groupProcess->addWidget(m_processCombo);
    // The same choice as checkable actions, so the menu bar and the command
    // palette reach every process too (the picker itself is not a command).
    auto *processChoice = new QActionGroup(this);
    processChoice->setExclusive(true);
    for (Process process : kProcesses) {
        auto *action = new QAction(processName(process), processChoice);
        action->setObjectName(QStringLiteral("actionMeshProcess") + processKey(process));
        action->setCheckable(true);
        action->setChecked(process == m_process);
        action->setData(static_cast<int>(process));
        action->setToolTip(tr("Show the %1 commands on the Mesh 2D tab.").arg(processName(process)));
        connect(action, &QAction::triggered, this, [this, process] { setCurrentProcess(process); });
        m_processActions.append(action);
    }
    connect(m_processCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        setCurrentProcess(static_cast<Process>(m_processCombo->currentData().toInt()));
    });
    mountGroup(m_groupProcess);

    // ── Select: the three element tools + selection commands ──────────────
    m_groupSelect = new RibbonGroup(tr("Select"), this);
    m_actPickCells = new QAction(QIcon(QStringLiteral(":/swmmvis/SelectCell")),
                                 tr("Select 2D Cells"), this);
    // objectNames tie these into SWMMVisProjectWindow::toolActionKeys() so
    // the canvas active-tool sync unchecks them when another tool starts.
    m_actPickCells->setObjectName(QStringLiteral("actionPick2DCells"));
    m_actPickCells->setIconText(tr("Cells"));
    m_actPickCells->setCheckable(true);
    m_actPickCells->setToolTip(
        tr("Select 2D cells. Click selects a cell (Shift = add, Ctrl = toggle); drag a box or "
           "press L to lasso several. Edit their values in Properties or the attribute table; "
           "right-click a selection to plot its results. Esc clears."));
    m_actEditEdge =
        new QAction(QIcon(QStringLiteral(":/swmmvis/SelectTriEdge")), tr("Select Edge"), this);
    m_actEditEdge->setObjectName(QStringLiteral("actionMeshSelectEdge"));
    m_actEditEdge->setIconText(tr("Edges"));
    m_actEditEdge->setCheckable(true);
    m_actEditEdge->setToolTip(tr("Select edges: click an edge or drag a box to select several; "
                                 "Ctrl/⌘-click two boundary edges to add the whole run between "
                                 "them. Edit boundary conditions and conveyance in Properties "
                                 "or the attribute table. Esc clears."));
    m_actEditVertex =
        new QAction(QIcon(QStringLiteral(":/swmmvis/SelectTriNode")), tr("Select Vertex"), this);
    m_actEditVertex->setObjectName(QStringLiteral("actionMeshSelectVertex"));
    m_actEditVertex->setIconText(tr("Vertices"));
    m_actEditVertex->setCheckable(true);
    m_actEditVertex->setToolTip(tr("Select vertices: click a vertex or drag a box. Edit elevation, "
                                   "tag and node coupling in Properties or the attribute table. "
                                   "Esc clears."));
    m_editGroup = new QActionGroup(this);
    m_editGroup->setExclusive(true);
    m_editGroup->addAction(m_actEditVertex);
    m_editGroup->addAction(m_actEditEdge);
    connect(m_actEditVertex, &QAction::toggled, this, [this](bool on) {
        emit editVertexToggled(on);
        updateEnabledState();
    });
    connect(m_actEditEdge, &QAction::toggled, this, [this](bool on) {
        emit editEdgeToggled(on);
        updateEnabledState();
    });
    m_actClearSelection =
        new QAction(QIcon(QStringLiteral(":/swmmvis/SelectNone")), tr("Clear Selection"), this);
    m_actClearSelection->setObjectName(QStringLiteral("actionMeshClearSelection"));
    m_actClearSelection->setToolTip(tr("Clear the selection (Esc on the map does the same)."));
    connect(m_actClearSelection, &QAction::triggered, this, [this] {
        if (m_selection)
            m_selection->clear();
    });
    m_actEditInTable =
        new QAction(QIcon(QStringLiteral(":/swmmvis/TableView")), tr("Edit in Table"), this);
    m_actEditInTable->setObjectName(QStringLiteral("actionMeshEditInTable"));
    m_actEditInTable->setToolTip(
        tr("Open the attribute table on the selected element kind. Select rows and use "
           "\"Apply value to selected rows\" to edit many elements in one undo step."));
    connect(m_actEditInTable, &QAction::triggered, this, [this] {
        if (!m_activeMesh)
            return;
        // The selection decides the table; with nothing selected, the active tool does.
        int kind = m_actEditEdge->isChecked() ? 1 : m_actEditVertex->isChecked() ? 0 : 2;
        if (!currentSelectedEdges().isEmpty())
            kind = 1;
        else if (!currentSelectedVertices().isEmpty())
            kind = 0;
        else if (!currentSelectedCells().isEmpty())
            kind = 2;
        emit editInTableRequested(m_activeMesh, kind);
    });
    m_groupSelect->addAction(m_actPickCells);
    m_groupSelect->addAction(m_actEditEdge);
    m_groupSelect->addAction(m_actEditVertex);
    m_groupSelect->addStack({m_actClearSelection, m_actEditInTable});
    mountGroup(m_groupSelect);

    // ── One group per process; only the picked process's group shows ─────
    for (Process process : kProcesses) {
        auto *group = new RibbonGroup(processName(process), this);
        m_processGroups.insert(static_cast<int>(process), group);
        mountGroup(group);
    }
    // Infiltration's ownership review (the host places it in the group).
    m_actReviewOwnership = new QAction(QIcon(QStringLiteral(":/swmmvis/Warning")),
                                       tr("Review Ownership…"), this);
    m_actReviewOwnership->setObjectName(QStringLiteral("actionMeshReviewOwnership"));
    m_actReviewOwnership->setIconText(tr("Review\nOwnership"));
    m_actReviewOwnership->setToolTip(
        tr("Review which cells infiltrate into the surface bank, the subcatchment aquifer or the "
           "2D aquifer, and migrate obsolete destinations."));
    connect(m_actReviewOwnership, &QAction::triggered, this, [this] {
        if (m_surfaceOwnershipReview)
            m_surfaceOwnershipReview();
    });
    // ── Cell Data and Profile: filled by the host ──────────────────────────
    m_groupCellData = new RibbonGroup(tr("Cell Data"), this);
    mountGroup(m_groupCellData);
    m_groupProfile = new RibbonGroup(tr("Profile"), this);
    mountGroup(m_groupProfile);

    // Expanding zero-min spacer: groups left-pack and the compactor knows to
    // discount it by name.
    {
        auto *spacer = new QWidget(this);
        spacer->setObjectName(QStringLiteral("ribbonBarSpacer"));
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        spacer->setMinimumSize(0, 0);
        addWidget(spacer);
    }

    // ── Hover probe (owned QObject; outlives any single canvas) ────────
    m_hover = new mesh::MeshHoverProbe(this);
    connect(m_hover, &mesh::MeshHoverProbe::elevationChanged, this,
            &MeshEditingToolbar::onHoverElevation);

    updateGroupVisibility();
    updateEnabledState();
}

MeshEditingToolbar::~MeshEditingToolbar() = default;

void MeshEditingToolbar::mountGroup(openswmmvis::ui::RibbonGroup *group)
{
    m_groupSlots.insert(group, addWidget(group));
}

void MeshEditingToolbar::setSurfaceOwnershipReview(std::function<void()> fn)
{
    m_surfaceOwnershipReview = std::move(fn);
    updateEnabledState();
}

void MeshEditingToolbar::setProcessStatusProvider(ProcessStatusFn fn)
{
    m_processStatus = std::move(fn);
    refreshProcessStatus();
}

void MeshEditingToolbar::setProcessLockProvider(ProcessLockFn fn)
{
    m_processLock = std::move(fn);
    refreshProcessStatus();
}

void MeshEditingToolbar::refreshProcessStatus()
{
    for (int row = 0; row < m_processCombo->count(); ++row) {
        const auto process = static_cast<Process>(m_processCombo->itemData(row).toInt());
        const QString status = m_processStatus ? m_processStatus(process) : QString();
        const QString locked = m_processLock ? m_processLock(process) : QString();
        const QString name = processName(process);
        const QString label = status.isEmpty() ? name : tr("%1 — %2").arg(name, status);
        m_processCombo->setItemText(row, label);
        if (row < m_processActions.size())
            m_processActions[row]->setText(label);
        // A switched-off process says how to switch it on; its records stay
        // inspectable in Properties and the attribute table.
        m_processCombo->setItemData(row,
                                    !locked.isEmpty()   ? locked
                                    : status.isEmpty() ? QString()
                                                       : tr("%1: %2.").arg(name, status),
                                    Qt::ToolTipRole);
        if (auto *group = m_processGroups.value(static_cast<int>(process)))
            group->setToolTip(m_processCombo->itemData(row, Qt::ToolTipRole).toString());
    }
    // The picker sizes to its longest label; a new status can change that, and
    // a widget-hosting group only re-measures when told.
    m_groupProcess->refreshWidth();
    updateEnabledState(); // a lock follows its switch
}

void MeshEditingToolbar::setCurrentProcess(Process process)
{
    const int row = m_processCombo->findData(static_cast<int>(process));
    if (row >= 0 && m_processCombo->currentIndex() != row) {
        m_processCombo->setCurrentIndex(row); // re-enters through currentIndexChanged
        return;
    }
    if (m_process == process)
        return;
    m_process = process;
    for (QAction *action : std::as_const(m_processActions))
        if (action->data().toInt() == static_cast<int>(process))
            action->setChecked(true);
    updateGroupVisibility();
    emit processChanged(process);
}

void MeshEditingToolbar::addMeshAction(QAction *action)
{
    if (action)
        m_groupMesh->addAction(action);
    updateGroupVisibility();
}

void MeshEditingToolbar::addProcessActions(Process process, const QList<QAction *> &actions)
{
    auto *group = m_processGroups.value(static_cast<int>(process));
    if (!group || actions.isEmpty())
        return;
    if (actions.size() <= 2) {
        for (QAction *action : actions)
            group->addAction(action);
    } else {
        for (int i = 0; i < actions.size(); i += openswmmvis::ui::kRibbonStackRows)
            group->addStack(actions.mid(i, openswmmvis::ui::kRibbonStackRows));
    }
    updateGroupVisibility();
}

void MeshEditingToolbar::setCommandNeeds(QAction *action, Needs needs)
{
    if (!action)
        return;
    if (needs == Needs::Nothing)
        m_needs.remove(action);
    else
        m_needs.insert(action, needs);
    updateEnabledState();
}

void MeshEditingToolbar::setCommandProcess(QAction *action, Process process)
{
    if (!action)
        return;
    m_locks.insert(action, process);
    updateEnabledState();
}

QString MeshEditingToolbar::blockedReason(const QAction *action) const
{
    auto *key = const_cast<QAction *>(action);
    if (const auto lock = m_locks.constFind(key); lock != m_locks.cend() && m_processLock)
        if (QString why = m_processLock(*lock); !why.isEmpty())
            return why;
    switch (m_needs.value(key, Needs::Nothing)) {
    case Needs::Cells:
        return m_haveCells ? QString() : tr("Select one or more 2D cells with the Cells tool.");
    case Needs::Nothing:
        break;
    }
    return {};
}

void MeshEditingToolbar::addCellDataActions(const QList<QAction *> &actions)
{
    for (QAction *action : actions)
        if (action)
            m_groupCellData->addAction(action);
    updateGroupVisibility();
}

void MeshEditingToolbar::addProfileAction(QAction *action)
{
    if (action)
        m_groupProfile->addAction(action);
    updateGroupVisibility();
}

void MeshEditingToolbar::updateGroupVisibility()
{
    // A group shows when it has commands; process groups only for the
    // picked process. Hiding goes through the bar's action so QToolBar's
    // layout (and the compactor, which skips hidden groups) agree.
    for (auto it = m_groupSlots.cbegin(); it != m_groupSlots.cend(); ++it) {
        auto *group = it.key();
        bool show = !group->groupActions().isEmpty() || group == m_groupMesh ||
                    group == m_groupProcess;
        const int process = m_processGroups.key(group, -1);
        if (process >= 0)
            show = show && process == static_cast<int>(m_process);
        it.value()->setVisible(show);
    }
}

// ---------------------------------------------------------------------
// Canvas rebinding
// ---------------------------------------------------------------------

void MeshEditingToolbar::rebindCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas)
        return;

    if (m_canvas) {
        disconnect(m_canvas, &MapCanvas::layerAdded, this, &MeshEditingToolbar::onLayerAdded);
        disconnect(m_canvas, &MapCanvas::layerRemoved, this, &MeshEditingToolbar::onLayerRemoved);
    }
    if (m_activeMesh) {
        disconnectMeshLayer(m_activeMesh);
        m_activeMesh = nullptr;
    }
    m_canvas = canvas;
    if (m_canvas) {
        connect(m_canvas, &MapCanvas::layerAdded, this, &MeshEditingToolbar::onLayerAdded);
        connect(m_canvas, &MapCanvas::layerRemoved, this, &MeshEditingToolbar::onLayerRemoved);
    }
    m_hover->setCanvas(m_canvas);
    rebuildMeshCombo();
    refreshProcessStatus();
}

void MeshEditingToolbar::rebindSelectionManager(SelectionManager *sel)
{
    if (m_selection == sel)
        return;
    if (m_selection) {
        disconnect(m_selection, &SelectionManager::selectionChanged, this,
                   &MeshEditingToolbar::onSelectionChanged);
    }
    m_selection = sel;
    if (m_selection) {
        connect(m_selection, &SelectionManager::selectionChanged, this,
                &MeshEditingToolbar::onSelectionChanged);
    }
    updateEnabledState();
}

void MeshEditingToolbar::showEvent(QShowEvent *event)
{
    QToolBar::showEvent(event);
    refreshProcessStatus();
}

// ---------------------------------------------------------------------
// Mesh combo bookkeeping
// ---------------------------------------------------------------------

void MeshEditingToolbar::rebuildMeshCombo()
{
    QSignalBlocker block(m_meshCombo);
    m_meshCombo->clear();
    m_meshCombo->addItem(tr("(none)"), QVariant::fromValue<quintptr>(0));

    if (!m_canvas) {
        m_meshCombo->setEnabled(false);
        updateEnabledState();
        return;
    }

    int activeRow = kNoneIndex;
    int firstMeshRow = kNoneIndex;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        auto *mesh = qobject_cast<SWMM2DMeshLayer *>(l);
        if (!mesh)
            continue;
        const QString label =
            mesh->sourcePath().isEmpty() ? mesh->name() : QFileInfo(mesh->sourcePath()).fileName();
        m_meshCombo->addItem(label,
                             QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(mesh)));
        if (firstMeshRow == kNoneIndex)
            firstMeshRow = m_meshCombo->count() - 1;
        if (mesh->isActiveMesh())
            activeRow = m_meshCombo->count() - 1;
    }
    // If no layer is flagged active but at least one mesh exists, auto-
    // pick the first one — otherwise the user opens the tab and the
    // edit toggles are disabled with no obvious next action.
    if (activeRow == kNoneIndex && firstMeshRow != kNoneIndex)
        activeRow = firstMeshRow;
    m_meshCombo->setCurrentIndex(activeRow);
    m_meshCombo->setEnabled(m_meshCombo->count() > 1);

    // Sync internal state with the combo's current row (no signal because
    // we're blocking, so step through the same logic manually).
    SWMM2DMeshLayer *picked = nullptr;
    if (activeRow != kNoneIndex) {
        picked =
            reinterpret_cast<SWMM2DMeshLayer *>(m_meshCombo->itemData(activeRow).value<quintptr>());
    }
    if (picked != m_activeMesh) {
        if (m_activeMesh)
            disconnectMeshLayer(m_activeMesh);
        m_activeMesh = picked;
        if (m_activeMesh)
            connectMeshLayer(m_activeMesh);
    }
    m_hover->setActiveMesh(m_activeMesh);
    updateEnabledState();
}

void MeshEditingToolbar::onActiveMeshComboChanged(int index)
{
    SWMM2DMeshLayer *picked = nullptr;
    if (index != kNoneIndex && index >= 0 && index < m_meshCombo->count()) {
        picked =
            reinterpret_cast<SWMM2DMeshLayer *>(m_meshCombo->itemData(index).value<quintptr>());
    }

    if (m_activeMesh && m_activeMesh != picked) {
        disconnectMeshLayer(m_activeMesh);
        // Q-V4: combo selection drives setActiveMesh on the picked layer
        // and clears it on every other mesh on the canvas. One source of
        // truth (the isActiveMesh flag); no parallel "currently editing".
        m_activeMesh->setActiveMesh(false);
    }
    if (m_canvas) {
        for (OpenSWMMVisLayer *l : m_canvas->layers()) {
            auto *mesh = qobject_cast<SWMM2DMeshLayer *>(l);
            if (!mesh || mesh == picked)
                continue;
            if (mesh->isActiveMesh())
                mesh->setActiveMesh(false);
        }
    }
    m_activeMesh = picked;
    if (m_activeMesh) {
        m_activeMesh->setActiveMesh(true);
        connectMeshLayer(m_activeMesh);
    }
    m_hover->setActiveMesh(m_activeMesh);
    updateEnabledState();
}

void MeshEditingToolbar::onLayerAdded(OpenSWMMVisLayer *layer)
{
    if (qobject_cast<SWMM2DMeshLayer *>(layer))
        rebuildMeshCombo();
}


void MeshEditingToolbar::connectMeshLayer(SWMM2DMeshLayer *layer)
{
    if (!layer)
        return;
    connect(layer, &SWMM2DMeshLayer::activeMeshChanged, this,
            &MeshEditingToolbar::onActiveMeshFlagChanged, Qt::UniqueConnection);
    // Qt 6 asserts on Qt::UniqueConnection with non-PMF slots — lambdas
    // can't be compared for identity, so Qt can't de-dupe. Disconnect any
    // prior handler on this (layer, signal, this) tuple first, then make
    // exactly one fresh connection.
    QObject::disconnect(layer, &SWMM2DMeshLayer::selectionInvalidated, this, nullptr);
    connect(layer, &SWMM2DMeshLayer::selectionInvalidated, this, [this] {
        if (m_selection)
            m_selection->clear();
        updateEnabledState();
    });
}

void MeshEditingToolbar::onLayerRemoved(OpenSWMMVisLayer *layer)
{
    auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer);
    if (!mesh)
        return;
    if (m_activeMesh == mesh) {
        disconnectMeshLayer(m_activeMesh);
        m_activeMesh = nullptr;
        m_hover->setActiveMesh(nullptr);
    }
    rebuildMeshCombo();
}

void MeshEditingToolbar::onActiveMeshFlagChanged(bool /*isActive*/)
{
    // Re-sync the combo if some other entry point (e.g. Layer Tree
    // right-click) flipped a mesh's active flag.
    rebuildMeshCombo();
}

void MeshEditingToolbar::disconnectMeshLayer(SWMM2DMeshLayer *layer)
{
    if (!layer)
        return;
    disconnect(layer, nullptr, this, nullptr);
}

QList<int> MeshEditingToolbar::currentSelectedVertices() const
{
    QList<int> out;
    if (!m_selection || !m_activeMesh)
        return out;
    const QString wantKey = mesh::MeshObjectRef::layerKey(m_activeMesh->sourcePath());
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshVertex)
            continue;
        QString lk;
        int vi = -1;
        if (!mesh::MeshObjectRef::parseVertex(ref, &lk, &vi))
            continue;
        if (lk != wantKey)
            continue;
        out.append(vi);
    }
    return out;
}

QList<int> MeshEditingToolbar::currentSelectedCells() const
{
    QList<int> out;
    if (!m_selection)
        return out;
    // Accept every MeshCell ref (no strict layerKey filter) so the label still
    // works when the 2D mesh is results-only (no SWMM2DMeshLayer to key by).
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshCell)
            continue;
        QString lk;
        int tri = -1;
        if (mesh::MeshObjectRef::parseCell(ref, &lk, &tri))
            out.append(tri);
    }
    return out;
}

QList<QPair<int, int>> MeshEditingToolbar::currentSelectedEdges() const
{
    QList<QPair<int, int>> out;
    if (!m_selection || !m_activeMesh)
        return out;
    const QString wantKey = mesh::MeshObjectRef::layerKey(m_activeMesh->sourcePath());
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshEdge)
            continue;
        QString lk;
        int tri = -1, e = -1;
        if (!mesh::MeshObjectRef::parseEdge(ref, &lk, &tri, &e))
            continue;
        if (lk != wantKey)
            continue;
        out.append(qMakePair(tri, e));
    }
    // SelectionManager holds a QSet, whose iteration order is hash-seeded
    // per process — sort so "the first selected edge", command slot order,
    // and everything displayed from this list are deterministic.
    std::sort(out.begin(), out.end());
    return out;
}

void MeshEditingToolbar::onAutoCoupleClicked()
{
    if (!m_activeMesh)
        return;
    const QVector<QPair<QString, QPointF>> nodes =
        m_nodeLocator ? m_nodeLocator() : QVector<QPair<QString, QPointF>>{};
    if (nodes.isEmpty()) {
        QMessageBox::information(
            this, tr("Auto-couple"),
            tr("No SWMM nodes with coordinates are available to couple against."));
        return;
    }
    // Selection if any, else the whole mesh.
    const QList<int> targets = currentSelectedVertices();
    const auto result = mesh::findCoincidentNodes(m_activeMesh->mesh(), nodes, targets);

    int coupled = 0;
    for (auto it = result.matches.cbegin(); it != result.matches.cend(); ++it)
        if (m_activeMesh->applyMeshVertexCoupledNode(it.key(), it.value()))
            ++coupled;

    QString msg = tr("Coupled %1 vertex(es) to coincident SWMM nodes.").arg(coupled);
    if (result.alreadyCoupled > 0)
        msg += tr("\n%1 coincident vertex(es) were already coupled and left unchanged.")
                   .arg(result.alreadyCoupled);
    if (result.unmatchedNodes > 0)
        msg += tr("\n%1 node(s) had no coincident %2vertex.")
                   .arg(result.unmatchedNodes)
                   .arg(targets.isEmpty() ? QString() : tr("selected "));
    QMessageBox::information(this, tr("Auto-couple"), msg);
}

void MeshEditingToolbar::onRemapClicked()
{
    if (!m_activeMesh)
        return;
    const QVector<QPair<QString, QPointF>> nodes =
        m_nodeLocator ? m_nodeLocator() : QVector<QPair<QString, QPointF>>{};
    SWMMModelLayer *model = nullptr;
    if (m_canvas)
        for (OpenSWMMVisLayer *l : m_canvas->layers())
            if ((model = qobject_cast<SWMMModelLayer *>(l)))
                break;
    if (nodes.isEmpty() || !model) {
        QMessageBox::information(this, tr("Remap 1D↔2D"),
                                 tr("No SWMM nodes with coordinates are available to map."));
        return;
    }
    QStringList selected;
    if (m_selection)
        for (const SWMMObjectRef &r : m_selection->selection())
            if (r.objectType == SWMMObjectRef::Node)
                selected.append(r.name);

    // 2D aquifer preview — read-only; the engine resolves the same mapping
    // at initialize (NODE_ENROLMENT / LINK_SEEPAGE AUTO).
    QString msg;
    if (const auto gwIn =
            m_gwPreviewSource ? m_gwPreviewSource() : std::optional<mesh::GwPreviewInput>{}) {
        const mesh::GwPreviewResult g =
            mesh::previewGroundwaterMapping(m_activeMesh->mesh(), *gwIn);
        auto listed = [this](const QStringList &ids) {
            const QStringList head = ids.mid(0, 8);
            return head.join(QStringLiteral(", ")) +
                   (ids.size() > head.size() ? QStringLiteral(", …") : QString());
        };
        auto pct = [](double part, double whole) {
            return whole > 0.0 ? QString::number(100.0 * part / whole, 'f', 0)
                               : QStringLiteral("0");
        };
        msg += tr("2D aquifer (preview — resolved by the engine at run start):");
        msg += tr("\n• Nodes exchanging with the aquifer: %1; outside the mesh "
                  "(legacy exfiltration, a system loss): %2")
                   .arg(g.nodesInside)
                   .arg(g.nodesOutside.size());
        if (!gwIn->seepingConduits.isEmpty())
            msg += tr("\n• Seeping conduits recharging the aquifer: %1 (%2% of "
                      "their length inside the mesh); entirely outside: %3%4")
                       .arg(g.conduitsInside)
                       .arg(pct(g.conduitLengthInside, g.conduitLengthTotal))
                       .arg(g.conduitsOutside.size())
                       .arg(g.conduitsOutside.isEmpty()
                                ? QString()
                                : QStringLiteral(" (") + listed(g.conduitsOutside) + ')');
        if (!gwIn->subcatchments.isEmpty()) {
            msg +=
                tr("\n• Subcatchment geometric overlaps: %1 (%2% of "
                   "their polygon area inside the mesh; recharge unavailable until reviewed coupling is qualified)")
                    .arg(g.subcatchTo2D)
                    .arg(pct(g.subcatchAreaInside, g.subcatchAreaTotal));
            if (!g.subcatchLumped.isEmpty())
                msg += tr("\n• Subcatchments kept on their lumped groundwater "
                          "aquifer: %1")
                           .arg(g.subcatchLumped.size());
            if (!g.subcatchOutside.isEmpty())
                msg += tr("\n• Subcatchments outside the mesh (infiltration is a "
                          "system loss): %1 (%2)")
                           .arg(g.subcatchOutside.size())
                           .arg(listed(g.subcatchOutside));
            if (!g.subcatchNoPolygon.isEmpty())
                msg += tr("\n• Subcatchments without a polygon (not mapped): %1 (%2)")
                           .arg(g.subcatchNoPolygon.size())
                           .arg(listed(g.subcatchNoPolygon));
        }
    }
    // The dialog replaces the stacked message boxes (plan §7.3 / D8): the
    // surface-ownership review is a button on it, and Apply is one undo step.
    Remap1D2DDialog dlg(model, m_activeMesh, m_canvas, nodes, selected, msg,
                        m_surfaceOwnershipReview, this);
    dlg.exec();
}

void MeshEditingToolbar::onSelectionChanged()
{
    // Push current selection into every mesh layer's highlight sets so
    // the renderer paints the selection overlay. We iterate ALL mesh
    // layers on the canvas (not just m_activeMesh) so picks made
    // against a fallback layer when no mesh is "active" still light up.
    // Each layer only keeps refs whose layerKey matches its own.
    if (m_selection && m_canvas) {
        for (OpenSWMMVisLayer *l : m_canvas->layers()) {
            auto *mesh = qobject_cast<SWMM2DMeshLayer *>(l);
            if (!mesh)
                continue;
            const QString wantKey = mesh::MeshObjectRef::layerKey(mesh->sourcePath());
            QSet<int> selV;
            QSet<int> selE;
            QSet<int> selT;
            for (const SWMMObjectRef &ref : m_selection->selection()) {
                if (ref.objectType == SWMMObjectRef::MeshVertex) {
                    QString lk;
                    int vi = -1;
                    if (mesh::MeshObjectRef::parseVertex(ref, &lk, &vi) && lk == wantKey)
                        selV.insert(vi);
                } else if (ref.objectType == SWMMObjectRef::MeshEdge) {
                    QString lk;
                    int tri = -1, e = -1;
                    if (mesh::MeshObjectRef::parseEdge(ref, &lk, &tri, &e) && lk == wantKey)
                        selE.insert(mesh::edgeSlot(tri, e));
                } else if (ref.objectType == SWMMObjectRef::MeshCell) {
                    QString lk;
                    int tri = -1;
                    if (mesh::MeshObjectRef::parseCell(ref, &lk, &tri) && lk == wantKey)
                        selT.insert(tri);
                }
            }
            mesh->setHighlightedVertices(selV);
            mesh->setHighlightedEdges(selE);
            mesh->setHighlightedTriangles(selT);
        }
        // The 2D RESULTS-layer cell highlight is applied by
        // SWMMVisProjectWindow against the active results layer only.
    }
    updateEnabledState();
}

void MeshEditingToolbar::onHoverElevation(double z, bool finite)
{
    emit hoverElevationChanged(z, finite);
}

void MeshEditingToolbar::updateEnabledState()
{
    const bool haveMesh = m_activeMesh != nullptr;
    m_actEditVertex->setEnabled(haveMesh);
    m_actEditEdge->setEnabled(haveMesh);
    m_actAutoCouple->setEnabled(haveMesh);
    m_actRemap->setEnabled(haveMesh);
    m_actEditInTable->setEnabled(haveMesh);
    m_actClearSelection->setEnabled(m_selection && !m_selection->isEmpty());
    m_actReviewOwnership->setEnabled(haveMesh && bool(m_surfaceOwnershipReview));

    m_haveCells = !currentSelectedCells().isEmpty();
    for (QAction *action : m_needs.keys() + m_locks.keys())
        action->setEnabled(blockedReason(action).isEmpty());
    emit commandStatesChanged();
}

