/*!
 * \file   maptooladdtext.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/tools/maptooladdtext.h"

#include "layers/annotationlayer.h"
#include "layers/annotationtextitem.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "ui/dialogs/annotationstyledialog.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPointF>

namespace {

/*! Convert a map (x, y) to scene coords using the canvas Y-flip convention
 *  (see annotationlayer.cpp). Kept private so the picker can hit-test in
 *  scene space without leaking the convention into the public tool API. */
QPointF mapToScenePoint(double mapX, double mapY) noexcept
{
    return { mapX, -mapY };
}

} // anonymous

OpenSWMMVisMapToolAddText::OpenSWMMVisMapToolAddText(MapCanvas *canvas,
                                                     OpenSWMMVisAnnotationLayer *layer,
                                                     QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Add Text"), canvas, parent)
    , m_layer(layer)
{
}

QCursor OpenSWMMVisMapToolAddText::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolAddText::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas || !m_layer) return;
    if (event->button() != Qt::LeftButton) return;

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    // First, check whether the click hit an existing annotation — if so,
    // open the editor for that one instead of creating a new item. This is
    // the in-place edit affordance promised by the dialog-on-place / edit-
    // on-reclick UX.
    if (auto *existing = m_layer->annotationAtScenePos(mapToScenePoint(mx, my))) {
        AnnotationStyleDialog dlg(existing, m_canvas->parentWidget()
                                              ? m_canvas->parentWidget()
                                              : qobject_cast<QWidget *>(parent()));
        dlg.setEditMode(true);
        dlg.exec();
        // The dialog edits the live AnnotationTextItem directly via
        // QPropertyModel, so accept-or-cancel state is already reflected.
        // (A "Cancel must restore" path would need a snapshot — left to a
        // follow-up; right now the editor commits as it goes.)
        return;
    }

    // Fresh placement: build a default-styled item, show the dialog seeded
    // with it. On accept, push an AddAnnotationCommand so the placement is
    // undoable; on cancel, the temporary item is discarded.
    auto *item = new AnnotationTextItem();
    item->setPosition(mx, my);

    AnnotationStyleDialog dlg(item, m_canvas->parentWidget()
                                          ? m_canvas->parentWidget()
                                          : qobject_cast<QWidget *>(parent()));
    dlg.setEditMode(false);
    const int rc = dlg.exec();
    if (rc != QDialog::Accepted) {
        delete item;
        return;
    }

    // Hand ownership to an undo command. The command takes the item, adopts
    // it into the layer on redo, and detaches (via takeAnnotation) on undo.
    auto *cmd = new AddAnnotationCommand(m_layer, item, m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;  // command would orphan the item; safer to clean up

    emit annotationAdded(item->id());

    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("addtext-commit"));
}
