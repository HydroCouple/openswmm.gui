/*!
 * \file   patternregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.2 — project-scoped factory + lookup for
 *         PatternProvider instances.
 *
 * One instance per open project. Responsibilities:
 *   - hold all PatternProvider instances (Qt-parented).
 *   - enforce case-insensitive name uniqueness (legacy SWMM parity).
 *   - emit signals when providers are added / removed / renamed so the
 *     Object Browser, editor dialog, and DWF inflow pickers refresh.
 *
 * Engine I/O wraps `swmm_pattern_*` (count / id / type / factor_count /
 * factor / add / set_factors).
 *
 * Mirrors `TimeseriesRegistry` (Slice BQ Phase 6.7.3.2) in structure so the
 * Object Browser's lazy-init / engine-handle-invalidation path is identical.
 */
#ifndef OPENSWMMVIS_PATTERN_PATTERNREGISTRY_H
#define OPENSWMMVIS_PATTERN_PATTERNREGISTRY_H

#include "pattern/patternprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::pattern {

class PatternRegistry : public QObject
{
    Q_OBJECT

public:
    explicit PatternRegistry(QObject *parent = nullptr);
    ~PatternRegistry() override;

    // ── Lookup ──────────────────────────────────────────────────────────────

    /*! \brief All providers in insertion order. */
    QVector<PatternProvider *> providers() const { return m_providers; }

    int providerCount() const noexcept { return m_providers.size(); }

    /*! \brief Find by exact name (case-insensitive). Returns nullptr if absent. */
    PatternProvider *findByName(const QString &name) const;

    /*! \brief True iff a provider with this name exists (case-insensitive). */
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    // ── Mutation ────────────────────────────────────────────────────────────

    /*! \brief Create + own a new provider with the given name + type.
     *  \returns the new provider, or nullptr if the name is already in use
     *           or the name is empty.
     *
     *  If an engine handle is attached (see attachEngine), the new pattern is
     *  also added to the engine via `swmm_pattern_add` so the two stay in
     *  sync from the moment of creation. */
    PatternProvider *create(const QString &name, PatternType type);

    /*! \brief Clone an existing provider under a new name. Copies the source
     *  provider's type and all factor values. Returns nullptr if either name
     *  is invalid or \p newName already exists. */
    PatternProvider *duplicate(const QString &srcName, const QString &newName);

    /*! \brief Remove + delete a provider. Safe to call with a stale pointer
     *  (no-op if not owned by this registry). When an engine handle is
     *  attached the matching pattern is also dropped from the engine via
     *  `swmm_pattern_remove`, cascading to any reference sites. */
    void remove(PatternProvider *p);

    /*! \brief Rename a provider. Performs the uniqueness check; returns
     *  false (and emits no signal) if \p newName collides with a different
     *  existing provider. Same provider with same name is a no-op success.
     *  When an engine handle is attached the rename is mirrored via
     *  `swmm_pattern_rename` so every engine-side reference updates atomically. */
    bool rename(PatternProvider *p, const QString &newName);

    // ── Engine I/O ──────────────────────────────────────────────────────────

    /*! \brief Populate the registry from a live SWMM engine handle. Walks
     *  `swmm_pattern_*`, creates one PatternProvider per entry — loading
     *  all factors. Pre-existing providers are NOT cleared; conflicts
     *  (duplicate name) are skipped silently.
     *
     *  \param engineHandle Opaque pointer to SWMM_Engine. Passed as void*
     *                      so this header stays free of the engine C header.
     *  \returns the number of providers added. */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider's factors back to the engine via
     *  `swmm_pattern_set_factors`. New providers (missing from the engine)
     *  are added via `swmm_pattern_add` first (requires the engine to be
     *  in BUILDING state).
     *  \returns the number of patterns written. */
    int saveToEngine(void *engineHandle);

    /*! \brief Attach an engine handle so subsequent mutations (create /
     *  remove / rename / duplicate) are mirrored to the engine via the
     *  matching `swmm_pattern_*` calls. Passing nullptr detaches. Used by
     *  `SWMMModelLayer::ensurePatternRegistry`. */
    void attachEngine(void *engineHandle) { m_engineHandle = engineHandle; }

    /*! \brief The currently-attached engine handle (or nullptr). */
    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    /*! \brief A provider was created and added to the registry. */
    void providerAdded(openswmmvis::pattern::PatternProvider *provider);

    /*! \brief A provider is about to be removed. Subscribers must drop refs. */
    void providerAboutToBeRemoved(openswmmvis::pattern::PatternProvider *provider);

    /*! \brief A provider's name changed. */
    void providerRenamed(openswmmvis::pattern::PatternProvider *provider,
                         const QString &prevName, const QString &newName);

private:
    QVector<PatternProvider *>            m_providers;       ///< Insertion order.
    QHash<QString, PatternProvider *>     m_byLowerName;     ///< Case-insensitive index.
    void                                 *m_engineHandle = nullptr; ///< Live engine for mirroring mutations.
};

} // namespace openswmmvis::pattern

#endif // OPENSWMMVIS_PATTERN_PATTERNREGISTRY_H
