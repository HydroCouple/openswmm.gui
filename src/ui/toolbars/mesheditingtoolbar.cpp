/*!
 * \file   mesheditingtoolbar.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/toolbars/mesheditingtoolbar.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "core/unitsystem.h"
#include "map/mapcanvas.h"
#include "map/meshcommands.h"
#include "mesh/meshautocouple.h"
#include "mesh/meshbctype.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshinfil.h"
#include "mesh/meshnodemapper.h"
#include "mesh/meshhoverprobe.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include "ui/toolbars/ribbongroup.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QShowEvent>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QScopedValueRollback>

#include <algorithm>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVariant>
#include <QWidget>

#include <cmath>
#include <utility>

namespace {
constexpr int kNoneIndex = 0;
}

MeshEditingToolbar::MeshEditingToolbar(const QString &title, QWidget *parent)
    : QToolBar(title, parent)
{
    setObjectName(QStringLiteral("toolBarMeshEditing"));
    setMovable(true);
    // Default style for action-based buttons; Edit Vertex / Edit Edge are
    // forced icon-only by leaving their QAction text empty (only tooltip).
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // Iteration 3 — the controls live inside captioned ribbon groups on
    // the Mesh 2D tab. Each group hosts a mini QToolBar so the
    // long-standing QAction-based show/hide machinery
    // (updateEnabledState) keeps working unchanged; refreshGroupWidths()
    // re-measures the groups when contextual clusters appear/disappear.
    // Iteration 4 — group order is Mesh | Vertices | Edges | 2D Results |
    // Profile | Coupling: the profile plotter stands alone (it no longer
    // shares the 2D Results group with the cell-selection cluster whose
    // width oscillates contextually) and Coupling is last on the tab.
    using openswmmvis::ui::RibbonGroup;
    const auto makeGroupBar = [this](const QString &caption) {
        auto *group = new RibbonGroup(caption, this);
        auto *bar = new QToolBar(group);
        bar->setMovable(false);
        bar->setFloatable(false);
        bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        bar->setIconSize(QSize(20, 20));
        group->addWidget(bar);
        m_groups.append(group);
        addWidget(group);
        return bar;
    };
    m_barMesh     = makeGroupBar(tr("Mesh"));
    m_barVertices = makeGroupBar(tr("Vertices"));
    m_barEdges    = makeGroupBar(tr("Edges"));
    m_barResults  = makeGroupBar(tr("2D Results"));
    m_barProfile  = makeGroupBar(tr("Profile"));
    m_barCoupling = makeGroupBar(tr("Coupling"));

    // ── Active-mesh combo ──────────────────────────────────────────────
    m_barMesh->addWidget(new QLabel(tr("Mesh:"), this));
    m_meshCombo = new QComboBox(this);
    m_meshCombo->setMinimumWidth(180);
    m_meshCombo->setToolTip(tr(
        "Active 2D mesh. Selecting a mesh marks it as the active mesh for\n"
        "editing and (where applicable) result coupling."));
    m_meshCombo->addItem(tr("(none)"), QVariant::fromValue<quintptr>(0));
    m_meshCombo->setEnabled(false);
    m_barMesh->addWidget(m_meshCombo);
    connect(m_meshCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MeshEditingToolbar::onActiveMeshComboChanged);

    // ── Hover elevation readout ────────────────────────────────────────
    m_barMesh->addWidget(new QLabel(tr("Z:"), this));
    m_hoverLabel = new QLabel(QStringLiteral("—"), this);
    m_hoverLabel->setMinimumWidth(80);
    m_hoverLabel->setToolTip(tr(
        "Elevation under the cursor, interpolated linearly across the\n"
        "triangle the cursor is inside (NaN outside the mesh)."));
    m_barMesh->addWidget(m_hoverLabel);

    // ── Vertex group: [icon-only toggle] [info label] [Z spin] ─────────
    // Icon-only: pass empty QString for the action text so the toolbar
    // shows just the SVG glyph. Tooltip carries the description.
    m_actEditVertex = new QAction(
        QIcon(QStringLiteral(":/swmmvis/SelectTriNode")), QString(), this);
    // Step G — objectName ties this action into SWMMVisProjectWindow::
    // toolActionKeys() so the canvas active-tool sync visually unchecks
    // it when the user picks the general-purpose Select / 2D-cells /
    // mesh-edge tools.
    m_actEditVertex->setObjectName(QStringLiteral("actionMeshSelectVertex"));
    m_actEditVertex->setCheckable(true);
    m_actEditVertex->setToolTip(tr(
        "Select Vertex — click a mesh vertex to select it, then edit\n"
        "its elevation in the Z spinbox to the right. Esc clears."));
    m_barVertices->addAction(m_actEditVertex);

    m_vertexInfoLbl = new QLabel(tr("Vertex: (none)"), this);
    m_vertexInfoLbl->setMinimumWidth(160);
    m_vertexInfoLbl->setToolTip(tr("Selected vertex index and tag (coupled SWMM node id)."));
    m_barVertices->addWidget(m_vertexInfoLbl);

    m_zSpin = new QDoubleSpinBox(this);
    m_zSpin->setRange(-1.0e6, 1.0e6);
    m_zSpin->setDecimals(3);
    m_zSpin->setSingleStep(0.1);
    m_zSpin->setMinimumWidth(110);
    // Coalesce edits: emit valueChanged only when editing finishes (Enter,
    // focus-out, or arrow step) rather than on every keystroke, so applying
    // the elevation doesn't trigger a mesh recompute + redraw per digit typed.
    m_zSpin->setKeyboardTracking(false);
    m_zSpin->setToolTip(tr(
        "Elevation of the selected vertex in project vertical units.\n"
        "Edits propagate via the layer's attributeChanged signal."));
    m_zSpin->setEnabled(false);
    m_barVertices->addWidget(m_zSpin);
    connect(m_zSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &MeshEditingToolbar::onZSpinChanged);

    // Descriptive tag + coupled-node editors for the selected vertex. Commit
    // on editingFinished (Enter / focus-out) so a partial id isn't applied
    // per keystroke. Disabled until a single vertex is selected.
    m_vertexTagEdit = new QLineEdit(this);
    m_vertexTagEdit->setMinimumWidth(90);
    m_vertexTagEdit->setPlaceholderText(tr("tag"));
    m_vertexTagEdit->setToolTip(tr("Descriptive vertex tag ([2D_VERTICES] TAG column)."));
    m_vertexTagEdit->setEnabled(false);
    m_actVertexTag = m_barVertices->addWidget(m_vertexTagEdit);
    connect(m_vertexTagEdit, &QLineEdit::editingFinished,
            this, &MeshEditingToolbar::onVertexTagCommit);

    // Coupled SWMM node: a dropdown of available nodes (editable so a name can
    // still be typed). A blank entry clears the coupling. Populated from the
    // node lister SWMMVis installs.
    m_vertexCoupledCombo = new QComboBox(this);
    m_vertexCoupledCombo->setEditable(true);
    m_vertexCoupledCombo->setInsertPolicy(QComboBox::NoInsert);
    m_vertexCoupledCombo->setMinimumWidth(120);
    m_vertexCoupledCombo->setToolTip(tr("Coupled SWMM node ([2D_VERTEX_NODE_MAP]); blank = uncoupled."));
    m_vertexCoupledCombo->setEnabled(false);
    m_actVertexCoupled = m_barVertices->addWidget(m_vertexCoupledCombo);
    connect(m_vertexCoupledCombo, &QComboBox::activated,
            this, &MeshEditingToolbar::onVertexCoupledCommit);
    if (m_vertexCoupledCombo->lineEdit())
        connect(m_vertexCoupledCombo->lineEdit(), &QLineEdit::editingFinished,
                this, &MeshEditingToolbar::onVertexCoupledCommit);

    // Auto-couple: couple vertices to the SWMM node sitting at the same map
    // coordinate. Operates on the selection, or the whole mesh when nothing
    // is selected, via the node locator SWMMVis installs.
    m_actAutoCouple = new QAction(
        QIcon(QStringLiteral(":/swmmvis/Snap")), tr("Auto-couple"), this);
    m_actAutoCouple->setToolTip(tr(
        "Couple mesh vertices to coincident SWMM nodes.\n"
        "Applies to the selected vertices, or scans the whole mesh when\n"
        "nothing is selected. Already-coupled vertices are left unchanged."));
    m_barCoupling->addAction(m_actAutoCouple);
    connect(m_actAutoCouple, &QAction::triggered,
            this, &MeshEditingToolbar::onAutoCoupleClicked);

    // Remap 1D↔2D (Plan Part C.4): the superset of Auto-couple. Coincident
    // nodes → vertex coupling; non-coincident nodes inside the mesh → cell
    // coupling (several nodes may share one cell); outside → reported.
    // Decouples 1D↔2D mapping from mesh generation — re-runnable anytime.
    m_actRemap = new QAction(
        QIcon(QStringLiteral(":/swmmvis/Remap1D2D")), tr("Remap 1D↔2D"), this);
    m_actRemap->setToolTip(tr(
        "Map SWMM model nodes onto the active mesh.\n"
        "Nodes coincident with a mesh vertex use vertex coupling; other\n"
        "nodes inside the mesh couple to their containing cell (several\n"
        "nodes may share one cell). All existing couplings are cleared\n"
        "first (you will be asked to confirm)."));
    m_barCoupling->addAction(m_actRemap);
    connect(m_actRemap, &QAction::triggered,
            this, &MeshEditingToolbar::onRemapClicked);

    // Coupling Cd / exchange-area editors — the optional CD / AREA columns of
    // [2D_VERTEX_NODE_MAP]. Only meaningful on coupled vertices, so they are
    // shown contextually (see updateEnabledState) and commit apply-as-you-go
    // to every selected coupled vertex.
    {
        auto *page = new QWidget(this);
        auto *lay  = new QHBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(new QLabel(tr("Cd:"), page));
        m_vertexCdSpin = new QDoubleSpinBox(page);
        m_vertexCdSpin->setRange(0.001, 1.0);
        m_vertexCdSpin->setDecimals(3);
        m_vertexCdSpin->setSingleStep(0.05);
        m_vertexCdSpin->setValue(0.65);
        m_vertexCdSpin->setKeyboardTracking(false);
        m_vertexCdSpin->setToolTip(tr(
            "Coupling discharge coefficient ([2D_VERTEX_NODE_MAP] CD column).\n"
            "Scales the orifice-equation exchange with the coupled node.\n"
            "Default 0.65. Applies to every selected coupled vertex."));
        lay->addWidget(m_vertexCdSpin);
        m_actVertexCd = m_barVertices->addWidget(page);
    }
    connect(m_vertexCdSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &MeshEditingToolbar::onVertexCdCommit);

    {
        auto *page = new QWidget(this);
        auto *lay  = new QHBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(new QLabel(tr("Area:"), page));
        m_vertexAreaSpin = new QDoubleSpinBox(page);
        m_vertexAreaSpin->setRange(0.0001, 1.0e6);
        m_vertexAreaSpin->setDecimals(3);
        m_vertexAreaSpin->setSingleStep(0.1);
        m_vertexAreaSpin->setValue(1.0);
        // Suffix follows the active mesh's units (refreshVertexEditor): the
        // AREA column is mesh-length² — ft² on a US project unless the mesh
        // file is tagged `;; UNITS: SI (m)` — and the engine scales it with
        // the mesh. It is NOT always m².
        m_vertexAreaSpin->setSuffix(tr(" m²"));
        m_vertexAreaSpin->setKeyboardTracking(false);
        m_vertexAreaSpin->setToolTip(tr(
            "Coupling exchange area ([2D_VERTEX_NODE_MAP] AREA column), the\n"
            "orifice-throat area of the 1D↔2D exchange, in the mesh's length\n"
            "units squared (project units unless the mesh file is tagged SI).\n"
            "Default 1.0. Applies to every selected coupled vertex."));
        lay->addWidget(m_vertexAreaSpin);
        m_actVertexArea = m_barVertices->addWidget(page);
    }
    connect(m_vertexAreaSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &MeshEditingToolbar::onVertexAreaCommit);

    // ── Edge group: [icon-only toggle] [info label] [BC combo] [param] [browse] ──
    m_actEditEdge = new QAction(
        QIcon(QStringLiteral(":/swmmvis/SelectTriEdge")), QString(), this);
    // Step G — paired with actionMeshSelectVertex / actionPick2DCells /
    // ui->actionSelect via SWMMVisProjectWindow::toolActionKeys() so the
    // active-tool sync keeps the four-way radio in step.
    m_actEditEdge->setObjectName(QStringLiteral("actionMeshSelectEdge"));
    m_actEditEdge->setCheckable(true);
    m_actEditEdge->setToolTip(tr(
        "Select Edge — click a boundary edge or drag a box to select\n"
        "multiple, then assign a boundary condition. Ctrl/⌘-click two\n"
        "boundary edges to add the whole run between them. Esc clears."));
    m_barEdges->addAction(m_actEditEdge);

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

    m_edgeInfoLbl = new QLabel(tr("Edge: (none)"), this);
    m_edgeInfoLbl->setMinimumWidth(160);
    m_edgeInfoLbl->setToolTip(tr("Selected edge index and tag (if any)."));
    m_barEdges->addWidget(m_edgeInfoLbl);

    // BC type combo.
    m_bcTypeCombo = new QComboBox(this);
    m_bcTypeCombo->setObjectName(QStringLiteral("meshBcTypeCombo"));
    m_bcTypeCombo->setMinimumWidth(200);
    m_bcTypeCombo->setToolTip(tr(
        "Boundary condition type for the selected edge(s).\n"
        "Changes apply immediately to every selected edge.\n"
        "NB: NORMAL_FLOW needs a non-zero bed slope — with slope 0\n"
        "the edge behaves as a Wall."));
    using mesh::MeshBCTypes;
    for (auto t : {MeshBCTypes::Type::Wall,
                   MeshBCTypes::Type::NormalFlow,
                   MeshBCTypes::Type::SpecifiedStageConst,
                   MeshBCTypes::Type::SpecifiedStageTS,
                   MeshBCTypes::Type::SpecifiedFlowConst,
                   MeshBCTypes::Type::SpecifiedFlowTS,
                   MeshBCTypes::Type::RatingCurve}) {
        m_bcTypeCombo->addItem(MeshBCTypes::label(t), static_cast<int>(t));
    }
    m_bcTypeCombo->setEnabled(false);
    m_actBCTypeCombo = m_barEdges->addWidget(m_bcTypeCombo);
    connect(m_bcTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MeshEditingToolbar::onBCTypeChanged);

    // BC parameter stack — one page per type. Indices match the combo
    // rows: 0 Wall, 1 NormalFlow, 2 StageConst, 3 StageTS, 4 FlowConst,
    // 5 FlowTS, 6 RatingCurve.
    m_bcParamStack = new QStackedWidget(this);
    m_bcParamStack->setMinimumWidth(180);
    m_bcParamStack->setEnabled(false);

    auto makeSpinPage = [&](QDoubleSpinBox **outSpin,
                             const QString &labelText, double lo, double hi,
                             int decimals, double step, const QString &tip) {
        auto *page = new QWidget(m_bcParamStack);
        auto *lay  = new QHBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(new QLabel(labelText));
        auto *spin = new QDoubleSpinBox(page);
        spin->setRange(lo, hi);
        spin->setDecimals(decimals);
        spin->setSingleStep(step);
        spin->setToolTip(tip);
        lay->addWidget(spin);
        *outSpin = spin;
        m_bcParamStack->addWidget(page);
    };

    auto makeNamePage = [&](QComboBox **outCombo, const QString &labelText) {
        auto *page = new QWidget(m_bcParamStack);
        auto *lay  = new QHBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(new QLabel(labelText));
        auto *combo = new QComboBox(page);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->setMinimumWidth(120);
        combo->setToolTip(tr(
            "Pick an existing object, or type a new name and click the\n"
            "Browse button to create / edit it in the CRUD dialog."));
        lay->addWidget(combo);
        *outCombo = combo;
        m_bcParamStack->addWidget(page);
    };

    // 0 Wall — empty page.
    m_bcParamStack->addWidget(new QWidget(m_bcParamStack));
    // 1 NormalFlow — slope spin.
    makeSpinPage(&m_slopeSpin, tr("Slope:"), 0.0, 1.0, 5, 0.001,
                 tr("Bed slope (dimensionless). Must be > 0 — the engine "
                    "treats a zero slope as a wall (no auto-compute)."));
    // 2 SpecifiedStageConst — stage spin.
    makeSpinPage(&m_stageSpin, tr("Stage:"), -1.0e6, 1.0e6, 3, 0.1,
                 tr("Prescribed water-surface elevation (project vertical units)."));
    // 3 SpecifiedStageTS — TS combo.
    makeNamePage(&m_stageTSCombo, tr("TS:"));
    // 4 SpecifiedFlowConst — flow spin.
    makeSpinPage(&m_flowSpin, tr("Flow/m:"), -1.0e6, 1.0e6, 4, 0.01,
                 tr("Prescribed discharge per metre of edge (outward-positive)."));
    // 5 SpecifiedFlowTS — TS combo.
    makeNamePage(&m_flowTSCombo, tr("TS:"));
    // 6 RatingCurve — curve combo.
    makeNamePage(&m_curveCombo, tr("Curve:"));

    // objectNames so tests (and Squish-style tooling) can findChild the
    // param widgets — they are created inside the page lambdas above.
    m_slopeSpin->setObjectName(QStringLiteral("meshBcSlopeSpin"));
    m_stageSpin->setObjectName(QStringLiteral("meshBcStageSpin"));
    m_stageTSCombo->setObjectName(QStringLiteral("meshBcStageTSCombo"));
    m_flowSpin->setObjectName(QStringLiteral("meshBcFlowSpin"));
    m_flowTSCombo->setObjectName(QStringLiteral("meshBcFlowTSCombo"));
    m_curveCombo->setObjectName(QStringLiteral("meshBcCurveCombo"));

    m_actBCParamStack = m_barEdges->addWidget(m_bcParamStack);

    // Shared Browse button — only relevant for the TS / Curve types.
    // Dispatches to the matching picker based on the current BC type;
    // shown only when the current type carries a named object. Rendered as a
    // text "…" button (no icon) per the toolbar's TextBesideIcon style.
    m_actBrowseObj = new QAction(QStringLiteral("…"), this);
    m_actBrowseObj->setToolTip(tr(
        "Browse / create / edit the timeseries or rating curve referenced\n"
        "by the selected BC. Only shown when the BC type is a TS or\n"
        "Rating Curve."));
    m_actBrowseObj->setEnabled(false);
    m_barEdges->addAction(m_actBrowseObj);
    connect(m_actBrowseObj, &QAction::triggered, this,
            &MeshEditingToolbar::onBrowseBCObject);

    // ── Conveyance (ψ) spinbox — Engine §11A flux-attenuation factor ─────
    // Lives outside the BC param stack: it applies to every edge (interior
    // and boundary), so the BC gate (haveBoundaryEdge) would hide it from
    // exactly the case it was added for.
    {
        auto *page = new QWidget(this);
        auto *lay  = new QHBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(new QLabel(QStringLiteral("ψ:"), page));
        m_conveySpin = new QDoubleSpinBox(page);
        m_conveySpin->setObjectName(QStringLiteral("meshBcConveySpin"));
        m_conveySpin->setRange(0.0, 1.0);
        m_conveySpin->setDecimals(3);
        m_conveySpin->setSingleStep(0.05);
        m_conveySpin->setValue(1.000);
        m_conveySpin->setToolTip(tr(
            "Per-edge conveyance (flux attenuation multiplier).\n"
            "1.000 = unrestricted (default); 0.000 = closed.\n"
            "Applies to every selected edge — interior or boundary —\n"
            "and mirrors automatically across interior-edge halves."));
        lay->addWidget(m_conveySpin);
        m_actConveySpin = m_barEdges->addWidget(page);
    }
    connect(m_conveySpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &MeshEditingToolbar::commitConveyance);

    // Apply-as-you-go commit handlers — every parameter widget writes
    // through to all currently-selected edges on commit.
    auto commitBC = [this]() { commitBCParam(); };
    connect(m_slopeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, commitBC);
    connect(m_stageSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, commitBC);
    connect(m_flowSpin,  qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, commitBC);
    connect(m_stageTSCombo, &QComboBox::currentTextChanged, this, commitBC);
    connect(m_flowTSCombo,  &QComboBox::currentTextChanged, this, commitBC);
    connect(m_curveCombo,   &QComboBox::currentTextChanged, this, commitBC);

    // Cell-selection info label — created here, but placed on the toolbar by
    // SWMMVis (via addToolWidget) immediately after the Select-2D-Cells action
    // so it reads "[Select 2D Cells] [cell label]" like the edge selector.
    m_cellInfoLbl = new QLabel(tr("Cell: (none)"), this);
    m_cellInfoLbl->setMinimumWidth(150);
    m_cellInfoLbl->setToolTip(tr("Selected 2D cell index and tag (if any)."));

    // Per-cell parameter editor + descriptive tag editor. Created here but NOT
    // placed: SWMMVis inserts them right after the cell info label (via
    // setCellEditorActions) so they sit in the 2D-cell group, and the toolbar
    // hides them unless at least one cell is selected.
    //
    // The parameter combo names which attribute the value box prescribes, so
    // every per-cell attribute is editable from one pair of widgets. Entries
    // come from mesh::cellParamSpecs(); the ones awaiting engine support are
    // listed greyed so the roadmap is visible.
    m_cellParamPage = new QWidget(this);
    {
        auto *lay = new QHBoxLayout(m_cellParamPage);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);

        m_cellParamCombo = new QComboBox(m_cellParamPage);
        m_cellParamCombo->setMinimumWidth(130);
        m_cellParamCombo->setToolTip(
            tr("2D cell parameter to prescribe on the selection."));
        for (const mesh::CellParamSpec &s : mesh::cellParamSpecs()) {
            m_cellParamCombo->addItem(s.label, QVariant(s.key));
            const int row = m_cellParamCombo->count() - 1;
            m_cellParamCombo->setItemData(row, s.tooltip, Qt::ToolTipRole);
            if (!s.enabled) {
                // Visible but unselectable until the engine carries the field.
                if (auto *model =
                        qobject_cast<QStandardItemModel *>(m_cellParamCombo->model()))
                    if (QStandardItem *item = model->item(row))
                        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            }
        }
        lay->addWidget(m_cellParamCombo);

        m_cellValueSpin = new QDoubleSpinBox(m_cellParamPage);
        m_cellValueSpin->setMinimumWidth(90);
        m_cellValueSpin->setKeyboardTracking(false);
        lay->addWidget(m_cellValueSpin);

        // Kind::Enum parameters (the infiltration method) get a combo, not a
        // numeric spinner over the enumeration's integer range. Both editors
        // occupy the same slot; onCellParamChanged shows the right one.
        m_cellEnumCombo = new QComboBox(m_cellParamPage);
        m_cellEnumCombo->setMinimumWidth(150);
        m_cellEnumCombo->setVisible(false);
        lay->addWidget(m_cellEnumCombo);

        connect(m_cellParamCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MeshEditingToolbar::onCellParamChanged);
        connect(m_cellValueSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &MeshEditingToolbar::onCellParamCommit);
        connect(m_cellEnumCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MeshEditingToolbar::onCellEnumCommit);
    }
    m_cellParamPage->setEnabled(false);
    onCellParamChanged(m_cellParamCombo->currentIndex());   // seed spin config

    m_cellTagEdit = new QLineEdit(this);
    m_cellTagEdit->setMinimumWidth(90);
    m_cellTagEdit->setPlaceholderText(tr("cell tag"));
    m_cellTagEdit->setToolTip(tr("Descriptive triangle tag ([2D_TRIANGLES] TAG column)."));
    m_cellTagEdit->setEnabled(false);
    connect(m_cellTagEdit, &QLineEdit::editingFinished,
            this, &MeshEditingToolbar::onCellTagCommit);

    // Expanding spacer pins everything to the left. Trailing tool actions
    // (Pick 2D Cell / Trace Profile) insert *before* this spacer via
    // addToolAction(), so they sit at the right end of the left-aligned
    // group — after the BC controls — rather than after the stretch.
    {
        auto *spacer = new QWidget(this);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_spacerAction = addWidget(spacer);
    }

    // ── Hover probe (owned QObject; outlives any single canvas) ────────
    m_hover = new mesh::MeshHoverProbe(this);
    connect(m_hover, &mesh::MeshHoverProbe::elevationChanged,
            this, &MeshEditingToolbar::onHoverElevation);

    updateEnabledState();
}

QWidget *MeshEditingToolbar::cellParamEditorWidget() const { return m_cellParamPage; }
QWidget *MeshEditingToolbar::cellTagWidget() const         { return m_cellTagEdit; }

QByteArray MeshEditingToolbar::currentCellParamKey() const
{
    if (!m_cellParamCombo) return {};
    return m_cellParamCombo->currentData().toByteArray();
}

void MeshEditingToolbar::setCellEditorActions(QAction *paramAct, QAction *tagAct)
{
    m_actCellParam = paramAct;
    m_actCellTag   = tagAct;
    updateEnabledState();   // apply initial hidden state
}

void MeshEditingToolbar::setDepthUnitLabel(const QString &label)
{
    if (label.isEmpty() || m_depthUnitLabel == label) return;
    m_depthUnitLabel = label;
    if (m_cellParamCombo)   // re-apply the suffix to the live parameter
        onCellParamChanged(m_cellParamCombo->currentIndex());
}

MeshEditingToolbar::~MeshEditingToolbar() = default;

void MeshEditingToolbar::addToolAction(QAction *action)
{
    // Trailing tool actions live in the "2D Results" ribbon group.
    if (action && m_barResults)
        m_barResults->addAction(action);
}

void MeshEditingToolbar::addToolSeparator()
{
    // The captioned group boundary already delimits the trailing tools —
    // kept as a no-op for API compatibility.
}

QAction *MeshEditingToolbar::addToolWidget(QWidget *widget)
{
    if (!widget || !m_barResults) return nullptr;
    return m_barResults->addWidget(widget);
}

void MeshEditingToolbar::addCellAction(QAction *action)
{
    if (!action || !m_barResults) return;
    m_actCellInfil = action;
    m_barResults->addAction(action);
    updateEnabledState();   // apply initial hidden state
}

void MeshEditingToolbar::addProfileAction(QAction *action)
{
    if (action && m_barProfile)
        m_barProfile->addAction(action);
}

// ---------------------------------------------------------------------
// Canvas rebinding
// ---------------------------------------------------------------------

void MeshEditingToolbar::rebindCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas) return;

    if (m_canvas) {
        disconnect(m_canvas, &MapCanvas::layerAdded,
                   this, &MeshEditingToolbar::onLayerAdded);
        disconnect(m_canvas, &MapCanvas::layerRemoved,
                   this, &MeshEditingToolbar::onLayerRemoved);
    }
    if (m_activeMesh) {
        disconnectMeshLayer(m_activeMesh);
        m_activeMesh = nullptr;
    }
    m_canvas = canvas;
    if (m_canvas) {
        connect(m_canvas, &MapCanvas::layerAdded,
                this, &MeshEditingToolbar::onLayerAdded);
        connect(m_canvas, &MapCanvas::layerRemoved,
                this, &MeshEditingToolbar::onLayerRemoved);
    }
    m_hover->setCanvas(m_canvas);
    rebuildMeshCombo();

    // Only populate the 2D editing dropdowns when there is actually a mesh to
    // edit. rebindCanvas() runs on every project-window activation, and these
    // lists are built from the whole model -- every node for the coupled-node
    // combo, every timeseries and curve for the BC combos, each timeseries
    // point converted to a QDateTime along the way. Doing that for a 1D-only
    // model populated dropdowns nobody could open.
    //
    // rebuildMeshCombo() leaves a single "(none)" entry when the project has
    // no mesh layer, so count() > 1 is the has-a-mesh test.
    const bool hasMesh = m_meshCombo && m_meshCombo->count() > 1;
    if (hasMesh) {
        refreshBCNameLists();
        refreshNodeList();   // coupled-node dropdown for this project
        m_bcListsStale = false;
    } else {
        // Remember that they need building, so enabling 2D later (or adding a
        // mesh layer) still gets populated combos.
        m_bcListsStale = true;
    }
    updateEnabledState();
}

void MeshEditingToolbar::rebindSelectionManager(SelectionManager *sel)
{
    if (m_selection == sel) return;
    if (m_selection) {
        disconnect(m_selection, &SelectionManager::selectionChanged,
                   this, &MeshEditingToolbar::onSelectionChanged);
    }
    m_selection = sel;
    if (m_selection) {
        connect(m_selection, &SelectionManager::selectionChanged,
                this, &MeshEditingToolbar::onSelectionChanged);
    }
    refreshVertexEditor();
    refreshEdgeEditor();
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
        return;
    }

    int activeRow = kNoneIndex;
    int firstMeshRow = kNoneIndex;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        auto *mesh = qobject_cast<SWMM2DMeshLayer *>(l);
        if (!mesh) continue;
        const QString label = mesh->sourcePath().isEmpty()
                               ? mesh->name()
                               : QFileInfo(mesh->sourcePath()).fileName();
        m_meshCombo->addItem(label,
                             QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(mesh)));
        if (firstMeshRow == kNoneIndex)
            firstMeshRow = m_meshCombo->count() - 1;
        if (mesh->isActiveMesh())
            activeRow = m_meshCombo->count() - 1;
    }
    // If no layer is flagged active but at least one mesh exists, auto-
    // pick the first one — otherwise the user opens the toolbar and the
    // edit toggles are disabled with no obvious next action.
    if (activeRow == kNoneIndex && firstMeshRow != kNoneIndex)
        activeRow = firstMeshRow;
    m_meshCombo->setCurrentIndex(activeRow);
    m_meshCombo->setEnabled(m_meshCombo->count() > 1);

    // Sync internal state with the combo's current row (no signal because
    // we're blocking, so step through the same logic manually).
    SWMM2DMeshLayer *picked = nullptr;
    if (activeRow != kNoneIndex) {
        picked = reinterpret_cast<SWMM2DMeshLayer *>(
            m_meshCombo->itemData(activeRow).value<quintptr>());
    }
    if (picked != m_activeMesh) {
        if (m_activeMesh) disconnectMeshLayer(m_activeMesh);
        m_activeMesh = picked;
        if (m_activeMesh) connectMeshLayer(m_activeMesh);
    }
    m_hover->setActiveMesh(m_activeMesh);
    refreshVertexEditor();
    refreshEdgeEditor();
}

void MeshEditingToolbar::onActiveMeshComboChanged(int index)
{
    SWMM2DMeshLayer *picked = nullptr;
    if (index != kNoneIndex && index >= 0 && index < m_meshCombo->count()) {
        picked = reinterpret_cast<SWMM2DMeshLayer *>(
            m_meshCombo->itemData(index).value<quintptr>());
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
            if (!mesh || mesh == picked) continue;
            if (mesh->isActiveMesh()) mesh->setActiveMesh(false);
        }
    }
    m_activeMesh = picked;
    if (m_activeMesh) {
        m_activeMesh->setActiveMesh(true);
        connectMeshLayer(m_activeMesh);
    }
    m_hover->setActiveMesh(m_activeMesh);

    refreshVertexEditor();
    refreshEdgeEditor();
    updateEnabledState();
}

void MeshEditingToolbar::onLayerAdded(OpenSWMMVisLayer *layer)
{
    if (!qobject_cast<SWMM2DMeshLayer *>(layer))
        return;
    rebuildMeshCombo();
    // A mesh just arrived, so the dropdowns rebindCanvas() skipped for a
    // 1D-only project are now needed.
    if (m_bcListsStale) {
        refreshBCNameLists();
        refreshNodeList();
        m_bcListsStale = false;
    }
}

void MeshEditingToolbar::onLayerRemoved(OpenSWMMVisLayer *layer)
{
    auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer);
    if (!mesh) return;
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

void MeshEditingToolbar::connectMeshLayer(SWMM2DMeshLayer *layer)
{
    if (!layer) return;
    connect(layer, &SWMM2DMeshLayer::activeMeshChanged,
            this, &MeshEditingToolbar::onActiveMeshFlagChanged,
            Qt::UniqueConnection);
    connect(layer, &SWMM2DMeshLayer::attributeChanged,
            this, &MeshEditingToolbar::onAttributeChanged,
            Qt::UniqueConnection);
    // Qt 6 asserts on Qt::UniqueConnection with non-PMF slots — lambdas
    // can't be compared for identity, so Qt can't de-dupe.  Disconnect
    // any prior handler on this specific (layer, signal, this) tuple
    // first, then make exactly one fresh connection.  Without this
    // guard, opening a complex .inp whose mesh-generation finish path
    // routes through rebuildMeshCombo → connectMeshLayer trips the
    // assertion the moment AddLayerCommand::redo() emits layerAdded.
    QObject::disconnect(layer, &SWMM2DMeshLayer::selectionInvalidated,
                        this, nullptr);
    connect(layer, &SWMM2DMeshLayer::selectionInvalidated,
            this, [this] {
                if (m_selection) m_selection->clear();
                refreshVertexEditor();
                refreshEdgeEditor();
            });
}

void MeshEditingToolbar::disconnectMeshLayer(SWMM2DMeshLayer *layer)
{
    if (!layer) return;
    disconnect(layer, nullptr, this, nullptr);
}

// ---------------------------------------------------------------------
// Selection + editor refresh
// ---------------------------------------------------------------------

QList<int> MeshEditingToolbar::currentSelectedVertices() const
{
    QList<int> out;
    if (!m_selection || !m_activeMesh) return out;
    const QString wantKey = mesh::MeshObjectRef::layerKey(m_activeMesh->sourcePath());
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshVertex) continue;
        QString lk;
        int vi = -1;
        if (!mesh::MeshObjectRef::parseVertex(ref, &lk, &vi)) continue;
        if (lk != wantKey) continue;
        out.append(vi);
    }
    return out;
}

QString MeshEditingToolbar::meshAreaUnitLabel() const
{
    if (m_activeMesh && m_activeMesh->meshUnitsSI()) return QStringLiteral("m²");
    auto *us = UnitSystem::instance();
    return (us ? us->lengthLabel() : QStringLiteral("ft")) + QStringLiteral("²");
}

void MeshEditingToolbar::refreshVertexEditor()
{
    if (m_vertexAreaSpin)
        m_vertexAreaSpin->setSuffix(QStringLiteral(" ") + meshAreaUnitLabel());
    const QList<int> verts = currentSelectedVertices();
    // The coupled-node dropdown's item list is refreshed on rebind; here we
    // only set the current value. Visibility/enablement is owned by
    // updateEnabledState().
    if (verts.isEmpty() || !m_activeMesh) {
        m_vertexInfoLbl->setText(tr("Vertex: (none)"));
        QSignalBlocker block(m_zSpin);
        m_zSpin->setValue(0.0);
        m_zSpin->setEnabled(false);
        return;
    }

    const auto &vertices = m_activeMesh->mesh().vertices;

    // Aggregate across the selection: mean Z, plus the common tag / coupled
    // node (blank when the selected vertices disagree). Committing overwrites
    // every selected vertex, mirroring the Z spinbox's multi-edit behaviour.
    double sumZ = 0.0; int n = 0;
    QString commonTag, commonNode;
    bool first = true, tagSame = true, nodeSame = true;
    // Coupling Cd/Area aggregate over COUPLED vertices only — uncoupled ones
    // carry defaults that would mask a uniform edited value.
    double commonCd = 0.65, commonArea = 1.0;
    bool cdSame = true, areaSame = true, firstCoupled = true;
    int nCoupled = 0;
    for (int vi : verts) {
        if (vi < 0 || vi >= vertices.size()) continue;
        const auto &v = vertices[vi];
        sumZ += v.z; ++n;
        if (first) { commonTag = v.tag; commonNode = v.coupledNode; first = false; }
        else {
            if (v.tag != commonTag)          tagSame  = false;
            if (v.coupledNode != commonNode) nodeSame = false;
        }
        if (!v.coupledNode.isEmpty()) {
            ++nCoupled;
            if (firstCoupled) {
                commonCd = v.couplingCd; commonArea = v.couplingArea;
                firstCoupled = false;
            } else {
                if (v.couplingCd   != commonCd)   cdSame   = false;
                if (v.couplingArea != commonArea) areaSame = false;
            }
        }
    }
    if (n == 0) {
        m_vertexInfoLbl->setText(tr("Vertex: (none)"));
        QSignalBlocker block(m_zSpin);
        m_zSpin->setValue(0.0);
        m_zSpin->setEnabled(false);
        return;
    }

    if (verts.size() == 1) {
        const auto &v = vertices[verts.front()];
        QString vLabel = tr("Vertex #%1").arg(verts.front());
        if (!v.tag.isEmpty())         vLabel += tr(" [%1]").arg(v.tag);
        if (!v.coupledNode.isEmpty()) vLabel += tr(" (%1)").arg(v.coupledNode);
        m_vertexInfoLbl->setText(vLabel);
    } else {
        m_vertexInfoLbl->setText(tr("%1 vertices selected").arg(verts.size()));
    }

    {
        QSignalBlocker block(m_zSpin);
        m_zSpin->setValue(sumZ / n);
        m_zSpin->setEnabled(m_actEditVertex->isChecked());
    }
    if (m_vertexTagEdit) {
        QSignalBlocker block(m_vertexTagEdit);
        m_vertexTagEdit->setText(tagSame ? commonTag : QString());
        m_vertexTagEdit->setPlaceholderText(tagSame ? QString() : tr("<multiple>"));
    }
    if (m_vertexCoupledCombo) {
        QSignalBlocker block(m_vertexCoupledCombo);
        m_vertexCoupledCombo->setCurrentText(nodeSame ? commonNode : QString());
    }
    // Seed Cd/Area from the coupled vertices; mixed values show the defaults
    // (same convention as the Manning's spin).
    if (m_vertexCdSpin) {
        QSignalBlocker block(m_vertexCdSpin);
        m_vertexCdSpin->setValue((nCoupled > 0 && cdSame) ? commonCd : 0.65);
    }
    if (m_vertexAreaSpin) {
        QSignalBlocker block(m_vertexAreaSpin);
        m_vertexAreaSpin->setValue((nCoupled > 0 && areaSame) ? commonArea : 1.0);
    }
}

void MeshEditingToolbar::refreshEdgeEditor()
{
    if (!m_selection || !m_activeMesh) {
        m_edgeInfoLbl->setText(tr("Edges: (none)"));
        return;
    }
    // Every widget write below is display-only. commitBCParam /
    // commitConveyance / onBCTypeChanged early-return while this is set, so
    // the per-slot attributeChanged emissions of a RUNNING bulk command
    // cannot re-enter here and push a nested edit that rewrites
    // already-updated edges with their old values (§V.VC.3).
    const QScopedValueRollback<bool> refreshing(m_refreshingEdgeEditor, true);

    const auto edges = currentSelectedEdges();
    int nBoundary = 0;
    for (const auto &pr : edges)
        if (m_activeMesh->isBoundaryEdge(pr.first, pr.second)) ++nBoundary;

    // Aggregate across the selection (refreshVertexEditor's same-flag
    // convention): the BC fields aggregate over BOUNDARY edges — interior
    // slots hold the default Wall value the engine ignores, and folding them
    // in would report "mixed" for a perfectly uniform boundary run — while
    // ψ aggregates over every selected edge (it applies to interior edges
    // too).
    const auto &bcs = m_activeMesh->edgeBCs();
    bool typeSame = true, slopeSame = true, headSame = true, flowSame = true,
         tsSame = true, curveSame = true, conveySame = true;
    mesh::MeshEdgeBC first;
    bool haveFirst = false;         // first BOUNDARY edge in sorted order
    double convey0 = 1.0;
    bool haveConvey = false;        // first edge of any kind
    for (const auto &pr : edges) {
        const int flat = pr.first * 3 + pr.second;
        if (flat < 0 || flat >= bcs.size()) continue;
        const mesh::MeshEdgeBC &bc = bcs[flat];
        if (!haveConvey) { convey0 = bc.conveyance; haveConvey = true; }
        else if (bc.conveyance != convey0) conveySame = false;
        if (!m_activeMesh->isBoundaryEdge(pr.first, pr.second)) continue;
        if (!haveFirst) { first = bc; haveFirst = true; continue; }
        if (bc.type    != first.type)    typeSame  = false;
        if (bc.slope   != first.slope)   slopeSame = false;
        if (bc.head    != first.head)    headSame  = false;
        if (bc.flow    != first.flow)    flowSame  = false;
        if (bc.tseries != first.tseries) tsSame    = false;
        if (bc.curve   != first.curve)   curveSame = false;
    }

    // ψ: the uniform value, or the 1.000 default on disagreement (the
    // vertex editor's Cd/Area convention).
    if (m_conveySpin) {
        QSignalBlocker block(m_conveySpin);
        m_conveySpin->setValue((haveConvey && conveySame) ? convey0 : 1.000);
    }

    if (edges.isEmpty()) {
        m_edgeInfoLbl->setText(tr("Edge: (none)"));
    } else if (edges.size() == 1) {
        // "Edge #T:E (tag)" — tag pulled from boundaryEdges when present.
        const int tri = edges.front().first;
        const int e   = edges.front().second;
        const auto &triangles = m_activeMesh->mesh().triangles;
        QString tag;
        if (tri >= 0 && tri < triangles.size()) {
            const auto &t = triangles[tri];
            int va = -1, vb = -1;
            switch (e) {
            case 0: va = t.v1; vb = t.v2; break;
            case 1: va = t.v2; vb = t.v0; break;
            case 2: va = t.v0; vb = t.v1; break;
            }
            for (const auto &be : m_activeMesh->mesh().boundaryEdges) {
                if ((be.v0 == va && be.v1 == vb) ||
                    (be.v0 == vb && be.v1 == va)) {
                    tag = be.tag;
                    break;
                }
            }
        }
        if (tag.isEmpty())
            m_edgeInfoLbl->setText(tr("Edge #%1:%2").arg(tri).arg(e));
        else
            m_edgeInfoLbl->setText(tr("Edge #%1:%2 (%3)").arg(tri).arg(e).arg(tag));
    } else {
        m_edgeInfoLbl->setText(tr("Edges: %1 (%2 boundary)")
                                .arg(edges.size()).arg(nBoundary));
    }

    // Hydrate the BC widgets for ANY selection with at least one boundary
    // edge — a single edge is just the N=1 uniform case (§V.VC.2, the
    // "mixed" gap, closed with the existing placeholder idiom). Uniform
    // fields show their value; a mixed type renders the combo empty
    // (index -1 → updateEnabledState hides the param stack); a uniform
    // type with mixed params shows type defaults in the spins and a
    // <multiple> placeholder in the name combos. Every setter runs under
    // a QSignalBlocker: these combos commit on currentTextChanged, so an
    // unblocked display refresh WAS a bulk write of an arbitrary edge's
    // value onto the whole selection.
    if (haveFirst) {
        const int comboIdx = (typeSame && m_bcTypeCombo)
            ? m_bcTypeCombo->findData(static_cast<int>(first.type))
            : -1;
        if (m_bcTypeCombo) {
            QSignalBlocker block(m_bcTypeCombo);
            m_bcTypeCombo->setCurrentIndex(comboIdx);   // -1 renders empty
        }
        if (m_bcParamStack && comboIdx >= 0
            && comboIdx < m_bcParamStack->count())
            m_bcParamStack->setCurrentIndex(comboIdx);
        if (m_slopeSpin) { QSignalBlocker b(m_slopeSpin);
            m_slopeSpin->setValue(slopeSame ? first.slope : 0.0); }
        if (m_stageSpin) { QSignalBlocker b(m_stageSpin);
            m_stageSpin->setValue(headSame ? first.head : 0.0); }
        if (m_flowSpin)  { QSignalBlocker b(m_flowSpin);
            m_flowSpin->setValue(flowSame ? first.flow : 0.0); }
        const auto setNameCombo = [](QComboBox *combo, bool same,
                                     const QString &value,
                                     const QString &placeholder) {
            if (!combo) return;
            QSignalBlocker b(combo);
            combo->setCurrentText(same ? value : QString());
            if (combo->lineEdit())
                combo->lineEdit()->setPlaceholderText(same ? QString()
                                                           : placeholder);
        };
        setNameCombo(m_stageTSCombo, tsSame, first.tseries, tr("<multiple>"));
        setNameCombo(m_flowTSCombo,  tsSame, first.tseries, tr("<multiple>"));
        setNameCombo(m_curveCombo,   curveSame, first.curve, tr("<multiple>"));
    }
    updateEnabledState();
}

QList<int> MeshEditingToolbar::currentSelectedCells() const
{
    QList<int> out;
    if (!m_selection) return out;
    // Accept every MeshCell ref (no strict layerKey filter) so the label still
    // works when the 2D mesh is results-only (no SWMM2DMeshLayer to key by).
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshCell) continue;
        QString lk; int tri = -1;
        if (mesh::MeshObjectRef::parseCell(ref, &lk, &tri))
            out.append(tri);
    }
    return out;
}

void MeshEditingToolbar::refreshCellEditor()
{
    if (!m_cellInfoLbl) return;
    const QList<int> cells = currentSelectedCells();

    // Visibility / enablement is owned by updateEnabledState(); here we only
    // populate values. For a multi-cell selection we seed the common value
    // (blank / default when the cells disagree); committing overwrites all.
    if (cells.isEmpty()) {
        m_cellInfoLbl->setText(tr("Cell: (none)"));
        return;
    }

    // Values are read for the CURRENTLY SELECTED parameter only — the combo
    // decides what the value box means.
    const QByteArray key = currentCellParamKey();
    const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
    const double fallback = spec ? spec->defaultValue : 0.0;

    bool first = true, valueSame = true, tagSame = true;
    double commonValue = fallback;
    QString commonTag;
    int counted = 0;
    if (m_activeMesh) {
        const mesh::MeshResult &m = m_activeMesh->mesh();
        for (int t : cells) {
            if (t < 0 || t >= m.triangles.size()) continue;
            const double raw = mesh::cellParamValue(m, t, key);
            const double v   = std::isfinite(raw) ? raw : fallback;
            ++counted;
            if (first) { commonValue = v; commonTag = m.triangles[t].tag; first = false; }
            else {
                if (v != commonValue)             valueSame = false;
                if (m.triangles[t].tag != commonTag) tagSame = false;
            }
        }
    }
    const bool isEnum = spec && spec->kind == mesh::CellParamSpec::Kind::Enum;
    if (m_cellValueSpin && !isEnum) {
        QSignalBlocker block(m_cellValueSpin);
        m_cellValueSpin->setValue(valueSame ? commonValue : fallback);
    }
    if (m_cellEnumCombo && isEnum) {
        QSignalBlocker block(m_cellEnumCombo);
        const int row = m_cellEnumCombo->findData(
            QVariant(valueSame ? commonValue : fallback));
        m_cellEnumCombo->setCurrentIndex(row >= 0 ? row : 0);
    }
    if (m_cellTagEdit) {
        QSignalBlocker block(m_cellTagEdit);
        m_cellTagEdit->setText(tagSame ? commonTag : QString());
        m_cellTagEdit->setPlaceholderText(tagSame ? tr("cell tag") : tr("<multiple>"));
    }

    if (cells.size() == 1) {
        // Echo the live value + tag so single-cell edits are visibly confirmed
        // (these attributes are not drawn on the map).
        QString detail;
        if (counted && valueSame && spec && isEnum) {
            // Echo the enumeration's LABEL — "-1" is not a readable confirmation.
            const int li = int(std::lround(commonValue - spec->min));
            if (li >= 0 && li < spec->enumLabels.size())
                detail = QStringLiteral("  %1=%2")
                             .arg(spec->label, spec->enumLabels.at(li));
        } else if (counted && valueSame && spec) {
            detail = QStringLiteral("  %1%2%3")
                         .arg(spec->prefix.isEmpty() ? spec->label + QStringLiteral("=")
                                                     : spec->prefix)
                         .arg(commonValue, 0, 'g', 4)
                         .arg(spec->lengthUnit && !m_depthUnitLabel.isEmpty()
                                  ? QStringLiteral(" ") + m_depthUnitLabel
                                  : QString());
        }
        if (commonTag.isEmpty() || !tagSame)
            m_cellInfoLbl->setText(tr("Cell #%1%2").arg(cells.front()).arg(detail));
        else
            m_cellInfoLbl->setText(
                tr("Cell #%1%2 (%3)").arg(cells.front()).arg(detail).arg(commonTag));
    } else {
        m_cellInfoLbl->setText(tr("Cells: %1 selected").arg(cells.size()));
    }
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
            if (!mesh) continue;
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
                        selE.insert(tri * 3 + e);
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

        // NOTE: the 2D RESULTS-layer cell highlight is no longer driven from
        // here. Cell selection is a results-ANALYSIS concern, so the highlight
        // is now applied by SWMMVisProjectWindow against the *active* 2D
        // results layer only (keyed), rather than being pushed unkeyed to every
        // results layer on the canvas. This toolbar stays an editing view over
        // the mesh (vertex Z + edge BC); the cell label below is informational.
    }
    refreshVertexEditor();
    refreshEdgeEditor();
    refreshCellEditor();
}

void MeshEditingToolbar::onAttributeChanged(const QString &refName)
{
    Q_UNUSED(refName);
    if (!m_activeMesh) return;
    // Re-read the affected editor so an edit (from here or any other view) is
    // reflected immediately. Each refresh keys off the current selection, so
    // only the editor showing the changed element actually updates. This is
    // the only on-screen confirmation for attributes that aren't drawn on the
    // map (Manning's n, tags, coupling).
    refreshVertexEditor();
    refreshEdgeEditor();
    refreshCellEditor();
}

void MeshEditingToolbar::onHoverElevation(double z, bool finite)
{
    emit hoverElevationChanged(z, finite);
    if (!finite) {
        m_hoverLabel->setText(QStringLiteral("—"));
        return;
    }
    m_hoverLabel->setText(QString::number(z, 'f', 3));
}

// Every vertex commit below routes through mesh::pushVertexParamEdit so the
// edit lands on the same undo stack the cell editors, the Cell Data dialog and
// the Attribute Table use — one command per commit, no matter how many
// vertices the selection carries.
void MeshEditingToolbar::onZSpinChanged(double z)
{
    if (m_suppressZSignal) return;
    if (!m_activeMesh) return;
    // Apply the elevation to every selected vertex (one or many).
    const QList<int> verts = currentSelectedVertices();
    mesh::pushVertexParamEdit(m_activeMesh,
                              QVector<int>(verts.cbegin(), verts.cend()),
                              "z", z, m_canvas);
}

void MeshEditingToolbar::onVertexTagCommit()
{
    if (!m_activeMesh || !m_vertexTagEdit) return;
    const QList<int> verts = currentSelectedVertices();
    if (verts.isEmpty()) return;
    mesh::pushVertexParamEdit(m_activeMesh,
                              QVector<int>(verts.cbegin(), verts.cend()),
                              "tag", m_vertexTagEdit->text().trimmed(),
                              m_canvas);
}

void MeshEditingToolbar::onVertexCoupledCommit()
{
    if (!m_activeMesh || !m_vertexCoupledCombo) return;
    const QList<int> verts = currentSelectedVertices();
    if (verts.isEmpty()) return;
    mesh::pushVertexParamEdit(m_activeMesh,
                              QVector<int>(verts.cbegin(), verts.cend()),
                              "coupledNode",
                              m_vertexCoupledCombo->currentText().trimmed(),
                              m_canvas);
}

void MeshEditingToolbar::onVertexCdCommit()
{
    if (!m_activeMesh || !m_vertexCdSpin) return;
    const QList<int> verts = currentSelectedVertices();
    if (verts.isEmpty()) return;
    // The layer rejects uncoupled vertices, so mixed selections are safe.
    mesh::pushVertexParamEdit(m_activeMesh,
                              QVector<int>(verts.cbegin(), verts.cend()),
                              "couplingCd", m_vertexCdSpin->value(), m_canvas);
}

void MeshEditingToolbar::onVertexAreaCommit()
{
    if (!m_activeMesh || !m_vertexAreaSpin) return;
    const QList<int> verts = currentSelectedVertices();
    if (verts.isEmpty()) return;
    mesh::pushVertexParamEdit(m_activeMesh,
                              QVector<int>(verts.cbegin(), verts.cend()),
                              "couplingArea", m_vertexAreaSpin->value(),
                              m_canvas);
}

void MeshEditingToolbar::onAutoCoupleClicked()
{
    if (!m_activeMesh) return;
    const QVector<QPair<QString, QPointF>> nodes =
        m_nodeLocator ? m_nodeLocator() : QVector<QPair<QString, QPointF>>{};
    if (nodes.isEmpty()) {
        QMessageBox::information(this, tr("Auto-couple"),
            tr("No SWMM nodes with coordinates are available to couple against."));
        return;
    }
    // Selection if any, else the whole mesh.
    const QList<int> targets = currentSelectedVertices();
    const auto result = mesh::findCoincidentNodes(m_activeMesh->mesh(), nodes, targets);

    int coupled = 0;
    for (auto it = result.matches.cbegin(); it != result.matches.cend(); ++it)
        if (m_activeMesh->applyMeshVertexCoupledNode(it.key(), it.value())) ++coupled;

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
    if (!m_activeMesh) return;
    const QVector<QPair<QString, QPointF>> nodes =
        m_nodeLocator ? m_nodeLocator() : QVector<QPair<QString, QPointF>>{};
    if (nodes.isEmpty()) {
        QMessageBox::information(this, tr("Remap 1D↔2D"),
            tr("No SWMM nodes with coordinates are available to map."));
        return;
    }

    // Full re-map: every existing vertex and cell coupling is cleared first.
    {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Remap 1D↔2D"));
        box.setText(tr("Re-map %1 SWMM node(s) onto the active mesh?").arg(nodes.size()));
        box.setInformativeText(tr(
            "All existing 1D↔2D couplings on this mesh (vertex and cell,\n"
            "including manually edited ones) will be cleared and rebuilt."));
        QPushButton *remapBtn = box.addButton(tr("Clear && Re-map"), QMessageBox::DestructiveRole);
        QPushButton *cancelBtn = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(cancelBtn);
        box.exec();
        if (box.clickedButton() != remapBtn) return;   // cancelled
    }

    mesh::MeshResult working = m_activeMesh->mesh();
    working.cellCouplings.clear();
    for (mesh::MeshVertex &v : working.vertices)
        v.coupledNode.clear();

    const auto r = mesh::mapNodesToMesh(working, nodes, -1.0, false);

    // Apply — clear every vertex coupling, then set the new matches through
    // the per-vertex mutator; cell rows wholesale (previous set returned for
    // a future undo command).
    const int nVerts = m_activeMesh->mesh().vertices.size();
    for (int vi = 0; vi < nVerts; ++vi)
        if (!r.vertexMatches.contains(vi))
            m_activeMesh->applyMeshVertexCoupledNode(vi, QString());
    int vApplied = 0;
    for (auto it = r.vertexMatches.cbegin(); it != r.vertexMatches.cend(); ++it)
        if (m_activeMesh->applyMeshVertexCoupledNode(it.key(), it.value())) ++vApplied;

    m_activeMesh->applyCellCouplings(r.cellMatches);

    QString msg = tr("Vertex-coupled %1 node(s); cell-coupled %2 node(s).")
                      .arg(vApplied).arg(r.cellMatches.size());
    if (r.sharedCells > 0)
        msg += tr("\n%1 cell(s) received more than one node (e.g. weir/orifice "
                  "endpoints).").arg(r.sharedCells);
    if (!r.unmatched.isEmpty()) {
        QStringList head = r.unmatched.mid(0, 8);
        msg += tr("\n%1 node(s) fall outside the mesh: %2%3")
                   .arg(r.unmatched.size())
                   .arg(head.join(QStringLiteral(", ")))
                   .arg(r.unmatched.size() > head.size()
                            ? QStringLiteral(", …") : QString());
    }
    QMessageBox::information(this, tr("Remap 1D↔2D"), msg);
}

void MeshEditingToolbar::refreshGroupWidths()
{
    // Contextual clusters just showed/hid inside the mini bars — drop the
    // groups' cached widths and re-run the host bar's layout (QToolBar
    // layouts never refresh themselves on child hint changes).
    for (auto *group : std::as_const(m_groups))
        group->refreshWidth();
    if (layout()) {
        layout()->invalidate();
        layout()->setGeometry(contentsRect());
    }
}

void MeshEditingToolbar::onCellParamChanged(int index)
{
    if (!m_cellParamCombo || !m_cellValueSpin || !m_cellEnumCombo) return;
    const QByteArray key = m_cellParamCombo->itemData(index).toByteArray();
    const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
    if (!spec) return;

    const bool isEnum = (spec->kind == mesh::CellParamSpec::Kind::Enum);
    m_cellValueSpin->setVisible(!isEnum);
    m_cellEnumCombo->setVisible(isEnum);

    if (isEnum) {
        // Registry contract: enumLabels[i] is the enumeration's i-th value in
        // its own order, and spec->min is the first one — so the stored value
        // is min + i, not the label index (mesh::InfilMethod::None is -1).
        QSignalBlocker blockEnum(m_cellEnumCombo);
        m_cellEnumCombo->clear();
        for (int i = 0; i < spec->enumLabels.size(); ++i)
            m_cellEnumCombo->addItem(spec->enumLabels.at(i),
                                     QVariant(spec->min + double(i)));
        m_cellEnumCombo->setToolTip(spec->tooltip);
        blockEnum.unblock();
        refreshCellEditor();
        return;
    }

    // Reconfiguring the range/decimals changes the spin's value, which would
    // otherwise commit the new parameter's default onto the selection.
    QSignalBlocker block(m_cellValueSpin);
    m_cellValueSpin->setDecimals(spec->decimals);
    m_cellValueSpin->setRange(spec->min, spec->max);
    m_cellValueSpin->setSingleStep(spec->step);
    m_cellValueSpin->setPrefix(spec->prefix);
    m_cellValueSpin->setSuffix(spec->lengthUnit && !m_depthUnitLabel.isEmpty()
                                   ? QStringLiteral(" ") + m_depthUnitLabel
                                   : QString());
    m_cellValueSpin->setToolTip(spec->tooltip);
    block.unblock();

    refreshCellEditor();    // seed the value from the current selection
}

void MeshEditingToolbar::onCellParamCommit()
{
    if (!m_cellValueSpin) return;
    commitCellParam(m_cellValueSpin->value());
}

void MeshEditingToolbar::onCellEnumCommit(int index)
{
    if (!m_cellEnumCombo || index < 0) return;
    commitCellParam(m_cellEnumCombo->itemData(index).toDouble());
}

void MeshEditingToolbar::commitCellParam(double value)
{
    if (!m_activeMesh) return;
    const QList<int> cells = currentSelectedCells();
    if (cells.isEmpty()) return;
    const QByteArray key = currentCellParamKey();
    if (key.isEmpty()) return;
    const QVector<int> tris(cells.cbegin(), cells.cend());

    if (!key.startsWith("infil.")) {
        // One undo entry for the whole selection, on the same stack every
        // other editing surface uses.
        mesh::pushCellParamEdit(m_activeMesh, tris, key, value, m_canvas);
        return;
    }

    // Infiltration must NOT take the generic path. MeshSetTriangleAttributeCommand
    // (which pushCellParamEdit builds) restores the old NUMBER on undo, so
    // undoing an edit made on a cell that was INHERITING from its region tag
    // leaves a per-cell override carrying identical values — the cell silently
    // stops tracking its region and the next region-level edit misses it.
    // mesh::pushCellInfilEdit is the only helper that snapshots provenance.
    //
    // That command writes ONE row to many cells, while this edit changes one
    // field of each cell's OWN resolved row (cells in the selection may resolve
    // to different methods and numbers). So group the selection by the row it
    // ends up with and push one command per distinct row, wrapped in a macro so
    // the whole edit is still a single Ctrl+Z.
    const mesh::MeshResult &m = m_activeMesh->mesh();
    QVector<mesh::InfilRow> rows;
    QVector<QVector<int>>   groups;
    for (int t : tris) {
        if (t < 0 || t >= m.triangles.size()) continue;
        mesh::InfilRow next = mesh::resolveInfil(m, t).row;
        if (key == "infil.method") {
            const int mi = int(std::lround(value));
            if (mi < int(mesh::InfilMethod::None)
                || mi > int(mesh::InfilMethod::Constant))
                return;
            next.method = static_cast<mesh::InfilMethod>(mi);
            // Slots the new method does not read carry no meaning — clear them
            // so a method switch cannot leave a stale number behind.
            for (int k = 0; k < mesh::kInfilMaxParams; ++k)
                if (!mesh::infilUsesParam(next.method, k)) next.p[k] = qQNaN();
        } else {
            const int slot = mesh::infilSlotForKey(next.method, key);
            if (slot < 0) continue;   // masked: this cell's method has no such slot
            next.p[slot] = value;
        }
        int g = rows.indexOf(next);
        if (g < 0) { rows.append(next); groups.append(QVector<int>()); g = rows.size() - 1; }
        groups[g].append(t);
    }
    if (rows.isEmpty()) return;

    MapUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr;
    const bool macro = stack && rows.size() > 1;
    if (macro)
        stack->beginMacro(tr("Set infiltration on %n cell(s)", nullptr,
                             int(tris.size())));
    for (int g = 0; g < rows.size(); ++g)
        mesh::pushCellInfilEdit(m_activeMesh, groups[g], rows[g], m_canvas);
    if (macro) stack->endMacro();
}

void MeshEditingToolbar::onCellTagCommit()
{
    if (!m_activeMesh || !m_cellTagEdit) return;
    const QList<int> cells = currentSelectedCells();
    if (cells.isEmpty()) return;
    const QString tag = m_cellTagEdit->text().trimmed();
    mesh::pushCellTagEdit(m_activeMesh,
                          QVector<int>(cells.cbegin(), cells.cend()),
                          tag, m_canvas);
}

void MeshEditingToolbar::updateEnabledState()
{
    const bool haveMesh = m_activeMesh != nullptr;
    m_actEditVertex->setEnabled(haveMesh);
    m_actEditEdge->setEnabled(haveMesh);

    const bool vertexMode = haveMesh && m_actEditVertex->isChecked();
    m_zSpin->setEnabled(vertexMode && !currentSelectedVertices().isEmpty());

    // Vertex tag + coupled-node editors (vertex group): shown + editable when
    // one OR MORE vertices are selected in vertex mode; committing overwrites
    // every selected vertex. Hidden otherwise (same collapse as BC controls).
    const bool showVertexEdit =
        vertexMode && !currentSelectedVertices().isEmpty();
    if (m_actVertexTag)       m_actVertexTag->setVisible(showVertexEdit);
    if (m_actVertexCoupled)   m_actVertexCoupled->setVisible(showVertexEdit);
    if (m_vertexTagEdit)      m_vertexTagEdit->setEnabled(showVertexEdit);
    if (m_vertexCoupledCombo) m_vertexCoupledCombo->setEnabled(showVertexEdit);

    // Coupling Cd / Area: only meaningful on coupled vertices, so shown +
    // editable when the selection carries at least one. They appear the
    // moment a coupling is committed (attributeChanged → refresh chain).
    bool anyCoupled = false;
    if (showVertexEdit) {
        const auto &vv = m_activeMesh->mesh().vertices;
        for (int vi : currentSelectedVertices())
            if (vi >= 0 && vi < vv.size() && !vv[vi].coupledNode.isEmpty()) {
                anyCoupled = true; break;
            }
    }
    const bool showCoupling = showVertexEdit && anyCoupled;
    if (m_actVertexCd)     m_actVertexCd->setVisible(showCoupling);
    if (m_actVertexArea)   m_actVertexArea->setVisible(showCoupling);
    if (m_vertexCdSpin)    m_vertexCdSpin->setEnabled(showCoupling);
    if (m_vertexAreaSpin)  m_vertexAreaSpin->setEnabled(showCoupling);

    // Auto-couple works from a selection OR the whole mesh, so it only
    // needs vertex mode (not a selection).
    if (m_actAutoCouple) {
        m_actAutoCouple->setVisible(vertexMode);
        m_actAutoCouple->setEnabled(vertexMode);
    }

    // Cell parameter + tag editors (2D-cell group): shown + editable when one
    // OR MORE cells are selected on an editable mesh; committing overwrites all.
    const bool showCellEdit = haveMesh && !currentSelectedCells().isEmpty();
    if (m_actCellParam)  m_actCellParam->setVisible(showCellEdit);
    if (m_actCellTag)    m_actCellTag->setVisible(showCellEdit);
    if (m_cellParamPage) m_cellParamPage->setEnabled(showCellEdit);
    if (m_cellTagEdit)   m_cellTagEdit->setEnabled(showCellEdit);
    // The whole-row infiltration form reads the selection, so it is dead
    // without one. Disabled rather than hidden, unlike the editor widgets
    // above: the same QAction is mirrored into the Model ▸ Mesh menu, and a
    // menu entry that disappears is harder to find than one that is greyed.
    if (m_actCellInfil) m_actCellInfil->setEnabled(showCellEdit);

    // Slice §V.VC — BC controls follow Edit Edge mode + selection state.
    // BCs apply to boundary edges only, so the param stack enables only when
    // the selection contains at least one boundary edge (interior edges are
    // still selectable for flux plotting).
    const bool edgeMode = haveMesh && m_actEditEdge->isChecked();
    bool haveBoundaryEdge = false;
    if (haveMesh) {
        for (const auto &pr : currentSelectedEdges())
            if (m_activeMesh->isBoundaryEdge(pr.first, pr.second)) { haveBoundaryEdge = true; break; }
    }

    // §V.VC — BC controls appear CONTEXTUALLY (hidden, not just disabled, so
    // they free toolbar space): the type combo only when a boundary edge is
    // selected in edge mode; the param widget only for a type that carries a
    // parameter (not Wall); the "…" browse button only for TS / curve types.
    using mesh::MeshBCTypes;
    const auto curType = m_bcTypeCombo
        ? static_cast<MeshBCTypes::Type>(m_bcTypeCombo->currentData().toInt())
        : MeshBCTypes::Type::Wall;
    const bool typeHasParam = (curType != MeshBCTypes::Type::Wall);
    const bool browsable = (curType == MeshBCTypes::Type::SpecifiedStageTS ||
                            curType == MeshBCTypes::Type::SpecifiedFlowTS ||
                            curType == MeshBCTypes::Type::RatingCurve);

    const bool showBC = edgeMode && haveBoundaryEdge;
    if (m_actBCTypeCombo)  m_actBCTypeCombo->setVisible(showBC);
    if (m_actBCParamStack) m_actBCParamStack->setVisible(showBC && typeHasParam);
    if (m_actBrowseObj)    m_actBrowseObj->setVisible(showBC && browsable);

    // Keep the embedded widgets enabled when shown.
    m_bcTypeCombo->setEnabled(showBC);
    m_bcParamStack->setEnabled(showBC && typeHasParam);
    if (m_actBrowseObj) m_actBrowseObj->setEnabled(showBC && browsable);

    // Engine §11A — ψ is independent of haveBoundaryEdge: any selected edge
    // (interior or boundary) is a valid target. Visibility tracks edgeMode
    // and "any edges selected".
    const bool haveAnyEdge = haveMesh && !currentSelectedEdges().isEmpty();
    const bool showPsi     = edgeMode && haveAnyEdge;
    if (m_actConveySpin) m_actConveySpin->setVisible(showPsi);
    if (m_conveySpin)    m_conveySpin->setEnabled(showPsi);

    refreshGroupWidths();
}

// ---------------------------------------------------------------------
// Slice §V.VC — edge selection + BC editing
// ---------------------------------------------------------------------

QList<QPair<int,int>> MeshEditingToolbar::currentSelectedEdges() const
{
    QList<QPair<int,int>> out;
    if (!m_selection || !m_activeMesh) return out;
    const QString wantKey = mesh::MeshObjectRef::layerKey(m_activeMesh->sourcePath());
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshEdge) continue;
        QString lk;
        int tri = -1, e = -1;
        if (!mesh::MeshObjectRef::parseEdge(ref, &lk, &tri, &e)) continue;
        if (lk != wantKey) continue;
        out.append(qMakePair(tri, e));
    }
    // SelectionManager holds a QSet, whose iteration order is hash-seeded
    // per process — sort so "the first selected edge", command slot order,
    // and everything displayed from this list are deterministic.
    std::sort(out.begin(), out.end());
    return out;
}

void MeshEditingToolbar::onBCTypeChanged(int index)
{
    // Display-only refresh in progress — never commit from it (§V.VC.3).
    if (m_refreshingEdgeEditor) return;
    // Stack index alignment matches the combo: see buildUi.
    if (m_bcParamStack && index >= 0 && index < m_bcParamStack->count())
        m_bcParamStack->setCurrentIndex(index);
    // Apply the new type immediately to all selected edges (the param
    // value carries over from whatever is currently in the spin/combo
    // for the new page).
    commitBCParam();
    // The Browse button only makes sense for the TS / Curve types.
    updateEnabledState();
}

void MeshEditingToolbar::commitBCParam()
{
    if (m_refreshingEdgeEditor) return;   // display-only refresh (§V.VC.3)
    if (!m_activeMesh || !m_bcTypeCombo) return;
    // Mixed-type display parks the combo at index -1, where currentData()
    // is an invalid variant whose toInt() is 0 == Wall — committing from
    // that state would silently wall off every selected edge.
    if (m_bcTypeCombo->currentIndex() < 0) return;
    const auto edges = currentSelectedEdges();
    if (edges.isEmpty()) return;

    using mesh::MeshBCTypes;
    const auto t = static_cast<MeshBCTypes::Type>(
        m_bcTypeCombo->currentData().toInt());

    mesh::MeshEdgeBC bc;
    bc.type = t;
    switch (t) {
    case MeshBCTypes::Type::Wall:
        break;
    case MeshBCTypes::Type::NormalFlow:
        bc.slope = m_slopeSpin ? m_slopeSpin->value() : 0.0;
        break;
    case MeshBCTypes::Type::SpecifiedStageConst:
        bc.head = m_stageSpin ? m_stageSpin->value() : 0.0;
        break;
    case MeshBCTypes::Type::SpecifiedStageTS:
        bc.tseries = m_stageTSCombo ? m_stageTSCombo->currentText() : QString();
        break;
    case MeshBCTypes::Type::SpecifiedFlowConst:
        bc.flow = m_flowSpin ? m_flowSpin->value() : 0.0;
        break;
    case MeshBCTypes::Type::SpecifiedFlowTS:
        bc.tseries = m_flowTSCombo ? m_flowTSCombo->currentText() : QString();
        break;
    case MeshBCTypes::Type::RatingCurve:
        bc.curve = m_curveCombo ? m_curveCombo->currentText() : QString();
        break;
    }

    // pushEdgeBCEdit keeps each slot's existing group label — group editing
    // ships via §V.VD's dedicated Group submenu, not this commit path — and
    // skips interior edges, since boundary conditions are meaningful only
    // where the mesh ends (interior edges stay selectable for flux plotting).
    // One undo entry for the whole selection.
    mesh::pushEdgeBCEdit(m_activeMesh,
                         QVector<QPair<int,int>>(edges.cbegin(), edges.cend()),
                         bc, m_canvas);
}

void MeshEditingToolbar::commitConveyance()
{
    if (m_refreshingEdgeEditor) return;   // display-only refresh (§V.VC.3)
    if (!m_activeMesh || !m_conveySpin) return;
    const auto edges = currentSelectedEdges();
    if (edges.isEmpty()) return;
    // ψ applies to interior edges too, so the helper does NOT gate on
    // isBoundaryEdge for this key (unlike the BC commit above), and the
    // layer mirrors the value onto the neighbour half.
    mesh::pushEdgeParamEdit(m_activeMesh,
                            QVector<QPair<int,int>>(edges.cbegin(), edges.cend()),
                            "conveyance", m_conveySpin->value(), m_canvas);
}

void MeshEditingToolbar::onBrowseBCObject()
{
    if (!m_bcTypeCombo) return;
    using mesh::MeshBCTypes;
    const auto t = static_cast<MeshBCTypes::Type>(
        m_bcTypeCombo->currentData().toInt());
    switch (t) {
    case MeshBCTypes::Type::SpecifiedStageTS: onPickStageTseries(); break;
    case MeshBCTypes::Type::SpecifiedFlowTS:  onPickFlowTseries();  break;
    case MeshBCTypes::Type::RatingCurve:      onPickRatingCurve();  break;
    default: break;   // browse is a no-op for scalar types
    }
}

void MeshEditingToolbar::onPickStageTseries()
{
    if (!m_stageTSPicker || !m_stageTSCombo) return;
    const QString picked = m_stageTSPicker(m_stageTSCombo->currentText());
    refreshBCNameLists();   // CRUD may have added/renamed a TS
    if (!picked.isEmpty()) m_stageTSCombo->setCurrentText(picked);
}

void MeshEditingToolbar::onPickFlowTseries()
{
    if (!m_flowTSPicker || !m_flowTSCombo) return;
    const QString picked = m_flowTSPicker(m_flowTSCombo->currentText());
    refreshBCNameLists();
    if (!picked.isEmpty()) m_flowTSCombo->setCurrentText(picked);
}

void MeshEditingToolbar::onPickRatingCurve()
{
    if (!m_curvePicker || !m_curveCombo) return;
    const QString picked = m_curvePicker(m_curveCombo->currentText());
    refreshBCNameLists();
    if (!picked.isEmpty()) m_curveCombo->setCurrentText(picked);
}

void MeshEditingToolbar::refreshBCNameLists()
{
    auto repop = [](QComboBox *combo, const QStringList &names) {
        if (!combo) return;
        const QString keep = combo->currentText();
        QSignalBlocker block(combo);
        combo->clear();
        // ONE insert, not one per name. addItem() in a loop emits
        // rowsInserted per item, and on macOS each emission makes Qt's
        // accessibility bridge rebuild the combo view's ENTIRE element array
        // (QMacAccessibilityElement updateTableModel -> populateTableArray),
        // which is quadratic in the list length. QSignalBlocker does not help:
        // it silences the combo, not its internal model. addItems() brackets
        // the whole range in a single begin/endInsertRows.
        QStringList entries;
        entries.reserve(names.size() + 1);
        entries << QString();       // empty entry = "(none)"
        entries << names;
        combo->addItems(entries);
        if (!keep.isEmpty()) combo->setCurrentText(keep);
    };
    const QStringList tsNames = m_tsLister ? m_tsLister() : QStringList{};
    const QStringList curveNames = m_curveLister ? m_curveLister() : QStringList{};
    repop(m_stageTSCombo, tsNames);
    repop(m_flowTSCombo,  tsNames);
    repop(m_curveCombo,   curveNames);
}

void MeshEditingToolbar::showEvent(QShowEvent *event)
{
    QToolBar::showEvent(event);
    if (m_nodeListStale) refreshNodeList();
}

void MeshEditingToolbar::refreshNodeList()
{
    if (!m_vertexCoupledCombo) return;

    // This is wired to SWMMModelLayer::geometryChanged, so it runs on EVERY
    // model edit -- every object added, deleted or moved -- and it costs
    // O(nodes) each time (150-400 ms on an all-pipes model). Paying that for
    // a dropdown the user cannot see is pure waste, so defer while hidden and
    // flush in showEvent().
    //
    // This is NOT a repeat of the reverted AttributeTablePanel::refresh
    // deferral. That one was reverted because the table's MODEL is read
    // programmatically by callers who never show the panel, so skipping it
    // changed results. This combo has exactly one reader -- a user picking
    // from it -- and the function already preserves currentText across
    // repopulation, so a deferred flush is observationally identical.
    if (!isVisible()) {
        m_nodeListStale = true;
        return;
    }
    m_nodeListStale = false;

    const QString keep = m_vertexCoupledCombo->currentText();
    QSignalBlocker block(m_vertexCoupledCombo);
    m_vertexCoupledCombo->clear();
    const QStringList nodes = m_nodeLister ? m_nodeLister() : QStringList{};
    // Batch insert — see refreshBCNameLists() for why the per-item loop this
    // replaces was quadratic. This one is the expensive instance: the list is
    // every node in the model (42,809 on West Whiteland), and a stack sample
    // caught the app burning 100% CPU here, inside
    // -[QMacAccessibilityElement populateTableArray:], during window
    // activation -- i.e. on every model open and every tab switch.
    QStringList entries;
    entries.reserve(nodes.size() + 1);
    entries << QString();   // blank = uncoupled
    entries << nodes;
    m_vertexCoupledCombo->addItems(entries);
    m_vertexCoupledCombo->setCurrentText(keep);
}
