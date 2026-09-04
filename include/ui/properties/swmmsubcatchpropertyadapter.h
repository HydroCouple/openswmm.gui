/*!
 * \file   swmmsubcatchpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AG.3 — Property-tree adapter for SWMM subcatchments.
 */

#ifndef SWMMSUBCATCHPROPERTYADAPTER_H
#define SWMMSUBCATCHPROPERTYADAPTER_H

#include <QObject>
#include <QString>
#include <QUuid>                            // Stats-source identity (QA.2 mirror)

#include <openswmm/engine/openswmm_engine.h>

#include "ui/properties/userflagseditref.h"   // USER_FLAGS Phase 4
#include "ui/properties/dataobjectref.h"       // Phase 3 — gage / outlet pickers
#include "ui/properties/subcatchcompoundeditref.h" // Phase 3 — landuse/GW/LID

class SWMMModelLayer;

namespace openswmmvis { class OutputStatsRegistry; }    // QA.2 mirror

class SWMMSubcatchPropertyAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name        READ name  WRITE setName)
    /*! Slice TA — free-form `[TAGS]` label. Direct engine read/write
     *  (mirror of node/link tag wiring); the existing `changed()`
     *  signal + PropertiesPanel.objectEdited fan-out is sufficient for
     *  two-way sync with the Attribute Table since tag changes don't
     *  affect map symbology or attribute-table layout. */
    Q_PROPERTY(QString tag         READ tag         WRITE setTag         NOTIFY changed)
    Q_PROPERTY(double  area        READ area        WRITE setArea        NOTIFY changed)
    Q_PROPERTY(double  width       READ width       WRITE setWidth       NOTIFY changed)
    Q_PROPERTY(double  slope       READ slope       WRITE setSlope       NOTIFY changed)
    Q_PROPERTY(double  impervPct   READ impervPct   WRITE setImpervPct   NOTIFY changed)
    Q_PROPERTY(double  pctZeroImperv READ pctZeroImperv WRITE setPctZeroImperv NOTIFY changed)
    Q_PROPERTY(double  nImperv     READ nImperv     WRITE setNImperv     NOTIFY changed)
    Q_PROPERTY(double  nPerv       READ nPerv       WRITE setNPerv       NOTIFY changed)
    Q_PROPERTY(double  dsImperv    READ dsImperv    WRITE setDsImperv    NOTIFY changed)
    Q_PROPERTY(double  dsPerv      READ dsPerv      WRITE setDsPerv      NOTIFY changed)
    /*! Subcatchment rainfall scale factor — optional token 9 of [SUBCATCHMENTS].
     *  Multiplies gage-derived rainfall for this subcatchment only, composing
     *  multiplicatively with the gage's own scale factor. Must be > 0;
     *  default 1.0 (no scaling). */
    Q_PROPERTY(double  rainScaleFactor READ rainScaleFactor WRITE setRainScaleFactor NOTIFY changed)
    /*! Subcatchment snowfall scale factor — optional token 10 of
     *  [SUBCATCHMENTS]. Composes with the gage snow catch factor (SCF): SCF
     *  corrects the physical gage's snow-catch deficiency, this represents
     *  spatial variation across the catchment. Must be > 0; default 1.0. */
    Q_PROPERTY(double  snowScaleFactor READ snowScaleFactor WRITE setSnowScaleFactor NOTIFY changed)

    // Phase 3 — Subcatchment gaps (docs/ATTRIBUTE_EDITOR_WIRING_PLAN_2026-06-04.md).
    /*! Rain gage assignment (R3 picker over gage names). */
    Q_PROPERTY(DataObjectRef rainGage READ rainGageRef WRITE setRainGageRef NOTIFY changed)
    /*! Outlet target — a node OR another subcatchment (cascade). Combined
     *  picker; the WRITE slot resolves the name to the right engine call. */
    Q_PROPERTY(DataObjectRef outlet   READ outletRef   WRITE setOutletRef   NOTIFY changed)
    /*! Infiltration model (Horton / Mod-Horton / Green-Ampt / Mod-GA / CN). */
    Q_PROPERTY(InfilModel infilModel  READ infilModel  WRITE setInfilModel  NOTIFY changed)
    // Per-model parameter rows. Editability is gated by the live model in
    // PropertiesPanel (mirror of the storage Functional/Tabular greying).
    Q_PROPERTY(double hortonF0      READ hortonF0      WRITE setHortonF0      NOTIFY changed)
    Q_PROPERTY(double hortonFmin    READ hortonFmin    WRITE setHortonFmin    NOTIFY changed)
    Q_PROPERTY(double hortonDecay   READ hortonDecay   WRITE setHortonDecay   NOTIFY changed)
    Q_PROPERTY(double hortonDryTime READ hortonDryTime WRITE setHortonDryTime NOTIFY changed)
    Q_PROPERTY(double gaSuction      READ gaSuction      WRITE setGaSuction      NOTIFY changed)
    Q_PROPERTY(double gaConductivity READ gaConductivity WRITE setGaConductivity NOTIFY changed)
    Q_PROPERTY(double gaInitDeficit  READ gaInitDeficit  WRITE setGaInitDeficit  NOTIFY changed)
    Q_PROPERTY(double cnNumber       READ cnNumber       WRITE setCnNumber       NOTIFY changed)
    Q_PROPERTY(double cnDryTime      READ cnDryTime      WRITE setCnDryTime      NOTIFY changed)
    // Compound cells (open SubcatchCompoundEditDialog tabs).
    Q_PROPERTY(SubcatchCompoundEditRef landUse
               READ landUseRef     WRITE setLandUseRef     NOTIFY changed)
    Q_PROPERTY(SubcatchCompoundEditRef groundwater
               READ groundwaterRef WRITE setGroundwaterRef NOTIFY changed)
    Q_PROPERTY(SubcatchCompoundEditRef lidUsage
               READ lidUsageRef    WRITE setLidUsageRef    NOTIFY changed)
    Q_PROPERTY(SubcatchCompoundEditRef loadings
               READ loadingsRef    WRITE setLoadingsRef    NOTIFY changed)

    /*! Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
     *  user-flag assignments row (see SWMMNodePropertyAdapter). */
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)

    // Read-only post-run summary (no WRITE → non-editable). Mirrors the
    // Attribute Table's subcatchment dynamics columns; values follow the
    // bound stats source (see setStatsSource below).
    Q_PROPERTY(double statPrecip    READ statPrecip    NOTIFY changed)
    Q_PROPERTY(double statRunoffVol READ statRunoffVol NOTIFY changed)
    Q_PROPERTY(double statMaxRunoff READ statMaxRunoff NOTIFY changed)

public:
    /*! Infiltration model codes (engine [INFILTRATION] order). */
    enum InfilModel { Horton = 0, ModHorton = 1, GreenAmpt = 2,
                      ModGreenAmpt = 3, CurveNumber = 4 };
    Q_ENUM(InfilModel)

    SWMMSubcatchPropertyAdapter(SWMM_Engine engine, QString name,
                                  QObject *parent = nullptr);

    /*! USER_FLAGS Phase 4 — bind the owning layer so userFlagsRef() can
     *  reach the shared UserFlagsModel (mirror of
     *  SWMMNodePropertyAdapter::setModelLayer; nullptr-safe). */
    void setModelLayer(SWMMModelLayer *layer) { m_layer = layer; }
    [[nodiscard]] SWMMModelLayer *modelLayer() const { return m_layer; }

    [[nodiscard]] UserFlagsEditRef userFlagsRef() const;

    [[nodiscard]] QString name() const { return m_name; }
    [[nodiscard]] QString tag()  const;

    [[nodiscard]] double area()      const;
    [[nodiscard]] double width()     const;
    [[nodiscard]] double slope()     const;
    [[nodiscard]] double impervPct() const;
    [[nodiscard]] double pctZeroImperv() const;
    [[nodiscard]] double nImperv()   const;
    [[nodiscard]] double nPerv()     const;
    [[nodiscard]] double dsImperv()  const;
    [[nodiscard]] double dsPerv()    const;
    [[nodiscard]] double rainScaleFactor() const;
    [[nodiscard]] double snowScaleFactor() const;

    // Phase 3 — picker / enum / infiltration param accessors.
    [[nodiscard]] DataObjectRef rainGageRef() const;
    [[nodiscard]] DataObjectRef outletRef()   const;
    [[nodiscard]] InfilModel    infilModel()  const;
    [[nodiscard]] double hortonF0()      const;
    [[nodiscard]] double hortonFmin()    const;
    [[nodiscard]] double hortonDecay()   const;
    [[nodiscard]] double hortonDryTime() const;
    [[nodiscard]] double gaSuction()      const;
    [[nodiscard]] double gaConductivity() const;
    [[nodiscard]] double gaInitDeficit()  const;
    [[nodiscard]] double cnNumber()       const;
    [[nodiscard]] double cnDryTime()      const;
    [[nodiscard]] SubcatchCompoundEditRef landUseRef()     const;
    [[nodiscard]] SubcatchCompoundEditRef groundwaterRef() const;
    [[nodiscard]] SubcatchCompoundEditRef lidUsageRef()    const;
    [[nodiscard]] SubcatchCompoundEditRef loadingsRef()    const;

    // Read-only post-run summary getters. Zero until a run's results are
    // bound via setStatsSource (mirror of SWMMNodePropertyAdapter QA.2).
    [[nodiscard]] double statPrecip()    const;
    [[nodiscard]] double statRunoffVol() const;
    [[nodiscard]] double statMaxRunoff() const;

    /*! Stats-source dispatch — see SWMMNodePropertyAdapter (Slice QA.2)
     *  for the full contract. */
    void setStatsRegistry(openswmmvis::OutputStatsRegistry *registry);
    void setStatsSource(const QUuid &id);
    [[nodiscard]] QUuid statsSourceId() const { return m_statsSourceId; }

    /*! See SWMMNodePropertyAdapter::displayLabelFor. */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setName(const QString &newName);
    void setTag(const QString &t);
    void setArea(double v);
    void setWidth(double v);
    void setSlope(double v);
    void setImpervPct(double v);
    void setPctZeroImperv(double v);
    void setNImperv(double v);
    void setNPerv(double v);
    void setDsImperv(double v);
    void setDsPerv(double v);
    void setRainScaleFactor(double v);
    void setSnowScaleFactor(double v);

    // Phase 3 — picker / enum / infiltration param write slots.
    void setRainGageRef(const DataObjectRef &r);
    void setOutletRef(const DataObjectRef &r);
    void setInfilModel(InfilModel m);
    void setHortonF0(double v);
    void setHortonFmin(double v);
    void setHortonDecay(double v);
    void setHortonDryTime(double v);
    void setGaSuction(double v);
    void setGaConductivity(double v);
    void setGaInitDeficit(double v);
    void setCnNumber(double v);
    void setCnDryTime(double v);
    // Compound refs are coordinates only; the dialog performs the writes.
    void setLandUseRef(const SubcatchCompoundEditRef &)     { emit changed(); }
    void setGroundwaterRef(const SubcatchCompoundEditRef &) { emit changed(); }
    void setLidUsageRef(const SubcatchCompoundEditRef &)    { emit changed(); }
    void setLoadingsRef(const SubcatchCompoundEditRef &)    { emit changed(); }

    void setUserFlagsRef(const UserFlagsEditRef &) { emit changed(); }

    /*! Round-4 follow-up 2026-05-12 — see SWMMNodePropertyAdapter::refresh. */
    void refresh() { emit changed(); }

    /*! See SWMMNodePropertyAdapter::updateStoredName. */
    void updateStoredName(const QString &newName) { m_name = newName; }

signals:
    void changed();
    void renameRequested(const QString &oldName, const QString &newName);
    /*! See SWMMNodePropertyAdapter::displayLabelsChanged. */
    void displayLabelsChanged();

private:
    [[nodiscard]] int idx() const;
    /*! Curve Number and its drying time share one engine setter, so both
     *  Q_PROPERTY writers funnel through here after re-reading the sibling. */
    void writeCurveNumber(double cn, double dryTime);
    SWMM_Engine     m_engine;
    QString         m_name;
    SWMMModelLayer *m_layer = nullptr;   ///< USER_FLAGS Phase 4 — borrow.

    /// Stats-source dispatch state — see SWMMNodePropertyAdapter.
    QUuid                              m_statsSourceId;
    openswmmvis::OutputStatsRegistry  *m_statsRegistry = nullptr;
};

#endif // SWMMSUBCATCHPROPERTYADAPTER_H
