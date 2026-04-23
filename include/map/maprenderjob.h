/*!
 * \file   maprenderjob.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Background render job that composites raster layers into a QImage.
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
 * Cancellation:  Call cancel() to skip remaining layers; the partially-rendered
 * image is still delivered via finished() so the canvas always has a buffer.
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
     */
    MapRenderJob(const QList<OpenSWMMVisLayer *> &layers,
                 const MapExtent &extent,
                 const QSize &imageSize,
                 SpatialReferenceSystem *srs,
                 const QColor &bgColor,
                 QObject *parent = nullptr);

    /*!
     * \brief Destructor — non-blocking.
     * \details Worker thread is independent of the job's lifetime: it owns
     * its own copies of all rendering state (snapshot taken in start()) and
     * a shared cancel flag. The job can be deleted any time without freezing
     * the GUI thread or risking use-after-free in the worker.
     */
    ~MapRenderJob() override = default;

    /*!
     * \brief Queues the render on a QtConcurrent thread.
     * \details finished() is emitted when done. Calling start() a second
     *          time before finished() fires has no effect.
     */
    void start();

    /*!
     * \brief Signals the worker to skip remaining layers.
     */
    void cancel();

    /*!
     * \brief Returns true after cancel() has been called.
     */
    [[nodiscard]] bool isCancelled() const { return m_cancelled->load(); }

signals:

    /*!
     * \brief Emitted on the GUI thread when rendering is complete (or cancelled).
     * \param result   The composited raster image, sized to imageSize.
     */
    void finished(QImage result);

private:
    void run();

    QList<OpenSWMMVisLayer *>    m_layers;
    MapExtent                m_extent;
    QSize                    m_imageSize;
    SpatialReferenceSystem  *m_srs;
    QColor                   m_bgColor;
    bool                     m_started   { false };
    // Shared with the worker lambda. Survives the job's destruction so the
    // worker can keep checking it after `delete m_renderJob` from the GUI.
    std::shared_ptr<std::atomic<bool>> m_cancelled
        { std::make_shared<std::atomic<bool>>(false) };
};

#endif // MAPRENDERJOB_H
