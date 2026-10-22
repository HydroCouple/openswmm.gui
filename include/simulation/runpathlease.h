/* SPDX-License-Identifier: GPL-3.0-or-later */

/**
 * @file      runpathlease.h
 * @brief     Process-local reservations for ordinary and checkpoint run output paths.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef RUNPATHLEASE_H
#define RUNPATHLEASE_H
#include <QStringList>
#include <QMutex>
#include <QMutexLocker>
#include <QFileInfo>
#include <filesystem>

// Process-local reservations, shared by ordinary and checkpoint workers.
// Native exclusive publication remains responsible for external races.
class RunPathLease
{
public:
    /**
     * @brief Create a scoped owner of process-local path reservations.
     * @param[in] owner Opaque identity token, never dereferenced; reservations with the same identity do not conflict with each other.
     */
    explicit RunPathLease(const void *owner) : m_owner(owner) {}
    /**
     * @brief Release only reservations created by this lease, under the shared registry mutex.
     */
    ~RunPathLease()
    {
        QMutexLocker lock(&s_mutex);
        for (auto i = s_entries.begin(); i != s_entries.end();)
            if (m_ids.contains(i->id))
                i = s_entries.erase(i);
            else
                ++i;
    }
    /**
     * @brief Prohibit copying reservation ownership.
     */
    RunPathLease(const RunPathLease &) = delete;
    /**
     * @brief Prohibit assigning reservation ownership.
     */
    RunPathLease &operator=(const RunPathLease &) = delete;
    /**
     * @brief Convert a Qt path to native filesystem encoding.
     * @param[in] path File/directory path text.
     * @return Wide path on Windows, UTF-8 path elsewhere; performs no existence checks.
     */
    static std::filesystem::path nativePath(const QString &path)
    {
#ifdef Q_OS_WIN
        return std::filesystem::path(path.toStdWString());
#else
        return std::filesystem::path(path.toUtf8().constData());
#endif
    }
    /**
     * @brief Detect equal/aliased paths or ancestor-directory containment.
     * @param[in] a First file/directory path; empty never overlaps.
     * @param[in] b Second file/directory path; empty never overlaps.
     * @return True for filesystem equivalence or canonical ancestor relation; if canonicalization fails, falls back to absolute-string equality.
     * @details Canonical component comparisons are case-insensitive on Windows. This is an overwrite/reservation guard, not an external filesystem lock.
     */
    static bool overlaps(const QString &a, const QString &b)
    {
        if (a.isEmpty() || b.isEmpty())
            return false;
        std::error_code ec;
        if (std::filesystem::equivalent(nativePath(a), nativePath(b), ec))
            return true;
        const auto ca = std::filesystem::weakly_canonical(nativePath(a), ec);
        if (ec)
            return QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
        const auto cb = std::filesystem::weakly_canonical(nativePath(b), ec);
        if (ec)
            return QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
        auto within = [](const auto &child, const auto &parent) {
            auto c = child.begin();
            for (auto p = parent.begin(); p != parent.end(); ++p, ++c) {
                if (c == child.end())
                    return false;
#ifdef Q_OS_WIN
                if (QString::fromStdWString(c->wstring())
                        .compare(QString::fromStdWString(p->wstring()), Qt::CaseInsensitive) != 0)
                    return false;
#else
                if (*c != *p)
                    return false;
#endif
            }
            return true;
        };
        return within(ca, cb) || within(cb, ca);
    }
    /**
     * @brief Reserve a read/write path when it does not conflict with active operations.
     * @param[in] path Candidate path; empty succeeds without reserving anything.
     * @param[in] write True for exclusive writer intent; different owners may share read-only reservations.
     * @param[out] error Optional conflict diagnostic, unchanged on success.
     * @param[in] active Additional paths to reject on any overlap, regardless of read/write intent.
     * @return False on overlap with active paths or another-owner reservation involving a writer; true after recording a unique lease entry otherwise.
     */
    bool add(const QString &path, bool write, QString *error, const QStringList &active = {})
    {
        if (path.isEmpty())
            return true;
        for (const auto &p : active)
            if (overlaps(path, p)) {
                if (error)
                    *error = QStringLiteral("Path is already in use: %1").arg(path);
                return false;
            }
        QMutexLocker lock(&s_mutex);
        for (const auto &prior : s_entries)
            if (prior.owner != m_owner && (write || prior.write) && overlaps(path, prior.path)) {
                if (error)
                    *error = QStringLiteral("Path conflicts with another operation: %1").arg(path);
                return false;
            }
        const auto id = ++s_next;
        m_ids.append(id);
        s_entries.append({id, m_owner, path, write});
        return true;
    }
    /**
     * @brief Snapshot all currently reserved paths under the shared mutex.
     * @return Owning list including duplicate paths/reservations in registration order.
     */
    static QStringList activePaths()
    {
        QMutexLocker lock(&s_mutex);
        QStringList out;
        for (const auto &entry : s_entries)
            out.append(entry.path);
        return out;
    }

private:
    struct Entry {
        quint64 id;
        const void *owner;
        QString path;
        bool write;
    };
    const void *m_owner;
    QList<quint64> m_ids;
    inline static QMutex s_mutex;
    inline static QList<Entry> s_entries;
    inline static quint64 s_next = 0;
};
#endif
