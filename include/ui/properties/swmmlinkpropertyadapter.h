/*!
 * \file   swmmlinkpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AG.3 — Property-tree adapter for SWMM links.  Mirrors the
 * per-node-type adapter design: a thin `Q_OBJECT` base over
 * `(engine, name)` exposing universal link attributes, plus one
 * subclass per LinkType with the Q_PROPERTYs that the corresponding
 * SWMM5 .inp section actually carries.  The Property Browser dock
 * picks the right subclass per identified link.
 */

#ifndef SWMMLINKPROPERTYADAPTER_H
#define SWMMLINKPROPERTYADAPTER_H

#include <QObject>
#include <QString>
#include <QUuid>                            // Stats-source identity (QA.2 mirror)

#include <openswmm/engine/openswmm_engine.h>

#include "ui/properties/culvertcoderef.h"        // ATTRIBUTE_EDITOR_WIRING Phase 0
#include "ui/properties/dataobjectref.h"
#include "ui/properties/initialqualityeditref.h" // Initial-quality UI round
#include "ui/properties/linkcompoundeditref.h"   // Slice SC.1
#include "ui/properties/userflagseditref.h"      // USER_FLAGS Phase 4

class SWMMModelLayer;

namespace openswmmvis { class OutputStatsRegistry; }    // QA.2 mirror

/*! Base link adapter — exposes the attributes that ALL link types
 *  share (name, kind, endpoints).  Per-type Q_PROPERTYs live on
 *  subclasses below so the Property Browser only shows attributes
 *  applicable to the identified link's kind. */
class SWMMLinkPropertyAdapter : public QObject
{
    Q_OBJECT

public:
    /*! Type-safe enums declared with Q_ENUM so QPropertyModel
     *  renders the key name ("CONDUIT", "ON", "TRANSVERSE") instead
     *  of the raw integer.  Subclasses inherit these via
     *  metaObject() — no need to redeclare. */
    enum LinkKind     { Conduit = 0, Pump = 1, Orifice = 2, Weir = 3, Outlet = 4 };
    enum FlapGate     { NO = 0, YES = 1 };
    enum PumpInitState { OFF = 0, ON = 1 };
    // Slice SD partial — orifice flow-attack classification. Mirrors
    // SWMM_OrificeType in openswmm_links.h:34 and the legacy SWMM-GUI
    // combo order in SWMM-GUI/Epaswmm5/objprops.txt:862.
    enum OrificeType  { SIDE = 0, BOTTOM = 1 };
    // Slice SD partial (BN-LINK-03) — weir flow classification. Mirrors
    // SWMM_WeirType in openswmm_links.h and the legacy combo at
    // SWMM-GUI/Epaswmm5/objprops.txt:160. The companion "Shape" combo
    // is derived from the type (see objprops.txt:162) — no separate
    // Q_ENUM needed on the GUI side.
    enum WeirType {
        TRANSVERSE  = 0,
        SIDEFLOW    = 1,
        VNOTCH      = 2,
        TRAPEZOIDAL = 3,
        ROADWAY     = 4,
    };
    // Slice SD partial (BN-LINK-04) — outlet rating-curve classification.
    // Mirrors SWMM_OutletRatingType in openswmm_links.h and the legacy
    // combo at SWMM-GUI/Epaswmm5/objprops.txt:913. The four-value
    // FUNCTIONAL×{HEAD,DEPTH} / TABULAR×{HEAD,DEPTH} grid drives
    // conditional visibility of the exponent / curve picker rows.
    enum OutletRatingType {
        FUNCTIONAL_HEAD  = 0,
        FUNCTIONAL_DEPTH = 1,
        TABULAR_HEAD     = 2,
        TABULAR_DEPTH    = 3,
    };
    Q_ENUM(LinkKind)
    Q_ENUM(FlapGate)
    Q_ENUM(PumpInitState)
    Q_ENUM(OrificeType)
    Q_ENUM(WeirType)
    Q_ENUM(OutletRatingType)

    // Common to every link type.
    Q_PROPERTY(QString  name         READ name  WRITE setName)
    Q_PROPERTY(LinkKind linkKind     READ linkKind)
    Q_PROPERTY(QString  fromNode     READ fromNode)
    Q_PROPERTY(QString  toNode       READ toNode)
    /*! Slice SA — Free-form `[TAGS]` label (mirrors
     *  `SWMMNodePropertyAdapter::tag`). Editable text; index-keyed in the
     *  engine, so it survives a rename. Subclasses must declare a matching
     *  `Q_PROPERTY` row to actually expose the field to QPropertyModel —
     *  declaring the WRITE+READ pair here keeps QML / scripting clients
     *  happy without forcing the inline cell on every link kind. */
    Q_PROPERTY(QString  tag          READ tag   WRITE setTag   NOTIFY changed)

    SWMMLinkPropertyAdapter(SWMM_Engine engine, QString name,
                              QObject *parent = nullptr);

    /*! Slice BM.0-Browse-Edit — bind the active project's model layer
     *  so DataObjectRef-typed properties (`pumpCurve`) can construct
     *  refs with the right layer pointer. Mirror of
     *  `SWMMNodePropertyAdapter::setModelLayer`. */
    void setModelLayer(SWMMModelLayer *layer) { m_layer = layer; }
    [[nodiscard]] SWMMModelLayer *modelLayer() const { return m_layer; }

    [[nodiscard]] QString  name() const     { return m_name; }
    [[nodiscard]] LinkKind linkKind() const;
    [[nodiscard]] QString  fromNode() const;
    [[nodiscard]] QString  toNode() const;
    [[nodiscard]] QString  tag()      const;   ///< Slice SA — `[TAGS]` value.

    [[nodiscard]] double length()           const;
    [[nodiscard]] double roughness()        const;
    [[nodiscard]] double offsetUp()         const;
    [[nodiscard]] double offsetDn()         const;
    [[nodiscard]] double crestHeight()      const;
    [[nodiscard]] double dischargeCoeff()   const;
    [[nodiscard]] double endContractions()  const;
    [[nodiscard]] FlapGate      flapGate()      const;
    [[nodiscard]] PumpInitState pumpInitState() const;
    [[nodiscard]] QString       pumpCurveName() const;
    [[nodiscard]] OrificeType      orificeType()      const;  ///< Slice SD partial
    [[nodiscard]] WeirType         weirType()         const;  ///< Slice SD partial
    [[nodiscard]] OutletRatingType outletRatingType() const;  ///< Slice SD partial
    [[nodiscard]] double           outletExpon()      const;  ///< Slice SD partial
    [[nodiscard]] double           pumpStartupDepth() const;  ///< BN-LINK-05
    [[nodiscard]] double           pumpShutoffDepth() const;  ///< BN-LINK-05
    [[nodiscard]] double           orificeOpenCloseRate() const;  ///< BN-LINK-06

    // Slice SB — conduit scalar parity (matches legacy ConduitProps in
    // SWMM-GUI/Epaswmm5/objprops.txt rows 12-17, 21). All accessors live
    // on the base so the GETTER_D macro can be reused; only the
    // SWMMConduitPropertyAdapter subclass exposes them as Q_PROPERTY rows
    // because pumps / orifices / weirs / outlets don't carry these
    // fields on the legacy editor side.
    [[nodiscard]] double initialFlow()      const;   ///< BN-LINK-01a getter
    [[nodiscard]] double maxFlow()          const;   ///< BN-LINK-01b getter
    [[nodiscard]] double lossInlet()        const;   ///< swmm_link_get_loss_coeff[inlet]
    [[nodiscard]] double lossOutlet()       const;   ///< swmm_link_get_loss_coeff[outlet]
    [[nodiscard]] double lossAvg()          const;   ///< swmm_link_get_loss_coeff[avg]
    [[nodiscard]] double seepRate()         const;   ///< swmm_link_get_seep_rate
    [[nodiscard]] int    barrels()          const;   ///< swmm_link_get_barrels

    // Direct inline cross-section geometry — geom1..geom4 of the engine
    // xsect tuple, exposed alongside the compound `xsection` editor so the
    // user can tweak a single dimension without opening the dialog. The
    // engine has no per-geom setter, so each setter read-modify-writes the
    // whole tuple (mirrors the loss-coeff slots). `xsectShapeId` lets the
    // Property Browser grey out geoms that don't apply to the current
    // shape (see openswmmvis::xsectGeomApplies).
    [[nodiscard]] int    xsectShapeId() const;       ///< current SWMM_XSECT_* id
    [[nodiscard]] double xsectGeom1()   const;
    [[nodiscard]] double xsectGeom2()   const;
    [[nodiscard]] double xsectGeom3()   const;
    [[nodiscard]] double xsectGeom4()   const;

    // Read-only post-run summary statistics (mirror of the node adapter's
    // Slice QA.2 block and the Attribute Table's link dynamics columns).
    // Zero until a run's results are bound via setStatsSource; the three
    // pump utilisation getters are meaningful only on pumps.
    [[nodiscard]] double statMaxFlow()       const;
    [[nodiscard]] double statMaxVelocity()   const;
    [[nodiscard]] double statMaxFilling()    const;
    [[nodiscard]] double statVolFlow()       const;
    [[nodiscard]] double statSurchargeTime() const;
    [[nodiscard]] double statPumpCycles()    const;
    [[nodiscard]] double statPumpOnTime()    const;
    [[nodiscard]] double statPumpVolume()    const;

    /*! Slice BM.0-Browse-Edit — `DataObjectRef`-typed accessor for the
     *  pump curve, used by the `Q_PROPERTY` on `SWMMPumpPropertyAdapter`.
     *  Constructs the ref with `kind=AnyCurve` (pump curves accept
     *  several engine curve types). Empty `currentName` means no curve
     *  is assigned yet (the engine returns curveIdx == -1). */
    [[nodiscard]] DataObjectRef pumpCurveRef() const;

    /*! Slice SC.1 — Compound-attribute refs for the link-side Property
     *  Browser cells (cross section / inlet usage). Each builds the
     *  engine summary live so the cell shows a useful preview even
     *  before the user opens the dialog. */
    [[nodiscard]] LinkCompoundEditRef xsectionRef()    const;
    [[nodiscard]] LinkCompoundEditRef inletUsageRef()  const;

    /*! ATTRIBUTE_EDITOR_WIRING Phase 0 — culvert code is a single
     *  HDS-5 enum value, edited inline via `CulvertCodeComboBox`
     *  (no separate dialog). */
    [[nodiscard]] CulvertCodeRef culvertCodeRef() const;

    /*! Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
     *  user-flag assignments row (see SWMMNodePropertyAdapter). */
    [[nodiscard]] UserFlagsEditRef userFlagsRef() const;

    /*! Initial-quality UI round — per-element [INITIAL_QUALITY] overrides
     *  row (see SWMMNodePropertyAdapter). */
    [[nodiscard]] InitialQualityEditRef initialQualityRef() const;

    /*! See SWMMNodePropertyAdapter::displayLabelFor — same contract,
     *  returns "" for unknown property names. */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setName(const QString &newName);
    void setTag(const QString &t);              ///< Slice SA
    void setLength(double v);
    void setRoughness(double v);
    void setOffsetUp(double v);
    void setOffsetDn(double v);
    void setCrestHeight(double v);
    void setDischargeCoeff(double v);
    void setEndContractions(double v);
    void setFlapGate(FlapGate v);
    void setPumpInitState(PumpInitState v);
    void setOrificeType(OrificeType v);            ///< Slice SD partial
    void setWeirType(WeirType v);                  ///< Slice SD partial
    void setOutletRatingType(OutletRatingType v);  ///< Slice SD partial
    void setOutletExpon(double v);                 ///< Slice SD partial
    void setPumpStartupDepth(double v);            ///< BN-LINK-05
    void setPumpShutoffDepth(double v);            ///< BN-LINK-05
    void setOrificeOpenCloseRate(double v);        ///< BN-LINK-06
    // Slice SB — conduit scalar setters. Loss-coefficient setters read
    // the other two coefficients from the engine first then write the
    // full triple via `swmm_link_set_loss_coeff` (atomic from a caller's
    // view); the three rows look independent in the Property Browser
    // but the three-coeff write is one engine call.
    void setInitialFlow(double v);
    void setMaxFlow(double v);
    void setLossInlet(double v);
    void setLossOutlet(double v);
    void setLossAvg(double v);
    void setSeepRate(double v);
    void setBarrels(int v);
    // Inline cross-section geom setters — read-modify-write the engine
    // xsect tuple (shape + the other three geoms preserved). No-op when
    // the geom doesn't apply to the current shape, so a stray write from
    // a greyed cell can't corrupt the section.
    void setXsectGeom1(double v);
    void setXsectGeom2(double v);
    void setXsectGeom3(double v);
    void setXsectGeom4(double v);
    /*! Slice BM.0-Browse-Edit — writes the selected curve via
     *  `swmm_link_set_pump_curve`. Looks up the engine curve index from
     *  `ref.currentName`; no-op if empty or unknown. */
    void setPumpCurveRef(const DataObjectRef &ref);

    /*! Slice SC.1 — Compound-ref write slots. The cell's
     *  `LinkCompoundEditButton` invokes these via the delegate's
     *  setModelData round-trip after the dialog closes. The actual
     *  engine mutations already happened inside the dialog (the
     *  Apply-as-you-go pages route through
     *  `SWMMModelLayer::applyLinkXsect` / `_Barrels` / `_CulvertCode`),
     *  so these slots are no-ops except for emitting `changed()` so the
     *  property tree (and the attribute table) re-reads its summary.
     *  Their existence is what flips `QMetaProperty::isWritable()` to
     *  true, which is what `QVariantPropertyItem` checks to mark the
     *  cell editable — without these, the cell never enters edit mode
     *  and the registered `LinkCompoundEditButton` creator never fires. */
    void setXsectionRef(const LinkCompoundEditRef &)    { emit changed(); }
    void setUserFlagsRef(const UserFlagsEditRef &)      { emit changed(); }
    void setInitialQualityRef(const InitialQualityEditRef &) { emit changed(); }
    void setCulvertCodeRef(const CulvertCodeRef &)      { emit changed(); }
    void setInletUsageRef(const LinkCompoundEditRef &)  { emit changed(); }

    /*! Round-4 follow-up — see SWMMNodePropertyAdapter::refresh. */
    void refresh() { emit changed(); }

    /*! See SWMMNodePropertyAdapter::updateStoredName. */
    void updateStoredName(const QString &newName) { m_name = newName; }

    /*! Stats-source dispatch — see SWMMNodePropertyAdapter (Slice QA.2)
     *  for the full contract. The stat* getters read the editing engine's
     *  ambient stats while the id is null, and the registry-resolved
     *  SWMMResultsLayer's linkStat*() accessors otherwise. */
    void setStatsRegistry(openswmmvis::OutputStatsRegistry *registry);
    void setStatsSource(const QUuid &id);
    [[nodiscard]] QUuid statsSourceId() const { return m_statsSourceId; }

signals:
    void changed();
    void renameRequested(const QString &oldName, const QString &newName);
    /*! See SWMMNodePropertyAdapter::displayLabelsChanged. */
    void displayLabelsChanged();

protected:
    [[nodiscard]] int linkIdx() const;
    //! Read-modify-write one geom (ordinal 1..4) of the xsect tuple; no-op
    //! when the geom doesn't apply to the current shape. Emits changed().
    void writeXsectGeom(int ordinal, double v);
    SWMM_Engine     m_engine;
    QString         m_name;
    SWMMModelLayer *m_layer = nullptr;  ///< Bound via setModelLayer.

    /// Stats-source dispatch state — see SWMMNodePropertyAdapter.
    QUuid                              m_statsSourceId;
    openswmmvis::OutputStatsRegistry  *m_statsRegistry = nullptr;
};

/*! Conduit adapter — `[CONDUITS]` columns:
 *  Name, FromNode, ToNode, Length, Roughness, InOffset, OutOffset,
 *  InitFlow, MaxFlow, EntryLoss / ExitLoss / AvgLoss, Seepage Rate,
 *  Barrels, Flap Gate, Culvert Code. Slice SB (2026-05-25) added the
 *  loss-coeff / seepage / barrels / init-flow / max-flow scalar rows
 *  via the BN-LINK-01a/b engine-getter pair; the cross-section + inlet
 *  usage + culvert-code compound cells land in Slice SC. */
class SWMMConduitPropertyAdapter : public SWMMLinkPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double length     READ length     WRITE setLength    NOTIFY changed)
    Q_PROPERTY(double roughness  READ roughness  WRITE setRoughness NOTIFY changed)
    Q_PROPERTY(double offsetUp   READ offsetUp   WRITE setOffsetUp  NOTIFY changed)
    Q_PROPERTY(double offsetDn   READ offsetDn   WRITE setOffsetDn  NOTIFY changed)
    // Slice SB — conduit scalar parity rows. Loss coefficients are
    // exposed as three separate Q_PROPERTYs per §S.1 Q-S4 decision.
    Q_PROPERTY(double initialFlow READ initialFlow WRITE setInitialFlow NOTIFY changed)
    Q_PROPERTY(double maxFlow     READ maxFlow     WRITE setMaxFlow     NOTIFY changed)
    Q_PROPERTY(double lossInlet   READ lossInlet   WRITE setLossInlet   NOTIFY changed)
    Q_PROPERTY(double lossOutlet  READ lossOutlet  WRITE setLossOutlet  NOTIFY changed)
    Q_PROPERTY(double lossAvg     READ lossAvg     WRITE setLossAvg     NOTIFY changed)
    Q_PROPERTY(double seepRate    READ seepRate    WRITE setSeepRate    NOTIFY changed)
    Q_PROPERTY(int    barrels     READ barrels     WRITE setBarrels     NOTIFY changed)
    Q_PROPERTY(SWMMLinkPropertyAdapter::FlapGate flapGate
               READ flapGate WRITE setFlapGate NOTIFY changed)
    // Slice SC.1 — compound-attribute "Edit…" cells.
    Q_PROPERTY(LinkCompoundEditRef xsection
               READ xsectionRef    WRITE setXsectionRef    NOTIFY changed)
    // Direct inline geom1..geom4 (generic labels; PropertiesPanel greys
    // out the ones that don't apply to the current shape).
    Q_PROPERTY(double geom1 READ xsectGeom1 WRITE setXsectGeom1 NOTIFY changed)
    Q_PROPERTY(double geom2 READ xsectGeom2 WRITE setXsectGeom2 NOTIFY changed)
    Q_PROPERTY(double geom3 READ xsectGeom3 WRITE setXsectGeom3 NOTIFY changed)
    Q_PROPERTY(double geom4 READ xsectGeom4 WRITE setXsectGeom4 NOTIFY changed)
    Q_PROPERTY(CulvertCodeRef culvertCode
               READ culvertCodeRef WRITE setCulvertCodeRef NOTIFY changed)
    Q_PROPERTY(LinkCompoundEditRef inletUsage
               READ inletUsageRef  WRITE setInletUsageRef  NOTIFY changed)
    // Read-only post-run summary (no WRITE → non-editable). Mirrors the
    // Attribute Table's link dynamics columns; values follow the bound
    // stats source (see the base class).
    Q_PROPERTY(double statMaxFlow       READ statMaxFlow       NOTIFY changed)
    Q_PROPERTY(double statMaxVelocity   READ statMaxVelocity   NOTIFY changed)
    Q_PROPERTY(double statMaxFilling    READ statMaxFilling    NOTIFY changed)
    Q_PROPERTY(double statVolFlow       READ statVolFlow       NOTIFY changed)
    Q_PROPERTY(double statSurchargeTime READ statSurchargeTime NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality
               READ initialQualityRef WRITE setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

/*! Pump adapter — `[PUMPS]` columns:
 *  Name, FromNode, ToNode, PumpCurve, Status, Startup, Shutoff.
 *  Slice SD partial (BN-LINK-05, 2026-05-25) — startup/shutoff depth
 *  rows now round-trip via the new engine accessors. */
class SWMMPumpPropertyAdapter : public SWMMLinkPropertyAdapter
{
    Q_OBJECT
    // Slice BM.0-Browse-Edit (2026-05-25) — typed as DataObjectRef so the
    // QPropertyItemDelegate hands out the picker editor (combo + browse
    // button + right-click menu). Engine setter is swmm_link_set_pump_curve.
    Q_PROPERTY(DataObjectRef pumpCurve
               READ pumpCurveRef WRITE setPumpCurveRef NOTIFY changed)
    Q_PROPERTY(SWMMLinkPropertyAdapter::PumpInitState initState
               READ pumpInitState WRITE setPumpInitState NOTIFY changed)
    Q_PROPERTY(double startupDepth
               READ pumpStartupDepth WRITE setPumpStartupDepth NOTIFY changed)
    Q_PROPERTY(double shutoffDepth
               READ pumpShutoffDepth WRITE setPumpShutoffDepth NOTIFY changed)
    // Read-only post-run summary — the shared link block plus the pump
    // utilisation trio (Attribute Table parity; see the base class).
    Q_PROPERTY(double statMaxFlow       READ statMaxFlow       NOTIFY changed)
    Q_PROPERTY(double statMaxVelocity   READ statMaxVelocity   NOTIFY changed)
    Q_PROPERTY(double statMaxFilling    READ statMaxFilling    NOTIFY changed)
    Q_PROPERTY(double statVolFlow       READ statVolFlow       NOTIFY changed)
    Q_PROPERTY(double statSurchargeTime READ statSurchargeTime NOTIFY changed)
    Q_PROPERTY(double statPumpCycles    READ statPumpCycles    NOTIFY changed)
    Q_PROPERTY(double statPumpOnTime    READ statPumpOnTime    NOTIFY changed)
    Q_PROPERTY(double statPumpVolume    READ statPumpVolume    NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality
               READ initialQualityRef WRITE setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

/*! Orifice adapter — `[ORIFICES]` columns:
 *  Name, FromNode, ToNode, Type, Offset, Cd, Gated.  Type
 *  (SIDE/BOTTOM) has no engine accessor today; deferred to AG.4.
 *  Slice SC.1 (extended 2026-05-25) — adds the cross-section cell,
 *  shape combo filtered by the dialog to CIRCULAR / RECT_CLOSED only
 *  (the two legacy orifice shapes per `objprops.txt:866`). */
class SWMMOrificePropertyAdapter : public SWMMLinkPropertyAdapter
{
    Q_OBJECT
    // Slice SD partial (2026-05-25) — orifice TYPE row, engine-backed via
    // BN-LINK-02 (swmm_link_get/set_orifice_type). Sits at the top of the
    // form matching legacy OrificeProps[5] row order.
    Q_PROPERTY(SWMMLinkPropertyAdapter::OrificeType orificeType
               READ orificeType WRITE setOrificeType NOTIFY changed)
    Q_PROPERTY(double offset         READ offsetUp        WRITE setOffsetUp        NOTIFY changed)
    Q_PROPERTY(double dischargeCoeff READ dischargeCoeff  WRITE setDischargeCoeff  NOTIFY changed)
    Q_PROPERTY(SWMMLinkPropertyAdapter::FlapGate flapGate
               READ flapGate WRITE setFlapGate NOTIFY changed)
    // Slice SD partial (BN-LINK-06) — orifice open/close rate (1/s).
    // 0 = instantaneous. Legacy SWMM-GUI surfaces this as "Time to
    // Open/Close (hr)" — clients that want the hours UX should
    // convert before/after calling the setter.
    Q_PROPERTY(double openCloseRate
               READ orificeOpenCloseRate WRITE setOrificeOpenCloseRate NOTIFY changed)
    Q_PROPERTY(LinkCompoundEditRef xsection
               READ xsectionRef WRITE setXsectionRef NOTIFY changed)
    // Direct inline geom1..geom4 (orifices use CIRCULAR / RECT_CLOSED, so
    // only geom1/geom2 ever apply; the rest grey out).
    Q_PROPERTY(double geom1 READ xsectGeom1 WRITE setXsectGeom1 NOTIFY changed)
    Q_PROPERTY(double geom2 READ xsectGeom2 WRITE setXsectGeom2 NOTIFY changed)
    Q_PROPERTY(double geom3 READ xsectGeom3 WRITE setXsectGeom3 NOTIFY changed)
    Q_PROPERTY(double geom4 READ xsectGeom4 WRITE setXsectGeom4 NOTIFY changed)
    // Read-only post-run summary (Attribute Table parity; see base class).
    Q_PROPERTY(double statMaxFlow       READ statMaxFlow       NOTIFY changed)
    Q_PROPERTY(double statMaxVelocity   READ statMaxVelocity   NOTIFY changed)
    Q_PROPERTY(double statMaxFilling    READ statMaxFilling    NOTIFY changed)
    Q_PROPERTY(double statVolFlow       READ statVolFlow       NOTIFY changed)
    Q_PROPERTY(double statSurchargeTime READ statSurchargeTime NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality
               READ initialQualityRef WRITE setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

/*! Weir adapter — `[WEIRS]` columns:
 *  Name, FromNode, ToNode, Type, CrestHt, Cd, Gated, EndCon,
 *  EndCoeff.  Type (TRANSVERSE/SIDEFLOW/V-NOTCH/TRAPEZOIDAL) and
 *  EndCoeff have no engine accessors today; deferred to AG.4.
 *  Slice SC.1 (extended 2026-05-25) — adds the cross-section cell,
 *  shape combo filtered by the dialog to the four legal weir
 *  cross-sections (RECT_OPEN, TRAPEZOIDAL, TRIANGULAR for V-NOTCH,
 *  RECT_OPEN again for ROADWAY) per legacy `objprops.txt:875`. */
class SWMMWeirPropertyAdapter : public SWMMLinkPropertyAdapter
{
    Q_OBJECT
    // Slice SD partial (BN-LINK-03, 2026-05-25) — weir TYPE row sits at
    // the top of the form matching legacy WeirProps[5] row order.
    Q_PROPERTY(SWMMLinkPropertyAdapter::WeirType weirType
               READ weirType WRITE setWeirType NOTIFY changed)
    Q_PROPERTY(double offsetUp        READ offsetUp        WRITE setOffsetUp        NOTIFY changed)
    Q_PROPERTY(double offsetDn        READ offsetDn        WRITE setOffsetDn        NOTIFY changed)
    Q_PROPERTY(double crestHeight     READ crestHeight     WRITE setCrestHeight     NOTIFY changed)
    Q_PROPERTY(double dischargeCoeff  READ dischargeCoeff  WRITE setDischargeCoeff  NOTIFY changed)
    Q_PROPERTY(double endContractions READ endContractions WRITE setEndContractions NOTIFY changed)
    Q_PROPERTY(SWMMLinkPropertyAdapter::FlapGate flapGate
               READ flapGate WRITE setFlapGate NOTIFY changed)
    Q_PROPERTY(LinkCompoundEditRef xsection
               READ xsectionRef WRITE setXsectionRef NOTIFY changed)
    // Direct inline geom1..geom4 (weirs use RECT_OPEN / TRAPEZOIDAL /
    // TRIANGULAR; inapplicable geoms grey out per shape).
    Q_PROPERTY(double geom1 READ xsectGeom1 WRITE setXsectGeom1 NOTIFY changed)
    Q_PROPERTY(double geom2 READ xsectGeom2 WRITE setXsectGeom2 NOTIFY changed)
    Q_PROPERTY(double geom3 READ xsectGeom3 WRITE setXsectGeom3 NOTIFY changed)
    Q_PROPERTY(double geom4 READ xsectGeom4 WRITE setXsectGeom4 NOTIFY changed)
    // Read-only post-run summary (Attribute Table parity; see base class).
    Q_PROPERTY(double statMaxFlow       READ statMaxFlow       NOTIFY changed)
    Q_PROPERTY(double statMaxVelocity   READ statMaxVelocity   NOTIFY changed)
    Q_PROPERTY(double statMaxFilling    READ statMaxFilling    NOTIFY changed)
    Q_PROPERTY(double statVolFlow       READ statVolFlow       NOTIFY changed)
    Q_PROPERTY(double statSurchargeTime READ statSurchargeTime NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality
               READ initialQualityRef WRITE setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

/*! Outlet adapter — `[OUTLETS]` columns:
 *  Name, FromNode, ToNode, Offset, Type, Coeff, Expon.
 *  Slice SD partial (BN-LINK-04, 2026-05-25) — adds the rating-type
 *  combo + functional-form coefficient/exponent + tabular curve picker
 *  rows; the four FUNCTIONAL/TABULAR × HEAD/DEPTH values gate
 *  conditional visibility of coeff/expon vs. the curve picker. */
class SWMMOutletPropertyAdapter : public SWMMLinkPropertyAdapter
{
    Q_OBJECT
    // Slice SD partial — rating-curve TYPE sits at the top to match
    // legacy OutletProps[7] row order.
    Q_PROPERTY(SWMMLinkPropertyAdapter::OutletRatingType ratingType
               READ outletRatingType WRITE setOutletRatingType NOTIFY changed)
    Q_PROPERTY(double offset         READ offsetUp        WRITE setOffsetUp        NOTIFY changed)
    // Functional-form coefficient (legacy "Coefficient" row) reuses
    // the shared discharge-coeff scalar — same engine field (cd) used
    // by orifices and weirs.
    Q_PROPERTY(double coefficient    READ dischargeCoeff  WRITE setDischargeCoeff  NOTIFY changed)
    Q_PROPERTY(double expon          READ outletExpon     WRITE setOutletExpon     NOTIFY changed)
    // Tabular curve picker reuses the existing DataObjectRef pump-curve
    // accessor (the engine shares the curve-index slot between pumps
    // and tabular outlets per `LinksHandler::handle_outlets:229`).
    Q_PROPERTY(DataObjectRef outletCurve
               READ pumpCurveRef WRITE setPumpCurveRef NOTIFY changed)
    Q_PROPERTY(SWMMLinkPropertyAdapter::FlapGate flapGate
               READ flapGate WRITE setFlapGate NOTIFY changed)
    // Read-only post-run summary (Attribute Table parity; see base class).
    Q_PROPERTY(double statMaxFlow       READ statMaxFlow       NOTIFY changed)
    Q_PROPERTY(double statMaxVelocity   READ statMaxVelocity   NOTIFY changed)
    Q_PROPERTY(double statMaxFilling    READ statMaxFilling    NOTIFY changed)
    Q_PROPERTY(double statVolFlow       READ statVolFlow       NOTIFY changed)
    Q_PROPERTY(double statSurchargeTime READ statSurchargeTime NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality
               READ initialQualityRef WRITE setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

#endif // SWMMLINKPROPERTYADAPTER_H
