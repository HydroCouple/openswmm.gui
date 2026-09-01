/*!
 * \file   meshstagecache.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/meshstagecache.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

Q_LOGGING_CATEGORY(lcMeshPerf, "openswmm.mesh.perf")

namespace mesh {

namespace {

// One canonical QDataStream configuration for both hashing and payload IO —
// version-pinned and full-double so keys and payloads are stable across Qt
// releases and lossless for coordinates.
void configureStream(QDataStream &s)
{
    s.setVersion(QDataStream::Qt_6_0);
    s.setFloatingPointPrecision(QDataStream::DoublePrecision);
}

} // namespace

MeshStageCache::MeshStageCache(const QString &projectInpPath)
{
    const QFileInfo fi(projectInpPath);
    if (projectInpPath.isEmpty())
        return;
    const QString dir = fi.absoluteDir().filePath(QStringLiteral(".meshcache"));
    if (QDir().mkpath(dir))
        m_dir = dir;
}

bool MeshStageCache::isUsable() const
{
    return !m_dir.isEmpty() && QFileInfo(m_dir).isDir();
}

QString MeshStageCache::entryPath(char stage, const QByteArray &key) const
{
    return m_dir + QLatin1Char('/') + QLatin1Char(stage) + QLatin1Char('-')
         + QString::fromLatin1(key) + QStringLiteral(".bin");
}

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------

QByteArray MeshStageCache::boundaryKey(const FileIdentity &src,
                                       const QByteArray   &subcatchHash,
                                       const QString      &layerName,
                                       const QString      &boundaryCRSWkt,
                                       const QString      &meshCRSWkt,
                                       double simplifyEps,
                                       double maxBoundaryEdgeLen,
                                       double minCellSize,
                                       bool   minSizeEnforce)
{
    QByteArray blob;
    {
        QDataStream s(&blob, QIODevice::WriteOnly);
        configureStream(s);
        s << kFormatVersion << quint8('A');
        s << src.absPath << src.mtimeMs << src.sizeBytes;
        s << subcatchHash;
        s << layerName << boundaryCRSWkt << meshCRSWkt;
        s << simplifyEps << maxBoundaryEdgeLen;
        // 2026-08-17 — the payload is conditioned geometry when a minimum cell
        // size is set, so the size has to be part of the identity.
        // 2026-09-01 — enforcement mode changes what conditioning may do to
        // the rings, so it is part of the identity too.
        s << minCellSize << minSizeEnforce;
    }
    return QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toHex();
}

QByteArray MeshStageCache::terrainKey(const FileIdentity &dem, int band,
                                      const DTMThinnerOptions &opts,
                                      bool doThinning,
                                      const QRectF &dtmBbox)
{
    QByteArray blob;
    {
        QDataStream s(&blob, QIODevice::WriteOnly);
        configureStream(s);
        s << kFormatVersion << quint8('B');
        s << dem.absPath << dem.mtimeMs << dem.sizeBytes << qint32(band);
        s << doThinning;
        s << opts.gridSpacing << opts.normalDotThreshold << opts.useAverageDot
          << qint32(opts.maxPoints) << qint32(opts.maxIterations);
        // Quantise the bbox so last-ulp jitter in the corner transform does
        // not defeat the cache.
        s << qRound64(dtmBbox.left()   * 1e6) << qRound64(dtmBbox.top()    * 1e6)
          << qRound64(dtmBbox.right()  * 1e6) << qRound64(dtmBbox.bottom() * 1e6);
    }
    return QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toHex();
}

// ---------------------------------------------------------------------------
// Load / store
// ---------------------------------------------------------------------------

namespace {

// Header check shared by both loaders; returns false on any anomaly (= miss).
bool readHeader(QDataStream &s, quint8 expectStage,
                quint32 magic, quint16 version)
{
    quint32 m = 0; quint16 v = 0; quint8 st = 0;
    s >> m >> v >> st;
    return s.status() == QDataStream::Ok
        && m == magic && v == version && st == expectStage;
}

} // namespace

bool MeshStageCache::loadBoundary(const QByteArray &key, BoundaryPrep *out) const
{
    if (!out || m_dir.isEmpty()) return false;
    QFile f(entryPath('A', key));
    if (!f.open(QIODevice::ReadOnly)) return false;
    QDataStream s(&f);
    configureStream(s);
    if (!readHeader(s, 'A', kMagic, kFormatVersion)) return false;
    BoundaryPrep v;
    s >> v.domains >> v.holeRings >> v.holeSeeds >> v.holeValid >> v.skippedRings;
    if (s.status() != QDataStream::Ok || !s.atEnd()) return false;
    if (v.holeSeeds.size() != v.holeRings.size()
        || v.holeValid.size() != v.holeRings.size()) return false;
    *out = std::move(v);
    return true;
}

bool MeshStageCache::storeBoundary(const QByteArray &key, const BoundaryPrep &v) const
{
    if (m_dir.isEmpty()) return false;
    QSaveFile f(entryPath('A', key));
    if (!f.open(QIODevice::WriteOnly)) return false;
    {
        QDataStream s(&f);
        configureStream(s);
        s << kMagic << kFormatVersion << quint8('A');
        s << v.domains << v.holeRings << v.holeSeeds << v.holeValid << v.skippedRings;
    }
    const bool ok = f.commit();
    if (ok) prune();
    return ok;
}

bool MeshStageCache::loadTerrain(const QByteArray &key, TerrainPoints *out) const
{
    if (!out || m_dir.isEmpty()) return false;
    QFile f(entryPath('B', key));
    if (!f.open(QIODevice::ReadOnly)) return false;
    QDataStream s(&f);
    configureStream(s);
    if (!readHeader(s, 'B', kMagic, kFormatVersion)) return false;
    TerrainPoints v;
    s >> v.xyDtm >> v.z;
    if (s.status() != QDataStream::Ok || !s.atEnd()) return false;
    if (v.z.size() != v.xyDtm.size()) return false;
    *out = std::move(v);
    return true;
}

bool MeshStageCache::storeTerrain(const QByteArray &key, const TerrainPoints &v) const
{
    if (m_dir.isEmpty()) return false;
    QSaveFile f(entryPath('B', key));
    if (!f.open(QIODevice::WriteOnly)) return false;
    {
        QDataStream s(&f);
        configureStream(s);
        s << kMagic << kFormatVersion << quint8('B');
        s << v.xyDtm << v.z;
    }
    const bool ok = f.commit();
    if (ok) prune();
    return ok;
}

void MeshStageCache::prune(int keepPerStage) const
{
    if (m_dir.isEmpty()) return;
    QDir d(m_dir);
    for (const char *pattern : {"A-*.bin", "B-*.bin"})
    {
        QFileInfoList entries = d.entryInfoList(
            {QString::fromLatin1(pattern)}, QDir::Files, QDir::Time);
        for (qsizetype i = keepPerStage; i < entries.size(); ++i)
            QFile::remove(entries[i].absoluteFilePath());
    }
}

} // namespace mesh
