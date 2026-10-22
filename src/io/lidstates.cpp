/**
 * @file      lidstates.cpp
 * @brief     Value snapshots of LID node elevations, heads, and moisture states.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/lidstates.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QTimeZone>

namespace openswmmvis::io
{
namespace
{
constexpr char magic[] = "OSLID001";
constexpr int headerSize = 60; // magic, final .out size/mtime, digest, JSON length
/**
 * @brief Translate state-file diagnostics in the LidStates context.
 * @param[in] text Translation source text.
 * @return Localized text.
 */
QString tr(const char *text)
{
    return QCoreApplication::translate("LidStates", text);
}
/**
 * @brief Select the companion wire format on a borrowed stream.
 * @param[in,out] stream Stream configured for Qt 6, little-endian byte order, and single-precision floating values.
 */
void configure(QDataStream &stream)
{
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
}
// Pair saved states with this exact results file. Hashing is streamed and done
// once at completion, and once per opened/replaced results file, not per frame.
/**
 * @brief Hash an existing output file without loading it wholly into memory.
 * @param[in] path File to read.
 * @return SHA-256 bytes, or an empty array when opening or hashing fails.
 */
QByteArray digest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result() : QByteArray();
}
}
QString lidStatePath(const QString &outputPath)
{
    return outputPath + QStringLiteral(".lidstate");
}

bool LidStateWriter::open(const QString &outputPath, const LidStateFrame &geometry)
{
    m_output = outputPath;
    m_nodes = geometry.nodes;
    QJsonArray nodes;
    for (const auto &node : m_nodes) {
        QJsonArray cells;
        for (const auto &c : node.cells)
            cells.append(QJsonArray{c.layer, c.bottom, c.top, c.porosity});
        nodes.append(
            QJsonObject{{"name", node.name}, {"richards", node.richards}, {"cells", cells}});
    }
    const QByteArray meta = QJsonDocument(QJsonObject{{"si", geometry.si}, {"nodes", nodes}})
                                .toJson(QJsonDocument::Compact);
    if (meta.size() > 16 * 1024 * 1024) {
        m_error = tr("LID results metadata exceeds supported size.");
        return false;
    }
    m_file.setFileName(lidStatePath(outputPath));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = m_file.errorString();
        return false;
    }
    QDataStream stream(&m_file);
    configure(stream);
    stream.writeRawData(magic, 8);
    stream << qint64(0) << qint64(0);
    stream.writeRawData(QByteArray(32, '\0').constData(), 32);
    stream << quint32(meta.size());
    stream.writeRawData(meta.constData(), meta.size());
    if (stream.status() != QDataStream::Ok || !m_file.flush()) {
        m_error = m_file.errorString();
        return false;
    }
    return true;
}
bool LidStateWriter::append(const LidStateFrame &frame)
{
    if (!m_file.isOpen() || !frame.time.isValid())
        return false;
    const qint64 time = frame.time.toMSecsSinceEpoch();
    if (time <= m_lastTime)
        return time == m_lastTime;
    if (frame.nodes.size() != m_nodes.size())
        return false;
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    configure(stream);
    stream << time;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (frame.nodes[i].name != m_nodes[i].name ||
            frame.nodes[i].cells.size() != m_nodes[i].cells.size())
            return false;
        for (const auto &cell : frame.nodes[i].cells)
            stream << float(cell.moisture) << float(cell.pressure) << float(cell.head);
    }
    if (m_file.write(bytes) != bytes.size() || !m_file.flush()) {
        m_error = m_file.errorString();
        m_file.close();
        return false;
    }
    m_lastTime = time;
    return true;
}
bool LidStateWriter::finish()
{
    if (!m_file.isOpen())
        return false;
    const QByteArray hash = digest(m_output);
    if (hash.size() != 32) {
        m_error = tr("Cannot verify LID companion results.");
        return false;
    }
    const QFileInfo info(m_output);
    m_file.seek(8);
    QDataStream stream(&m_file);
    configure(stream);
    stream << info.size() << info.lastModified().toMSecsSinceEpoch();
    stream.writeRawData(hash.constData(), 32);
    const bool ok = stream.status() == QDataStream::Ok && m_file.flush();
    if (!ok)
        m_error = m_file.errorString();
    m_file.close();
    return ok;
}

LidStateFrame LidStateReader::frameAt(const QString &outputPath, bool live, const QDateTime &cursor,
                                      const QStringList &names)
{
    LidStateFrame out;
    auto fail = [&](const QString &why) {
        out.nodes.clear();
        out.time = {};
        out.status = why;
        return out;
    };
    if (!cursor.isValid())
        return fail(tr("Choose a results time to display LID water states."));
    QFile file(lidStatePath(outputPath));
    if (!file.open(QIODevice::ReadOnly))
        return fail(tr("LID water states were not recorded. Run this model again to save them."));
    QDataStream stream(&file);
    configure(stream);
    if (file.read(8) != QByteArray(magic, 8))
        return fail(tr("Unsupported LID results format."));
    qint64 size = 0, mtime = 0;
    quint32 metaSize = 0;
    stream >> size >> mtime;
    const QByteArray hash = file.read(32);
    stream >> metaSize;
    if (stream.status() != QDataStream::Ok || metaSize > 16 * 1024 * 1024 || metaSize == 0 ||
        file.size() < headerSize + metaSize)
        return fail(tr("Incomplete LID results header."));
    if (size > 0) {
        const QFileInfo info(outputPath);
        // mtime is a cache key, not an identity requirement: copied / moved
        // results remain usable after their content hash is verified.
        const auto currentMtime = info.lastModified().toMSecsSinceEpoch();
        if (info.size() != size)
            return fail(tr("LID states belong to different results."));
        if (m_verifiedPath != outputPath || m_verifiedSize != size ||
            m_verifiedMtime != currentMtime || m_verifiedDigest != hash) {
            if (digest(outputPath) != hash)
                return fail(tr("LID states belong to different results."));
            m_verifiedPath = outputPath;
            m_verifiedSize = size;
            m_verifiedMtime = currentMtime;
            m_verifiedDigest = hash;
        }
    } else if (!live)
        return fail(tr("LID results were not finalized; recorded states are unavailable."));
    const QJsonDocument document = QJsonDocument::fromJson(file.read(metaSize));
    if (!document.isObject())
        return fail(tr("Invalid LID results metadata."));
    const auto meta = document.object();
    out.si = meta.value("si").toBool();
    QVector<LidNodeState> nodes;
    qint64 cellCount = 0;
    for (const auto &value : meta.value("nodes").toArray()) {
        const auto object = value.toObject();
        LidNodeState node;
        node.name = object.value("name").toString();
        node.richards = object.value("richards").toBool();
        for (const auto &cell : object.value("cells").toArray()) {
            const auto row = cell.toArray();
            if (row.size() != 4)
                return fail(tr("Invalid LID cell geometry."));
            LidCellState c;
            c.layer = row[0].toInt();
            c.bottom = row[1].toDouble();
            c.top = row[2].toDouble();
            c.porosity = row[3].toDouble();
            if (c.layer <= 0 || !(c.top > c.bottom) || !(c.porosity > 0 && c.porosity <= 1) ||
                (!node.cells.isEmpty() && c.top > node.cells.last().bottom + 1.e-6))
                return fail(tr("Invalid LID cell geometry."));
            node.cells.push_back(c);
            if (++cellCount > 1000000)
                return fail(tr("LID results metadata exceeds supported size."));
        }
        nodes.push_back(node);
    }
    if (!cellCount)
        return fail(tr("No recorded porous LID cells."));
    const qint64 offset = headerSize + metaSize, stride = 8 + 12 * cellCount;
    const qint64 count = (file.size() - offset) / stride;
    if (!live && (file.size() - offset) % stride != 0)
        return fail(tr("Incomplete LID results frame."));
    const qint64 cursorMs = cursor.toMSecsSinceEpoch();
    // Upper bound: latest completed sample at or before the cursor. Never
    // extrapolate a future state backwards or interpolate across wetting fronts.
    qint64 lo = 0, hi = count;
    while (lo < hi) {
        const qint64 mid = lo + (hi - lo) / 2;
        file.seek(offset + mid * stride);
        qint64 time = 0;
        stream >> time;
        if (time <= cursorMs)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return fail(tr("No LID sample at or before this time."));
    file.seek(offset + (lo - 1) * stride);
    qint64 time = 0;
    stream >> time;
    out.time = QDateTime::fromMSecsSinceEpoch(time, QTimeZone::UTC);
    for (auto &node : nodes) {
        if (!names.contains(node.name)) {
            file.seek(file.pos() + 12 * node.cells.size());
            continue;
        }
        for (auto &cell : node.cells) {
            float moisture, pressure, head;
            stream >> moisture >> pressure >> head;
            cell.moisture = moisture;
            cell.pressure = pressure;
            cell.head = head;
        }
        out.nodes.push_back(node);
    }
    if (stream.status() != QDataStream::Ok)
        return fail(tr("Incomplete LID results frame."));
    if (out.nodes.isEmpty())
        return fail(tr("No LID states for nodes on this profile."));
    out.status = tr("LID sample: %1 (last completed state)").arg(out.time.toString(Qt::ISODate));
    return out;
}
} // namespace openswmmvis::io
