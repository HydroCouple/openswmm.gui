/*!
 * \file   outputstatsregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice QA.1 — per-project registry that tags every loaded SWMMResultsLayer
 * with a stable identity. Downstream consumers (the node attribute panel,
 * Slice CB Statistics dashboard, Slice BF tabular results, Slice BH custom
 * reports) read from a single source of truth so the "which output produced
 * these numbers?" answer is uniform across the GUI.
 *
 * The registry is owned by SWMMVisProjectWindow (one per project window;
 * multi-project = multi-registry per Slice 13's session model). Layers
 * call registerLayer() from their openResults() success path and
 * unregisterLayer() from closeResults(); the registry emits
 * identitiesChanged() on every mutation so panels can refresh their
 * "Stats source:" combos without manual repaints.
 *
 * The null UUID is reserved as a sentinel meaning "use the editing
 * engine's ambient stats" — consumers that want to keep today's
 * behaviour when no source is explicitly chosen branch on
 * `identity.stableId.isNull()`.
 */

#ifndef OUTPUTSTATSREGISTRY_H
#define OUTPUTSTATSREGISTRY_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUuid>

class SWMMResultsLayer;

namespace openswmmvis {

/*! One row in the registry. Captures everything a consumer needs to
 *  render a row in the Stats-source combo and dispatch a stat-getter
 *  call back to the underlying layer. */
struct OutputIdentity
{
    /*! Stable identity for this loaded output, generated at registration
     *  time. Survives layer reorder so per-project QSettings can persist
     *  the user's last-selected stats source by UUID. The null UUID is
     *  the "editing-engine" sentinel and is never produced by
     *  registerLayer. */
    QUuid stableId;

    /*! Human-readable short label, derived from the .out filename without
     *  extension (e.g. "run-A"). Collision-resolved by appending "(N)"
     *  when two .out files share a basename across loaded layers. */
    QString shortLabel;

    /*! Full absolute path to the .out file. Suitable for use as the
     *  combo entry's tool-tip. */
    QString tooltipPath;

    /*! Pointer to the underlying layer. Null-checked by consumers
     *  before dispatch (the layer may be destroyed before the registry
     *  receives the unregister callback in pathological teardown
     *  orders; QPointer in the registry's internal storage handles
     *  that gracefully). */
    SWMMResultsLayer *layer = nullptr;
};

/*! Per-project registry of loaded output layers. */
class OutputStatsRegistry : public QObject
{
    Q_OBJECT

public:
    explicit OutputStatsRegistry(QObject *parent = nullptr);
    ~OutputStatsRegistry() override;

    /*! Add \p layer to the registry. No-op if \p layer is already
     *  registered (looked up by pointer identity, not stableId).
     *  Emits identitiesChanged() on a real change.
     *
     *  \p resultsFilePath is captured at registration time and used to
     *  derive the short label and tooltip. The registry never calls
     *  methods on \p layer directly — that keeps the registry's .cpp
     *  free of any swmmresultslayer.h dependency, which in turn makes
     *  unit tests linkable without pulling in the full GUI graph. */
    void registerLayer(SWMMResultsLayer *layer,
                       const QString    &resultsFilePath);

    /*! Remove \p layer from the registry. No-op if \p layer is not
     *  registered. Emits identitiesChanged() on a real change. */
    void unregisterLayer(SWMMResultsLayer *layer);

    /*! Snapshot of every currently-registered identity, in registration
     *  order. */
    [[nodiscard]] QList<OutputIdentity> identities() const;

    /*! Look up an identity by its stableId. Returns a default-constructed
     *  OutputIdentity (null UUID, empty label) when \p id is the null
     *  UUID or no match is found. */
    [[nodiscard]] OutputIdentity identityFor(const QUuid &id) const;

signals:
    /*! Emitted after registerLayer / unregisterLayer mutates the set. */
    void identitiesChanged();

private:
    /*! Recompute every entry's shortLabel after a set mutation. Resolves
     *  filename collisions by appending "(N)" to duplicates in
     *  registration order. */
    void recomputeLabels();

    struct Slot
    {
        QUuid                       stableId;
        QString                     shortLabel;
        QString                     tooltipPath;
        QPointer<SWMMResultsLayer>  layer;
    };

    QList<Slot> m_slots;
};

} // namespace openswmmvis

#endif // OUTPUTSTATSREGISTRY_H
