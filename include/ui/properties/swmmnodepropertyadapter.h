/**
 * @file      swmmnodepropertyadapter.h
 * @brief     Slice Z.5.3 / AG.3 — Property-tree adapter for SWMM nodes.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice Z.5.3 / AG.3 — Property-tree adapter for SWMM nodes.
 *
 * A thin `Q_OBJECT` façade over (engine, node_idx) that exposes the
 * editable node attributes as `Q_PROPERTY` for `QPropertyModel`.
 * READ slots call `swmm_node_get_*`; WRITE slots call
 * `swmm_node_set_*`.  No state stored on the adapter — every read
 * round-trips to the engine so the property tree stays in sync
 * with whatever the SoA has after Run / Step / Save As / etc.
 *
 * Object-type stays read-only (no `Q_PROPERTY` for `nodeType`) per
 * the 2026-05-11 user rule: type conversion is a right-click action,
 * not an inline edit.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMNODEPROPERTYADAPTER_H
#define SWMMNODEPROPERTYADAPTER_H

#include <QObject>
#include <QString>
#include <QUuid> // Slice QA.2 — stats-source identity

#include <functional> // writeInletUsage() mutator

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h> // SWMM_InletUsage

#include "layers/nodecoupling2d.h" // 1D↔2D coupling state
#include "ui/properties/dataobjectref.h"
#include "ui/properties/initialqualityeditref.h"
#include "ui/properties/nodecompoundeditref.h"
#include "ui/properties/userflagseditref.h"

class SWMMModelLayer;
namespace openswmmvis
{
class OutputStatsRegistry;
} // Slice QA.2

/*! Base node adapter — exposes only the attributes that ALL node
 *  types share (per the SWMM5 .inp [JUNCTIONS]/[OUTFALLS]/[STORAGE]/
 *  [DIVIDERS] sections in `InpWriter.cpp`).  Type-specific Q_PROPERTYs
 *  live on subclasses below so the Property Browser doesn't show
 *  inapplicable properties (e.g. outfalls won't show `dividerType`).
 *
 *  All accessors live on the base — subclasses just declare the
 *  Q_PROPERTYs they want exposed.  Public getters/setters that aren't
 *  listed as Q_PROPERTY on the subclass simply don't appear in its
 *  metaObject, so QPropertyModel ignores them. */
class SWMMNodePropertyAdapter : public QObject
{
    Q_OBJECT

public:
    /*! Round-4 follow-up 2026-05-12 — type-safe enums declared with
     *  Q_ENUM so QPropertyModel renders the key name ("CUTOFF",
     *  "FREE", etc.) instead of the raw integer.  Values mirror
     *  SWMM_NodeType / SWMM_OutfallType / SWMM_DividerType + a
     *  yes/no boolean wrapped for flap-gate.  Subclasses inherit
     *  these via metaObject() — no need to redeclare. */
    /*! Junction/Outfall/Storage/Divider match SWMM_NodeType ordinals; the
     *  GUI presents VirtualJunction as a distinct kind even though the
     *  engine stores it as a JUNCTION with the is_virtual flag set. */
    /*! An inlet junction is a virtual junction that additionally carries the
     *  is_inlet flag and one [INLET_USAGE] row, so nodeKind() tests it FIRST. */
    enum NodeKind {
        Junction = 0,
        Outfall = 1,
        Storage = 2,
        Divider = 3,
        VirtualJunction = 4,
        InletJunction = 5
    };
    enum OutfallType { FREE = 0, NORMAL = 1, FIXED = 2, TIDAL = 3, TIMESERIES = 4 };
    enum DividerType { CUTOFF = 0, OVERFLOW_ = 1, TABULAR = 2, WEIR = 3 };
    enum FlapGate { NO = 0, YES = 1 };
    /*! Inlet placement mode — ordinals match `SWMM_InletPlacement`, so the
     *  int round-trips through SWMM_InletUsage::placement unchanged. */
    enum InletPlacement { AUTOMATIC = 0, ON_GRADE = 1, ON_SAG = 2 };
    /*! Storage geometry form — ordinals match the engine's `SWMM_StorageShape`
     *  (and the legacy solver's `enum StorageType`), so the int round-trips
     *  through `swmm_node_get/set_storage_shape` unchanged.
     *
     *  - Tabular:    depth–area curve by id (`swmm_node_set_storage_curve`).
     *  - Functional: power law Area = A·Depth^B + C
     *                (`swmm_node_set_storage_functional`).
     *  - The four geometric shapes: three raw dimensions L/W/Z
     *    (`swmm_node_set_storage_geometry`), from which the engine derives its
     *    internal area coefficients. See storageshapegeom.h for what L/W/Z mean
     *    per shape — they differ, which is why the row labels are generic. */
    enum StorageShape {
        Tabular = 0,
        Functional = 1,
        Cylindrical = 2,
        Conical = 3,
        Paraboloid = 4,
        Pyramidal = 5
    };
    /*! 1D↔2D coupling structure ([2D_NODE_COUPLING] TYPE) — ordinals match
     *  SWMM_NodeCoupling2DType. The enumerator names are what the browser
     *  shows (class-scope unique, hence OUTFALL_2D / OPEN_STORAGE). */
    enum Coupling2DType {
        NOT_COUPLEABLE = 0,
        MANHOLE = 1,
        GRATE = 2,
        CURB = 3,
        OPEN_STORAGE = 4,
        OUTFALL_2D = 5
    };
    /*! CREST policy; CREST_DEFAULT = not authored (the structure default). */
    enum Coupling2DCrest { CREST_DEFAULT = 0, NODE_RIM, MESH_BED, CREST_MAX, CREST_MIN };
    /*! COEFFS preset; COEFFS_GLOBAL = not authored ([2D_OPTIONS] COUPLING_COEFFS). */
    enum Coupling2DCoeffs {
        COEFFS_GLOBAL = 0,
        COEFFS_DEFAULT,
        RUBINATO2017,
        RUBINATO2017_LOWER,
        RUBINATO2017_UPPER
    };
    /*! Per-node LAW; LAW_GLOBAL = not authored ([2D_OPTIONS] COUPLING_LAW). */
    enum Coupling2DLaw { LAW_GLOBAL = 0, ORIFICE, WEIR_ORIFICE };
    Q_ENUM(NodeKind)
    Q_ENUM(OutfallType)
    Q_ENUM(DividerType)
    Q_ENUM(FlapGate)
    Q_ENUM(StorageShape)
    Q_ENUM(InletPlacement)
    Q_ENUM(Coupling2DType)
    Q_ENUM(Coupling2DCrest)
    Q_ENUM(Coupling2DCoeffs)
    Q_ENUM(Coupling2DLaw)

    // Common to every node type — Name + Type + Invert + Coords + Tag.
    Q_PROPERTY(QString name READ name WRITE setName)
    Q_PROPERTY(NodeKind nodeKind READ nodeKind)
    Q_PROPERTY(double invertElev READ invertElev WRITE setInvertElev NOTIFY changed)
    /*! Free-form `[TAGS]` label (DB.3 engine surface). Editable text;
     *  index-keyed in the engine, so survives a rename. */
    Q_PROPERTY(QString tag READ tag WRITE setTag NOTIFY changed)
    // Slice DB — coordinates editable. Writes go through `coordChangeRequested`
    // so the bound layer can route them via `applyNodeMove`, which refreshes
    // the cached scene coords + attached-link bboxes (a raw engine setter
    // would leave the canvas stale until the next full rebuild).
    Q_PROPERTY(double xCoord READ xCoord WRITE setXCoord NOTIFY changed)
    Q_PROPERTY(double yCoord READ yCoord WRITE setYCoord NOTIFY changed)

    /**
     * @brief Bind live properties and output summaries to a named node.
     * @param[in] engine Borrowed non-null engine that must remain valid while this adapter is used.
     * @param[in] name Case-preserved existing object ID copied into the adapter; indexes are resolved afresh.
     * @param[in] parent Optional Qt owner; construct and use on the GUI thread.
     * @details Forwards active-project unit changes as displayLabelsChanged; does not own the engine or create model data.
     */
    SWMMNodePropertyAdapter(SWMM_Engine engine, QString name, QObject *parent = nullptr);

    /*! DB.4c — set the model layer the compound-edit pickers can call
     *  back into to create new TS / patterns / UHs. Optional.
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
     * @return Engine node subtype with inlet/virtual junction flags distinguished; Junction for unresolved/read failure.
     */
    [[nodiscard]] NodeKind nodeKind() const;

    /**
     * @return UTF-8 descriptive tag from a 256-byte buffer, or empty on unresolved/unreadable object.
     */
    [[nodiscard]] QString tag() const;
    /**
     * @brief Read the node invert elevation in project length units (ft or m).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double invertElev() const;
    /**
     * @brief Read the hydraulic full depth above invert in project length units; zero requests engine auto-depth where supported.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double maxDepth() const;
    /*! Rendering-only rim (ground) depth above the invert; 0 = unset.
     *  Exposed as the "Max Depth, Display" row on virtual junctions, whose
     *  real max depth is the derived pipe crown. Never read by the solver —
     *  see swmm_node_get_rim_depth().
     * @brief Read the display/street rim depth above invert in project length units; nonpositive writes clear the authored display rim.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double rimDepth() const;
    /**
     * @brief Read the initial water depth above invert in project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double initialDepth() const;
    /**
     * @brief Read the extra pressurized head allowance above maximum depth in project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double surchargeDepth() const;
    /**
     * @brief Read the surface ponding area in squared project length units (ft² or m²).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double pondedArea() const;
    [[nodiscard]] double storageArea() const;
    /**
     * @brief Read the storage-node seepage rate in project rainfall units (in/hr or mm/hr).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double seepRate() const;
    /**
     * @return Authored x coordinate in model horizontal CRS units, or zero on unresolved/read failure.
     */
    [[nodiscard]] double xCoord() const;
    /**
     * @return Authored y coordinate in model horizontal CRS units, or zero on unresolved/read failure.
     */
    [[nodiscard]] double yCoord() const;
    /**
     * @return Outfall boundary selector; FREE when unresolved or the engine getter rejects this node.
     */
    [[nodiscard]] OutfallType outfallType() const;
    /**
     * @return YES for a nonzero engine outfall flap flag; NO when unresolved/unreadable.
     */
    [[nodiscard]] FlapGate outfallFlapGate() const;
    /**
     * @return Divider flow-split mode, defaulting to CUTOFF on unresolved/read failure.
     */
    [[nodiscard]] DividerType dividerType() const;

    // Slice DB — read-only computed + statistics summary getters.
    // `crownElev` / `fullVolume` / `degree` are populated at input time
    // (computed when links connect at load). The four `stat*` values
    // are populated only after a simulation run; pre-run they read back
    // as zero. Exposed via Q_PROPERTY without WRITE so QPropertyModel
    // renders them as non-editable rows.
    /**
     * @brief Read the derived highest connected-link crown elevation in project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double crownElev() const;
    /**
     * @brief Read the full hydraulic storage volume in cubic project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double fullVolume() const;
    /**
     * @return Number of incident links reported by the engine; zero when unresolved/read fails.
     */
    [[nodiscard]] int degree() const;
    /**
     * @brief Read the maximum simulated depth in project length units from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statMaxDepth() const;
    /**
     * @brief Read the peak overflow in project flow units from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statMaxOverflow() const;
    /**
     * @brief Read the total flooded volume in cubic project length units from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statVolFlooded() const;
    /**
     * @brief Read the total flooded duration in hours from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statTimeFlooded() const;

    // Slice DB.2 — compound-attribute refs. Each returns a value that
    // the registered `NodeCompoundEditButton` editor creator picks up
    // via the custom metatype dispatch; clicking the cell opens the
    // matching page of `NodeCompoundEditDialog`. The `summary` field
    // is computed live from the engine count so the cell shows a
    // useful preview even before the user opens the dialog.
    /**
     * @brief Build an editor coordinate and live summary for external inflow entries counted for this node.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] NodeCompoundEditRef inflowsRef() const;
    /**
     * @brief Build an editor coordinate and live summary for dry-weather inflow entries counted for this node.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] NodeCompoundEditRef dwfRef() const;
    /**
     * @brief Build an editor coordinate and live summary for RDII entries counted for this node.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] NodeCompoundEditRef rdiiRef() const;
    /**
     * @brief Build an editor coordinate and live summary for pollutants with nonempty treatment expressions on this node.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] NodeCompoundEditRef treatmentRef() const;

    /*! AQUIFER_GROUNDWATER_EXCHANGE plan G5 — read-only navigation row
     *  listing the subcatchments that discharge groundwater to this node
     *  ("from S1, S3" / "(none)"); the button opens
     *  GroundwaterExchangeDialog for the picked subcatchment.
     * @brief Build an editor coordinate and live summary for subcatchments discharging groundwater to this node.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] NodeCompoundEditRef groundwaterSourcesRef() const;
    /**
     * @brief Build an editor coordinate and live summary for storage-LID bottom connection mode.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] NodeCompoundEditRef lidBottomConnectionRef() const;

    /*! Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
     *  user-flag assignments row. Same compound-ref pattern as the
     *  accessors above; the UserFlagsEditButton editor opens
     *  UserFlagValuesDialog. Requires setModelLayer() for the shared
     *  UserFlagsModel; without a layer the cell disables gracefully.
     * @brief Build the per-object flag editor payload and current set-count summary.
     * @return Reference to the layer's shared flag store, lazily created by ensureUserFlagsModel(), or null model when no layer is bound. All pointers remain borrowed.
     */
    [[nodiscard]] UserFlagsEditRef userFlagsRef() const;

    /*! Initial-quality UI round — per-element [INITIAL_QUALITY] overrides
     *  row. Same compound-ref pattern; the InitialQualityEditButton editor
     *  opens InitialQualityDialog scoped to this node.
     * @brief Build a scoped initial-concentration editor payload.
     * @return Borrowed engine, node/link identity and current explicit override-count summary; wroteChanges is initially false.
     */
    [[nodiscard]] InitialQualityEditRef initialQualityRef() const;

    // Slice DA.4.3 — outfall stage-data accessors. Live on the base class
    // (rather than only on the outfall subclass) so the cpp can use the
    // same `nodeIdx() / m_engine` helpers as every other accessor. Only
    // the outfall subclass declares them as Q_PROPERTY, so QPropertyModel
    // only shows the rows on outfall nodes.
    //
    // The engine stores stage / tidal-curve-idx / timeseries-idx in a
    // shared union `outfall_param` slot, so switching outfall type is
    // destructive of the prior-type's assignment — getters return 0 /
    // empty when the current outfall type doesn't match what the property
    // means. PropertiesPanel greys out the inapplicable rows via
    // `setRowEditable` to reinforce this visually.
    /**
     * @return Prescribed fixed outfall stage in project length units; zero when current type is not FIXED or the node is unavailable.
     */
    [[nodiscard]] double outfallStage() const;
    /**
     * @return TidalCurve picker coordinate carrying the current curve only when the node is TIDAL; empty name otherwise. Borrowed pointers remain project-owned.
     */
    [[nodiscard]] DataObjectRef outfallTidalCurveRef() const;
    /**
     * @return TimeSeries picker coordinate carrying the current stage series only when type is TIMESERIES; empty name otherwise.
     */
    [[nodiscard]] DataObjectRef outfallTimeseriesRef() const;

    // Slice AG.4 — storage-unit geometry accessors. Live on the base class
    // (like the outfall stage-data accessors above) so the cpp can reuse the
    // shared `nodeIdx() / m_engine` helpers; only SWMMStoragePropertyAdapter
    // declares them as Q_PROPERTY.
    //
    // `storageShape()` now reads the engine's real shape field
    // (`swmm_node_get_storage_shape`) rather than inferring TABULAR from
    // `curve_idx >= 0` — that inference could not represent the four geometric
    // shapes, which are curve-less but are not power-law functional either.
    //
    // Which of the parameter accessors is meaningful depends on the shape:
    //   Tabular            → storageCurveRef()
    //   Functional         → storageCoeffA/ExpB/ConstC()   (A·d^B + C)
    //   geometric shapes   → storageParam1/2/3()           (raw L/W/Z)
    // The panel greys the inapplicable rows; see storageshapegeom.h.
    // Inlet-junction accessors (INLET_EDITOR_AND_INLET_JUNCTION_GUI_PLAN
    // §3.4). Like the outfall / storage blocks they live on the base class so
    // the cpp reuses nodeIdx() / m_engine; only
    // SWMMInletJunctionPropertyAdapter declares them as Q_PROPERTY.
    //
    // All of them read and write ONE engine row — the node's SWMM_InletUsage
    // (host_kind = SWMM_INLET_HOST_NODE). Every setter is a read-modify-write
    // of that row pushed as a SetInletUsageCommand through the model layer, so
    // each edit is one undo step and every view refreshes from attributeChanged.
    // Without a bound layer the setters are no-ops (the command needs one).
    /**
     * @return Inlet picker coordinate locked to STREET-compatible designs for an inlet junction; current name is empty when no usage row exists.
     */
    [[nodiscard]] DataObjectRef inletDesignRef() const;
    /**
     * @return CaptureNode picker coordinate with host node index and the current receiving node ID, empty when unavailable.
     */
    [[nodiscard]] DataObjectRef captureNodeRef() const;
    /**
     * @return Authored inlet count or one when no readable node-usage row exists.
     */
    [[nodiscard]] int numInlets() const;
    /**
     * @return Authored inlet blockage percentage, or zero without a readable usage row.
     */
    [[nodiscard]] double pctClogged() const;
    /**
     * @return Inlet capture limit in project flow units, zero meaning unlimited; zero when no readable usage exists.
     */
    [[nodiscard]] double flowRestriction() const;
    /**
     * @return Local gutter-depression height in project length units, or zero without a readable usage row.
     */
    [[nodiscard]] double depressionHeight() const;
    /**
     * @return Local gutter-depression width in project length units, or zero without a readable usage row.
     */
    [[nodiscard]] double depressionWidth() const;
    /**
     * @return Authored AUTOMATIC/ON_GRADE/ON_SAG placement enum, default AUTOMATIC without a readable usage row.
     */
    [[nodiscard]] InletPlacement placement() const;
    /*! Read-only: the name of the upstream STREET conduit's street section,
     *  i.e. the gutter the inlet sits in. Empty when not resolvable.
     * @return STREET cross-section name from the first readable incoming street conduit; empty if none resolves. Scans links without changing geometry.
     */
    [[nodiscard]] QString approachStreet() const;

    /**
     * @return Storage-LID picker coordinate holding the attached control name, or empty name when unavailable/unassigned.
     */
    [[nodiscard]] DataObjectRef lidControlRef() const;
    /**
     * @return Authored initial storage-LID saturation percent, or zero on missing node/read failure.
     */
    [[nodiscard]] double lidInitialSaturation() const;
    /**
     * @brief Attach or clear a storage-node LID control while preserving initial saturation.
     * @param[in] r currentName resolves the control; empty clears it and unknown nonempty IDs are ignored.
     * @details Engine validates the applicable node/control type. Successful write emits changed; failure is silent.
     */
    void setLidControlRef(const DataObjectRef &r);
    /**
     * @brief Change initial saturation of an already attached storage-node LID.
     * @param[in] sat Saturation percentage in [0,100], validated by the engine.
     * @details Requires a readable existing LID attachment; missing attachment/node or rejection produces no signal. Acceptance emits changed.
     */
    void setLidInitialSaturation(double sat);
    /**
     * @return Engine storage shape in Tabular..Pyramidal; Functional on unresolved/read failure or an out-of-range selector.
     */
    [[nodiscard]] StorageShape storageShape() const;
    /**
     * @return StorageCurve picker coordinate and current assigned table name; empty when absent/unreadable. Pointer ownership stays with the project.
     */
    [[nodiscard]] DataObjectRef storageCurveRef() const;
    /**
     * @brief Read the functional storage area coefficient A in area = A × depth^B + C, using project area/depth units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double storageCoeffA() const;
    /**
     * @brief Read the dimensionless functional storage exponent B in area = A × depth^B + C.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double storageExpB() const;
    /**
     * @brief Read the constant functional storage surface area C in squared project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double storageConstC() const;
    /**
     * @brief Read the first raw geometric storage dimension: full major axis/base length in project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double storageParam1() const;
    /**
     * @brief Read the second raw geometric storage dimension: full minor axis/base width in project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double storageParam2() const;
    /**
     * @brief Read the third raw storage parameter: run/rise slope for cone/pyramid or top-axis height for paraboloid.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double storageParam3() const;

    /*! Maps a Q_PROPERTY identifier (e.g. \c "maxDepth") to a
     *  human-readable column-0 label (e.g. \c "Max Depth (ft)") with
     *  unit suffixes pulled live from \c UnitSystem::instance().
     *  Q_INVOKABLE so QPropertyModel can pick it up generically without
     *  taking a SWMM-specific dependency.  Returns an empty string for
     *  unknown property names — the model then falls back to the raw
     *  identifier.
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
    void setTag(const QString &t);
    /**
     * @brief Update the node invert elevation in project length units (ft or m).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setInvertElev(double v);
    /**
     * @brief Update the hydraulic full depth above invert in project length units; zero requests engine auto-depth where supported.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setMaxDepth(double v);
    /**
     * @brief Update the display/street rim depth above invert in project length units; nonpositive writes clear the authored display rim.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setRimDepth(double v);
    /**
     * @brief Update the initial water depth above invert in project length units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setInitialDepth(double v);
    /**
     * @brief Update the extra pressurized head allowance above maximum depth in project length units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setSurchargeDepth(double v);
    /**
     * @brief Update the surface ponding area in squared project length units (ft² or m²).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setPondedArea(double v);
    void setStorageArea(double v);
    /**
     * @brief Update the storage-node seepage rate in project rainfall units (in/hr or mm/hr).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setSeepRate(double v);
    /**
     * @brief Request a map move while preserving the other current coordinate.
     * @param[in] v Proposed x coordinate in model horizontal CRS units; no local finiteness check occurs.
     * @details Missing object or equal coordinate is ignored. Emits coordChangeRequested instead of writing directly; layer acceptance is external.
     */
    void setXCoord(double v);
    /**
     * @brief Request a map move while preserving the other current coordinate.
     * @param[in] v Proposed y coordinate in model horizontal CRS units; no local finiteness check occurs.
     * @details Missing object or equal coordinate is ignored. Emits coordChangeRequested instead of writing directly; layer acceptance is external.
     */
    void setYCoord(double v);
    /**
     * @brief Set the outfall boundary type.
     * @param[in] v Valid OutfallType selector passed unchanged to the typed engine API.
     * @details Missing nodes and engine rejection are silent; SWMM_OK emits changed. The adapter does not add enum/range validation.
     */
    void setOutfallType(OutfallType v);
    /**
     * @brief Set the outfall reverse-flow flap flag.
     * @param[in] v Valid FlapGate selector passed unchanged to the typed engine API.
     * @details Missing nodes and engine rejection are silent; SWMM_OK emits changed. The adapter does not add enum/range validation.
     */
    void setOutfallFlapGate(FlapGate v);
    /**
     * @brief Set the divider flow-splitting method.
     * @param[in] v Valid DividerType selector passed unchanged to the typed engine API.
     * @details Missing nodes and engine rejection are silent; SWMM_OK emits changed. The adapter does not add enum/range validation.
     */
    void setDividerType(DividerType v);

    /*! Compound-ref write slots — the cell's NodeCompoundEditButton
     *  invokes these via the delegate's setModelData round-trip after
     *  the dialog closes. The actual engine mutations already happened
     *  inside the dialog (swmm_*_add/remove/set), so these are no-ops
     *  except for emitting changed() so the property tree (and the
     *  attribute table) re-reads its summary. Their existence is what
     *  flips `QMetaProperty::isWritable()` to true, which is what
     *  `QVariantPropertyItem` checks to mark the cell editable —
     *  without these, the cell never enters edit mode and the
     *  registered NodeCompoundEditButton creator never fires.
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setInflowsRef(const NodeCompoundEditRef &) { emit changed(); }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setDwfRef(const NodeCompoundEditRef &) { emit changed(); }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setRdiiRef(const NodeCompoundEditRef &) { emit changed(); }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setTreatmentRef(const NodeCompoundEditRef &) { emit changed(); }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setGroundwaterSourcesRef(const NodeCompoundEditRef &) { emit changed(); }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setLidBottomConnectionRef(const NodeCompoundEditRef &) { emit changed(); }
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

    // Slice DA.4.3 — outfall stage-data setters. Each calls the matching
    // swmm_node_set_outfall_* engine setter, which also flips the outfall
    // type to the matching kind (FIXED / TIDAL / TIMESERIES) — that's a
    // deliberate engine invariant. So calling setOutfallStage()
    // implicitly switches the outfall to FIXED. The setRowEditable
    // wiring in PropertiesPanel re-runs on every `changed()` and updates
    // which row is enabled accordingly.
    /**
     * @brief Assign a fixed outfall water-surface elevation.
     * @param[in] v Stage in project length units.
     * @details The engine setter also selects FIXED outfall type. Missing node/refusal is silent; acceptance emits changed.
     */
    void setOutfallStage(double v);
    /**
     * @brief Assign a named tidal curve and select tidal outfall behavior.
     * @param[in] r currentName identifies a curve table; empty/unresolved names are ignored. Other ref fields do not rebind the adapter.
     * @details Engine acceptance emits changed; no error status is returned.
     */
    void setOutfallTidalCurveRef(const DataObjectRef &r);
    /**
     * @brief Assign a named stage time series and select series outfall behavior.
     * @param[in] r currentName identifies the engine table; empty/unresolved names are ignored.
     * @details Engine acceptance emits changed; existing borrowed context remains bound.
     */
    void setOutfallTimeseriesRef(const DataObjectRef &r);

    // Slice AG.4 — storage geometry setters.
    //   setStorageShape(Functional) detaches any tabular curve so the engine
    //     falls back to the functional coefficients.
    //   setStorageShape(Tabular) keeps the current curve if one is assigned,
    //     else assigns the first [STORAGE]-type curve in the model; if none
    //     exist it is a no-op (the row stays FUNCTIONAL after re-read).
    //   The coefficient setters read-modify-write the engine's atomic (A,B,C)
    //     triple so each row edits one term without clobbering the others.
    //   setStorageShape(<geometric>) detaches any curve (engine-side) and
    //     re-derives the area coefficients from the node's current L/W/Z.
    //   The param setters read-modify-write the engine's atomic (p1,p2,p3)
    //     triple, same as the coefficient setters do for (A,B,C). The engine
    //     rejects invalid dimensions (L<=0, W<=0, Z<0, or Z==0 on a paraboloid)
    //     and leaves the node on its previous geometry, so a bad keystroke
    //     cannot wedge the model.
    // Inlet-junction setters — each is a read-modify-write of the node's one
    // SWMM_InletUsage row pushed as a SetInletUsageCommand (see the getters).
    /**
     * @brief Update the design identity in a node-hosted inlet-usage row.
     * @param[in] r currentName must resolve to an inlet design; empty/unknown picks are ignored.
     * @details Requires a model layer and a resolved capture node; equal designs are ignored. Writes route through writeInletUsage() and the shared undo stack.
     */
    void setInletDesignRef(const DataObjectRef &r);
    /**
     * @brief Update the receiving node in a node-hosted inlet-usage row.
     * @param[in] r currentName must resolve to a node; empty/unknown picks are ignored.
     * @details Requires a model layer and resolved inlet design; engine validation handles legal recipients. Shared write path ignores equal picks and emits changed after submission.
     */
    void setCaptureNodeRef(const DataObjectRef &r);
    /**
     * @brief Modify one node inlet-usage attribute while preserving its sibling fields.
     * @param[in] v Inlet count clamped to [1,5].
     * @details Requires a bound layer, valid node and complete design/capture identities. Unchanged values are ignored (fuzzy comparison for doubles); shared writeInletUsage() submits one undoable edit and emits changed.
     */
    void setNumInlets(int v);
    /**
     * @brief Modify one node inlet-usage attribute while preserving its sibling fields.
     * @param[in] v Blockage percentage clamped to [0,99].
     * @details Requires a bound layer, valid node and complete design/capture identities. Unchanged values are ignored (fuzzy comparison for doubles); shared writeInletUsage() submits one undoable edit and emits changed.
     */
    void setPctClogged(double v);
    /**
     * @brief Modify one node inlet-usage attribute while preserving its sibling fields.
     * @param[in] v Capture limit in project flow units; negative values clamp to zero (unlimited).
     * @details Requires a bound layer, valid node and complete design/capture identities. Unchanged values are ignored (fuzzy comparison for doubles); shared writeInletUsage() submits one undoable edit and emits changed.
     */
    void setFlowRestriction(double v);
    /**
     * @brief Modify one node inlet-usage attribute while preserving its sibling fields.
     * @param[in] v Local depression height in project length units; negative input clamps to zero.
     * @details Requires a bound layer, valid node and complete design/capture identities. Unchanged values are ignored (fuzzy comparison for doubles); shared writeInletUsage() submits one undoable edit and emits changed.
     */
    void setDepressionHeight(double v);
    /**
     * @brief Modify one node inlet-usage attribute while preserving its sibling fields.
     * @param[in] v Local depression width in project length units; negative input clamps to zero.
     * @details Requires a bound layer, valid node and complete design/capture identities. Unchanged values are ignored (fuzzy comparison for doubles); shared writeInletUsage() submits one undoable edit and emits changed.
     */
    void setDepressionWidth(double v);
    /**
     * @brief Modify one node inlet-usage attribute while preserving its sibling fields.
     * @param[in] v InletPlacement enum passed as the authored placement selector.
     * @details Requires a bound layer, valid node and complete design/capture identities. Unchanged values are ignored (fuzzy comparison for doubles); shared writeInletUsage() submits one undoable edit and emits changed.
     */
    void setPlacement(InletPlacement v);

    // 1D↔2D coupling structure ([2D_NODE_COUPLING], 1D2D fine-scale coupling
    // plan §7.1). On the base like the outfall / storage accessors; the
    // junction, storage, divider and outfall subclasses declare the rows.
    // Every write is one SetNodeCoupling2DCommand through the bound model
    // layer (undoable, and the same operation the coupling selector and the
    // remap dialog perform). Numeric parameters are text: blank = not
    // authored, the structure / class default applies.
    /**
     * @return Current effective structure type, or NOT_COUPLEABLE if the shared reader fails.
     */
    [[nodiscard]] Coupling2DType couplingType() const;
    /**
     * @return True when current couplingType() is not NOT_COUPLEABLE.
     */
    [[nodiscard]] bool coupleable() const;
    /**
     * @brief Read the authored crest-elevation selection.
     * @return Matching token enum; zero represents inherited/default or unrecognized text.
     */
    [[nodiscard]] Coupling2DCrest couplingCrest() const;
    /**
     * @brief Read the authored exchange coefficient preset.
     * @return Matching token enum; zero represents inherited/default or unrecognized text.
     */
    [[nodiscard]] Coupling2DCoeffs couplingCoeffs() const;
    /**
     * @brief Read the authored exchange-law selection.
     * @return Matching token enum; zero represents inherited/default or unrecognized text.
     */
    [[nodiscard]] Coupling2DLaw couplingLaw() const;
    /**
     * @brief Read authored manhole opening diameter in project length units as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingDiameter() const { return couplingParam(QStringLiteral("D")); }
    /**
     * @brief Read authored inlet perimeter in project length units as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingPerimeter() const
    {
        return couplingParam(QStringLiteral("PERIMETER"));
    }
    /**
     * @brief Read authored opening area in squared project length units as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingStructureArea() const
    {
        return couplingParam(QStringLiteral("AREA"));
    }
    /**
     * @brief Read authored chamber area in squared project length units as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingChamberArea() const
    {
        return couplingParam(QStringLiteral("CHAMBER_AREA"));
    }
    /**
     * @brief Read authored dimensionless lid open fraction as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingLidOpenFraction() const
    {
        return couplingParam(QStringLiteral("LID_OPEN_FRACTION"));
    }
    /**
     * @brief Read authored weir exchange coefficient in the selected coefficient convention as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingCw() const { return couplingParam(QStringLiteral("CW")); }
    /**
     * @brief Read authored submerged-weir exchange coefficient as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingCsw() const { return couplingParam(QStringLiteral("CSW")); }
    /**
     * @brief Read authored dimensionless inflow orifice coefficient as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingCoi() const { return couplingParam(QStringLiteral("COI")); }
    /**
     * @brief Read authored dimensionless outflow orifice coefficient as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingCoo() const { return couplingParam(QStringLiteral("COO")); }
    /**
     * @brief Read authored outfall discharge spread distance in project length units as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingSpread() const { return couplingParam(QStringLiteral("SPREAD")); }
    /**
     * @brief Read authored outfall tailwater relaxation duration in seconds as its original token.
     * @return Stored text, or empty for inherited/default/unreadable settings; does not format or evaluate the effective numeric default.
     */
    [[nodiscard]] QString couplingTailwaterRelax() const
    {
        return couplingParam(QStringLiteral("TAILWATER_RELAX"));
    }
    /**
     * @brief Select an allowed explicit node-coupling structure.
     * @param[in] v Structure enum constrained by nodecoupling2d::allowedTypes().
     * @details Invalid choices emit changed without writing; equal authored state is ignored. A changed structure clears its old structure-specific parameters before writeCoupling().
     */
    void setCouplingType(Coupling2DType v);
    /**
     * @brief Enable default coupling or author an explicit disabled row.
     * @param[in] on True restores the node's default structure when available, otherwise authors a manhole; false authors None.
     * @details A changed enable state clears old parameters and delegates to writeCoupling(); no-op when reading fails or enable state is unchanged.
     */
    void setCoupleable(bool on);
    /**
     * @brief Set the crest-elevation selection through the coupling token map.
     * @param[in] v Coupling2DCrest selector; zero/unknown map to empty text and remove the explicit override.
     * @details Delegates to setCouplingParam(); row authoring/undo/refusal behavior is shared.
     */
    void setCouplingCrest(Coupling2DCrest v);
    /**
     * @brief Set the exchange coefficient preset through the coupling token map.
     * @param[in] v Coupling2DCoeffs selector; zero/unknown map to empty text and remove the explicit override.
     * @details Delegates to setCouplingParam(); row authoring/undo/refusal behavior is shared.
     */
    void setCouplingCoeffs(Coupling2DCoeffs v);
    /**
     * @brief Set the exchange-law selection through the coupling token map.
     * @param[in] v Coupling2DLaw selector; zero/unknown map to empty text and remove the explicit override.
     * @details Delegates to setCouplingParam(); row authoring/undo/refusal behavior is shared.
     */
    void setCouplingLaw(Coupling2DLaw v);
    /**
     * @brief Author manhole opening diameter in project length units without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingDiameter(const QString &v) { setCouplingParam(QStringLiteral("D"), v); }
    /**
     * @brief Author inlet perimeter in project length units without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingPerimeter(const QString &v)
    {
        setCouplingParam(QStringLiteral("PERIMETER"), v);
    }
    /**
     * @brief Author opening area in squared project length units without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingStructureArea(const QString &v) { setCouplingParam(QStringLiteral("AREA"), v); }
    /**
     * @brief Author chamber area in squared project length units without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingChamberArea(const QString &v)
    {
        setCouplingParam(QStringLiteral("CHAMBER_AREA"), v);
    }
    /**
     * @brief Author dimensionless lid open fraction without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingLidOpenFraction(const QString &v)
    {
        setCouplingParam(QStringLiteral("LID_OPEN_FRACTION"), v);
    }
    /**
     * @brief Author weir exchange coefficient in the selected coefficient convention without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingCw(const QString &v) { setCouplingParam(QStringLiteral("CW"), v); }
    /**
     * @brief Author submerged-weir exchange coefficient without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingCsw(const QString &v) { setCouplingParam(QStringLiteral("CSW"), v); }
    /**
     * @brief Author dimensionless inflow orifice coefficient without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingCoi(const QString &v) { setCouplingParam(QStringLiteral("COI"), v); }
    /**
     * @brief Author dimensionless outflow orifice coefficient without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingCoo(const QString &v) { setCouplingParam(QStringLiteral("COO"), v); }
    /**
     * @brief Author outfall discharge spread distance in project length units without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingSpread(const QString &v) { setCouplingParam(QStringLiteral("SPREAD"), v); }
    /**
     * @brief Author outfall tailwater relaxation duration in seconds without changing the coupling structure type.
     * @param[in] v Numeric token in the documented units; trimmed empty text removes that explicit parameter.
     * @details Uses setCouplingParam(), marks the row authored and submits a layer undoable edit or a direct engine write. Notifications force a reread even after refusal; no validation error is returned.
     */
    void setCouplingTailwaterRelax(const QString &v)
    {
        setCouplingParam(QStringLiteral("TAILWATER_RELAX"), v);
    }

    /**
     * @brief Switch the authoritative storage-area representation.
     * @param[in] v StorageShape enum matching engine ordinals.
     * @details Tabular retains an existing curve or assigns the first STORAGE curve; with none, emits changed to restore the control. Other accepted shapes detach curves; invalid geometric dimensions are seeded to (1,1,1) after a shape switch. Engine refusals otherwise return silently.
     */
    void setStorageShape(StorageShape v);
    /**
     * @brief Assign a tabular storage curve or revert to functional storage.
     * @param[in] r currentName names an existing curve; empty passes -1 to clear and select FUNCTIONAL. Unknown names are ignored.
     * @details Emits changed only on engine acceptance; pointer fields do not rebind the adapter.
     */
    void setStorageCurveRef(const DataObjectRef &r);
    /**
     * @brief Update the functional storage area coefficient A in area = A × depth^B + C, using project area/depth units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads the functional (A,B,C) tuple and preserves sibling terms. An accepted engine write switches storage to FUNCTIONAL and detaches a tabular/geometric shape; emits changed. Failures are not returned.
     */
    void setStorageCoeffA(double v);
    /**
     * @brief Update the dimensionless functional storage exponent B in area = A × depth^B + C.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads the functional (A,B,C) tuple and preserves sibling terms. An accepted engine write switches storage to FUNCTIONAL and detaches a tabular/geometric shape; emits changed. Failures are not returned.
     */
    void setStorageExpB(double v);
    /**
     * @brief Update the constant functional storage surface area C in squared project length units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads the functional (A,B,C) tuple and preserves sibling terms. An accepted engine write switches storage to FUNCTIONAL and detaches a tabular/geometric shape; emits changed. Failures are not returned.
     */
    void setStorageConstC(double v);
    /**
     * @brief Update the first raw geometric storage dimension: full major axis/base length in project length units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads all three geometric dimensions, replaces only this component and submits the complete tuple. Engine validates the current shape atomically (positive first/second axes, nonnegative third parameter, positive third for paraboloid); only acceptance emits changed.
     */
    void setStorageParam1(double v);
    /**
     * @brief Update the second raw geometric storage dimension: full minor axis/base width in project length units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads all three geometric dimensions, replaces only this component and submits the complete tuple. Engine validates the current shape atomically (positive first/second axes, nonnegative third parameter, positive third for paraboloid); only acceptance emits changed.
     */
    void setStorageParam2(double v);
    /**
     * @brief Update the third raw storage parameter: run/rise slope for cone/pyramid or top-axis height for paraboloid.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads all three geometric dimensions, replaces only this component and submits the complete tuple. Engine validates the current shape atomically (positive first/second axes, nonnegative third parameter, positive third for paraboloid); only acceptance emits changed.
     */
    void setStorageParam3(double v);

    /*! Round-4 follow-up 2026-05-12 — force the Property Browser
     *  to re-query.  Used when the table view edits the same object
     *  externally; emitting `changed()` triggers QPropertyModel to
     *  re-read every property (each getter round-trips to the
     *  engine, so values stay authoritative).
     * @details Emits changed unconditionally to reread live properties; no engine input is written.
     */
    void refresh() { emit changed(); }

    /*! Called by PropertiesPanel after applyRename() succeeds so the
     *  adapter's stored name matches the engine's new name.
     * @param[in] newName Accepted engine ID copied verbatim; no engine rename or signal occurs here.
     */
    void updateStoredName(const QString &newName) { m_name = newName; }

    // ------------------------------------------------------------------
    // Slice QA.2 — stats-source dispatch
    // ------------------------------------------------------------------
    //
    // The four `statMax*` getters dispatch on `m_statsSourceId`:
    //   - null UUID  → today's behaviour (read from m_engine via
    //                   swmm_node_get_stat_*, i.e. the editing-engine's
    //                   ambient post-run stats).
    //   - non-null   → look up the corresponding SWMMResultsLayer via
    //                   m_statsRegistry and call its nodeStatMax*()
    //                   accessor (returns 0 until engine API QA-01 lands
    //                   — see swmmresultslayer.cpp QA.3 comment).
    //
    // setStatsRegistry is called once by PropertiesPanel when the adapter
    // is first wired up; setStatsSource is called from the combo's
    // currentIndexChanged slot every time the user picks a different
    // source. Either may be called with nullptr / null UUID to revert
    // to the editing-engine default.
    /**
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
    /*! Emitted when setName() is called with a non-empty, non-duplicate name.
     *  The attribute panel connects this to SWMMModelLayer::applyRename().
     * @param[in] oldName Stored engine ID before the proposed rename.
     * @param[in] newName Nonempty trimmed proposed ID; the receiving layer validates uniqueness and lifecycle.
     */
    void renameRequested(const QString &oldName, const QString &newName);
    /*! Slice DB — emitted when setXCoord() or setYCoord() is called with a
     *  value distinct from the current coord pair. The attribute panel
     *  connects this to `SWMMModelLayer::applyNodeMove`, which writes the
     *  engine + rebuilds the scene cache + bumps link bboxes + repaints.
     *  A direct `swmm_spatial_set_node_coord` would update the engine but
     *  leave the cached scene coords stale until the next full geometry
     *  rebuild. The signal carries the full pair so the panel doesn't have
     *  to re-read the other coord.
     * @brief Request a layer-mediated map move so engine coordinates and scene caches stay synchronized.
     * @param[in] newX Proposed x in model horizontal CRS units.
     * @param[in] newY Proposed y in the same CRS units.
     * @details The signal does not itself change coordinates; the panel/layer applies the move.
     */
    void coordChangeRequested(double newX, double newY);
    /*! Emitted whenever the active unit system changes, so the
     *  Property Browser can refresh column-0 labels (e.g. swap
     *  "(ft)" → "(m)") without rebuilding the property tree.
     * @brief Request property labels to be rebuilt after active-project units change, without changing stored input values.
     */
    void displayLabelsChanged();

protected:
    /**
     * @brief Resolve the stored node ID in the current engine.
     * @return Zero-based index, or -1 for missing engine/name/object; no index is cached across renames.
     */
    [[nodiscard]] int nodeIdx() const;

    /*! Read-modify-write of this node's SWMM_InletUsage row as ONE undoable
     *  step (SetInletUsageCommand via SWMMModelLayer::pushInletUsageEdit).
     *  \p mutate receives the current row — or an engine-default one when
     *  the node has none yet — and returns false to cancel the write (a
     *  no-op edit, or a name that did not resolve). Requires a bound model
     *  layer; without one it does nothing.
     * @brief Read-modify-submit one complete node-hosted inlet record as a shared undoable edit.
     * @param[in] mutate Synchronous callback receiving the current row or engine-default row; false abandons the request.
     * @details Requires a bound layer and valid node. Missing rows default to one unclogged inlet, no limit/depression and automatic placement; incomplete design/capture identities are not submitted. Emits changed after pushInletUsageEdit(), without exposing its failure result.
     */
    void writeInletUsage(const std::function<bool(SWMM_InletUsage &)> &mutate);

    /*! 1D↔2D coupling helpers: an authored parameter token ("" = none), and
     *  a write of the whole state as one undoable step.
     * @brief Read one explicit 1D/2D node-coupling parameter token.
     * @param[in] key Canonical uppercase parameter name.
     * @return Authored text or empty for absent/read failure.
     */
    [[nodiscard]] QString couplingParam(const QString &key) const;
    /**
     * @brief Insert/remove one token in a snapshot of the current node-coupling state.
     * @param[in] key Canonical parameter key defining engine units/ranges.
     * @param[in] token Trimmed value; empty removes the override so defaults apply.
     * @details A successful state read is required. Marks the row authored, then delegates to writeCoupling(); parser/physical validation remains in the shared writer/engine.
     */
    void setCouplingParam(const QString &key, const QString &token);
    /**
     * @brief Submit a complete reviewed coupling-state value.
     * @param[in] s Desired state for this stored node identity.
     * @details Equal readable states are ignored. Bound layers push an undoable coupling edit; otherwise writes directly. Emits changed after submission even if refused, so the UI restores effective state.
     */
    void writeCoupling(const nodecoupling2d::State &s);

    SWMM_Engine m_engine;
    QString m_name;
    SWMMModelLayer *m_layer = nullptr; ///< DB.4c — for compound-cell pickers

    /// Slice QA.2 — stats-source dispatch state. Null UUID = use the
    /// editing engine's ambient stats; non-null = read from the
    /// SWMMResultsLayer that the registry maps the UUID to.
    QUuid m_statsSourceId;
    openswmmvis::OutputStatsRegistry *m_statsRegistry = nullptr;
};

/*! Junction adapter — `[JUNCTIONS]` columns:
 *  Name, Elev, MaxDepth, InitDepth, SurDepth, Aponded.
 *  Read-only summary block (Slice DB): crown elev, full volume, degree,
 *  and 4 post-run statistics. */
class SWMMJunctionPropertyAdapter : public SWMMNodePropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double maxDepth READ maxDepth WRITE setMaxDepth NOTIFY changed)
    Q_PROPERTY(double initialDepth READ initialDepth WRITE setInitialDepth NOTIFY changed)
    Q_PROPERTY(double surchargeDepth READ surchargeDepth WRITE setSurchargeDepth NOTIFY changed)
    Q_PROPERTY(double storageArea READ storageArea WRITE setStorageArea NOTIFY changed)
    Q_PROPERTY(double pondedArea READ pondedArea WRITE setPondedArea NOTIFY changed)
    // 1D↔2D coupling structure (fine-scale coupling plan §7.1).
    Q_PROPERTY(bool coupleable READ coupleable WRITE setCoupleable NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DType couplingType READ couplingType WRITE
                   setCouplingType NOTIFY changed)
    Q_PROPERTY(
        QString couplingDiameter READ couplingDiameter WRITE setCouplingDiameter NOTIFY changed)
    Q_PROPERTY(
        QString couplingPerimeter READ couplingPerimeter WRITE setCouplingPerimeter NOTIFY changed)
    Q_PROPERTY(QString couplingStructureArea READ couplingStructureArea WRITE
                   setCouplingStructureArea NOTIFY changed)
    Q_PROPERTY(QString couplingChamberArea READ couplingChamberArea WRITE setCouplingChamberArea
                   NOTIFY changed)
    Q_PROPERTY(QString couplingLidOpenFraction READ couplingLidOpenFraction WRITE
                   setCouplingLidOpenFraction NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DCrest couplingCrest READ couplingCrest WRITE
                   setCouplingCrest NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DLaw couplingLaw READ couplingLaw WRITE
                   setCouplingLaw NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DCoeffs couplingCoeffs READ couplingCoeffs WRITE
                   setCouplingCoeffs NOTIFY changed)
    Q_PROPERTY(QString couplingCw READ couplingCw WRITE setCouplingCw NOTIFY changed)
    Q_PROPERTY(QString couplingCsw READ couplingCsw WRITE setCouplingCsw NOTIFY changed)
    Q_PROPERTY(QString couplingCoi READ couplingCoi WRITE setCouplingCoi NOTIFY changed)
    Q_PROPERTY(QString couplingCoo READ couplingCoo WRITE setCouplingCoo NOTIFY changed)
    // Slice DB — read-only summary (no WRITE → QPropertyModel renders as
    // non-editable). Computed by engine at link-insert / post-run time.
    Q_PROPERTY(double crownElev READ crownElev NOTIFY changed)
    Q_PROPERTY(double fullVolume READ fullVolume NOTIFY changed)
    Q_PROPERTY(int degree READ degree NOTIFY changed)
    Q_PROPERTY(double statMaxDepth READ statMaxDepth NOTIFY changed)
    Q_PROPERTY(double statMaxOverflow READ statMaxOverflow NOTIFY changed)
    Q_PROPERTY(double statVolFlooded READ statVolFlooded NOTIFY changed)
    Q_PROPERTY(double statTimeFlooded READ statTimeFlooded NOTIFY changed)
    // Slice DB.2 — compound-attribute "Edit…" buttons.
    Q_PROPERTY(NodeCompoundEditRef inflows READ inflowsRef WRITE setInflowsRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef dwf READ dwfRef WRITE setDwfRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef rdii READ rdiiRef WRITE setRdiiRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef treatment READ treatmentRef WRITE setTreatmentRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef groundwaterSources READ groundwaterSourcesRef WRITE
                   setGroundwaterSourcesRef NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality READ initialQualityRef WRITE
                   setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMNodePropertyAdapter::SWMMNodePropertyAdapter;
};

/*! Virtual-junction adapter — `[VIRTUAL_JUNCTIONS]` carries only Name and
 *  Elev; everything else is derived from the two attached conduits (max
 *  depth = pipe crown, zero surcharge depth / ponded area), so this adapter
 *  intentionally exposes NO depth / ponding attributes. The invert stays
 *  editable on the base adapter — with zero offsets the two conduit end
 *  elevations follow the node invert directly, so editing it IS the
 *  grade-break write-through. Read-only summary mirrors the junction
 *  adapter's computed block.
 *
 *  Point lateral inflows are allowed at a virtual junction (engine plan
 *  VJ_LATERAL_INFLOW_PLAN_2026-09-04: the zero-storage update integrates
 *  them; only 2D surface coupling stays prohibited), so the Inflows / DWF /
 *  RDII / Treatment compound rows are exposed exactly as on a junction —
 *  the attribute table already showed them on virtual-junction rows.
 *
 *  The one editable depth is `rimDepth` — the optional `[VIRTUAL_JUNCTIONS]`
 *  MaxDepth, which supplies the ground elevation profile views draw. It feeds
 *  no hydraulics; the solver keeps using the pipe crown. */
class SWMMVirtualJunctionPropertyAdapter : public SWMMNodePropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double rimDepth READ rimDepth WRITE setRimDepth NOTIFY changed)
    Q_PROPERTY(double crownElev READ crownElev NOTIFY changed)
    Q_PROPERTY(int degree READ degree NOTIFY changed)
    Q_PROPERTY(double statMaxDepth READ statMaxDepth NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef inflows READ inflowsRef WRITE setInflowsRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef dwf READ dwfRef WRITE setDwfRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef rdii READ rdiiRef WRITE setRdiiRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef treatment READ treatmentRef WRITE setTreatmentRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef groundwaterSources READ groundwaterSourcesRef WRITE
                   setGroundwaterSourcesRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMNodePropertyAdapter::SWMMNodePropertyAdapter;
};

/*! Inlet-junction adapter — a virtual junction that additionally owns one
 *  `[INLET_USAGE]` row (host_kind = node). It inherits the whole
 *  virtual-junction surface (derived depths, editable invert, the lateral
 *  inflow / DWF / RDII / treatment compound rows) and adds the eight editable
 *  usage fields plus a read-only context block.
 *
 *  `rimDepth` is re-labelled "Street Max Depth": unlike a sealed virtual
 *  junction, an inlet junction floods above the street section's full depth,
 *  and this is the optional `[INLET_JUNCTIONS]` MaxDepth override for it (see
 *  displayLabelFor). It is still not read by the solver's routing — it is the
 *  flood threshold and the ground line profile views draw.
 *
 *  Every editable row writes ONE SWMM_InletUsage through
 *  `SWMMModelLayer::applySetInletUsage` wrapped in a SetInletUsageCommand,
 *  so an edit here and the same edit made from a conduit's Inlets page are
 *  literally the same undoable operation. */
class SWMMInletJunctionPropertyAdapter : public SWMMVirtualJunctionPropertyAdapter
{
    Q_OBJECT
    // rimDepth / crownElev / degree are INHERITED from the virtual-junction
    // adapter and deliberately not redeclared: QPropertyModel builds its rows
    // from the whole metaObject, so a redeclaration would render the row
    // twice. rimDepth's inlet-specific label ("Street Max Depth") comes from
    // displayLabelFor(), which branches on nodeKind().
    Q_PROPERTY(DataObjectRef inletDesign READ inletDesignRef WRITE setInletDesignRef NOTIFY changed)
    Q_PROPERTY(DataObjectRef captureNode READ captureNodeRef WRITE setCaptureNodeRef NOTIFY changed)
    Q_PROPERTY(int numInlets READ numInlets WRITE setNumInlets NOTIFY changed)
    Q_PROPERTY(double pctClogged READ pctClogged WRITE setPctClogged NOTIFY changed)
    Q_PROPERTY(double flowRestriction READ flowRestriction WRITE setFlowRestriction NOTIFY changed)
    Q_PROPERTY(
        double depressionHeight READ depressionHeight WRITE setDepressionHeight NOTIFY changed)
    Q_PROPERTY(double depressionWidth READ depressionWidth WRITE setDepressionWidth NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::InletPlacement placement READ placement WRITE setPlacement
                   NOTIFY changed)
    // Read-only context (no WRITE → QPropertyModel renders non-editable).
    Q_PROPERTY(QString approachStreet READ approachStreet NOTIFY changed)
public:
    /*! Spelled out rather than `using Base::Base` because the direct base
     *  only INHERITS its constructor; re-inheriting an inherited constructor
     *  is legal but reads as an accident.
     * @brief Construct the inlet-junction property facade through the virtual-junction adapter.
     * @param[in] engine Borrowed model engine; lifetime requirements match SWMMNodePropertyAdapter.
     * @param[in] name Stored inlet-junction ID.
     * @param[in] parent Optional Qt owner; use on the GUI thread.
     */
    SWMMInletJunctionPropertyAdapter(SWMM_Engine engine, QString name, QObject *parent = nullptr)
        : SWMMVirtualJunctionPropertyAdapter(engine, name, parent)
    {
    }
};

/*! Outfall adapter — `[OUTFALLS]` columns:
 *  Name, Elev, Type, Gated, (StageData when FIXED).  No depth/ponding
 *  attributes — those don't apply at a boundary node. */
class SWMMOutfallPropertyAdapter : public SWMMNodePropertyAdapter
{
    Q_OBJECT
    // Enum properties on the derived class need the type spelled with
    // its base-class scope so moc can resolve it without re-declaring
    // Q_ENUM in every subclass.
    Q_PROPERTY(SWMMNodePropertyAdapter::OutfallType outfallType READ outfallType WRITE
                   setOutfallType NOTIFY changed)
    // Slice DA.4.3 — stage-data per-type rows. PropertiesPanel toggles
    // their `isEditable` flag based on the live `outfallType` so the
    // user can only edit the row that matches the current type.
    Q_PROPERTY(double outfallStage READ outfallStage WRITE setOutfallStage NOTIFY changed)
    Q_PROPERTY(DataObjectRef outfallTidalCurve READ outfallTidalCurveRef WRITE
                   setOutfallTidalCurveRef NOTIFY changed)
    Q_PROPERTY(DataObjectRef outfallTimeseries READ outfallTimeseriesRef WRITE
                   setOutfallTimeseriesRef NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::FlapGate outfallFlapGate READ outfallFlapGate WRITE
                   setOutfallFlapGate NOTIFY changed)
    // 1D↔2D coupling: an outfall is OUTFALL_2D or NOT_COUPLEABLE; a flap
    // gate blocks only reverse flow, the 2D tailwater still applies.
    Q_PROPERTY(bool coupleable READ coupleable WRITE setCoupleable NOTIFY changed)
    Q_PROPERTY(QString couplingSpread READ couplingSpread WRITE setCouplingSpread NOTIFY changed)
    Q_PROPERTY(QString couplingTailwaterRelax READ couplingTailwaterRelax WRITE
                   setCouplingTailwaterRelax NOTIFY changed)
    // Slice DB — same read-only summary block as the other node kinds.
    // Stat values are still meaningful at outfalls (max depth = stage,
    // max overflow is always zero by definition but kept for parity).
    Q_PROPERTY(double crownElev READ crownElev NOTIFY changed)
    Q_PROPERTY(double fullVolume READ fullVolume NOTIFY changed)
    Q_PROPERTY(int degree READ degree NOTIFY changed)
    Q_PROPERTY(double statMaxDepth READ statMaxDepth NOTIFY changed)
    Q_PROPERTY(double statMaxOverflow READ statMaxOverflow NOTIFY changed)
    Q_PROPERTY(double statVolFlooded READ statVolFlooded NOTIFY changed)
    Q_PROPERTY(double statTimeFlooded READ statTimeFlooded NOTIFY changed)
    // Slice DB.2 — compound-attribute "Edit…" buttons. SWMM allows
    // Inflows / DWF / Treatment at boundary nodes; RDII surfaced for
    // parity though legacy SWMM-GUI grays it out for outfalls.
    Q_PROPERTY(NodeCompoundEditRef inflows READ inflowsRef WRITE setInflowsRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef dwf READ dwfRef WRITE setDwfRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef rdii READ rdiiRef WRITE setRdiiRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef treatment READ treatmentRef WRITE setTreatmentRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef groundwaterSources READ groundwaterSourcesRef WRITE
                   setGroundwaterSourcesRef NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality READ initialQualityRef WRITE
                   setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMNodePropertyAdapter::SWMMNodePropertyAdapter;
};

/*! Storage adapter — `[STORAGE]` columns:
 *  Name, Elev, MaxDepth, InitDepth, Shape (curve/functional), SurDepth.
 *  Adds seepage rate (engine accessors exist).  Storage curve picker
 *  and functional coefficients land in AG.4. */
class SWMMStoragePropertyAdapter : public SWMMNodePropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(DataObjectRef lidControl READ lidControlRef WRITE setLidControlRef NOTIFY changed)
    Q_PROPERTY(double lidInitialSaturation READ lidInitialSaturation WRITE setLidInitialSaturation
                   NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef lidBottomConnection READ lidBottomConnectionRef WRITE
                   setLidBottomConnectionRef NOTIFY changed)
    Q_PROPERTY(double maxDepth READ maxDepth WRITE setMaxDepth NOTIFY changed)
    Q_PROPERTY(double initialDepth READ initialDepth WRITE setInitialDepth NOTIFY changed)
    Q_PROPERTY(double surchargeDepth READ surchargeDepth WRITE setSurchargeDepth NOTIFY changed)
    Q_PROPERTY(double seepRate READ seepRate WRITE setSeepRate NOTIFY changed)
    // 1D↔2D coupling structure (fine-scale coupling plan §7.1).
    Q_PROPERTY(bool coupleable READ coupleable WRITE setCoupleable NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DType couplingType READ couplingType WRITE
                   setCouplingType NOTIFY changed)
    Q_PROPERTY(
        QString couplingDiameter READ couplingDiameter WRITE setCouplingDiameter NOTIFY changed)
    Q_PROPERTY(
        QString couplingPerimeter READ couplingPerimeter WRITE setCouplingPerimeter NOTIFY changed)
    Q_PROPERTY(QString couplingStructureArea READ couplingStructureArea WRITE
                   setCouplingStructureArea NOTIFY changed)
    Q_PROPERTY(QString couplingChamberArea READ couplingChamberArea WRITE setCouplingChamberArea
                   NOTIFY changed)
    Q_PROPERTY(QString couplingLidOpenFraction READ couplingLidOpenFraction WRITE
                   setCouplingLidOpenFraction NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DCrest couplingCrest READ couplingCrest WRITE
                   setCouplingCrest NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DLaw couplingLaw READ couplingLaw WRITE
                   setCouplingLaw NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DCoeffs couplingCoeffs READ couplingCoeffs WRITE
                   setCouplingCoeffs NOTIFY changed)
    Q_PROPERTY(QString couplingCw READ couplingCw WRITE setCouplingCw NOTIFY changed)
    Q_PROPERTY(QString couplingCsw READ couplingCsw WRITE setCouplingCsw NOTIFY changed)
    Q_PROPERTY(QString couplingCoi READ couplingCoi WRITE setCouplingCoi NOTIFY changed)
    Q_PROPERTY(QString couplingCoo READ couplingCoo WRITE setCouplingCoo NOTIFY changed)
    // Slice AG.4 — storage geometry. The Shape combobox switches between a tabular
    // depth–area curve, the functional power-law form, and the four geometric shapes.
    // PropertiesPanel greys out the rows that don't apply to the live shape (curve vs.
    // coefficients vs. dimensions), mirroring the outfall stage-data wiring.
    //
    // The three dimension rows keep GENERIC names (Param 1/2/3) because QPropertyModel
    // reflects rows from metaObject() and cannot rename or hide them at runtime; the
    // shape-specific meaning ("Major Axis Length", "Side Slope", …) is supplied as the
    // row label + tooltip via storageshapegeom.h. This is the same compromise the link
    // xsect editor already makes for geom1..geom4.
    Q_PROPERTY(SWMMNodePropertyAdapter::StorageShape storageShape READ storageShape WRITE
                   setStorageShape NOTIFY changed)
    Q_PROPERTY(
        DataObjectRef storageCurve READ storageCurveRef WRITE setStorageCurveRef NOTIFY changed)
    Q_PROPERTY(double storageCoeffA READ storageCoeffA WRITE setStorageCoeffA NOTIFY changed)
    Q_PROPERTY(double storageExpB READ storageExpB WRITE setStorageExpB NOTIFY changed)
    Q_PROPERTY(double storageConstC READ storageConstC WRITE setStorageConstC NOTIFY changed)
    Q_PROPERTY(double storageParam1 READ storageParam1 WRITE setStorageParam1 NOTIFY changed)
    Q_PROPERTY(double storageParam2 READ storageParam2 WRITE setStorageParam2 NOTIFY changed)
    Q_PROPERTY(double storageParam3 READ storageParam3 WRITE setStorageParam3 NOTIFY changed)
    // Slice DB — same read-only summary block; full volume is especially
    // useful at storage nodes because it's the integrated curve volume.
    Q_PROPERTY(double crownElev READ crownElev NOTIFY changed)
    Q_PROPERTY(double fullVolume READ fullVolume NOTIFY changed)
    Q_PROPERTY(int degree READ degree NOTIFY changed)
    Q_PROPERTY(double statMaxDepth READ statMaxDepth NOTIFY changed)
    Q_PROPERTY(double statMaxOverflow READ statMaxOverflow NOTIFY changed)
    Q_PROPERTY(double statVolFlooded READ statVolFlooded NOTIFY changed)
    Q_PROPERTY(double statTimeFlooded READ statTimeFlooded NOTIFY changed)
    // Slice DB.2 — compound-attribute "Edit…" buttons.
    Q_PROPERTY(NodeCompoundEditRef inflows READ inflowsRef WRITE setInflowsRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef dwf READ dwfRef WRITE setDwfRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef rdii READ rdiiRef WRITE setRdiiRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef treatment READ treatmentRef WRITE setTreatmentRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef groundwaterSources READ groundwaterSourcesRef WRITE
                   setGroundwaterSourcesRef NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality READ initialQualityRef WRITE
                   setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMNodePropertyAdapter::SWMMNodePropertyAdapter;
};

/*! Divider adapter — `[DIVIDERS]` columns:
 *  Name, Elev, DivLink, DivType, (type-specific params), MaxDepth,
 *  InitDepth, SurDepth, Aponded.  DivLink and type-specific
 *  coefficients land in AG.4. */
class SWMMDividerPropertyAdapter : public SWMMNodePropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(SWMMNodePropertyAdapter::DividerType dividerType READ dividerType WRITE
                   setDividerType NOTIFY changed)
    Q_PROPERTY(double maxDepth READ maxDepth WRITE setMaxDepth NOTIFY changed)
    Q_PROPERTY(double initialDepth READ initialDepth WRITE setInitialDepth NOTIFY changed)
    Q_PROPERTY(double surchargeDepth READ surchargeDepth WRITE setSurchargeDepth NOTIFY changed)
    Q_PROPERTY(double pondedArea READ pondedArea WRITE setPondedArea NOTIFY changed)
    // 1D↔2D coupling structure (fine-scale coupling plan §7.1).
    Q_PROPERTY(bool coupleable READ coupleable WRITE setCoupleable NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DType couplingType READ couplingType WRITE
                   setCouplingType NOTIFY changed)
    Q_PROPERTY(
        QString couplingDiameter READ couplingDiameter WRITE setCouplingDiameter NOTIFY changed)
    Q_PROPERTY(
        QString couplingPerimeter READ couplingPerimeter WRITE setCouplingPerimeter NOTIFY changed)
    Q_PROPERTY(QString couplingStructureArea READ couplingStructureArea WRITE
                   setCouplingStructureArea NOTIFY changed)
    Q_PROPERTY(QString couplingChamberArea READ couplingChamberArea WRITE setCouplingChamberArea
                   NOTIFY changed)
    Q_PROPERTY(QString couplingLidOpenFraction READ couplingLidOpenFraction WRITE
                   setCouplingLidOpenFraction NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DCrest couplingCrest READ couplingCrest WRITE
                   setCouplingCrest NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DLaw couplingLaw READ couplingLaw WRITE
                   setCouplingLaw NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::Coupling2DCoeffs couplingCoeffs READ couplingCoeffs WRITE
                   setCouplingCoeffs NOTIFY changed)
    Q_PROPERTY(QString couplingCw READ couplingCw WRITE setCouplingCw NOTIFY changed)
    Q_PROPERTY(QString couplingCsw READ couplingCsw WRITE setCouplingCsw NOTIFY changed)
    Q_PROPERTY(QString couplingCoi READ couplingCoi WRITE setCouplingCoi NOTIFY changed)
    Q_PROPERTY(QString couplingCoo READ couplingCoo WRITE setCouplingCoo NOTIFY changed)
    // Slice DB — same read-only summary block.
    Q_PROPERTY(double crownElev READ crownElev NOTIFY changed)
    Q_PROPERTY(double fullVolume READ fullVolume NOTIFY changed)
    Q_PROPERTY(int degree READ degree NOTIFY changed)
    Q_PROPERTY(double statMaxDepth READ statMaxDepth NOTIFY changed)
    Q_PROPERTY(double statMaxOverflow READ statMaxOverflow NOTIFY changed)
    Q_PROPERTY(double statVolFlooded READ statVolFlooded NOTIFY changed)
    Q_PROPERTY(double statTimeFlooded READ statTimeFlooded NOTIFY changed)
    // Slice DB.2 — compound-attribute "Edit…" buttons.
    Q_PROPERTY(NodeCompoundEditRef inflows READ inflowsRef WRITE setInflowsRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef dwf READ dwfRef WRITE setDwfRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef rdii READ rdiiRef WRITE setRdiiRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef treatment READ treatmentRef WRITE setTreatmentRef NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef groundwaterSources READ groundwaterSourcesRef WRITE
                   setGroundwaterSourcesRef NOTIFY changed)
    Q_PROPERTY(InitialQualityEditRef initialQuality READ initialQualityRef WRITE
                   setInitialQualityRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMNodePropertyAdapter::SWMMNodePropertyAdapter;
};

#endif // SWMMNODEPROPERTYADAPTER_H
