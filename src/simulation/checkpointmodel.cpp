/* SPDX-License-Identifier: GPL-3.0-or-later */

/**
 * @file      checkpointmodel.cpp
 * @brief     Project-owned checkpoint model and controller for GUI views.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "simulation/checkpointmodel.h"
#include "core/displaynumber.h"
#include "simulation/runpathlease.h"
#include "simulation/simulationrunner.h"
#include "core/swmmdatetime.h"
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>
#include <QDir>
#include <QSet>
#include <algorithm>
#include <limits>

QString CheckpointModel::recipe(const CheckpointRequest &r)
{
    QJsonObject o{
        {"revision", r.projectRevision}, {"hash", QString::fromLatin1(r.modelSha256.toHex())},
        {"engine", r.engineVersion},     {"profile", r.outputProfile},
        {"source", r.sourceHistory},     {"detail", r.detailedSources},
        {"token", r.ownershipToken},     {"model", r.modelPath}};
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
CheckpointModel::CheckpointModel(const QString &catalog, QObject *parent)
    : QAbstractTableModel(parent), m_catalog(catalog)
{
    load();
    connect(&m_io, &CheckpointWorker::finished, this, [this](const CheckpointResult &r) {
        if (r.operation == CheckpointRequest::Operation::Export) {
            m_status = r.success ? tr("Exported: %1").arg(r.publishedPath)
                                 : tr("Export failed [%1]: %2").arg(r.errorCode).arg(r.error);
        } else {
            for (int row = 0; row < m_entries.size(); ++row)
                if (m_entries[row].id == m_ioEntry) {
                    auto &e = m_entries[row];
                    e.compatible = r.success && !r.stale && !r.cancelled && !m_closed;
                    e.inspection = e.compatible  ? tr("Compatible")
                                   : r.cancelled ? tr("Cancelled")
                                   : r.stale     ? tr("Stale")
                                             : tr("Failed [%1]: %2").arg(r.errorCode).arg(r.error);
                    if (e.compatible) {
                        e.metadata = r.metadata;
                        e.recipe = m_recipe;
                        e.revision = r.projectRevision;
                    }
                    m_status = e.inspection;
                    changed(row);
                    persist();
                    break;
                }
        }
        emit stateChanged();
        if (!m_closed)
            emit notice(m_status);
    });
    connect(&m_run, &CheckpointWorker::snowLedgerChanged, this,
            [this](quint64, const QStringList &rows) {
                if (!m_closed)
                    emit runSnowLedgerChanged(rows);
            });
    connect(&m_run, &CheckpointWorker::saved, this,
            [this](quint64, const CheckpointSaveResult &s) { recordSave(s); });
    connect(&m_run, &CheckpointWorker::timeline, this,
            [this](quint64, double a, double b) { emit runTimeline(a, b); });
    connect(&m_run, &CheckpointWorker::progress, this, [this](quint64, double seconds) {
        m_status = tr("Running · accepted %1 s").arg(openswmmvis::displayNumber(seconds));
        emit runProgress(seconds);
        emit stateChanged();
    });
    connect(&m_run, &CheckpointWorker::finished, this, [this](const CheckpointResult &r) {
        // Fallback covers saves whose queued signal could not be delivered.
        for (const auto &saved : r.saves)
            recordSave(saved);
        m_status =
            r.success
                ? tr("Completed · accepted %1 s").arg(openswmmvis::displayNumber(r.acceptedSeconds))
            : r.cancelled ? tr("Stopped · accepted %1 s; partial files retained")
                                .arg(openswmmvis::displayNumber(r.acceptedSeconds))
                          : tr("Run failed [%1]: %2 · accepted %3 s; partial files retained")
                                .arg(r.errorCode)
                                .arg(r.error, openswmmvis::displayNumber(r.acceptedSeconds));
        emit runFinished(r);
        emit stateChanged();
        if (!m_closed)
            emit notice(m_status);
    });
}
int CheckpointModel::rowCount(const QModelIndex &p) const
{
    return p.isValid() ? 0 : m_entries.size();
}
int CheckpointModel::columnCount(const QModelIndex &p) const
{
    return p.isValid() ? 0 : 5;
}
const CheckpointEntry *CheckpointModel::entry(int row) const
{
    return row >= 0 && row < m_entries.size() ? &m_entries[row] : nullptr;
}
QVariant CheckpointModel::data(const QModelIndex &i, int role) const
{
    const auto *e = entry(i.row());
    if (!e || !i.isValid())
        return {};
    if (role == Qt::CheckStateRole && i.column() == 4)
        return e->pinned ? Qt::Checked : Qt::Unchecked;
    if (role == Qt::ToolTipRole)
        return e->path + "\n" + e->inspection;
    if (role != Qt::DisplayRole)
        return {};
    switch (i.column()) {
    case 0:
        return e->name;
    case 1:
        return e->metadata.isEmpty()
                   ? (std::isfinite(e->acceptedDate) ? calendar(e->acceptedDate) : tr("Unknown"))
                   : calendar(e->metadata.value("engine_date"));
    case 2:
        return e->metadata.isEmpty()                              ? tr("Unknown")
               : e->metadata.value("relocatable_bundle").toBool() ? tr("Bundle")
               : e->metadata.value("file_outputs").toBool()       ? tr("File-backed")
                                                                  : tr("Checkpoint");
    case 3:
        return e->inspection;
    default:
        return {};
    }
}
QVariant CheckpointModel::headerData(int c, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    const QStringList labels{tr("Name"), tr("Accepted time"), tr("Kind"), tr("Inspection"),
                             tr("Pin")};
    return labels.value(c);
}
Qt::ItemFlags CheckpointModel::flags(const QModelIndex &i) const
{
    auto f = QAbstractTableModel::flags(i);
    if (i.isValid() && i.column() == 4)
        f |= Qt::ItemIsUserCheckable;
    return f;
}
bool CheckpointModel::setData(const QModelIndex &i, const QVariant &v, int role)
{
    if (!entry(i.row()) || i.column() != 4 || role != Qt::CheckStateRole)
        return false;
    m_entries[i.row()].pinned = v.toInt() == Qt::Checked;
    changed(i.row());
    persist();
    emit stateChanged();
    return true;
}
void CheckpointModel::changed(int row)
{
    emit dataChanged(index(row, 0), index(row, 4));
}
void CheckpointModel::setContext(const CheckpointRequest &r)
{
    const auto next = recipe(r);
    m_context = r;
    if (next == m_recipe)
        return;
    m_recipe = next;
    m_io.invalidate();
    for (int row = 0; row < m_entries.size(); ++row) {
        auto &e = m_entries[row];
        e.compatible = false;
        if (!e.metadata.isEmpty())
            e.inspection = tr("Stale · previous result");
        changed(row);
    }
    emit stateChanged();
}
void CheckpointModel::setInventory(const QStringList &inputs, const QStringList &active)
{
    m_context.protectedInputs = inputs;
    m_context.activePaths = active;
}
bool CheckpointModel::importFile(const QString &path)
{
    if (m_closed || !QFileInfo(path).isAbsolute() || !QFileInfo(path).isFile())
        return false;
    for (const auto &e : m_entries)
        if (SimulationRunner::sameFilePath(path, e.path))
            return true;
    CheckpointEntry e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.name = QFileInfo(path).fileName();
    e.path = path;
    beginInsertRows({}, m_entries.size(), m_entries.size());
    m_entries.append(e);
    endInsertRows();
    persist();
    emit stateChanged();
    return true;
}
bool CheckpointModel::forget(int row)
{
    if (!entry(row) || m_closed)
        return false;
    if (m_entries[row].id == m_ioEntry && ioBusy())
        m_io.invalidate();
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    persist();
    emit stateChanged();
    return true;
}
bool CheckpointModel::inspectEntry(int row)
{
    if (!entry(row) || ioBusy() || m_closed)
        return false;
    auto r = m_context;
    r.operation = CheckpointRequest::Operation::Inspect;
    r.checkpointPath = m_entries[row].path;
    m_ioContext = r;
    m_ioEntry = m_entries[row].id;
    m_entries[row].compatible = false;
    m_entries[row].inspection = tr("Checking…");
    changed(row);
    if (!m_io.start(r)) {
        m_entries[row].inspection = tr("Dispatch failed");
        changed(row);
        return false;
    }
    m_status = tr("Checking… · no estimated percentage");
    emit stateChanged();
    return true;
}
bool CheckpointModel::exportEntry(int row, const QString &folder)
{
    if (!entry(row) || ioBusy() || m_closed)
        return false;
    auto r = m_context;
    r.operation = CheckpointRequest::Operation::Export;
    r.checkpointPath = m_entries[row].path;
    r.destination = folder;
    m_ioContext = r;
    m_ioEntry.clear();
    if (!m_io.start(r))
        return false;
    m_status = tr("Exporting…");
    emit stateChanged();
    return true;
}
bool CheckpointModel::startRun(int row, const QString &folder)
{
    if (runBusy() || ioBusy() || m_closed ||
        (row >= 0 && (!entry(row) || !entry(row)->compatible || entry(row)->recipe != m_recipe)))
        return false;
    auto r = m_context;
    r.destination = folder;
    r.operation =
        row < 0 ? CheckpointRequest::Operation::Run : CheckpointRequest::Operation::Resume;
    r.checkpointPath = row < 0 ? QString() : entry(row)->path;
    m_runContext = r;
    m_runRecipe = m_recipe;
    m_lineage = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!m_run.start(r))
        return false;
    m_status = tr("Preparing a new run…");
    emit runStarted(QFileInfo(folder).fileName(), r.modelPath);
    emit stateChanged();
    return true;
}
bool CheckpointModel::save(const QString &path)
{
    return !m_closed && m_run.queueSave(path);
}
void CheckpointModel::cancelIO()
{
    m_io.cancel();
    m_status = tr("Cancel requested · waiting for native IO to finish");
    emit stateChanged();
}
void CheckpointModel::cancelRun()
{
    m_run.cancel();
    m_status = tr("Stop requested · waiting for the accepted boundary");
    emit stateChanged();
}
void CheckpointModel::closeProject()
{
    m_closed = true;
    m_io.invalidate();
    m_run.cancel();
    emit stateChanged();
}
QStringList CheckpointModel::reservedPaths() const
{
    QStringList out;
    if (runBusy())
        out << m_runContext.destination << m_runContext.modelPath << m_runContext.checkpointPath;
    if (ioBusy()) {
        if (m_ioContext.operation == CheckpointRequest::Operation::Export)
            out << m_ioContext.destination;
        out << m_ioContext.modelPath << m_ioContext.checkpointPath;
        for (const auto &e : m_entries)
            if (e.id == m_ioEntry)
                out << e.path;
    }
    out.removeAll(QString());
    return out;
}
void CheckpointModel::recordSave(const CheckpointSaveResult &s)
{
    if (!s.success) {
        m_status = tr("Save failed [%1]: %2").arg(s.errorCode).arg(s.error);
        if (!m_closed)
            emit notice(m_status);
        return;
    }
    for (const auto &e : m_entries)
        if (e.path == s.path)
            return;
    CheckpointEntry e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.path = s.path;
    e.name = QFileInfo(s.path).fileName();
    e.lineage = m_lineage;
    e.publication = ++m_publication;
    e.recipe = m_runRecipe;
    e.revision = m_runContext.projectRevision;
    e.acceptedDate = s.acceptedDate;
    e.acceptedSeconds = s.acceptedSeconds;
    e.inspection = tr("Saved · inspection required");
    beginInsertRows({}, m_entries.size(), m_entries.size());
    m_entries.append(e);
    endInsertRows();
    const bool stored = persist();
    m_status =
        stored ? tr("Saved: %1").arg(s.path) : tr("Saved; list update failed: %1").arg(s.path);
    emit stateChanged();
    if (!m_closed)
        emit notice(m_status);
}
void CheckpointModel::load()
{
    QFile f(m_catalog);
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly)) {
        m_writable = false;
        m_status = tr("Catalog unreadable; files remain unchanged");
        return;
    }
    const auto bytes = f.readAll();
    m_catalogHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    const auto root = doc.object();
    if (error.error != QJsonParseError::NoError || root.value("schema").toInt() != 1 ||
        !root.value("entries").isArray()) {
        m_writable = false;
        m_status = tr("Catalog invalid; files remain unchanged");
        return;
    }
    QSet<QString> ids;
    for (const auto &v : root.value("entries").toArray()) {
        const auto o = v.toObject();
        CheckpointEntry e;
        e.id = o.value("id").toString();
        e.path = o.value("path").toString();
        e.name = o.value("name").toString();
        bool ok = false;
        e.publication = o.value("publication").toString().toULongLong(&ok);
        if (e.id.isEmpty() || ids.contains(e.id) || !QFileInfo(e.path).isAbsolute() || !ok) {
            m_entries.clear();
            m_writable = false;
            m_status = tr("Catalog invalid; files remain unchanged");
            return;
        }
        ids.insert(e.id);
        e.lineage = o.value("lineage").toString();
        e.pinned = o.value("pinned").toBool();
        e.revision = o.value("revision").toString();
        e.recipe = o.value("recipe").toString();
        e.metadata = o.value("metadata").toObject();
        e.acceptedDate = o.value("accepted_date").toDouble(qQNaN());
        e.acceptedSeconds = o.value("accepted_seconds").toDouble(qQNaN());
        e.inspection = e.metadata.isEmpty() ? tr("Not checked") : tr("Stale · previous result");
        m_publication = std::max(m_publication, e.publication);
        m_entries.append(e);
    }
}
bool CheckpointModel::persist()
{
    if (!m_writable) {
        emit notice(tr("List update failed; catalog is unreadable or invalid"));
        return false;
    }
    QJsonArray entries;
    for (const auto &e : m_entries)
        entries.append(QJsonObject{
            {"id", e.id},
            {"name", e.name},
            {"path", e.path},
            {"lineage", e.lineage},
            {"publication", QString::number(e.publication)},
            {"pinned", e.pinned},
            {"revision", e.revision},
            {"recipe", e.recipe},
            {"metadata", e.metadata},
            {"accepted_date",
             std::isfinite(e.acceptedDate) ? QJsonValue(e.acceptedDate) : QJsonValue::Null},
            {"accepted_seconds",
             std::isfinite(e.acceptedSeconds) ? QJsonValue(e.acceptedSeconds) : QJsonValue::Null}});
    QFile existing(m_catalog);
    QByteArray current;
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) {
            emit notice(tr("List update failed; catalog cannot be read"));
            return false;
        }
        current = QCryptographicHash::hash(existing.readAll(), QCryptographicHash::Sha256);
    }
    if (current != m_catalogHash) {
        emit notice(tr("List update failed; catalog changed in another view or application"));
        return false;
    }
    QSaveFile f(m_catalog);
    const auto bytes = QJsonDocument(QJsonObject{{"schema", 1}, {"entries", entries}}).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        emit notice(tr("List update failed: %1").arg(f.errorString()));
        return false;
    }
    m_catalogHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return true;
}
QString CheckpointModel::calendar(const QJsonValue &raw)
{
    if (!raw.isDouble())
        return tr("—");
    const auto date = openswmmvis::core::swmmDateTimeToMilliseconds(raw.toDouble());
    return date.isValid() ? date.toString("yyyy-MM-dd HH:mm:ss.zzz") : tr("Invalid date");
}
QString CheckpointModel::coverage(const QJsonObject &r)
{
    if (!r.value("enabled").toBool())
        return tr("Disabled");
    if (r.value("records").toString() == "0")
        return tr("Enabled · 0 records · cadence %1 s")
            .arg(openswmmvis::displayNumber(r.value("cadence_seconds").toDouble()));
    return tr("%1 records · %2 → %3 · cadence %4 s")
        .arg(r.value("records").toString(), calendar(r.value("first_date")),
             calendar(r.value("last_date")),
             openswmmvis::displayNumber(r.value("cadence_seconds").toDouble()));
}
QString CheckpointModel::clocks(const QJsonObject &m)
{
    const auto seconds = [](double s) { return openswmmvis::displayNumber(s); };
    QString text =
        tr("Engine: %1 s · %2\n")
            .arg(seconds(m.value("engine_seconds").toDouble()), calendar(m.value("engine_date")));
    const auto source = m.value("completed_source_seconds");
    text += source.isNull()
                ? tr("Completed sources: Not applicable")
                : tr("Completed sources: %1 s · pending interval %2 s")
                      .arg(seconds(source.toDouble()),
                           seconds(m.value("engine_seconds").toDouble() - source.toDouble()));
    const auto driver = m.value("driver");
    if (driver.isNull())
        text += tr("\nDriver: No driver");
    else {
        const auto d = driver.toObject();
        const double next = d.value("next_update_seconds").toDouble();
        text += tr("\nDriver: %1 · %2 · next sequence %3")
                    .arg(next < 0 ? tr("Exhausted")
                         : next <= m.value("engine_seconds").toDouble()
                             ? tr("Due now")
                             : tr("Next update %1 s").arg(seconds(next)),
                         d.value("identity").toString(), d.value("next_sequence").toString());
    }
    return text;
}
QList<CheckpointFile> CheckpointModel::files(int row) const
{
    const auto *e = entry(row);
    if (!e)
        return {};
    QList<CheckpointFile> out;
    out.append(
        {tr("Checkpoint"), e->path, e->metadata.value("checkpoint_bytes").toString(), {}, false});
    for (const auto &v : e->metadata.value("companions").toArray()) {
        const auto f = v.toObject();
        out.append({f.value("role").toString(),
                    f.value("path").toString(),
                    f.value("required_bytes").toString(),
                    {},
                    false});
    }
    for (auto &file : out) {
        QFileInfo f(file.path);
        file.readable = f.isFile() && f.isReadable();
        file.currentBytes = file.readable ? QString::number(f.size()) : tr("Missing / unreadable");
    }
    return out;
}
QList<int> CheckpointModel::suggestions(int n) const
{
    if (n <= 0)
        return {};
    QList<int> result;
    QMap<QString, QList<int>> groups;
    for (int row = 0; row < m_entries.size(); ++row) {
        const auto &e = m_entries[row];
        if (e.lineage.isEmpty() || !e.publication || e.metadata.isEmpty() ||
            !QFileInfo(e.path).isReadable())
            continue;
        bool safe = true;
        for (const auto &f : files(row))
            if (!f.readable)
                safe = false;
        for (const auto &p : reservedPaths() + m_context.activePaths + RunPathLease::activePaths())
            for (const auto &f : files(row))
                if (RunPathLease::overlaps(f.path, p))
                    safe = false;
        if (safe)
            groups[e.lineage].append(row);
    }
    for (auto rows : groups) {
        std::sort(rows.begin(), rows.end(), [this](int a, int b) {
            return m_entries[a].publication > m_entries[b].publication;
        });
        for (int i = n; i < rows.size(); ++i)
            if (!m_entries[rows[i]].pinned)
                result.append(rows[i]);
    }
    return result;
}
CheckpointStorage CheckpointModel::storage(const QList<int> &rows) const
{
    CheckpointStorage out;
    for (int row : rows)
        for (const auto &f : files(row)) {
            if (!f.readable) {
                out.complete = false;
                continue;
            }
            bool duplicate = false;
            for (const auto &path : out.paths)
                if (SimulationRunner::sameFilePath(f.path, path))
                    duplicate = true;
            if (duplicate)
                continue;
            out.paths.append(f.path);
            bool ok = false;
            const auto size = f.currentBytes.toULongLong(&ok);
            if (!ok || size > std::numeric_limits<quint64>::max() - out.bytes)
                out.complete = false;
            else
                out.bytes += size;
        }
    return out;
}
