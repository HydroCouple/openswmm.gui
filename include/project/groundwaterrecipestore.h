/**
 * @file      groundwaterrecipestore.h
 * @brief     Persistence of groundwater assignment recipes and provenance.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_GROUNDWATERRECIPESTORE_H
#define OPENSWMMVIS_GROUNDWATERRECIPESTORE_H
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

// Snapshot provenance, never an instruction to replay forcing automatically.
// Engine rows remain authoritative; events record accepted apply/undo/redo.
class GroundwaterRecipeStore : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Create an empty provenance history named groundwaterAssignmentHistory.
     * @param[in] parent Optional QObject owner.
     */
    explicit GroundwaterRecipeStore(QObject *parent = nullptr);
    /**
     * @brief Find the owner's direct history child, optionally creating it.
     * @param[in] owner QObject whose direct children are searched; null returns null.
     * @param[in] create Allocate a parent-owned store when none exists.
     * @return Borrowed existing/new child, or null when unavailable.
     */
    static GroundwaterRecipeStore *forOwner(QObject *owner, bool create = true);
    /**
     * @brief Access accepted apply/undo/redo provenance snapshots in insertion order.
     * @return Store-owned array reference; later append or restore changes it. No engine forcing is replayed.
     */
    const QJsonArray &events() const { return events_; }
    /**
     * @brief Inspect the last restoration failure that blocks appending and serializing.
     * @return Empty after construction or successful restore, otherwise the retained diagnostic.
     */
    QString loadError() const { return loadError_; }
    /**
     * @brief Validate and append a version-1 snapshot-only assignment record.
     * @param[in] recipe Record containing target, cells and sourcePath; sourceSha256, when present, must be an object.
     * @param[out] error Optional failure diagnostic; not cleared on success.
     * @return False on invalid record or a retained load error without changing events; true appends and emits edited().
     * @note Missing id/event fields receive a UUID/applied default. recordedAt is replaced with the current UTC timestamp.
     */
    bool append(const QJsonObject &recipe, QString *error = nullptr);
    /**
     * @brief Validate a complete history and resolve source paths against its project directory.
     * @param[in] payload JSON array of version-1 snapshot-only records.
     * @param[in] baseDirectory Directory anchor; empty leaves path tokens unchanged.
     * @param[out] error Optional failure diagnostic; unchanged on success.
     * @return True replaces events and clears loadError(); false retains previous events and records a load error. No edited signal is emitted.
     */
    bool restore(const QJsonValue &payload, const QString &baseDirectory, QString *error = nullptr);
    /**
     * @brief Encode history using paths relative to the destination project directory.
     * @param[in] baseDirectory Directory anchor; empty leaves path tokens unchanged.
     * @param[out] out Replaced only after all records validate.
     * @param[out] error Optional validation/load-error diagnostic.
     * @return False if restoration previously failed or a record is invalid; true does not mutate the store.
     */
    bool serialize(const QString &baseDirectory, QJsonArray &out, QString *error = nullptr) const;
signals:
    /**
     * @brief Announce a successfully appended provenance record; restore does not emit this signal.
     */
    void edited();

private:
    QJsonArray events_;
    QString loadError_;
};
#endif
