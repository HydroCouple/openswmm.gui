/*!
 * \file   legendclasseditcommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QUndoCommand wrappers for per-class renderer edits driven from
 *         the legend (Slice BB Phase 8.6.16).
 *
 *         Every per-class colour / size / symbol edit issued by the
 *         legend's right-click menu or by the LegendPropertiesDialog
 *         Layers tab flows through one of these commands so Ctrl+Z
 *         reverses it uniformly across every view of the same data
 *         (canvas paint, on-canvas legend, dock, dialog).
 *
 *         The command stores the layer + classKey, snapshots the
 *         renderer's "before" state through IFeatureRenderer::colorForClass
 *         (or sibling queries), and on undo writes that snapshot back via
 *         setColorForClass. Mutating the renderer is followed by an
 *         explicit emit of layer->repaintRequested() — the canvas + legend
 *         overlay are both subscribed to it (existing signal chain).
 *
 *         The layer is held via QPointer so the command becomes a clean
 *         no-op if the layer is removed before undo / redo fires.
 */
#ifndef OPENSWMMVIS_MAP_LEGEND_CLASS_EDIT_COMMANDS_H
#define OPENSWMMVIS_MAP_LEGEND_CLASS_EDIT_COMMANDS_H

#include <QColor>
#include <QPointer>
#include <QString>
#include <QUndoCommand>

class OpenSWMMVisLayer;

namespace openswmmvis::map {

/*!
 * \class SetRendererClassColorCommand
 * \brief Records a per-class colour change on a layer's IFeatureRenderer.
 *
 *        The "old" colour is read from `renderer->colorForClass(classKey)`
 *        at construction time. For renderers with optional overrides
 *        (GraduatedRenderer) an invalid old colour denotes "no override
 *        was active", and undo restores that state by writing back an
 *        invalid colour — which the renderer's setColorForClass treats
 *        as "drop override".
 *
 *        mergeWith() collapses rapid colour-picker drags on the SAME
 *        (layer, classKey) into one undoable step.
 */
class SetRendererClassColorCommand : public QUndoCommand
{
public:
    SetRendererClassColorCommand(OpenSWMMVisLayer *layer,
                                 QString           classKey,
                                 QColor            newColor,
                                 QUndoCommand     *parent = nullptr);

    void undo() override;
    void redo() override;

    int  id() const override { return 0x4C4C4543; /* "LCEC" — legend class-edit colour */ }
    bool mergeWith(const QUndoCommand *other) override;

private:
    void applyColor(const QColor &c);

    QPointer<OpenSWMMVisLayer> m_layer;
    QString                    m_classKey;
    QColor                     m_oldColor;   // invalid = "no override before"
    QColor                     m_newColor;
    bool                       m_firstRedo = true;
};

} // namespace openswmmvis::map

#endif // OPENSWMMVIS_MAP_LEGEND_CLASS_EDIT_COMMANDS_H
