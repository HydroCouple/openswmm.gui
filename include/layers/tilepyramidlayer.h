/*!
 * \file   tilepyramidlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Shared tile-pyramid machinery for tiled raster layers
 *         (GISRasterLayer, WMTSLayer): a thread-safe tile cache that stores
 *         each tile with its geographic extent, plus a grid-agnostic
 *         coarser-tile fallback so a missing tile paints as a blurry
 *         stand-in cut from a cached coarser tile instead of a blank hole.
 */

#ifndef TILEPYRAMIDLAYER_H
#define TILEPYRAMIDLAYER_H

#include "layers/openswmmvislayer.h"

#include <QCache>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QPair>
#include <QRectF>
#include <QString>

/*!
 * \class TilePyramidLayer
 * \brief Base class owning the tile cache + parent-fallback compositor shared
 *        by tile-pyramid layers.
 * \details Subclasses keep their layer-specific tile *production* (GDAL warp
 *          for GISRasterLayer, HTTP GetTile for WMTSLayer) and *placement*
 *          (axis-aligned vs quadToQuad draw); the base resolves "what image /
 *          sub-rect should paint this tile" under one lock.
 *
 *          The fallback is extent-based and grid-agnostic: every cached tile
 *          carries its geographic extent (in the subclass's tile CRS), and a
 *          missing tile falls back to the smallest cached tile whose extent
 *          contains it — no assumption of power-of-two col/row halving, which
 *          is what lets the raster's dyadic grid and WMTS's arbitrary
 *          TileMatrixSets share one implementation.
 *
 *          Thread-safety: all protected cache operations lock an internal
 *          mutex, so a worker-thread render() may resolve tiles while the GUI
 *          thread inserts or clears them.
 */
class TilePyramidLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

public:
    explicit TilePyramidLayer(OpenSWMMVisWorkspace *parent = nullptr);
    ~TilePyramidLayer() override;

protected:
    /*! \brief A resolved per-tile draw: paint \c src (a sub-rect of \c img,
     *         in image pixels with row 0 = the tile extent's yMax edge) into
     *         the tile's destination footprint. \c exact is false when the
     *         image is a coarser-tile stand-in. */
    struct TileDraw
    {
        QImage img;
        QRectF src;
        bool   exact = false;
    };

    /*! \brief Insert a produced tile (no-op for null images). \p extent is
     *         the tile's geographic extent in the subclass's tile CRS;
     *         \p level is the subclass's pyramid-level tag (metadata only —
     *         the fallback search is extent-based). */
    void cacheTile(const QString &key, const QImage &img,
                   const MapExtent &extent, int level);

    /*! \brief True when \p key holds a non-null cached tile. Does not
     *         promote the entry in the LRU. */
    [[nodiscard]] bool isTileCached(const QString &key) const;

    /*! \brief The cached image for \p key, or a null QImage. */
    [[nodiscard]] QImage cachedTileImage(const QString &key) const;

    /*! \brief Drop every cached tile (style / CRS / source change). */
    void clearTiles();

    void setTileCacheCapacity(int maxTiles);
    [[nodiscard]] int tileCacheCapacity() const;

    /*!
     * \brief Resolve the image + source sub-rect that should paint the tile
     *        \p key with geographic extent \p tileExtent.
     * \details Returns the exact cached tile when present. Otherwise searches
     *          the cache for the smallest tile whose extent contains
     *          \p tileExtent (the nearest coarser stand-in) and returns the
     *          extent-derived sub-rect of its image. Candidates wider than
     *          \p maxSpanRatio × the tile's own span are rejected so the
     *          stand-in never degenerates below a few source pixels.
     * \returns true when something can be painted for this tile.
     */
    [[nodiscard]] bool resolveTileForDraw(const QString &key,
                                          const MapExtent &tileExtent,
                                          double maxSpanRatio,
                                          TileDraw &out) const;

private:
    struct CachedTile
    {
        QImage    img;
        MapExtent extent;
        int       level = 0;
    };

    mutable QMutex m_tileMutex;
    QCache<QString, CachedTile> m_tileCache{256};
    // extent/level side-index of the cache contents, iterated by the fallback
    // search (QCache itself can't be walked without promoting entries).
    // QCache evicts silently, so the index is pruned lazily on lookup misses
    // and swept whenever it outgrows the cache capacity.
    mutable QHash<QString, QPair<MapExtent, int>> m_tileIndex;

    void pruneIndexLocked();
};

#endif // TILEPYRAMIDLAYER_H
