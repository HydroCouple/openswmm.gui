/*!
 * \file   meshtrianglepropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Q_PROPERTY-backed view of one 2D mesh triangle (cell). Mirrors
 * MeshVertexPropertyAdapter / MeshEdgePropertyAdapter: every READ calls into
 * the layer's `mesh()`; every WRITE calls an `applyMeshTriangle*` helper,
 * which emits `attributeChanged` so all views stay in sync. Exposes the
 * editable per-triangle attributes — Manning's roughness, the initial water
 * depth (INIT_DEPTH, m, default 0 = dry), and the descriptive tag — all
 * round-tripped through `[2D_TRIANGLES]`.
 */
#ifndef OPENSWMMVIS_UI_PROPERTIES_MESHTRIANGLEPROPERTYADAPTER_H
#define OPENSWMMVIS_UI_PROPERTIES_MESHTRIANGLEPROPERTYADAPTER_H

#include <QObject>
#include <QPointer>
#include <QString>

class SWMM2DMeshLayer;

class MeshTrianglePropertyAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int     index    READ index)
    Q_PROPERTY(int     v0       READ v0)
    Q_PROPERTY(int     v1       READ v1)
    Q_PROPERTY(int     v2       READ v2)
    Q_PROPERTY(double  mannings READ mannings WRITE setMannings NOTIFY changed)
    Q_PROPERTY(double  initDepth READ initDepth WRITE setInitDepth NOTIFY changed)
    Q_PROPERTY(QString tag      READ tag      WRITE setTag      NOTIFY changed)

public:
    MeshTrianglePropertyAdapter(SWMM2DMeshLayer *layer, int triIdx,
                                QObject *parent = nullptr);

    [[nodiscard]] int     index()    const { return m_idx; }
    [[nodiscard]] int     v0()       const;
    [[nodiscard]] int     v1()       const;
    [[nodiscard]] int     v2()       const;
    [[nodiscard]] double  mannings() const;
    [[nodiscard]] double  initDepth() const;
    [[nodiscard]] QString tag()      const;

    /*! \brief Same display-label convention as SWMM*PropertyAdapter. */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setMannings(double n);
    void setInitDepth(double d);
    void setTag(const QString &tag);
    void refresh() { emit changed(); }

signals:
    void changed();

private slots:
    void onLayerAttributeChanged(const QString &refName);

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    int                       m_idx = -1;
    QString                   m_refName;  // cached MeshObjectRef::cell name
};

#endif // OPENSWMMVIS_UI_PROPERTIES_MESHTRIANGLEPROPERTYADAPTER_H
