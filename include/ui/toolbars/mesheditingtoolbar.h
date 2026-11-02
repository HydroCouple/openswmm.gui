/**
 * @file      mesheditingtoolbar.h
 * @brief     The Mesh 2D ribbon tab: active mesh, process picker, selection tools and the
 *            active process's configurators.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Process-aware editing plan, Phase 2 (docs/development/process-aware-spatial-editing.md §4.2,
 * §6.1). The tab reads left to right: Mesh → Process → Select → the active process's group →
 * Cell Data → Profile. The ribbon holds selectors and commands only. Values are edited in
 * Properties (one element) and the attribute table (bulk), which share one field model and
 * undo path; the former inline editors (vertex Z / tag / coupling, edge BC pages, conveyance,
 * cell parameter page) left the ribbon so its width no longer changes with the selection.
 *
 * MVC contract: the toolbar is one of N views over `SWMM2DMeshLayer`. The active-mesh combo
 * mirrors the layer's `isActiveMesh` flag; selection lives in the project's SelectionManager;
 * the surface boundary command reuses the attribute-table model's editor, so every route
 * writes through the same commands.
 *
 * Lifecycle: `rebindCanvas(MapCanvas*)` is called by SWMMVis each time the active project
 * window changes — mirrors TerrainToolbar's pattern.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_TOOLBARS_MESHEDITINGTOOLBAR_H
#define OPENSWMMVIS_UI_TOOLBARS_MESHEDITINGTOOLBAR_H

#include "mesh/meshgwpreview.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QPointF>
#include <QPointer>
#include <QToolBar>
#include <QVector>

#include <functional>
#include <optional>

namespace openswmmvis::ui
{
class RibbonGroup;
}

class MapCanvas;
class MeshAttributeTableModel;
class OpenSWMMVisLayer;
class SWMM2DMeshLayer;

class QAction;
class QActionGroup;
class QComboBox;

namespace mesh
{
class MeshHoverProbe;
}

class SelectionManager;
struct SWMMObjectRef;

class MeshEditingToolbar : public QToolBar
{
    Q_OBJECT
public:
    /// Processes whose configurators the tab groups. The picker shows one group at a time.
    enum class Process { SurfaceFlow, Infiltration, Groundwater, SurfaceQuality, Snow, Heat };
    /// Selection a command needs before it can act (enables/disables it).
    enum class Needs { Nothing, Cells, BoundaryEdges };

    /**
     * @param[in] title Display title copied into QToolBar.
     * @param[in] parent Optional QWidget owner, caller owns until parented/hosted.
     * @brief Build the captioned groups, the vertex/edge/cell selection tools, the mesh-wide
     *   coupling commands, the surface boundary command and the owned hover probe.
     * @note GUI-thread only. No canvas/model/selection initially; callbacks are injected later
     *   and bound objects stay externally owned.
     */
    explicit MeshEditingToolbar(const QString &title, QWidget *parent = nullptr);
    /** @brief Destroy owned widgets/actions; bound canvas, mesh and selection stay external. */
    ~MeshEditingToolbar() override;

protected:
    /**
     * @param[in] event Required borrowed Qt show event.
     * @note Refreshes the process status labels (options may have changed while hidden).
     */
    void showEvent(QShowEvent *event) override;

public:
    /*! \brief Bind the toolbar to a new project canvas. Disconnects from
     *  the previous canvas's signals, populates the mesh combo from the
     *  new canvas's `SWMM2DMeshLayer` instances, and re-establishes the
     *  active-mesh probe. Pass nullptr when no project is active.
     * @param[in] canvas Optional borrowed canvas guarded by QPointer; null detaches.
     * @note Equality including null→null no-op. Does not rebind the SelectionManager.
     */
    void rebindCanvas(MapCanvas *canvas);

    /*! \brief Bind the toolbar to a project's SelectionManager: selection drives the mesh
     *  highlight and the selection-scoped commands' enabled state. Pass nullptr to detach.
     * @param[in] sel Optional borrowed selection manager guarded by QPointer.
     */
    void rebindSelectionManager(SelectionManager *sel);

    /*! \brief Install a callback that returns every SWMM node id with its map position
     *  (Auto-couple and Remap 1D↔2D match vertices / cells against these). */
    using NodeLocatorFn = std::function<QVector<QPair<QString, QPointF>>()>;
    /**
     * @param[in] fn Moved synchronous locator; empty means no nodes are available.
     */
    void setNodeLocator(NodeLocatorFn fn) { m_nodeLocator = std::move(fn); }

    /*! \brief Optional read-only 2D aquifer preview input for the Remap dialog. */
    using GwPreviewSourceFn = std::function<std::optional<mesh::GwPreviewInput>()>;
    /**
     * @param[in] fn Moved callback opening the surface-ownership review. Drives the
     *   Review Ownership command and the Remap dialog's review button; empty disables both.
     */
    void setSurfaceOwnershipReview(std::function<void()> fn);
    /** @param[in] fn Moved preview-input callback; empty omits the aquifer preview text. */
    void setGwPreviewSource(GwPreviewSourceFn fn) { m_gwPreviewSource = std::move(fn); }

    /*! \brief Short status for a process ("off", "no pollutants"); empty when the
     *  process is in use. Shown beside its name in the picker and as
     *  the group's tooltip. Informational only: configurators stay reachable so a process can
     *  be set up before it is switched on. */
    using ProcessStatusFn = std::function<QString(Process)>;
    /** @param[in] fn Moved status callback; empty shows no status. Refreshes immediately. */
    void setProcessStatusProvider(ProcessStatusFn fn);
    /** @brief Re-query the status callback and relabel the picker (also on show and popup). */
    void refreshProcessStatus();

    /** @brief Add a mesh-wide command (Burn Channels) to the Mesh group as a large face.
     * @param[in] action Optional borrowed action; null ignored, ownership unchanged. */
    void addMeshAction(QAction *action);
    /** @brief Populate a process group: up to two large faces, otherwise stacks of three.
     * @param[in] process Group to fill; call once per process.
     * @param[in] actions Borrowed live actions in display order; ownership unchanged. */
    void addProcessActions(Process process, const QList<QAction *> &actions);
    /** @brief Enable \a action only while the selection holds what it needs.
     * @param[in] action Borrowed action already placed on the tab.
     * @param[in] needs Required selection; Nothing removes the requirement. */
    void setCommandNeeds(QAction *action, Needs needs);
    /** @brief Fill the Cell Data group (GIS sampling of cell parameters) with large faces.
     * @param[in] actions Borrowed live actions in display order. */
    void addCellDataActions(const QList<QAction *> &actions);
    /** @brief Add the mesh profile tool to its Profile group.
     * @param[in] action Optional borrowed action; null ignored. */
    void addProfileAction(QAction *action);

    /** @return Process whose group is shown. */
    [[nodiscard]] Process currentProcess() const { return m_process; }
    /** @brief Show \a process's group and select it in the picker. */
    void setCurrentProcess(Process process);

    /** @return Owned checkable "Select 2D Cells" tool (objectName actionPick2DCells); the host
     *  connects its toggled signal to the canvas tool. */
    [[nodiscard]] QAction *pickCellsAction() const { return m_actPickCells; }
    /** @return Owned checkable actions, one per process in picker order, kept in step with the
     *  picker; the host lists them under Model → Mesh → Process (and the palette finds them). */
    [[nodiscard]] QList<QAction *> processActions() const { return m_processActions; }
    /** @return Owned "Review Ownership…" command; the host places it in a process group. */
    [[nodiscard]] QAction *reviewOwnershipAction() const { return m_actReviewOwnership; }

    /**
     * @return Borrowed currently bound mesh guarded by QPointer, null after destruction. Can be
     *   auto-selected first mesh without isActiveMesh flag set by rebuild.
     */
    [[nodiscard]] SWMM2DMeshLayer *activeMesh() const { return m_activeMesh; }

signals:
    /** @param[in] active Vertex selection tool toggled on/off. */
    void editVertexToggled(bool active);
    /** @param[in] active Edge selection tool toggled on/off. */
    void editEdgeToggled(bool active);
    /** @param[in] z Elevation under the cursor; @param[in] finite false outside the mesh. */
    void hoverElevationChanged(double z, bool finite);
    /** @brief "Edit in Table": show \a mesh's element table for \a meshKind
     *  (0 vertices, 1 edges, 2 cells), chosen from the selection or the active tool. */
    void editInTableRequested(SWMM2DMeshLayer *mesh, int meshKind);
    /** @param[in] process Newly shown process group. */
    void processChanged(MeshEditingToolbar::Process process);

private slots:
    void onActiveMeshComboChanged(int index);
    void onLayerAdded(OpenSWMMVisLayer *layer);
    void onLayerRemoved(OpenSWMMVisLayer *layer);
    void onActiveMeshFlagChanged(bool isActive);
    void onHoverElevation(double z, bool finite);
    void onAutoCoupleClicked(); // couple vertices to coincident SWMM nodes
    void onRemapClicked();      // Remap 1D↔2D: vertex + cell coupling (Plan C.4)
    void onSelectionChanged();
    void onBoundaryCondition(); // surface BC editor for the selected boundary edges

private:
    void rebuildMeshCombo();
    void connectMeshLayer(SWMM2DMeshLayer *layer);
    void disconnectMeshLayer(SWMM2DMeshLayer *layer);
    void updateEnabledState();
    void updateGroupVisibility();
    /** @brief Mount \a group on the bar and remember its slot so it can be hidden. */
    void mountGroup(openswmmvis::ui::RibbonGroup *group);
    QList<int> currentSelectedVertices() const;          // all selected vertex indices
    QList<QPair<int, int>> currentSelectedEdges() const; // (tri, eLocal) pairs
    QList<int> currentSelectedCells() const;             // all selected triangle indices

    QPointer<MapCanvas> m_canvas;
    QPointer<SelectionManager> m_selection;
    QPointer<SWMM2DMeshLayer> m_activeMesh;
    mesh::MeshHoverProbe *m_hover = nullptr;

    // Groups, in tab order; m_groupSlots holds each group's toolbar action (its visibility).
    openswmmvis::ui::RibbonGroup *m_groupMesh = nullptr;
    openswmmvis::ui::RibbonGroup *m_groupProcess = nullptr;
    openswmmvis::ui::RibbonGroup *m_groupSelect = nullptr;
    QHash<int, openswmmvis::ui::RibbonGroup *> m_processGroups; ///< Process → its group.
    openswmmvis::ui::RibbonGroup *m_groupCellData = nullptr;
    openswmmvis::ui::RibbonGroup *m_groupProfile = nullptr;
    QHash<openswmmvis::ui::RibbonGroup *, QAction *> m_groupSlots;

    QComboBox *m_meshCombo = nullptr;
    QComboBox *m_processCombo = nullptr;
    QList<QAction *> m_processActions; ///< Menu/palette mirror of the picker (exclusive).
    Process m_process = Process::SurfaceFlow;
    ProcessStatusFn m_processStatus;

    QAction *m_actEditVertex = nullptr;
    QAction *m_actEditEdge = nullptr;
    QAction *m_actPickCells = nullptr;
    QActionGroup *m_editGroup = nullptr;
    QAction *m_actClearSelection = nullptr;
    QAction *m_actEditInTable = nullptr;
    QAction *m_actAutoCouple = nullptr; // couple vertices to coincident nodes
    QAction *m_actRemap = nullptr;      // Remap 1D↔2D (Plan C.4)
    QAction *m_actBoundaryCondition = nullptr;
    QAction *m_actReviewOwnership = nullptr;
    QHash<QAction *, Needs> m_needs; ///< Selection-scoped commands.

    /// Edge model behind the Boundary Condition… editor; outlives the modeless dialog.
    MeshAttributeTableModel *m_edgeModel = nullptr;

    NodeLocatorFn m_nodeLocator;
    GwPreviewSourceFn m_gwPreviewSource;
    std::function<void()> m_surfaceOwnershipReview;
};

#endif // OPENSWMMVIS_UI_TOOLBARS_MESHEDITINGTOOLBAR_H
