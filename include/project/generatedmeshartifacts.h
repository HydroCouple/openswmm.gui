// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "projectsaveoutputs.h"
#include <QTemporaryDir>
#include <QSet>
#include <QSaveFile>
#include <functional>

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

    static std::shared_ptr<GeneratedMeshArtifacts> create(const QString &projectPath,
                                                         QString *error) {
        auto artifacts = std::shared_ptr<GeneratedMeshArtifacts>(
            new GeneratedMeshArtifacts(QFileInfo(projectPath).absoluteDir().filePath(
                QStringLiteral(".openswmm-generation-XXXXXX"))));
        if (!artifacts->directory_.isValid()) {
            if (error) *error = QStringLiteral("Cannot prepare generated outputs: %1")
                                   .arg(artifacts->directory_.errorString());
            return {};
        }
        return artifacts;
    }

    QString directoryPath() const { return directory_.path(); }
    const QList<Entry> &entries() const { return entries_; }
    void protectInput(const QString &path, int allowedRole = -1) {
        if (!path.isEmpty()) inputs_.append({path, allowedRole});
    }

    QString reserve(const QString &finalPath, const QString &fileName, QString *error,
                    ProjectSaveOutputs::Role role = ProjectSaveOutputs::Auxiliary) {
        if ((role != ProjectSaveOutputs::Mesh && role != ProjectSaveOutputs::Auxiliary)
            || sealed_ || finalPath.isEmpty() || fileName.isEmpty() || QFileInfo(fileName).fileName() != fileName
            || fileName == QStringLiteral(".") || fileName == QStringLiteral("..")) {
            if (error) *error = QStringLiteral("Invalid generated output name: %1").arg(fileName);
            return {};
        }
        Entry entry{QFileInfo(finalPath).absoluteFilePath(), directory_.filePath(fileName), {}, {}, role};
        for (const auto &other : entries_)
            if (other.stagedPath == entry.stagedPath || other.finalPath == entry.finalPath) {
                if (error) *error = QStringLiteral("Duplicate generated output: %1").arg(finalPath);
                return {};
            }
        if (!ProjectSaveOutputs::captureDestination(entry.finalPath, &entry.destination, error)) return {};
        entries_.append(entry);
        return entry.stagedPath;
    }

    // Snapshot before parsing so displayed geometry and the eventual Save use
    // the same bytes. A cancellation callback returns true to stop copying.
    bool copySource(const QString &sourcePath, const QString &stagedPath, QString *error,
                    const std::function<bool()> &cancelled = {}) {
        const auto fail = [&](const QString &message) {
            if (error) *error = message;
            return false;
        };
        bool reserved = false;
        for (const auto &entry : entries_) reserved |= entry.stagedPath == stagedPath;
        if (sealed_ || !reserved)
            return fail(QStringLiteral("The import snapshot is not reserved for this job."));
        const auto isCancelled = [&] { return cancelled && cancelled(); };
        if (isCancelled()) return fail(QStringLiteral("Mesh import cancelled."));
        ProjectSaveOutputs::DestinationState before, after;
        if (!QFileInfo(sourcePath).isFile() || QFileInfo(sourcePath).size() <= 0)
            return fail(QStringLiteral("The mesh source is missing or empty: %1").arg(sourcePath));
        if (!ProjectSaveOutputs::captureDestination(sourcePath, &before, error)) return false;
        QFile source(sourcePath);
        QSaveFile target(stagedPath);
        if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly))
            return fail(QStringLiteral("Cannot snapshot mesh %1: %2 %3")
                            .arg(sourcePath, source.errorString(), target.errorString()));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QByteArray buffer(1024 * 1024, '\0');
        for (;;) {
            if (isCancelled()) return fail(QStringLiteral("Mesh import cancelled."));
            const qint64 count = source.read(buffer.data(), buffer.size());
            if (count < 0) return fail(QStringLiteral("Cannot read mesh source: %1").arg(source.errorString()));
            if (count == 0) break;
            hash.addData(QByteArrayView(buffer.constData(), count));
            if (target.write(buffer.constData(), count) != count)
                return fail(QStringLiteral("Cannot write mesh snapshot: %1").arg(target.errorString()));
        }
        if (!ProjectSaveOutputs::captureDestination(sourcePath, &after, error)) return false;
        if (before.resolvedPath != after.resolvedPath || before.fingerprint != after.fingerprint
            || before.fingerprint != QString::fromLatin1(hash.result().toHex()))
            return fail(QStringLiteral("The mesh source changed during import. Import it again: %1").arg(sourcePath));
        if (isCancelled()) return fail(QStringLiteral("Mesh import cancelled."));
        if (!target.commit())
            return fail(QStringLiteral("Cannot finish mesh snapshot: %1").arg(target.errorString()));
        return true;
    }

    // A new self-contained GeoTIFF must not inherit an old companion's
    // georeferencing, masks or overviews. Deleting/replacing those requires
    // its own output manifest, so refuse them in this bounded implementation.
    bool requireAbsent(const QString &path, QString *error) {
        if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
            if (error) *error = QStringLiteral("Generated raster destination has a companion file: %1. "
                "Choose a destination without existing auxiliary metadata, masks or overviews.").arg(path);
            return false;
        }
        absentCompanions_.append(path);
        return true;
    }

    bool seal(QString *error) {
        if (sealed_) return true; // Never recertify an already sealed payload.
        if (entries_.isEmpty()) {
            if (error) *error = QStringLiteral("No generated outputs to retain.");
            return false;
        }
        QSet<QString> owned;
        for (auto &entry : entries_) {
            const QFileInfo file(entry.stagedPath);
            ProjectSaveOutputs::DestinationState payload;
            if (!file.isFile() || file.isSymLink() || file.size() <= 0) {
                if (error) *error = QStringLiteral("Missing or empty generated output: %1").arg(entry.finalPath);
                return false;
            }
            if (!ProjectSaveOutputs::captureDestination(entry.stagedPath, &payload, error)) return false;
            entry.payloadHash = payload.fingerprint;
            owned.insert(file.absoluteFilePath());
        }
        for (const auto &file : QDir(directory_.path()).entryInfoList(
                 QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
            if (!owned.contains(file.absoluteFilePath())) {
                if (error) *error = QStringLiteral("Pending output requires an unsupported companion: %1")
                                       .arg(file.fileName());
                return false;
            }
        }
        sealed_ = true;
        return true;
    }

    void protectInputs(ProjectSaveOutputs &outputs) const {
        outputs.protectDirectory(directory_.path());
        for (const auto &input : inputs_) outputs.protect(input.first, input.second);
        for (const auto &entry : entries_) outputs.protect(entry.stagedPath, -1);
        for (const auto &path : absentCompanions_) outputs.protect(path, -1);
    }

    // A first Save As can rebase an untitled import. Only destination identity
    // comes from the override; payload ownership and hash remain immutable.
    bool prepareSave(ProjectSaveOutputs &outputs, QString *error,
                     const Entry *meshDestinationOverride = nullptr) const {
        const auto fail = [&](const QString &message) {
            if (error) *error = message;
            return false;
        };
        if (!sealed_) return fail(QStringLiteral("Generated outputs have not finished preparation."));
        for (const auto &path : absentCompanions_)
            if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
                return fail(QStringLiteral("Raster companion appeared since generation: %1").arg(path));
        protectInputs(outputs);
        for (const auto &entry : entries_) {
            const Entry &destination = entry.role == ProjectSaveOutputs::Mesh && meshDestinationOverride
                ? *meshDestinationOverride : entry;
            const QString stage = outputs.stage(destination.finalPath, entry.role,
                                                 false, &destination.destination);
            if (stage.isEmpty()) return fail(outputs.error());
            QFile source(entry.stagedPath), target(stage);
            if (QFileInfo(entry.stagedPath).isSymLink() || !source.open(QIODevice::ReadOnly)
                || !target.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return fail(QStringLiteral("Cannot prepare generated output %1: %2 %3")
                                .arg(entry.finalPath, source.errorString(), target.errorString()));
            QCryptographicHash hash(QCryptographicHash::Sha256);
            QByteArray buffer(1024 * 1024, '\0');
            for (;;) {
                const qint64 count = source.read(buffer.data(), buffer.size());
                if (count < 0) return fail(QStringLiteral("Cannot read generated output %1: %2")
                                              .arg(entry.finalPath, source.errorString()));
                if (count == 0) break;
                hash.addData(QByteArrayView(buffer.constData(), count));
                if (target.write(buffer.constData(), count) != count)
                    return fail(QStringLiteral("Cannot stage generated output %1: %2")
                                    .arg(entry.finalPath, target.errorString()));
            }
            if (QString::fromLatin1(hash.result().toHex()) != entry.payloadHash)
                return fail(QStringLiteral("Pending generated output changed: %1. Generate again.").arg(entry.finalPath));
            if (!target.flush()) return fail(QStringLiteral("Cannot flush generated output %1: %2")
                                                .arg(entry.finalPath, target.errorString()));
            target.close();
            if (target.error() != QFileDevice::NoError)
                return fail(QStringLiteral("Cannot close generated output %1: %2")
                                .arg(entry.finalPath, target.errorString()));
        }
        return true;
    }

private:
    explicit GeneratedMeshArtifacts(const QString &path) : directory_(path) {}
    QTemporaryDir directory_;
    QList<Entry> entries_;
    QList<QPair<QString, int>> inputs_;
    QStringList absentCompanions_;
    bool sealed_ = false;
};
