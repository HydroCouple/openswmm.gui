/*!
 * \file   maprenderjob.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Background render job implementation.
 */

#include "map/maprenderjob.h"
#include "layers/openswmmvislayer.h"
#include "render/renderperf.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MapRenderJob::MapRenderJob(const QList<OpenSWMMVisLayer *> &layers,
                           const MapExtent &extent,
                           const QSize &imageSize,
                           qreal devicePixelRatio,
                           SpatialReferenceSystem *srs,
                           const QColor &bgColor,
                           QObject *parent)
    : QObject(parent),
      m_layers(layers),
      m_extent(extent),
      m_imageSize(imageSize),
      m_dpr(devicePixelRatio > 0.0 ? devicePixelRatio : 1.0),
      m_srs(srs),
      m_bgColor(bgColor)
{
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void MapRenderJob::start()
{
    if (m_started)
        return;
    m_started = true;

    // Snapshot ALL state needed by the worker into the lambda capture so the
    // worker is independent of this object's lifetime. The job can be deleted
    // (via deleteLater) the instant cancel() is called — the worker will keep
    // running with its own copies and the shared cancel flag, then post the
    // result back via QPointer-guarded invokeMethod (which silently no-ops if
    // the canvas was destroyed).
    auto layers      = m_layers;
    auto extent      = m_extent;
    auto imageSize   = m_imageSize;
    auto dpr         = m_dpr;
    auto srs         = m_srs;
    auto bgColor     = m_bgColor;
    auto cancelFlag  = m_cancelled;
    QPointer<MapRenderJob> self(this);

    QtConcurrent::run([layers, extent, imageSize, dpr, srs, bgColor,
                       cancelFlag, self]() {
        // Allocate the device-pixel-sized backing image but tell Qt its
        // logical size is imageSize (via setDevicePixelRatio). Layer
        // render() then operates in logical coordinates exactly as before,
        // while float tile rects are rasterised at full device precision —
        // closes the sub-pixel seams visible at the boundaries of raster
        // basemap tiles on Retina displays.
        const QSize devSize(qRound(imageSize.width()  * dpr),
                            qRound(imageSize.height() * dpr));
        QImage img(devSize, QImage::Format_ARGB32_Premultiplied);
        img.setDevicePixelRatio(dpr);
        img.fill(bgColor);

        // Opt-in profiling (openswmm.render.perf) — isDebugEnabled() is a
        // relaxed atomic load, so the disabled path stays free.
        const bool perfOn = lcRenderPerf().isDebugEnabled();
        QElapsedTimer jobTimer;
        if (perfOn)
            jobTimer.start();
        int layersRendered = 0;

        {
            QPainter p(&img);
            p.setRenderHints(QPainter::Antialiasing
                             | QPainter::TextAntialiasing
                             | QPainter::SmoothPixmapTransform
                             | QPainter::LosslessImageRendering);

            for (OpenSWMMVisLayer *layer : std::as_const(layers))
            {
                if (cancelFlag->load())
                    break;
                if (!layer || !layer->isVisible() || !layer->isRasterLayer())
                    continue;

                QElapsedTimer layerTimer;
                if (perfOn)
                    layerTimer.start();
                p.save();
                p.setOpacity(layer->opacity());
                layer->render(&p, extent, imageSize, srs);
                p.restore();
                ++layersRendered;
                if (perfOn)
                    qCDebug(lcRenderPerf).noquote()
                        << QStringLiteral("[job.layer] %1 (%2) %3 ms")
                               .arg(layer->name(),
                                    QString::fromLatin1(
                                        layer->metaObject()->className()))
                               .arg(layerTimer.elapsed());
            }
        } // QPainter destroyed — img is finalised

        if (perfOn)
            qCDebug(lcRenderPerf).noquote()
                << QStringLiteral("[job.total] %1 ms layers=%2 devSize=%3x%4 "
                                  "cancelled=%5")
                       .arg(jobTimer.elapsed())
                       .arg(layersRendered)
                       .arg(devSize.width()).arg(devSize.height())
                       .arg(cancelFlag->load() ? 1 : 0);

        // Cancellation check before posting back: if the canvas already
        // discarded this job (and started a newer one), don't bother
        // delivering a stale buffer. Saves a redundant repaint.
        if (cancelFlag->load())
            return;

        // Post back to the job IF it still exists. QPointer goes null when
        // the job is destroyed; the lambda then no-ops safely.
        QImage result = std::move(img);
        QMetaObject::invokeMethod(qApp, [self, result]() {
            if (self) emit self->finished(result);
        }, Qt::QueuedConnection);
    });
}

void MapRenderJob::cancel()
{
    m_cancelled->store(true);
}

// ---------------------------------------------------------------------------
// Worker entry point — kept for backward compatibility with header.
// All worker logic now lives in the lambda inside start().
// ---------------------------------------------------------------------------

void MapRenderJob::run()
{
    // Intentionally empty — start() runs the work in a self-contained lambda.
}
