/*!
 * \file   swmmpollutantpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [POLLUTANTS]. Full scalar
 * coverage backed by `openswmm_pollutants.h` (kDecay, rain/gw/init/rdii
 * concentrations, molecular weight, co-pollutant, snow-only flag,
 * units). Sub-grid coverage (build-up / wash-off curves) lands in the
 * BP structured editor.
 */

#ifndef SWMMPOLLUTANTPROPERTYADAPTER_H
#define SWMMPOLLUTANTPROPERTYADAPTER_H

#include "ui/properties/dataobjectref.h"
#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMPollutantPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    // Units are write-once at pollutant creation time (engine has no
    // `swmm_pollutant_set_units`); exposed as read-only here so the
    // Property Browser still surfaces the value.
    Q_PROPERTY(int    units      READ units                          NOTIFY changed)
    Q_PROPERTY(double rainConc   READ rainConc   WRITE setRainConc   NOTIFY changed)
    Q_PROPERTY(double gwConc     READ gwConc     WRITE setGwConc     NOTIFY changed)
    Q_PROPERTY(double initConc   READ initConc   WRITE setInitConc   NOTIFY changed)
    Q_PROPERTY(double rdiiConc   READ rdiiConc   WRITE setRdiiConc   NOTIFY changed)
    Q_PROPERTY(double kDecay     READ kDecay     WRITE setKDecay     NOTIFY changed)
    Q_PROPERTY(double mwt        READ mwt        WRITE setMwt        NOTIFY changed)
    Q_PROPERTY(bool   snowOnly   READ snowOnly   WRITE setSnowOnly   NOTIFY changed)
    // Slice BM.0-Browse-Edit (2026-05-25) — typed as DataObjectRef
    // (kind=Pollutant) so the cell hosts the picker editor.
    Q_PROPERTY(DataObjectRef coPollutant
               READ coPollutantRef WRITE setCoPollutantRef NOTIFY changed)
    Q_PROPERTY(double coPollutantFrac READ coPollutantFrac WRITE setCoPollutantFrac NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    [[nodiscard]] int    units()    const;
    [[nodiscard]] double rainConc() const;
    [[nodiscard]] double gwConc()   const;
    [[nodiscard]] double initConc() const;
    [[nodiscard]] double rdiiConc() const;
    [[nodiscard]] double kDecay()   const;
    [[nodiscard]] double mwt()      const;
    [[nodiscard]] bool   snowOnly() const;
    /*! Name of the linked co-pollutant, or empty if none. */
    [[nodiscard]] QString coPollutant()     const;
    /*! Slice BM.0-Browse-Edit — DataObjectRef wrapper of `coPollutant()`. */
    [[nodiscard]] DataObjectRef coPollutantRef() const;
    [[nodiscard]] double  coPollutantFrac() const;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setRainConc(double v);
    void setGwConc(double v);
    void setInitConc(double v);
    void setRdiiConc(double v);
    void setKDecay(double v);
    void setMwt(double v);
    void setSnowOnly(bool v);
    void setCoPollutant(const QString &poll);
    /*! Slice BM.0-Browse-Edit — DataObjectRef-form setter for the
     *  co-pollutant. Thin wrapper around `setCoPollutant` that extracts
     *  the name from the ref. */
    void setCoPollutantRef(const DataObjectRef &ref);
    void setCoPollutantFrac(double v);

private:
    [[nodiscard]] int idx() const;
};

#endif // SWMMPOLLUTANTPROPERTYADAPTER_H
