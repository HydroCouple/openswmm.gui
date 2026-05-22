/*!
 * \file   swmmlayerglrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */
#include "map/swmmlayerglrenderer.h"

#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
#include "map/mapextent.h"

#include <QColor>
#include <QDebug>
#include <QElapsedTimer>
#include <QImage>
#include <QPainterPath>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QPen>
#include <QSurfaceFormat>
#include <QTransform>
#include <QVector>

#include <array>
#include <algorithm>
#include <vector>

SWMMLayerGLRenderer::SWMMLayerGLRenderer(SWMMModelLayer *layer,
                                          QObject        *parent)
    : QObject(parent)
    , m_layer(layer)
{
}

SWMMLayerGLRenderer::~SWMMLayerGLRenderer()
{
    cleanup();
}

void SWMMLayerGLRenderer::cleanup()
{
    if (m_ctx && m_surface && m_ctx->makeCurrent(m_surface)) {
        delete m_fbo;     m_fbo     = nullptr;
        m_ctx->doneCurrent();
    }
    delete m_ctx;     m_ctx     = nullptr;
    delete m_surface; m_surface = nullptr;
    m_initialized = false;
}

bool SWMMLayerGLRenderer::ensureInit()
{
    if (m_initialized) return true;
    if (m_initFailed)  return false;

    m_surface = new QOffscreenSurface();
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    // GL 4.1 Core Profile. The B.1 spike used the default GL 2.1
    // compatibility profile — that's what Apple's GL→Metal translation
    // gives by default, but it's also where QPainter fills (polygons,
    // ellipses, drawPath, fillRect) silently break on macOS Apple
    // Silicon. Going to 4.1 Core uses Apple's modern shader pipeline
    // and resolves the polygon-fill / pen-color bugs that Phase B.4
    // hit. This is the highest GL Apple ships.
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    m_surface->setFormat(fmt);
    m_surface->create();
    if (!m_surface->isValid()) {
        qWarning() << "[SWMMLayerGLRenderer] offscreen surface invalid";
        m_initFailed = true;
        return false;
    }

    m_ctx = new QOpenGLContext();
    m_ctx->setFormat(fmt);
    if (!m_ctx->create()) {
        qWarning() << "[SWMMLayerGLRenderer] QOpenGLContext::create failed";
        m_initFailed = true;
        return false;
    }
    if (!m_ctx->makeCurrent(m_surface)) {
        qWarning() << "[SWMMLayerGLRenderer] makeCurrent failed";
        m_initFailed = true;
        return false;
    }

    qInfo() << "[SWMMLayerGLRenderer] init OK · vendor="
            << reinterpret_cast<const char *>(
                   m_ctx->functions()->glGetString(GL_VENDOR))
            << " renderer="
            << reinterpret_cast<const char *>(
                   m_ctx->functions()->glGetString(GL_RENDERER))
            << " version="
            << reinterpret_cast<const char *>(
                   m_ctx->functions()->glGetString(GL_VERSION));

    m_ctx->doneCurrent();
    m_initialized = true;
    return true;
}

bool SWMMLayerGLRenderer::ensureFbo(QSize size)
{
    if (size.isEmpty()) return false;
    if (m_fbo && m_fboSize == size) return true;

    delete m_fbo;
    QOpenGLFramebufferObjectFormat ff;
    // CombinedDepthStencil is REQUIRED for QPainter polygon fills on a
    // GL paint engine. Qt uses the stencil buffer to track polygon
    // winding/coverage during tessellation; with NoAttachment, every
    // drawPolygon / drawPath / drawEllipse renders as outline only —
    // the symptom that broke subcatchment fills and the yellow-tint
    // selection highlight on glyphs. Stroke primitives (drawLines)
    // don't need stencil, which is why those worked from the start.
    ff.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    ff.setInternalTextureFormat(GL_RGBA8);
    // 4x MSAA — hardware edge AA. QOpenGLFramebufferObject::toImage()
    // handles the multisample resolve on readback.
    ff.setSamples(4);
    m_fbo = new QOpenGLFramebufferObject(size, ff);
    if (!m_fbo->isValid()) {
        qWarning() << "[SWMMLayerGLRenderer] FBO invalid at" << size
                   << "(MSAA samples=4 + CombinedDepthStencil)";
        // Fall back to non-MSAA, keep the stencil — fills matter more
        // than edge AA.
        delete m_fbo;
        ff.setSamples(0);
        m_fbo = new QOpenGLFramebufferObject(size, ff);
        if (!m_fbo->isValid()) {
            qWarning() << "[SWMMLayerGLRenderer] non-MSAA FBO also invalid";
            delete m_fbo; m_fbo = nullptr;
            return false;
        }
    }
    m_fboSize = size;
    return true;
}

QImage SWMMLayerGLRenderer::render(const MapExtent &extent, QSize viewportSize)
{
    if (!m_layer || !m_layer->isVisible() || viewportSize.isEmpty()
        || !extent.isValid())
        return {};
    QElapsedTimer t_total; t_total.start();
    if (!ensureInit()) return {};
    if (!m_ctx->makeCurrent(m_surface)) {
        qWarning() << "[SWMMLayerGLRenderer] makeCurrent failed in render";
        return {};
    }
    if (!ensureFbo(viewportSize)) {
        m_ctx->doneCurrent();
        return {};
    }
    const qint64 t_setup = t_total.elapsed();

    // ----- Phase B.3 (Path 2): QPainter on a QOpenGLPaintDevice bound to
    // the FBO. Qt's GL paint engine handles tessellation, batching, and
    // upload — we just call `drawLines` with the same QVector<QLineF> the
    // CPU path uses. No raw shaders, no VBO, no projection matrix.
    //
    // The transform mirrors what QGraphicsScene::render(painter, target,
    // sourceRect) applies in the existing CPU path: map scene-space
    // [xMin, xMax] × [-yMax, -yMin] → pixel-space [0, w] × [0, h].
    // The layer's m_linkSceneFlat already stores Y-flipped scene coords,
    // so the transform is a straight scale + translate — no per-vertex
    // math.

    // GL renders the whole layer — lines, subcatchment polygons,
    // node glyphs, gage glyphs. Polygon / shape FILLS still don't
    // render through Qt's GL paint engine on macOS Apple Silicon
    // (subcatchments, outfall triangles, storage squares, divider
    // diamonds, gage diamonds will appear as OUTLINES only) — the
    // user has accepted this trade-off in favour of the per-frame
    // perf win, and we'll revisit the fill issue later. Junctions
    // are drawn via `drawPoints` with a wide cosmetic pen so they
    // appear as filled dots (no brush needed). See
    // ~/.claude/.../memory/feedback_qt_gl_fill_limitation.md.
    const std::vector<double>   &flat    = m_layer->m_linkSceneFlat;
    const std::vector<uint32_t> &offsets = m_layer->m_linkVertexOffset;
    const std::vector<uint32_t> &counts  = m_layer->m_linkVertexCount;
    const auto &linkHid  = m_layer->m_linkHiddenFlag;
    const auto &linkSel  = m_layer->m_linkSelectedFlag;
    const auto &nodeHid  = m_layer->m_nodeHiddenFlag;
    const auto &nodeSel  = m_layer->m_nodeSelectedFlag;
    const auto &catchHid = m_layer->m_catchHiddenFlag;
    const auto &catchSel = m_layer->m_catchSelectedFlag;
    const auto &gageHid  = m_layer->m_gageHiddenFlag;
    const auto &gageSel  = m_layer->m_gageSelectedFlag;

    // Per-class selection styling. The GL fallback can only honour
    // pen.color() and pen.widthF() (it draws outlines on QPainter over
    // a QOpenGLPaintDevice, with brush.color() backing the
    // wide-pen junction "fill" trick); cap/join/dash work the same as
    // the CPU compositor.
    auto *prefs = PreferencesManager::instance();
    const QPen   selLinkPen      = prefs->selectionPen  (QStringLiteral("link"));
    const QPen   selSubcatchPen  = prefs->selectionPen  (QStringLiteral("subcatchment"));
    const QPen   selNodePen      = prefs->selectionPen  (QStringLiteral("node"));
    const QBrush selNodeFill     = prefs->selectionBrush(QStringLiteral("node"));
    const QPen   selGagePen      = prefs->selectionPen  (QStringLiteral("gage"));

    m_fbo->bind();
    {
        QOpenGLPaintDevice device(m_fbo->size());
        QPainter painter(&device);

        // Clear-as-source so the FBO starts fully transparent. Without
        // this, the previous frame's contents bleed through (m_fbo has
        // no clear-on-bind semantics).
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(0, 0, viewportSize.width(), viewportSize.height(),
                         Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

        painter.setRenderHint(QPainter::Antialiasing,           true);
        painter.setRenderHint(QPainter::TextAntialiasing,       true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform,  true);
        painter.setRenderHint(QPainter::LosslessImageRendering, true);

        // Same transform shape as MapCanvas::paintEvent's
        // m_scene->render(target, source) call — see the comment at the
        // call site in mapcanvas.cpp.
        const double sx = double(viewportSize.width())  / extent.width();
        const double sy = double(viewportSize.height()) / extent.height();
        QTransform t;
        t.scale(sx, sy);
        t.translate(-extent.xMin(), extent.yMax());
        painter.setTransform(t);

        const QRectF exposed(extent.xMin(), -extent.yMax(),
                             extent.width(), extent.height());
        const qreal invViewScale = (sx > 0.0) ? (1.0 / sx) : 1.0;

        // ----- Subcatchments (polygons, outline-only on this stack) ------
        if (m_layer->showSubcatchments())
        {
            const auto &cps    = m_layer->m_catchScenePts;
            const auto &cboxes = m_layer->m_catchSceneBBoxes;
            const auto &sym    = m_layer->subcatchmentSymbol();
            QPen pen(sym.outlineColor, sym.outlineWidth);
            pen.setCosmetic(true);
            painter.setBrush(Qt::NoBrush);
            for (int i = 0; i < cps.size(); ++i) {
                if (size_t(i) < catchHid.size() && catchHid[i]) continue;
                if (cps[i].size() < 3) continue;
                if (i < cboxes.size()
                    && !exposed.intersects(cboxes[i])) continue;
                const bool sel = size_t(i) < catchSel.size() && catchSel[i];
                pen.setColor(sel ? selSubcatchPen.color() : sym.outlineColor);
                pen.setWidthF(sel ? selSubcatchPen.widthF()
                                  : sym.outlineWidth);
                painter.setPen(pen);
                painter.drawPolygon(QPolygonF(cps[i]));
            }
        }

        // ----- Links (poly-lines) ----------------------------------------
        if (m_layer->showLinks())
        {
            // Pull the full pen from PreferencesManager so cap/join/
            // style edits from the Rendering page are honoured by the
            // GL fallback path too. Outlets (case 4) now use their own
            // pen instead of falling back to the conduit symbol.
            auto linkPenForType = [](int linkType) {
                auto *prefs = PreferencesManager::instance();
                switch (linkType) {
                case 1:  return prefs->linkPen(QStringLiteral("pump"));
                case 2:  return prefs->linkPen(QStringLiteral("orifice"));
                case 3:  return prefs->linkPen(QStringLiteral("weir"));
                case 4:  return prefs->linkPen(QStringLiteral("outlet"));
                default: return prefs->linkPen(QStringLiteral("conduit"));
                }
            };

            const QVector<int> visible = m_layer->m_linkGrid.isEmpty()
                ? QVector<int>{}
                : m_layer->m_linkGrid.query(exposed);
            const bool useGrid = !m_layer->m_linkGrid.isEmpty();
            const int total = useGrid ? visible.size() : int(counts.size());

            std::array<QVector<QLineF>, 5> segsByType;
            std::array<QVector<QLineF>, 5> selSegsByType;
            for (int k = 0; k < total; ++k) {
                const int i = useGrid ? visible[k] : k;
                if (i < 0 || size_t(i) >= counts.size()) continue;
                if (size_t(i) < linkHid.size() && linkHid[i]) continue;
                const uint32_t cnt = counts[i];
                if (cnt < 2) continue;
                const uint32_t off = offsets[i];
                const double *p = flat.data() + size_t(off) * 2;
                const int type = (m_layer->m_links[i].linkType >= 0
                               && m_layer->m_links[i].linkType < 5)
                               ? m_layer->m_links[i].linkType : 0;
                const bool sel = size_t(i) < linkSel.size() && linkSel[i];
                auto &target = sel ? selSegsByType[size_t(type)]
                                   : segsByType[size_t(type)];
                for (uint32_t j = 1; j < cnt; ++j) {
                    target.emplace_back(QPointF(p[(j-1)*2], p[(j-1)*2+1]),
                                        QPointF(p[ j   *2], p[ j   *2+1]));
                }
            }

            for (int t = 0; t < 5; ++t) {
                if (segsByType[size_t(t)].isEmpty()) continue;
                QPen pen = linkPenForType(t);
                pen.setCosmetic(true);
                painter.setPen(pen);
                painter.drawLines(segsByType[size_t(t)]);
            }
            for (int t = 0; t < 5; ++t) {
                if (selSegsByType[size_t(t)].isEmpty()) continue;
                QPen hi = linkPenForType(t);
                hi.setColor(selLinkPen.color());
                // ADDITIVE width: selection pen width stacks on top of
                // the base link pen so the halo is always visible.
                hi.setWidthF(hi.widthF() + selLinkPen.widthF());
                if (selLinkPen.style() != Qt::SolidLine)
                    hi.setStyle(selLinkPen.style());
                hi.setCosmetic(true);
                painter.setPen(hi);
                painter.drawLines(selSegsByType[size_t(t)]);
            }
        }

        // Subcatchments render on the CPU compositor — large polygons
        // with mandatory fills.

        // ----- Nodes (per-type glyph buckets) ----------------------------
        if (m_layer->showNodes())
        {
            // Bucket points by node type. Junctions get drawPoints
            // (filled appearance via wide pen). Other types use shape
            // outlines (drawPolygon / drawRect) — the GL paint engine
            // drops their fills, but the silhouette still reads.
            QVector<QPointF> juncBase, juncSel;
            QVector<QPointF> outBase,  outSel;
            QVector<QPointF> stoBase,  stoSel;
            QVector<QPointF> divBase,  divSel;
            const double haloScene = 16.0 * invViewScale;
            const auto &nps   = m_layer->m_nodeScenePts;
            const auto &nodes = m_layer->m_nodes;
            for (int i = 0; i < nodes.size(); ++i) {
                if (size_t(i) < nodeHid.size() && nodeHid[i]) continue;
                if (i >= nps.size()) continue;
                const QPointF &sp = nps[i];
                if (!exposed.contains(sp)) {
                    QRectF e = exposed.adjusted(-haloScene, -haloScene,
                                                 haloScene,  haloScene);
                    if (!e.contains(sp)) continue;
                }
                const int nt = (nodes[i].nodeType >= 0
                                && nodes[i].nodeType < 4)
                                ? nodes[i].nodeType : 0;
                const bool sel = size_t(i) < nodeSel.size() && nodeSel[i];
                switch (nt) {
                case 0: (sel ? juncSel : juncBase).append(sp); break;
                case 1: (sel ? outSel  : outBase ).append(sp); break;
                case 2: (sel ? stoSel  : stoBase ).append(sp); break;
                case 3: (sel ? divSel  : divBase ).append(sp); break;
                }
            }

            // Junctions — drawEllipse with a *thick* cosmetic pen.
            // Apple's GL Core profile deprecates `glPointSize`, so
            // `drawPoints` with a wide pen renders inconsistently
            // ("some junctions render, others don't"). A drawEllipse
            // where the pen thickness matches the diameter produces a
            // self-overlapping outline that visually fills the circle
            // — same look as drawPoint, reliable on Apple's GL stack
            // because it goes through QPainter's stroke path which
            // works for every junction.
            const auto &jSym = m_layer->junctionSymbol();
            auto drawJunctions = [&](const QVector<QPointF> &pts,
                                     const QColor &color,
                                     double diameterPx) {
                if (pts.isEmpty()) return;
                QPen pen(color, diameterPx);
                pen.setCapStyle(Qt::RoundCap);
                pen.setCosmetic(true);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                // Tiny ellipse: radius is small so the fat pen fills
                // the visual circle. Cosmetic pen means the thickness
                // is in pixels, the ellipse position is in scene
                // space; the painter transform handles the projection.
                const double r = diameterPx * 0.05 * invViewScale;
                for (const QPointF &c : pts)
                    painter.drawEllipse(c, r, r);
            };
            drawJunctions(juncBase, jSym.fillColor, jSym.size);
            // Selected junctions: the wide-pen trick fills the glyph,
            // so the selection brush colour drives the visible fill and
            // the pen width adds to the base diameter.
            drawJunctions(juncSel,  selNodeFill.color(),
                          jSym.size + selNodePen.widthF());

            // Outline-only glyphs for the rest. Shape-distinct enough
            // at typical sizes; selection becomes a yellow outline.
            auto drawShapeOutlines = [&](const QVector<QPointF> &pts,
                                         const SWMMElementSymbol &sym,
                                         int nodeType,
                                         const QColor &outline,
                                         double penAdj) {
                if (pts.isEmpty()) return;
                const double r = (sym.size * 0.5) * invViewScale;
                QPen pen(outline, sym.outlineWidth + penAdj);
                pen.setCosmetic(true);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                for (const QPointF &c : pts) {
                    switch (nodeType) {
                    case 1: { // outfall — triangle
                        QPolygonF tri;
                        tri << QPointF(c.x(),     c.y() - r)
                            << QPointF(c.x() - r, c.y() + r * 0.8)
                            << QPointF(c.x() + r, c.y() + r * 0.8);
                        painter.drawPolygon(tri);
                        break;
                    }
                    case 2: // storage — square
                        painter.drawRect(QRectF(c.x() - r, c.y() - r,
                                                2 * r, 2 * r));
                        break;
                    case 3: { // divider — diamond
                        QPolygonF dia;
                        dia << QPointF(c.x(),     c.y() - r)
                            << QPointF(c.x() + r, c.y())
                            << QPointF(c.x(),     c.y() + r)
                            << QPointF(c.x() - r, c.y());
                        painter.drawPolygon(dia);
                        break;
                    }
                    }
                }
            };
            drawShapeOutlines(outBase, m_layer->outfallSymbol(), 1,
                              m_layer->outfallSymbol().outlineColor, 0.0);
            drawShapeOutlines(outSel,  m_layer->outfallSymbol(), 1,
                              selNodePen.color(), selNodePen.widthF());
            drawShapeOutlines(stoBase, m_layer->storageSymbol(), 2,
                              m_layer->storageSymbol().outlineColor, 0.0);
            drawShapeOutlines(stoSel,  m_layer->storageSymbol(), 2,
                              selNodePen.color(), selNodePen.widthF());
            drawShapeOutlines(divBase, m_layer->dividerSymbol(), 3,
                              m_layer->dividerSymbol().outlineColor, 0.0);
            drawShapeOutlines(divSel,  m_layer->dividerSymbol(), 3,
                              selNodePen.color(), selNodePen.widthF());
        }

        // ----- Rain gages (diamond outlines) -----------------------------
        if (m_layer->showRainGages() && !m_layer->m_gages.isEmpty())
        {
            const auto &sym = m_layer->rainGageSymbol();
            const double r         = (sym.size * 0.5) * invViewScale;
            const double haloScene = 16.0 * invViewScale;
            QVector<QPointF> basePts, selPts;
            const auto &gps   = m_layer->m_gageScenePts;
            const auto &gages = m_layer->m_gages;
            for (int i = 0; i < gages.size(); ++i) {
                if (size_t(i) < gageHid.size() && gageHid[i]) continue;
                if (i >= gps.size()) continue;
                const QPointF &sp = gps[i];
                if (!exposed.contains(sp)) {
                    QRectF e = exposed.adjusted(-haloScene, -haloScene,
                                                 haloScene,  haloScene);
                    if (!e.contains(sp)) continue;
                }
                const bool sel = size_t(i) < gageSel.size() && gageSel[i];
                (sel ? selPts : basePts).append(sp);
            }
            auto drawDiamond = [&](const QVector<QPointF> &pts,
                                   const QColor &outline, double penAdj) {
                if (pts.isEmpty()) return;
                QPen pen(outline, sym.outlineWidth + penAdj);
                pen.setCosmetic(true);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                for (const QPointF &c : pts) {
                    QPolygonF dia;
                    dia << QPointF(c.x(),     c.y() - r)
                        << QPointF(c.x() + r, c.y())
                        << QPointF(c.x(),     c.y() + r)
                        << QPointF(c.x() - r, c.y());
                    painter.drawPolygon(dia);
                }
            };
            drawDiamond(basePts, sym.outlineColor, 0.0);
            drawDiamond(selPts,  selGagePen.color(), selGagePen.widthF());
        }

        painter.end();
    }

    const qint64 t_paint = t_total.elapsed() - t_setup;

    QImage out = m_fbo->toImage();
    m_fbo->release();
    m_ctx->doneCurrent();
    const qint64 t_readback = t_total.elapsed() - t_setup - t_paint;

    // Force premultiplied ARGB32 so QPainter::drawImage on
    // m_frameBuffer (also Premultiplied) is a fast, format-matched
    // blit with no per-pixel conversion.
    if (out.format() != QImage::Format_ARGB32_Premultiplied)
        out.convertTo(QImage::Format_ARGB32_Premultiplied);

    qDebug().noquote() << "[SWMMLayerGLRenderer::render] setup_ms=" << t_setup
                       << " paint_ms="    << t_paint
                       << " readback_ms=" << t_readback
                       << " total_ms="    << t_total.elapsed()
                       << " linkSel_set=" << std::count(linkSel.begin(), linkSel.end(), uint8_t(1))
                       << " nodeSel_set=" << std::count(nodeSel.begin(), nodeSel.end(), uint8_t(1));
    return out;
}
