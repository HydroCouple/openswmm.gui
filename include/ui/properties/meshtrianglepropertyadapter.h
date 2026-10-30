/**
 * @file      meshtrianglepropertyadapter.h
 * @brief     Q_PROPERTY-backed view of one 2D mesh triangle (cell).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Q_PROPERTY-backed view of one 2D mesh triangle (cell). Mirrors
 * MeshVertexPropertyAdapter / MeshEdgePropertyAdapter: every READ calls into
 * the layer's `mesh()`; every WRITE calls an `applyMeshTriangle*` helper,
 * which emits `attributeChanged` so all views stay in sync. Exposes the
 * editable per-triangle attributes — Manning's roughness, the initial water
 * depth (INIT_DEPTH, mesh length units, default 0 = dry), and the descriptive
 * tag — all round-tripped through `[2D_TRIANGLES]`.
 *
 * Numeric writes route through mesh::pushCellParamEdit when a canvas is set,
 * so a panel edit lands on the same undo stack as a toolbar or Cell Data
 * edit.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PROPERTIES_MESHTRIANGLEPROPERTYADAPTER_H
#define OPENSWMMVIS_UI_PROPERTIES_MESHTRIANGLEPROPERTYADAPTER_H

#include "map/mapcanvas.h" // QPointer<MapCanvas> needs the complete type

#include <QObject>
#include <QPointer>
#include <QString>

class SWMM2DMeshLayer;

class MeshTrianglePropertyAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int index READ index)
    Q_PROPERTY(int v0 READ v0)
    Q_PROPERTY(int v1 READ v1)
    Q_PROPERTY(int v2 READ v2)
    Q_PROPERTY(double mannings READ mannings WRITE setMannings NOTIFY changed)
    Q_PROPERTY(double initDepth READ initDepth WRITE setInitDepth NOTIFY changed)
    Q_PROPERTY(QString tag READ tag WRITE setTag NOTIFY changed)

public:
    /**
     * @brief Bind live mesh properties to one cell without copying mesh storage.
     * @param[in] layer Borrowed mesh layer guarded by QPointer; nullptr or destroyed layer makes reads return their unavailable defaults.
     * @param[in] triIdx Zero-based mesh cell index.
     * @param[in] parent Optional Qt owner; use this adapter on the GUI thread.
     * @details Subscribes to matching layer attribute notifications; a topology change requires callers to ensure the stored index still identifies the intended object.
     */
    MeshTrianglePropertyAdapter(SWMM2DMeshLayer *layer, int triIdx, QObject *parent = nullptr);

    /**
     * @return Captured zero-based cell index, without current bounds validation.
     */
    [[nodiscard]] int index() const { return m_idx; }
    /**
     * @return First zero-based mesh vertex index, or -1 for an unavailable cell.
     */
    [[nodiscard]] int v0() const;
    /**
     * @return Second zero-based mesh vertex index, or -1 for an unavailable cell.
     */
    [[nodiscard]] int v1() const;
    /**
     * @return Third zero-based mesh vertex index, or -1 for an unavailable cell; quadrilateral fourth corners are not exposed by this property.
     */
    [[nodiscard]] int v2() const;
    /**
     * @return Authored surface Manning roughness coefficient, or NaN when the cell is unavailable.
     */
    [[nodiscard]] double mannings() const;
    /**
     * @return Initial water depth in mesh length units; an unset/nonfinite stored depth reads as zero (dry), while an unavailable cell reads as NaN.
     */
    [[nodiscard]] double initDepth() const;
    /**
     * @return Authored descriptive/region tag, or empty for an unavailable cell.
     */
    [[nodiscard]] QString tag() const;
    Q_PROPERTY(QString infiltrationOwner READ infiltrationOwner NOTIFY changed)
    Q_PROPERTY(QString infiltrationSource READ infiltrationSource NOTIFY changed)
    Q_PROPERTY(QString infiltrationConflict READ infiltrationConflict NOTIFY changed)
    Q_PROPERTY(QString surfaceOwnershipReview READ surfaceOwnershipReview NOTIFY changed)
    Q_PROPERTY(QString remainingMeshWeather READ remainingMeshWeather NOTIFY changed)
    Q_PROPERTY(QString spatialSourcePervious READ spatialSourcePervious NOTIFY changed)
    /**
     * @brief Read the surface-area ownership review enum label.
     * @return Cached review value or the translated Unavailable label when the corresponding layer/cell data is absent or nonfinite; no live engine preview is run.
     */
    QString surfaceOwnershipReview() const;
    /**
     * @brief Read the remaining mesh-weather area share as an eight-significant-digit percentage.
     * @return Cached review value or the translated Unavailable label when the corresponding layer/cell data is absent or nonfinite; no live engine preview is run.
     */
    QString remainingMeshWeather() const;
    /**
     * @brief Read the spatial-source pervious area share as an eight-significant-digit percentage.
     * @return Cached review value or the translated Unavailable label when the corresponding layer/cell data is absent or nonfinite; no live engine preview is run.
     */
    QString spatialSourcePervious() const;
    /**
     * @brief Read the effective infiltration owner enum label.
     * @return Cached review value or the translated Unavailable label when the corresponding layer/cell data is absent or nonfinite; no live engine preview is run.
     */
    QString infiltrationOwner() const;
    /**
     * @brief Read the cached explanation identifying the authored infiltration/groundwater owner source.
     * @return Cached review value or the translated Unavailable label when the corresponding layer/cell data is absent or nonfinite; no live engine preview is run.
     */
    QString infiltrationSource() const;
    /**
     * @brief Read the effective infiltration conflict enum label.
     * @return Cached review value or the translated Unavailable label when the corresponding layer/cell data is absent or nonfinite; no live engine preview is run.
     */
    QString infiltrationConflict() const;

    /*! \brief Same display-label convention as SWMM*PropertyAdapter. Length
     *         properties carry the project's depth unit (see
     *         setDepthUnitLabel).
     * @brief Build a translated label for a mesh property.
     * @param[in] property Exact Q_PROPERTY key.
     * @return Known label (including configured depth units for cell lengths), or the unchanged key when unrecognized.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

    /*! \brief Canvas whose undo stack numeric edits are pushed onto. Without
     *         one the edits still apply, just unundoably.
     * @param[in] canvas Borrowed guarded canvas whose undo stack receives numeric cell edits; nullptr applies them without an undo stack.
     */
    void setCanvas(MapCanvas *canvas) { m_canvas = canvas; }

    /*! \brief Project depth unit ("ft" / "m") shown in length labels;
     *         defaults to "m".
     * @param[in] label Display-only depth unit label (normally ft or m), copied verbatim; does not convert stored values or emit changed.
     */
    void setDepthUnitLabel(const QString &label) { m_depthUnit = label; }

public slots:
    /**
     * @brief Submit a shared cell-parameter edit for surface roughness.
     * @param[in] n Manning roughness value in the cell-parameter domain (editor metadata [0.001,1]).
     * @details Routes validation and undo through pushCellParamEdit using the configured canvas; missing layer or a rejected edit yields no direct status. Refresh comes from layer signals.
     */
    void setMannings(double n);
    /**
     * @brief Submit a shared cell-parameter edit for initial ponded depth.
     * @param[in] d Depth in mesh length units, nonnegative (editor metadata [0,1000]); zero starts dry.
     * @details Routes validation/undo through pushCellParamEdit; missing layer is a no-op and no direct error status is returned.
     */
    void setInitDepth(double d);
    /**
     * @brief Apply a descriptive/region cell tag through the layer.
     * @param[in] tag Literal text, with empty clearing the stored tag.
     * @details Layer attribute notifications refresh the property model; missing layer is a no-op.
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
    QPointer<MapCanvas> m_canvas;
    int m_idx = -1;
    QString m_refName; // cached MeshObjectRef::cell name
    QString m_depthUnit = QStringLiteral("m");
};

#endif // OPENSWMMVIS_UI_PROPERTIES_MESHTRIANGLEPROPERTYADAPTER_H
