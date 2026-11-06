/**
 * @file      maprenderjob.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Background render job that composites raster layers into a QImage.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPRENDERJOB_H
#define MAPRENDERJOB_H

#include "map/mapextent.h"

#include <QImage>
#include <QList>
#include <QObject>
#include <QSize>

#include <atomic>
#include <memory>

class OpenSWMMVisLayer;
class SpatialReferenceSystem;

/*!
 * \class MapRenderJob
 * \brief Renders raster layers into an off-screen QImage on a worker thread.
 *
 * \details This class implements the QGIS-style "map render job" pattern:
 *
 *  1. The MapCanvas creates a MapRenderJob with the current extent, viewport
 *     size, CRS, and the list of visible raster layers.
 *  2. start() dispatches the render to a QtConcurrent worker thread.
 *  3. Each raster layer's render() method is called with a QPainter backed by
 *     the target QImage.
 *  4. When all layers have painted, finished(QImage) is emitted on the GUI thread.
 *
 * Cancellation: Call cancel() to skip remaining layers; a cancelled worker
 * normally discards its image before queuing finished(). A queued callback can
 * still race cancellation.
 *
 * \note Layer render() implementations must be safe for concurrent read access.
 */
class MapRenderJob : public QObject
{
    Q_OBJECT

public:
    /*!
     * \brief Constructs a render job.
     * \param layers       Shallow copy of the raster-layer stack (bottom-to-top order).
     * \param extent       Geographic extent in canvas-CRS coordinates.
     * \param imageSize    Pixel dimensions of the output image.
     * \param srs          Canvas CRS (not owned; must outlive the job).
     * \param bgColor      Background fill colour for the buffer.
     * \param parent       Qt parent.
     * @param[in] devicePixelRatio Output device-pixel/logical-pixel ratio; nonpositive/NaN falls
     *   back1, positive infinity is not explicitly rejected.
     * @note Extent and imageSize copied; imageSize is LOGICAL pixels. Layer list and CRS are
     *   BORROWED raw pointers:they must remain alive and read-stable until dispatched worker
     *   finishes, even after job QObject deletion. Only value captures and cancel flag survive
     *   independently. Owning GUI thread starts job; layer render/getters must tolerate worker
     *   access.
     */
    MapRenderJob(const QList<OpenSWMMVisLayer *> &layers, const MapExtent &extent,
                 const QSize &imageSize, qreal devicePixelRatio, SpatialReferenceSystem *srs,
                 const QColor &bgColor, QObject *parent = nullptr);

    /*!
     * \brief Destructor — non-blocking.
     * \details Worker thread is independent of the job's lifetime: it owns
     * copies of value rendering state plus borrowed layer/CRS pointers and
     * a shared cancel flag. The job can be deleted any time without freezing
     * the GUI thread; borrowed layer/CRS lifetime still requires caller coordination.
     * @note Default destructor does not cancel or wait. A running worker keeps raw layer/CRS
     *   pointers and can continue reads; caller must preserve those objects until completion
     *   independently of job lifetime. Queued completion is suppressed if job QPointer is null.
     */
    ~MapRenderJob() override = default;

    /*!
     * \brief Queues the render on a QtConcurrent thread.
     * \details finished() is emitted when done. Calling start() a second
     *          time before finished() fires has no effect.
     * @note One-shot for entire lifetime:second call ignored even after finish. Snapshots value
     *   state/raw pointers and dispatches global QtConcurrent worker. Builds premultiplied ARGB
     *   backing at round(logicalSize*dpr), sets DPR, fills background, then visible raster layers
     *   render in list order with layer opacity.
     * @note Cooperative cancel checked between layers and before queued publication; cancelled work
     *   is DISCARDED rather than emitting partial image. Cancel after last check can race queued
     *   delivery and still emit. No per-layer interruption, exception/failure signal,
     *   allocation-size validation or layer ownership capture. Completion posts through application
     *   GUI event queue and requires qApp lifetime.
     */
    void start();

    /*!
     * \brief Signals the worker to skip remaining layers.
     * @brief Atomically set shared cancellation flag, including before start; never resets,
     *   restarts or waits.
     * @note Stops future layer iterations after current render returns and normally suppresses
     *   finished delivery. Does not make borrowed layers/CRS safe to destroy immediately.
     */
    void cancel();

    /*!
     * \brief Returns true after cancel() has been called.
     * @return Atomic requested-cancellation state, not whether worker stopped or a finished
     *   callback can no longer arrive.
     */
    [[nodiscard]] bool isCancelled() const { return m_cancelled->load(); }

signals:

    /*!
     * \brief Emitted on the GUI thread when rendering is complete (or cancelled).
     * \param result   The composited raster image, sized to imageSize.
     * @note Result owns implicitly shared image data with DEVICE backing dimensions
     *   round(imageSize*dpr) and DPR metadata, so logical image size follows requested viewport.
     *   Normally emitted once on GUI thread for noncancelled job; queued race may deliver after
     *   cancel. Empty/allocation-failed image has no separate error signal.
     */
    void finished(QImage result);

private:
    /**
     * @brief Retained private compatibility no-op; all actual worker composition executes in
     *   start() capture lambda.
     */
    void run();

    QList<OpenSWMMVisLayer *> m_layers;
    MapExtent m_extent;
    QSize m_imageSize;
    qreal m_dpr{1.0};
    SpatialReferenceSystem *m_srs;
    QColor m_bgColor;
    bool m_started{false};
    // Shared with the worker lambda. Survives the job's destruction so the
    // worker can keep checking it after `delete m_renderJob` from the GUI.
    std::shared_ptr<std::atomic<bool>> m_cancelled{std::make_shared<std::atomic<bool>>(false)};
};

#endif // MAPRENDERJOB_H
