/*!
 * \file   swmmraingagepropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [RAINGAGES]. Rain Gage is
 * technically a spatial object (has map coordinates) but was missed by
 * the original AG.3 adapter set; DA.2 closes that gap.
 *
 * Scalar coverage: rainType + dataSource (engine has matching getters).
 * Recording interval / timeseries-id / filename are exposed as
 * write-only (engine has only setters today — see `DA-ENG-04`).
 */

#ifndef SWMMRAINGAGEPROPERTYADAPTER_H
#define SWMMRAINGAGEPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"
#include "ui/properties/userflagseditref.h"   // USER_FLAGS Phase 4
#include "ui/properties/dataobjectref.h"       // DA.2 parity — Series Name picker
#include "ui/properties/rainintervalref.h"     // DA.2 parity — H:MM interval combo

class SWMMRainGagePropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(int    rainType    READ rainType    WRITE setRainType    NOTIFY changed)
    /*! Recording interval. Legacy [RAINGAGES] token 2, edited as an H:MM
     *  clock combo (RainIntervalComboBox). The ref carries seconds; the
     *  engine stores GageData.interval_sec. */
    Q_PROPERTY(RainIntervalRef rainInterval
               READ rainIntervalRef WRITE setRainIntervalRef NOTIFY changed)
    /*! Snow-catch deficiency correction factor (legacy SCF, token 3).
     *  Distinct from the rainfall scale factor. */
    Q_PROPERTY(double snowFactor   READ snowFactor   WRITE setSnowFactor   NOTIFY changed)
    Q_PROPERTY(int    dataSource  READ dataSource  WRITE setDataSource  NOTIFY changed)
    /*! DA.2 parity — TIME SERIES source: the in-model series id. Renders
     *  as a TimeSeries-filtered picker (DataObjectPickerEditor). Only
     *  meaningful when dataSource == TIMESERIES (panel greys it otherwise). */
    Q_PROPERTY(DataObjectRef seriesName
               READ seriesNameRef WRITE setSeriesNameRef NOTIFY changed)
    /*! Current rainfall rate in project units — read-only display. */
    Q_PROPERTY(double currentRainfall READ currentRainfall NOTIFY changed)
    /*! Slice IO-11e — external rain-file path for this gage. The string
     *  is the original token (relative or absolute) as known to the
     *  engine; reads also surface the resolved absolute via
     *  resolvedFilePath() for tooltip / status display. */
    Q_PROPERTY(QString filePath         READ filePath         WRITE setFilePath         NOTIFY changed)
    Q_PROPERTY(QString resolvedFilePath READ resolvedFilePath                            NOTIFY changed)
    /*! DA.2 parity — DATA FILE source: station id (standard SWMM rain file
     *  grammar `Fname Station Units`) and rain-depth units (0=IN, 1=MM).
     *  Only meaningful when dataSource == FILE. */
    Q_PROPERTY(QString stationId  READ stationId  WRITE setStationId  NOTIFY changed)
    Q_PROPERTY(int     rainUnits  READ rainUnits  WRITE setRainUnits  NOTIFY changed)
    /*! Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
     *  user-flag assignments row (see SWMMNodePropertyAdapter). */
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    [[nodiscard]] UserFlagsEditRef userFlagsRef() const;

    [[nodiscard]] int    rainType()         const;
    [[nodiscard]] RainIntervalRef rainIntervalRef() const;
    [[nodiscard]] double snowFactor()        const;
    [[nodiscard]] int    dataSource()       const;
    [[nodiscard]] DataObjectRef seriesNameRef() const;
    [[nodiscard]] double currentRainfall()  const;
    [[nodiscard]] QString filePath()         const;  ///< .original token
    [[nodiscard]] QString resolvedFilePath() const;  ///< .absolute (post-resolve)
    [[nodiscard]] QString stationId()        const;
    [[nodiscard]] int     rainUnits()        const;  ///< 0=IN, 1=MM

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setRainType(int v);
    void setRainIntervalRef(const RainIntervalRef &r);
    void setSnowFactor(double v);
    void setDataSource(int v);
    void setSeriesNameRef(const DataObjectRef &r);
    void setFilePath(const QString &p);
    void setStationId(const QString &s);
    void setRainUnits(int v);
    void setUserFlagsRef(const UserFlagsEditRef &) { emit changed(); }

private:
    [[nodiscard]] int idx() const;
};

#endif // SWMMRAINGAGEPROPERTYADAPTER_H
