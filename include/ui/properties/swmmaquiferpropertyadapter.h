/*!
 * \file   swmmaquiferpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [AQUIFERS]. Full scalar coverage
 * backed by `swmm_aquifer_get/set_param` over the SWMM_AquiferParam codes,
 * plus the upper-zone evaporation pattern (`swmm_aquifer_get/set_evap_pattern`)
 * surfaced as a `DataObjectRef` (Pattern, MONTHLY) so the cell hosts the
 * picker editor. Labels carry the active UnitSystem's length / rate units.
 */

#ifndef SWMMAQUIFERPROPERTYADAPTER_H
#define SWMMAQUIFERPROPERTYADAPTER_H

#include "ui/properties/dataobjectref.h"
#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMAquiferPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT

    // Order mirrors SWMM_AquiferParam / AquiferProvider::Param.
    Q_PROPERTY(double porosity        READ porosity        WRITE setPorosity        NOTIFY changed)
    Q_PROPERTY(double wiltingPoint    READ wiltingPoint    WRITE setWiltingPoint    NOTIFY changed)
    Q_PROPERTY(double fieldCapacity   READ fieldCapacity   WRITE setFieldCapacity   NOTIFY changed)
    Q_PROPERTY(double conductivity    READ conductivity    WRITE setConductivity    NOTIFY changed)
    Q_PROPERTY(double conductSlope    READ conductSlope    WRITE setConductSlope    NOTIFY changed)
    Q_PROPERTY(double tensionSlope    READ tensionSlope    WRITE setTensionSlope    NOTIFY changed)
    Q_PROPERTY(double upperEvapFrac   READ upperEvapFrac   WRITE setUpperEvapFrac   NOTIFY changed)
    Q_PROPERTY(double lowerEvapDepth  READ lowerEvapDepth  WRITE setLowerEvapDepth  NOTIFY changed)
    Q_PROPERTY(double lowerLossCoeff  READ lowerLossCoeff  WRITE setLowerLossCoeff  NOTIFY changed)
    Q_PROPERTY(double bottomElev      READ bottomElev      WRITE setBottomElev      NOTIFY changed)
    Q_PROPERTY(double waterTableElev  READ waterTableElev  WRITE setWaterTableElev  NOTIFY changed)
    Q_PROPERTY(double upperMoisture   READ upperMoisture   WRITE setUpperMoisture   NOTIFY changed)
    Q_PROPERTY(DataObjectRef evapPattern
               READ evapPatternRef WRITE setEvapPatternRef NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    [[nodiscard]] double porosity()       const;
    [[nodiscard]] double wiltingPoint()   const;
    [[nodiscard]] double fieldCapacity()  const;
    [[nodiscard]] double conductivity()   const;
    [[nodiscard]] double conductSlope()   const;
    [[nodiscard]] double tensionSlope()   const;
    [[nodiscard]] double upperEvapFrac()  const;
    [[nodiscard]] double lowerEvapDepth() const;
    [[nodiscard]] double lowerLossCoeff() const;
    [[nodiscard]] double bottomElev()     const;
    [[nodiscard]] double waterTableElev() const;
    [[nodiscard]] double upperMoisture()  const;

    /*! Name of the upper-zone evaporation pattern, or empty if none. */
    [[nodiscard]] QString       evapPattern()    const;
    [[nodiscard]] DataObjectRef evapPatternRef() const;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setPorosity(double v);
    void setWiltingPoint(double v);
    void setFieldCapacity(double v);
    void setConductivity(double v);
    void setConductSlope(double v);
    void setTensionSlope(double v);
    void setUpperEvapFrac(double v);
    void setLowerEvapDepth(double v);
    void setLowerLossCoeff(double v);
    void setBottomElev(double v);
    void setWaterTableElev(double v);
    void setUpperMoisture(double v);
    void setEvapPattern(const QString &pattern);
    void setEvapPatternRef(const DataObjectRef &ref);

private:
    [[nodiscard]] int    idx() const;
    [[nodiscard]] double param(int code) const;
    void                 setParam(int code, double v);
};

#endif // SWMMAQUIFERPROPERTYADAPTER_H
