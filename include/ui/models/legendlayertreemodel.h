/**
 * @file      legendlayertreemodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BB Phase 8.6.16 — QAbstractItemModel exposing a MapCanvas's
 *         layers as a two-level tree (Layer → LegendSymbolItem) for the
 *         LegendPropertiesDialog Layers tab.
 *
 *         The model is a thin view over the canvas's live layer list +
 *         each layer's IFeatureRenderer::legendSymbolItems(). It does
 *         not own state; per-class colour edits write through to the
 *         renderer via SetRendererClassColorCommand on the canvas's
 *         MapUndoStack, and the model resets in response to the layer's
 *         repaintRequested signal so external edits show up live.
 *
 *         Columns:
 *           Col 0 — "Item"   (layer name at level 0, item label at level 1)
 *           Col 1 — "Color"  (swatch + opens QColorDialog when editable)
 *
 *         Layer-header rows are not editable. Item rows are editable in
 *         the Color column iff the parent layer's renderer reports
 *         supportsClassEdit(Color).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_MODELS_LEGENDLAYERTREEMODEL_H
#define OPENSWMMVIS_UI_MODELS_LEGENDLAYERTREEMODEL_H

#include <QAbstractItemModel>
#include <QColor>
#include <QList>
#include <QPointer>
#include <QString>
#include <QVector>

class MapCanvas;
class OpenSWMMVisLayer;

namespace OpenSWMM::Render
{
class LegendOverlayStyle;
}

namespace openswmmvis::ui
{

class LegendLayerTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column {
        ColItem = 0,
        ColColor = 1,
        ColSize = 2, /*!< Slice BB Phase 8.6.16 — per-class marker / line size. */
        ColCount
    };

    /*! \brief Roles used by LegendColorDelegate for direct access to the
     *         classKey and the layer pointer without going through
     *         internalPointer(). */
    enum CustomRole {
        ClassKeyRole = Qt::UserRole + 1,
        LayerPtrRole,  /*!< OpenSWMMVisLayer* */
        EditableRole,  /*!< bool — true iff renderer
                                               supports class-edit Color. */
        SublayerIdRole /*!< Slice S4 P5 — QString;
                                               non-empty iff this row was
                                               contributed by a sublayer
                                               (LegendSymbolItem::sublayerId).
                                               Used by the LegendDock right-
                                               click handler to route to the
                                               originating sublayer's
                                               style dialog. */
    };

    /**
     * @brief Cache the canvas legend as layer headers and symbol children, subscribing to live changes.
     * @param[in] canvas Borrowed canvas tracked by QPointer; null creates an empty tree.
     * @param[in] parent Optional QObject owner; use on the GUI thread with the canvas/renderers.
     */
    explicit LegendLayerTreeModel(MapCanvas *canvas, QObject *parent = nullptr);
    /**
     * @brief Disconnect tracked layer/sublayer signals without deleting their borrowed objects.
     */
    ~LegendLayerTreeModel() override;

    /*! \brief Optional per-item override store. When set, the model reads
     *         visible / userLabel from the style and routes setData
     *         (CheckStateRole / EditRole) back through it.
     * @param[in] style Borrowed override store tracked by QPointer; null disables per-item label/visibility editing.
     * @note Replaces subscriptions and resets the tree when the pointer changes. Existing overrides are read, never copied into this model as independent state.
     */
    void setOverlayStyle(OpenSWMM::Render::LegendOverlayStyle *style);

    // QAbstractItemModel ─────────────────────────────────────────────
    /**
     * @brief Expose Item, Color and Size columns at every tree level.
     * @param[in] parent Ignored; even leaf indexes report ColCount.
     * @return Three columns.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Count root layer headers or one layer's cached legend-symbol children.
     * @param[in] parent Invalid for root, otherwise a current layer-header index from this model.
     * @return Layer count at root, item count below a valid header, zero below symbols/invalid parent rows.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Build a current layer or symbol index using an encoded parent layer identity.
     * @param[in] row Zero-based sibling row.
     * @param[in] column Column 0–2.
     * @param[in] parent Invalid for a layer header, or a valid current header index for symbol children.
     * @return Model index, or invalid for negative/out-of-range coordinates or grandchildren. Indexes are invalidated by model resets.
     */
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    /**
     * @brief Resolve a symbol index back to its layer header in column zero.
     * @param[in] child Current index from this model.
     * @return Invalid for root headers/invalid child identities, otherwise the parent header index.
     */
    QModelIndex parent(const QModelIndex &child) const override;
    /**
     * @brief Read cached legend labels, class colours/sizes, override visibility and routing identities.
     * @param[in] idx Current model index.
     * @param[in] role Qt display/edit/check/decoration or custom ClassKey/LayerPtr/Editable/SublayerId roles.
     * @return Requested value or invalid for unavailable/unsupported cells. LayerPtrRole is borrowed; sizes retain the renderer's class-size convention, without conversion.
     * @note Header rows expose only name, layer pointer and false editability. Label/checkbox overrides are available on keyed symbol rows when an overlay style is bound.
     */
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    /**
     * @brief Route legend-item edits to the shared overlay style or project renderer undo commands.
     * @param[in] idx Existing keyed symbol cell; headers are rejected.
     * @param[in] value CheckStateRole uses Checked for visible; Item edits use label text; Color uses a valid QColor; Size uses a convertible value greater than zero.
     * @param[in] role CheckStateRole for Item visibility, or EditRole for label/colour/size.
     * @return True for an accepted route, false for unsupported/invalid cells or values.
     * @note Visibility/label edits write the override store directly. Colour/size edits push onto the canvas undo stack when available; without a stack they still return true and emit dataChanged without changing the renderer. Resets may occur synchronously through shared-object signals.
     */
    bool setData(const QModelIndex &idx, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @brief Advertise class edits supported by the cached renderer and optional overlay style.
     * @param[in] idx Current model index.
     * @return Enabled/selectable for valid rows; colour and positive cached size may be editable, and keyed Item cells gain label editing/checkboxes when style-bound. Invalid indexes have no flags.
     */
    Qt::ItemFlags flags(const QModelIndex &idx) const override;
    /**
     * @brief Supply the three horizontal legend editor headings.
     * @param[in] section Item=0, Color=1 or Size=2.
     * @param[in] orientation Only Horizontal is supported.
     * @param[in] role Only DisplayRole is supported.
     * @return Translated heading, or invalid QVariant for other requests.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private slots:
    /**
     * @brief Reset/rebuild cached layers and subscriptions after canvas insertion/removal/reordering.
     */
    void onCanvasLayersChanged(); // layerAdded/Removed/OrderChanged
    /**
     * @brief Reset/rebuild the complete legend cache after layer, renderer, sublayer or override changes.
     */
    void onLayerRepaintRequested(); // any layer's renderer state shifted

private:
    // Internal nodes: layer headers (top-level) + item rows (one per
    // LegendSymbolItem inside a layer). Stored flat for fast lookup;
    // index parent/child computed via internalId.
    struct ItemRow {
        QString classKey;
        QString label;
        QColor color;
        bool visible = true;
        qreal size = -1.0;  /*!< Negative = renderer has no per-class size. */
        QString sublayerId; /*!< Slice S4 P5 — populated from
                                      LegendSymbolItem::sublayerId; non-empty
                                      for sublayer-contributed rows. */
    };
    struct LayerNode {
        QPointer<OpenSWMMVisLayer> layer;
        QString name;
        QVector<ItemRow> items;
        bool editableColor = false; /*!< renderer supports Color. */
        bool editableSize = false;  /*!< renderer supports Size. */
    };

    /**
     * @brief Rebuild borrowed layer/symbol snapshots in reverse canvas paint order and reconnect invalidation sources.
     * @note Keeps hidden symbol rows so users can re-enable them; caller surrounds runtime refreshes with a model reset. No canvas yields an empty cache.
     */
    void rebuildLayerCache();
    /**
     * @brief Subscribe to a layer's display, renderer and current sublayer invalidation signals.
     * @param[in] layer Borrowed layer tracked in QPointers for later disconnection; null is ignored. Unique connections avoid duplicate slot subscriptions.
     */
    void connectLayer(OpenSWMMVisLayer *layer);
    /**
     * @brief Disconnect tracked layer/sublayer-to-model subscriptions and clear the tracked pointer lists.
     * @note Does not disconnect the canvas or overlay-style subscriptions and never deletes any layer.
     */
    void disconnectAllLayers();

    QPointer<MapCanvas> m_canvas;
    QPointer<OpenSWMM::Render::LegendOverlayStyle> m_style;
    QVector<LayerNode> m_layers;
    QList<QPointer<OpenSWMMVisLayer>> m_connectedLayers;
    // VS.9 — sublayer-host layers route each sublayer's invalidated()
    // signal here so style/visibility edits on a sublayer refresh the
    // legend live. Tracked as QObject* (ISublayer derives QObject) so the
    // header needs no isublayer.h include; disconnected on every rebuild.
    QList<QPointer<QObject>> m_connectedSublayers;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_LEGENDLAYERTREEMODEL_H
