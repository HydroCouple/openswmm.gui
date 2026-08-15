/*!
 * \file   meshvertexpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
    Q_PROPERTY(int     index          READ index)
    Q_PROPERTY(double  x              READ x)
    Q_PROPERTY(double  y              READ y)
    Q_PROPERTY(double  z              READ z              WRITE setZ              NOTIFY changed)
    Q_PROPERTY(QString coupledNodeId  READ coupledNodeId  WRITE setCoupledNodeId  NOTIFY changed)
    Q_PROPERTY(QString tag            READ tag            WRITE setTag            NOTIFY changed)

public:
    MeshVertexPropertyAdapter(SWMM2DMeshLayer *layer, int vertexIdx,
                              QObject *parent = nullptr);

    [[nodiscard]] int     index()         const { return m_idx; }
    [[nodiscard]] double  x()             const;
    [[nodiscard]] double  y()             const;
    [[nodiscard]] double  z()             const;
    [[nodiscard]] QString coupledNodeId() const;
    [[nodiscard]] QString tag()           const;

    /*! \brief Same display-label convention as SWMM*PropertyAdapter. */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setZ(double v);
    void setCoupledNodeId(const QString &id);
    void setTag(const QString &tag);
    void refresh() { emit changed(); }

signals:
    void changed();

private slots:
    void onLayerAttributeChanged(const QString &refName);

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    int                       m_idx = -1;
    QString                   m_refName;  // cached MeshObjectRef name for filtering signals
};

#endif // OPENSWMMVIS_UI_PROPERTIES_MESHVERTEXPROPERTYADAPTER_H
