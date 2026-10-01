#include <QSet>
/*!
 * \file   outputstatsregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice QA.1 — implementation. See outputstatsregistry.h for design notes.
 */

#include "output/outputstatsregistry.h"

// Slice QA.2 — registerLayer now stores QPointer<SWMMResultsLayer>
// and dereferences it via .data() / operator=, both of which need
// the complete type. (Earlier QA.1 cut held the raw pointer only.)
#include "layers/swmmresultslayer.h"

#include <QFileInfo>
#include <QHash>
#include <QDir>
#include <QDateTime>

namespace openswmmvis {

OutputStatsRegistry::OutputStatsRegistry(QObject *parent)
    : QObject(parent)
{
}

OutputStatsRegistry::~OutputStatsRegistry() = default;

void OutputStatsRegistry::registerLayer(SWMMResultsLayer *layer,
                                        const QString    &resultsFilePath)
{
    if (!layer) return;

    // Idempotent by pointer identity — re-registering the same layer is
    // a no-op so callers can safely wire registerLayer to both
    // resultsOpened and a one-shot construction hook without thinking
    // about ordering.
    for (const auto &s : m_slots) {
        if (s.layer.data() == layer) return;
    }

    Slot s;
    s.stableId    = QUuid::createUuid();
    s.tooltipPath = QFileInfo(resultsFilePath).absoluteFilePath();
    s.layer       = layer;
    auto run=latestRun(s.tooltipPath);
    s.runId=run.id.isEmpty()?importRun(s.tooltipPath):run.id;
    // shortLabel will be filled by recomputeLabels() below; leaving it
    // blank here means a midflight observer would never see a partial
    // state. recomputeLabels reads from s.tooltipPath, not from the
    // layer pointer.
    m_slots.append(s);

    recomputeLabels();
    emit identitiesChanged();
}

void OutputStatsRegistry::unregisterLayer(SWMMResultsLayer *layer)
{
    if (!layer) return;

    bool removed = false;
    for (int i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].layer.data() == layer) {
            m_slots.removeAt(i);
            removed = true;
            break;
        }
    }
    if (!removed) return;

    recomputeLabels();
    emit identitiesChanged();
}

QList<OutputIdentity> OutputStatsRegistry::identities() const
{
    QList<OutputIdentity> out;
    out.reserve(m_slots.size());
    for (const auto &s : m_slots) {
        OutputIdentity id;
        id.stableId    = s.stableId;
        id.shortLabel  = s.shortLabel;
        id.tooltipPath = s.tooltipPath;
        // QPointer::data() yields nullptr after the layer is destroyed;
        // consumers null-check before dispatch.
        id.layer       = s.layer.data();
        id.runId       = s.runId;
        out.append(id);
    }
    return out;
}

OutputIdentity OutputStatsRegistry::identityFor(const QUuid &id) const
{
    if (id.isNull()) return {};
    for (const auto &s : m_slots) {
        if (s.stableId == id) {
            OutputIdentity out;
            out.stableId    = s.stableId;
            out.shortLabel  = s.shortLabel;
            out.tooltipPath = s.tooltipPath;
            out.layer       = s.layer.data();
            out.runId       = s.runId;
            return out;
        }
    }
    return {};
}

void OutputStatsRegistry::recomputeLabels()
{
    // Two-pass: (1) compute the basename for every slot from the cached
    // tooltipPath (captured at register-time), (2) walk in registration
    // order and append "(N)" when an earlier slot already claimed the
    // same basename. The first occurrence stays unlabelled so single-
    // output projects (the common case) read naturally.
    QHash<QString, int> seen;
    for (auto &s : m_slots) {
        const QString base = QFileInfo(s.tooltipPath).completeBaseName();
        const int count = seen.value(base, 0);
        if (count == 0) {
            s.shortLabel = base;
        } else {
            s.shortLabel = QStringLiteral("%1 (%2)").arg(base).arg(count + 1);
        }
        seen[base] = count + 1;
        auto version=run(s.runId);
        if(!version.id.isEmpty())s.shortLabel+=QStringLiteral(" · Run %1 · %2").arg(version.number).arg(version.state);
    }
}

OutputRun OutputStatsRegistry::run(const QString &id) const
{
    for (const auto &r : m_runs)
        if (r.id == id)
            return r;
    return {};
}
OutputRun OutputStatsRegistry::latestRun(const QString &path) const
{
    const auto p = QFileInfo(path).absoluteFilePath();
    for (auto it = m_runs.crbegin(); it != m_runs.crend(); ++it)
        if (it->path == p)
            return *it;
    return {};
}
QString OutputStatsRegistry::importRun(const QString &path)
{
    auto previous = latestRun(path);
    OutputRun r;
    r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.sourceId = previous.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                       : previous.sourceId;
    r.path = QFileInfo(path).absoluteFilePath();
    r.number = previous.id.isEmpty() ? 1 : previous.number + 1;
    r.previousId = previous.id;
    r.label = QFileInfo(path).completeBaseName() + QStringLiteral(" · ") +
              QFileInfo(QFileInfo(path).absolutePath()).fileName();
    r.state = QStringLiteral("Imported");
    r.completedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_runs.append(r);
    emit runsChanged();
    return r.id;
}
QString OutputStatsRegistry::beginRun(const QString &path, const QJsonObject &snapshot,
                                      const QString &reportPath)
{
    QString id = importRun(path);
    for (auto &r : m_runs)
        if (r.id == id)
        {
            r.state = QStringLiteral("Running");
            r.native = true;
            r.snapshot = snapshot;
            r.reportPath = reportPath;
            r.completedAt.clear();
        }
    for (auto &s : m_slots)
        if (s.tooltipPath == QFileInfo(path).absoluteFilePath())
            s.runId = id;
    recomputeLabels();
    emit identitiesChanged();
    emit runsChanged();
    return id;
}
void OutputStatsRegistry::finishRun(const QString &id, bool success, bool cancelled)
{
    for (auto &r : m_runs)
        if (r.id == id)
        {
            r.state = success
                          ? QStringLiteral("Complete")
                          : (cancelled ? QStringLiteral("Cancelled") : QStringLiteral("Failed"));
            r.completedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            QFileInfo f(r.path);
            r.size = f.size();
            r.modified = f.lastModified().toMSecsSinceEpoch();
        }
    recomputeLabels();
    emit identitiesChanged();
    emit runsChanged();
}
void OutputStatsRegistry::contentOpened(SWMMResultsLayer *layer, const QString &path, bool live)
{
    for (auto &s : m_slots)
        if (s.layer == layer)
        {
            auto r = run(s.runId);
            QFileInfo f(path);
            if (r.id.isEmpty() || r.path != f.absoluteFilePath() ||
                (!live && r.state != QStringLiteral("Running") && r.size >= 0 &&
                 (r.size != f.size() || r.modified != f.lastModified().toMSecsSinceEpoch())))
                s.runId = importRun(path);
            s.tooltipPath = f.absoluteFilePath();
            for (auto &v : m_runs)
                if (v.id == s.runId)
                {
                    if (live && !v.native)
                        v.state = QStringLiteral("Running");
                    else if (!live && !v.native)
                        v.state = QStringLiteral("Imported");
                    v.size = f.size();
                    v.modified = f.lastModified().toMSecsSinceEpoch();
                }
            break;
        }
    recomputeLabels();
    emit identitiesChanged();
    emit runsChanged();
}
void OutputStatsRegistry::attachAnalysis(const QString &id, const QString &fingerprint,
                                         const QString &package, const QJsonObject &snapshot)
{
    for (auto &r : m_runs)
        if (r.id == id)
        {
            r.fingerprint = fingerprint;
            r.packagePath = package;
            r.snapshot = snapshot;
        }
    emit runsChanged();
}
void OutputStatsRegistry::adoptAnalysis(const QString &id, const QString &sourceId,
                                        const QString &path, const QString &label,
                                        const QString &fingerprint, const QString &package,
                                        const QJsonObject &snapshot)
{
    if (run(id).id.isEmpty())
    {
        OutputRun r;
        r.id = id;
        r.sourceId = sourceId;
        r.path = path;
        r.label = label;
        r.state = "Imported";
        m_runs.append(r);
    }
    attachAnalysis(id, fingerprint, package, snapshot);
}
void OutputStatsRegistry::setRetainedPath(const QString &id, const QString &path)
{
    for (auto &r : m_runs)
        if (r.id == id)
            r.retainedPath = path;
    emit runsChanged();
}
QJsonArray OutputStatsRegistry::saveRuns(const QString &base) const
{
    QJsonArray a;
    QDir dir(base);
    for (const auto &r : m_runs)
        a.append(QJsonObject{
            {"id", r.id},
            {"sourceId", r.sourceId},
            {"path", dir.relativeFilePath(r.path)},
            {"label", r.label},
            {"state", r.state},
            {"previousId", r.previousId},
            {"completedAt", r.completedAt},
            {"fingerprint", r.fingerprint},
            {"package", r.packagePath.isEmpty() ? QString() : dir.relativeFilePath(r.packagePath)},
            {"retained",
             r.retainedPath.isEmpty() ? QString() : dir.relativeFilePath(r.retainedPath)},
            {"report", r.reportPath.isEmpty() ? QString() : dir.relativeFilePath(r.reportPath)},
            {"snapshot", r.snapshot},
            {"number", r.number},
            {"size", double(r.size)},
            {"modified", double(r.modified)},
            {"native", r.native}});
    return a;
}
void OutputStatsRegistry::restoreRuns(const QJsonArray &a, const QString &base)
{
    m_runs.clear();
    QDir dir(base);
    QSet<QString> ids;
    for (auto v : a)
    {
        auto j = v.toObject();
        OutputRun r;
        r.id = j["id"].toString();
        if (r.id.isEmpty() || ids.contains(r.id))
            continue;
        ids.insert(r.id);
        r.sourceId = j["sourceId"].toString();
        r.path = dir.absoluteFilePath(j["path"].toString());
        r.label = j["label"].toString();
        r.state = j["state"].toString();
        if (r.state == "Running")
            r.state = "Interrupted";
        r.previousId = j["previousId"].toString();
        r.completedAt = j["completedAt"].toString();
        r.fingerprint = j["fingerprint"].toString();
        r.snapshot = j["snapshot"].toObject();
        r.number = j["number"].toInt(1);
        r.size = qint64(j["size"].toDouble(-1));
        r.modified = qint64(j["modified"].toDouble(-1));
        r.native = j["native"].toBool();
        QString p = j["package"].toString();
        if (!p.isEmpty())
            r.packagePath = dir.absoluteFilePath(p);
        p = j["retained"].toString();
        if (!p.isEmpty())
            r.retainedPath = dir.absoluteFilePath(p);
        p = j["report"].toString();
        if (!p.isEmpty())
            r.reportPath = dir.absoluteFilePath(p);
        m_runs.append(r);
    }
    for (auto &s : m_slots)
        s.runId = latestRun(s.tooltipPath).id;
    recomputeLabels();
    emit identitiesChanged();
    emit runsChanged();
}

} // namespace openswmmvis
