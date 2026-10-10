/*!
 * \file maptooladdtext.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \license GPL-3.0-or-later
 */
#include "map/tools/maptooladdtext.h"
#include "layers/annotationlayer.h"
#include "layers/annotationtextitem.h"
#include "layers/annotationfeatures.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/textlayercommands.h"
#include "ui/dialogs/annotationstyledialog.h"
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>

OpenSWMMVisMapToolAddText::OpenSWMMVisMapToolAddText(MapCanvas *canvas, OpenSWMMVisAnnotationLayer *layer, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Edit Text"), canvas, parent), m_layer(layer) {}
void OpenSWMMVisMapToolAddText::setLayer(OpenSWMMVisAnnotationLayer *layer) { m_layer = layer; }
QCursor OpenSWMMVisMapToolAddText::cursor() const { return Qt::CrossCursor; }
void OpenSWMMVisMapToolAddText::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas || !m_layer || !m_canvas->layers().contains(m_layer) || !m_layer->isVisible() || event->button() != Qt::LeftButton) return;
    m_pressPixel = event->position(); m_dragged = false; m_before = m_layer->layerJson();
    if (auto *item = m_layer->annotationAtPixel(m_pressPixel)) {
        m_pressedId = item->id(); m_startOffset = m_layer->renderedOffset(item->id());
    } else m_pressedId.clear();
}
void OpenSWMMVisMapToolAddText::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_layer || m_pressedId.isEmpty() || !(event->buttons() & Qt::LeftButton)) return;
    const auto delta = event->position()-m_pressPixel;
    if (!m_dragged && delta.manhattanLength() < QApplication::startDragDistance()) return;
    if (auto *item = m_layer->annotation(m_pressedId)) {
        m_dragged = true; item->setPinned(true); item->setOffset(m_startOffset+delta);
        m_canvas->update();
    }
}
void OpenSWMMVisMapToolAddText::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_canvas || !m_layer || !m_canvas->layers().contains(m_layer) || !m_layer->isVisible()
        || event->button() != Qt::LeftButton || m_before.isEmpty()) return;
    if (m_dragged) {
        const auto after = m_layer->layerJson();
        m_canvas->undoStack()->push(new EditTextLayerCommand(m_layer, m_before, after, tr("Move text")));
    } else if (!m_pressedId.isEmpty()) {
        if (auto *item = m_layer->annotation(m_pressedId)) {
            AnnotationStyleDialog dlg(item, m_canvas, m_canvas); dlg.setEditMode(true);
            if (dlg.exec() == QDialog::Accepted) {
                const auto after = m_layer->layerJson();
                if (after != m_before) m_canvas->undoStack()->push(new EditTextLayerCommand(m_layer, m_before, after, tr("Edit text")));
            }
        }
    } else {
        double x = 0, y = 0; toMapCoords(event->pos().x(), event->pos().y(), x, y);
        QPointF position(x,y);
        if (!AnnotationFeatures::transform(position, m_canvas->canvasSRS(), m_layer->srs())) return;
        auto *item = new AnnotationTextItem(m_layer);
        item->setPosition(position.x(), position.y());
        AnnotationStyleDialog dlg(item, m_canvas, m_canvas);
        if (dlg.exec() == QDialog::Accepted) {
            const QString id = item->id(); m_layer->addAnnotation(item);
            const auto after = m_layer->layerJson();
            m_canvas->undoStack()->push(new EditTextLayerCommand(m_layer, m_before, after, tr("Add text")));
            emit annotationAdded(id);
        } else delete item;
    }
    m_pressedId.clear(); m_before = {}; m_dragged = false;
    m_canvas->update();
}
void OpenSWMMVisMapToolAddText::deactivate()
{
    if (m_layer && m_dragged && !m_before.isEmpty()) m_layer->restoreLayer(m_before);
    m_pressedId.clear(); m_before = {}; m_dragged = false;
    OpenSWMMVisMapTool::deactivate();
}
void OpenSWMMVisMapToolAddText::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) m_canvas->setActiveTool(nullptr);
}
