/**
 * @file      curveregistry.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.1 — project-scoped factory + lookup for
 *         CurveProvider instances.
 *
 * Mirrors `TimeseriesRegistry` (Phase 6.7.3.2) and `PatternRegistry`
 * (Phase 6.7.2) in structure. Engine I/O shares the `swmm_table_*` API
 * with timeseries — filters by type code (anything != TIMESERIES = curve).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_CURVE_CURVEREGISTRY_H
#define OPENSWMMVIS_CURVE_CURVEREGISTRY_H

#include "curve/curveprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::curve
{

class CurveRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an empty owner of curve providers.
     * @param[in] parent Optional QObject owner.
     * @details Access on the QObject/engine owning thread; cached engine handle is borrowed.
     */
    explicit CurveRegistry(QObject *parent = nullptr);
    /**
     * @brief Destroy owned child providers without destroying the engine.
     */
    ~CurveRegistry() override;

    /**
     * @brief Copy curves in insertion/load order.
     * @return Borrowed owned pointers, invalidated on removal or registry destruction.
     */
    QVector<CurveProvider *> providers() const { return m_providers; }
    /**
     * @brief Count registered curves.
     * @return Provider count narrowed to int.
     */
    int providerCount() const noexcept { return m_providers.size(); }

    /**
     * @brief Resolve a provider through lowercase-name matching.
     * @param[in] name Identifier without trimming.
     * @return Borrowed provider, or null when absent.
     */
    CurveProvider *findByName(const QString &name) const;
    /**
     * @brief Test case-insensitive name occupancy.
     * @param[in] name Candidate identifier.
     * @return True when findByName() resolves it.
     */
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    /*! \brief Create + own a new provider with the given name + type.
     *  When an engine is bound (loadFromEngine / saveToEngine) the curve is
     *  added to it at once, so it can be referenced before any flush.
     *  \returns the new provider, or nullptr if the name collides.
     * @param[in] name Nonempty identifier unused among local curves, independent of timeseries names.
     * @param[in] type Semantic curve type used when adding an engine table.
     * @return Borrowed owned provider, or null for empty/duplicate name; local creation remains successful if an engine add fails.
     * @details Wires point edits to full table replacement and type edits to engine type updates. Emits providerAdded() after local insertion.
     */
    CurveProvider *create(const QString &name, CurveType type);

    /**
     * @brief Unregister a curve and request engine table/reference cleanup.
     * @param[in] p Owned provider; null/nonmember is ignored.
     * @details Emits providerAboutToBeRemoved() before engine deletion, removes local entries, then schedules deleteLater(). Engine delete errors are ignored.
     */
    void remove(CurveProvider *p);
    /**
     * @brief Rename an owned curve after engine-side rename succeeds when applicable.
     * @param[in] p Non-null provider expected to belong to this registry; membership not checked.
     * @param[in] newName Nonempty new identifier; case-only spelling allowed.
     * @return False on invalid input, local collision, or engine rename rejection; true for successful/unchanged text.
     */
    bool rename(CurveProvider *p, const QString &newName);

    /*! \brief Populate from a live SWMM engine handle. Walks `swmm_table_*`,
     *  filters out TIMESERIES tables (type code 0). \returns count added.
     * @param[in] engineHandle Borrowed engine retained for subsequent mirroring; null returns zero without replacing the cached handle.
     * @return Number added; existing names are skipped rather than refreshed.
     * @details Loads only type 1..11 tables, skips unreadable points, and uses ordinary setAllPoints(), which can immediately mirror accepted loaded points. Nonmonotone replacement rejection leaves an empty provider.
     */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider back to the engine via
     *  `swmm_curve_add` (if new) + `swmm_table_clear` + `swmm_table_add_point`.
     * @param[in] engineHandle Borrowed engine retained for later mutations; null returns zero without changing the cached handle.
     * @return Number whose clear and every point add succeeded; existing type-set return codes are not checked.
     * @details Rewrites curves only, distinguishing same-named timeseries. A failed point add can leave a cleared/partial engine table; no rollback across providers.
     */
    int saveToEngine(void *engineHandle);

    /*! \brief Flush to the engine bound by the last loadFromEngine /
     *  saveToEngine(handle) call. No-op when none is bound. Lets the editor
     *  dialog flush on close without knowing the engine, the way
     *  TimeseriesRegistry::saveToEngine() does.
     * @return Number written using the cached engine, or zero when none was supplied.
     */
    int saveToEngine();

signals:
    /**
     * @brief Announce a registered signal-wired curve provider.
     * @param[in] provider Borrowed owned child.
     */
    void providerAdded(openswmmvis::curve::CurveProvider *provider);
    /**
     * @brief Announce impending curve removal before unregistering.
     * @param[in] provider Borrowed still-live/registered child.
     */
    void providerAboutToBeRemoved(openswmmvis::curve::CurveProvider *provider);
    /**
     * @brief Announce a name after lookup rekeying.
     * @param[in] provider Borrowed curve provider.
     * @param[in] prevName Removed name.
     * @param[in] newName Current name.
     */
    void providerRenamed(openswmmvis::curve::CurveProvider *provider, const QString &prevName,
                         const QString &newName);

private:
    /*! Replace the engine table's points with \p p's (no-op when detached).
     * @param[in] p Borrowed provider whose engine table is cleared then repopulated; null or missing engine/table is a no-op.
     * @details Stops at the first failed clear/add without a diagnostic or rollback; earlier points may already have changed.
     */
    void pushPoints(CurveProvider *p);

    QVector<CurveProvider *> m_providers;
    QHash<QString, CurveProvider *> m_byLowerName;
    /*! Engine bound by loadFromEngine/saveToEngine, so remove() can delete
     *  the engine table too (the transect/timeseries registries' idiom). */
    void *m_engineHandle = nullptr;
};

} // namespace openswmmvis::curve

#endif // OPENSWMMVIS_CURVE_CURVEREGISTRY_H
