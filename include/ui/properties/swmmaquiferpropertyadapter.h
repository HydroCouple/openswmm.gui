/**
 * @file      swmmaquiferpropertyadapter.h
 * @brief     Property-tree adapter for [AQUIFERS].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [AQUIFERS]. Full scalar coverage
 * backed by `swmm_aquifer_get/set_param` over the SWMM_AquiferParam codes,
 * plus the upper-zone evaporation pattern (`swmm_aquifer_get/set_evap_pattern`)
 * surfaced as a `DataObjectRef` (Pattern, MONTHLY) so the cell hosts the
 * picker editor. Labels carry the active UnitSystem's length / rate units.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMAQUIFERPROPERTYADAPTER_H
#define SWMMAQUIFERPROPERTYADAPTER_H

#include "ui/properties/dataobjectref.h"
#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMAquiferPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT

    // Order mirrors SWMM_AquiferParam / AquiferProvider::Param.
    Q_PROPERTY(double porosity READ porosity WRITE setPorosity NOTIFY changed)
    Q_PROPERTY(double wiltingPoint READ wiltingPoint WRITE setWiltingPoint NOTIFY changed)
    Q_PROPERTY(double fieldCapacity READ fieldCapacity WRITE setFieldCapacity NOTIFY changed)
    Q_PROPERTY(double conductivity READ conductivity WRITE setConductivity NOTIFY changed)
    Q_PROPERTY(double conductSlope READ conductSlope WRITE setConductSlope NOTIFY changed)
    Q_PROPERTY(double tensionSlope READ tensionSlope WRITE setTensionSlope NOTIFY changed)
    Q_PROPERTY(double upperEvapFrac READ upperEvapFrac WRITE setUpperEvapFrac NOTIFY changed)
    Q_PROPERTY(double lowerEvapDepth READ lowerEvapDepth WRITE setLowerEvapDepth NOTIFY changed)
    Q_PROPERTY(double lowerLossCoeff READ lowerLossCoeff WRITE setLowerLossCoeff NOTIFY changed)
    Q_PROPERTY(double bottomElev READ bottomElev WRITE setBottomElev NOTIFY changed)
    Q_PROPERTY(double waterTableElev READ waterTableElev WRITE setWaterTableElev NOTIFY changed)
    Q_PROPERTY(double upperMoisture READ upperMoisture WRITE setUpperMoisture NOTIFY changed)
    Q_PROPERTY(DataObjectRef evapPattern READ evapPatternRef WRITE setEvapPatternRef NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /**
     * @brief Pore volume fraction, physically in [0,1].
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double porosity() const;
    /**
     * @brief Wilting-point water-content fraction.
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double wiltingPoint() const;
    /**
     * @brief Field-capacity water-content fraction.
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double fieldCapacity() const;
    /**
     * @brief Saturated hydraulic conductivity in project rainfall rate units (in/hr or mm/hr).
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double conductivity() const;
    /**
     * @brief Dimensionless conductivity/moisture exponential slope.
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double conductSlope() const;
    /**
     * @brief Capillary tension slope in project rainfall depth units (in or mm).
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double tensionSlope() const;
    /**
     * @brief Upper-zone share of evaporation, a fraction in [0,1].
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double upperEvapFrac() const;
    /**
     * @brief Maximum depth over which lower-zone evaporation occurs, in project length units (ft or m).
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double lowerEvapDepth() const;
    /**
     * @brief Deep groundwater loss rate in project rainfall rate units (in/hr or mm/hr).
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double lowerLossCoeff() const;
    /**
     * @brief Aquifer bottom elevation in project length units (ft or m).
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double bottomElev() const;
    /**
     * @brief Initial water-table elevation in project length units (ft or m).
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double waterTableElev() const;
    /**
     * @brief Initial unsaturated-zone water-content fraction.
     * @return Authored engine value, or zero when the aquifer cannot be resolved; engine read status is not returned.
     */
    [[nodiscard]] double upperMoisture() const;

    /*! Name of the upper-zone evaporation pattern, or empty if none.
     * @return Assigned monthly evaporation pattern ID, or empty when unset/missing/unreadable; no ownership of the pattern is transferred.
     */
    [[nodiscard]] QString evapPattern() const;
    /**
     * @return Picker coordinate with borrowed engine/layer, Pattern kind, MONTHLY type lock (0), and the current pattern name.
     */
    [[nodiscard]] DataObjectRef evapPatternRef() const;

    /**
     * @brief Supply the property browser label for a supported stored property key.
     * @param[in] property Exact Q_PROPERTY key; comparisons are case-sensitive.
     * @return Translated label, including active-project units where implemented; empty for unknown keys. This does not inspect or change the property value.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /**
     * @brief Write the aquifer's pore volume fraction, physically in [0,1].
     * @param[in] v Fraction in [0,1].
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setPorosity(double v);
    /**
     * @brief Write the aquifer's wilting-point water-content fraction.
     * @param[in] v Fraction in [0,1].
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setWiltingPoint(double v);
    /**
     * @brief Write the aquifer's field-capacity water-content fraction.
     * @param[in] v Fraction in [0,1].
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setFieldCapacity(double v);
    /**
     * @brief Write the aquifer's saturated hydraulic conductivity in project rainfall rate units (in/hr or mm/hr).
     * @param[in] v Nonnegative conductivity in in/hr or mm/hr.
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setConductivity(double v);
    /**
     * @brief Write the aquifer's dimensionless conductivity/moisture exponential slope.
     * @param[in] v Nonnegative dimensionless slope.
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setConductSlope(double v);
    /**
     * @brief Write the aquifer's capillary tension slope in project rainfall depth units (in or mm).
     * @param[in] v Nonnegative tension slope in in or mm.
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setTensionSlope(double v);
    /**
     * @brief Write the aquifer's upper-zone share of evaporation, a fraction in [0,1].
     * @param[in] v Fraction in [0,1].
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setUpperEvapFrac(double v);
    /**
     * @brief Write the aquifer's maximum depth over which lower-zone evaporation occurs, in project length units (ft or m).
     * @param[in] v Nonnegative depth in ft or m.
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setLowerEvapDepth(double v);
    /**
     * @brief Write the aquifer's deep groundwater loss rate in project rainfall rate units (in/hr or mm/hr).
     * @param[in] v Nonnegative rate in in/hr or mm/hr.
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setLowerLossCoeff(double v);
    /**
     * @brief Write the aquifer's aquifer bottom elevation in project length units (ft or m).
     * @param[in] v Signed elevation in ft or m.
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setBottomElev(double v);
    /**
     * @brief Write the aquifer's initial water-table elevation in project length units (ft or m).
     * @param[in] v Signed elevation in ft or m.
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setWaterTableElev(double v);
    /**
     * @brief Write the aquifer's initial unsaturated-zone water-content fraction.
     * @param[in] v Fraction in [0,1].
     * @details Resolves the live aquifer and delegates bounds/lifecycle checks to the engine. Emits changed only on SWMM_OK; unresolved objects or rejected edits produce no signal or error result. Coupled physical validation runs at engine start.
     */
    void setUpperMoisture(double v);
    /**
     * @brief Change the aquifer's upper-zone monthly evaporation multiplier pattern.
     * @param[in] pattern Pattern ID trimmed before lookup; empty clears the assignment using nullptr.
     * @details The engine copies the name in pre-start editing state; monthly-pattern identity is resolved/validated during model startup. Emits changed only on acceptance; failures are silent.
     */
    void setEvapPattern(const QString &pattern);
    /**
     * @brief Commit the name selected by the monthly-pattern picker.
     * @param[in] ref Only currentName is used; embedded engine/layer pointers do not rebind this adapter.
     * @details Delegates to setEvapPattern(), including clearing and failure behavior.
     */
    void setEvapPatternRef(const DataObjectRef &ref);

private:
    /**
     * @brief Resolve the stored object name against the current engine collection.
     * @return Zero-based live index, or -1 for a missing engine, empty name or unresolved ID; no index is cached across renames.
     */
    [[nodiscard]] int idx() const;
    /**
     * @brief Read one scalar aquifer parameter by engine selector.
     * @param[in] code SWMM_AquiferParam selector.
     * @return Authored value in that parameter's deck units, or zero on missing aquifer/read failure.
     */
    [[nodiscard]] double param(int code) const;
    /**
     * @brief Write one aquifer scalar and notify only after engine acceptance.
     * @param[in] code SWMM_AquiferParam selector determining units, bounds and lifecycle policy.
     * @param[in] v New authored value; no local conversion or clamping occurs.
     * @details Missing aquifer is a no-op; engine errors are not surfaced.
     */
    void setParam(int code, double v);
};

#endif // SWMMAQUIFERPROPERTYADAPTER_H
