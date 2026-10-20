/*!
 * \file   remap1d2ddialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/remap1d2ddialog.h"

#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/meshcommands.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshenginesync.h"
#include "ui/widgets/nodecouplingselector.h"

#include <openswmm/engine/openswmm_nodes.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <cmath>

Remap1D2DDialog::Remap1D2DDialog(SWMMModelLayer *model, SWMM2DMeshLayer *mesh, MapCanvas *canvas,
                                 QVector<QPair<QString, QPointF>> nodes,
                                 const QStringList &mapSelection, const QString &extraInfo,
                                 std::function<void()> ownershipReview, QWidget *parent)
    : QDialog(parent), m_layer(model), m_mesh(mesh), m_canvas(canvas), m_nodes(std::move(nodes)),
      m_model(new NodeCouplingTableModel(this))
{
    setWindowTitle(tr("Remap 1D ↔ 2D"));
    resize(820, 620);
    m_authorCdArea = mesh::couplingAuthorsCdArea(model ? model->engine() : nullptr);
    m_model->setExtraColumns({tr("Current"), tr("Proposed"), tr("|rim − bed|")});
    m_model->load(model);

    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Couple the 1D nodes to the active mesh. Switch nodes on or off "
                                "here; the Proposed column follows."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto *host = new QWidget(this);
    NodeCouplingSelectorDialog::buildEditor(host, m_model, mapSelection, nullptr);
    lay->addWidget(host, 1);

    auto *opts = new QGroupBox(tr("Existing couplings"), this);
    auto *optLay = new QHBoxLayout(opts);
    m_rebuild = new QRadioButton(tr("Clear && rebuild"), opts);
    m_rebuild->setToolTip(tr("Drop every coupling on this mesh and map all coupleable nodes "
                             "again. A node that lands where it was keeps its Cd / area."));
    m_preserve = new QRadioButton(tr("Preserve existing"), opts);
    m_preserve->setToolTip(tr("Keep the couplings of coupleable nodes; map only the others. "
                              "Couplings of nodes switched off are removed."));
    m_rebuild->setChecked(true);
    m_onlyCoupleable = new QCheckBox(tr("Map only coupleable nodes"), opts);
    m_onlyCoupleable->setChecked(true);
    optLay->addWidget(m_rebuild);
    optLay->addWidget(m_preserve);
    optLay->addStretch();
    optLay->addWidget(m_onlyCoupleable);
    lay->addWidget(opts);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(m_summary);
    if (!extraInfo.isEmpty()) {
        auto *extra = new QLabel(extraInfo, this);
        extra->setWordWrap(true);
        extra->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lay->addWidget(extra);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *applyBtn = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    if (ownershipReview) {
        auto *review = buttons->addButton(tr("Review surface ownership…"), QDialogButtonBox::ActionRole);
        connect(review, &QPushButton::clicked, this, [this, ownershipReview] {
            reject();
            ownershipReview();
        });
    }
    applyBtn->setDefault(true);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(applyBtn, &QPushButton::clicked, this, [this] {
        QString error;
        if (apply(&error)) accept();
        else QMessageBox::warning(this, windowTitle(), error);
    });

    connect(m_rebuild, &QRadioButton::toggled, this, [this] { refreshPlan(); });
    connect(m_onlyCoupleable, &QCheckBox::toggled, this, [this] { refreshPlan(); });
    connect(m_model, &NodeCouplingTableModel::countsChanged, this, [this] { refreshPlan(); });
    refreshPlan();
}

void Remap1D2DDialog::setClearAndRebuild(bool on)
{
    (on ? m_rebuild : m_preserve)->setChecked(true);
}

void Remap1D2DDialog::setMapOnlyCoupleable(bool on)
{
    m_onlyCoupleable->setChecked(on);
}

void Remap1D2DDialog::refreshPlan()
{
    if (!m_mesh) return;
    const bool only = m_onlyCoupleable->isChecked();
    auto coupleable = [this, only](const QString &n) {
        return !only || m_model->rowOf(n) < 0 || m_model->isCoupleable(n);
    };
    const mesh::MeshResult &mr = m_mesh->mesh();
    m_plan = mesh::planRemap(mr, m_nodes, coupleable, m_rebuild->isChecked(), m_authorCdArea);

    // |rim − bed| at the proposed coupling: the node's rim (invert + depth)
    // against the vertex or cell-centroid bed — a large gap is a datum
    // mismatch or a node mapped to the wrong terrain.
    QHash<QString, double> bed;
    for (auto it = m_plan.mapped.vertexMatches.cbegin(); it != m_plan.mapped.vertexMatches.cend(); ++it)
        bed.insert(it.value(), mr.vertices[it.key()].z);
    for (const mesh::CellCoupling &c : m_plan.mapped.cellMatches)
        if (!bed.contains(c.nodeId) && c.tri >= 0 && c.tri < mr.triangles.size()) {
            const mesh::MeshTriangle &t = mr.triangles[c.tri];
            double z = 0.0;
            for (int k = 0; k < t.vertexCount(); ++k) z += mr.vertices[t.vertex(k)].z;
            bed.insert(c.nodeId, z / t.vertexCount());
        }
    const SWMM_Engine e = m_layer ? m_layer->engine() : nullptr;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const QString node = m_model->rowAt(r).edited.node;
        QString gap;
        if (e && bed.contains(node)) {
            const int idx = swmm_node_index(e, node.toUtf8().constData());
            double inv = 0.0, depth = 0.0;
            if (idx >= 0 && swmm_node_get_invert_elev(e, idx, &inv) == 0
                && swmm_node_get_max_depth(e, idx, &depth) == 0)
                gap = QString::number(std::fabs(inv + depth - bed.value(node)), 'f', 2);
        }
        m_model->setExtraValues(node, {m_plan.current.value(node, QStringLiteral("—")),
                                       m_plan.proposed.value(node, m_plan.current.value(node, QStringLiteral("—"))),
                                       gap});
    }

    QString s = tr("Proposed: %1 vertex coupling(s), %2 cell coupling(s); %3 vertex(es) change.")
                    .arg(m_plan.mapped.vertexMatches.size())
                    .arg(m_plan.mapped.cellMatches.size())
                    .arg(m_plan.vertices.size());
    if (m_plan.mapped.sharedCells > 0)
        s += tr(" %1 cell(s) receive more than one node.").arg(m_plan.mapped.sharedCells);
    if (!m_plan.mapped.unmatched.isEmpty()) {
        const QStringList head = m_plan.mapped.unmatched.mid(0, 8);
        s += tr(" %1 node(s) fall outside the mesh: %2%3")
                 .arg(m_plan.mapped.unmatched.size())
                 .arg(head.join(QStringLiteral(", ")))
                 .arg(m_plan.mapped.unmatched.size() > head.size() ? QStringLiteral(", …") : QString());
    }
    if (!m_authorCdArea)
        s += tr(" New rows leave Cd / area to each node's structure (weir / orifice law).");
    m_summary->setText(s);
}

bool Remap1D2DDialog::apply(QString *error)
{
    if (!m_mesh || !m_layer) return false;
    refreshPlan();
    MapUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr;
    if (stack) stack->beginMacro(tr("Remap 1D ↔ 2D"));
    bool ok = m_layer->pushNodeCoupling2DEdits(m_model->changedStates(), tr("Set Node Coupling"), error);
    if (ok) {
        if (stack) {
            if (!m_plan.vertices.isEmpty())
                stack->push(new MeshSetVertexAttributeCommand(
                    m_mesh, QByteArrayLiteral("coupledNode"), m_plan.vertices, m_plan.newAttrs,
                    m_plan.oldAttrs, tr("Remap Vertex Couplings"), m_canvas));
            stack->push(new MeshSetCellCouplingsCommand(
                m_mesh, m_plan.newCells, m_mesh->cellCouplings(), tr("Remap Cell Couplings"), m_canvas));
        } else {
            for (int i = 0; i < m_plan.vertices.size(); ++i) {
                const int v = m_plan.vertices[i];
                const mesh::MeshVertex &a = m_plan.newAttrs[i];
                m_mesh->applyMeshVertexCoupledNode(v, a.coupledNode);
                m_mesh->applyMeshVertexCouplingCd(v, a.couplingCd);
                m_mesh->applyMeshVertexCouplingArea(v, a.couplingArea);
                m_mesh->applyMeshVertexCouplingAuthored(v, a.couplingCdSet, a.couplingAreaSet);
            }
            m_mesh->applyCellCouplings(m_plan.newCells);
        }
    }
    if (stack) stack->endMacro();
    return ok;
}
