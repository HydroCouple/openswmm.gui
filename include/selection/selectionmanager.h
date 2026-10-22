/**
 * @file      selectionmanager.h
 * @brief     per-project Cross-View Selection Bus.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Phase 1.4 — per-project Cross-View Selection Bus.
 *
 * Every panel / view that cares about selection (the map canvas, attribute
 * table, object browser, charts) subscribes to one SelectionManager per
 * project and reports its own gestures back through the same instance.
 * That makes selection consistent across views without per-pair sync code,
 * and gives Phase 6 select-by-attribute / select-by-location a stable
 * destination.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SELECTIONMANAGER_H
#define SELECTIONMANAGER_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

/*!
 * \brief Identifies a single SWMM object.
 *
 * The pair (objectType, name) is unique within one project: SWMM enforces
 * unique IDs per object class, but two different object classes (a node
 * and a link) may share a name in some legacy `.inp` files.
 */
struct SWMMObjectRef {
    /*!
     * \brief Coarse object kind. Mirrors the engine's logical groupings;
     *        finer subtypes (e.g. junction vs outfall) live elsewhere on
     *        the layer / model side because they don't change the identity
     *        of the object for selection purposes.
     */
    enum ObjectType : int {
        Unknown = 0,
        Node = 1, ///< Junction / outfall / divider / storage
        Link = 2, ///< Conduit / pump / orifice / weir / outlet
        Subcatchment = 3,
        RainGage = 4,
        Pollutant = 5,
        LandUse = 6,
        Control = 7,
        // Non-spatial data objects (Slice BM.0). Values appended at end so
        // existing serialisation / sidecar `.oswp` files keep working.
        Curve = 8,
        TimeSeries = 9,
        TimePattern = 10,
        LIDControl = 11,
        Aquifer = 12,
        Snowpack = 13,
        Transect = 14,
        Hydrograph = 15,
        Street = 16,
        Inlet = 17,
        // 2D mesh elements (Slice §V.VA — Mesh Editing Toolbar). The
        // `name` field encodes mesh-layer persistent id + element index
        // (see `MeshObjectRef::vertex` / `::edge` on `SWMM2DMeshLayer`),
        // so multiple meshes on one canvas can co-exist in selection
        // without collision.
        MeshVertex = 18,
        MeshEdge = 19,
        MeshCell = 20, ///< 2D mesh triangle/cell (see `MeshObjectRef::cell`)
        // GIS vector feature layers (SVBC round B). The `name` field encodes
        // "gis::<layerId>#f<fid>" (see `GisObjectRef::feature`), so features
        // from several imported layers co-exist on the bus without
        // collision, keyed by the layer's stable id.
        Feature = 21,
    };

    ObjectType objectType = Unknown;
    QString name;

    /**
     * @brief Create an invalid selection identity with Unknown kind and empty name.
     */
    constexpr SWMMObjectRef() = default;
    /**
     * @brief Capture a selection kind and exact identifier.
     * @param[in] t Object kind; construction does not validate the enum value.
     * @param[in] n Name copied verbatim; GIS/mesh kinds use their encoded layer/element keys.
     */
    SWMMObjectRef(ObjectType t, QString n) : objectType(t), name(std::move(n)) {}

    /**
     * @brief Test the minimal identity requirements.
     * @return True when kind differs from Unknown and name is nonempty; does not validate existence or encoded GIS/mesh syntax.
     */
    bool isValid() const { return objectType != Unknown && !name.isEmpty(); }

    /**
     * @brief Compare complete selection identity using case-sensitive name equality.
     * @param[in] o Other selection reference.
     * @return True only for matching kind and exact name.
     */
    bool operator==(const SWMMObjectRef &o) const
    {
        return objectType == o.objectType && name == o.name;
    }
    /**
     * @brief Compare selection identity for inequality.
     * @param[in] o Other selection reference.
     * @return Inverse of operator==().
     */
    bool operator!=(const SWMMObjectRef &o) const { return !(*this == o); }
};

/**
 * @brief Hash the same kind/name fields used by equality.
 * @param[in] r Selection reference.
 * @param[in] seed Qt hash seed supplied by a containing hash/set.
 * @return Name hash combined with the numeric kind; collisions remain possible.
 */
inline size_t qHash(const SWMMObjectRef &r, size_t seed = 0)
{
    return qHash(r.name, seed) ^ static_cast<size_t>(r.objectType);
}

Q_DECLARE_METATYPE(SWMMObjectRef)

/*!
 * \class SelectionManager
 * \brief Owner of the canonical selection set for one SWMM project.
 *
 * The four selection modes follow the QGIS / ArcGIS Pro vocabulary:
 *
 *  - **Replace** — discard the current selection and use \p refs as the new one.
 *  - **Add**     — union with the current selection.
 *  - **Toggle**  — flip each ref's membership (selected ↔ unselected).
 *  - **Subtract** — remove each ref from the current selection.
 *
 * `selectionChanged(current, added, removed)` is emitted whenever the set
 * changes; the *added* and *removed* sets let subscribers do incremental
 * updates instead of diffing their own copy of `current`.
 */
class SelectionManager : public QObject
{
    Q_OBJECT

public:
    enum Mode {
        Replace = 0,
        Add = 1,
        Toggle = 2,
        Subtract = 3,
    };

    /**
     * @brief Create an empty selection bus and register its reference/set Qt metatypes.
     * @param[in] parent Optional QObject owner.
     * @details Use from its QObject owning thread; callbacks observe the new set after mutation.
     */
    explicit SelectionManager(QObject *parent = nullptr);
    /**
     * @brief Destroy local selection state and QObject connections.
     */
    ~SelectionManager() override;

    /*! \brief Apply \p refs to the current selection using \p mode.
     * @param[in] refs Input set copied/applied exactly, including invalid references; no per-reference filtering occurs in this overload.
     * @param[in] mode Replace, Add, Toggle, or Subtract set operation; an unsupported value leaves the selection unchanged.
     * @details Emits selectionChanged() only when the resulting set differs. Does not query engine/layer existence.
     */
    void select(const QSet<SWMMObjectRef> &refs, Mode mode = Replace);

    /*! \brief Convenience overload for a single ref.
     * @param[in] ref Single identity; invalid input becomes an empty input set, so Replace clears while other modes normally do nothing.
     * @param[in] mode Set operation forwarded to the set overload.
     */
    void select(const SWMMObjectRef &ref, Mode mode = Replace);

    /*! \brief Drop everything. Equivalent to `select({}, Replace)`. */
    void clear();

    /*! \brief Read-only view of the current selection.
     * @return Borrowed set reference whose contents change on subsequent edits and whose lifetime ends with the manager.
     */
    [[nodiscard]] const QSet<SWMMObjectRef> &selection() const { return m_selection; }

    /*! \brief True if \p ref is in the current selection.
     * @param[in] ref Exact kind/name identity to test.
     * @return Current set membership using case-sensitive name equality.
     */
    [[nodiscard]] bool contains(const SWMMObjectRef &ref) const
    {
        return m_selection.contains(ref);
    }

    /**
     * @brief Count selected identities.
     * @return Set size narrowed to int.
     */
    [[nodiscard]] int size() const { return m_selection.size(); }
    /**
     * @brief Test whether no identities are selected.
     * @return True for an empty set.
     */
    [[nodiscard]] bool isEmpty() const { return m_selection.isEmpty(); }

signals:
    /*! \brief Emitted on every set change. \p added / \p removed are
     *         disjoint and convey the delta from the previous selection.
     * @param[in] current Complete newly installed set.
     * @param[in] added Identities present now and absent previously.
     * @param[in] removed Identities removed from the previous set.
     * @details References are valid during signal delivery; copy when retaining a snapshot. Synchronous observers must account for possible reentrant selection updates.
     */
    void selectionChanged(const QSet<SWMMObjectRef> &current, const QSet<SWMMObjectRef> &added,
                          const QSet<SWMMObjectRef> &removed);

private:
    /**
     * @brief Install a changed set and publish its incremental delta.
     * @param[in] next Replacement set copied into canonical storage; identical input is a no-op.
     */
    void applyChange(const QSet<SWMMObjectRef> &next);

    QSet<SWMMObjectRef> m_selection;
};

#endif // SELECTIONMANAGER_H
