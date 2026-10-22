// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      generatedmeshartifacts.h
 * @brief     Ownership and cleanup of generated mesh companion files.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include "projectsaveoutputs.h"
#include <QTemporaryDir>
#include <QSet>
#include <QHash>
#include <QSaveFile>
#include <functional>
#include <atomic>

// Shared by a generation job, its future result and the adopted mesh layer.
// Only the last owner removes the private directory. Final files belong to
// ProjectSaveOutputs, never to this temporary-directory owner.
class GeneratedMeshArtifacts
{
public:
    struct Entry {
        QString finalPath;
        QString stagedPath;
        ProjectSaveOutputs::DestinationState destination;
        QString payloadHash;
        ProjectSaveOutputs::Role role = ProjectSaveOutputs::Auxiliary;
    };

    /**
     * @brief Create a private generation directory adjacent to the project.
     * @param[in] projectPath Path whose parent directory hosts the temporary directory.
     * @param[out] error Optional creation-failure diagnostic.
     * @return Shared payload owner, or empty on directory creation failure. Its directory is removed when the last owner releases it.
     */
    static std::shared_ptr<GeneratedMeshArtifacts> create(const QString &projectPath,
                                                          QString *error)
    {
        auto artifacts = std::shared_ptr<GeneratedMeshArtifacts>(new GeneratedMeshArtifacts(
            QFileInfo(projectPath)
                .absoluteDir()
                .filePath(QStringLiteral(".openswmm-generation-XXXXXX"))));
        if (!artifacts->directory_.isValid()) {
            if (error)
                *error = QStringLiteral("Cannot prepare generated outputs: %1")
                             .arg(artifacts->directory_.errorString());
            return {};
        }
        return artifacts;
    }

    // Keep pending imported mesh/terrain snapshots alive when another operation
    // adds outputs. Preserve their original destination fingerprints and hashes.
    /**
     * @brief Retain and copy pending immutable entries from an earlier generated payload.
     * @param[in] previous Shared prior owner; null or already published is a successful no-op.
     * @param[out] error Optional diagnostic for unfinished/conflicting state.
     * @return True after copying entries/protections and retaining parent ownership; false if this owner is sealed/nonempty or the prior owner is unsealed.
     */
    bool inheritPending(const std::shared_ptr<GeneratedMeshArtifacts> &previous, QString *error)
    {
        if (!previous || previous->isPublished())
            return true;
        if (sealed_ || !entries_.isEmpty() || !previous->sealed_) {
            if (error)
                *error = QStringLiteral("Cannot combine unfinished generated outputs.");
            return false;
        }
        entries_ = previous->entries_;
        inputs_ = previous->inputs_;
        absentCompanions_ = previous->absentCompanions_;
        entryOwners_ = previous->entryOwners_;
        companionOwners_ = previous->companionOwners_;
        for (const auto &entry : entries_)
            if (!entryOwners_.contains(entry.stagedPath))
                entryOwners_.insert(entry.stagedPath, previous);
        for (const auto &path : absentCompanions_)
            if (!companionOwners_.contains(path))
                companionOwners_.insert(path, previous);
        parents_.append(previous);
        return true;
    }

    /**
     * @brief Access the private payload directory that must never be a final-output destination.
     * @return Temporary directory path, valid while the shared owner survives.
     */
    QString directoryPath() const { return directory_.path(); }
    /**
     * @brief Inspect reserved payloads and captured final-destination fingerprints.
     * @return Owner-held list reference; reserve/inherit/seal may mutate it before publication.
     */
    const QList<Entry> &entries() const { return entries_; }
    /**
     * @brief Inspect the shared publication flag.
     * @return True once markPublished() has accepted this payload; false initially.
     */
    bool isPublished() const { return published_; }
    /**
     * @brief Test whether an entry still needs publication in this owner's current state.
     * @param[in] entry Reserved or inherited entry whose stagedPath identifies its origin.
     * @return False if this owner or its surviving original entry owner is published; true otherwise.
     */
    bool isPendingEntry(const Entry &entry) const
    {
        if (published_)
            return false;
        const auto owner = entryOwners_.value(entry.stagedPath).lock();
        return !owner || !owner->isPublished();
    }
    /**
     * @brief Find this owner's recorded mesh destination, then the first nonempty inherited destination.
     * @return Owner- or ancestor-held path reference; empty when none was recorded. Keep the owner alive while using it.
     */
    const QString &publishedMeshPath() const
    {
        if (!publishedMeshPath_.isEmpty())
            return publishedMeshPath_;
        for (const auto &parent : parents_)
            if (!parent->publishedMeshPath().isEmpty())
                return parent->publishedMeshPath();
        return publishedMeshPath_;
    }
    // Undo states can share this immutable payload owner. Once its outputs
    // have been published successfully, none is pending in any of those
    // states. Keep the snapshots alive, but never replay obsolete output
    // fingerprints (or overwrite auxiliary files) on an Undo/Redo Save.
    /**
     * @brief Mark this shared payload and its ancestors as already adopted by a successful Save.
     * @param[in] meshPath Published mesh destination, optionally empty; recorded only on the first call.
     * @note Retains snapshots for undo states but prevents them being republished. Coordinate calls with readers; the atomic flag does not synchronize all other members.
     */
    void markPublished(const QString &meshPath = {})
    {
        if (published_)
            return;
        publishedMeshPath_ = meshPath;
        published_ = true;
        for (const auto &parent : parents_)
            parent->markPublished(meshPath);
    }
    /**
     * @brief Record a generation input to protect during a future project save.
     * @param[in] path Input filename; empty is ignored.
     * @param[in] allowedRole Optional publication role allowed to replace it; -1 forbids all roles.
     */
    void protectInput(const QString &path, int allowedRole = -1)
    {
        if (!path.isEmpty())
            inputs_.append({path, allowedRole});
    }

    /**
     * @brief Capture a final destination and reserve a unique filename in the private directory.
     * @param[in] finalPath Nonempty eventual destination, stored as an absolute path.
     * @param[in] fileName Simple filename, excluding dot/dot-dot and directory components.
     * @param[out] error Optional failure diagnostic.
     * @param[in] role Mesh or Auxiliary; other output roles are rejected.
     * @return Staging filename without creating its contents, or empty on sealed/duplicate/invalid destinations or inspection failure.
     */
    QString reserve(const QString &finalPath, const QString &fileName, QString *error,
                    ProjectSaveOutputs::Role role = ProjectSaveOutputs::Auxiliary)
    {
        if ((role != ProjectSaveOutputs::Mesh && role != ProjectSaveOutputs::Auxiliary) ||
            sealed_ || finalPath.isEmpty() || fileName.isEmpty() ||
            QFileInfo(fileName).fileName() != fileName || fileName == QStringLiteral(".") ||
            fileName == QStringLiteral("..")) {
            if (error)
                *error = QStringLiteral("Invalid generated output name: %1").arg(fileName);
            return {};
        }
        Entry entry{
            QFileInfo(finalPath).absoluteFilePath(), directory_.filePath(fileName), {}, {}, role};
        for (const auto &other : entries_)
            if (other.stagedPath == entry.stagedPath || other.finalPath == entry.finalPath) {
                if (error)
                    *error = QStringLiteral("Duplicate generated output: %1").arg(finalPath);
                return {};
            }
        if (!ProjectSaveOutputs::captureDestination(entry.finalPath, &entry.destination, error))
            return {};
        entries_.append(entry);
        return entry.stagedPath;
    }

    // Snapshot before parsing so displayed geometry and the eventual Save use
    // the same bytes. A cancellation callback returns true to stop copying.
    /**
     * @brief Snapshot a nonempty regular source into a reserved payload with cancellation and SHA-256 consistency checks.
     * @param[in] sourcePath Existing readable source filename.
     * @param[in] stagedPath Filename previously reserved by this owner; the owner must be unsealed.
     * @param[out] error Optional cancellation/validation/IO diagnostic.
     * @param[in] cancelled Optional callback polled before and during copying; true cancels.
     * @return True commits the snapshot after source identity/hash remain unchanged; false leaves no newly committed QSaveFile output.
     */
    bool copySource(const QString &sourcePath, const QString &stagedPath, QString *error,
                    const std::function<bool()> &cancelled = {})
    {
        const auto fail = [&](const QString &message) {
            if (error)
                *error = message;
            return false;
        };
        bool reserved = false;
        for (const auto &entry : entries_)
            reserved |= entry.stagedPath == stagedPath;
        if (sealed_ || !reserved)
            return fail(QStringLiteral("The import snapshot is not reserved for this job."));
        const auto isCancelled = [&] { return cancelled && cancelled(); };
        if (isCancelled())
            return fail(QStringLiteral("Mesh import cancelled."));
        ProjectSaveOutputs::DestinationState before, after;
        if (!QFileInfo(sourcePath).isFile() || QFileInfo(sourcePath).size() <= 0)
            return fail(QStringLiteral("The mesh source is missing or empty: %1").arg(sourcePath));
        if (!ProjectSaveOutputs::captureDestination(sourcePath, &before, error))
            return false;
        QFile source(sourcePath);
        QSaveFile target(stagedPath);
        if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly))
            return fail(QStringLiteral("Cannot snapshot mesh %1: %2 %3")
                            .arg(sourcePath, source.errorString(), target.errorString()));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QByteArray buffer(1024 * 1024, '\0');
        for (;;) {
            if (isCancelled())
                return fail(QStringLiteral("Mesh import cancelled."));
            const qint64 count = source.read(buffer.data(), buffer.size());
            if (count < 0)
                return fail(
                    QStringLiteral("Cannot read mesh source: %1").arg(source.errorString()));
            if (count == 0)
                break;
            hash.addData(QByteArrayView(buffer.constData(), count));
            if (target.write(buffer.constData(), count) != count)
                return fail(
                    QStringLiteral("Cannot write mesh snapshot: %1").arg(target.errorString()));
        }
        if (!ProjectSaveOutputs::captureDestination(sourcePath, &after, error))
            return false;
        if (before.resolvedPath != after.resolvedPath || before.fingerprint != after.fingerprint ||
            before.fingerprint != QString::fromLatin1(hash.result().toHex()))
            return fail(QStringLiteral("The mesh source changed during import. Import it again: %1")
                            .arg(sourcePath));
        if (isCancelled())
            return fail(QStringLiteral("Mesh import cancelled."));
        if (!target.commit())
            return fail(
                QStringLiteral("Cannot finish mesh snapshot: %1").arg(target.errorString()));
        return true;
    }

    // A new self-contained GeoTIFF must not inherit an old companion's
    // georeferencing, masks or overviews. Deleting/replacing those requires
    // its own output manifest, so refuse them in this bounded implementation.
    /**
     * @brief Register a companion filename that must remain absent until publication preparation.
     * @param[in] path Companion path; existing files and dangling symlinks are rejected.
     * @param[out] error Optional existing-companion diagnostic.
     * @return True records the absence constraint, false preserves the list.
     */
    bool requireAbsent(const QString &path, QString *error)
    {
        if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
            if (error)
                *error =
                    QStringLiteral(
                        "Generated raster destination has a companion file: %1. "
                        "Choose a destination without existing auxiliary metadata, masks or overviews.")
                        .arg(path);
            return false;
        }
        absentCompanions_.append(path);
        return true;
    }

    /**
     * @brief Certify nonempty payload files and disallow further reservations.
     * @param[out] error Optional missing/changed/unsupported-companion diagnostic.
     * @return True after hashing all entries and verifying private-directory contents, or immediately if already sealed. False may have recorded hashes for earlier entries but leaves sealed false.
     */
    bool seal(QString *error)
    {
        if (sealed_)
            return true; // Never recertify an already sealed payload.
        if (entries_.isEmpty()) {
            if (error)
                *error = QStringLiteral("No generated outputs to retain.");
            return false;
        }
        QSet<QString> owned;
        for (auto &entry : entries_) {
            const QFileInfo file(entry.stagedPath);
            ProjectSaveOutputs::DestinationState payload;
            if (!file.isFile() || file.isSymLink() || file.size() <= 0) {
                if (error)
                    *error = QStringLiteral("Missing or empty generated output: %1")
                                 .arg(entry.finalPath);
                return false;
            }
            if (!ProjectSaveOutputs::captureDestination(entry.stagedPath, &payload, error))
                return false;
            if (!entry.payloadHash.isEmpty() && entry.payloadHash != payload.fingerprint) {
                if (error)
                    *error = QStringLiteral("Pending output changed: %1").arg(entry.finalPath);
                return false;
            }
            entry.payloadHash = payload.fingerprint;
            owned.insert(file.absoluteFilePath());
        }
        for (const auto &file : QDir(directory_.path())
                                    .entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System |
                                                   QDir::NoDotAndDotDot)) {
            if (!owned.contains(file.absoluteFilePath())) {
                if (error)
                    *error = QStringLiteral("Pending output requires an unsupported companion: %1")
                                 .arg(file.fileName());
                return false;
            }
        }
        sealed_ = true;
        return true;
    }

    /**
     * @brief Register all retained private directories, snapshots, source inputs and absent companions with a save transaction.
     * @param[in,out] outputs Transaction receiving protections, including inherited owners' protections. No final files are written.
     */
    void protectInputs(ProjectSaveOutputs &outputs) const
    {
        outputs.protectDirectory(directory_.path());
        for (const auto &parent : parents_)
            parent->protectInputs(outputs);
        for (const auto &input : inputs_)
            outputs.protect(input.first, input.second);
        for (const auto &entry : entries_)
            outputs.protect(entry.stagedPath, -1);
        for (const auto &path : absentCompanions_)
            outputs.protect(path, -1);
    }

    // A first Save As can rebase an untitled import. Only destination identity
    // comes from the override; payload ownership and hash remain immutable.
    /**
     * @brief Copy immutable pending payloads into checked transaction staging files.
     * @param[in,out] outputs Transaction receiving protections and staged outputs; publication remains the caller's responsibility.
     * @param[out] error Optional unsealed/changed-payload/destination/IO diagnostic.
     * @param[in] meshDestinationOverride Optional alternate mesh destination identity for a first Save As; source payload/hash remain those of the original entry.
     * @return True prepares every pending entry, or only protections if already published. False may leave earlier stages in outputs, which retains cleanup ownership.
     */
    bool prepareSave(ProjectSaveOutputs &outputs, QString *error,
                     const Entry *meshDestinationOverride = nullptr) const
    {
        const auto fail = [&](const QString &message) {
            if (error)
                *error = message;
            return false;
        };
        if (published_) {
            protectInputs(outputs);
            return true;
        }
        if (!sealed_)
            return fail(QStringLiteral("Generated outputs have not finished preparation."));
        for (const auto &path : absentCompanions_) {
            const auto owner = companionOwners_.value(path).lock();
            if (owner && owner->isPublished())
                continue;
            if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
                return fail(
                    QStringLiteral("Raster companion appeared since generation: %1").arg(path));
        }
        protectInputs(outputs);
        for (const auto &entry : entries_) {
            if (!isPendingEntry(entry))
                continue;
            const Entry &destination =
                entry.role == ProjectSaveOutputs::Mesh && meshDestinationOverride
                    ? *meshDestinationOverride
                    : entry;
            const QString stage =
                outputs.stage(destination.finalPath, entry.role, false, &destination.destination);
            if (stage.isEmpty())
                return fail(outputs.error());
            QFile source(entry.stagedPath), target(stage);
            if (QFileInfo(entry.stagedPath).isSymLink() || !source.open(QIODevice::ReadOnly) ||
                !target.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return fail(QStringLiteral("Cannot prepare generated output %1: %2 %3")
                                .arg(entry.finalPath, source.errorString(), target.errorString()));
            QCryptographicHash hash(QCryptographicHash::Sha256);
            QByteArray buffer(1024 * 1024, '\0');
            for (;;) {
                const qint64 count = source.read(buffer.data(), buffer.size());
                if (count < 0)
                    return fail(QStringLiteral("Cannot read generated output %1: %2")
                                    .arg(entry.finalPath, source.errorString()));
                if (count == 0)
                    break;
                hash.addData(QByteArrayView(buffer.constData(), count));
                if (target.write(buffer.constData(), count) != count)
                    return fail(QStringLiteral("Cannot stage generated output %1: %2")
                                    .arg(entry.finalPath, target.errorString()));
            }
            if (QString::fromLatin1(hash.result().toHex()) != entry.payloadHash)
                return fail(QStringLiteral("Pending generated output changed: %1. Generate again.")
                                .arg(entry.finalPath));
            if (!target.flush())
                return fail(QStringLiteral("Cannot flush generated output %1: %2")
                                .arg(entry.finalPath, target.errorString()));
            target.close();
            if (target.error() != QFileDevice::NoError)
                return fail(QStringLiteral("Cannot close generated output %1: %2")
                                .arg(entry.finalPath, target.errorString()));
        }
        return true;
    }

private:
    /**
     * @brief Construct the private temporary directory owner.
     * @param[in] path QTemporaryDir filename template; create() checks the resulting validity.
     */
    explicit GeneratedMeshArtifacts(const QString &path) : directory_(path) {}
    QTemporaryDir directory_;
    QList<Entry> entries_;
    QList<std::shared_ptr<GeneratedMeshArtifacts>> parents_;
    QHash<QString, std::weak_ptr<GeneratedMeshArtifacts>> entryOwners_, companionOwners_;
    QList<QPair<QString, int>> inputs_;
    QStringList absentCompanions_;
    bool sealed_ = false;
    std::atomic_bool published_{false};
    QString publishedMeshPath_;
};
