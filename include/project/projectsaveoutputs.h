// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QUuid>
#include <functional>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QString>
#include <filesystem>
#include <memory>
#include <vector>

// Synchronous built-in Save's explicit output set. Serialization writes only
// owned adjacent staging files. A checked journal retains old/new bytes until
// publication commits. Interrupted, uncommitted saves restore the old file set.
class ProjectSaveOutputs
{
public:
    enum Role { Model = 0, Mesh = 1, Component = 2, Settings = 3, Auxiliary = 4 };
    struct DestinationState { QString resolvedPath; QString fingerprint; };
    // Capture before a long-running generation job. Save can then distinguish
    // its original destination from a file created or edited in the meantime.
    // Missing destinations are valid, and capturing never creates directories.
    static bool captureDestination(const QString &path, DestinationState *state,
                                   QString *error = nullptr) {
        if (error) error->clear();
        if (state) *state = {};
        if (path.isEmpty() || !state) {
            if (error) *error = QStringLiteral("A save destination and state are required");
            return false;
        }
        ProjectSaveOutputs checker;
        DestinationState captured;
        captured.resolvedPath = resolved(path);
        if (captured.resolvedPath.isEmpty() || !checker.fingerprint(path, captured.fingerprint)) {
            if (error) *error = checker.error_.isEmpty()
                ? QStringLiteral("Cannot resolve save destination %1").arg(path) : checker.error_;
            return false;
        }
        if (resolved(path) != captured.resolvedPath) {
            if (error) *error = QStringLiteral("Save destination changed while being inspected: %1").arg(path);
            return false;
        }
        *state = std::move(captured);
        return true;
    }
    struct PreparedOutput { QString finalPath; QString stagedPath; Role role; };
    QList<PreparedOutput> preparedOutputs() const {
        QList<PreparedOutput> outputs;
        for (const auto &entry : entries_)
            outputs.append({entry.finalPath, entry.file->fileName(), entry.role});
        return outputs;
    }
    void protect(const QString &path, int allowedRole) {
        if (!path.isEmpty()) protected_.push_back({path, allowedRole});
    }
    // A temporary owner may recursively remove its directory when released.
    // No published output can safely live anywhere inside that namespace.
    void protectDirectory(const QString &path) {
        if (!path.isEmpty()) protectedDirectories_.push_back({
            QDir::cleanPath(QFileInfo(path).absoluteFilePath()), resolved(path)});
    }
    const QString &error() const { return error_; }
    bool publicationStarted() const { return publicationStarted_; }
    bool recoveryRequired() const { return publicationStarted_ && !rolledBack_; }

    // Opening a project is the recovery boundary. Save refuses a pending journal
    // rather than applying an edited in-memory model over an unresolved disk set.
    static bool recover(const QString &model, QString *error, QString *notice = nullptr) {
        const QString physical = resolved(model);
        const QFileInfo journal(physical + QStringLiteral(".openswmm-save.json"));
        // Ordinary Open must continue to work on read-only project directories.
        if (!journal.exists() && !journal.isSymLink() &&
            !QFileInfo::exists(physical + QStringLiteral(".openswmm-save.lock"))) return true;
        ProjectSaveOutputs transaction;
        if (!transaction.acquire(model, true)) {
            if (error) *error = transaction.error();
            return false;
        }
        if (!QFileInfo::exists(transaction.journalPath_) && !QFileInfo(transaction.journalPath_).isSymLink()) return true;
        if (!transaction.readJournal() || !transaction.restoreOrFinish()) {
            if (error) *error = transaction.error() + QStringLiteral(" Recovery files retained at %1.")
                .arg(transaction.journalPath_);
            return false;
        }
        if (notice) *notice = transaction.committed_
            ? QStringLiteral("Completed cleanup of a previously saved project.")
            : QStringLiteral("Recovered an interrupted save; restored the previous saved project.");
        return true;
    }

    QString stage(const QString &path, Role role, bool reuse = false,
                  const DestinationState *expected = nullptr) {
        if (path.isEmpty()) { error_ = QStringLiteral("Empty save destination"); return {}; }
        const QString finalPath = QFileInfo(path).absoluteFilePath();
        if (!checkProtectedDirectories(finalPath)) return {};
        if (role == Model && !lock_ && !acquire(finalPath, false)) return {};
        for (const auto &p : protected_) {
            if (p.allowedRole != role && sameFile(p.path, finalPath)) {
                error_ = QStringLiteral("Output %1 would replace protected project input %2")
                             .arg(finalPath, p.path);
                return {};
            }
        }
        for (const auto &entry : entries_) {
            if (!sameFile(entry.finalPath, finalPath)) continue;
            if (reuse && entry.role == role && entry.finalPath == finalPath) {
                if (expected) {
                    DestinationState current;
                    if (!captureDestination(finalPath, &current, &error_)) return {};
                    if (current.resolvedPath != expected->resolvedPath || current.fingerprint != expected->fingerprint) {
                        error_ = QStringLiteral("Destination changed since generation: %1").arg(finalPath);
                        return {};
                    }
                }
                return entry.file->fileName();
            }
            error_ = QStringLiteral("Conflicting save outputs share %1 and %2")
                         .arg(entry.finalPath, finalPath);
            return {};
        }
        if (!journalPath_.isEmpty() &&
            (sameFile(finalPath, journalPath_) ||
             sameFile(finalPath, modelPath_ + QStringLiteral(".openswmm-save.lock")) ||
             resolved(finalPath).startsWith(journalPath_ + QStringLiteral(".data-")))) {
            error_ = QStringLiteral("Output conflicts with Save recovery metadata: %1").arg(finalPath);
            return {};
        }
        if (!checkDestination(finalPath)) return {};
        // Do not create final directories during preparation. For new nested
        // component destinations, use the nearest existing ancestor instead.
        QDir parent = QFileInfo(finalPath).absoluteDir();
        // GUI reference patching/settings serialization use the staging file's
        // directory as their anchor, so these two must remain true siblings.
        if ((role == Model || role == Settings) && !parent.exists()) {
            error_ = QStringLiteral("Model/settings destination directory does not exist: %1").arg(finalPath);
            return {};
        }
        while (!parent.exists()) {
            const QString ancestor = QFileInfo(parent.absolutePath()).absolutePath();
            if (ancestor == parent.absolutePath()) { error_ = QStringLiteral("No staging directory for %1").arg(finalPath); return {}; }
            parent = QDir(ancestor);
        }
        std::unique_ptr<StagedFile> file;
        {
            // QTemporaryFile::close() keeps its handle open, and on Windows that
            // handle blocks writers from renaming their output over the staged
            // path. Let it go out of scope to release it; StagedFile owns removal.
            QTemporaryFile temporary(parent.filePath(QStringLiteral(".openswmm-stage-XXXXXX")));
            if (!temporary.open()) {
                error_ = QStringLiteral("Cannot stage %1: %2").arg(finalPath, temporary.errorString());
                return {};
            }
            temporary.setAutoRemove(false);
            file = std::make_unique<StagedFile>(temporary.fileName());
        }
        const QString stagedPath = file->fileName();
        DestinationState current;
        if (!captureDestination(finalPath, &current, &error_)) return {};
        if (expected && (current.resolvedPath != expected->resolvedPath || current.fingerprint != expected->fingerprint)) {
            error_ = QStringLiteral("Destination changed since generation: %1").arg(finalPath);
            return {};
        }
        entries_.push_back({finalPath, role, std::move(file), current.resolvedPath, current.fingerprint});
        return stagedPath;
    }

    bool publish() {
        // Validate the complete prepared set before the first final write.
        // Aliases are checked again after serialization and before journaling.
        for (const auto &entry : entries_) {
            if (!checkProtectedDirectories(entry.finalPath)) return false;
            if (!checkDestination(entry.finalPath)) return false;
            if (!QFileInfo(entry.file->fileName()).isFile()) {
                error_ = QStringLiteral("Missing staged output for %1").arg(entry.finalPath);
                return false;
            }
        }
        for (size_t i = 0; i < entries_.size(); ++i) {
            for (size_t j = 0; j < i; ++j) {
                if (sameFile(entries_[i].finalPath, entries_[j].finalPath)) {
                    error_ = QStringLiteral("Save destinations now alias: %1").arg(entries_[i].finalPath);
                    return false;
                }
            }
            for (const auto &p : protected_) {
                if (p.allowedRole != entries_[i].role && sameFile(p.path, entries_[i].finalPath)) {
                    error_ = QStringLiteral("Save destination now aliases protected input: %1").arg(p.path);
                    return false;
                }
            }
        }
        if (!lock_) { error_ = QStringLiteral("A model destination is required for transactional Save"); return false; }
        // Commit the manifest before changing any destination. Data paths are
        // derived from an opaque ID; a journal never supplies arbitrary backup paths.
        dataPath_ = journalPath_ + QStringLiteral(".data-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!QDir().mkdir(dataPath_)) { error_ = QStringLiteral("Cannot create recovery directory %1").arg(dataPath_); return false; }
        if (!QFile::setPermissions(dataPath_, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
            error_ = QStringLiteral("Cannot secure recovery directory %1").arg(dataPath_);
            QDir().rmdir(dataPath_); return false;
        }
        for (Role role : {Component, Mesh, Auxiliary, Settings, Model}) {
            for (const auto &entry : entries_) {
                if (entry.role != role) continue;
                // Three distinct failures, reported distinctly: one message for
                // all of them hid an unreadable destination (fingerprint's own
                // error was overwritten) behind "changed", sending users to look
                // for an edit that never happened.
                QString current;
                const QString nowResolved = resolved(entry.finalPath);
                if (nowResolved != entry.physicalPath) {
                    error_ = nowResolved.isEmpty()
                        ? QStringLiteral("Destination path could not be resolved when Save was "
                                         "published (it resolved to %1 when Save began): %2")
                              .arg(entry.physicalPath, entry.finalPath)
                        : QStringLiteral("Destination changed while Save was being prepared — it "
                                         "now resolves to %1 instead of %2: %3")
                              .arg(nowResolved, entry.physicalPath, entry.finalPath);
                    cleanupData();
                    return false;
                }
                if (!fingerprint(entry.finalPath, current)) {
                    error_ = QStringLiteral("Destination could not be re-read before publishing "
                                            "(%1): %2").arg(error_, entry.finalPath);
                    cleanupData();
                    return false;
                }
                if (current != entry.oldHash) {
                    error_ = QStringLiteral("Destination changed while Save was being prepared "
                                            "(its contents differ from when Save began): %1")
                                 .arg(entry.finalPath);
                    cleanupData();
                    return false;
                }
                Record record{entry.physicalPath, entry.oldHash, {},
                              int(QFile::permissions(entry.finalPath))};
                records_.push_back(record);
                const int i = int(records_.size()) - 1;
                if (!copyFile(entry.file->fileName(), dataFile(i, false)) ||
                    !fingerprint(dataFile(i, false), records_.back().newHash) ||
                    (current != absent() && !copyFile(entry.finalPath, dataFile(i, true), current))) {
                    cleanupData(); return false;
                }
                QString backup;
                if (current != absent() && (!fingerprint(dataFile(i, true), backup) || backup != current)) {
                    error_ = QStringLiteral("Destination changed while its rollback copy was prepared: %1").arg(entry.finalPath);
                    cleanupData(); return false;
                }
            }
        }
        if (!writeJournal(false)) { cleanupData(); return false; }
        for (const auto &entry : entries_) QFile::remove(entry.file->fileName());
        auto fail = [&]() {
            const QString cause = error_;
            if (restoreOrFinish()) {
                rolledBack_ = true;
                error_ = cause + QStringLiteral(" Previous saved files were restored.");
            } else {
                error_ = cause + QStringLiteral(" Recovery could not finish: %1. Recovery files retained at %2.")
                    .arg(error_, journalPath_);
            }
            return false;
        };
        if (!checkpoint(0)) return fail();
        for (int i = 0; i < int(records_.size()); ++i) {
            const auto &record = records_[i];
            QString current;
            if (resolved(record.path) != record.path || !fingerprint(record.path, current) || current != record.oldHash) {
                error_ = QStringLiteral("Destination changed before publication: %1").arg(record.path);
                return fail();
            }
            publicationStarted_ = true;
            if (!QDir().mkpath(QFileInfo(record.path).absolutePath()) ||
                !copyFile(dataFile(i, false), record.path, record.newHash)) {
                if (error_.isEmpty()) error_ = QStringLiteral("Cannot create output directory for %1").arg(record.path);
                return fail();
            }
            if (!checkpoint(i + 1)) return fail();
        }
        for (const auto &record : records_) {
            QString current;
            if (resolved(record.path) != record.path || !fingerprint(record.path, current) || current != record.newHash) {
                error_ = QStringLiteral("Destination changed during publication: %1").arg(record.path);
                return fail();
            }
        }
        if (!writeJournal(true)) return fail();
        committed_ = true;
        // Once this marker exists, restart must retain the complete new set.
        // Cleanup failure is harmless and retried by recovery on the next open.
        checkpoint(int(records_.size()) + 1);
        cleanupJournal();
        return true;
    }

private:
    static std::filesystem::path nativePath(const QString &path) {
#ifdef Q_OS_WIN
        return std::filesystem::path(path.toStdWString());
#else
        return std::filesystem::path(path.toUtf8().constData());
#endif
    }
    static bool sameFile(const QString &a, const QString &b) {
        std::error_code ec;
        if (std::filesystem::equivalent(nativePath(a), nativePath(b), ec)) return true;
        const auto ca = std::filesystem::weakly_canonical(nativePath(a), ec);
        if (!ec) {
            const auto cb = std::filesystem::weakly_canonical(nativePath(b), ec);
            if (!ec && ca == cb) return true;
        }
        return QDir::cleanPath(QFileInfo(a).absoluteFilePath())
            == QDir::cleanPath(QFileInfo(b).absoluteFilePath());
    }
    bool checkDestination(const QString &path) {
        const QFileInfo info(path);
        if ((info.isSymLink() && !info.exists()) || (info.exists() && !info.isFile())) {
            error_ = QStringLiteral("Cannot replace non-regular or dangling destination %1").arg(path);
            return false;
        }
        if (info.exists() && !(info.permissions() & (QFileDevice::WriteOwner | QFileDevice::WriteGroup | QFileDevice::WriteOther))) {
            error_ = QStringLiteral("Cannot replace read-only destination %1").arg(path);
            return false;
        }
        return true;
    }
    bool checkProtectedDirectories(const QString &path) {
        const auto within = [](const QString &candidate, const QString &directory) {
            if (candidate.isEmpty() || directory.isEmpty()) return false;
            const QString prefix = directory.endsWith('/') ? directory : directory + '/';
#ifdef Q_OS_WIN
            constexpr auto sensitivity = Qt::CaseInsensitive;
#else
            constexpr auto sensitivity = Qt::CaseSensitive;
#endif
            return candidate.compare(directory, sensitivity) == 0 || candidate.startsWith(prefix, sensitivity);
        };
        const QString logical = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        const QString physical = resolved(path);
        for (const auto &directory : protectedDirectories_) {
            // Keep the original identity protected, and also check its current
            // resolution in case an ancestor symlink changed after registration.
            if (within(logical, directory.path) || within(physical, directory.resolvedPath)
                || within(physical, resolved(directory.path))) {
                error_ = QStringLiteral("Output %1 would be published inside protected directory %2")
                             .arg(path, directory.path);
                return false;
            }
        }
        return true;
    }
    struct Protected { QString path; int allowedRole; };
    struct ProtectedDirectory { QString path, resolvedPath; };
    // A uniquely named staging file with no open handle, removed on destruction.
    struct StagedFile {
        explicit StagedFile(QString path) : path_(std::move(path)) {}
        ~StagedFile() { QFile::remove(path_); }
        StagedFile(const StagedFile &) = delete;
        StagedFile &operator=(const StagedFile &) = delete;
        const QString &fileName() const { return path_; }
    private:
        QString path_;
    };
    struct Entry {
        QString finalPath; Role role; std::unique_ptr<StagedFile> file;
        QString physicalPath, oldHash;
    };
    struct Record { QString path, oldHash, newHash; int permissions = 0; };
    static QString absent() { return QStringLiteral("absent"); }
    static QString resolved(const QString &path) {
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(nativePath(QFileInfo(path).absoluteFilePath()), ec);
        if (ec) return {};
#ifdef Q_OS_WIN
        return QString::fromStdWString(canonical.wstring());
#else
        return QString::fromUtf8(canonical.string().c_str());
#endif
    }
    bool fingerprint(const QString &path, QString &hash) {
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) { hash = absent(); return true; }
        QFile file(path);
        QCryptographicHash digest(QCryptographicHash::Sha256);
        if (!info.isFile() || !file.open(QIODevice::ReadOnly) || !digest.addData(&file)) {
            error_ = QStringLiteral("Cannot verify save file %1: %2").arg(path, file.errorString());
            return false;
        }
        hash = QString::fromLatin1(digest.result().toHex());
        return true;
    }
    bool acquire(const QString &model, bool recovering) {
        modelPath_ = resolved(model);
        if (modelPath_.isEmpty()) { error_ = QStringLiteral("Cannot resolve model destination %1").arg(model); return false; }
        journalPath_ = modelPath_ + QStringLiteral(".openswmm-save.json");
        lock_ = std::make_unique<QLockFile>(modelPath_ + QStringLiteral(".openswmm-save.lock"));
        // A long Save is not a stale lock; QLockFile still detects exited processes.
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock()) {
            error_ = QStringLiteral("Another operation is saving or recovering %1").arg(model);
            return false;
        }
        if (!recovering && (QFileInfo::exists(journalPath_) || QFileInfo(journalPath_).isSymLink())) {
            error_ = QStringLiteral("An interrupted save needs recovery. Reopen %1 before saving; journal: %2")
                         .arg(model, journalPath_);
            return false;
        }
        return true;
    }
    QString dataFile(int index, bool old) const {
        return QDir(dataPath_).filePath(QString::number(index) + (old ? QStringLiteral(".old") : QStringLiteral(".new")));
    }
    bool copyFile(const QString &source, const QString &target, const QString &expected = {}) {
        QFile input(source);
        QSaveFile output(target);
        output.setDirectWriteFallback(false);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
            error_ = QStringLiteral("Cannot copy %1 to %2: %3").arg(source, target,
                input.isOpen() ? output.errorString() : input.errorString());
            return false;
        }
        QCryptographicHash digest(QCryptographicHash::Sha256);
        char buffer[65536];
        while (true) {
            const qint64 n = input.read(buffer, sizeof buffer);
            if (n < 0 || (n > 0 && output.write(buffer, n) != n)) {
                error_ = QStringLiteral("Cannot copy save bytes to %1: %2").arg(target,
                    n < 0 ? input.errorString() : output.errorString());
                output.cancelWriting(); return false;
            }
            if (!n) break;
            if (!expected.isEmpty()) digest.addData(QByteArrayView(buffer, n));
        }
        if (!expected.isEmpty() && QString::fromLatin1(digest.result().toHex()) != expected) {
            error_ = QStringLiteral("Save source changed while copying %1").arg(source);
            output.cancelWriting(); return false;
        }
        if (!output.commit()) {
            error_ = QStringLiteral("Cannot publish %1: %2").arg(target, output.errorString()); return false;
        }
        return true;
    }
    bool writeJournal(bool committed) {
        QJsonArray entries;
        for (const auto &r : records_)
            entries.append(QJsonObject{{"path", r.path}, {"old", r.oldHash}, {"new", r.newHash}, {"permissions", r.permissions}});
        const QByteArray bytes = QJsonDocument(QJsonObject{
            {"version", 1}, {"model", modelPath_}, {"data", QFileInfo(dataPath_).fileName()},
            {"committed", committed}, {"entries", entries}}).toJson();
        QSaveFile file(journalPath_);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            error_ = QStringLiteral("Cannot record save recovery journal %1: %2").arg(journalPath_, file.errorString()); return false;
        }
        return true;
    }
    bool readJournal() {
        auto invalid = [&]() { error_ = QStringLiteral("Invalid save recovery journal %1").arg(journalPath_); return false; };
        QFile file(journalPath_);
        if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) return invalid();
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
        const auto obj = doc.object();
        const QString data = obj.value("data").toString();
        const QString prefix = QFileInfo(journalPath_).fileName() + QStringLiteral(".data-");
        const QString id = data.mid(prefix.size());
        if (parse.error != QJsonParseError::NoError || obj.value("version").toInt() != 1 ||
            obj.value("model").toString() != modelPath_ || !obj.value("committed").isBool() ||
            !data.startsWith(prefix) || QUuid(id).isNull() ||
            QUuid(id).toString(QUuid::WithoutBraces) != id || !obj.value("entries").isArray()) return invalid();
        dataPath_ = QFileInfo(journalPath_).absoluteDir().filePath(data);
        if (!QFileInfo(dataPath_).isDir() || QFileInfo(dataPath_).isSymLink()) return invalid();
        committed_ = obj.value("committed").toBool();
        auto hashValid = [](const QString &s) {
            return s.size() == 64 && QString::fromLatin1(QByteArray::fromHex(s.toLatin1()).toHex()) == s;
        };
        bool hasModel = false;
        for (const auto &v : obj.value("entries").toArray()) {
            const auto o = v.toObject();
            Record r{o.value("path").toString(), o.value("old").toString(), o.value("new").toString(), o.value("permissions").toInt()};
            if (r.path.isEmpty() || !QFileInfo(r.path).isAbsolute() || QDir::cleanPath(r.path) != r.path ||
                (r.oldHash != absent() && !hashValid(r.oldHash)) || !hashValid(r.newHash) ||
                sameFile(r.path, journalPath_) || sameFile(r.path, modelPath_ + QStringLiteral(".openswmm-save.lock")) ||
                r.path.startsWith(dataPath_ + QDir::separator())) return invalid();
            for (const auto &prior : records_) if (sameFile(prior.path, r.path)) return invalid();
            hasModel |= r.path == modelPath_;
            records_.push_back(r);
        }
        if (!hasModel) return invalid();
        return true;
    }
    bool restoreOrFinish() {
        // Validate the entire set before the first repair. Unknown changes or
        // corrupt rollback bytes require intervention, never silent overwrite.
        for (int i = 0; i < int(records_.size()); ++i) {
            const auto &r = records_[i];
            QString current, backup;
            if (resolved(r.path) != r.path || !fingerprint(r.path, current) ||
                (committed_ ? current != r.newHash : (current != r.oldHash && current != r.newHash))) {
                error_ = QStringLiteral("Save recovery found a changed destination: %1").arg(r.path); return false;
            }
            if (!committed_ && r.oldHash != absent() &&
                (QFileInfo(dataFile(i, true)).isSymLink() || !fingerprint(dataFile(i, true), backup) || backup != r.oldHash)) {
                error_ = QStringLiteral("Save recovery copy is missing or damaged: %1").arg(dataFile(i, true)); return false;
            }
        }
        if (!committed_) for (int i = int(records_.size()) - 1; i >= 0; --i) {
            const auto &r = records_[i];
            QString current;
            if (resolved(r.path) != r.path || !fingerprint(r.path, current)) {
                error_ = QStringLiteral("Destination identity changed during recovery: %1").arg(r.path); return false;
            }
            if (current == r.oldHash) {
                if (current != absent() && !QFile::setPermissions(r.path, QFile::Permissions(r.permissions))) {
                    error_ = QStringLiteral("Cannot restore file permissions for %1").arg(r.path); return false;
                }
                continue;
            }
            if (current != r.newHash) { error_ = QStringLiteral("Destination changed during recovery: %1").arg(r.path); return false; }
            if (r.oldHash == absent()) {
                if (!QFile::remove(r.path)) { error_ = QStringLiteral("Cannot remove interrupted new output %1").arg(r.path); return false; }
            } else {
                if (!copyFile(dataFile(i, true), r.path, r.oldHash)) return false;
                if (!QFile::setPermissions(r.path, QFile::Permissions(r.permissions))) {
                    error_ = QStringLiteral("Cannot restore file permissions for %1").arg(r.path); return false;
                }
            }
        }
        return cleanupJournal();
    }
    bool cleanupJournal() {
        if (!QFile::remove(journalPath_)) { error_ = QStringLiteral("Cannot remove completed recovery journal %1").arg(journalPath_); return false; }
        cleanupData();
        return true;
    }
    void cleanupData() {
        // Never recursively remove a directory or follow journal-provided names.
        for (int i = 0; i < int(records_.size()); ++i) {
            QFile::remove(dataFile(i, true));
            QFile::remove(dataFile(i, false));
        }
        QDir().rmdir(dataPath_);
    }
    bool checkpoint(int count) {
        if (!checkpoint_ || checkpoint_(count)) return true;
        error_ = QStringLiteral("Publication interrupted at checkpoint %1").arg(count);
        return false;
    }
    friend class ProjectSaveOutputsTestAccess;
    std::function<bool(int)> checkpoint_;
    std::unique_ptr<QLockFile> lock_;
    QString modelPath_, journalPath_, dataPath_;
    std::vector<Record> records_;
    bool committed_ = false;
    bool rolledBack_ = false;
    std::vector<Protected> protected_;
    std::vector<ProtectedDirectory> protectedDirectories_;
    std::vector<Entry> entries_;
    QString error_;
    bool publicationStarted_ = false;
};
