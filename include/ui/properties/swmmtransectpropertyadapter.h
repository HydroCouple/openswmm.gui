/*!
 * \file   swmmtransectpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 / Phase 6.7.4 — Property-tree adapter for [TRANSECTS].
 *
 * Full scalar coverage backed by `openswmm_infrastructure.h` (DA-ENG-09
 * + BQ-TR-02 shipped): roughness triple, bank stations, encroachment
 * stations, modifier triple (xFactor / yFactor / lengthFactor), and
 * comments. Station-elevation point list is exposed as a read-only count
 * summary — full editing of the point list lives in TransectEditorDialog
 * (Slice BQ Phase 6.7.4).
 *
 * Per-field setters read the current engine triple, mutate the one field
 * being written, and write back atomically — the engine ships only triple
 * setters for roughness / bank / encroachment / modifiers.
 *
 * The adapter listens to the bound SWMMModelLayer's `transectChanged(name)`
 * signal so edits made elsewhere (TransectEditorDialog) refresh the
 * property panel in lock-step ([[feedback_mvc_synchronized_uis]]).
 */

#ifndef SWMMTRANSECTPROPERTYADAPTER_H
#define SWMMTRANSECTPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMTransectPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT

    Q_PROPERTY(QString comments           READ comments           WRITE setComments           NOTIFY changed)

    Q_PROPERTY(double  nLeftBank          READ nLeftBank          WRITE setNLeftBank          NOTIFY changed)
    Q_PROPERTY(double  nRightBank         READ nRightBank         WRITE setNRightBank         NOTIFY changed)
    Q_PROPERTY(double  nChannel           READ nChannel           WRITE setNChannel           NOTIFY changed)

    Q_PROPERTY(double  xLeftBank          READ xLeftBank          WRITE setXLeftBank          NOTIFY changed)
    Q_PROPERTY(double  xRightBank         READ xRightBank         WRITE setXRightBank         NOTIFY changed)

    Q_PROPERTY(double  xLeftEncroachment  READ xLeftEncroachment  WRITE setXLeftEncroachment  NOTIFY changed)
    Q_PROPERTY(double  xRightEncroachment READ xRightEncroachment WRITE setXRightEncroachment NOTIFY changed)

    Q_PROPERTY(double  stationMultiplier  READ stationMultiplier  WRITE setStationMultiplier  NOTIFY changed)
    Q_PROPERTY(double  elevationOffset    READ elevationOffset    WRITE setElevationOffset    NOTIFY changed)
    Q_PROPERTY(double  meanderFactor      READ meanderFactor      WRITE setMeanderFactor      NOTIFY changed)

    // Read-only station count (Property Browser surface — full point list
    // edited through TransectEditorDialog).
    Q_PROPERTY(int     stationCount       READ stationCount       NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    [[nodiscard]] QString comments()           const;
    [[nodiscard]] double  nLeftBank()          const;
    [[nodiscard]] double  nRightBank()         const;
    [[nodiscard]] double  nChannel()           const;
    [[nodiscard]] double  xLeftBank()          const;
    [[nodiscard]] double  xRightBank()         const;
    [[nodiscard]] double  xLeftEncroachment()  const;
    [[nodiscard]] double  xRightEncroachment() const;
    [[nodiscard]] double  stationMultiplier()  const;
    [[nodiscard]] double  elevationOffset()    const;
    [[nodiscard]] double  meanderFactor()      const;
    [[nodiscard]] int     stationCount()       const;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setComments(const QString &v);
    void setNLeftBank(double v);
    void setNRightBank(double v);
    void setNChannel(double v);
    void setXLeftBank(double v);
    void setXRightBank(double v);
    void setXLeftEncroachment(double v);
    void setXRightEncroachment(double v);
    void setStationMultiplier(double v);
    void setElevationOffset(double v);
    void setMeanderFactor(double v);

private:
    [[nodiscard]] int idx() const;
};

#endif // SWMMTRANSECTPROPERTYADAPTER_H
