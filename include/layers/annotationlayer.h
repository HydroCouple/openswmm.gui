/**
 * @file      annotationlayer.h
 * @brief     Map layer for editable text annotations and their presentation.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_LAYERS_ANNOTATIONLAYER_H
#define OPENSWMMVIS_LAYERS_ANNOTATIONLAYER_H
#include "layers/openswmmvislayer.h"
#include <QHash>
#include <QJsonArray>
#include <QTransform>
class AnnotationTextItem;

// Persistent text model. The canvas paints all text layers together, above
// the CPU/GPU map, to share collision placement and screen-space picking.
class OpenSWMMVisAnnotationLayer : public OpenSWMMVisLayer
{
    Q_OBJECT
public:
    /**
     * @brief Create an empty in-memory annotation collection with manual placement.
     * @param[in] name Display name copied to the layer base.
     * @param[in] parent Optional workspace QObject owner. No graphics-scene items or files are created.
     */
    explicit OpenSWMMVisAnnotationLayer(const QString &name = QStringLiteral("Text"),
                                        OpenSWMMVisWorkspace *parent = nullptr);
    /**
     * @brief Adopt a uniquely identified annotation model into this layer.
     * @param[in] item Nonnull item not already identified here; caller must first remove it from another layer's raw list before transferring ownership.
     * @return False for null/duplicate ID, leaving ownership unchanged; true reparents to this QObject, appends and emits annotationsChanged() then repaintRequested().
     * @note Item changed() signals forward to the same aggregate/repaint pair. Do not delete or change its ID externally while attached; use take/remove for lifetime changes.
     */
    bool addAnnotation(AnnotationTextItem *item);
    /**
     * @param[in] id Exact attached item identity.
     * @return True after takeAnnotation() removes membership/notifies and the item is deleted; false when absent. Cached item pointers become invalid on success.
     */
    bool removeAnnotation(const QString &id);
    /**
     * @brief Detach an annotation for undo/transfer without deleting it.
     * @param[in] id Exact attached identity.
     * @return Caller-owned item with QObject parent cleared and item-to-layer connections removed, or null when absent. Clears cached bounds/offset and emits annotationsChanged() then repaintRequested() on success.
     */
    AnnotationTextItem *takeAnnotation(const QString &id);
    /**
     * @param[in] id Exact case-sensitive attached identity.
     * @return Borrowed first matching item, or null; membership is independent of current painted visibility.
     */
    AnnotationTextItem *annotation(const QString &id) const;
    /**
     * @brief Snapshot item ordering without transferring ownership.
     * @return Owned vector of borrowed QObject-owned item pointers in paint order, invalid after item removal/layer destruction.
     */
    QVector<AnnotationTextItem *> annotations() const { return m_items; }
    /**
     * @param[in] scenePos Canvas scene coordinate, mapped by the last valid paint transform.
     * @return annotationAtPixel() hit using last-painted bounds; stale/unpainted cache may not represent the current view.
     */
    AnnotationTextItem *annotationAtScenePos(const QPointF &scenePos) const;
    /**
     * @param[in] pixel Logical viewport pixel.
     * @return Last-added item whose cached rendered rectangle contains the point, or null for hidden/zero-opacity layer/no hit. Uses bounding rectangles rather than exact glyph shapes.
     */
    AnnotationTextItem *annotationAtPixel(const QPointF &pixel) const;
    /**
     * @brief Inspect the most recently painted item hit bounds.
     * @param[in] id Exact annotation identity.
     * @return Logical-pixel rotated/padded rectangle or empty when absent/not rendered. Not recomputed by this accessor.
     */
    QRectF renderedBounds(const QString &id) const { return m_bounds.value(id); }
    /**
     * @brief Inspect the most recently chosen layout displacement.
     * @param[in] id Exact annotation identity.
     * @return Logical-pixel chosen offset, or (0,0) for no cache entry; auto-layout placement does not mutate the item's stored offset.
     */
    QPointF renderedOffset(const QString &id) const { return m_offsets.value(id); }
    /**
     * @return Whether unpinned annotations participate in shared collision placement, initially false.
     */
    bool autoLayout() const { return m_autoLayout; }
    /**
     * @param[in] enabled New collision-layout flag.
     * @note Equality is a no-op; changes emit annotationsChanged() then repaintRequested() without changing any item's persisted position/offset.
     */
    void setAutoLayout(bool enabled);
    /**
     * @brief Serialize ordered annotation models without layer settings.
     * @return Owned item JSON array, including IDs/styles/references; no file write or paint occurs.
     */
    QJsonArray toJson() const;
    /**
     * @brief Replace all annotation models from permissive item JSON.
     * @param[in] items Array whose object entries become new owned AnnotationTextItems; nonobjects and duplicate IDs are skipped.
     * @note Deletes prior items and clears their cached bounds/offsets with layer signals blocked, then emits one annotationsChanged()/repaintRequested() pair subject to any outer signal block. Not atomic/strictly validated; no previous-state rollback is supplied.
     */
    void fromJson(const QJsonArray &items);
    /**
     * @brief Copy layer identity/display/CRS/auto-layout and ordered item state.
     * @return Owned JSON object; CRS is WKT or empty, no sources/files are resolved.
     */
    QJsonObject layerJson() const;
    /**
     * @param[in] object Permissive project JSON. Missing common fields use name Text, visible true, opacity one, autoLayout false, and an empty replacement item list.
     * @note Restores nonempty ID before references rebind; a nonempty different CRS is allocated/adopted, but an empty CRS token leaves the current CRS unchanged. Emits common setter notifications plus the final replacement item aggregate; no validation/atomic rollback or file IO occurs.
     */
    void restoreLayer(const QJsonObject &object);
    /**
     * @return Translated (in-memory) display text; annotations persist only when the host serializes them.
     */
    QString sourceDescription() const override { return tr("(in-memory)"); }
    /**
     * @return Owned single row describing stored item count, including items not currently painted.
     */
    QVector<QPair<QString, QString>> extendedMetadata() const override
    {
        return {{tr("Text items"), QString::number(m_items.size())}};
    }
    /**
     * @brief Leave all three borrowed scene/extent/CRS arguments unused.
     * @note Text is painted through paintLayers(), so this override creates no scene-owned items.
     */
    void populateScene(QGraphicsScene *, const MapExtent &, const SpatialReferenceSystem *) override
    {
    }
    /**
     * @brief Clear cached painted bounds/offsets without touching the unused scene argument.
     * @note Does not delete persistent annotations, clear the last transform or emit signals.
     */
    void depopulateScene(QGraphicsScene *) override
    {
        m_bounds.clear();
        m_offsets.clear();
    }
    /**
     * @brief No-op for the unused borrowed scene/extent/CRS arguments; shared canvas painting handles text refresh.
     */
    void refreshScene(QGraphicsScene *, const MapExtent &, const SpatialReferenceSystem *) override
    {
    }
    /**
     * @brief Paint all visible text layers with shared screen-space collision placement and linked-feature resolution.
     * @param[in,out] p Active GUI-thread viewport painter; per-label state is saved/restored.
     * @param[in] layers Borrowed stable top-level layers in stacking order. Used for text collections and linked source lookup; source lookup expects nonnull pointers.
     * @param[in] extent Valid positive canvas map extent; invalid input clears caches then returns without painting.
     * @param[in] srs Optional borrowed canvas CRS; missing CRS references assume compatible coordinates.
     * @param[in] viewport Positive logical-pixel viewport dimensions.
     * @note Clears every annotation layer's bounds/offset cache, resolves live feature anchors/expressions and reprojects where possible. Unresolved expression-linked text is omitted; free/static fallback text may remain at its stored anchor without a leader. Manual/pinned labels reserve their chosen rectangles first; higher-stacked auto labels get first collision-free candidates and unmatched candidates are hidden.
     * @note Placement changes only cached rendered offsets, not persisted item offsets/coordinates. Leaders paint behind all glyphs, and layer opacity replaces painter opacity per label. No result workers/scene items are created; stable source data and GUI-thread access are required.
     */
    static void paintLayers(QPainter &p, const QList<OpenSWMMVisLayer *> &layers,
                            const MapExtent &extent, const SpatialReferenceSystem *srs,
                            const QSize &viewport);
signals:
    /**
     * @brief Notify item-list, forwarded item-state, or auto-layout edits before repaintRequested().
     * @note Cached painted geometry updates on the next paintLayers() call, not synchronously at this signal.
     */
    void annotationsChanged();

private:
    QVector<AnnotationTextItem *> m_items;
    QHash<QString, QRectF> m_bounds;
    QHash<QString, QPointF> m_offsets;
    QTransform m_sceneToPixel;
    bool m_autoLayout = false;
};
#endif
