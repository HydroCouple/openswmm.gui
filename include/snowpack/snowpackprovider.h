/**
 * @file      snowpackprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     MVC model for a single SWMM snow pack ([SNOWPACKS]).
 *
 * One SnowpackProvider per `[SNOWPACKS]` entry. Owned by a project-scoped
 * SnowpackRegistry. Mirrors AquiferProvider: the engine exposes snow-pack
 * parameters as four grouped get/set calls (PLOWABLE, IMPERVIOUS, PERVIOUS,
 * REMOVAL), so the surface/removal and UEB scalars are stored in an index-addressed array
 * whose order matches Param (0..ParamCount-1). The optional REMOVAL
 * destination subcatchment is a separate string field. The provider stays
 * engine-agnostic — the registry maps index ranges to the grouped engine calls.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_SNOWPACK_SNOWPACKPROVIDER_H
#define OPENSWMMVIS_SNOWPACK_SNOWPACKPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::snowpack
{

class SnowpackProvider : public QObject
{
    Q_OBJECT

public:
    /*! Field order: the three 7-value surface groups in engine argument order
     *  (cmin, cmax, tbase, fwfrac, sd0, fw0, last), then the six REMOVAL
     *  values. `Last` is the plowable fraction for PLOWABLE and the 100%-cover
     *  depth for IMPERVIOUS / PERVIOUS. */
    enum Param {
        PlowableCmin = 0,
        PlowableCmax,
        PlowableTbase,
        PlowableFwFrac,
        PlowableSd0,
        PlowableFw0,
        PlowableLast,
        ImperviousCmin,
        ImperviousCmax,
        ImperviousTbase,
        ImperviousFwFrac,
        ImperviousSd0,
        ImperviousFw0,
        ImperviousLast,
        PerviousCmin,
        PerviousCmax,
        PerviousTbase,
        PerviousFwFrac,
        PerviousSd0,
        PerviousFw0,
        PerviousLast,
        RemovalDsnow,
        RemovalFOut,
        RemovalFImp,
        RemovalFPerv,
        RemovalFImelt,
        RemovalFSubcatch,
        UebForest,
        UebDrift,
        UebGroundHeat,
        UebExtinctionDepth,
        UebInitialEnergy,
        ParamCount
    };

    /**
     * @brief Create an engine-independent authoring value model.
     * @param[in] name Initial identifier copied verbatim; this constructor does not enforce registry uniqueness.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit SnowpackProvider(QString name, QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources.
     */
    ~SnowpackProvider() override;

    /**
     * @brief Read the current authoring identifier.
     * @return Copied name; use registry rename() for uniqueness and engine reference synchronization.
     */
    QString name() const noexcept { return m_name; }

    /*! \returns parameter \p k (0..ParamCount-1), or 0 if out of range.
     * @brief Read a snowpack parameter in engine authoring units.
     * @param[in] k Param index: melt coefficients use in/hr/degree F or mm/hr/degree C; temperature uses F or C; snow/free-water/cover/removal depths use inches or mm water equivalent. Surface/removal fractions are dimensionless. UEB forest/drift are dimensionless, ground heat W/m2, extinction depth metres, and initial energy kJ/m2 in both unit systems.
     * @return Stored value, or zero for an out-of-range index; no conversion occurs.
     */
    double param(int k) const noexcept;

    /*! \returns destination subcatchment for the RemovalFSubcatch fraction.
     * @return Copied removal-destination identifier, empty when none is assigned.
     */
    QString removalSubcatch() const noexcept { return m_removalSubcatch; }

    /**
     * @brief Replace the local identifier and notify name observers when text changes.
     * @param[in] newName New name copied verbatim, with no trimming/uniqueness/engine validation.
     * @details Equal text is a no-op. Registry-managed objects should be renamed through their registry so engine references follow.
     */
    void setName(QString newName);
    /**
     * @brief Store a snowpack authoring parameter and notify when unequal.
     * @param[in] k Param index in 0..ParamCount-1; invalid indices are ignored.
     * @param[in] v Value in the units described by param(); local setter does not enforce finite values, ranges, or cross-field relationships.
     * @details Engine batch save validates the complete authoring data. Equal values are a no-op.
     */
    void setParam(int k, double v);
    /**
     * @brief Store the removal-destination name and notify when text changes.
     * @param[in] name Identifier copied verbatim, or empty to clear; resolved by the engine during batch save.
     */
    void setRemovalSubcatch(QString name);
    /**
     * @brief Read the explicit snow formulation override.
     * @return -1 for project default, 0 for degree-day, or 1 for UEB.
     */
    int modelKind() const noexcept { return m_modelKind; } // -1 project default
    /**
     * @brief Resolve the explicit override against the cached project default.
     * @return modelKind() when nonnegative, otherwise the last setDefaultModel() value; no engine query occurs.
     */
    int effectiveModel() const noexcept { return m_modelKind < 0 ? m_defaultModel : m_modelKind; }
    /**
     * @brief Update the formulation override and notify parameter observers when changed.
     * @param[in] kind -1 to inherit project default, 0 degree-day, or 1 UEB; other values are ignored.
     */
    void setModelKind(int kind);
    /**
     * @brief Cache the project formulation used when the pack inherits it.
     * @param[in] kind 0 degree-day or 1 UEB; other values are ignored.
     * @details Emits paramsChanged() on a changed cached default even if an explicit override currently hides it.
     */
    void setDefaultModel(int kind);

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
    QString m_removalSubcatch;
    double m_param[ParamCount] = {};
    int m_modelKind = -1;
    int m_defaultModel = 0;
};

} // namespace openswmmvis::snowpack

#endif // OPENSWMMVIS_SNOWPACK_SNOWPACKPROVIDER_H
