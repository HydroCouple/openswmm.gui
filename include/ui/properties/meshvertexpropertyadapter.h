/**
 * @file      meshvertexpropertyadapter.h
 * @brief     Slice §V.VF — Q_PROPERTY-backed view of one 2D mesh vertex.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice §V.VF — Q_PROPERTY-backed view of one 2D mesh vertex.
 *
 * Stateless wrapper around `SWMM2DMeshLayer` keyed by vertex index. Every
 * READ calls back into the layer's `mesh()` data; every WRITE slot calls
 * the layer's `applyMesh*` helpers, which emit `attributeChanged` so all
 * other views (toolbar, attribute table, map renderer) stay in sync
 * (`feedback_mvc_synchronized_uis`).
 *
 * Slice §V.VE — `coupledNodeId` is the read/write surface for vertex-to-
 * SWMM-node coupling, stored in the vertex's `coupledNode` field and
 * round-tripped through INP via `[2D_VERTEX_NODE_MAP]`. `tag` is the
 * separate descriptive label (the `[2D_VERTICES]` TAG column).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PROPERTIES_MESHVERTEXPROPERTYADAPTER_H
#define OPENSWMMVIS_UI_PROPERTIES_MESHVERTEXPROPERTYADAPTER_H

#include <QObject>
#include <QPointer>
#include <QString>

class SWMM2DMeshLayer;

class MeshVertexPropertyAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int index READ index)
    Q_PROPERTY(double x READ x)
    Q_PROPERTY(double y READ y)
    Q_PROPERTY(double z READ z WRITE setZ NOTIFY changed)
    Q_PROPERTY(QString coupledNodeId READ coupledNodeId WRITE setCoupledNodeId NOTIFY changed)
    Q_PROPERTY(QString tag READ tag WRITE setTag NOTIFY changed)

public:
    /**
     * @brief Bind live mesh properties to one vertex without copying mesh storage.
     * @param[in] layer Borrowed mesh layer guarded by QPointer; nullptr or destroyed layer makes reads return their unavailable defaults.
     * @param[in] vertexIdx Zero-based mesh vertex index.
     * @param[in] parent Optional Qt owner; use this adapter on the GUI thread.
     * @details Subscribes to matching layer attribute notifications; a topology change requires callers to ensure the stored index still identifies the intended object.
     */
    MeshVertexPropertyAdapter(SWMM2DMeshLayer *layer, int vertexIdx, QObject *parent = nullptr);

    /**
     * @return Captured zero-based vertex index, without checking current mesh bounds.
     */
    [[nodiscard]] int index() const { return m_idx; }
    /**
     * @return Vertex x in mesh horizontal CRS units; NaN for missing layer or invalid index.
     */
    [[nodiscard]] double x() const;
    /**
     * @return Vertex y in mesh horizontal CRS units; NaN for missing layer or invalid index.
     */
    [[nodiscard]] double y() const;
    /**
     * @return Authored elevation in mesh vertical units; NaN for missing layer or invalid index.
     */
    [[nodiscard]] double z() const;
    /**
     * @return Copied SWMM node ID for [2D_VERTEX_NODE_MAP], or empty when unassigned/unavailable.
     */
    [[nodiscard]] QString coupledNodeId() const;
    /**
     * @return Descriptive vertex tag, independent of node coupling, or empty when unavailable.
     */
    [[nodiscard]] QString tag() const;

    /*! \brief Same display-label convention as SWMM*PropertyAdapter.
     * @brief Build a translated label for a mesh property.
     * @param[in] property Exact Q_PROPERTY key.
     * @return Known label (including configured depth units for cell lengths), or the unchanged key when unrecognized.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /**
     * @brief Apply a vertex elevation through the shared layer geometry-update path.
     * @param[in] v New elevation in mesh vertical units; no local range/finiteness check is performed.
     * @details Missing layer is a no-op; layer return status is not exposed. A changed valid vertex rebuilds affected elevation geometry and refreshes through layer signals.
     */
    void setZ(double v);
    /**
     * @brief Assign a vertex's coupled SWMM node through the mesh layer.
     * @param[in] id Node ID, or empty to clear coupling; the adapter passes text unchanged.
     * @details Layer validates/applies the change and supplies notifications; failures return silently.
     */
    void setCoupledNodeId(const QString &id);
    /**
     * @brief Assign the vertex's descriptive mesh tag.
     * @param[in] tag Literal tag text passed to the layer.
     * @details Missing layer is a no-op; successful changes refresh via the layer attribute signal, without a duplicate adapter emit.
     */
    void setTag(const QString &tag);
    /**
     * @brief Request unconditional rereading of every property by emitting changed; does not mutate the mesh.
     */
    void refresh() { emit changed(); }

signals:
    /**
     * @brief Notify the property model to reread mesh values after refresh or a relevant layer edit.
     */
    void changed();

private slots:
    /**
     * @brief Forward only notifications identifying this adapter's mesh object.
     * @param[in] refName MeshObjectRef name supplied by the layer; exact equality with the captured reference emits changed.
     */
    void onLayerAttributeChanged(const QString &refName);

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    int m_idx = -1;
    QString m_refName; // cached MeshObjectRef name for filtering signals
};

#endif // OPENSWMMVIS_UI_PROPERTIES_MESHVERTEXPROPERTYADAPTER_H
