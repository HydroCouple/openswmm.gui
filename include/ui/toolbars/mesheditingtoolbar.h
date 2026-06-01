/*!
 * \file   mesheditingtoolbar.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice §V.VB — Mesh Editing toolbar.
 *
 * Peer of TerrainToolbar. Lets the user pick which 2D mesh is active,
 * hover-read elevation, click a vertex and retype its elevation, and
 * (in §V.VC) select edges and assign per-edge BCs.
 *
 * MVC contract: the toolbar is one of N views over `SWMM2DMeshLayer`.
 * It reads from the layer on every focus (no shadow state), writes
 * through `applyMesh*` helpers, and subscribes to `attributeChanged`
 * so edits from other views (future Property Browser, Attribute Table)
 * keep the toolbar's spinbox in sync. The active-mesh combo mirrors
 * the existing `SWMM2DMeshLayer::isActiveMesh` flag (Q-V4-resolved).
 *
 * Lifecycle: `rebindCanvas(MapCanvas*)` is called by SWMMVis each time
 * the active project window changes — mirrors TerrainToolbar's pattern.
 */
#ifndef OPENSWMMVIS_UI_TOOLBARS_MESHEDITINGTOOLBAR_H
#define OPENSWMMVIS_UI_TOOLBARS_MESHEDITINGTOOLBAR_H

#include <QPointer>
#include <QToolBar>

#include <functional>

class MapCanvas;
class OpenSWMMVisLayer;
class SWMM2DMeshLayer;

class QAction;
class QActionGroup;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolButton;

namespace mesh { class MeshHoverProbe; }

class SelectionManager;
struct SWMMObjectRef;

class MeshEditingToolbar : public QToolBar
{
    Q_OBJECT
public:
    explicit MeshEditingToolbar(const QString &title, QWidget *parent = nullptr);
    ~MeshEditingToolbar() override;

    /*! \brief Bind the toolbar to a new project canvas. Disconnects from
     *  the previous canvas's signals, populates the mesh combo from the
     *  new canvas's `SWMM2DMeshLayer` instances, and re-establishes the
     *  active-mesh probe / selection subscriptions. Pass nullptr when no
     *  project is active (clears + disables). */
    void rebindCanvas(MapCanvas *canvas);

    /*! \brief Bind the toolbar to a project's SelectionManager so the
     *  spinbox / edge-info label update when selection changes (e.g.
     *  from a future Property Browser tab or an Attribute Table click).
     *  Pass nullptr to detach. */
    void rebindSelectionManager(SelectionManager *sel);

    /*! \brief Install a callback that opens a timeseries picker dialog
     *  and returns the picked name (empty = user cancelled). Per
     *  feedback_data_object_pickers, the implementation must dispatch
     *  through `TimeseriesEditorDialog::pickTimeseries`. SWMMVis sets
     *  this; the toolbar stays decoupled from the registries.
     *  Same shape for the curve picker (RatingCurve). */
    using PickerFn = std::function<QString(const QString &currentName)>;
    void setStageTimeseriesPicker(PickerFn fn) { m_stageTSPicker = std::move(fn); }
    void setFlowTimeseriesPicker(PickerFn fn)  { m_flowTSPicker  = std::move(fn); }
    void setRatingCurvePicker(PickerFn fn)     { m_curvePicker   = std::move(fn); }

    /*! \brief Install lister callbacks that return the available
     *  timeseries / curve names for the project. The TS/curve BC pages
     *  populate their QComboBox from these so the user can pick an
     *  existing object without opening the CRUD dialog. Empty list →
     *  combobox shows only "(none)". */
    using ListerFn = std::function<QStringList()>;
    void setTimeseriesLister(ListerFn fn) { m_tsLister    = std::move(fn); }
    void setCurveLister(ListerFn fn)      { m_curveLister = std::move(fn); }
    void refreshBCNameLists();    // re-query listers + repopulate combos

    /*! \brief Add a trailing tool action (e.g. Pick 2D Cells / Trace Profile)
     *  to the *left* of the expanding spacer so it stays visible. Use this
     *  instead of QToolBar::addAction, which appends after the spacer and
     *  pushes the action off the right edge into the overflow chevron. */
    void addToolAction(QAction *action);
    void addToolSeparator();

    /*! \brief Add a trailing widget (e.g. the cell-selection info label) to
     *  the left of the expanding spacer, mirroring addToolAction. Returns the
     *  QAction handle for the embedded widget. */
    QAction *addToolWidget(QWidget *widget);

    /*! \brief The cell-selection info label (created in the ctor; placed by
     *  SWMMVis right after the Select-2D-Cells action via addToolWidget). */
    [[nodiscard]] QLabel *cellInfoLabel() const { return m_cellInfoLbl; }

    [[nodiscard]] SWMM2DMeshLayer *activeMesh() const { return m_activeMesh; }

signals:
    /*! \brief Emitted when the user toggles the Edit Vertex action.
     *  SWMMVis hooks this to activate / deactivate MapToolMeshSelectVertex
     *  on the corresponding project window. */
    void editVertexToggled(bool active);

    /*! \brief Emitted when the user toggles the Edit Edge action. */
    void editEdgeToggled(bool active);

private slots:
    void onActiveMeshComboChanged(int index);
    void onLayerAdded(OpenSWMMVisLayer *layer);
    void onLayerRemoved(OpenSWMMVisLayer *layer);
    void onActiveMeshFlagChanged(bool isActive);
    void onAttributeChanged(const QString &refName);
    void onHoverElevation(double z, bool finite);
    void onZSpinChanged(double z);
    void onSelectionChanged();
    void onBCTypeChanged(int index);
    void commitBCParam();             // apply-as-you-go: write current param to selected edges
    void commitConveyance();          // apply-as-you-go: ψ → every selected edge (interior + boundary)
    void onBrowseBCObject();          // shared "…" → dispatches to right CRUD picker
    void onPickStageTseries();
    void onPickFlowTseries();
    void onPickRatingCurve();

private:
    void rebuildMeshCombo();
    void connectMeshLayer(SWMM2DMeshLayer *layer);
    void disconnectMeshLayer(SWMM2DMeshLayer *layer);
    void refreshVertexEditor();
    void refreshEdgeEditor();
    void refreshCellEditor();
    void updateEnabledState();
    int  currentSingleSelectedVertex() const;  // -1 if not exactly one
    QList<int> currentSelectedVertices() const;          // all selected vertex indices
    QList<QPair<int,int>> currentSelectedEdges() const;  // (tri, eLocal) pairs
    QList<int> currentSelectedCells() const;             // all selected triangle indices

    QPointer<MapCanvas>           m_canvas;
    QPointer<SelectionManager>    m_selection;
    QPointer<SWMM2DMeshLayer>     m_activeMesh;
    mesh::MeshHoverProbe         *m_hover = nullptr;

    // Top row
    QComboBox     *m_meshCombo      = nullptr;
    QLabel        *m_hoverLabel     = nullptr;
    QAction       *m_spacerAction   = nullptr;   // trailing stretch; tool actions insert before it

    // Edit mode toggles
    QAction       *m_actEditVertex  = nullptr;
    QAction       *m_actEditEdge    = nullptr;
    QActionGroup  *m_editGroup      = nullptr;

    // Vertex Z editor
    QLabel        *m_vertexInfoLbl  = nullptr;
    QDoubleSpinBox*m_zSpin          = nullptr;

    // BC controls (Slice §V.VC — fully wired for all 7 GUI BC types).
    QComboBox     *m_bcTypeCombo    = nullptr;
    QStackedWidget*m_bcParamStack   = nullptr;
    QAction       *m_actBrowseObj   = nullptr;   // launches TS / Curve CRUD dialog
    QLabel        *m_edgeInfoLbl    = nullptr;
    // addWidget() handles for the BC combo + param stack so they can be
    // shown/hidden contextually (hiding the QAction collapses the gap).
    QAction       *m_actBCTypeCombo  = nullptr;
    QAction       *m_actBCParamStack = nullptr;

    // Cell-selection info label (after the Select-2D-Cells tool).
    QLabel        *m_cellInfoLbl    = nullptr;

    // Param-page widgets (held directly so the apply path reads values
    // without going through QStackedWidget::currentWidget casts).
    QDoubleSpinBox*m_slopeSpin      = nullptr;  // NormalFlow
    QDoubleSpinBox*m_stageSpin      = nullptr;  // SpecifiedStageConst
    QComboBox     *m_stageTSCombo   = nullptr;  // SpecifiedStageTS — editable combo over registry
    QDoubleSpinBox*m_flowSpin       = nullptr;  // SpecifiedFlowConst
    QComboBox     *m_flowTSCombo    = nullptr;  // SpecifiedFlowTS  — editable combo over registry
    QComboBox     *m_curveCombo     = nullptr;  // RatingCurve     — editable combo over registry

    // Engine §11A — per-edge flux attenuation (conveyance) widget. Lives
    // OUTSIDE the BC param stack because it applies to every edge (interior
    // and boundary), not just boundary edges. Visibility tracks edgeMode +
    // any selected edge (independent of haveBoundaryEdge).
    QDoubleSpinBox*m_conveySpin     = nullptr;
    QAction       *m_actConveySpin  = nullptr;

    bool           m_suppressZSignal = false;

    PickerFn       m_stageTSPicker;
    PickerFn       m_flowTSPicker;
    PickerFn       m_curvePicker;

    ListerFn       m_tsLister;
    ListerFn       m_curveLister;
};

#endif // OPENSWMMVIS_UI_TOOLBARS_MESHEDITINGTOOLBAR_H
