/**
 * @file      swmmpollutantpropertyadapter.h
 * @brief     Property-tree adapter for [POLLUTANTS].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [POLLUTANTS]. Full scalar
 * coverage backed by `openswmm_pollutants.h` (kDecay, rain/gw/init/rdii
 * concentrations, molecular weight, co-pollutant, snow-only flag,
 * units). Sub-grid coverage (build-up / wash-off curves) lands in the
 * BP structured editor.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMPOLLUTANTPROPERTYADAPTER_H
#define SWMMPOLLUTANTPROPERTYADAPTER_H

#include "ui/properties/dataobjectref.h"
#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMPollutantPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT

public:
    /*! Concentration units. Q_ENUM so the Property Browser shows the name
     *  rather than a raw integer. Mixed-case to dodge macro collisions. */
    enum Units { MgPerL = 0, UgPerL = 1, CountPerL = 2 };
    Q_ENUM(Units)

private:
    // Units are write-once at pollutant creation time (engine has no
    // `swmm_pollutant_set_units`); exposed as read-only here so the
    // Property Browser still surfaces the value.
    Q_PROPERTY(SWMMPollutantPropertyAdapter::Units units READ units NOTIFY changed)
    Q_PROPERTY(double rainConc READ rainConc WRITE setRainConc NOTIFY changed)
    Q_PROPERTY(double gwConc READ gwConc WRITE setGwConc NOTIFY changed)
    Q_PROPERTY(double initConc READ initConc WRITE setInitConc NOTIFY changed)
    Q_PROPERTY(double rdiiConc READ rdiiConc WRITE setRdiiConc NOTIFY changed)
    Q_PROPERTY(double kDecay READ kDecay WRITE setKDecay NOTIFY changed)
    Q_PROPERTY(double mwt READ mwt WRITE setMwt NOTIFY changed)
    Q_PROPERTY(bool snowOnly READ snowOnly WRITE setSnowOnly NOTIFY changed)
    // Slice BM.0-Browse-Edit (2026-05-25) — typed as DataObjectRef
    // (kind=Pollutant) so the cell hosts the picker editor.
    Q_PROPERTY(DataObjectRef coPollutant READ coPollutantRef WRITE setCoPollutantRef NOTIFY changed)
    Q_PROPERTY(double coPollutantFrac READ coPollutantFrac WRITE setCoPollutantFrac NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /**
     * @return Concentration-unit enum read from the engine; MgPerL (0) when unresolved/read fails.
     */
    [[nodiscard]] Units units() const;
    /**
     * @brief Read rainfall concentration in the pollutant's declared mg/L, µg/L or count/L units.
     * @return Authored engine value, or zero on unresolved pollutant/read failure.
     */
    [[nodiscard]] double rainConc() const;
    /**
     * @brief Read groundwater inflow concentration in the pollutant's declared concentration units.
     * @return Authored engine value, or zero on unresolved pollutant/read failure.
     */
    [[nodiscard]] double gwConc() const;
    /**
     * @brief Read initial conveyance-network concentration in the declared concentration units.
     * @return Authored engine value, or zero on unresolved pollutant/read failure.
     */
    [[nodiscard]] double initConc() const;
    /**
     * @brief Read rDII inflow concentration in the declared concentration units.
     * @return Authored engine value, or zero on unresolved pollutant/read failure.
     */
    [[nodiscard]] double rdiiConc() const;
    /**
     * @brief Read first-order pollutant decay coefficient in 1/day.
     * @return Authored engine value, or zero on unresolved pollutant/read failure.
     */
    [[nodiscard]] double kDecay() const;
    /**
     * @brief Read molecular weight in g/mol.
     * @return Authored engine value, or zero on unresolved pollutant/read failure.
     */
    [[nodiscard]] double mwt() const;
    /**
     * @return True when buildup applies only with snow cover; false when missing/read fails.
     */
    [[nodiscard]] bool snowOnly() const;
    /*! Name of the linked co-pollutant, or empty if none.
     * @return Name of the co-pollutant contributing a fraction of its buildup, or empty when absent/missing/read fails.
     */
    [[nodiscard]] QString coPollutant() const;
    /*! Slice BM.0-Browse-Edit — DataObjectRef wrapper of `coPollutant()`.
     * @return Pollutant picker coordinate holding borrowed engine/layer and the current co-pollutant ID.
     */
    [[nodiscard]] DataObjectRef coPollutantRef() const;
    /**
     * @return Dimensionless authored co-pollutant multiplier, or zero on unresolved/read failure.
     */
    [[nodiscard]] double coPollutantFrac() const;

    /**
     * @brief Supply the property browser label for a supported stored property key.
     * @param[in] property Exact Q_PROPERTY key; comparisons are case-sensitive.
     * @return Translated label, including active-project units where implemented; empty for unknown keys. This does not inspect or change the property value.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /**
     * @brief Write rainfall concentration in the pollutant's declared mg/L, µg/L or count/L units.
     * @param[in] v Value in the documented deck units, passed unchanged to the engine without local range/finiteness checks.
     * @details Emits changed only when the engine setter succeeds. Initial concentration is a pre-start input; a running engine can refuse it. Other engine-specific checks are delegated.
     */
    void setRainConc(double v);
    /**
     * @brief Write groundwater inflow concentration in the pollutant's declared concentration units.
     * @param[in] v Value in the documented deck units, passed unchanged to the engine without local range/finiteness checks.
     * @details Emits changed only when the engine setter succeeds. Initial concentration is a pre-start input; a running engine can refuse it. Other engine-specific checks are delegated.
     */
    void setGwConc(double v);
    /**
     * @brief Write initial conveyance-network concentration in the declared concentration units.
     * @param[in] v Value in the documented deck units, passed unchanged to the engine without local range/finiteness checks.
     * @details Emits changed only when the engine setter succeeds. Initial concentration is a pre-start input; a running engine can refuse it. Other engine-specific checks are delegated.
     */
    void setInitConc(double v);
    /**
     * @brief Write rDII inflow concentration in the declared concentration units.
     * @param[in] v Value in the documented deck units, passed unchanged to the engine without local range/finiteness checks.
     * @details Emits changed only when the engine setter succeeds. Initial concentration is a pre-start input; a running engine can refuse it. Other engine-specific checks are delegated.
     */
    void setRdiiConc(double v);
    /**
     * @brief Write first-order pollutant decay coefficient in 1/day.
     * @param[in] v Value in the documented deck units, passed unchanged to the engine without local range/finiteness checks.
     * @details Emits changed only when the engine setter succeeds. Initial concentration is a pre-start input; a running engine can refuse it. Other engine-specific checks are delegated.
     */
    void setKDecay(double v);
    /**
     * @brief Write molecular weight in g/mol.
     * @param[in] v Value in the documented deck units, passed unchanged to the engine without local range/finiteness checks.
     * @details Emits changed only when the engine setter succeeds. Initial concentration is a pre-start input; a running engine can refuse it. Other engine-specific checks are delegated.
     */
    void setMwt(double v);
    /**
     * @brief Set the pollutant's snow-cover buildup gate.
     * @param[in] v True requires snow cover; false removes that gate.
     * @details Writes an integer flag and emits changed on engine acceptance only.
     */
    void setSnowOnly(bool v);
    /**
     * @brief Rebind a co-pollutant while preserving its current fraction.
     * @param[in] poll Pollutant ID; blank or unresolved names resolve to -1 and clear the relationship. Nonblank lookup uses the original, untrimmed text.
     * @details Emits changed only after engine acceptance; no local cycle or fraction validation is performed.
     */
    void setCoPollutant(const QString &poll);
    /*! Slice BM.0-Browse-Edit — DataObjectRef-form setter for the
     *  co-pollutant. Thin wrapper around `setCoPollutant` that extracts
     *  the name from the ref.
     * @brief Commit a pollutant-picker name without rebinding the adapter.
     * @param[in] ref Only currentName is used; an empty name clears via setCoPollutant().
     */
    void setCoPollutantRef(const DataObjectRef &ref);
    /**
     * @brief Change the co-pollutant multiplier while retaining the current referenced index.
     * @param[in] v Dimensionless multiplier, passed unchanged without local range checks.
     * @details Rereads the co-pollutant index and emits changed on successful paired setter; unresolved objects are ignored.
     */
    void setCoPollutantFrac(double v);

private:
    /**
     * @brief Resolve the stored object name against the current engine collection.
     * @return Zero-based live index, or -1 for a missing engine, empty name or unresolved ID; no index is cached across renames.
     */
    [[nodiscard]] int idx() const;
};

#endif // SWMMPOLLUTANTPROPERTYADAPTER_H
