/**
 * @file      pollutantprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     MVC model for a single SWMM pollutant ([POLLUTANTS]).
 *
 * One PollutantProvider per `[POLLUTANTS]` entry. Owned by a project-scoped
 * PollutantRegistry. Mirrors StreetProvider in shape — scalar fields with a
 * paramsChanged() signal so the editor's list pane and field form refresh in
 * lock-step. Engine I/O walks `swmm_pollutant_*` from openswmm_pollutants.h.
 *
 * Units (0=MG/L, 1=UG/L, 2=#/L) are write-once at engine creation time — the
 * engine has no `swmm_pollutant_set_units`, so changing units only takes
 * effect for a pollutant that does not yet exist in the engine.
 *
 * Co-pollutant is stored by name (empty = none) and resolved to an engine
 * index at save time.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_POLLUTANT_POLLUTANTPROVIDER_H
#define OPENSWMMVIS_POLLUTANT_POLLUTANTPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::pollutant
{

class PollutantProvider : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an engine-independent authoring value model.
     * @param[in] name Initial identifier copied verbatim; this constructor does not enforce registry uniqueness.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit PollutantProvider(QString name, QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources.
     */
    ~PollutantProvider() override;

    // ── Identity ────────────────────────────────────────────────────────────
    /**
     * @brief Read the current authoring identifier.
     * @return Copied name; use registry rename() for uniqueness and engine reference synchronization.
     */
    QString name() const noexcept { return m_name; }

    // ── Scalar properties ───────────────────────────────────────────────────
    /**
     * @brief Read a local pollutant authoring value.
     * @return Concentration unit code: 0 mg/L, 1 micrograms/L, 2 count/L.
     * @details The engine uses this only when adding a missing pollutant; editing an already existing pollutant does not change its engine units.
     */
    int units() const noexcept { return m_units; } ///< 0=MG/L,1=UG/L,2=#/L
    /**
     * @brief Read a local pollutant authoring value.
     * @return Rainfall concentration in units().
     */
    double rainConc() const noexcept { return m_rainConc; }
    /**
     * @brief Read a local pollutant authoring value.
     * @return Groundwater concentration in units().
     */
    double gwConc() const noexcept { return m_gwConc; }
    /**
     * @brief Read a local pollutant authoring value.
     * @return Initial node concentration in units().
     */
    double initConc() const noexcept { return m_initConc; }
    /**
     * @brief Read a local pollutant authoring value.
     * @return RDII concentration in units().
     */
    double rdiiConc() const noexcept { return m_rdiiConc; }
    /**
     * @brief Read a local pollutant authoring value.
     * @return First-order decay coefficient per day.
     */
    double kDecay() const noexcept { return m_kDecay; } ///< 1/day
    /**
     * @brief Read a local pollutant authoring value.
     * @return Molecular weight retained in the engine authoring representation, without conversion.
     */
    double mwt() const noexcept { return m_mwt; } ///< molecular weight
    /**
     * @brief Read a local pollutant authoring value.
     * @return Whether the pollutant is associated only with snowfall/snowmelt.
     */
    bool snowOnly() const noexcept { return m_snowOnly; }
    /**
     * @brief Read a local pollutant authoring value.
     * @return Co-pollutant identifier; empty means none.
     * @details Name resolution is deferred to the registry second save pass; an unresolved name produces engine index -1.
     */
    QString coPollutant() const noexcept { return m_coPollutant; } ///< empty = none
    /**
     * @brief Read a local pollutant authoring value.
     * @return Dimensionless co-pollutant contribution factor.
     */
    double coFraction() const noexcept { return m_coFraction; }

    // ── Mutators (emit changed signals) ─────────────────────────────────────
    /**
     * @brief Replace the local identifier and notify name observers when text changes.
     * @param[in] newName New name copied verbatim, with no trimming/uniqueness/engine validation.
     * @details Equal text is a no-op. Registry-managed objects should be renamed through their registry so engine references follow.
     */
    void setName(QString newName);
    /**
     * @brief Store the pollutant authoring value and emit paramsChanged() when unequal.
     * @param[in] v Concentration unit code: 0 mg/L, 1 micrograms/L, 2 count/L.
     * @details No range/finiteness/reference validation or immediate engine write is performed. The engine uses this only when adding a missing pollutant; editing an already existing pollutant does not change its engine units.
     */
    void setUnits(int v);
    /**
     * @brief Store the pollutant authoring value and emit paramsChanged() when unequal.
     * @param[in] v Rainfall concentration in units().
     * @details No range/finiteness/reference validation or immediate engine write is performed.
     */
    void setRainConc(double v);
    /**
     * @brief Store the pollutant authoring value and emit paramsChanged() when unequal.
     * @param[in] v Groundwater concentration in units().
     * @details No range/finiteness/reference validation or immediate engine write is performed.
     */
    void setGwConc(double v);
    /**
     * @brief Store the pollutant authoring value and emit paramsChanged() when unequal.
     * @param[in] v Initial node concentration in units().
     * @details No range/finiteness/reference validation or immediate engine write is performed.
     */
    void setInitConc(double v);
    /**
     * @brief Store the pollutant authoring value and emit paramsChanged() when unequal.
     * @param[in] v RDII concentration in units().
     * @details No range/finiteness/reference validation or immediate engine write is performed.
     */
    void setRdiiConc(double v);
    /**
     * @brief Store the pollutant authoring value and emit paramsChanged() when unequal.
     * @param[in] v First-order decay coefficient per day.
     * @details No range/finiteness/reference validation or immediate engine write is performed.
     */
    void setKDecay(double v);
    /**
     * @brief Store the pollutant authoring value and emit paramsChanged() when unequal.
     * @param[in] v Molecular weight retained in the engine authoring representation, without conversion.
     * @details No range/finiteness/reference validation or immediate engine write is performed.
     */
    void setMwt(double v);
    /**
     * @brief Store the pollutant authoring value and emit paramsChanged() when unequal.
     * @param[in] v Whether the pollutant is associated only with snowfall/snowmelt.
     * @details No range/finiteness/reference validation or immediate engine write is performed.
     */
    void setSnowOnly(bool v);
    /**
     * @brief Store the pollutant authoring value and emit paramsChanged() when unequal.
     * @param[in] name Co-pollutant identifier; empty means none.
     * @details No range/finiteness/reference validation or immediate engine write is performed. Name resolution is deferred to the registry second save pass; an unresolved name produces engine index -1.
     */
    void setCoPollutant(QString name);
    /**
     * @brief Store the pollutant authoring value and emit paramsChanged() when unequal.
     * @param[in] v Dimensionless co-pollutant contribution factor.
     * @details No range/finiteness/reference validation or immediate engine write is performed.
     */
    void setCoFraction(double v);

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

    int m_units = 0;
    double m_rainConc = 0.0;
    double m_gwConc = 0.0;
    double m_initConc = 0.0;
    double m_rdiiConc = 0.0;
    double m_kDecay = 0.0;
    double m_mwt = 0.0;
    bool m_snowOnly = false;
    QString m_coPollutant;
    double m_coFraction = 0.0;
};

} // namespace openswmmvis::pollutant

#endif // OPENSWMMVIS_POLLUTANT_POLLUTANTPROVIDER_H
