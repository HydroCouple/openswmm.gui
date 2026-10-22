/**
 * @file      legendclasseditcommands.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QUndoCommand wrappers for per-class renderer edits driven from
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_LEGEND_CLASS_EDIT_COMMANDS_H
#define OPENSWMMVIS_MAP_LEGEND_CLASS_EDIT_COMMANDS_H

#include <QColor>
#include <QPointer>
#include <QString>
#include <QUndoCommand>

class OpenSWMMVisLayer;

namespace openswmmvis::map
{

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
    /**
     * @brief Snapshot original class colour at construction and store target for subsequent
     *   undo/redo.
     * @param[in] layer Optional borrowed layer guarded by QPointer; null/deletion makes application
     *   no-op.
     * @param[in] classKey Opaque exact current legend class key; kind-qualified for
     *   model/1D-results facades.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns command after push.
     * @param[in] newColor Target QColor including invalid value; interpretation delegated to
     *   renderer.
     * @note GUI-thread operation; does not edit at construction. Caller must check supported
     *   class-edit capability/key. invalid QColor follows renderer no-override/drop-override
     *   convention. QPointer guards layer destruction, not class-key disappearance or renderer
     *   replacement.
     */
    SetRendererClassColorCommand(OpenSWMMVisLayer *layer, QString classKey, QColor newColor,
                                 QUndoCommand *parent = nullptr);

    /**
     * @brief Apply the original captured class colour through the current renderer/facade route.
     * @note Null/deleted layer no-op; unsupported/key errors are ignored. Original snapshot belongs
     *   to construction-time renderer state, not a full style snapshot.
     */
    void undo() override;
    /**
     * @brief Apply latest target class colour, including initial push; no first-redo skip.
     * @note Uses current renderer/key route and its notifications; no engine edits or success
     *   reporting.
     */
    void redo() override;

    /**
     * @return Stable command-family discriminator 0x4C4C4543 for colour used by QUndoStack before
     *   mergeWith; no mutation.
     */
    int id() const override { return 0x4C4C4543; /* "LCEC" — legend class-edit colour */ }
    /**
     * @param[in] other Optional borrowed incoming command; must be same concrete class and
     *   identical guarded layer/key.
     * @return True when matched: replaces only this target colour with incoming target, preserving
     *   original before snapshot. False for null/different class/target. Does not apply value
     *   itself or impose a time threshold; Qt has normally already redone incoming command.
     */
    bool mergeWith(const QUndoCommand *other) override;

private:
    /**
     * @brief Dispatch a stored class colour to current live layer route.
     * @param[in] c Colour including invalid reset value, passed verbatim.
     * @note Deleted layer no-op. Single feature renderer receives setColorForClass then explicit
     *   repaintRequested even if setter rejects; model/1D-results facade handles own repaint.
     *   Unsupported layer no-op; errors are ignored.
     */
    void applyColor(const QColor &c);

    QPointer<OpenSWMMVisLayer> m_layer;
    QString m_classKey;
    QColor m_oldColor; // invalid = "no override before"
    QColor m_newColor;
    bool m_firstRedo = true;
};

/*!
 * \class SetRendererClassSizeCommand
 * \brief Records a per-class size (or line width) change on a layer's
 *        IFeatureRenderer. Mirrors SetRendererClassColorCommand semantics:
 *        snapshots `sizeForClass(classKey)` at construction, restores it
 *        on undo, and merges consecutive edits on the same target so
 *        spinbox drags collapse to a single undo step.
 *
 *        A negative `oldSize` denotes "no per-class size present"; the
 *        renderer's setSizeForClass interprets the value verbatim — only
 *        renderers whose supportsClassEdit(Size) returns true should be
 *        targeted.
 */
class SetRendererClassSizeCommand : public QUndoCommand
{
public:
    /**
     * @brief Snapshot original class size at construction and store target for subsequent
     *   undo/redo.
     * @param[in] layer Optional borrowed layer guarded by QPointer; null/deletion makes application
     *   no-op.
     * @param[in] classKey Opaque exact current legend class key; kind-qualified for
     *   model/1D-results facades.
     * @param[in] parent Optional QUndoCommand owner; otherwise stack owns command after push.
     * @param[in] newSize Target marker size or line width in renderer screen-pixel convention; no
     *   local finite/range validation.
     * @note GUI-thread operation; does not edit at construction. Caller must check supported
     *   class-edit capability/key. negative size is passed through as renderer-specific
     *   unset/default value. QPointer guards layer destruction, not class-key disappearance or
     *   renderer replacement.
     */
    SetRendererClassSizeCommand(OpenSWMMVisLayer *layer, QString classKey, qreal newSize,
                                QUndoCommand *parent = nullptr);

    /**
     * @brief Apply the original captured class size through the current renderer/facade route.
     * @note Null/deleted layer no-op; unsupported/key errors are ignored. Original snapshot belongs
     *   to construction-time renderer state, not a full style snapshot.
     */
    void undo() override;
    /**
     * @brief Apply latest target class size, including initial push; no first-redo skip.
     * @note Uses current renderer/key route and its notifications; no engine edits or success
     *   reporting.
     */
    void redo() override;

    /**
     * @return Stable command-family discriminator 0x4C434553 for size used by QUndoStack before
     *   mergeWith; no mutation.
     */
    int id() const override { return 0x4C434553; /* "LCES" — legend class-edit size */ }
    /**
     * @param[in] other Optional borrowed incoming command; must be same concrete class and
     *   identical guarded layer/key.
     * @return True when matched: replaces only this target size with incoming target, preserving
     *   original before snapshot. False for null/different class/target. Does not apply value
     *   itself or impose a time threshold; Qt has normally already redone incoming command.
     */
    bool mergeWith(const QUndoCommand *other) override;

private:
    /**
     * @brief Dispatch stored class size to current live layer route.
     * @param[in] s Renderer screen-pixel size/width, including negative unset value; no local
     *   validation.
     * @note Deleted/unsupported layer no-op. Single renderer receives setter then repaintRequested;
     *   model facade handles own repaint. Current SWMMResultsLayer has no size WRITE route here,
     *   even though original size can be read. Setter failure ignored.
     */
    void applySize(qreal s);

    QPointer<OpenSWMMVisLayer> m_layer;
    QString m_classKey;
    qreal m_oldSize = -1.0;
    qreal m_newSize = -1.0;
};

} // namespace openswmmvis::map

#endif // OPENSWMMVIS_MAP_LEGEND_CLASS_EDIT_COMMANDS_H
