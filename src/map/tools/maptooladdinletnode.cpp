/*!
 * \file   maptooladdinletnode.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptooladdinletnode.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/inletjunctionsetupdialog.h"

#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_links.h>

#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

using ConduitSplitPick::ConduitHit;

OpenSWMMVisMapToolAddInletNode::OpenSWMMVisMapToolAddInletNode(
        MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Add Inlet Junction"), canvas, parent)
{
}

OpenSWMMVisMapToolAddInletNode::~OpenSWMMVisMapToolAddInletNode()
{
    if (m_pendingDialog)
        m_pendingDialog->close();   // WA_DeleteOnClose
}

openswmmvis::ui::InletJunctionSetupDialog *
OpenSWMMVisMapToolAddInletNode::pendingDialog() const
{
    return m_pendingDialog.data();
}

QCursor OpenSWMMVisMapToolAddInletNode::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolAddInletNode::activate()
{
    m_hover = {};
    m_armed = false;
    OpenSWMMVisMapTool::activate();
    emit statusMessageChanged(
        tr("Click a street conduit to insert an inlet junction at that point."));
}

void OpenSWMMVisMapToolAddInletNode::deactivate()
{
    m_hover = {};
    m_armed = false;
    emit statusMessageChanged(QString());
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolAddInletNode::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    m_hover = ConduitSplitPick::pickConduit(m_canvas, event->pos());
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addij-rubber"));
}

void OpenSWMMVisMapToolAddInletNode::mousePressEvent(QMouseEvent *event)
{
    // Arm on press, commit on release: the setup dialog below is modal, and a
    // modal session started while the button is down latches the canvas's
    // button state app-wide on macOS.
    m_armed = (event->button() == Qt::LeftButton) && m_canvas;
}

void OpenSWMMVisMapToolAddInletNode::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_armed || !m_canvas)
        return;
    m_armed = false;

    const ConduitHit hit = ConduitSplitPick::pickConduit(m_canvas, event->pos());
    if (!hit.valid()) {
        // D-G3: no free placement — an inlet junction needs a conduit.
        emit statusMessageChanged(
            tr("Inlet junctions are inserted on a conduit — click a conduit."));
        return;
    }
    if (!hit.isStreet) {
        emit statusMessageChanged(
            tr("Inlet junctions can only be placed on street conduits"));
        return;
    }

    // D-G6 — configure before mutating: the engine requires a design and a
    // capture node for the node to validate, so an insert-then-edit flow
    // would leave the model unsaveable between the two steps.
    SWMM_Engine eng = hit.layer->engine();
    QVector<int> exclude;
    if (eng) {
        int n1 = -1, n2 = -1;
        swmm_link_get_from_node(eng, hit.linkIdx, &n1);
        swmm_link_get_to_node  (eng, hit.linkIdx, &n2);
        if (n1 >= 0) exclude << n1;
        if (n2 >= 0) exclude << n2;
    }
    // A second click while a setup panel is still open supersedes it.
    if (m_pendingDialog)
        m_pendingDialog->close();

    // Non-modal floating panel: the user can pick the capture node on the
    // map while it is open. The split runs from accepted(), with the hit
    // re-validated then (the conduit may have been edited meanwhile).
    auto *canvasWidget = qobject_cast<QWidget *>(m_canvas);
    QWidget *parentTop = canvasWidget ? canvasWidget->window() : nullptr;
    auto *dlg = new openswmmvis::ui::InletJunctionSetupDialog(hit.layer, exclude, parentTop);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_pendingDialog = dlg;

    QPointer<SWMMModelLayer> layer(hit.layer);
    QPointer<MapCanvas>      canvas(m_canvas);
    const QString splitLink = hit.name;
    const double  t         = hit.t;
    connect(dlg, &QDialog::accepted, this, [this, dlg, layer, canvas, splitLink, t]() {
        if (!layer || !canvas) return;
        if (layer->linkIndex(splitLink) < 0) {
            emit statusMessageChanged(
                tr("Conduit \"%1\" no longer exists — inlet junction not inserted.")
                    .arg(splitLink));
            return;
        }
        const QString nodeName = ConduitSplitPick::nextNodeName(
            layer, QStringLiteral("inlet_junction"));
        const QString linkName = ConduitSplitPick::nextSplitLinkName(layer, splitLink);

        auto *cmd = new InsertInletJunctionCommand(layer, splitLink, t,
                                                   nodeName, linkName,
                                                   dlg->inletDesign(),
                                                   dlg->captureNode(), canvas);
        if (canvas->undoStack())
            canvas->undoStack()->push(cmd);
        else
            delete cmd;

        if (layer->nodeIndex(nodeName) >= 0) {
            // swmm_conduit_split_inlet creates the row with AUTOMATIC
            // placement; apply the chosen mode as its own undoable step so
            // the setup dialog's third field is not silently dropped.
            const int ni = layer->nodeIndex(nodeName);
            SWMM_InletUsage u{};
            if (dlg->placement() != int(SWMM_INLET_AUTOMATIC)
                && layer->inletUsageFor(SWMM_INLET_HOST_NODE, ni, &u)) {
                u.placement = dlg->placement();
                layer->pushInletUsageEdit(u);
            }

            layer->setSelectedElements({{nodeName, SWMMModelLayer::kKindNode}});
            emit inletJunctionAdded(nodeName, splitLink, linkName);
            emit statusMessageChanged(
                tr("Inserted inlet junction \"%1\" on \"%2\".")
                    .arg(nodeName, splitLink));
        } else {
            emit statusMessageChanged(
                tr("Could not insert an inlet junction on \"%1\".").arg(splitLink));
        }
        canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                           QStringLiteral("addij-commit"));
    });
    connect(dlg, &QDialog::rejected, this, [this]() {
        emit statusMessageChanged(tr("Inlet junction cancelled."));
    });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();

    m_hover = {};
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addij-commit"));
}

void OpenSWMMVisMapToolAddInletNode::paint(QPainter *painter, const MapExtent &,
                                            const SpatialReferenceSystem *)
{
    if (!painter || !m_hover.valid()) return;

    // Marker preview at the prospective split point (layer CRS → pixels via
    // the canvas transform on the scene-space coordinate).
    double cx = m_hover.point.x(), cy = m_hover.point.y();
    m_hover.layer->transformLayerToCanvas(m_hover.point.x(), m_hover.point.y(),
                                          cx, cy);
    int px = 0, py = 0;
    toPixelCoords(cx, cy, px, py);

    painter->save();
    // Match the placed symbol's colour; a non-street conduit under the cursor
    // draws the same marker in outline only, so the hover shows the tool is
    // live but the click will be refused.
    QPen pen(m_hover.layer->inletJunctionSymbol().fillColor);
    pen.setWidth(2);
    pen.setStyle(m_hover.isStreet ? Qt::SolidLine : Qt::DotLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    // Diamond — the inlet-junction marker shape (D-G7).
    const QPolygon diamond({ QPoint(px, py - 7), QPoint(px + 7, py),
                             QPoint(px, py + 7), QPoint(px - 7, py) });
    painter->drawPolygon(diamond);
    if (m_hover.isStreet) {
        painter->drawLine(px - 10, py, px + 10, py);
        painter->drawLine(px, py - 10, px, py + 10);
    }
    painter->restore();
}
