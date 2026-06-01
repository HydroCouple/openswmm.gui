/*!
 * \file   meshedgepropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice §V.VF — Q_PROPERTY-backed view of one 2D mesh edge.
 * Stateless wrapper around `SWMM2DMeshLayer` keyed by `(triIdx, edgeLocal)`.
 * Read/write surface for the per-edge boundary condition (Slice §V.VC).
 */
#ifndef OPENSWMMVIS_UI_PROPERTIES_MESHEDGEPROPERTYADAPTER_H
#define OPENSWMMVIS_UI_PROPERTIES_MESHEDGEPROPERTYADAPTER_H

#include "mesh/meshbctype.h"

#include <QObject>
#include <QPointer>
#include <QString>

class SWMM2DMeshLayer;

class MeshEdgePropertyAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int     triIdx       READ triIdx)
    Q_PROPERTY(int     edgeLocal    READ edgeLocal)
    Q_PROPERTY(bool    isBoundary   READ isBoundary)
    Q_PROPERTY(double  length       READ length)
    Q_PROPERTY(int     bcType       READ bcType        WRITE setBCType        NOTIFY changed)
    Q_PROPERTY(double  bcHead       READ bcHead        WRITE setBCHead        NOTIFY changed)
    Q_PROPERTY(double  bcSlope      READ bcSlope       WRITE setBCSlope       NOTIFY changed)
    Q_PROPERTY(double  bcFlow       READ bcFlow        WRITE setBCFlow        NOTIFY changed)
    Q_PROPERTY(QString bcTseries    READ bcTseries     WRITE setBCTseries     NOTIFY changed)
    Q_PROPERTY(QString bcCurve      READ bcCurve       WRITE setBCCurve       NOTIFY changed)
    Q_PROPERTY(QString bcGroup      READ bcGroup       WRITE setBCGroup       NOTIFY changed)
    Q_PROPERTY(double  bcConveyance READ bcConveyance  WRITE setBCConveyance  NOTIFY changed)

public:
    MeshEdgePropertyAdapter(SWMM2DMeshLayer *layer, int triIdx, int edgeLocal,
                            QObject *parent = nullptr);

    [[nodiscard]] int     triIdx()     const { return m_tri; }
    [[nodiscard]] int     edgeLocal()  const { return m_e; }
    [[nodiscard]] bool    isBoundary() const;
    [[nodiscard]] double  length()     const;
    [[nodiscard]] int     bcType()     const;
    [[nodiscard]] double  bcHead()     const;
    [[nodiscard]] double  bcSlope()    const;
    [[nodiscard]] double  bcFlow()     const;
    [[nodiscard]] QString bcTseries()  const;
    [[nodiscard]] QString bcCurve()    const;
    [[nodiscard]] QString bcGroup()    const;
    [[nodiscard]] double  bcConveyance() const;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setBCType(int newType);
    void setBCHead(double v);
    void setBCSlope(double v);
    void setBCFlow(double v);
    void setBCTseries(const QString &name);
    void setBCCurve(const QString &name);
    void setBCGroup(const QString &name);
    void setBCConveyance(double v);
    void refresh() { emit changed(); }

signals:
    void changed();

private slots:
    void onLayerAttributeChanged(const QString &refName);

private:
    /*! Mutate one field of the stored BC slot and write it back through
     *  `applyMeshEdgeBC`. Centralises the read-modify-write so each
     *  setter is one line. */
    template <typename F>
    void mutate(F &&fn);

    QPointer<SWMM2DMeshLayer> m_layer;
    int                       m_tri = -1;
    int                       m_e   = -1;
    QString                   m_refName;
};

#endif // OPENSWMMVIS_UI_PROPERTIES_MESHEDGEPROPERTYADAPTER_H
