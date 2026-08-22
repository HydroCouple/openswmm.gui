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

#include <QPair>
#include <QPointF>
#include <QPointer>
#include <QToolBar>
#include <QVector>

#include <functional>

namespace openswmmvis::ui { class RibbonGroup; }

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

protected:
    //! Flushes a node-list refresh that was deferred while hidden.
    void showEvent(QShowEvent *event) override;

public:

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
    /*! \brief Lister of couplable SWMM node ids for the coupled-node dropdown. */
    void setNodeLister(ListerFn fn)       { m_nodeLister = std::move(fn); }
    /*! \brief Locator of SWMM node ids + map coordinates ([COORDINATES],
     *  same CRS as the mesh vertices) for the Auto-couple action. */
    using NodeLocatorFn = std::function<QVector<QPair<QString, QPointF>>()>;
    void setNodeLocator(NodeLocatorFn fn) { m_nodeLocator = std::move(fn); }
    void refreshBCNameLists();    // re-query listers + repopulate combos
    void refreshNodeList();       // re-query node lister + repopulate the combo

    /*! \brief Add a trailing tool action (e.g. Pick 2D Cells / Trace Profile)
     *  to the *left* of the expanding spacer so it stays visible. Use this
     *  instead of QToolBar::addAction, which appends after the spacer and
     *  pushes the action off the right edge into the overflow chevron. */
    void addToolAction(QAction *action);
    void addToolSeparator();

    /*! \brief Add the mesh profile-plot action to its standalone "Profile"
     *  ribbon group (iteration 4 — kept apart from the cell-selection
     *  cluster whose width changes contextually). */
    void addProfileAction(QAction *action);

    /*! \brief Add a trailing widget (e.g. the cell-selection info label) to
     *  the left of the expanding spacer, mirroring addToolAction. Returns the
     *  QAction handle for the embedded widget. */
    QAction *addToolWidget(QWidget *widget);

    /*! \brief The cell-selection info label (created in the ctor; placed by
     *  SWMMVis right after the Select-2D-Cells action via addToolWidget). */
    [[nodiscard]] QLabel *cellInfoLabel() const { return m_cellInfoLbl; }

    /*! \brief Per-cell editor widgets. The first is a parameter page —
     *  a combo naming the attribute to prescribe (Manning's n, initial
     *  depth, …, from mesh::cellParamSpecs) beside the value editor; the
     *  second is the descriptive tag field.
     *
     *  Created in the ctor but NOT placed — SWMMVis positions them right
     *  after the cell info label so they sit in the 2D-cell group, then
     *  hands the embedding QActions back via setCellEditorActions() so the
     *  toolbar can hide them when no cell is selected. */
    [[nodiscard]] QWidget *cellParamEditorWidget() const;
    [[nodiscard]] QWidget *cellTagWidget() const;
    void setCellEditorActions(QAction *paramAct, QAction *tagAct);

    /*! \brief Place a cell-scoped action (currently "Assign Infiltration to
     *  Selection…") in the 2D-cell cluster, beside the param / tag editors.
     *
     *  Enabled only while at least one cell is selected — the dialog it opens
     *  reads the selection, so offering it with nothing selected is offering a
     *  dead end. Disabled rather than hidden (unlike the editor widgets)
     *  because the same QAction is mirrored into the Model ▸ Mesh menu. */
    void addCellAction(QAction *action);

    /*! \brief Key of the parameter currently shown in the cell editor
     *         (mesh::CellParamSpec::key). Empty when the editor is absent. */
    [[nodiscard]] QByteArray currentCellParamKey() const;

    /*! \brief Project depth unit ("ft" / "m") shown on length-valued cell
     *         parameters. Set by SWMMVis on each project-window switch. */
    void setDepthUnitLabel(const QString &label);

    [[nodiscard]] SWMM2DMeshLayer *activeMesh() const { return m_activeMesh; }

signals:
    /*! \brief Emitted when the user toggles the Edit Vertex action.
     *  SWMMVis hooks this to activate / deactivate MapToolMeshSelectVertex
     *  on the corresponding project window. */
    void editVertexToggled(bool active);

    /*! \brief Emitted when the user toggles the Edit Edge action. */
    void editEdgeToggled(bool active);

    /*! \brief Mirrors the mesh hover-Z probe outward (\a finite false =
     *  cursor off-mesh). SWMMVis feeds this into the status-bar
     *  coordinate readout so the elevation survives the toolbar's
     *  tab-scoped visibility. */
    void hoverElevationChanged(double z, bool finite);

private slots:
    void onActiveMeshComboChanged(int index);
    void onLayerAdded(OpenSWMMVisLayer *layer);
    void onLayerRemoved(OpenSWMMVisLayer *layer);
    void onActiveMeshFlagChanged(bool isActive);
    void onAttributeChanged(const QString &refName);
    void onHoverElevation(double z, bool finite);
    void onZSpinChanged(double z);
    void onVertexTagCommit();        // descriptive vertex tag → selected vertex
    void onVertexCoupledCommit();    // coupled SWMM node → selected vertex
    void onVertexCdCommit();         // coupling Cd → selected coupled vertices
    void onVertexAreaCommit();       // coupling area → selected coupled vertices
    void onAutoCoupleClicked();      // couple vertices to coincident SWMM nodes
    void onRemapClicked();           // Remap 1D↔2D: vertex + cell coupling (Plan C.4)
    void onCellParamCommit();        // selected parameter → selected cells
    void onCellEnumCommit(int index); // Kind::Enum editor → selected cells
    void onCellParamChanged(int index); // reconfigure the value editor
    void onCellTagCommit();          // descriptive triangle tag → selected cell
    void onSelectionChanged();
    void onBCTypeChanged(int index);
    void commitBCParam();             // apply-as-you-go: write current param to selected edges
    void commitConveyance();          // apply-as-you-go: ψ → every selected edge (interior + boundary)
    void onBrowseBCObject();          // shared "…" → dispatches to right CRUD picker
    void onPickStageTseries();
    void onPickFlowTseries();
    void onPickRatingCurve();

private:
    /*! The BC / coupled-node dropdowns were skipped because the project has
     *  no mesh, and must be built if one appears. rebindCanvas() runs on every
     *  window activation and these lists span the whole model (one entry per
     *  node, plus every timeseries and curve), so building them for a 1D-only
     *  project is pure cost. */
    bool m_bcListsStale = false;

    /*! refreshNodeList() was skipped because the toolbar was hidden, and must
     *  run before the user can see the combo. See refreshNodeList() for why
     *  deferring this one is safe when deferring the attribute table's model
     *  was not. */
    bool m_nodeListStale = false;

    void rebuildMeshCombo();
    void refreshGroupWidths();   // re-measure the ribbon groups after
                                 // contextual clusters show/hide
    void connectMeshLayer(SWMM2DMeshLayer *layer);
    void disconnectMeshLayer(SWMM2DMeshLayer *layer);
    void refreshVertexEditor();
    void refreshEdgeEditor();
    void refreshCellEditor();
    void updateEnabledState();
    /*! \brief Write \p value into the currently selected cell parameter on
     *  every selected cell, as one undo entry.
     *
     *  The `infil.*` keys are routed to mesh::pushCellInfilEdit rather than
     *  mesh::pushCellParamEdit: only that command snapshots PROVENANCE, so
     *  only its undo puts an inheriting cell back to inheriting instead of
     *  leaving a materialised override carrying the same numbers. */
    void commitCellParam(double value);

    QList<int> currentSelectedVertices() const;          // all selected vertex indices
    QList<QPair<int,int>> currentSelectedEdges() const;  // (tri, eLocal) pairs
    QList<int> currentSelectedCells() const;             // all selected triangle indices

    // Iteration 3 — captioned ribbon groups, each hosting a mini
    // toolbar so the action-based show/hide machinery keeps working.
    QList<openswmmvis::ui::RibbonGroup *> m_groups;
    QToolBar *m_barMesh     = nullptr;
    QToolBar *m_barVertices = nullptr;
    QToolBar *m_barEdges    = nullptr;
    QToolBar *m_barCoupling = nullptr;
    QToolBar *m_barResults  = nullptr;   // Pick cells cluster (SWMMVis inserts)
    QToolBar *m_barProfile  = nullptr;   // standalone profile plotter

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
    QLineEdit     *m_vertexTagEdit  = nullptr;   // descriptive tag
    QComboBox     *m_vertexCoupledCombo = nullptr;// coupled SWMM node (dropdown)
    QAction       *m_actVertexTag     = nullptr; // embedding actions for hide
    QAction       *m_actVertexCoupled = nullptr;
    QDoubleSpinBox*m_vertexCdSpin   = nullptr;   // coupling Cd ([2D_VERTEX_NODE_MAP] CD)
    QDoubleSpinBox*m_vertexAreaSpin = nullptr;   // coupling exchange area, m² (AREA)
    QAction       *m_actVertexCd    = nullptr;   // embedding actions for hide
    QAction       *m_actVertexArea  = nullptr;
    QAction       *m_actAutoCouple  = nullptr;   // couple vertices to coincident nodes
    QAction       *m_actRemap       = nullptr;   // Remap 1D↔2D (Plan C.4)

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
    // Cell parameter page: "<parameter> [value]" — one editor for every
    // per-cell attribute in mesh::cellParamSpecs().
    QWidget       *m_cellParamPage  = nullptr;
    QComboBox     *m_cellParamCombo = nullptr;
    QDoubleSpinBox*m_cellValueSpin  = nullptr;
    // Value editor for CellParamSpec::Kind::Enum parameters (infiltration
    // method). Shares the slot the spin box occupies — exactly one of the two
    // is visible, chosen by the selected parameter's kind. A −1…5 numeric
    // spinner for an enumeration is not an editor, it is a puzzle.
    QComboBox     *m_cellEnumCombo  = nullptr;
    QLineEdit     *m_cellTagEdit    = nullptr;   // descriptive triangle tag
    QAction       *m_actCellParam   = nullptr;   // embedding actions for hide
    QAction       *m_actCellTag     = nullptr;
    QAction       *m_actCellInfil   = nullptr;   // "Assign Infiltration to Selection…"
    // Project depth unit ("ft"/"m") shown on length-valued parameters.
    QString        m_depthUnitLabel = QStringLiteral("m");

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
    ListerFn       m_nodeLister;
    NodeLocatorFn  m_nodeLocator;
};

#endif // OPENSWMMVIS_UI_TOOLBARS_MESHEDITINGTOOLBAR_H
