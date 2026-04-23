/*!
 * \file   selectionmanager.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Phase 1.4 — per-project Cross-View Selection Bus.
 *
 * Every panel / view that cares about selection (the map canvas, attribute
 * table, object browser, charts) subscribes to one SelectionManager per
 * project and reports its own gestures back through the same instance.
 * That makes selection consistent across views without per-pair sync code,
 * and gives Phase 6 select-by-attribute / select-by-location a stable
 * destination.
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
struct SWMMObjectRef
{
    /*!
     * \brief Coarse object kind. Mirrors the engine's logical groupings;
     *        finer subtypes (e.g. junction vs outfall) live elsewhere on
     *        the layer / model side because they don't change the identity
     *        of the object for selection purposes.
     */
    enum ObjectType : int {
        Unknown      = 0,
        Node         = 1,   ///< Junction / outfall / divider / storage
        Link         = 2,   ///< Conduit / pump / orifice / weir / outlet
        Subcatchment = 3,
        RainGage     = 4,
        Pollutant    = 5,
        LandUse      = 6,
        Control      = 7,
    };

    ObjectType objectType = Unknown;
    QString    name;

    constexpr SWMMObjectRef() = default;
    SWMMObjectRef(ObjectType t, QString n)
        : objectType(t), name(std::move(n)) {}

    bool isValid() const { return objectType != Unknown && !name.isEmpty(); }

    bool operator==(const SWMMObjectRef &o) const
    { return objectType == o.objectType && name == o.name; }
    bool operator!=(const SWMMObjectRef &o) const { return !(*this == o); }
};

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
        Replace  = 0,
        Add      = 1,
        Toggle   = 2,
        Subtract = 3,
    };

    explicit SelectionManager(QObject *parent = nullptr);
    ~SelectionManager() override;

    /*! \brief Apply \p refs to the current selection using \p mode. */
    void select(const QSet<SWMMObjectRef> &refs, Mode mode = Replace);

    /*! \brief Convenience overload for a single ref. */
    void select(const SWMMObjectRef &ref, Mode mode = Replace);

    /*! \brief Drop everything. Equivalent to `select({}, Replace)`. */
    void clear();

    /*! \brief Read-only view of the current selection. */
    [[nodiscard]] const QSet<SWMMObjectRef> &selection() const { return m_selection; }

    /*! \brief True if \p ref is in the current selection. */
    [[nodiscard]] bool contains(const SWMMObjectRef &ref) const
    { return m_selection.contains(ref); }

    [[nodiscard]] int  size()    const { return m_selection.size(); }
    [[nodiscard]] bool isEmpty() const { return m_selection.isEmpty(); }

signals:
    /*! \brief Emitted on every set change. \p added / \p removed are
     *         disjoint and convey the delta from the previous selection. */
    void selectionChanged(const QSet<SWMMObjectRef> &current,
                          const QSet<SWMMObjectRef> &added,
                          const QSet<SWMMObjectRef> &removed);

private:
    void applyChange(const QSet<SWMMObjectRef> &next);

    QSet<SWMMObjectRef> m_selection;
};

#endif // SELECTIONMANAGER_H
