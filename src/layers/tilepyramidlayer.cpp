/*!
 * \file   tilepyramidlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "layers/tilepyramidlayer.h"

#include <limits>

TilePyramidLayer::TilePyramidLayer(OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(parent)
{
}

TilePyramidLayer::~TilePyramidLayer() = default;

void TilePyramidLayer::cacheTile(const QString &key, const QImage &img,
                                 const MapExtent &extent, int level)
{
    if (img.isNull())
        return;
    QMutexLocker lock(&m_tileMutex);
    m_tileCache.insert(key, new CachedTile{ img, extent, level });
    m_tileIndex.insert(key, { extent, level });
    if (m_tileIndex.size() > m_tileCache.maxCost() + 32)
        pruneIndexLocked();
}

bool TilePyramidLayer::isTileCached(const QString &key) const
{
    QMutexLocker lock(&m_tileMutex);
    return m_tileCache.contains(key);
}

QImage TilePyramidLayer::cachedTileImage(const QString &key) const
{
    QMutexLocker lock(&m_tileMutex);
    const CachedTile *t = m_tileCache.object(key);
    return t ? t->img : QImage();
}

void TilePyramidLayer::clearTiles()
{
    QMutexLocker lock(&m_tileMutex);
    m_tileCache.clear();
    m_tileIndex.clear();
}

void TilePyramidLayer::setTileCacheCapacity(int maxTiles)
{
    QMutexLocker lock(&m_tileMutex);
    m_tileCache.setMaxCost(maxTiles);
}

int TilePyramidLayer::tileCacheCapacity() const
{
    QMutexLocker lock(&m_tileMutex);
    return m_tileCache.maxCost();
}

// Drop side-index entries whose cache entry has been evicted. Caller holds
// m_tileMutex.
void TilePyramidLayer::pruneIndexLocked()
{
    for (auto it = m_tileIndex.begin(); it != m_tileIndex.end();)
    {
        if (m_tileCache.contains(it.key()))
            ++it;
        else
            it = m_tileIndex.erase(it);
    }
}

bool TilePyramidLayer::resolveTileForDraw(const QString &key,
                                          const MapExtent &tileExtent,
                                          double maxSpanRatio,
                                          TileDraw &out) const
{
    QMutexLocker lock(&m_tileMutex);

    // Exact hit — QCache::object() promotes the entry to most-recently-used,
    // which also protects actively-used tiles from eviction.
    if (const CachedTile *t = m_tileCache.object(key); t && !t->img.isNull())
    {
        out.img   = t->img;
        out.src   = QRectF(t->img.rect());
        out.exact = true;
        return true;
    }

    if (tileExtent.width() <= 0.0 || tileExtent.height() <= 0.0)
        return false;

    // Fallback: the smallest cached tile whose extent contains this tile.
    // Tiles at one level are disjoint, so any container is coarser (or a
    // same-footprint tile from another level — an equally good stand-in).
    const double epsX = tileExtent.width()  * 1e-6;
    const double epsY = tileExtent.height() * 1e-6;
    const double maxSpan = tileExtent.width() * maxSpanRatio + epsX;

    QString bestKey;
    double  bestSpan = std::numeric_limits<double>::max();
    for (auto it = m_tileIndex.cbegin(); it != m_tileIndex.cend(); ++it)
    {
        const MapExtent &ce = it.value().first;
        const double span = ce.width();
        if (span >= bestSpan || span > maxSpan)
            continue;
        if (ce.xMin() > tileExtent.xMin() + epsX ||
            ce.xMax() < tileExtent.xMax() - epsX ||
            ce.yMin() > tileExtent.yMin() + epsY ||
            ce.yMax() < tileExtent.yMax() - epsY)
            continue;
        bestKey  = it.key();
        bestSpan = span;
    }

    while (!bestKey.isEmpty())
    {
        const CachedTile *anc = m_tileCache.object(bestKey);
        if (!anc || anc->img.isNull())
        {
            // Evicted since it was indexed — prune and give up (a second
            // full search is not worth it; the tile stays blank this frame).
            m_tileIndex.remove(bestKey);
            break;
        }
        // Extent-derived sub-rect in the ancestor image. Image row 0 is the
        // ancestor extent's yMax (north) edge.
        const double w = anc->img.width();
        const double h = anc->img.height();
        const MapExtent &ae = anc->extent;
        if (ae.width() <= 0.0 || ae.height() <= 0.0)
            break;
        out.img = anc->img;
        out.src = QRectF(
            (tileExtent.xMin() - ae.xMin()) / ae.width()  * w,
            (ae.yMax() - tileExtent.yMax()) / ae.height() * h,
            tileExtent.width()  / ae.width()  * w,
            tileExtent.height() / ae.height() * h);
        out.exact = false;
        return true;
    }
    return false;
}
