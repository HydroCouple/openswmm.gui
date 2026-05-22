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

#include <openswmm/engine/openswmm_engine.h>

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
    Q_ENUM(NodeKind)
    Q_ENUM(OutfallType)
    Q_ENUM(DividerType)
    Q_ENUM(FlapGate)

    // Common to every node type — Name + Type + Invert.
    Q_PROPERTY(QString  name        READ name  WRITE setName)
    Q_PROPERTY(NodeKind nodeKind    READ nodeKind)
    Q_PROPERTY(double   invertElev  READ invertElev  WRITE setInvertElev  NOTIFY changed)

    /*! \param engine  Engine handle (non-null).
     *  \param name    Node id (must already exist on the engine). */
    SWMMNodePropertyAdapter(SWMM_Engine engine, QString name,
                              QObject *parent = nullptr);

    [[nodiscard]] QString    name() const     { return m_name; }
    [[nodiscard]] NodeKind   nodeKind() const;

    [[nodiscard]] double invertElev() const;
    [[nodiscard]] double maxDepth() const;
    [[nodiscard]] double initialDepth() const;
    [[nodiscard]] double surchargeDepth() const;
    [[nodiscard]] double pondedArea() const;
    [[nodiscard]] double seepRate() const;
    [[nodiscard]] OutfallType outfallType() const;
    [[nodiscard]] FlapGate    outfallFlapGate() const;
    [[nodiscard]] DividerType dividerType() const;

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
    void setInvertElev(double v);
    void setMaxDepth(double v);
    void setInitialDepth(double v);
    void setSurchargeDepth(double v);
    void setPondedArea(double v);
    void setSeepRate(double v);
    void setOutfallType(OutfallType v);
    void setOutfallFlapGate(FlapGate v);
    void setDividerType(DividerType v);

    /*! Round-4 follow-up 2026-05-12 — force the Property Browser
     *  to re-query.  Used when the table view edits the same object
     *  externally; emitting `changed()` triggers QPropertyModel to
     *  re-read every property (each getter round-trips to the
     *  engine, so values stay authoritative). */
    void refresh() { emit changed(); }

    /*! Called by AttributePanel after applyRename() succeeds so the
     *  adapter's stored name matches the engine's new name. */
    void updateStoredName(const QString &newName) { m_name = newName; }

signals:
    void changed();
    /*! Emitted when setName() is called with a non-empty, non-duplicate name.
     *  The attribute panel connects this to SWMMModelLayer::applyRename(). */
    void renameRequested(const QString &oldName, const QString &newName);
    /*! Emitted whenever the active unit system changes, so the
     *  Property Browser can refresh column-0 labels (e.g. swap
     *  "(ft)" → "(m)") without rebuilding the property tree. */
    void displayLabelsChanged();

protected:
    [[nodiscard]] int nodeIdx() const;

    SWMM_Engine m_engine;
    QString     m_name;
};

/*! Junction adapter — `[JUNCTIONS]` columns:
 *  Name, Elev, MaxDepth, InitDepth, SurDepth, Aponded. */
class SWMMJunctionPropertyAdapter : public SWMMNodePropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double maxDepth        READ maxDepth        WRITE setMaxDepth        NOTIFY changed)
    Q_PROPERTY(double initialDepth    READ initialDepth    WRITE setInitialDepth    NOTIFY changed)
    Q_PROPERTY(double surchargeDepth  READ surchargeDepth  WRITE setSurchargeDepth  NOTIFY changed)
    Q_PROPERTY(double pondedArea      READ pondedArea      WRITE setPondedArea      NOTIFY changed)
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
    Q_PROPERTY(SWMMNodePropertyAdapter::FlapGate    outfallFlapGate
               READ outfallFlapGate WRITE setOutfallFlapGate NOTIFY changed)
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
public:
    using SWMMNodePropertyAdapter::SWMMNodePropertyAdapter;
};

#endif // SWMMNODEPROPERTYADAPTER_H
