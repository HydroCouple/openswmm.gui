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

class SWMMRainGagePropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(int    rainType    READ rainType    WRITE setRainType    NOTIFY changed)
    Q_PROPERTY(int    dataSource  READ dataSource  WRITE setDataSource  NOTIFY changed)
    /*! Current rainfall rate in project units — read-only display. */
    Q_PROPERTY(double currentRainfall READ currentRainfall NOTIFY changed)
    /*! Slice IO-11e — external rain-file path for this gage. The string
     *  is the original token (relative or absolute) as known to the
     *  engine; reads also surface the resolved absolute via
     *  resolvedFilePath() for tooltip / status display. */
    Q_PROPERTY(QString filePath         READ filePath         WRITE setFilePath         NOTIFY changed)
    Q_PROPERTY(QString resolvedFilePath READ resolvedFilePath                            NOTIFY changed)
    /*! Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
     *  user-flag assignments row (see SWMMNodePropertyAdapter). */
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    [[nodiscard]] UserFlagsEditRef userFlagsRef() const;

    [[nodiscard]] int    rainType()         const;
    [[nodiscard]] int    dataSource()       const;
    [[nodiscard]] double currentRainfall()  const;
    [[nodiscard]] QString filePath()         const;  ///< .original token
    [[nodiscard]] QString resolvedFilePath() const;  ///< .absolute (post-resolve)

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setRainType(int v);
    void setDataSource(int v);
    void setFilePath(const QString &p);
    void setUserFlagsRef(const UserFlagsEditRef &) { emit changed(); }

private:
    [[nodiscard]] int idx() const;
};

#endif // SWMMRAINGAGEPROPERTYADAPTER_H
