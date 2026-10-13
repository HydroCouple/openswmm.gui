/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RUNPATHLEASE_H
#define RUNPATHLEASE_H
#include <QStringList>
#include <QMutex>
#include <QMutexLocker>
#include <QFileInfo>
#include <filesystem>

// Process-local reservations, shared by ordinary and checkpoint workers.
// Native exclusive publication remains responsible for external races.
class RunPathLease {
public:
    explicit RunPathLease(const void* owner):m_owner(owner) {}
    ~RunPathLease() {
        QMutexLocker lock(&s_mutex);
        for(auto i=s_entries.begin();i!=s_entries.end();)
            if(m_ids.contains(i->id))i=s_entries.erase(i);else ++i;
    }
    RunPathLease(const RunPathLease&)=delete;
    RunPathLease& operator=(const RunPathLease&)=delete;
    static std::filesystem::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
        return std::filesystem::path(path.toStdWString());
#else
        return std::filesystem::path(path.toUtf8().constData());
#endif
    }
    static bool overlaps(const QString& a,const QString& b) {
        if(a.isEmpty()||b.isEmpty())return false;
        std::error_code ec;
        if(std::filesystem::equivalent(nativePath(a),nativePath(b),ec))return true;
        const auto ca=std::filesystem::weakly_canonical(nativePath(a),ec);
        if(ec)return QFileInfo(a).absoluteFilePath()==QFileInfo(b).absoluteFilePath();
        const auto cb=std::filesystem::weakly_canonical(nativePath(b),ec);
        if(ec)return QFileInfo(a).absoluteFilePath()==QFileInfo(b).absoluteFilePath();
        auto within=[](const auto& child,const auto& parent) {
            auto c=child.begin();for(auto p=parent.begin();p!=parent.end();++p,++c) {
                if(c==child.end())return false;
#ifdef Q_OS_WIN
                if(QString::fromStdWString(c->wstring()).compare(QString::fromStdWString(p->wstring()),Qt::CaseInsensitive)!=0)return false;
#else
                if(*c!=*p)return false;
#endif
            }
            return true;
        };
        return within(ca,cb)||within(cb,ca);
    }
    bool add(const QString& path,bool write,QString* error,const QStringList& active={}) {
        if(path.isEmpty())return true;
        for(const auto& p:active)if(overlaps(path,p)) {
            if(error)*error=QStringLiteral("Path is already in use: %1").arg(path);return false;
        }
        QMutexLocker lock(&s_mutex);
        for(const auto& prior:s_entries)if(prior.owner!=m_owner&&(write||prior.write)&&overlaps(path,prior.path)) {
            if(error)*error=QStringLiteral("Path conflicts with another operation: %1").arg(path);return false;
        }
        const auto id=++s_next;m_ids.append(id);s_entries.append({id,m_owner,path,write});return true;
    }
    static QStringList activePaths() {
        QMutexLocker lock(&s_mutex);QStringList out;
        for(const auto& entry:s_entries)out.append(entry.path);return out;
    }
private:
    struct Entry {quint64 id;const void* owner;QString path;bool write;};
    const void* m_owner;
    QList<quint64> m_ids;
    inline static QMutex s_mutex;
    inline static QList<Entry> s_entries;
    inline static quint64 s_next=0;
};
#endif
