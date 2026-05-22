/*!
 * \file   swmmlayerglrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 *
 * Phase B.2 / B.3 of docs/RENDERING_5M_PLAN.md — offscreen GL renderer
 * for `SWMMModelLayer`.
 *
 * Path (b) of the architecture decision: an offscreen `QOpenGLContext`
 * + `QOpenGLFramebufferObject`, a per-layer VBO holding the flat
 * scene-coords from `m_linkSceneFlat`, and a single
 * `glDrawArrays(GL_LINES, …)` per visible-link subrange. The output
 * FBO is read back as a `QImage` and handed to MapCanvas's existing
 * QImage compositor (m_frameBuffer). Keeps MapCanvas's paint pipeline
 * untouched while moving the heavy SWMM layer to GPU.
 *
 * Trade-off: ~3–5 ms `glReadPixels` per frame at 1080p (independent
 * of link count). Acceptable today; if it ever becomes the wall, we
 * graduate to a child `QOpenGLWidget` overlay (path (a) of the plan).
 */
#ifndef SWMMLAYERGLRENDERER_H
#define SWMMLAYERGLRENDERER_H

#include <QImage>
#include <QObject>
#include <QSize>

class SWMMModelLayer;
class MapExtent;
class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;

class SWMMLayerGLRenderer : public QObject
{
    Q_OBJECT

public:
    explicit SWMMLayerGLRenderer(SWMMModelLayer *layer,
                                 QObject       *parent = nullptr);
    ~SWMMLayerGLRenderer() override;

    /*!
     * \brief Render the layer at \p extent into an offscreen FBO of
     *        \p viewportSize pixels and return the result as a
     *        premultiplied-ARGB32 QImage that can be blitted onto
     *        m_frameBuffer with `QPainter::drawImage`.
     *
     * Returns a null QImage if GL init fails or the layer has nothing
     * to draw. Lazy-init: the first call creates the GL context,
     * surface, shader program, and VBO; subsequent calls reuse them.
     */
    QImage render(const MapExtent &extent, QSize viewportSize);

private:
    bool ensureInit();
    bool ensureFbo(QSize size);
    void cleanup();

    SWMMModelLayer *m_layer = nullptr;

    bool m_initialized = false;
    bool m_initFailed  = false;

    QOpenGLContext           *m_ctx     = nullptr;
    QOffscreenSurface        *m_surface = nullptr;
    QOpenGLFramebufferObject *m_fbo     = nullptr;
    QSize                     m_fboSize;
};

#endif // SWMMLAYERGLRENDERER_H
