/**
 * @file      lidcontrolprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     MVC model for a single SWMM LID control ([LID_CONTROLS]).
 *
 * Owned by a project-scoped LidControlRegistry. Engine I/O walks `swmm_lid_*`
 * from openswmm_infrastructure.h. Standard controls expose scalar layer fields;
 * NODE controls (type 8) use an ordered layer stack, optional Richards flow,
 * and per-layer pollutant treatment. Values remain in authoring input units.
 * The registry loads current engine values, then writes only new or dirty
 * controls. Direct writes to public flowOptions/retention/treatments do not
 * mark dirty; use setNodeFlow()/setNodeLayers() when notifying an edit.
 *
 * Type codes: 0=BIO_CELL, 1=RAIN_GARDEN, 2=GREEN_ROOF, 3=INFIL_TRENCH,
 * 4=PERM_PAVEMENT, 5=RAIN_BARREL, 6=ROOFTOP_DISCONN, 7=VEGETATIVE_SWALE,
 * 8=NODE.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_LID_LIDCONTROLPROVIDER_H
#define OPENSWMMVIS_LID_LIDCONTROLPROVIDER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <openswmm/engine/openswmm_infrastructure.h>

namespace openswmmvis::lid
{

struct LidLayerTreatment {
    int layer = 1;
    QString pollutant;
    double removal = 0.0, decay = 0.0;
    QString expression;
};
class LidControlProvider : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an engine-independent authoring value model.
     * @param[in] name Initial identifier copied verbatim; this constructor does not enforce registry uniqueness.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit LidControlProvider(QString name, QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources.
     */
    ~LidControlProvider() override;

    /**
     * @brief Read the current authoring identifier.
     * @return Copied name; use registry rename() for uniqueness and engine reference synchronization.
     */
    QString name() const noexcept { return m_name; }
    /**
     * @brief Read the local LID formulation code.
     * @return Standard type 0..7 or ordered NODE type 8; the provider does not validate assigned codes.
     */
    int type() const noexcept { return m_type; } ///< 0..8

    /**
     * @brief Inspect the ordered NODE layer stack.
     * @return Borrowed layer vector, invalidated by replacement or provider destruction; parameter slots/units follow SWMM_LidNodeLayerKind.
     */
    const QVector<SWMM_LidNodeLayer> &nodeLayers() const { return m_nodeLayers; }
    /**
     * @brief Replace the NODE stack, mark it dirty, and emit paramsChanged() unconditionally.
     * @param[in] layers Owning ordered layer records: thickness/suction in inches or mm, conductivity in inches/hour or mm/hour, and fractional moisture parameters; unused slots must be zero.
     * @details No stack validity or compatibility checks occur until the engine configure operation.
     */
    void setNodeLayers(QVector<SWMM_LidNodeLayer> layers);
    SWMM_LidRichardsOptions flowOptions{0, 8, 1.e-7, 1.e-5, 30.0};
    QVector<SWMM_LidRichardsMaterial> retention;
    /**
     * @brief Replace the NODE flow formulation/material metadata and notify an edit.
     * @param[in] options Richards model, cell resolution, water-content absolute tolerance, relative tolerance, and maximum step in seconds.
     * @param[in] materials Per-layer retention records; alpha and specific storage use inverse metres in both unit systems.
     * @details Always marks dirty and emits paramsChanged(). Engine validation occurs on save; a material vector whose count differs from nodeLayers() is not passed by the registry.
     */
    void setNodeFlow(SWMM_LidRichardsOptions options, QVector<SWMM_LidRichardsMaterial> materials);
    QVector<LidLayerTreatment> treatments;

    // Surface layer.
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Surface depression-storage depth, inches for US inputs or mm for SI inputs.
     */
    double surfStorage() const noexcept { return m_surfStorage; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Surface Manning roughness n.
     */
    double surfRoughness() const noexcept { return m_surfRoughness; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Surface slope in percent.
     */
    double surfSlope() const noexcept { return m_surfSlope; }
    // Soil layer.
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Soil layer thickness in inches or mm.
     */
    double soilThick() const noexcept { return m_soilThick; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Soil void/total volume fraction, expected above field capacity and no greater than one.
     */
    double soilPorosity() const noexcept { return m_soilPorosity; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Field-capacity water-content fraction, expected above wilting point and below porosity.
     */
    double soilFc() const noexcept { return m_soilFc; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Wilting-point water-content fraction, expected nonnegative and below field capacity.
     */
    double soilWp() const noexcept { return m_soilWp; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Saturated soil conductivity in inches/hour or mm/hour.
     */
    double soilKsat() const noexcept { return m_soilKsat; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Dimensionless slope of the conductivity versus moisture-deficit relation.
     */
    double soilKslope() const noexcept { return m_soilKslope; }
    // Storage layer.
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Storage layer thickness in inches or mm.
     */
    double storThick() const noexcept { return m_storThick; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Storage void-to-solid volume ratio; porosity equals ratio/(1+ratio), despite the historical method name.
     */
    double storVoidFrac() const noexcept { return m_storVoidFrac; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Storage-bottom seepage rate in inches/hour or mm/hour.
     */
    double storKsat() const noexcept { return m_storKsat; }
    // Drain.
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Underdrain coefficient in the input depth/hour power law; its dimensional scale depends on drainExpon().
     */
    double drainCoeff() const noexcept { return m_drainCoeff; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Dimensionless underdrain head exponent.
     */
    double drainExpon() const noexcept { return m_drainExpon; }
    /**
     * @brief Read a standard-control authoring layer value.
     * @return Drain elevation above the storage-layer bottom in inches or mm.
     */
    double drainOffset() const noexcept { return m_drainOffset; }

    /**
     * @brief Test whether parameter setters have marked the control for synchronization.
     * @return Local edit flag; direct name changes and public-field assignments do not set it.
     */
    bool dirty() const noexcept { return m_dirty; }
    /**
     * @brief Acknowledge parameters as synchronized without changing values or emitting signals.
     */
    void clearDirty() noexcept { m_dirty = false; }

    /**
     * @brief Replace the local identifier and notify name observers when text changes.
     * @param[in] newName New name copied verbatim, with no trimming/uniqueness/engine validation.
     * @details Equal text is a no-op. Registry-managed objects should be renamed through their registry so engine references follow.
     */
    void setName(QString newName);
    /**
     * @brief Set the local formulation code, mark dirty, and notify when changed.
     * @param[in] v Standard type 0..7 or NODE type 8; no validation is performed locally.
     * @details Existing engine controls are not retyped by registry save; their engine identity was created with its original type.
     */
    void setType(int v);

    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Surface depression-storage depth, inches for US inputs or mm for SI inputs.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setSurfStorage(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Surface Manning roughness n.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setSurfRoughness(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Surface slope in percent.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setSurfSlope(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Soil layer thickness in inches or mm.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setSoilThick(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Soil void/total volume fraction, expected above field capacity and no greater than one.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setSoilPorosity(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Field-capacity water-content fraction, expected above wilting point and below porosity.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setSoilFc(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Wilting-point water-content fraction, expected nonnegative and below field capacity.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setSoilWp(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Saturated soil conductivity in inches/hour or mm/hour.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setSoilKsat(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Dimensionless slope of the conductivity versus moisture-deficit relation.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setSoilKslope(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Storage layer thickness in inches or mm.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setStorThick(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Storage void-to-solid volume ratio; porosity equals ratio/(1+ratio), despite the historical method name.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setStorVoidFrac(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Storage-bottom seepage rate in inches/hour or mm/hour.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setStorKsat(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Underdrain coefficient in the input depth/hour power law; its dimensional scale depends on drainExpon().
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setDrainCoeff(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Dimensionless underdrain head exponent.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setDrainExpon(double v);
    /**
     * @brief Store a standard-control layer value, mark dirty, and notify when unequal.
     * @param[in] v Drain elevation above the storage-layer bottom in inches or mm.
     * @details Performs no physical bounds/finiteness checks or engine write. Equal values are a no-op; engine save may reject invalid or lifecycle-incompatible values.
     */
    void setDrainOffset(double v);

signals:
    /**
     * @brief Notify observers after the stored identifier changes.
     * @param[in] prev Previous identifier.
     * @param[in] now New identifier, already stored when emitted.
     */
    void nameChanged(QString prev, QString now);
    /**
     * @brief Notify observers after authoring parameter changes; does not itself guarantee a successful engine write.
     */
    void paramsChanged();

private:
    QString m_name;
    int m_type = 0;

    double m_surfStorage = 0.0, m_surfRoughness = 0.0, m_surfSlope = 0.0;
    double m_soilThick = 0.0, m_soilPorosity = 0.0, m_soilFc = 0.0, m_soilWp = 0.0,
           m_soilKsat = 0.0, m_soilKslope = 0.0;
    double m_storThick = 0.0, m_storVoidFrac = 0.0, m_storKsat = 0.0;
    double m_drainCoeff = 0.0, m_drainExpon = 0.0, m_drainOffset = 0.0;

    QVector<SWMM_LidNodeLayer> m_nodeLayers;
    bool m_dirty = false;
};

} // namespace openswmmvis::lid

#endif // OPENSWMMVIS_LID_LIDCONTROLPROVIDER_H
