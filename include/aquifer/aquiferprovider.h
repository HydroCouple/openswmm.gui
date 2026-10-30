/**
 * @file      aquiferprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     MVC model for a single SWMM aquifer ([AQUIFERS]).
 *
 * One AquiferProvider per `[AQUIFERS]` entry. Owned by a project-scoped
 * AquiferRegistry. Mirrors PollutantProvider. The engine exposes aquifer
 * parameters through a uniform param-code API (swmm_aquifer_get/set_param),
 * so the twelve fields are stored in an index-addressed array whose order
 * matches SWMM_AquiferParam (0..ParamCount-1). The provider stays engine-
 * agnostic — the registry maps indices to engine codes (identity).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_AQUIFER_AQUIFERPROVIDER_H
#define OPENSWMMVIS_AQUIFER_AQUIFERPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::aquifer
{

class AquiferProvider : public QObject
{
    Q_OBJECT

public:
    /*! Field order mirrors SWMM_AquiferParam in openswmm_subcatchments.h. */
    enum Param {
        Porosity = 0,
        WiltingPoint,
        FieldCapacity,
        Conductivity,
        ConductSlope,
        TensionSlope,
        UpperEvapFrac,
        LowerEvapDepth,
        LowerLossCoeff,
        BottomElev,
        WaterTableElev,
        UpperMoisture,
        ParamCount
    };

    /**
     * @brief Create an engine-independent authoring value model.
     * @param[in] name Initial identifier copied verbatim; this constructor does not enforce registry uniqueness.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit AquiferProvider(QString name, QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources.
     */
    ~AquiferProvider() override;

    /**
     * @brief Read the current authoring identifier.
     * @return Copied name; use registry rename() for uniqueness and engine reference synchronization.
     */
    QString name() const noexcept { return m_name; }

    /*! \returns parameter \p k (0..ParamCount-1), or 0 if out of range.
     * @brief Read a parameter in engine input-file units.
     * @param[in] k Param code: fractions for porosity/moisture/evaporation, in/hr or mm/hr for conductivity/lower loss, ft or m for elevation/lower-evaporation depth/tension slope, and dimensionless conductivity slope.
     * @return Stored value for a valid code, otherwise zero; no unit conversion or physical validation occurs.
     */
    double param(int k) const noexcept;

    /*! Upper-zone evaporation pattern name (the `[AQUIFERS]` ETupat column);
     *  empty when none is assigned. Changes are folded into paramsChanged.
     * @return Copied upper-zone evaporation pattern name; empty means unassigned.
     */
    QString evapPattern() const noexcept { return m_evapPattern; }

    /**
     * @brief Replace the local identifier and notify name observers when text changes.
     * @param[in] newName New name copied verbatim, with no trimming/uniqueness/engine validation.
     * @details Equal text is a no-op. Registry-managed objects should be renamed through their registry so engine references follow.
     */
    void setName(QString newName);
    /**
     * @brief Replace one local aquifer authoring value and emit paramsChanged() when unequal.
     * @param[in] k Param code in 0..ParamCount-1; an out-of-range code is ignored.
     * @param[in] v Value in the units described by param(); no range/finiteness checks are performed here.
     * @details Engine validation on save may reject fractions outside 0..1, negative nonelevation values, lifecycle-incompatible edits, or cross-field combinations at initialization; this setter still retains the supplied local value.
     */
    void setParam(int k, double v);
    /**
     * @brief Replace the evaporation pattern identifier and notify parameter observers.
     * @param[in] pattern Pattern name copied verbatim, or empty to clear; existence is resolved by the engine later.
     * @details Identical text is a no-op; performs no engine write.
     */
    void setEvapPattern(QString pattern);

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
    double m_param[ParamCount] = {};
    QString m_evapPattern;
};

} // namespace openswmmvis::aquifer

#endif // OPENSWMMVIS_AQUIFER_AQUIFERPROVIDER_H
