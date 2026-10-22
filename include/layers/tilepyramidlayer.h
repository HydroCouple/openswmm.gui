/**
 * @file      tilepyramidlayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Shared tile-pyramid machinery for tiled raster layers
 *         (GISRasterLayer, WMTSLayer): a thread-safe tile cache that stores
 *         each tile with its geographic extent, plus a grid-agnostic
 *         coarser-tile fallback so a missing tile paints as a blurry
 *         stand-in cut from a cached coarser tile instead of a blank hole.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /**
     * @brief Create an empty mutex-protected, 256-tile unit-cost LRU cache.
     * @param[in] parent Optional workspace QObject owner. Base construction creates no tile producer or worker.
     */
    explicit TilePyramidLayer(OpenSWMMVisWorkspace *parent = nullptr);
    /**
     * @brief Release cached image values and metadata; callers must stop concurrent cache operations before destruction.
     */
    ~TilePyramidLayer() override;

protected:
    /*! \brief A resolved per-tile draw: paint \c src (a sub-rect of \c img,
     *         in image pixels with row 0 = the tile extent's yMax edge) into
     *         the tile's destination footprint. \c exact is false when the
     *         image is a coarser-tile stand-in. */
    struct TileDraw {
        QImage img;
        QRectF src;
        bool exact = false;
    };

    /*! \brief Insert a produced tile (no-op for null images). \p extent is
     *         the tile's geographic extent in the subclass's tile CRS;
     *         \p level is the subclass's pyramid-level tag (metadata only —
     *         the fallback search is extent-based).
     * @param[in] key Exact cache identity, replacing any prior entry. Empty keys can be cached but cannot serve as fallback candidates.
     * @param[in] img Image copied by Qt implicit sharing; null is ignored.
     * @param[in] extent Finite positive tile bounds in the common subclass tile CRS, with image row zero at yMax. Not validated.
     * @param[in] level Opaque level metadata; not used to choose ancestors.
     * @note Locks the cache, inserts with unit cost and lazily prunes stale extent entries. Capacity eviction/insertion failure can leave only a side-index record until later pruning. Emits no signals.
     */
    void cacheTile(const QString &key, const QImage &img, const MapExtent &extent, int level);

    /*! \brief True when \p key holds a non-null cached tile. Does not
     *         promote the entry in the LRU.
     * @param[in] key Exact cache identity.
     * @return Whether a live entry exists under the internal mutex, without promoting its LRU position.
     */
    [[nodiscard]] bool isTileCached(const QString &key) const;

    /*! \brief The cached image for \p key, or a null QImage.
     * @param[in] key Exact cache identity.
     * @return Implicitly shared owned image value or null on miss. Lookup holds the mutex and promotes a hit in LRU order; returned pixels remain usable after eviction.
     */
    [[nodiscard]] QImage cachedTileImage(const QString &key) const;

    /*! \brief Drop every cached tile (style / CRS / source change).
     * @note Locks and clears both cache and extent index without notification; existing returned image copies remain valid.
     */
    void clearTiles();

    /**
     * @param[in] maxTiles Nonnegative unit-cost entry budget passed directly to QCache; callers should avoid negative values.
     * @note Locks cache and can evict entries immediately. Side index is pruned lazily; no repaint or cache-changed signal is emitted.
     */
    void setTileCacheCapacity(int maxTiles);
    /**
     * @return Configured QCache maximum cost under lock, measured in tiles because each insertion costs one. Not the current occupancy or byte count.
     */
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
     * @param[in] key Exact target cache identity, checked before extent validation.
     * @param[in] tileExtent Finite positive target bounds in the same CRS as all cached extents, ordinary positive Y.
     * @param[in] maxSpanRatio Maximum ancestor-width/target-width ratio, normally at least one; not applied to exact hits and does not independently constrain height.
     * @param[out] out Assigned only on success: owned shared image, pixel source rectangle and exact/fallback flag. Unchanged on failure.
     * @note Holds one mutex through lookup. Exact hits promote LRU. Fallback picks the containing indexed tile with smallest width, allowing relative 1e-6 edge tolerance; ties follow hash order. A stale best entry is removed and returns false without searching another candidate. Source rectangle may extend slightly outside pixels within tolerance. No IO, image resampling or notifications occur.
     */
    [[nodiscard]] bool resolveTileForDraw(const QString &key, const MapExtent &tileExtent,
                                          double maxSpanRatio, TileDraw &out) const;

private:
    struct CachedTile {
        QImage img;
        MapExtent extent;
        int level = 0;
    };

    mutable QMutex m_tileMutex;
    QCache<QString, CachedTile> m_tileCache{256};
    // extent/level side-index of the cache contents, iterated by the fallback
    // search (QCache itself can't be walked without promoting entries).
    // QCache evicts silently, so the index is pruned lazily on lookup misses
    // and swept whenever it outgrows the cache capacity.
    mutable QHash<QString, QPair<MapExtent, int>> m_tileIndex;

    /**
     * @brief Drop extent-index keys whose image cache entries were evicted.
     * @note Caller must already hold m_tileMutex. Contains checks do not promote LRU; this method does not acquire a second lock or remove live cache entries.
     */
    void pruneIndexLocked();
};

#endif // TILEPYRAMIDLAYER_H
