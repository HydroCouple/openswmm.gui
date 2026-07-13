/*!
 * \file   swmmnodepropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */

#ifndef SWMMNODEPROPERTYADAPTER_H
#define SWMMNODEPROPERTYADAPTER_H

#include <QObject>
#include <QString>
#include <QUuid>                            // Slice QA.2 — stats-source identity

#include <openswmm/engine/openswmm_engine.h>

#include "ui/properties/dataobjectref.h"
#include "ui/properties/nodecompoundeditref.h"
#include "ui/properties/userflagseditref.h"

class SWMMModelLayer;
namespace openswmmvis { class OutputStatsRegistry; }    // Slice QA.2

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
    enum NodeKind    { Junction = 0, Outfall = 1, Storage = 2, Divider = 3 };
    enum OutfallType { FREE = 0, NORMAL = 1, FIXED = 2, TIDAL = 3, TIMESERIES = 4 };
    enum DividerType { CUTOFF = 0, OVERFLOW_ = 1, TABULAR = 2, WEIR = 3 };
    enum FlapGate    { NO = 0, YES = 1 };
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
        Tabular     = 0,
        Functional  = 1,
        Cylindrical = 2,
        Conical     = 3,
        Paraboloid  = 4,
        Pyramidal   = 5
    };
    Q_ENUM(NodeKind)
    Q_ENUM(OutfallType)
    Q_ENUM(DividerType)
    Q_ENUM(FlapGate)
    Q_ENUM(StorageShape)

    // Common to every node type — Name + Type + Invert + Coords + Tag.
    Q_PROPERTY(QString  name        READ name  WRITE setName)
    Q_PROPERTY(NodeKind nodeKind    READ nodeKind)
    Q_PROPERTY(double   invertElev  READ invertElev  WRITE setInvertElev  NOTIFY changed)
    /*! Free-form `[TAGS]` label (DB.3 engine surface). Editable text;
     *  index-keyed in the engine, so survives a rename. */
    Q_PROPERTY(QString  tag         READ tag         WRITE setTag         NOTIFY changed)
    // Slice DB — coordinates editable. Writes go through `coordChangeRequested`
    // so the bound layer can route them via `applyNodeMove`, which refreshes
    // the cached scene coords + attached-link bboxes (a raw engine setter
    // would leave the canvas stale until the next full rebuild).
    Q_PROPERTY(double   xCoord      READ xCoord      WRITE setXCoord      NOTIFY changed)
    Q_PROPERTY(double   yCoord      READ yCoord      WRITE setYCoord      NOTIFY changed)

    /*! \param engine  Engine handle (non-null).
     *  \param name    Node id (must already exist on the engine). */
    SWMMNodePropertyAdapter(SWMM_Engine engine, QString name,
                              QObject *parent = nullptr);

    /*! DB.4c — set the model layer the compound-edit pickers can call
     *  back into to create new TS / patterns / UHs. Optional. */
    void setModelLayer(SWMMModelLayer *layer) { m_layer = layer; }
    [[nodiscard]] SWMMModelLayer *modelLayer() const { return m_layer; }

    [[nodiscard]] QString    name() const     { return m_name; }
    [[nodiscard]] NodeKind   nodeKind() const;

    [[nodiscard]] QString tag() const;
    [[nodiscard]] double invertElev() const;
    [[nodiscard]] double maxDepth() const;
    [[nodiscard]] double initialDepth() const;
    [[nodiscard]] double surchargeDepth() const;
    [[nodiscard]] double pondedArea() const;
    [[nodiscard]] double seepRate() const;
    [[nodiscard]] double xCoord() const;
    [[nodiscard]] double yCoord() const;
    [[nodiscard]] OutfallType outfallType() const;
    [[nodiscard]] FlapGate    outfallFlapGate() const;
    [[nodiscard]] DividerType dividerType() const;

    // Slice DB — read-only computed + statistics summary getters.
    // `crownElev` / `fullVolume` / `degree` are populated at input time
    // (computed when links connect at load). The four `stat*` values
    // are populated only after a simulation run; pre-run they read back
    // as zero. Exposed via Q_PROPERTY without WRITE so QPropertyModel
    // renders them as non-editable rows.
    [[nodiscard]] double crownElev() const;
    [[nodiscard]] double fullVolume() const;
    [[nodiscard]] int    degree() const;
    [[nodiscard]] double statMaxDepth() const;
    [[nodiscard]] double statMaxOverflow() const;
    [[nodiscard]] double statVolFlooded() const;
    [[nodiscard]] double statTimeFlooded() const;

    // Slice DB.2 — compound-attribute refs. Each returns a value that
    // the registered `NodeCompoundEditButton` editor creator picks up
    // via the custom metatype dispatch; clicking the cell opens the
    // matching page of `NodeCompoundEditDialog`. The `summary` field
    // is computed live from the engine count so the cell shows a
    // useful preview even before the user opens the dialog.
    [[nodiscard]] NodeCompoundEditRef inflowsRef()  const;
    [[nodiscard]] NodeCompoundEditRef dwfRef()      const;
    [[nodiscard]] NodeCompoundEditRef rdiiRef()     const;
    [[nodiscard]] NodeCompoundEditRef treatmentRef() const;

    /*! Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
     *  user-flag assignments row. Same compound-ref pattern as the
     *  accessors above; the UserFlagsEditButton editor opens
     *  UserFlagValuesDialog. Requires setModelLayer() for the shared
     *  UserFlagsModel; without a layer the cell disables gracefully. */
    [[nodiscard]] UserFlagsEditRef userFlagsRef() const;

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
    [[nodiscard]] double         outfallStage()            const;
    [[nodiscard]] DataObjectRef  outfallTidalCurveRef()    const;
    [[nodiscard]] DataObjectRef  outfallTimeseriesRef()    const;

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
    [[nodiscard]] StorageShape   storageShape()      const;
    [[nodiscard]] DataObjectRef  storageCurveRef()   const;
    [[nodiscard]] double         storageCoeffA()     const;
    [[nodiscard]] double         storageExpB()       const;
    [[nodiscard]] double         storageConstC()     const;
    [[nodiscard]] double         storageParam1()     const;
    [[nodiscard]] double         storageParam2()     const;
    [[nodiscard]] double         storageParam3()     const;

    /*! Maps a Q_PROPERTY identifier (e.g. \c "maxDepth") to a
     *  human-readable column-0 label (e.g. \c "Max Depth (ft)") with
     *  unit suffixes pulled live from \c UnitSystem::instance().
     *  Q_INVOKABLE so QPropertyModel can pick it up generically without
     *  taking a SWMM-specific dependency.  Returns an empty string for
     *  unknown property names — the model then falls back to the raw
     *  identifier. */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setName(const QString &newName);
    void setTag(const QString &t);
    void setInvertElev(double v);
    void setMaxDepth(double v);
    void setInitialDepth(double v);
    void setSurchargeDepth(double v);
    void setPondedArea(double v);
    void setSeepRate(double v);
    void setXCoord(double v);
    void setYCoord(double v);
    void setOutfallType(OutfallType v);
    void setOutfallFlapGate(FlapGate v);
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
     *  registered NodeCompoundEditButton creator never fires. */
    void setInflowsRef(const NodeCompoundEditRef &)   { emit changed(); }
    void setDwfRef(const NodeCompoundEditRef &)       { emit changed(); }
    void setRdiiRef(const NodeCompoundEditRef &)      { emit changed(); }
    void setTreatmentRef(const NodeCompoundEditRef &) { emit changed(); }
    void setUserFlagsRef(const UserFlagsEditRef &)    { emit changed(); }

    // Slice DA.4.3 — outfall stage-data setters. Each calls the matching
    // swmm_node_set_outfall_* engine setter, which also flips the outfall
    // type to the matching kind (FIXED / TIDAL / TIMESERIES) — that's a
    // deliberate engine invariant. So calling setOutfallStage()
    // implicitly switches the outfall to FIXED. The setRowEditable
    // wiring in PropertiesPanel re-runs on every `changed()` and updates
    // which row is enabled accordingly.
    void setOutfallStage(double v);
    void setOutfallTidalCurveRef(const DataObjectRef &r);
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
    void setStorageShape(StorageShape v);
    void setStorageCurveRef(const DataObjectRef &r);
    void setStorageCoeffA(double v);
    void setStorageExpB(double v);
    void setStorageConstC(double v);
    void setStorageParam1(double v);
    void setStorageParam2(double v);
    void setStorageParam3(double v);

    /*! Round-4 follow-up 2026-05-12 — force the Property Browser
     *  to re-query.  Used when the table view edits the same object
     *  externally; emitting `changed()` triggers QPropertyModel to
     *  re-read every property (each getter round-trips to the
     *  engine, so values stay authoritative). */
    void refresh() { emit changed(); }

    /*! Called by PropertiesPanel after applyRename() succeeds so the
     *  adapter's stored name matches the engine's new name. */
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
    void setStatsRegistry(openswmmvis::OutputStatsRegistry *registry);
    void setStatsSource(const QUuid &id);
    [[nodiscard]] QUuid statsSourceId() const { return m_statsSourceId; }

signals:
    void changed();
    /*! Emitted when setName() is called with a non-empty, non-duplicate name.
     *  The attribute panel connects this to SWMMModelLayer::applyRename(). */
    void renameRequested(const QString &oldName, const QString &newName);
    /*! Slice DB — emitted when setXCoord() or setYCoord() is called with a
     *  value distinct from the current coord pair. The attribute panel
     *  connects this to `SWMMModelLayer::applyNodeMove`, which writes the
     *  engine + rebuilds the scene cache + bumps link bboxes + repaints.
     *  A direct `swmm_spatial_set_node_coord` would update the engine but
     *  leave the cached scene coords stale until the next full geometry
     *  rebuild. The signal carries the full pair so the panel doesn't have
     *  to re-read the other coord. */
    void coordChangeRequested(double newX, double newY);
    /*! Emitted whenever the active unit system changes, so the
     *  Property Browser can refresh column-0 labels (e.g. swap
     *  "(ft)" → "(m)") without rebuilding the property tree. */
    void displayLabelsChanged();

protected:
    [[nodiscard]] int nodeIdx() const;

    SWMM_Engine     m_engine;
    QString         m_name;
    SWMMModelLayer *m_layer = nullptr;  ///< DB.4c — for compound-cell pickers

    /// Slice QA.2 — stats-source dispatch state. Null UUID = use the
    /// editing engine's ambient stats; non-null = read from the
    /// SWMMResultsLayer that the registry maps the UUID to.
    QUuid                              m_statsSourceId;
    openswmmvis::OutputStatsRegistry  *m_statsRegistry = nullptr;
};

/*! Junction adapter — `[JUNCTIONS]` columns:
 *  Name, Elev, MaxDepth, InitDepth, SurDepth, Aponded.
 *  Read-only summary block (Slice DB): crown elev, full volume, degree,
 *  and 4 post-run statistics. */
class SWMMJunctionPropertyAdapter : public SWMMNodePropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double maxDepth        READ maxDepth        WRITE setMaxDepth        NOTIFY changed)
    Q_PROPERTY(double initialDepth    READ initialDepth    WRITE setInitialDepth    NOTIFY changed)
    Q_PROPERTY(double surchargeDepth  READ surchargeDepth  WRITE setSurchargeDepth  NOTIFY changed)
    Q_PROPERTY(double pondedArea      READ pondedArea      WRITE setPondedArea      NOTIFY changed)
    // Slice DB — read-only summary (no WRITE → QPropertyModel renders as
    // non-editable). Computed by engine at link-insert / post-run time.
    Q_PROPERTY(double crownElev       READ crownElev       NOTIFY changed)
    Q_PROPERTY(double fullVolume      READ fullVolume      NOTIFY changed)
    Q_PROPERTY(int    degree          READ degree          NOTIFY changed)
    Q_PROPERTY(double statMaxDepth    READ statMaxDepth    NOTIFY changed)
    Q_PROPERTY(double statMaxOverflow READ statMaxOverflow NOTIFY changed)
    Q_PROPERTY(double statVolFlooded  READ statVolFlooded  NOTIFY changed)
    Q_PROPERTY(double statTimeFlooded READ statTimeFlooded NOTIFY changed)
    // Slice DB.2 — compound-attribute "Edit…" buttons.
    Q_PROPERTY(NodeCompoundEditRef inflows
               READ inflowsRef   WRITE setInflowsRef   NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef dwf
               READ dwfRef       WRITE setDwfRef       NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef rdii
               READ rdiiRef      WRITE setRdiiRef      NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef treatment
               READ treatmentRef WRITE setTreatmentRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMNodePropertyAdapter::SWMMNodePropertyAdapter;
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
    Q_PROPERTY(SWMMNodePropertyAdapter::OutfallType outfallType
               READ outfallType WRITE setOutfallType NOTIFY changed)
    // Slice DA.4.3 — stage-data per-type rows. PropertiesPanel toggles
    // their `isEditable` flag based on the live `outfallType` so the
    // user can only edit the row that matches the current type.
    Q_PROPERTY(double         outfallStage
               READ outfallStage       WRITE setOutfallStage         NOTIFY changed)
    Q_PROPERTY(DataObjectRef  outfallTidalCurve
               READ outfallTidalCurveRef  WRITE setOutfallTidalCurveRef  NOTIFY changed)
    Q_PROPERTY(DataObjectRef  outfallTimeseries
               READ outfallTimeseriesRef  WRITE setOutfallTimeseriesRef  NOTIFY changed)
    Q_PROPERTY(SWMMNodePropertyAdapter::FlapGate    outfallFlapGate
               READ outfallFlapGate WRITE setOutfallFlapGate NOTIFY changed)
    // Slice DB — same read-only summary block as the other node kinds.
    // Stat values are still meaningful at outfalls (max depth = stage,
    // max overflow is always zero by definition but kept for parity).
    Q_PROPERTY(double crownElev       READ crownElev       NOTIFY changed)
    Q_PROPERTY(double fullVolume      READ fullVolume      NOTIFY changed)
    Q_PROPERTY(int    degree          READ degree          NOTIFY changed)
    Q_PROPERTY(double statMaxDepth    READ statMaxDepth    NOTIFY changed)
    Q_PROPERTY(double statMaxOverflow READ statMaxOverflow NOTIFY changed)
    Q_PROPERTY(double statVolFlooded  READ statVolFlooded  NOTIFY changed)
    Q_PROPERTY(double statTimeFlooded READ statTimeFlooded NOTIFY changed)
    // Slice DB.2 — compound-attribute "Edit…" buttons. SWMM allows
    // Inflows / DWF / Treatment at boundary nodes; RDII surfaced for
    // parity though legacy SWMM-GUI grays it out for outfalls.
    Q_PROPERTY(NodeCompoundEditRef inflows
               READ inflowsRef   WRITE setInflowsRef   NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef dwf
               READ dwfRef       WRITE setDwfRef       NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef rdii
               READ rdiiRef      WRITE setRdiiRef      NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef treatment
               READ treatmentRef WRITE setTreatmentRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
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
    Q_PROPERTY(double maxDepth        READ maxDepth        WRITE setMaxDepth        NOTIFY changed)
    Q_PROPERTY(double initialDepth    READ initialDepth    WRITE setInitialDepth    NOTIFY changed)
    Q_PROPERTY(double surchargeDepth  READ surchargeDepth  WRITE setSurchargeDepth  NOTIFY changed)
    Q_PROPERTY(double seepRate        READ seepRate        WRITE setSeepRate        NOTIFY changed)
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
    Q_PROPERTY(SWMMNodePropertyAdapter::StorageShape storageShape
               READ storageShape    WRITE setStorageShape    NOTIFY changed)
    Q_PROPERTY(DataObjectRef storageCurve
               READ storageCurveRef WRITE setStorageCurveRef NOTIFY changed)
    Q_PROPERTY(double storageCoeffA   READ storageCoeffA   WRITE setStorageCoeffA   NOTIFY changed)
    Q_PROPERTY(double storageExpB     READ storageExpB     WRITE setStorageExpB     NOTIFY changed)
    Q_PROPERTY(double storageConstC   READ storageConstC   WRITE setStorageConstC   NOTIFY changed)
    Q_PROPERTY(double storageParam1   READ storageParam1   WRITE setStorageParam1   NOTIFY changed)
    Q_PROPERTY(double storageParam2   READ storageParam2   WRITE setStorageParam2   NOTIFY changed)
    Q_PROPERTY(double storageParam3   READ storageParam3   WRITE setStorageParam3   NOTIFY changed)
    // Slice DB — same read-only summary block; full volume is especially
    // useful at storage nodes because it's the integrated curve volume.
    Q_PROPERTY(double crownElev       READ crownElev       NOTIFY changed)
    Q_PROPERTY(double fullVolume      READ fullVolume      NOTIFY changed)
    Q_PROPERTY(int    degree          READ degree          NOTIFY changed)
    Q_PROPERTY(double statMaxDepth    READ statMaxDepth    NOTIFY changed)
    Q_PROPERTY(double statMaxOverflow READ statMaxOverflow NOTIFY changed)
    Q_PROPERTY(double statVolFlooded  READ statVolFlooded  NOTIFY changed)
    Q_PROPERTY(double statTimeFlooded READ statTimeFlooded NOTIFY changed)
    // Slice DB.2 — compound-attribute "Edit…" buttons.
    Q_PROPERTY(NodeCompoundEditRef inflows
               READ inflowsRef   WRITE setInflowsRef   NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef dwf
               READ dwfRef       WRITE setDwfRef       NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef rdii
               READ rdiiRef      WRITE setRdiiRef      NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef treatment
               READ treatmentRef WRITE setTreatmentRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
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
    Q_PROPERTY(SWMMNodePropertyAdapter::DividerType dividerType
               READ dividerType WRITE setDividerType NOTIFY changed)
    Q_PROPERTY(double maxDepth            READ maxDepth        WRITE setMaxDepth        NOTIFY changed)
    Q_PROPERTY(double initialDepth        READ initialDepth    WRITE setInitialDepth    NOTIFY changed)
    Q_PROPERTY(double surchargeDepth      READ surchargeDepth  WRITE setSurchargeDepth  NOTIFY changed)
    Q_PROPERTY(double pondedArea          READ pondedArea      WRITE setPondedArea      NOTIFY changed)
    // Slice DB — same read-only summary block.
    Q_PROPERTY(double crownElev       READ crownElev       NOTIFY changed)
    Q_PROPERTY(double fullVolume      READ fullVolume      NOTIFY changed)
    Q_PROPERTY(int    degree          READ degree          NOTIFY changed)
    Q_PROPERTY(double statMaxDepth    READ statMaxDepth    NOTIFY changed)
    Q_PROPERTY(double statMaxOverflow READ statMaxOverflow NOTIFY changed)
    Q_PROPERTY(double statVolFlooded  READ statVolFlooded  NOTIFY changed)
    Q_PROPERTY(double statTimeFlooded READ statTimeFlooded NOTIFY changed)
    // Slice DB.2 — compound-attribute "Edit…" buttons.
    Q_PROPERTY(NodeCompoundEditRef inflows
               READ inflowsRef   WRITE setInflowsRef   NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef dwf
               READ dwfRef       WRITE setDwfRef       NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef rdii
               READ rdiiRef      WRITE setRdiiRef      NOTIFY changed)
    Q_PROPERTY(NodeCompoundEditRef treatment
               READ treatmentRef WRITE setTreatmentRef NOTIFY changed)
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)
public:
    using SWMMNodePropertyAdapter::SWMMNodePropertyAdapter;
};

#endif // SWMMNODEPROPERTYADAPTER_H
