/*!
 * \file   legendlayertreemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BB Phase 8.6.16 — QAbstractItemModel exposing a MapCanvas's
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

namespace OpenSWMM::Render { class LegendOverlayStyle; }

namespace openswmmvis::ui {

class LegendLayerTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column {
        ColItem  = 0,
        ColColor = 1,
        ColSize  = 2,   /*!< Slice BB Phase 8.6.16 — per-class marker / line size. */
        ColCount
    };

    /*! \brief Roles used by LegendColorDelegate for direct access to the
     *         classKey and the layer pointer without going through
     *         internalPointer(). */
    enum CustomRole {
        ClassKeyRole = Qt::UserRole + 1,
        LayerPtrRole,                     /*!< OpenSWMMVisLayer* */
        EditableRole,                     /*!< bool — true iff renderer
                                               supports class-edit Color. */
        SublayerIdRole                    /*!< Slice S4 P5 — QString;
                                               non-empty iff this row was
                                               contributed by a sublayer
                                               (LegendSymbolItem::sublayerId).
                                               Used by the LegendDock right-
                                               click handler to route to the
                                               originating sublayer's
                                               style dialog. */
    };

    explicit LegendLayerTreeModel(MapCanvas *canvas, QObject *parent = nullptr);
    ~LegendLayerTreeModel() override;

    /*! \brief Optional per-item override store. When set, the model reads
     *         visible / userLabel from the style and routes setData
     *         (CheckStateRole / EditRole) back through it. */
    void setOverlayStyle(OpenSWMM::Render::LegendOverlayStyle *style);

    // QAbstractItemModel ─────────────────────────────────────────────
    int columnCount(const QModelIndex &parent = {}) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &idx, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &idx) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private slots:
    void onCanvasLayersChanged();   // layerAdded/Removed/OrderChanged
    void onLayerRepaintRequested(); // any layer's renderer state shifted

private:
    // Internal nodes: layer headers (top-level) + item rows (one per
    // LegendSymbolItem inside a layer). Stored flat for fast lookup;
    // index parent/child computed via internalId.
    struct ItemRow {
        QString classKey;
        QString label;
        QColor  color;
        bool    visible = true;
        qreal   size    = -1.0;  /*!< Negative = renderer has no per-class size. */
        QString sublayerId;      /*!< Slice S4 P5 — populated from
                                      LegendSymbolItem::sublayerId; non-empty
                                      for sublayer-contributed rows. */
    };
    struct LayerNode {
        QPointer<OpenSWMMVisLayer> layer;
        QString          name;
        QVector<ItemRow> items;
        bool             editableColor = false;  /*!< renderer supports Color. */
        bool             editableSize  = false;  /*!< renderer supports Size. */
    };

    void rebuildLayerCache();
    void connectLayer(OpenSWMMVisLayer *layer);
    void disconnectAllLayers();

    QPointer<MapCanvas>                       m_canvas;
    QPointer<OpenSWMM::Render::LegendOverlayStyle> m_style;
    QVector<LayerNode>                        m_layers;
    QList<QPointer<OpenSWMMVisLayer>>         m_connectedLayers;
    // VS.9 — sublayer-host layers route each sublayer's invalidated()
    // signal here so style/visibility edits on a sublayer refresh the
    // legend live. Tracked as QObject* (ISublayer derives QObject) so the
    // header needs no isublayer.h include; disconnected on every rebuild.
    QList<QPointer<QObject>>                  m_connectedSublayers;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_LEGENDLAYERTREEMODEL_H
