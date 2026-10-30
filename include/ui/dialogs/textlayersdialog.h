/**
 * @file      textlayersdialog.h
 * @brief     Text Layers Dialog interface and supporting types.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_TEXTLAYERSDIALOG_H
#define OPENSWMM_TEXTLAYERSDIALOG_H
#include <QDialog>
#include <QPointer>
#include <QJsonObject>
class MapCanvas;
class OpenSWMMVisAnnotationLayer;
class QComboBox;
class QListWidget;
class QCheckBox;
class TextLayersDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] canvas Required borrowed raw canvas, must outlive dialog/callbacks/modal prompts;
     *   canvas undoStack must exist for edits/moves/generation.
     * @param[in] selected Optional borrowed annotation layer identity to preselect if currently in
     *   canvas; absent uses first annotation layer.
     * @param[in] parent Optional QWidget owner, caller manages parentless dialog.
     * @brief Build live text-layer manager, listing canvas annotation layers and ID-based items on
     *   GUI thread.
     * @note Operations apply immediately with snapshot undo; Close does not discard them. New layer
     *   is canvas-parented, copies canvas CRS and uses canvas addLayer with undo. Rename trims
     *   nonblank name; layout toggle edits selected layer; halo applies selected annotations or ALL
     *   when no selection; Delete/Unpin apply selected IDs (Unpin clears pinned and pixel offset).
     * @note Move dialog retains raw source/target through nested loop, transforms each native
     *   anchor into target CRS, skips failed transforms, transfers item ownership and records both
     *   complete layer snapshots in one macro; no rollback of earlier successful transfers. Layer
     *   list is not subscribed to external canvas additions/removals.
     * @note Edit on map accepts only with selected live layer; it does not install tool itself.
     *   Host checks result/selectedLayer. Annotation item refresh is queued/coalesced for model
     *   notifications; raw canvas and layer pointers in modal callbacks require caller lifetime
     *   coordination.
     */
    explicit TextLayersDialog(MapCanvas *canvas, OpenSWMMVisAnnotationLayer *selected,
                              QWidget *parent = nullptr);
    /**
     * @return Borrowed annotation layer from current combo only if its raw base pointer still
     *   appears in live canvas layers, otherwise null. No ownership transfer or acceptance check;
     *   raw required canvas must live. Does not refresh stale layer list.
     */
    OpenSWMMVisAnnotationLayer *selectedLayer() const;

private:
    /**
     * @param[in] selected Optional identity to prefer among current canvas annotation layers; not
     *   dereferenced until found. Null/absent leaves first item current.
     * @brief Rebuild selector in canvas layer order with combo signals blocked, then explicitly
     *   reload item list.
     * @note Includes hidden annotation layers; raw pointer payloads are not lifetime guards. Does
     *   not preserve former selection unless supplied, rename source or subscribe to canvas layer
     *   changes.
     */
    void reloadLayers(OpenSWMMVisAnnotationLayer *selected = nullptr);
    /**
     * @brief Rebuild selected layer annotations while retaining selected IDs, hydrate layout
     *   checkbox with signals blocked and update observed layer subscription.
     * @note On observed identity change disconnect all old layer-to-this signals;
     *   annotationsChanged queues at most one zero-delay refresh to avoid invalidating row loops.
     *   Guarded observed pointer prevents retained source ownership, but canvas/list payloads
     *   remain raw. Display expression when nonempty else literal text, append linked feature
     *   identity and pinned marker.
     * @note Missing layer clears list/unchecks layout. No source writes, current-row preservation
     *   guarantee, layer-list refresh or immediate source destruction callback. Queued refresh
     *   resolves CURRENT selection rather than retaining old layer.
     */
    void reloadItems();
    /**
     * @brief Open detached AnnotationStyleDialog draft for current live annotation, committing
     *   complete layer before/after snapshot only on accepted result.
     * @note Missing layer/current row/resolved item no-op. Draft cancellation leaves target
     *   unchanged; accept changes target before commit pushes command. Raw target/layer/canvas must
     *   survive modal editing. No model engine writes or noncurrent multi-item edit.
     */
    void editItem();
    /**
     * @brief Prompt for source/category/template and append missing linked feature-label
     *   annotations to selected text layer with undo macro.
     * @note Enumerate SWMM/GIS source features synchronously when source choice changes; list is a
     *   snapshot and source raw pointers must survive modal loop. Changing source resets expression
     *   to its label expression/field/name default; SWMM category filter only active for model
     *   source. Cancel/blank trimmed template exits without changes.
     * @note Skip keys already present in destination and failed source-to-destination CRS
     *   transforms; existing-key set is built before insertion and is not extended for duplicate
     *   keys inside enumeration. New item inherits source label font/color, expression/reference,
     *   callout/halo choices and pixel offset (12,-12); native anchor positions are transformed.
     *   Existing annotations/custom edits remain.
     * @note Enable automatic layout only if checked (unchecked does not disable existing layout).
     *   Snapshot destination edit and optional disable of source labels share macro; source labels
     *   disabled only if at least one added and option checked. No additions can still enable
     *   layout and produce undo before information message. No cap, progress/cancel loop, source
     *   engine mutation or rollback of partial additions.
     */
    void generate();
    /**
     * @param[in] before Caller-owned complete selected-layer JSON snapshot captured BEFORE
     *   already-performed direct mutations; must correspond to CURRENT selected layer.
     * @param[in] label Copied undo-menu text.
     * @brief Resolve current selected layer, snapshot after, push EditTextLayerCommand only for
     *   unequal JSON and reload item list.
     * @note Null selection no-op; required canvas undo stack is dereferenced without guard. Command
     *   first redo restores complete after snapshot, potentially recreating annotation objects; do
     *   not retain old item pointers across push. No per-field merge, transaction rollback, source
     *   identity validation or engine edit.
     */
    void commit(const QJsonObject &before, const QString &label);
    bool m_refreshPending = false;
    MapCanvas *m_canvas;
    QComboBox *m_layers;
    QListWidget *m_items;
    QCheckBox *m_layout;
    QPointer<OpenSWMMVisAnnotationLayer> m_observed;
};
#endif
