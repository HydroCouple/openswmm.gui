/**
 * @file      swmmlinkpropertyadapter.h
 * @brief     Property-tree adapter for SWMM links.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice AG.3 — Property-tree adapter for SWMM links.  Mirrors the
 * per-node-type adapter design: a thin `Q_OBJECT` base over
 * `(engine, name)` exposing universal link attributes, plus one
 * subclass per LinkType with the Q_PROPERTYs that the corresponding
 * SWMM5 .inp section actually carries.  The Property Browser dock
 * picks the right subclass per identified link.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMLINKPROPERTYADAPTER_H
#define SWMMLINKPROPERTYADAPTER_H

#include <QObject>
#include <QString>
#include <QUuid> // Stats-source identity (QA.2 mirror)

#include <openswmm/engine/openswmm_engine.h>

#include "ui/properties/culvertcoderef.h" // ATTRIBUTE_EDITOR_WIRING Phase 0
#include "ui/properties/dataobjectref.h"
#include "ui/properties/linkendpointref.h"
#include "ui/properties/initialqualityeditref.h" // Initial-quality UI round
#include "ui/properties/linkcompoundeditref.h"   // Slice SC.1
#include "ui/properties/userflagseditref.h"      // USER_FLAGS Phase 4

class SWMMModelLayer;

namespace openswmmvis
{
class OutputStatsRegistry;
} // QA.2 mirror

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
    enum LinkKind { Conduit = 0, Pump = 1, Orifice = 2, Weir = 3, Outlet = 4 };
    enum FlapGate { NO = 0, YES = 1 };
    enum PumpInitState { OFF = 0, ON = 1 };
    // Slice SD partial — orifice flow-attack classification. Mirrors
    // SWMM_OrificeType in openswmm_links.h:34 and the legacy SWMM-GUI
    // combo order in SWMM-GUI/Epaswmm5/objprops.txt:862.
    enum OrificeType { SIDE = 0, BOTTOM = 1 };
    // Slice SD partial (BN-LINK-03) — weir flow classification. Mirrors
    // SWMM_WeirType in openswmm_links.h and the legacy combo at
    // SWMM-GUI/Epaswmm5/objprops.txt:160. The companion "Shape" combo
    // is derived from the type (see objprops.txt:162) — no separate
    // Q_ENUM needed on the GUI side.
    enum WeirType {
        TRANSVERSE = 0,
        SIDEFLOW = 1,
        VNOTCH = 2,
        TRAPEZOIDAL = 3,
        ROADWAY = 4,
    };
    // Slice SD partial (BN-LINK-04) — outlet rating-curve classification.
    // Mirrors SWMM_OutletRatingType in openswmm_links.h and the legacy
    // combo at SWMM-GUI/Epaswmm5/objprops.txt:913. The four-value
    // FUNCTIONAL×{HEAD,DEPTH} / TABULAR×{HEAD,DEPTH} grid drives
    // conditional visibility of the exponent / curve picker rows.
    enum OutletRatingType {
        FUNCTIONAL_HEAD = 0,
        FUNCTIONAL_DEPTH = 1,
        TABULAR_HEAD = 2,
        TABULAR_DEPTH = 3,
    };
    Q_ENUM(LinkKind)
    Q_ENUM(FlapGate)
    Q_ENUM(PumpInitState)
    Q_ENUM(OrificeType)
    Q_ENUM(WeirType)
    Q_ENUM(OutletRatingType)

    enum LidAnchorPosition { NoLayerAnchor = -1, LayerBottom = 0, LayerTop = 1 };
    Q_ENUM(LidAnchorPosition)
    Q_PROPERTY(int lidOutletLayer READ lidOutletLayer WRITE setLidOutletLayer NOTIFY changed)
    Q_PROPERTY(LidAnchorPosition lidOutletPosition READ lidOutletPosition WRITE setLidOutletPosition
                   NOTIFY changed)
    /**
     * @return One-based storage-LID layer anchor, or zero when unanchored/read fails.
     */
    int lidOutletLayer() const;
    /**
     * @return LayerTop/LayerBottom when a positive layer is anchored; NoLayerAnchor otherwise.
     */
    LidAnchorPosition lidOutletPosition() const;
    /**
     * @brief Set a link outlet's storage-LID layer anchor while preserving its top/bottom choice.
     * @param[in] layer One-based LID layer; zero clears anchoring, subject to engine validation.
     * @details Engine acceptance emits changed; failures have no separate result.
     */
    void setLidOutletLayer(int layer);
    /**
     * @brief Change which surface of the current storage-LID layer anchors the outlet.
     * @param[in] position LayerTop/LayerBottom retains the selected layer; NoLayerAnchor writes layer zero.
     * @details Delegates to the engine and emits changed only on acceptance.
     */
    void setLidOutletPosition(LidAnchorPosition position);

    // Common to every link type.
    Q_PROPERTY(QString name READ name WRITE setName)
    Q_PROPERTY(LinkKind linkKind READ linkKind)
    Q_PROPERTY(LinkEndpointRef fromNode READ fromNodeRef WRITE setFromNodeRef NOTIFY changed)
    Q_PROPERTY(LinkEndpointRef toNode READ toNodeRef WRITE setToNodeRef NOTIFY changed)
    /*! Slice SA — Free-form `[TAGS]` label (mirrors
     *  `SWMMNodePropertyAdapter::tag`). Editable text; index-keyed in the
     *  engine, so it survives a rename. Subclasses must declare a matching
     *  `Q_PROPERTY` row to actually expose the field to QPropertyModel —
     *  declaring the WRITE+READ pair here keeps QML / scripting clients
     *  happy without forcing the inline cell on every link kind. */
    Q_PROPERTY(QString tag READ tag WRITE setTag NOTIFY changed)

    /**
     * @brief Bind live properties and output summaries to a named link.
     * @param[in] engine Borrowed engine that must remain valid while this adapter is used.
     * @param[in] name Case-preserved object ID copied into the adapter; indexes are resolved afresh.
     * @param[in] parent Optional Qt owner; construct and use on the GUI thread.
     * @details Forwards active-project unit changes as displayLabelsChanged; does not own the engine or create model data.
     */
    SWMMLinkPropertyAdapter(SWMM_Engine engine, QString name, QObject *parent = nullptr);

    /*! Slice BM.0-Browse-Edit — bind the active project's model layer
     *  so DataObjectRef-typed properties (`pumpCurve`) can construct
     *  refs with the right layer pointer. Mirror of
     *  `SWMMNodePropertyAdapter::setModelLayer`.
     * @param[in] layer Borrowed layer for shared stores, picker flows and layer-mediated edits; nullptr detaches. This raw pointer must outlive adapter use.
     */
    void setModelLayer(SWMMModelLayer *layer) { m_layer = layer; }
    /**
     * @return Borrowed currently bound model layer, or nullptr; no lifetime extension occurs.
     */
    [[nodiscard]] SWMMModelLayer *modelLayer() const { return m_layer; }

    /**
     * @return Stored object ID, independent of whether it still resolves in the engine.
     */
    [[nodiscard]] QString name() const { return m_name; }
    /**
     * @return Engine link-kind enum, default Conduit on unresolved/read failure.
     */
    [[nodiscard]] LinkKind linkKind() const;
    /**
     * @return Upstream node ID resolved from current link connectivity, or empty when missing/read fails.
     */
    [[nodiscard]] QString fromNode() const;
    /**
     * @return Downstream node ID resolved from current connectivity, or empty when missing/read fails.
     */
    [[nodiscard]] QString toNode() const;
    LinkEndpointRef fromNodeRef() const { return {m_engine, m_name, true, fromNode()}; }
    LinkEndpointRef toNodeRef() const { return {m_engine, m_name, false, toNode()}; }
    void setFromNodeRef(const LinkEndpointRef &ref);
    void setToNodeRef(const LinkEndpointRef &ref);
    /**
     * @return UTF-8 descriptive tag from a 256-byte buffer, or empty on unresolved/unreadable object.
     */
    [[nodiscard]] QString tag() const; ///< Slice SA — `[TAGS]` value.

    /**
     * @brief Read the conduit length in project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double length() const;
    /**
     * @brief Read the conduit friction coefficient (normally Manning n; force-main meaning follows the selected friction law).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double roughness() const;
    /**
     * @brief Read the upstream link offset in project length units, displayed as absolute invert elevation when LINK_OFFSETS is ELEVATION.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double offsetUp() const;
    /**
     * @brief Read the downstream link offset in project length units, displayed as absolute invert elevation when LINK_OFFSETS is ELEVATION.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double offsetDn() const;
    /**
     * @brief Read the control crest relative to the upstream node or absolute elevation according to LINK_OFFSETS, in project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double crestHeight() const;
    /**
     * @brief Read the weir/orifice/outlet discharge coefficient whose dimensions depend on structure and project unit convention.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double dischargeCoeff() const;
    /**
     * @brief Read the weir end-contraction count represented as a double by the engine API.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double endContractions() const;
    /**
     * @return YES for a nonzero engine flap flag; NO when unresolved/read fails.
     */
    [[nodiscard]] FlapGate flapGate() const;
    /**
     * @return ON for a nonzero initial-status flag; OFF when unresolved/read fails.
     */
    [[nodiscard]] PumpInitState pumpInitState() const;
    /**
     * @return Pump/outlet shared curve-slot table ID; empty when unassigned/unreadable.
     */
    [[nodiscard]] QString pumpCurveName() const;
    /**
     * @return BOTTOM for a nonzero engine orifice type, otherwise SIDE (including read failure).
     */
    [[nodiscard]] OrificeType orificeType() const; ///< Slice SD partial
    /**
     * @return Authored TRANSVERSE..ROADWAY type; TRANSVERSE for missing/read/out-of-range values.
     */
    [[nodiscard]] WeirType weirType() const; ///< Slice SD partial
    /**
     * @return Authored FUNCTIONAL_HEAD..TABULAR_DEPTH selector; FUNCTIONAL_HEAD for unavailable/out-of-range values.
     */
    [[nodiscard]] OutletRatingType outletRatingType() const; ///< Slice SD partial
    /**
     * @brief Read the dimensionless functional outlet rating exponent.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double outletExpon() const; ///< Slice SD partial
    /**
     * @brief Read the pump startup water depth in project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double pumpStartupDepth() const; ///< BN-LINK-05
    /**
     * @brief Read the pump shutoff water depth in project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double pumpShutoffDepth() const; ///< BN-LINK-05
    /**
     * @brief Read the orifice opening/closing rate in reciprocal seconds (zero means instantaneous).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double orificeOpenCloseRate() const; ///< BN-LINK-06

    // Slice SB — conduit scalar parity (matches legacy ConduitProps in
    // SWMM-GUI/Epaswmm5/objprops.txt rows 12-17, 21). All accessors live
    // on the base so the GETTER_D macro can be reused; only the
    // SWMMConduitPropertyAdapter subclass exposes them as Q_PROPERTY rows
    // because pumps / orifices / weirs / outlets don't carry these
    // fields on the legacy editor side.
    /**
     * @brief Read the initial conduit flow in project flow units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double initialFlow() const; ///< BN-LINK-01a getter
    /**
     * @brief Read the conduit maximum-flow limit in project flow units; zero means no limit.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double maxFlow() const; ///< BN-LINK-01b getter
    /**
     * @brief Read the dimensionless entrance head-loss coefficient.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double lossInlet() const; ///< swmm_link_get_loss_coeff[inlet]
    /**
     * @brief Read the dimensionless exit head-loss coefficient.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double lossOutlet() const; ///< swmm_link_get_loss_coeff[outlet]
    /**
     * @brief Read the dimensionless average distributed head-loss coefficient.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double lossAvg() const; ///< swmm_link_get_loss_coeff[avg]
    /**
     * @brief Read the conduit seepage loss rate in project rainfall units (in/hr or mm/hr).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double seepRate() const; ///< swmm_link_get_seep_rate
    /**
     * @return Parallel conduit barrel count; one when link is unresolved/read fails.
     */
    [[nodiscard]] int barrels() const; ///< swmm_link_get_barrels

    // Direct inline cross-section geometry — geom1..geom4 of the engine
    // xsect tuple, exposed alongside the compound `xsection` editor so the
    // user can tweak a single dimension without opening the dialog. The
    // engine has no per-geom setter, so each setter read-modify-writes the
    // whole tuple (mirrors the loss-coeff slots). `xsectShapeId` lets the
    // Property Browser grey out geoms that don't apply to the current
    // shape (see openswmmvis::xsectGeomApplies).
    /**
     * @return SWMM_XSectShape engine ID, or zero when unresolved/unreadable; not the GUI metadata row ordinal.
     */
    [[nodiscard]] int xsectShapeId() const; ///< current SWMM_XSECT_* id
    /**
     * @brief Read raw cross-section geometry slot 1 for the current shape.
     * @return Stored slot value, or zero when unreadable. Meaning depends on shape: length in project units, dimensionless slope/exponent/friction parameter, or a picker-owned engine index. See xsectshapegeom.h.
     */
    [[nodiscard]] double xsectGeom1() const;
    /**
     * @brief Read raw cross-section geometry slot 2 for the current shape.
     * @return Stored slot value, or zero when unreadable. Meaning depends on shape: length in project units, dimensionless slope/exponent/friction parameter, or a picker-owned engine index. See xsectshapegeom.h.
     */
    [[nodiscard]] double xsectGeom2() const;
    /**
     * @brief Read raw cross-section geometry slot 3 for the current shape.
     * @return Stored slot value, or zero when unreadable. Meaning depends on shape: length in project units, dimensionless slope/exponent/friction parameter, or a picker-owned engine index. See xsectshapegeom.h.
     */
    [[nodiscard]] double xsectGeom3() const;
    /**
     * @brief Read raw cross-section geometry slot 4 for the current shape.
     * @return Stored slot value, or zero when unreadable. Meaning depends on shape: length in project units, dimensionless slope/exponent/friction parameter, or a picker-owned engine index. See xsectshapegeom.h.
     */
    [[nodiscard]] double xsectGeom4() const;

    // Read-only post-run summary statistics (mirror of the node adapter's
    // Slice QA.2 block and the Attribute Table's link dynamics columns).
    // Zero until a run's results are bound via setStatsSource; the three
    // pump utilisation getters are meaningful only on pumps.
    /**
     * @brief Read the maximum flow in project flow units from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statMaxFlow() const;
    /**
     * @brief Read the maximum velocity in project length units per second from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statMaxVelocity() const;
    /**
     * @brief Read the maximum-depth/full-depth dimensionless ratio from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statMaxFilling() const;
    /**
     * @brief Read the total flow volume in cubic project length units from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statVolFlow() const;
    /**
     * @brief Read the total surcharged duration in hours from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statSurchargeTime() const;
    /**
     * @brief Read the pump start-cycle count represented as double from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statPumpCycles() const;
    /**
     * @brief Read the pump operating duration in hours from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statPumpOnTime() const;
    /**
     * @brief Read the pumped volume in cubic project length units from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statPumpVolume() const;

    /*! Slice BM.0-Browse-Edit — `DataObjectRef`-typed accessor for the
     *  pump curve, used by the `Q_PROPERTY` on `SWMMPumpPropertyAdapter`.
     *  Constructs the ref with `kind=AnyCurve` (pump curves accept
     *  several engine curve types). Empty `currentName` means no curve
     *  is assigned yet (the engine returns curveIdx == -1).
     * @return RatingCurve picker for an outlet, otherwise PumpCurve, carrying the current curve name and borrowed project context.
     */
    [[nodiscard]] DataObjectRef pumpCurveRef() const;

    /*! Slice SC.1 — Compound-attribute refs for the link-side Property
     *  Browser cells (cross section / inlet usage). Each builds the
     *  engine summary live so the cell shows a useful preview even
     *  before the user opens the dialog.
     * @brief Build an editor coordinate and live summary for current cross-section shape, four geometry values and barrel count.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] LinkCompoundEditRef xsectionRef() const;
    /**
     * @brief Build an editor coordinate and live summary for inlet usage records assigned to this conduit.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] LinkCompoundEditRef inletUsageRef() const;

    /*! ATTRIBUTE_EDITOR_WIRING Phase 0 — culvert code is a single
     *  HDS-5 enum value, edited inline via `CulvertCodeComboBox`
     *  (no separate dialog).
     * @return Borrowed engine/layer plus current link ID and HDS-5 inlet-control code, default zero when unavailable.
     */
    [[nodiscard]] CulvertCodeRef culvertCodeRef() const;

    /*! Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
     *  user-flag assignments row (see SWMMNodePropertyAdapter).
     * @brief Build the per-object flag editor payload and current set-count summary.
     * @return Reference to the layer's shared flag store, lazily created by ensureUserFlagsModel(), or null model when no layer is bound. All pointers remain borrowed.
     */
    [[nodiscard]] UserFlagsEditRef userFlagsRef() const;

    /*! Initial-quality UI round — per-element [INITIAL_QUALITY] overrides
     *  row (see SWMMNodePropertyAdapter).
     * @brief Build a scoped initial-concentration editor payload.
     * @return Borrowed engine, node/link identity and current explicit override-count summary; wroteChanges is initially false.
     */
    [[nodiscard]] InitialQualityEditRef initialQualityRef() const;

    /*! See SWMMNodePropertyAdapter::displayLabelFor — same contract,
     *  returns "" for unknown property names.
     * @param[in] property Exact case-sensitive Q_PROPERTY key.
     * @return Translated label using the active project units where applicable, or empty for an unsupported key. Values are not converted by this label method.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /**
     * @param[in] newName Proposed ID trimmed before comparison; empty or unchanged names are ignored.
     * @details Emits renameRequested only; layer-side acceptance must call updateStoredName() before this adapter uses the new ID.
     */
    void setName(const QString &newName);
    /**
     * @brief Replace the descriptive engine tag.
     * @param[in] t Literal tag text encoded as UTF-8; empty clears it.
     * @details Missing object is a no-op; emits changed only on engine acceptance, without returning a failure diagnostic.
     */
    void setTag(const QString &t); ///< Slice SA
    /**
     * @brief Update the conduit length in project length units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setLength(double v);
    /**
     * @brief Update the conduit friction coefficient (normally Manning n; force-main meaning follows the selected friction law).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setRoughness(double v);
    /**
     * @brief Update the upstream link offset in project length units, displayed as absolute invert elevation when LINK_OFFSETS is ELEVATION.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setOffsetUp(double v);
    /**
     * @brief Update the downstream link offset in project length units, displayed as absolute invert elevation when LINK_OFFSETS is ELEVATION.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setOffsetDn(double v);
    /**
     * @brief Update the control crest relative to the upstream node or absolute elevation according to LINK_OFFSETS, in project length units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setCrestHeight(double v);
    /**
     * @brief Update the weir/orifice/outlet discharge coefficient whose dimensions depend on structure and project unit convention.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setDischargeCoeff(double v);
    /**
     * @brief Update the weir end-contraction count represented as a double by the engine API.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setEndContractions(double v);
    /**
     * @brief Apply the link's FlapGate selection through the typed engine API.
     * @param[in] v Valid FlapGate selector passed without additional local range checks.
     * @details Missing link or rejected kind/state/value is silently ignored; SWMM_OK emits changed.
     */
    void setFlapGate(FlapGate v);
    /**
     * @brief Apply the link's PumpInitState selection through the typed engine API.
     * @param[in] v Valid PumpInitState selector passed without additional local range checks.
     * @details Missing link or rejected kind/state/value is silently ignored; SWMM_OK emits changed.
     */
    void setPumpInitState(PumpInitState v);
    /**
     * @brief Apply the link's OrificeType selection through the typed engine API.
     * @param[in] v Valid OrificeType selector passed without additional local range checks.
     * @details Missing link or rejected kind/state/value is silently ignored; SWMM_OK emits changed.
     */
    void setOrificeType(OrificeType v); ///< Slice SD partial
    /**
     * @brief Apply the link's WeirType selection through the typed engine API.
     * @param[in] v Valid WeirType selector passed without additional local range checks.
     * @details Missing link or rejected kind/state/value is silently ignored; SWMM_OK emits changed.
     */
    void setWeirType(WeirType v); ///< Slice SD partial
    /**
     * @brief Apply the link's OutletRatingType selection through the typed engine API.
     * @param[in] v Valid OutletRatingType selector passed without additional local range checks.
     * @details Missing link or rejected kind/state/value is silently ignored; SWMM_OK emits changed.
     */
    void setOutletRatingType(OutletRatingType v); ///< Slice SD partial
    /**
     * @brief Update the dimensionless functional outlet rating exponent.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setOutletExpon(double v); ///< Slice SD partial
    /**
     * @brief Update the pump startup water depth in project length units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setPumpStartupDepth(double v); ///< BN-LINK-05
    /**
     * @brief Update the pump shutoff water depth in project length units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setPumpShutoffDepth(double v); ///< BN-LINK-05
    /**
     * @brief Update the orifice opening/closing rate in reciprocal seconds (zero means instantaneous).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setOrificeOpenCloseRate(double v); ///< BN-LINK-06
    // Slice SB — conduit scalar setters. Loss-coefficient setters read
    // the other two coefficients from the engine first then write the
    // full triple via `swmm_link_set_loss_coeff` (atomic from a caller's
    // view); the three rows look independent in the Property Browser
    // but the three-coeff write is one engine call.
    /**
     * @brief Update the initial conduit flow in project flow units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setInitialFlow(double v);
    /**
     * @brief Update the conduit maximum-flow limit in project flow units; zero means no limit.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setMaxFlow(double v);
    /**
     * @brief Update the dimensionless entrance head-loss coefficient.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads the complete inlet/outlet/average loss tuple, changes one component and writes all three together. Sibling values are preserved; only SWMM_OK emits changed and failures are not returned.
     */
    void setLossInlet(double v);
    /**
     * @brief Update the dimensionless exit head-loss coefficient.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads the complete inlet/outlet/average loss tuple, changes one component and writes all three together. Sibling values are preserved; only SWMM_OK emits changed and failures are not returned.
     */
    void setLossOutlet(double v);
    /**
     * @brief Update the dimensionless average distributed head-loss coefficient.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads the complete inlet/outlet/average loss tuple, changes one component and writes all three together. Sibling values are preserved; only SWMM_OK emits changed and failures are not returned.
     */
    void setLossAvg(double v);
    /**
     * @brief Update the conduit seepage loss rate in project rainfall units (in/hr or mm/hr).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setSeepRate(double v);
    /**
     * @brief Set the number of parallel conduit barrels.
     * @param[in] v Integer count at least one; smaller input is ignored before the engine call.
     * @details Missing link or engine rejection is silent; acceptance emits changed.
     */
    void setBarrels(int v);
    // Inline cross-section geom setters — read-modify-write the engine
    // xsect tuple (shape + the other three geoms preserved). No-op when
    // the geom doesn't apply to the current shape, so a stray write from
    // a greyed cell can't corrupt the section.
    /**
     * @brief Update raw cross-section geometry slot 1 while preserving the shape and other slots.
     * @param[in] v New value in the shape-specific slot units; unused numeric slots remain writable for later shape switches.
     * @details Rejects IRREGULAR/STREET slot 1 and CUSTOM slot 2 because those hold picker-owned indexes. Engine acceptance emits changed; failed reads/writes are silent.
     */
    void setXsectGeom1(double v);
    /**
     * @brief Update raw cross-section geometry slot 2 while preserving the shape and other slots.
     * @param[in] v New value in the shape-specific slot units; unused numeric slots remain writable for later shape switches.
     * @details Rejects IRREGULAR/STREET slot 1 and CUSTOM slot 2 because those hold picker-owned indexes. Engine acceptance emits changed; failed reads/writes are silent.
     */
    void setXsectGeom2(double v);
    /**
     * @brief Update raw cross-section geometry slot 3 while preserving the shape and other slots.
     * @param[in] v New value in the shape-specific slot units; unused numeric slots remain writable for later shape switches.
     * @details Rejects IRREGULAR/STREET slot 1 and CUSTOM slot 2 because those hold picker-owned indexes. Engine acceptance emits changed; failed reads/writes are silent.
     */
    void setXsectGeom3(double v);
    /**
     * @brief Update raw cross-section geometry slot 4 while preserving the shape and other slots.
     * @param[in] v New value in the shape-specific slot units; unused numeric slots remain writable for later shape switches.
     * @details Rejects IRREGULAR/STREET slot 1 and CUSTOM slot 2 because those hold picker-owned indexes. Engine acceptance emits changed; failed reads/writes are silent.
     */
    void setXsectGeom4(double v);
    /*! Slice BM.0-Browse-Edit — writes the selected curve via
     *  `swmm_link_set_pump_curve`. Looks up the engine curve index from
     *  `ref.currentName`; no-op if empty or unknown.
     * @brief Change the shared pump/tabular-outlet curve reference.
     * @param[in] ref currentName names an existing table; empty clears via -1, unknown names are ignored. Other ref fields do not rebind the adapter.
     * @details Engine validates link/curve compatibility; acceptance emits changed and failure is silent.
     */
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
     *  and the registered `LinkCompoundEditButton` creator never fires.
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setXsectionRef(const LinkCompoundEditRef &) { emit changed(); }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setUserFlagsRef(const UserFlagsEditRef &) { emit changed(); }
    /**
     * @brief Refresh initial-quality properties only after the dialog reports applied writes.
     * @param[in] ref Editor value; only wroteChanges is inspected.
     * @details Emits changed when wroteChanges is true; does not write initial concentrations itself.
     */
    void setInitialQualityRef(const InitialQualityEditRef &ref)
    {
        if (ref.wroteChanges)
            emit changed();
    }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setCulvertCodeRef(const CulvertCodeRef &) { emit changed(); }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setInletUsageRef(const LinkCompoundEditRef &) { emit changed(); }

    /*! Round-4 follow-up — see SWMMNodePropertyAdapter::refresh.
     * @details Emits changed unconditionally to reread live properties; no engine input is written.
     */
    void refresh() { emit changed(); }

    /*! See SWMMNodePropertyAdapter::updateStoredName.
     * @param[in] newName Accepted engine ID copied verbatim; no engine rename or signal occurs here.
     */
    void updateStoredName(const QString &newName) { m_name = newName; }

    /*! Stats-source dispatch — see SWMMNodePropertyAdapter (Slice QA.2)
     *  for the full contract. The stat* getters read the editing engine's
     *  ambient stats while the id is null, and the registry-resolved
     *  SWMMResultsLayer's linkStat*() accessors otherwise.
     * @param[in] registry Borrowed output-statistics registry; nullptr uses the engine fallback. Registry must outlive adapter use.
     * @details Replaces the raw registry pointer without emitting changed; caller selects/refreshed source separately.
     */
    void setStatsRegistry(openswmmvis::OutputStatsRegistry *registry);
    /**
     * @param[in] id Output identity UUID; null chooses the editing engine's ambient statistics.
     * @details A changed UUID emits changed to reread summaries. A missing resolved output layer returns the statistic getter's unavailable value.
     */
    void setStatsSource(const QUuid &id);
    /**
     * @return Selected statistics identity; null means the editing engine rather than an external result layer.
     */
    [[nodiscard]] QUuid statsSourceId() const { return m_statsSourceId; }

signals:
    /**
     * @brief Request property-model rereads after accepted writes, refresh calls or edit-dialog round trips.
     * @details Some delegated edit paths also emit after refusal so controls restore effective engine values; this is not a universal write-success signal.
     */
    void changed();
    /**
     * @param[in] oldName Stored engine ID before the proposed rename.
     * @param[in] newName Nonempty trimmed proposed ID; the receiving layer validates uniqueness and lifecycle.
     */
    void renameRequested(const QString &oldName, const QString &newName);
    /*! See SWMMNodePropertyAdapter::displayLabelsChanged.
     * @brief Request property labels to be rebuilt after active-project units change, without changing stored input values.
     */
    void displayLabelsChanged();

protected:
    /**
     * @brief Resolve the stored link ID in the current engine.
     * @return Zero-based index, or -1 for missing engine/name/object; no index is cached across renames.
     */
    [[nodiscard]] int linkIdx() const;
    //! Read-modify-write one geom (ordinal 1..4) of the xsect tuple; no-op
    //! when the geom doesn't apply to the current shape. Emits changed().
    /**
     * @brief Perform a single-slot edit of the complete shape/geometry tuple.
     * @param[in] ordinal Slot in [1,4]; callers must provide a valid ordinal.
     * @param[in] v New numeric slot value in shape-dependent units.
     * @details Successful read is required; picker-index slots are refused and sibling values/shape are preserved. Emits changed only after the engine accepts the complete tuple.
     */
    void writeXsectGeom(int ordinal, double v);
    SWMM_Engine m_engine;
    QString m_name;
    SWMMModelLayer *m_layer = nullptr; ///< Bound via setModelLayer.

    /// Stats-source dispatch state — see SWMMNodePropertyAdapter.
    QUuid m_statsSourceId;
    openswmmvis::OutputStatsRegistry *m_statsRegistry = nullptr;
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
    Q_PROPERTY(double length READ length WRITE setLength NOTIFY changed)
    Q_PROPERTY(double roughness READ roughness WRITE setRoughness NOTIFY changed)
    Q_PROPERTY(double offsetUp READ offsetUp WRITE setOffsetUp NOTIFY changed)
    Q_PROPERTY(double offsetDn READ offsetDn WRITE setOffsetDn NOTIFY changed)
    // Slice SB — conduit scalar parity rows. Loss coefficients are
    // exposed as three separate Q_PROPERTYs per §S.1 Q-S4 decision.
    Q_PROPERTY(double initialFlow READ initialFlow WRITE setInitialFlow NOTIFY changed)
    Q_PROPERTY(double maxFlow READ maxFlow WRITE setMaxFlow NOTIFY changed)
    Q_PROPERTY(double lossInlet READ lossInlet WRITE setLossInlet NOTIFY changed)
    Q_PROPERTY(double lossOutlet READ lossOutlet WRITE setLossOutlet NOTIFY changed)
    Q_PROPERTY(double lossAvg READ lossAvg WRITE setLossAvg NOTIFY changed)
    Q_PROPERTY(double seepRate READ seepRate WRITE setSeepRate NOTIFY changed)
    Q_PROPERTY(int barrels READ barrels WRITE setBarrels NOTIFY changed)
    Q_PROPERTY(
        SWMMLinkPropertyAdapter::FlapGate flapGate READ flapGate WRITE setFlapGate NOTIFY changed)
    // Slice SC.1 — compound-attribute "Edit…" cells.
    Q_PROPERTY(LinkCompoundEditRef xsection READ xsectionRef WRITE setXsectionRef NOTIFY changed)
    // Direct inline geom1..geom4 (generic labels; PropertiesPanel greys
    // out the ones that don't apply to the current shape).
    Q_PROPERTY(double geom1 READ xsectGeom1 WRITE setXsectGeom1 NOTIFY changed)
    Q_PROPERTY(double geom2 READ xsectGeom2 WRITE setXsectGeom2 NOTIFY changed)
    Q_PROPERTY(double geom3 READ xsectGeom3 WRITE setXsectGeom3 NOTIFY changed)
    Q_PROPERTY(double geom4 READ xsectGeom4 WRITE setXsectGeom4 NOTIFY changed)
    Q_PROPERTY(
        CulvertCodeRef culvertCode READ culvertCodeRef WRITE setCulvertCodeRef NOTIFY changed)
    Q_PROPERTY(
        LinkCompoundEditRef inletUsage READ inletUsageRef WRITE setInletUsageRef NOTIFY changed)
    // Read-only post-run summary (no WRITE → non-editable). Mirrors the
    // Attribute Table's link dynamics columns; values follow the bound
    // stats source (see the base class).
    Q_PROPERTY(double statMaxFlow READ statMaxFlow NOTIFY changed)
    Q_PROPERTY(double statMaxVelocity READ statMaxVelocity NOTIFY changed)
    Q_PROPERTY(double statMaxFilling READ statMaxFilling NOTIFY changed)
    Q_PROPERTY(double statVolFlow READ statVolFlow NOTIFY changed)
    Q_PROPERTY(double statSurchargeTime READ statSurchargeTime NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality READ initialQualityRef WRITE
                   setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
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
    Q_PROPERTY(DataObjectRef pumpCurve READ pumpCurveRef WRITE setPumpCurveRef NOTIFY changed)
    Q_PROPERTY(SWMMLinkPropertyAdapter::PumpInitState initState READ pumpInitState WRITE
                   setPumpInitState NOTIFY changed)
    Q_PROPERTY(double startupDepth READ pumpStartupDepth WRITE setPumpStartupDepth NOTIFY changed)
    Q_PROPERTY(double shutoffDepth READ pumpShutoffDepth WRITE setPumpShutoffDepth NOTIFY changed)
    // Read-only post-run summary — the shared link block plus the pump
    // utilisation trio (Attribute Table parity; see the base class).
    Q_PROPERTY(double statMaxFlow READ statMaxFlow NOTIFY changed)
    Q_PROPERTY(double statMaxVelocity READ statMaxVelocity NOTIFY changed)
    Q_PROPERTY(double statMaxFilling READ statMaxFilling NOTIFY changed)
    Q_PROPERTY(double statVolFlow READ statVolFlow NOTIFY changed)
    Q_PROPERTY(double statSurchargeTime READ statSurchargeTime NOTIFY changed)
    Q_PROPERTY(double statPumpCycles READ statPumpCycles NOTIFY changed)
    Q_PROPERTY(double statPumpOnTime READ statPumpOnTime NOTIFY changed)
    Q_PROPERTY(double statPumpVolume READ statPumpVolume NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality READ initialQualityRef WRITE
                   setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
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
    Q_PROPERTY(SWMMLinkPropertyAdapter::OrificeType orificeType READ orificeType WRITE
                   setOrificeType NOTIFY changed)
    Q_PROPERTY(double offset READ offsetUp WRITE setOffsetUp NOTIFY changed)
    Q_PROPERTY(double dischargeCoeff READ dischargeCoeff WRITE setDischargeCoeff NOTIFY changed)
    Q_PROPERTY(
        SWMMLinkPropertyAdapter::FlapGate flapGate READ flapGate WRITE setFlapGate NOTIFY changed)
    // Slice SD partial (BN-LINK-06) — orifice open/close rate (1/s).
    // 0 = instantaneous. Legacy SWMM-GUI surfaces this as "Time to
    // Open/Close (hr)" — clients that want the hours UX should
    // convert before/after calling the setter.
    Q_PROPERTY(
        double openCloseRate READ orificeOpenCloseRate WRITE setOrificeOpenCloseRate NOTIFY changed)
    Q_PROPERTY(LinkCompoundEditRef xsection READ xsectionRef WRITE setXsectionRef NOTIFY changed)
    // Direct inline geom1..geom4 (orifices use CIRCULAR / RECT_CLOSED, so
    // only geom1/geom2 ever apply; the rest grey out).
    Q_PROPERTY(double geom1 READ xsectGeom1 WRITE setXsectGeom1 NOTIFY changed)
    Q_PROPERTY(double geom2 READ xsectGeom2 WRITE setXsectGeom2 NOTIFY changed)
    Q_PROPERTY(double geom3 READ xsectGeom3 WRITE setXsectGeom3 NOTIFY changed)
    Q_PROPERTY(double geom4 READ xsectGeom4 WRITE setXsectGeom4 NOTIFY changed)
    // Read-only post-run summary (Attribute Table parity; see base class).
    Q_PROPERTY(double statMaxFlow READ statMaxFlow NOTIFY changed)
    Q_PROPERTY(double statMaxVelocity READ statMaxVelocity NOTIFY changed)
    Q_PROPERTY(double statMaxFilling READ statMaxFilling NOTIFY changed)
    Q_PROPERTY(double statVolFlow READ statVolFlow NOTIFY changed)
    Q_PROPERTY(double statSurchargeTime READ statSurchargeTime NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality READ initialQualityRef WRITE
                   setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
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
    Q_PROPERTY(
        SWMMLinkPropertyAdapter::WeirType weirType READ weirType WRITE setWeirType NOTIFY changed)
    Q_PROPERTY(double offsetUp READ offsetUp WRITE setOffsetUp NOTIFY changed)
    Q_PROPERTY(double offsetDn READ offsetDn NOTIFY changed)
    Q_PROPERTY(double crestHeight READ crestHeight WRITE setCrestHeight NOTIFY changed)
    Q_PROPERTY(double dischargeCoeff READ dischargeCoeff WRITE setDischargeCoeff NOTIFY changed)
    Q_PROPERTY(double endContractions READ endContractions WRITE setEndContractions NOTIFY changed)
    Q_PROPERTY(
        SWMMLinkPropertyAdapter::FlapGate flapGate READ flapGate WRITE setFlapGate NOTIFY changed)
    Q_PROPERTY(LinkCompoundEditRef xsection READ xsectionRef WRITE setXsectionRef NOTIFY changed)
    // Direct inline geom1..geom4 (weirs use RECT_OPEN / TRAPEZOIDAL /
    // TRIANGULAR; inapplicable geoms grey out per shape).
    Q_PROPERTY(double geom1 READ xsectGeom1 WRITE setXsectGeom1 NOTIFY changed)
    Q_PROPERTY(double geom2 READ xsectGeom2 WRITE setXsectGeom2 NOTIFY changed)
    Q_PROPERTY(double geom3 READ xsectGeom3 WRITE setXsectGeom3 NOTIFY changed)
    Q_PROPERTY(double geom4 READ xsectGeom4 WRITE setXsectGeom4 NOTIFY changed)
    // Read-only post-run summary (Attribute Table parity; see base class).
    Q_PROPERTY(double statMaxFlow READ statMaxFlow NOTIFY changed)
    Q_PROPERTY(double statMaxVelocity READ statMaxVelocity NOTIFY changed)
    Q_PROPERTY(double statMaxFilling READ statMaxFilling NOTIFY changed)
    Q_PROPERTY(double statVolFlow READ statVolFlow NOTIFY changed)
    Q_PROPERTY(double statSurchargeTime READ statSurchargeTime NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality READ initialQualityRef WRITE
                   setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
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
    Q_PROPERTY(SWMMLinkPropertyAdapter::OutletRatingType ratingType READ outletRatingType WRITE
                   setOutletRatingType NOTIFY changed)
    Q_PROPERTY(double offset READ offsetUp WRITE setOffsetUp NOTIFY changed)
    // Functional-form coefficient (legacy "Coefficient" row) reuses
    // the shared discharge-coeff scalar — same engine field (cd) used
    // by orifices and weirs.
    Q_PROPERTY(double coefficient READ dischargeCoeff WRITE setDischargeCoeff NOTIFY changed)
    Q_PROPERTY(double expon READ outletExpon WRITE setOutletExpon NOTIFY changed)
    // Tabular curve picker reuses the existing DataObjectRef pump-curve
    // accessor (the engine shares the curve-index slot between pumps
    // and tabular outlets per `LinksHandler::handle_outlets:229`).
    Q_PROPERTY(DataObjectRef outletCurve READ pumpCurveRef WRITE setPumpCurveRef NOTIFY changed)
    Q_PROPERTY(
        SWMMLinkPropertyAdapter::FlapGate flapGate READ flapGate WRITE setFlapGate NOTIFY changed)
    // Read-only post-run summary (Attribute Table parity; see base class).
    Q_PROPERTY(double statMaxFlow READ statMaxFlow NOTIFY changed)
    Q_PROPERTY(double statMaxVelocity READ statMaxVelocity NOTIFY changed)
    Q_PROPERTY(double statMaxFilling READ statMaxFilling NOTIFY changed)
    Q_PROPERTY(double statVolFlow READ statVolFlow NOTIFY changed)
    Q_PROPERTY(double statSurchargeTime READ statSurchargeTime NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality READ initialQualityRef WRITE
                   setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

#endif // SWMMLINKPROPERTYADAPTER_H
