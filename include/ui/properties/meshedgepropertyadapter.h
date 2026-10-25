/**
 * @file      meshedgepropertyadapter.h
 * @brief     Slice §V.VF — Q_PROPERTY-backed view of one 2D mesh edge.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice §V.VF — Q_PROPERTY-backed view of one 2D mesh edge.
 * Stateless wrapper around `SWMM2DMeshLayer` keyed by `(triIdx, edgeLocal)`.
 * Read/write surface for the per-edge boundary condition (Slice §V.VC).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    Q_PROPERTY(int triIdx READ triIdx)
    Q_PROPERTY(int edgeLocal READ edgeLocal)
    Q_PROPERTY(bool isBoundary READ isBoundary)
    Q_PROPERTY(double length READ length)
    Q_PROPERTY(int bcType READ bcType WRITE setBCType NOTIFY changed)
    Q_PROPERTY(double bcHead READ bcHead WRITE setBCHead NOTIFY changed)
    Q_PROPERTY(double wallDrag READ wallDrag WRITE setWallDrag NOTIFY changed)
    Q_PROPERTY(double bcSlope READ bcSlope WRITE setBCSlope NOTIFY changed)
    Q_PROPERTY(double bcFlow READ bcFlow WRITE setBCFlow NOTIFY changed)
    Q_PROPERTY(QString bcTseries READ bcTseries WRITE setBCTseries NOTIFY changed)
    Q_PROPERTY(QString bcCurve READ bcCurve WRITE setBCCurve NOTIFY changed)
    Q_PROPERTY(QString bcGroup READ bcGroup WRITE setBCGroup NOTIFY changed)
    Q_PROPERTY(double bcConveyance READ bcConveyance WRITE setBCConveyance NOTIFY changed)

public:
    /**
     * @brief Bind live mesh properties to one edge without copying mesh storage.
     * @param[in] layer Borrowed mesh layer guarded by QPointer; nullptr or destroyed layer makes reads return their unavailable defaults.
     * @param[in] triIdx Zero-based mesh cell index.
     * @param[in] edgeLocal Zero-based local edge ordinal, 0..2 for triangles or 0..3 for quadrilaterals.
     * @param[in] parent Optional Qt owner; use this adapter on the GUI thread.
     * @details Subscribes to matching layer attribute notifications; a topology change requires callers to ensure the stored index still identifies the intended object.
     */
    MeshEdgePropertyAdapter(SWMM2DMeshLayer *layer, int triIdx, int edgeLocal,
                            QObject *parent = nullptr);

    /**
     * @return Captured zero-based owning cell index without bounds validation.
     */
    [[nodiscard]] int triIdx() const { return m_tri; }
    /**
     * @return Captured local edge ordinal without bounds validation.
     */
    [[nodiscard]] int edgeLocal() const { return m_e; }
    /**
     * @return Layer boundary classification; false without a layer or a valid boundary edge.
     */
    [[nodiscard]] bool isBoundary() const;
    /**
     * @return Euclidean edge length in mesh horizontal CRS units; NaN for missing/invalid cell, edge or endpoint indexes.
     */
    [[nodiscard]] double length() const;
    /**
     * @return GUI MeshBCTypes::Type selector (0..6), with constant/series variants distinct; missing storage reads as Wall.
     */
    [[nodiscard]] int bcType() const;
    /**
     * @return Constant boundary stage in project vertical units; missing storage reads as zero.
     */
    [[nodiscard]] double bcHead() const;
    /**
     * @return Dimensionless normal-flow friction slope; missing storage reads as zero (engine treats zero slope as a wall).
     */
    [[nodiscard]] double bcSlope() const;
    [[nodiscard]] double wallDrag() const;
    void setWallDrag(double v);
    /**
     * @return Constant prescribed discharge per metre of edge in project flow units per metre; missing storage reads as zero.
     */
    [[nodiscard]] double bcFlow() const;
    /**
     * @return Boundary time-series ID, or empty when unassigned/missing.
     */
    [[nodiscard]] QString bcTseries() const;
    /**
     * @return Rating-curve ID, or empty when unassigned/missing.
     */
    [[nodiscard]] QString bcCurve() const;
    /**
     * @return Optional named boundary group, or empty when absent.
     */
    [[nodiscard]] QString bcGroup() const;
    /**
     * @return Edge flux attenuation fraction ψ (normally [0,1]); absent BC storage returns its default 1.
     */
    [[nodiscard]] double bcConveyance() const;

    /**
     * @brief Build a translated label for a mesh property.
     * @param[in] property Exact Q_PROPERTY key.
     * @return Known label (including configured depth units for cell lengths), or the unchanged key when unrecognized.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /**
     * @brief Change the GUI boundary-condition type while preserving other BC fields.
     * @param[in] newType MeshBCTypes::Type integer in [0,6]; no enum-range check occurs in this adapter.
     * @details Delegates through mutate(); no engine conversion or failure status is returned here.
     */
    void setBCType(int newType);
    /**
     * @brief Change the constant prescribed boundary stage.
     * @param[in] v Stage in project vertical units, passed unchanged without local finiteness/range checks.
     * @details Preserves other BC fields and refreshes through layer notifications.
     */
    void setBCHead(double v);
    /**
     * @brief Change the normal-flow friction slope.
     * @param[in] v Dimensionless slope; positive gives normal flow, zero is interpreted as a wall by the engine. No local bounds check occurs.
     */
    void setBCSlope(double v);
    /**
     * @brief Change the constant prescribed discharge per metre of boundary edge.
     * @param[in] v Discharge in project flow units per metre, passed unchanged without local validation.
     */
    void setBCFlow(double v);
    /**
     * @brief Store a time-series reference for a series-driven boundary.
     * @param[in] name Literal time-series ID; empty clears the reference. Existence is not checked locally.
     */
    void setBCTseries(const QString &name);
    /**
     * @brief Store a stage-discharge rating-curve reference.
     * @param[in] name Literal curve ID; empty clears the reference. Existence is not checked locally.
     */
    void setBCCurve(const QString &name);
    /**
     * @brief Set the optional grouping label for this edge boundary.
     * @param[in] name Literal group text; empty clears it.
     */
    void setBCGroup(const QString &name);
    /**
     * @brief Change flux attenuation on this edge and its interior neighbor slot.
     * @param[in] v Dimensionless ψ in [0,1]; layer rejects out-of-range/nonfinite values.
     * @details Uses the dedicated layer helper to mirror interior edges; no direct error result is returned.
     */
    void setBCConveyance(double v);
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
    /*! Mutate one field of the stored BC slot and write it back through
     *  `applyMeshEdgeBC`. Centralises the read-modify-write so each
     *  setter is one line.
     * @brief Read-modify-write one complete edge BC slot through the shared layer.
     * @tparam F Synchronous callable accepting MeshEdgeBC&.
     * @param[in] fn Modifies a local value copy; other fields survive unchanged.
     * @details Missing layer is a no-op. applyMeshEdgeBC return status is not exposed; updates refresh via layer signals.
     */
    template <typename F> void mutate(F &&fn);

    QPointer<SWMM2DMeshLayer> m_layer;
    int m_tri = -1;
    int m_e = -1;
    QString m_refName;
};

#endif // OPENSWMMVIS_UI_PROPERTIES_MESHEDGEPROPERTYADAPTER_H
