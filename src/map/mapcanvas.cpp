/*!
 * \file   mapcanvas.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  QGIS-style hybrid map canvas: raster buffer + QGraphicsView overlay.
 */

#include "map/mapcanvas.h"
#include "map/scalebarsettings.h"
#include "core/preferencesmanager.h"
#include "map/openswmmvisscene.h"
#include "map/openswmmvisgraphicsview.h"
#include "map/mapextent.h"
#include "map/mapundostack.h"
#include "map/maprenderjob.h"
#include "map/meshprofileoverlay.h"
#include "map/profilepathoverlay.h"
#include "map/tools/maptool.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/xyztilelayer.h"
#include "map/spatialreferencesystem.h"
#include "map/crsmanager.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "layers/swmm2dresultslayer.h"
#include "render/qsg2drenderstats.h"
#include "render/renderperf.h"

#include <QElapsedTimer>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QtMath>
#include <QApplication>
#include <QHelpEvent>
#include <QToolTip>
#include <QVariantMap>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include <ogr_spatialref.h>   // OGRCoordinateTransformation for fullExtent

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

MapCanvas::MapCanvas(QWidget *parent)
    : QWidget(parent),
      m_scene(new OpenSWMMVisScene(this)),
      m_undoStack(new MapUndoStack(this)),
      m_refreshTimer(new QTimer(this))
{
    // ---- Widget setup ----
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // Pinch-to-zoom (touchscreen + macOS / Windows Precision touchpad).
    // Routed through event() → gestureEvent() → zoomAroundCursor so
    // behaviour matches the existing wheel-zoom path and stays
    // tool-independent.
    grabGesture(Qt::PinchGesture);

    // Scene has no background — the MapCanvas raster buffer paints behind it.
    m_scene->setBackgroundBrush(Qt::NoBrush);

    // P2 — catch-all scene-dirty tracking: any item add/remove/move/visual
    // update marks the cached m_sceneBuffer stale so the next non-gesture
    // paint re-renders it. update() guarantees a repaint is scheduled even
    // when the change produced no other paint trigger.
    connect(m_scene, &QGraphicsScene::changed, this,
            [this](const QList<QRectF> &) { m_sceneDirty = true; update(); });
    connect(m_scene, &QGraphicsScene::selectionChanged, this,
            [this]() { m_sceneDirty = true; update(); });

    // ---- Overlay view: hidden child used for transform math + scene rendering ----
    // Kept hidden to suppress its own spontaneous paint events (we call render()
    // from paintEvent() ourselves).  QWidget::render() works on hidden widgets.
    m_overlayView = new OpenSWMMVisGraphicsView(m_scene, this);
    m_overlayView->setGeometry(rect());
    m_overlayView->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_overlayView->hide();

    // Transparent background so our raster buffer shows through when rendered
    m_overlayView->setBackgroundBrush(Qt::NoBrush);
    m_overlayView->viewport()->setAutoFillBackground(false);

    // Overlay view configuration
    m_overlayView->setFrameShape(QFrame::NoFrame);
    m_overlayView->setTransformationAnchor(QGraphicsView::NoAnchor);
    m_overlayView->setResizeAnchor(QGraphicsView::NoAnchor);
    m_overlayView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_overlayView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_overlayView->setCacheMode(QGraphicsView::CacheNone);
    m_overlayView->setDragMode(QGraphicsView::NoDrag);
    m_overlayView->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    m_overlayView->setRenderHints(QPainter::Antialiasing
                                   | QPainter::TextAntialiasing
                                   | QPainter::SmoothPixmapTransform);

    // Default Web Mercator canvas CRS (EPSG:3857) — matches CartoDB/OSM tile convention
    m_canvasSRS = SpatialReferenceSystem::fromAuthCode(QStringLiteral("EPSG"), 3857);
    m_ownsSRS   = true;

    // Default extent: whole world in Web Mercator (meters)
    m_extent = MapExtent(-20037508.34, -20037508.34, 20037508.34, 20037508.34);

    // Seed the scene rect generously so early panning never hits a boundary.
    m_scene->setSceneRect(-40075016.68, -40075016.68, 80150033.36, 80150033.36);

    // Refresh timer coalesces multiple refresh() calls. 50 ms is the
    // tuned debounce — short enough that a selection update feels
    // responsive, long enough to absorb sustained activity (pan
    // deltas, hover-driven cursor refreshes). Phase A.4 explored 0 ms
    // and found no measurable selection-paint reduction (the dominant
    // paint trigger is mouse-move events post-selection, not the
    // synchronous signal cascade) — net result was higher per-paint
    // cost and no fewer paints. Keep at 50 ms; the real win for paint
    // reduction comes with Phase B (GL) + Phase C (tile cache).
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(50);
    connect(m_refreshTimer, &QTimer::timeout, this, &MapCanvas::refreshLayerItems);

    applyExtentToOverlay();

    // ----- Phase B.RHI — QSG renderer infrastructure (currently inactive) ---
    // The QQuickWidget/Metal approach works on most platforms, but on macOS the
    // CAMetalLayer created for a native child QQuickWidget is always composited
    // opaquely on top of the parent's content by Core Animation — regardless of
    // setClearColor(Qt::transparent) or WA_TranslucentBackground — unless the
    // *window* itself opts into per-pixel alpha, which is not possible in an MDI
    // host without invasive window-flag changes.
    //
    // Consequence: the Metal layer covered the raster m_frameBuffer (basemap,
    // DTM, 2D mesh), and because m_glRenderingEnabled=true disabled the CPU
    // paint path in SWMMLayerItem::paint(), selection highlighting via yellow
    // brush also stopped working.
    //
    // Current state: m_glRenderingEnabled is set to false (see SWMMModelLayer
    // header), so all rendering goes through the QPainter / QGraphicsScene CPU
    // path.  The QQuickWidget is created but explicitly hidden so macOS never
    // promotes it to a native CAMetalLayer child.  The m_qsgRenderer pointer is
    // set for future use when a proper off-screen compositing path is available
    // (e.g. QQuickRenderControl + QOffscreenSurface → QImage → drawImage into
    // m_frameBuffer, bypassing the native-child compositing entirely).
    // QSG render widget is TOP-LEVEL + WA_DontShowOnScreen. As a
    // hidden child of MapCanvas the QQuickWindow's render loop is
    // gated by visibility — grabFramebuffer() returns blank pixels —
    // and the new per-kind CPU bypass (SWMMLayerItem skips kinds
    // QSG owns) then leaves the canvas empty of SWMM elements.
    // Top-level + WA_DontShowOnScreen keeps the FBO render pipeline
    // alive without ever showing a native window. Screen + DPR are
    // explicitly synced to MapCanvas in showEvent() so the grabbed
    // framebuffer composites at the same device resolution as the
    // basemap and other CPU layers (otherwise on Retina the overlay
    // renders at DPR=1 and appears half-resolution / offset).
    m_qsgWidget = new QQuickWidget(nullptr);
    m_qsgWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_qsgWidget->setAttribute(Qt::WA_DontShowOnScreen);
    // WA_DontShowOnScreen keeps it off screen but it is still a *logically
    // visible* top-level with WA_QuitOnClose (default true), so
    // QApplicationPrivate::shouldQuit() counts it as a primary window and
    // lastWindowClosed never fires after the main window closes — the app
    // lingers in the Dock with no windows. Opt it out of the quit check.
    m_qsgWidget->setAttribute(Qt::WA_QuitOnClose, false);
    m_qsgWidget->setClearColor(Qt::transparent);
    m_qsgWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    // Force MSAA on the QQuickWidget's offscreen FBO. main.cpp already
    // puts samples=4 on QSurfaceFormat::defaultFormat for the main
    // window, but QQuickWidget can construct its private FBO/RHI
    // target with samples=0 unless told otherwise — that's why the
    // SWMM overlay's lines/glyph silhouettes were aliased even with
    // MSAA "enabled" globally. setting it on the widget itself before
    // setSource() is the path that actually reaches the RHI render
    // target.
    {
        QSurfaceFormat fmt = m_qsgWidget->format();
        if (fmt.samples() < 4)
            fmt.setSamples(4);
        m_qsgWidget->setFormat(fmt);
    }
    m_qsgWidget->setSource(QUrl(QStringLiteral("qrc:/openswmm/qml/swmmlayer.qml")));
    if (m_qsgWidget->status() != QQuickWidget::Ready) {
        qWarning() << "[MapCanvas] QML status:" << m_qsgWidget->status();
        for (const QQmlError &err : m_qsgWidget->errors())
            qWarning() << "[MapCanvas]  QML error:" << err.toString();
    }
    // VS.8 — the QML root is now a plain Item stacking the 2D-results
    // renderer (below) and the 1D network renderer (above); locate both by
    // objectName.
    if (QQuickItem *qmlRoot = m_qsgWidget->rootObject()) {
        m_qsgRenderer = qmlRoot->findChild<SWMMLayerQSGRenderer *>(
            QStringLiteral("swmmRenderer"));
        m_qsg2DRenderer = qmlRoot->findChild<SWMM2DResultsQSGRenderer *>(
            QStringLiteral("results2dRenderer"));
    }
    if (!m_qsgRenderer)
        qWarning() << "[MapCanvas] failed to obtain SWMMLayerQSGRenderer "
                      "from the QML scene — got" << m_qsgWidget->rootObject();
    if (!m_qsg2DRenderer)
        qWarning() << "[MapCanvas] failed to obtain SWMM2DResultsQSGRenderer "
                      "from the QML scene";
    if (m_qsg2DRenderer) {
        // QSG-2D-1M Phase 7 — an async contour job finished: the offscreen
        // QSG widget now holds fresher bands/isolines than the cached
        // framebuffer, so force a regrab on the next paint.
        connect(m_qsg2DRenderer, &SWMM2DResultsQSGRenderer::contentReady,
                this, [this]() {
                    m_qsgFrameDirty = true;
                    update();
                });
    }
    m_qsgWidget->show();

    // Scale bar appearance settings — child QObject so it's cleaned up with the canvas.
    m_scaleBarSettings = new ScaleBarSettings(this);
    connect(m_scaleBarSettings, &ScaleBarSettings::changed, this, qOverload<>(&QWidget::update));

    // Seed from persisted preferences and live-update whenever they change.
    syncScaleBarFromPreferences();
    connect(PreferencesManager::instance(), &PreferencesManager::preferenceChanged,
            this, [this](const QString &group, const QString &key) {
                if (group == QLatin1String("Decorations"))
                    syncScaleBarFromPreferences();
                else if (group == QLatin1String("Rendering")
                         && key == QLatin1String("QsgEnabled"))
                    syncQsgRenderKindsFromPreferences();
            });

}

MapCanvas::~MapCanvas()
{
    cancelRenderJob();

    // The offscreen QSG widget is a top-level (parent = nullptr) so
    // QObject parent-child cleanup doesn't free it. Delete explicitly.
    if (m_qsgWidget)
        m_qsgWidget->deleteLater();

    if (m_ownsSRS)
        delete m_canvasSRS;
}

// ---------------------------------------------------------------------------
// Scene / overlay access
// ---------------------------------------------------------------------------

OpenSWMMVisScene        *MapCanvas::mapScene()    const { return m_scene; }
OpenSWMMVisGraphicsView *MapCanvas::overlayView() const { return m_overlayView; }

// ---------------------------------------------------------------------------
// CRS
// ---------------------------------------------------------------------------

SpatialReferenceSystem *MapCanvas::canvasSRS() const { return m_canvasSRS; }

void MapCanvas::applyCRSInternal(SpatialReferenceSystem *srs, bool ownsSRS)
{
    if (!srs || srs == m_canvasSRS)
        return;

    if (m_ownsSRS)
        delete m_canvasSRS;

    m_canvasSRS = srs;
    m_ownsSRS   = ownsSRS;

    // Discard the raster buffer so stale tiles from the old CRS aren't shown
    // while the new render job is fetching tiles for the new CRS.
    cancelRenderJob();
    m_mapBuffer = QImage();
    m_sceneDirty = true;   // P2 — scene geometry is CRS-dependent

    for (OpenSWMMVisLayer *layer : std::as_const(m_layers))
        layer->onCanvasCRSChanged(m_canvasSRS);

    emit canvasSRSChanged(m_canvasSRS);
    refresh();
}

void MapCanvas::setCanvasSRS(SpatialReferenceSystem *srs, bool ownsSRS)
{
    if (!srs || srs == m_canvasSRS)
        return;

    QString oldCode = m_canvasSRS ? m_canvasSRS->toAuthority() : QString();
    QString newCode = srs->toAuthority();

    applyCRSInternal(srs, ownsSRS);

    // Only record undo for standard authority-code CRSes; "Local" has no roundtrip.
    // applyCRSInternal is called first so the CRS is already applied before push()
    // calls redo() — ChangeCRSCommand::redo() uses applyCRSInternal directly to
    // avoid this recursion path.
    if (!oldCode.isEmpty() && !newCode.isEmpty()
        && oldCode != QStringLiteral("Local")
        && newCode != QStringLiteral("Local"))
    {
        m_undoStack->push(new ChangeCRSCommand(oldCode, newCode, this));
    }
}

bool MapCanvas::setCanvasSRSByCode(const QString &authName, int code)
{
    SpatialReferenceSystem *srs = SpatialReferenceSystem::fromAuthCode(authName, code);
    if (!srs)
        return false;

    setCanvasSRS(srs, /*ownsSRS=*/true);
    return true;
}

// ---------------------------------------------------------------------------
// Extent / view
// ---------------------------------------------------------------------------

MapExtent MapCanvas::extent() const { return m_extent; }

void MapCanvas::setExtent(const MapExtent &extent, bool pushUndo)
{
    MapExtent fitExtent = arCorrectedExtent(extent);

    if (fitExtent == m_extent)
        return;

    if (pushUndo)
        m_undoStack->push(new PanZoomCommand(m_extent, fitExtent, this));

    m_extent = fitExtent;
    applyExtentToOverlay();

    emit extentChanged(m_extent);
    emit scaleChanged(scale());

    if (!m_isPanning)
        refresh();
}

void MapCanvas::zoomToFullExtent()
{
    MapExtent fe = fullExtent();
    if (!fe.isValid())
        return;

    // Add a small margin so features at the edge (outermost nodes, link
    // endpoints, subcatchment outlines) are not visually clipped against the
    // canvas border. 8% on each side matches QGIS's default "Zoom to Layer".
    fe = fe.scaled(1.08);
    setExtent(fe);
}

void MapCanvas::zoomIn(double factor)
{
    setExtent(m_extent.scaled(1.0 / factor));
}

void MapCanvas::zoomOut(double factor)
{
    setExtent(m_extent.scaled(factor));
}

void MapCanvas::pan(double dx, double dy)
{
    setExtent(m_extent.panned(dx, dy));
}

double MapCanvas::scale() const
{
    return scaleDenominator();
}

double MapCanvas::scaleDenominator() const
{
    if (width() <= 0 || !m_extent.isValid())
        return 1.0;

    // metresPerPixel() already handles CRS unit conversion (projected vs.
    // geographic).  Scale denominator N = ground_metres_per_pixel * pixels_per_metre_on_screen.
    const double mpp = metresPerPixel();
    if (mpp <= 0.0)
        return 1.0;

    // Prefer the actual screen DPI; fall back to 96 if the widget is not yet
    // parented to a window (e.g. during construction).  Matches QGIS, which
    // queries QGuiApplication::primaryScreen()->logicalDotsPerInchX().
    double dpi = 96.0;
    if (const QWindow *w = window() ? window()->windowHandle() : nullptr) {
        if (const QScreen *s = w->screen())
            dpi = s->logicalDotsPerInchX();
    } else if (const QScreen *s = QGuiApplication::primaryScreen()) {
        dpi = s->logicalDotsPerInchX();
    }
    if (dpi <= 0.0) dpi = 96.0;

    constexpr double metresPerInch = 0.0254;
    const double metresPerScreenPixel = metresPerInch / dpi;
    return mpp / metresPerScreenPixel;
}

void MapCanvas::setScaleDenominator(double denom)
{
    if (width() <= 0 || height() <= 0 || !m_extent.isValid() || denom <= 0.0)
        return;

    // Invert scaleDenominator(): given target N, work out the required
    // metres-per-pixel, then convert back to map-units-per-pixel using
    // the same CRS rules metresPerPixel() applied in the forward direction.
    double dpi = 96.0;
    if (const QWindow *w = window() ? window()->windowHandle() : nullptr) {
        if (const QScreen *s = w->screen())
            dpi = s->logicalDotsPerInchX();
    } else if (const QScreen *s = QGuiApplication::primaryScreen()) {
        dpi = s->logicalDotsPerInchX();
    }
    if (dpi <= 0.0) dpi = 96.0;

    constexpr double metresPerInch = 0.0254;
    const double metresPerScreenPixel = metresPerInch / dpi;
    const double targetMpp            = denom * metresPerScreenPixel;   // metres / pixel on the ground

    // metres → map units (inverse of the conversion inside metresPerPixel()).
    double mapUnitsPerPixel = targetMpp;
    if (m_canvasSRS) {
        if (m_canvasSRS->isProjected()) {
            const double f = m_canvasSRS->linearUnitsToMetres();
            if (f > 0.0) mapUnitsPerPixel = targetMpp / f;
        } else if (m_canvasSRS->isGeographic()) {
            constexpr double R = 6378137.0;
            const double lat   = qDegreesToRadians(m_extent.centerY());
            const double k     = (M_PI / 180.0) * R * std::abs(std::cos(lat));
            if (k > 0.0) mapUnitsPerPixel = targetMpp / k;
        }
    }

    // Build a new extent of the same aspect ratio centred on the current centre.
    const double cx     = m_extent.centerX();
    const double cy     = m_extent.centerY();
    const double halfW  = mapUnitsPerPixel * width()  * 0.5;
    const double halfH  = mapUnitsPerPixel * height() * 0.5;

    setExtent(MapExtent(cx - halfW, cy - halfH, cx + halfW, cy + halfH));
}

// ---------------------------------------------------------------------------
// Layer management
// ---------------------------------------------------------------------------

const QList<OpenSWMMVisLayer *> &MapCanvas::layers() const { return m_layers; }

void MapCanvas::addLayer(OpenSWMMVisLayer *layer, bool pushUndo)
{
    insertLayer(m_layers.count(), layer, pushUndo);
}

void MapCanvas::insertLayer(int position, OpenSWMMVisLayer *layer, bool pushUndo)
{
    if (!layer)
        return;

    position = qBound(0, position, m_layers.count());

    // QUndoStack::push() invokes command->redo() automatically, and our
    // AddLayerCommand::redo() calls back into insertLayer(pos, layer, false).
    // So when pushUndo is true we ONLY push the command — performing the
    // insertion here too would add the layer twice. Caller-facing API stays
    // unchanged; this just routes the work through the command path.
    if (pushUndo)
    {
        m_undoStack->push(new AddLayerCommand(layer, position, this));
        return;
    }

    m_layers.insert(position, layer);

    connect(layer, &OpenSWMMVisLayer::repaintRequested,
            this, &MapCanvas::onLayerRepaintRequested);

    // On-the-fly reprojection: when a layer's OWN CRS changes (user
    // reassigned it because the .inp was missing a CRS, or picked a
    // new one in the layer-properties dialog), the layer→canvas
    // transform has to be rebuilt and the scene re-rendered. Reuse
    // the canvas-CRS change path — it already calls
    // `onCanvasCRSChanged` on the layer and triggers the right
    // transform + re-populate.
    //
    // Qt::UniqueConnection does NOT work with lambdas (Qt can't
    // compare functor identities). Disconnect any prior handler on
    // this specific layer/signal first, then connect exactly one
    // fresh lambda. Without this guard, re-inserting a layer or
    // rapid-fire insertLayer calls would stack dozens of handlers
    // and every srsChanged emission would trigger a populateScene
    // storm.
    QObject::disconnect(layer, &OpenSWMMVisLayer::srsChanged,
                        this, nullptr);
    connect(layer, &OpenSWMMVisLayer::srsChanged, this,
            [this, layer](SpatialReferenceSystem *) {
                if (!m_layers.contains(layer)) return;
                layer->onCanvasCRSChanged(m_canvasSRS);
                if (!layer->isRasterLayer())
                    layer->refreshScene(m_scene, m_extent, m_canvasSRS);
                // Re-fit the view to the layer's reprojected extent so
                // the user actually sees the content — previously the
                // canvas kept its OLD extent (which was in the layer's
                // old CRS), making the network appear "to have
                // vanished" even though it rendered at the new
                // coordinates.
                zoomToFullExtent();
                invalidate(Raster | Scene | Overlay,
                           QStringLiteral("layer-srs-changed"));
            });

    updateLayerZValues();

    // CRITICAL ORDER: build the layer→canvas transform FIRST, then
    // populate. populateScene consumes m_transform when it walks features
    // and emits scene items in canvas-CRS coords; if onCanvasCRSChanged
    // ran AFTER populateScene, the first frame would render raw layer
    // coords (no transform) and the layer would appear off-screen — the
    // shapefile/raster "doesn't render" demo bug logged 2026-04-26.
    layer->onCanvasCRSChanged(m_canvasSRS);

    // Only vector (non-raster) layers populate the overlay scene.
    if (!layer->isRasterLayer())
        layer->populateScene(m_scene, m_extent, m_canvasSRS);

    // §QSG-1 — seed the new layer's per-kind QSG scope from the
    // current Preferences mask so it lights up the GPU path
    // immediately if the user already opted in, instead of waiting
    // for the next preference change.
    if (qobject_cast<SWMMModelLayer *>(layer))
        syncQsgRenderKindsFromPreferences();

    emit layerAdded(layer);
    // Trigger a full render cycle so the new layer is fetched and composited
    // immediately, without waiting for the user to pan or zoom.
    invalidate(Raster | Scene | Overlay, QStringLiteral("layer-added"));
}

OpenSWMMVisLayer *MapCanvas::takeLayer(int index, bool pushUndo)
{
    if (index < 0 || index >= m_layers.count())
        return nullptr;

    // Same pattern as insertLayer: when pushUndo is true, route through the
    // undo command so QUndoStack::push() → RemoveLayerCommand::redo() does
    // the actual removal exactly once via takeLayer(index, false).
    if (pushUndo)
    {
        OpenSWMMVisLayer *layer = m_layers.at(index);
        m_undoStack->push(new RemoveLayerCommand(layer, index, this));
        return layer;   // command's redo() has already removed it
    }

    OpenSWMMVisLayer *layer = m_layers.takeAt(index);

    disconnect(layer, &OpenSWMMVisLayer::repaintRequested,
               this, &MapCanvas::onLayerRepaintRequested);

    // VS.8 — a removed 2D results layer must fall back to CPU painting (it
    // may be re-hosted on a canvas without the QSG path) and the cached
    // pointer must not dangle.
    if (auto *r2d = qobject_cast<SWMM2DResultsLayer *>(layer)) {
        r2d->setQsgOwnsRendering(false);
        if (m_qsgCached2DLayer == r2d) {
            m_qsgCached2DLayer = nullptr;
            m_qsgFrameDirty    = true;
        }
        if (m_qsg2DRenderer)
            m_qsg2DRenderer->setLayer(nullptr);
    }

    if (!layer->isRasterLayer())
        layer->depopulateScene(m_scene);

    updateLayerZValues();

    emit layerRemoved(layer);
    update();
    return layer;
}

void MapCanvas::moveLayer(int fromIndex, int toIndex, bool pushUndo)
{
    if (fromIndex == toIndex)
        return;
    if (fromIndex < 0 || fromIndex >= m_layers.count())
        return;
    if (toIndex < 0 || toIndex >= m_layers.count())
        return;

    // Same pattern as insertLayer / takeLayer: route through the undo
    // command so the work happens exactly once via redo() on push.
    if (pushUndo)
    {
        m_undoStack->push(new MoveLayerCommand(fromIndex, toIndex, this));
        return;
    }

    m_layers.move(fromIndex, toIndex);
    updateLayerZValues();

    // updateLayerZValues only mutates the layer's `m_layerZValue`
    // scalar — existing scene items keep the z-values they were
    // assigned at populateScene time. Rebuild every vector layer so
    // visible stacking order matches the new model order immediately,
    // without waiting for some other event to trigger a repaint. The
    // raster channel is invalidated so cached tiles don't linger on
    // top of what used to be below them.
    for (OpenSWMMVisLayer *l : std::as_const(m_layers)) {
        if (l->isRasterLayer()) continue;
        l->depopulateScene(m_scene);
        l->populateScene(m_scene, m_extent, m_canvasSRS);
    }
    invalidate(Raster | Scene | Overlay,
               QStringLiteral("layer-order-changed"));

    emit layerOrderChanged();
    update();
}

void MapCanvas::reorderLayers(const QList<OpenSWMMVisLayer *> &newOrder, bool pushUndo)
{
    if (newOrder.count() != m_layers.count())
        return;

    if (pushUndo)
    {
        m_undoStack->push(new ReorderLayersCommand(m_layers, newOrder, this));
        return;
    }

    m_layers = newOrder;
    updateLayerZValues();

    for (OpenSWMMVisLayer *l : std::as_const(m_layers)) {
        if (l->isRasterLayer()) continue;
        l->depopulateScene(m_scene);
        l->populateScene(m_scene, m_extent, m_canvasSRS);
    }
    invalidate(Raster | Scene | Overlay,
               QStringLiteral("layer-order-changed"));

    emit layerOrderChanged();
    update();
}

int MapCanvas::layerCount() const { return m_layers.count(); }

OpenSWMMVisLayer *MapCanvas::layerAt(int index) const
{
    return (index >= 0 && index < m_layers.count()) ? m_layers.at(index) : nullptr;
}

// Helper: transform a layer's native-CRS extent into canvas-CRS by
// reprojecting its four corners. Falls back to the untransformed
// extent when the layer's CRS matches the canvas's (identity) or
// when no canvas SRS has been set yet.
static MapExtent layerExtentInCanvasCRS(const OpenSWMMVisLayer *layer,
                                        const SpatialReferenceSystem *canvasSRS)
{
    const MapExtent le = layer->extent();
    if (!le.isValid()) return le;
    auto *lsrs = layer->srs();
    if (!lsrs || !canvasSRS || !lsrs->ogrSpatialReference()
        || !canvasSRS->ogrSpatialReference())
        return le;
    if (lsrs->ogrSpatialReference()->IsSame(canvasSRS->ogrSpatialReference()))
        return le;

    auto *xform = OGRCreateCoordinateTransformation(
        lsrs->ogrSpatialReference(), canvasSRS->ogrSpatialReference());
    if (!xform) return le;

    double xs[4] = {le.xMin(), le.xMax(), le.xMax(), le.xMin()};
    double ys[4] = {le.yMin(), le.yMin(), le.yMax(), le.yMax()};
    xform->Transform(4, xs, ys);
    OGRCoordinateTransformation::DestroyCT(xform);

    double x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < 4; ++i) {
        x0 = std::min(x0, xs[i]); x1 = std::max(x1, xs[i]);
        y0 = std::min(y0, ys[i]); y1 = std::max(y1, ys[i]);
    }
    return MapExtent(x0, y0, x1, y1);
}

MapExtent MapCanvas::extentInCanvasCRS(const OpenSWMMVisLayer *layer,
                                       const MapExtent &nativeExtent) const
{
    if (!layer || !nativeExtent.isValid()) return nativeExtent;
    auto *lsrs = layer->srs();
    if (!lsrs || !m_canvasSRS || !lsrs->ogrSpatialReference()
        || !m_canvasSRS->ogrSpatialReference())
        return nativeExtent;
    if (lsrs->ogrSpatialReference()->IsSame(m_canvasSRS->ogrSpatialReference()))
        return nativeExtent;

    auto *xform = OGRCreateCoordinateTransformation(
        lsrs->ogrSpatialReference(), m_canvasSRS->ogrSpatialReference());
    if (!xform) return nativeExtent;

    double xs[4] = {nativeExtent.xMin(), nativeExtent.xMax(),
                    nativeExtent.xMax(), nativeExtent.xMin()};
    double ys[4] = {nativeExtent.yMin(), nativeExtent.yMin(),
                    nativeExtent.yMax(), nativeExtent.yMax()};
    xform->Transform(4, xs, ys);
    OGRCoordinateTransformation::DestroyCT(xform);

    double x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < 4; ++i) {
        x0 = std::min(x0, xs[i]); x1 = std::max(x1, xs[i]);
        y0 = std::min(y0, ys[i]); y1 = std::max(y1, ys[i]);
    }
    return MapExtent(x0, y0, x1, y1);
}

MapExtent MapCanvas::layerExtentInCanvasCRS(const OpenSWMMVisLayer *layer) const
{
    if (!layer) return {};
    return extentInCanvasCRS(layer, layer->extent());
}

MapExtent MapCanvas::fullExtent() const
{
    // Zoom to Full Extent zooms to the union of data layers.
    //
    // World-spanning basemap layers (XYZ tile providers, world-coverage WMS)
    // return isBasemapLayer() == true and are excluded — including them would
    // always drag the extent back to global scale even on a small local model.
    // Bounded raster layers (DTM, local WMS/WMTS with a defined footprint)
    // return isBasemapLayer() == false and ARE included alongside vector layers.
    //
    // Each layer's native-CRS extent is reprojected into canvas CRS via
    // layerExtentInCanvasCRS() so UTM-based layers and WGS-84-based layers
    // all contribute correctly to the union.
    MapExtent full;
    auto accumulate = [&](const OpenSWMMVisLayer *layer) {
        const MapExtent e = layerExtentInCanvasCRS(layer);
        if (!e.isValid()) return;
        full = full.isValid() ? full.united(e) : e;
    };

    for (const OpenSWMMVisLayer *layer : m_layers) {
        if (layer->isBasemapLayer()) continue;   // world-spanning — skip
        if (layer->isVisible()) accumulate(layer);
    }
    // Fallback: if no bounded layers produced a valid union (e.g. all layers
    // are basemaps), include everything so the button always does something.
    if (!full.isValid()) {
        for (const OpenSWMMVisLayer *layer : m_layers)
            if (layer->isVisible()) accumulate(layer);
    }
    return full;
}

// ---------------------------------------------------------------------------
// Tool management
// ---------------------------------------------------------------------------

OpenSWMMVisMapTool *MapCanvas::activeTool() const { return m_activeTool; }

void MapCanvas::setActiveTool(OpenSWMMVisMapTool *tool)
{
    if (m_activeTool == tool)
        return;

    if (m_activeTool)
        m_activeTool->deactivate();

    m_activeTool = tool;

    if (m_activeTool)
    {
        m_activeTool->activate();
        setCursor(m_activeTool->cursor());
    }
    else
    {
        setCursor(Qt::ArrowCursor);
    }

    emit activeToolChanged(m_activeTool);
}

void MapCanvas::setMeshProfileOverlay(MeshProfileOverlay *overlay)
{
    if (m_meshProfileOverlay == overlay)
        return;
    m_meshProfileOverlay = overlay;
    invalidate(Overlay, QStringLiteral("mesh-profile-overlay-bind"));
}

void MapCanvas::setProfilePathOverlay(ProfilePathOverlay *overlay)
{
    if (m_profilePathOverlay == overlay)
        return;
    m_profilePathOverlay = overlay;
    invalidate(Overlay, QStringLiteral("profile-path-overlay-bind"));
}

// ---------------------------------------------------------------------------
// Undo stack
// ---------------------------------------------------------------------------

MapUndoStack *MapCanvas::undoStack() const { return m_undoStack; }

int MapCanvas::maxUndoCount() const { return m_undoStack->maxUndoCount(); }

void MapCanvas::setMaxUndoCount(int count)
{
    m_undoStack->setMaxUndoCount(count);
    emit maxUndoCountChanged(count);
}

// ---------------------------------------------------------------------------
// Decorations
// ---------------------------------------------------------------------------

bool MapCanvas::showScaleBar() const { return m_showScaleBar; }

void MapCanvas::setShowScaleBar(bool show)
{
    if (m_showScaleBar != show)
    {
        m_showScaleBar = show;
        emit showScaleBarChanged(show);
        update();
    }
}

ScaleBarSettings *MapCanvas::scaleBarSettings() const { return m_scaleBarSettings; }

void MapCanvas::syncScaleBarFromPreferences()
{
    auto *p = PreferencesManager::instance();
    QPen pen(p->scaleBarPenColor(),
             p->scaleBarPenWidth(),
             static_cast<Qt::PenStyle>(p->scaleBarPenStyle()));
    m_scaleBarSettings->setPen(pen);
    m_scaleBarSettings->setFont(QFont(p->scaleBarFontFamily(), p->scaleBarFontSize()));
    m_scaleBarSettings->setUnits(static_cast<ScaleBarSettings::Units>(p->scaleBarUnits()));
    m_scaleBarSettings->setPosition(static_cast<ScaleBarSettings::Position>(p->scaleBarPosition()));
    m_scaleBarSettings->setMaxBarLength(p->scaleBarMaxBarLength());
    m_scaleBarSettings->setLabelDecimals(p->scaleBarLabelDecimals());
    m_scaleBarSettings->setCompactNotation(p->scaleBarCompactNotation());
}

void MapCanvas::syncQsgRenderKindsFromPreferences()
{
    // §QSG-4 — translate the single Preferences toggle into a full
    // QsgKinds bitmap and push it onto every SWMMModelLayer the
    // canvas knows about. New layers added later pick the same
    // mask up through this helper (called from insertLayer too).
    // When the toggle is OFF, the mask is QsgNone — the CPU
    // SWMMLayerItem path draws every kind as before.
    const auto *p = PreferencesManager::instance();
    const SWMMModelLayer::QsgKinds mask = p->qsgRenderEnabled()
        ? SWMMModelLayer::QsgKinds(SWMMModelLayer::QsgNodes
                                 | SWMMModelLayer::QsgLinks
                                 | SWMMModelLayer::QsgCatch
                                 | SWMMModelLayer::QsgGages)
        : SWMMModelLayer::QsgKinds(SWMMModelLayer::QsgNone);
    for (OpenSWMMVisLayer *layer : std::as_const(m_layers))
        if (auto *sl = qobject_cast<SWMMModelLayer *>(layer))
            sl->setQsgRenderKinds(mask);
    // The toggle flips which pipeline owns the visible glyphs. Invalidate the
    // cached QSG framebuffer and force the overlay renderer to rebuild its
    // geometry, otherwise re-enabling the GPU path composites a stale/empty
    // frame: setLayer() no-ops on the unchanged layer, and a self-render while
    // the overlay was off may already have consumed m_contentDirty against
    // empty (un-owned) geometry — the "glyphs vanish after toggling back" bug.
    m_qsgFrameDirty = true;
    m_qsgFrameCache = QImage();
    if (m_qsgRenderer)
        m_qsgRenderer->forceRebuild();
    if (m_qsg2DRenderer)
        m_qsg2DRenderer->forceRebuild();
    // Always schedule a repaint — the toggle changes which pipeline
    // owns the visible glyphs, so even with no QSG kinds active the
    // CPU path needs to repaint its node branch.
    update();
}

bool MapCanvas::showCoordinates() const { return m_showCoords; }

void MapCanvas::setShowCoordinates(bool show)
{
    if (m_showCoords != show)
    {
        m_showCoords = show;
        emit showCoordinatesChanged(show);
        update();
    }
}

void MapCanvas::setTerrainElevation(const std::optional<double> &z)
{
    if (m_terrainZ == z) return;
    m_terrainZ = z;
    update(); // repaint so the terrain label appears/disappears immediately
}

void MapCanvas::setTerrainUnit(const QString &unit)
{
    if (m_terrainUnit == unit) return;
    m_terrainUnit = unit;
    if (m_terrainZ.has_value()) update();
}

QColor MapCanvas::backgroundColor() const { return m_bgColor; }

void MapCanvas::setBackgroundColor(const QColor &color)
{
    if (m_bgColor != color)
    {
        m_bgColor = color;
        // Do NOT propagate to m_qsgWidget->setClearColor() here.  The QSG
        // widget's clear colour must stay Qt::transparent so that the raster
        // basemap and QGraphicsScene items (2D mesh, etc.) rendered into
        // m_frameBuffer are visible through the QSG overlay.  Setting the
        // clear colour to any opaque value covers m_frameBuffer entirely,
        // making the basemap and mesh invisible.  The background colour is
        // already applied to m_frameBuffer in paintEvent() via fillRect().
        emit backgroundColorChanged(color);
        update();
    }
}

// ---------------------------------------------------------------------------
// Coordinate conversion
// ---------------------------------------------------------------------------

void MapCanvas::toMapCoords(int px, int py, double &mapX, double &mapY) const
{
    if (!m_extent.isValid() || width() <= 0 || height() <= 0)
    {
        mapX = mapY = 0.0;
        return;
    }

    double sx = m_extent.width()  / width();
    double sy = m_extent.height() / height();

    mapX =  m_extent.xMin() + px * sx;
    mapY =  m_extent.yMax() - py * sy;   // Y increases upward
}

void MapCanvas::toPixelCoords(double mapX, double mapY, int &px, int &py) const
{
    if (!m_extent.isValid() || width() <= 0 || height() <= 0)
    {
        px = py = 0;
        return;
    }

    double sx = width()  / m_extent.width();
    double sy = height() / m_extent.height();

    px = static_cast<int>((mapX - m_extent.xMin()) * sx);
    py = static_cast<int>((m_extent.yMax() - mapY) * sy);
}

// ---------------------------------------------------------------------------
// Refresh — Phase 0.9 per-channel invalidation
// ---------------------------------------------------------------------------

namespace {

bool redrawLogEnabled()
{
    // Resolve once per process — env-var lookup is not free, but this gates
    // every redraw so we want it cheap. qEnvironmentVariableIntValue returns
    // 0 when unset, which means logging stays off by default.
    static const bool kEnabled =
        qEnvironmentVariableIntValue("SWMMVIS_LOG_REDRAW") != 0;
    return kEnabled;
}

QString channelsToString(MapCanvas::DirtyChannels c)
{
    QStringList parts;
    if (c & MapCanvas::Raster)  parts << QStringLiteral("Raster");
    if (c & MapCanvas::Scene)   parts << QStringLiteral("Scene");
    if (c & MapCanvas::Overlay) parts << QStringLiteral("Overlay");
    if (c & MapCanvas::Extent)  parts << QStringLiteral("Extent");
    return parts.isEmpty() ? QStringLiteral("(none)")
                           : parts.join(QLatin1Char('|'));
}

} // anonymous

void MapCanvas::invalidate(DirtyChannels channels, const QString &reason)
{
    if (channels == NoChannel) return;

    if (redrawLogEnabled())
    {
        qDebug().noquote() << QStringLiteral("[redraw:invalidate] %1  reason=%2")
                                  .arg(channelsToString(channels))
                                  .arg(reason.isEmpty() ? QStringLiteral("(unspecified)") : reason);
    }

    m_pendingChannels |= channels;
    if (!reason.isEmpty()) m_pendingReason = reason;

    if (m_suspendDepth > 0)
        return;     // batched — fire on resume()

    // Lazy-init per-channel timers. Created at-need so MapCanvas instances
    // that never call invalidate() pay no cost.
    if (!m_rasterTimer)
    {
        m_rasterTimer = new QTimer(this);
        m_rasterTimer->setSingleShot(true);
        m_rasterTimer->setInterval(150);     // tile reload is expensive
        connect(m_rasterTimer, &QTimer::timeout,
                this, &MapCanvas::fireRasterChannel);
    }
    if (!m_sceneTimer)
    {
        m_sceneTimer = new QTimer(this);
        m_sceneTimer->setSingleShot(true);
        m_sceneTimer->setInterval(50);       // vector items are cheap
        connect(m_sceneTimer, &QTimer::timeout,
                this, &MapCanvas::fireSceneChannel);
    }

    // Overlay / Extent fire immediately — no debounce.
    if (m_pendingChannels & Overlay)
    {
        m_pendingChannels &= ~Overlay;
        update();   // QWidget repaint of widget-coord decorations
        if (redrawLogEnabled())
            qDebug().noquote() << QStringLiteral("[redraw:fire] Overlay  reason=%1")
                                      .arg(m_pendingReason);
    }
    if (m_pendingChannels & Extent)
    {
        m_pendingChannels &= ~Extent;
        applyExtentToOverlay();
        emit extentChanged(m_extent);
        emit scaleChanged(scale());
        if (redrawLogEnabled())
            qDebug().noquote() << QStringLiteral("[redraw:fire] Extent  reason=%1")
                                      .arg(m_pendingReason);
    }

    if ((m_pendingChannels & Raster) && !m_rasterTimer->isActive())
        m_rasterTimer->start();
    if ((m_pendingChannels & Scene)  && !m_sceneTimer->isActive())
        m_sceneTimer->start();
}

void MapCanvas::suspendRefresh()
{
    ++m_suspendDepth;
    if (redrawLogEnabled())
        qDebug().noquote() << QStringLiteral("[redraw:suspend] depth=%1")
                                  .arg(m_suspendDepth);
}

void MapCanvas::resumeRefresh()
{
    if (m_suspendDepth <= 0) return;
    --m_suspendDepth;
    if (redrawLogEnabled())
        qDebug().noquote() << QStringLiteral("[redraw:resume] depth=%1 pending=%2")
                                  .arg(m_suspendDepth)
                                  .arg(channelsToString(m_pendingChannels));
    if (m_suspendDepth == 0 && m_pendingChannels != NoChannel)
    {
        // Re-emit through invalidate() so the immediate-channel paths
        // (Overlay/Extent) and the timer-armed paths (Raster/Scene) fire.
        const DirtyChannels pending = m_pendingChannels;
        m_pendingChannels = NoChannel;          // avoid double-OR on re-entry
        invalidate(pending, m_pendingReason + QStringLiteral(" (resume)"));
    }
}

void MapCanvas::fireRasterChannel()
{
    if (!(m_pendingChannels & Raster)) return;
    m_pendingChannels &= ~Raster;
    if (redrawLogEnabled())
        qDebug().noquote() << QStringLiteral("[redraw:fire] Raster  reason=%1")
                                  .arg(m_pendingReason);
    // Raster channel = re-warm caches + start a new background composite job.
    int vpW = width(), vpH = height();
    QSize vpSize(vpW, vpH);
    for (OpenSWMMVisLayer *layer : std::as_const(m_layers))
    {
        if (!layer->isRasterLayer() || !layer->isVisible()) continue;
        layer->setViewportSize(vpW, vpH);
        layer->fetchCache(m_extent, vpSize, m_canvasSRS);
    }
    startRenderJob();
}

void MapCanvas::fireSceneChannel()
{
    if (!(m_pendingChannels & Scene)) return;
    m_pendingChannels &= ~Scene;
    if (redrawLogEnabled())
        qDebug().noquote() << QStringLiteral("[redraw:fire] Scene  reason=%1")
                                  .arg(m_pendingReason);
    if (m_isPanning || m_isZooming)
        return;     // gesture in progress — wait for endPan() to retrigger
    int vpW = width(), vpH = height();
    for (OpenSWMMVisLayer *layer : std::as_const(m_layers))
    {
        if (layer->isRasterLayer()) continue;   // handled by raster channel
        layer->setViewportSize(vpW, vpH);
        if (!layer->isVisible())
        {
            layer->depopulateScene(m_scene);
            continue;
        }
        layer->refreshScene(m_scene, m_extent, m_canvasSRS);
    }
    m_sceneDirty = true;   // P2 — the sweep may have mutated scene items
    update();
    // No extra m_qsgWidget->update() needed: paintEvent() now drives the QSG
    // render synchronously via repaint() + grabFramebuffer(), so any dirty
    // updatePaintNode() state is picked up on the very next canvas repaint.
}

// ---------------------------------------------------------------------------
// renderSceneBuffer — rasterise the vector QGraphicsScene into m_sceneBuffer
// ---------------------------------------------------------------------------
//
// QGIS-style cache for the vector overlay (2D mesh, GIS vectors, annotations).
// Called from paintEvent on every non-gesture paint: it renders the scene live
// into m_sceneBuffer (so selection / profile / hover stay immediate) and the
// caller blits it 1:1. The same buffer is then blitted with a stale-buffer
// transform during an active pan/zoom gesture, so QGraphicsScene::render() —
// which paints the full mesh — does not run on every gesture frame (same
// approach as m_mapBuffer for raster layers).
void MapCanvas::renderSceneBuffer()
{
    if (!m_scene || !m_extent.isValid() || width() <= 0 || height() <= 0)
        return;

    const qreal dpr = devicePixelRatioF();
    const QSize devSize(qRound(width() * dpr), qRound(height() * dpr));
    if (m_sceneBuffer.size() != devSize
        || !qFuzzyCompare(m_sceneBuffer.devicePixelRatio(), dpr))
    {
        m_sceneBuffer = QImage(devSize, QImage::Format_ARGB32_Premultiplied);
        m_sceneBuffer.setDevicePixelRatio(dpr);
    }
    m_sceneBuffer.fill(Qt::transparent);

    QPainter sp(&m_sceneBuffer);
    sp.setRenderHints(QPainter::Antialiasing
                      | QPainter::TextAntialiasing
                      | QPainter::SmoothPixmapTransform);
    const QRectF targetRect(0, 0, width(), height());
    const QRectF sourceRect(m_extent.xMin(), -m_extent.yMax(),
                            m_extent.width(), m_extent.height());
    m_scene->render(&sp, targetRect, sourceRect, Qt::IgnoreAspectRatio);
    sp.end();

    m_sceneBufferExtent = m_extent;
    m_sceneDirty = false;   // cache is current for this extent (see paintEvent)
}

// ---------------------------------------------------------------------------
// Legacy refresh API — preserved verbatim so existing callers don't change
// behavior. Callers should migrate to invalidate(channels) over time.
// ---------------------------------------------------------------------------

void MapCanvas::refresh()
{
    if (!m_refreshTimer->isActive())
        m_refreshTimer->start();
}

void MapCanvas::refreshLayerItems()
{
    if (m_isPanning || m_isZooming)
        return;

    // End-of-gesture trigger for the QSG framebuffer regrab. During pan
    // and zoom, paintEvent uses a stale-buffer transform on the cached
    // QSG frame (drawImage with translated/scaled dstRect) instead of
    // re-running the synchronous repaint + grabFramebuffer pair, which
    // is the most expensive single call in paintEvent on large models.
    // When the gesture ends, refresh() fires this slot via the 50 ms
    // debounce timer; if the viewport has drifted from the cached
    // extent, mark the cache dirty so the next paint grabs a fresh
    // frame at the current extent. Basemap-tile-arrived refreshes do
    // NOT drift the extent, so they correctly skip the regrab.
    if (m_qsgCachedExtent.isValid() && m_extent != m_qsgCachedExtent)
        m_qsgFrameDirty = true;

    int vpW = width();
    int vpH = height();
    const QSize vpSize(vpW, vpH);

    // Update viewport size for all layers
    for (OpenSWMMVisLayer *layer : std::as_const(m_layers))
        layer->setViewportSize(vpW, vpH);

    // Process each layer by type:
    //   Raster layers -> trigger tile/data fetch so the cache is warm when the
    //                    background MapRenderJob calls render().
    //   Vector layers -> refresh their QGraphicsItems in the overlay scene.
    for (OpenSWMMVisLayer *layer : std::as_const(m_layers))
    {
        if (layer->isRasterLayer())
        {
            if (layer->isVisible())
                layer->fetchCache(m_extent, vpSize, m_canvasSRS);
            continue;
        }

        if (!layer->isVisible())
        {
            layer->depopulateScene(m_scene);
            continue;
        }
        layer->refreshScene(m_scene, m_extent, m_canvasSRS);
    }

    // Vector items were updated — trigger an immediate repaint so they appear
    // even before the async raster render job completes.
    m_sceneDirty = true;   // P2 — the sweep may have mutated scene items
    update();

    // Start a background render job to composite raster layers into m_mapBuffer
    startRenderJob();
}

// ---------------------------------------------------------------------------
// Render job management
// ---------------------------------------------------------------------------

void MapCanvas::startRenderJob()
{
    cancelRenderJob();

    // Collect only visible raster layers in bottom-to-top order
    QList<OpenSWMMVisLayer *> rasterLayers;
    for (OpenSWMMVisLayer *layer : std::as_const(m_layers))
    {
        if (layer->isRasterLayer() && layer->isVisible())
            rasterLayers.append(layer);
    }

    const qreal dpr = devicePixelRatioF();

    if (rasterLayers.isEmpty())
    {
        const QSize devSize(qRound(width()  * dpr),
                            qRound(height() * dpr));
        m_mapBuffer = QImage(devSize, QImage::Format_ARGB32_Premultiplied);
        m_mapBuffer.setDevicePixelRatio(dpr);
        m_mapBuffer.fill(m_bgColor);
        update();
        return;
    }

    // QGIS-style: do NOT discard the existing buffer when a new render job
    // starts. The stale buffer keeps showing under the in-flight scene-overlay
    // render until onRenderJobFinished() swaps in the fresh tiles, eliminating
    // the brief blank/background-color flash that otherwise occurs every
    // time the canvas refreshes (resize, pan end, zoom end, basemap toggle).

    m_renderJob = new MapRenderJob(rasterLayers,
                                   m_extent,
                                   size(),
                                   dpr,
                                   m_canvasSRS,
                                   m_bgColor,
                                   this);

    // Snapshot the extent we're rendering — when the result arrives this
    // becomes m_mapBufferExtent so paintEvent can position a stale buffer
    // correctly during subsequent pan/zoom before a new render finishes.
    m_pendingRenderExtent = m_extent;

    connect(m_renderJob, &MapRenderJob::finished,
            this,        &MapCanvas::onRenderJobFinished);

    m_renderJob->start();
}

void MapCanvas::cancelRenderJob()
{
    if (m_renderJob)
    {
        m_renderJob->cancel();
        // The job will still deliver finished() — disconnect to ignore it.
        disconnect(m_renderJob, &MapRenderJob::finished,
                   this,        &MapCanvas::onRenderJobFinished);
        // deleteLater is safe here: MapRenderJob::start() captures all worker
        // state by value into the lambda and uses a shared_ptr cancel flag
        // and QPointer for the finished-emit callback. The worker is fully
        // independent of the job's lifetime, so destroying the job now does
        // NOT race the worker's reads. No GUI-thread block during pan/zoom.
        m_renderJob->deleteLater();
        m_renderJob = nullptr;
    }
}

void MapCanvas::onRenderJobFinished(QImage result)
{
    if (m_renderJob)
        m_renderJob->deleteLater();
    m_renderJob = nullptr;
    m_mapBuffer = result;
    m_mapBufferExtent = m_pendingRenderExtent;  // record what extent this buffer covers
    update();
}

// ---------------------------------------------------------------------------
// paintEvent — composites raster buffer + tool overlay + decorations
// ---------------------------------------------------------------------------

void MapCanvas::paintEvent(QPaintEvent * /*event*/)
{
    // Opt-in profiling (openswmm.render.perf).
    const bool perfOn = lcRenderPerf().isDebugEnabled();
    QElapsedTimer paintTimer;
    if (perfOn)
        paintTimer.start();

    // Composite all layers into m_frameBuffer first, then blit in one call.
    // This eliminates any intermediate visual state that causes flickering.
    //
    // Allocate the backing image at device pixels and tag it with the
    // device-pixel ratio. QPainter then accepts logical coordinates from
    // every layer's render() exactly as before but rasterises into the
    // full device-pixel resolution — without this, float tile rects on
    // Retina (DPR=2) display as sub-pixel seams.
    const qreal dpr = devicePixelRatioF();
    const QSize devSize(qRound(width() * dpr), qRound(height() * dpr));
    if (m_frameBuffer.size() != devSize
        || !qFuzzyCompare(m_frameBuffer.devicePixelRatio(), dpr))
    {
        m_frameBuffer = QImage(devSize, QImage::Format_ARGB32_Premultiplied);
        m_frameBuffer.setDevicePixelRatio(dpr);
    }

    QPainter p(&m_frameBuffer);
    p.setRenderHints(QPainter::Antialiasing
                     | QPainter::TextAntialiasing
                     | QPainter::SmoothPixmapTransform);

    // ---- Background -------------------------------------------------------
    // QPainter on m_frameBuffer operates in logical pixels (the image has
    // setDevicePixelRatio(dpr)) — fill the widget's logical rect, not the
    // image's device-pixel rect, otherwise the fill overshoots on Retina.
    p.fillRect(rect(), m_bgColor);

    // ---- Layer 1: raster layers -------------------------------------------
    // Always blit the pre-rendered raster buffer using the QGIS-style
    // stale-buffer transform. During pan/zoom this draws m_mapBuffer at a
    // translated/scaled destination rect — one drawImage call, fast enough
    // for smooth gesture feedback. The per-pixel reverse-projection inside
    // XYZTileLayer::render() is too expensive to run per mouse-move at
    // device-pixel resolution (8M+ samples on Retina), and Qt coalesces the
    // resulting slow paint events into a single mouse-up paint — which is
    // why pan appeared frozen until release.
    if (!m_mapBuffer.isNull())
    {
        // QGIS-style stale-buffer transform.
        //
        // m_mapBuffer was rendered at m_mapBufferExtent. The current view is
        // at m_extent. If the user has panned or zoomed since the last render
        // completed, drawing the buffer at (0,0) puts the basemap at the wrong
        // place — the visible "flash" on mouse-up. Instead, compute the pixel
        // rect that maps the buffer's source-extent into the *current* view's
        // pixel space, then drawImage(rect, buffer). The stale tiles scroll
        // and scale smoothly until the new buffer arrives and slots in
        // exactly.
        if (m_mapBufferExtent.isValid() && m_extent.isValid()
            && m_extent.width() > 0 && m_extent.height() > 0)
        {
            const double pxPerCanvasX =
                static_cast<double>(width())  / m_extent.width();
            const double pxPerCanvasY =
                static_cast<double>(height()) / m_extent.height();

            const double dstLeft  =
                (m_mapBufferExtent.xMin() - m_extent.xMin()) * pxPerCanvasX;
            const double dstRight =
                (m_mapBufferExtent.xMax() - m_extent.xMin()) * pxPerCanvasX;
            const double dstTop   =
                (m_extent.yMax() - m_mapBufferExtent.yMax()) * pxPerCanvasY;
            const double dstBottom =
                (m_extent.yMax() - m_mapBufferExtent.yMin()) * pxPerCanvasY;

            const QRectF dstRect(dstLeft, dstTop,
                                 dstRight - dstLeft,
                                 dstBottom - dstTop);
            if (dstRect.isValid() && !dstRect.isEmpty())
                p.drawImage(dstRect, m_mapBuffer);
            else
                p.drawImage(0, 0, m_mapBuffer);
        }
        else
        {
            p.drawImage(0, 0, m_mapBuffer);
        }
    }

    // ---- Layer 2: vector scene items ----------------------------------------
    // The vector scene (2D mesh, GIS vectors, annotations) is expensive to
    // render on a large mesh, so the cost is scoped to *active pan/zoom
    // gestures* only: during a gesture we blit the cached m_sceneBuffer with
    // the basemap's stale-buffer transform, so the gesture stays smooth
    // regardless of triangle count. When NOT in a gesture we render the scene
    // live — so selection, the mesh profile tool, hover, and any scene edit
    // are immediately visible (the spatial-grid cull and the LOD overview keep
    // this affordable) — and keep m_sceneBuffer current for the next gesture.
    if (m_extent.isValid() && width() > 0 && height() > 0 && m_scene)
    {
        const bool gesture = m_isPanning || m_isZooming;
        if (gesture && !m_sceneBuffer.isNull() && m_sceneBufferExtent.isValid()
            && m_extent.width() > 0 && m_extent.height() > 0)
        {
            // Fast path: stale-buffer transform (same math as the basemap).
            const double pxPerCanvasX = double(width())  / m_extent.width();
            const double pxPerCanvasY = double(height()) / m_extent.height();
            const double dstLeft   =
                (m_sceneBufferExtent.xMin() - m_extent.xMin()) * pxPerCanvasX;
            const double dstRight  =
                (m_sceneBufferExtent.xMax() - m_extent.xMin()) * pxPerCanvasX;
            const double dstTop    =
                (m_extent.yMax() - m_sceneBufferExtent.yMax()) * pxPerCanvasY;
            const double dstBottom =
                (m_extent.yMax() - m_sceneBufferExtent.yMin()) * pxPerCanvasY;
            const QRectF dstRect(dstLeft, dstTop,
                                 dstRight - dstLeft, dstBottom - dstTop);
            if (dstRect.isValid() && !dstRect.isEmpty())
                p.drawImage(dstRect, m_sceneBuffer);
            else
                p.drawImage(0, 0, m_sceneBuffer);
        }
        else
        {
            // P2 — reuse the cached scene buffer when nothing scene-affecting
            // changed since it was rendered at this exact extent/size. Tile
            // arrivals and decoration-only repaints then skip the full
            // QGraphicsScene rasterisation (the dominant GUI-thread cost on
            // large models). Any doubt → re-render.
            const QSize wantDev(qRound(width() * dpr), qRound(height() * dpr));
            const bool sceneCacheValid =
                !m_sceneDirty
                && !m_sceneBuffer.isNull()
                && m_sceneBufferExtent.isValid()
                && m_sceneBufferExtent == m_extent
                && m_sceneBuffer.size() == wantDev
                && qFuzzyCompare(m_sceneBuffer.devicePixelRatio(), dpr);
            if (!sceneCacheValid)
            {
                QElapsedTimer sceneTimer;
                if (perfOn)
                    sceneTimer.start();
                // Live render (also refreshes the cache for the next gesture).
                renderSceneBuffer();
                if (perfOn)
                    qCDebug(lcRenderPerf).noquote()
                        << QStringLiteral("[paint.sceneRender] %1 ms")
                               .arg(sceneTimer.elapsed());
            }
            if (!m_sceneBuffer.isNull())
                p.drawImage(0, 0, m_sceneBuffer);
        }
    }

    // ---- Layer 2b: SWMM layer QSG rendering (Phase B.RHI) -----------------
    // The QQuickWidget runs off-screen (WA_DontShowOnScreen), so its FBO
    // content is never composited by the OS window server on top of the
    // MapCanvas — which was the cause of two bugs:
    //   • Basemap/DTM/mesh invisible — the opaque Metal layer blocked them.
    //   • Selection highlight missing — async update() meant updatePaintNode()
    //     was not called before the frame was presented.
    //
    // We now drive the rendering ourselves inside paintEvent():
    //   1. Push current canvas state (layer + extent) to the renderer.
    //   2. repaint() — synchronous: QQuickWidget::paintEvent() fires,
    //      QSG sync() calls updatePaintNode() for all dirty items (the
    //      selection-flag arrays are current by this point), then render()
    //      rasterises into the internal FBO.
    //   3. grabFramebuffer() reads the FBO → QImage.
    //   4. drawImage() into m_frameBuffer AFTER basemap / DTM / mesh so
    //      the stacking order is deterministic and all layers are visible.
    if ((m_qsgRenderer || m_qsg2DRenderer) && m_qsgWidget) {
        SWMMModelLayer *firstSwmm = nullptr;
        for (OpenSWMMVisLayer *layer : std::as_const(m_layers)) {
            if (!layer->isVisible()) continue;
            if (auto *sl = qobject_cast<SWMMModelLayer *>(layer)) {
                firstSwmm = sl;
                break;
            }
        }

        // VS.8 — a single SWMM2DResultsQSGRenderer can own one 2D results
        // layer. Choose the topmost visible 2D layer so the QSG framebuffer
        // composes in the same stack order as the QGraphicsScene. If that
        // top layer needs CPU-only masking, keep every 2D results layer on the
        // CPU path; drawing a lower QSG layer above a masked top layer would
        // invert their visual order.
        SWMM2DResultsLayer *top2D = nullptr;
        for (int i = int(m_layers.size()) - 1; i >= 0; --i) {
            OpenSWMMVisLayer *layer = m_layers.at(i);
            if (!layer || !layer->isVisible()) continue;
            if (auto *rl = qobject_cast<SWMM2DResultsLayer *>(layer)) {
                top2D = rl;
                break;
            }
        }
        SWMM2DResultsLayer *want2D =
            (top2D && m_qsg2DRenderer && !top2D->maskSpec().enabled)
                ? top2D : nullptr;
        const bool own2D = want2D != nullptr;

        // Ownership handoff — the setters no-op when unchanged, and their
        // repaintRequested emissions only schedule (not re-enter) a paint.
        for (OpenSWMMVisLayer *layer : std::as_const(m_layers)) {
            if (auto *rl = qobject_cast<SWMM2DResultsLayer *>(layer))
                rl->setQsgOwnsRendering(rl == want2D);
        }
        if (m_qsgCached2DLayer && m_qsgCached2DLayer != want2D)
            m_qsgCached2DLayer->setQsgOwnsRendering(false);

        // While the flood map renders in the QSG frame, the 1D network must
        // render there too (above it) — a CPU-painted network in the scene
        // buffer would composite UNDER the flood map. Force the kinds on and
        // restore the preference-derived mask when the 2D layer goes away.
        if (own2D && firstSwmm
            && firstSwmm->qsgRenderKinds() == SWMMModelLayer::QsgNone) {
            firstSwmm->setQsgRenderKinds(
                SWMMModelLayer::QsgKinds(SWMMModelLayer::QsgNodes
                                       | SWMMModelLayer::QsgLinks
                                       | SWMMModelLayer::QsgCatch
                                       | SWMMModelLayer::QsgGages));
            m_qsg1DForced = true;
        } else if (!own2D && m_qsg1DForced) {
            m_qsg1DForced = false;
            syncQsgRenderKindsFromPreferences();
        }

        // §QSG-1: skip the GPU render + readback entirely when no kind
        // is owned by the QSG overlay (the default Preferences state).
        // grabFramebuffer() is the single most expensive thing in
        // paintEvent on large models — running it on every paint when
        // it's drawing nothing was the silent killer of pan/zoom
        // responsiveness even with the QSG path nominally "off".
        const bool qsgActive =
            (firstSwmm
             && firstSwmm->qsgRenderKinds() != SWMMModelLayer::QsgNone)
            || own2D;
        if (!qsgActive) {
            // Skip the QSG render entirely; CPU layeritem owns every kind.
        } else {
            // QGIS-style stale-buffer transform for the QSG framebuffer.
            // The cache is regrabbed only when the SWMM overlay's content
            // actually changed (selection, symbology, layer swap, widget
            // resize) — extent drift during pan/zoom does NOT trigger a
            // regrab. During a gesture the cached frame is blitted into a
            // translated/scaled destination rect that maps the extent it
            // was rendered at (m_qsgCachedExtent) into the current
            // viewport. End-of-gesture refresh marks m_qsgFrameDirty
            // (see refreshLayerItems) so the next paint regrabs.
            // grabFramebuffer() is the most expensive single call in
            // paintEvent on large models; this path mirrors the basemap
            // m_mapBuffer treatment a few lines above.
            const bool layerChanged   = (firstSwmm != m_qsgCachedLayer)
                                        || (want2D != m_qsgCached2DLayer);
            const bool sizeChanged    = (size()    != m_qsgCachedSize);
            // Scrub-diagnosis probe — pairs with the [2D-qsg] sync logs to
            // show whether a slider tick reached the regrab at all.
            static const bool kQsgDebug =
                qEnvironmentVariableIsSet("OPENSWMM_2D_RENDER_DEBUG");
            if (kQsgDebug)
                qDebug("[2D-canvas] paint: regrab=%d (dirty=%d layerChg=%d "
                       "sizeChg=%d cacheNull=%d) own2D=%d 2Dt=%d",
                       int(m_qsgFrameDirty || layerChanged || sizeChanged
                           || m_qsgFrameCache.isNull()),
                       int(m_qsgFrameDirty), int(layerChanged),
                       int(sizeChanged), int(m_qsgFrameCache.isNull()),
                       int(own2D), want2D ? want2D->currentTimeIndex() : -999);
            if (m_qsgFrameDirty || layerChanged
                || sizeChanged || m_qsgFrameCache.isNull()) {
                if (m_qsgRenderer) {
                    m_qsgRenderer->setLayer(firstSwmm);
                    m_qsgRenderer->setMapExtent(m_extent);
                }
                if (m_qsg2DRenderer) {
                    m_qsg2DRenderer->setLayer(want2D);
                    m_qsg2DRenderer->setMapExtent(m_extent);
                }

                // Render at DEVICE-pixel resolution so MSAA samples are
                // taken at 1:1 with screen pixels. WA_DontShowOnScreen
                // leaves the QQuickWidget's effectiveDevicePixelRatio at
                // 1.0 even after setScreen(), so without this resize the
                // FBO is rasterised at logical resolution (e.g. 800×600
                // on a Retina canvas) and the subsequent drawImage(0,0)
                // pixel-doubles the result into the device-pixel
                // m_frameBuffer — exactly the "jagged even with MSAA"
                // symptom. Resizing to logical × dpr gives the FBO the
                // device resolution, and tagging the grabbed image's
                // DPR lets QPainter draw it back at the correct logical
                // extent below.
                const qreal qsgDpr = devicePixelRatioF();
                const QSize wantedDev(qRound(width()  * qsgDpr),
                                      qRound(height() * qsgDpr));
                if (m_qsgWidget->size() != wantedDev)
                    m_qsgWidget->resize(wantedDev);

                // Synchronous render: updatePaintNode() executes here so the
                // selection overlay reflects the latest flag arrays.
                // QSG-2D-1M Phase 1 — with OPENSWMM_RENDER_PERF=1, time the
                // two expensive halves of the QSG round trip (sync/render vs
                // GPU readback) so per-frame cost can be attributed.
                const bool kPerfOn =
                    OpenSWMM::Render::Qsg2DRenderStats::loggingEnabled();
                QElapsedTimer perfTimer;
                if (kPerfOn) perfTimer.start();
                m_qsgWidget->repaint();
                const double repaintMs =
                    kPerfOn ? perfTimer.nsecsElapsed() / 1e6 : -1.0;
                if (kPerfOn) perfTimer.restart();
                m_qsgFrameCache    = m_qsgWidget->grabFramebuffer();
                if (kPerfOn) {
                    OpenSWMM::Render::Qsg2DRenderStats canvasStats;
                    canvasStats.rendererName = QStringLiteral("canvas");
                    canvasStats.repaintMs    = repaintMs;
                    canvasStats.grabMs       = perfTimer.nsecsElapsed() / 1e6;
                    canvasStats.logIfEnabled();
                }
                m_qsgFrameCache.setDevicePixelRatio(qsgDpr);
                m_qsgFrameDirty    = false;
                m_qsgCachedLayer   = firstSwmm;
                m_qsgCached2DLayer = want2D;
                m_qsgCachedExtent  = m_extent;
                m_qsgCachedSize    = size();
            }

            // Composite the cached QSG frame into m_frameBuffer on top of
            // the basemap / DTM / mesh layers. When the current viewport
            // has drifted from the cached extent (mid-pan, mid-zoom)
            // compute the same dst-rect transform the basemap blit uses
            // and draw scaled — one drawImage call, no GPU readback.
            // Trade-off matches QGIS basemap behaviour: a thin
            // background-coloured strip appears at the leading edge of
            // the drag until refresh() triggers a fresh grab.
            if (!m_qsgFrameCache.isNull()) {
                if (m_qsgCachedExtent.isValid() && m_extent.isValid()
                    && m_extent.width() > 0 && m_extent.height() > 0)
                {
                    const double pxPerCanvasX =
                        static_cast<double>(width())  / m_extent.width();
                    const double pxPerCanvasY =
                        static_cast<double>(height()) / m_extent.height();

                    const double dstLeft  =
                        (m_qsgCachedExtent.xMin() - m_extent.xMin()) * pxPerCanvasX;
                    const double dstRight =
                        (m_qsgCachedExtent.xMax() - m_extent.xMin()) * pxPerCanvasX;
                    const double dstTop   =
                        (m_extent.yMax() - m_qsgCachedExtent.yMax()) * pxPerCanvasY;
                    const double dstBottom =
                        (m_extent.yMax() - m_qsgCachedExtent.yMin()) * pxPerCanvasY;

                    const QRectF dstRect(dstLeft, dstTop,
                                         dstRight - dstLeft,
                                         dstBottom - dstTop);
                    if (dstRect.isValid() && !dstRect.isEmpty())
                        p.drawImage(dstRect, m_qsgFrameCache);
                    else
                        p.drawImage(0, 0, m_qsgFrameCache);
                } else {
                    p.drawImage(0, 0, m_qsgFrameCache);
                }
            }
        } // close `} else {` for qsgActive path
    }

    // ---- Layer 2c: profile overlays (above the QSG flood-map mesh) --------
    // Drawn here — after the QSG frame is composited — so traced profile
    // lines, accepted 1D profile paths, alternative candidates, and markers
    // are never hidden by the 2D mesh. A QGraphicsScene item can't achieve
    // this: the whole scene buffer composites UNDER the QSG frame above.
    // Scene coords (sx = mapX, sy = -mapY) map to device pixels through the
    // same m_extent the basemap/scene/QSG all use, so overlays stay aligned
    // with the mesh at every zoom.
    if (m_meshProfileOverlay) {
        m_meshProfileOverlay->paint(p, [this](const QPointF &sp) {
            int px = 0, py = 0;
            toPixelCoords(sp.x(), -sp.y(), px, py);
            return QPointF(px, py);
        });
    }
    if (m_profilePathOverlay) {
        m_profilePathOverlay->paintOverlay(p, [this](const QPointF &sp) {
            int px = 0, py = 0;
            toPixelCoords(sp.x(), -sp.y(), px, py);
            return QPointF(px, py);
        });
    }

    // ---- Layer 3: tool overlay (rubber-band, measure, etc.) ---------------
    if (m_activeTool)
        m_activeTool->paint(&p, m_extent, m_canvasSRS);

    // ---- Layer 4: decorations ---------------------------------------------
    if (m_showScaleBar)
        renderScaleBar(p);

    if (m_showCoords)
        renderCoordinates(p, m_lastMouseMapX, m_lastMouseMapY);

    if (m_terrainZ.has_value())
        renderTerrainLabel(p);

    p.end(); // finalise m_frameBuffer before blitting

    if (perfOn)
        qCDebug(lcRenderPerf).noquote()
            << QStringLiteral("[paint.total] %1 ms gesture=%2")
                   .arg(paintTimer.elapsed())
                   .arg((m_isPanning || m_isZooming) ? 1 : 0);

    // ---- Blit the completed frame to the screen in one atomic operation ----
    QPainter screen(this);
    screen.drawImage(0, 0, m_frameBuffer);
}

// ---------------------------------------------------------------------------
// View transform management (overlay kept in sync with raster buffer)
// ---------------------------------------------------------------------------

void MapCanvas::applyExtentToOverlay()
{
    if (!m_extent.isValid() || width() <= 0 || height() <= 0)
        return;

    // Scene convention: sx = map_x, sy = -map_y (Y-up in scene, Y-down on screen)
    const double s  = static_cast<double>(width()) / m_extent.width();
    const double dx = -m_extent.xMin() * s;
    const double dy =  m_extent.yMax() * s;

    QTransform t(s, 0.0, 0.0, s, dx, dy);
    m_overlayView->setTransform(t);

    ensureOverlaySceneRectCovers(overlayVisibleSceneRect());
}

QRectF MapCanvas::overlayVisibleSceneRect() const
{
    // Do NOT use m_overlayView->viewport()->rect() — that view is hidden, so
    // its viewport rect is stale on macOS.  Derive from the transform directly.
    const QTransform &t = m_overlayView->transform();
    const double s = t.m11();
    if (s < 1e-12)
        return {};
    return QRectF(-t.dx() / s, -t.dy() / s,
                  (double)width() / s, (double)height() / s);
}

void MapCanvas::ensureOverlaySceneRectCovers(const QRectF &needed)
{
    const double s = qAbs(m_overlayView->transform().m11());
    if (qFuzzyIsNull(s))
        return;

    const double marginW = width()  / s * 3.0;
    const double marginH = height() / s * 3.0;

    QRectF padded = needed.adjusted(-marginW, -marginH, marginW, marginH);
    QRectF sr     = m_scene->sceneRect();
    if (!sr.contains(padded))
        m_scene->setSceneRect(sr.united(padded));
}

void MapCanvas::updateLayerZValues()
{
    for (int i = 0; i < m_layers.count(); ++i)
        m_layers[i]->setLayerZValue(i * 1000.0);
}

// ---------------------------------------------------------------------------
// Event routing
// ---------------------------------------------------------------------------

void MapCanvas::mousePressEvent(QMouseEvent *event)
{
    // Global middle-mouse pan: works regardless of which tool is active.
    if (event->button() == Qt::MiddleButton)
    {
        m_middlePanActive = true;
        m_middlePanStart  = event->pos();
        beginPan();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (m_activeTool)
        m_activeTool->mousePressEvent(event);
}

void MapCanvas::mouseMoveEvent(QMouseEvent *event)
{
    m_lastMousePxX = event->pos().x();
    m_lastMousePxY = event->pos().y();
    toMapCoords(m_lastMousePxX, m_lastMousePxY,
                m_lastMouseMapX, m_lastMouseMapY);
    emit cursorPositionChanged(m_lastMouseMapX, m_lastMouseMapY);

    if (m_showCoords || m_terrainZ.has_value())
        update();

    if (m_middlePanActive)
    {
        QPoint delta = event->pos() - m_middlePanStart;
        m_middlePanStart = event->pos();
        translateViewBy(delta.x(), delta.y());
        event->accept();
        return;
    }

    if (m_activeTool)
        m_activeTool->mouseMoveEvent(event);
}

void MapCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_middlePanActive)
    {
        m_middlePanActive = false;
        setCursor(m_activeTool ? m_activeTool->cursor() : Qt::ArrowCursor);
        endPan();
        event->accept();
        return;
    }
    if (m_activeTool)
        m_activeTool->mouseReleaseEvent(event);
}

void MapCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_activeTool)
        m_activeTool->mouseDoubleClickEvent(event);
}

void MapCanvas::wheelEvent(QWheelEvent *event)
{
    // Scroll-wheel zoom is always active, regardless of which tool is selected.
    double angle = event->angleDelta().y();
    if (!qFuzzyIsNull(angle))
    {
        double factor = (angle > 0) ? 1.5 : (1.0 / 1.5);
        zoomAroundCursor(factor, event->position().toPoint());
        event->accept();
        return;
    }
    // Non-vertical scroll (e.g., horizontal trackpad): delegate to the tool.
    if (m_activeTool)
        m_activeTool->wheelEvent(event);
}

void MapCanvas::keyPressEvent(QKeyEvent *event)
{
    if (m_activeTool)
        m_activeTool->keyPressEvent(event);
}

void MapCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (m_activeTool)
        m_activeTool->keyReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// Hover tooltip (Slice R Phase 4)
//
// Qt fires a QEvent::ToolTip on the widget under the cursor after a short
// hover delay. We intercept it here, hit-test the position against every
// visible SWMMModelLayer's pickAt API, and hand Qt a formatted tooltip via
// QToolTip::showText. Returning true on a hit tells Qt we handled the
// event so it doesn't re-fire the default empty tooltip.
// ---------------------------------------------------------------------------

namespace {

// Build a small multi-line tooltip from the layer's identifyByName
// attributes. Node / Rain-Gage rows get X/Y. Link rows show link type.
// Subcatchment rows show the polygon vertex count. Header is the
// element kind + name in bold HTML.
QString buildTooltip(SWMMModelLayer *sl, const QString &name)
{
    if (!sl || name.isEmpty()) return {};
    const QVariantMap a = sl->identifyByName(name);
    if (a.isEmpty()) return {};

    const QString kind = a.value(QStringLiteral("Type")).toString();
    QString html;
    html += QStringLiteral("<b>%1</b><br/><span style='color:#444'>%2</span>")
                .arg(name.toHtmlEscaped(), kind.toHtmlEscaped());

    auto addRow = [&](const QString &label, const QString &value) {
        if (value.isEmpty()) return;
        html += QStringLiteral("<br/>%1: %2")
                    .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
    };

    const QString nt = a.value(QStringLiteral("Node type")).toString();
    addRow(QObject::tr("Node type"), nt);
    const QString lt = a.value(QStringLiteral("Link type")).toString();
    addRow(QObject::tr("Link type"), lt);

    if (a.contains(QStringLiteral("X")) && a.contains(QStringLiteral("Y"))) {
        const double x = a.value(QStringLiteral("X")).toDouble();
        const double y = a.value(QStringLiteral("Y")).toDouble();
        addRow(QObject::tr("Coord"),
               QStringLiteral("%1, %2").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2));
    }

    const int vc = a.value(QStringLiteral("Vertex count"), -1).toInt();
    if (vc >= 0) addRow(QObject::tr("Vertices"), QString::number(vc));
    const int pvc = a.value(QStringLiteral("Polygon vertices"), -1).toInt();
    if (pvc >= 0) addRow(QObject::tr("Polygon vertices"), QString::number(pvc));

    return html;
}

} // anonymous

bool MapCanvas::event(QEvent *event)
{
    if (event->type() == QEvent::Gesture) {
        gestureEvent(static_cast<QGestureEvent *>(event));
        return true;
    }
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const QPoint pixel = help->pos();

        // Convert to map coords + tolerance — 12 canvas pixels is
        // roughly matches the Select tool's click tolerance and keeps
        // the tooltip trigger area forgiving without being promiscuous.
        double mx = 0.0, my = 0.0;
        toMapCoords(pixel.x(), pixel.y(), mx, my);
        double mx2 = 0.0, my2 = 0.0;
        toMapCoords(pixel.x() + 12, pixel.y() + 12, mx2, my2);
        const double tol = std::max(std::abs(mx2 - mx), std::abs(my2 - my));

        for (OpenSWMMVisLayer *l : std::as_const(m_layers)) {
            if (!l->isVisible()) continue;
            auto *sl = qobject_cast<SWMMModelLayer *>(l);
            if (!sl) continue;

            const auto pr = sl->pickAt(mx, my, tol);
            if (!pr.valid) continue;
            const QString tip = buildTooltip(sl, pr.name);
            if (tip.isEmpty()) continue;

            QToolTip::showText(help->globalPos(), tip, this);
            event->accept();
            return true;
        }
        // No hit — hide any lingering tooltip and pass through.
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QWidget::event(event);
}

void MapCanvas::gestureEvent(QGestureEvent *event)
{
    auto *pinch = static_cast<QPinchGesture *>(
        event->gesture(Qt::PinchGesture));
    if (!pinch)
        return;

    if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
        // QPinchGesture::scaleFactor() is the *incremental* factor since
        // the previous gestureUpdated, which composes cleanly with our
        // existing zoom step. centerPoint() is in global screen
        // coordinates — must convert to viewport pixels.
        const QPoint vp = mapFromGlobal(pinch->centerPoint().toPoint());
        zoomAroundCursor(pinch->scaleFactor(), vp);
    }
    event->accept();
}

void MapCanvas::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // No QSG widget visibility management needed — WA_DontShowOnScreen means
    // it is never presented by the OS, so there is no MDI-tab bleeding risk.
    // The canvas's own repaint cycle drives the QSG render via repaint() +
    // grabFramebuffer() in paintEvent().
    //
    // DPR sync: the offscreen QSG widget defaults to the primary screen
    // (DPR may differ from MapCanvas's screen). Match it to MapCanvas
    // so grabFramebuffer() returns pixels at the same device resolution
    // as the basemap/raster layers — otherwise on multi-monitor or
    // mismatched-Retina setups the overlay composites half-density.
    if (m_qsgWidget) {
        if (QWindow *qw = m_qsgWidget->windowHandle()) {
            if (QScreen *target = window() && window()->windowHandle()
                                   ? window()->windowHandle()->screen()
                                   : screen())
                qw->setScreen(target);
        }
    }
    refresh();
}

void MapCanvas::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
}

void MapCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // Keep the hidden QGraphicsView overlay sized to the canvas.
    m_overlayView->setGeometry(rect());
    // Keep the off-screen QSG widget sized to the canvas so grabFramebuffer()
    // returns an image that matches m_frameBuffer exactly.
    if (m_qsgWidget)
        m_qsgWidget->resize(rect().size());

    if (m_extent.isValid())
        m_extent = arCorrectedExtent(m_extent);
    applyExtentToOverlay();
    emit scaleChanged(scale());

    // Re-render at the new size so raster tiles match the widget dimensions.
    // This is essential when the canvas first becomes visible (e.g. switching
    // from the Welcome tab) or when the window is resized.
    if (width() > 0 && height() > 0)
        refresh();
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void MapCanvas::onLayerRepaintRequested()
{
    // Only invalidate the cached QSG framebuffer when a layer the QSG
    // overlay renders signalled — basemap-tile-arrived and other
    // non-SWMM repaints don't change the QSG overlay, and re-grabbing
    // for those was the dominant cost of paintEvent on large models.
    // VS.8 — 2D results layers live in the overlay too (animation ticks
    // and style edits arrive through this same channel).
    if (qobject_cast<SWMMModelLayer *>(sender())
        || qobject_cast<SWMM2DResultsLayer *>(sender()))
        m_qsgFrameDirty = true;
    // P2 — non-raster layers paint through the QGraphicsScene, so their
    // repaint requests invalidate the cached scene buffer. Raster senders
    // (basemap/DEM tile arrivals) leave it valid — that skip is the win.
    auto *senderLayer = qobject_cast<OpenSWMMVisLayer *>(sender());
    if (!senderLayer || !senderLayer->isRasterLayer())
        m_sceneDirty = true;
    // Schedule a canvas repaint; paintEvent() will drive the QSG render
    // synchronously via repaint() + grabFramebuffer() when needed so the
    // selection highlight is always current when the frame is composed.
    refresh();
}

// ---------------------------------------------------------------------------
// Pan state
// ---------------------------------------------------------------------------

void MapCanvas::beginPan()
{
    m_isPanning = true;
    m_panStartExtent = m_extent;

    // P3 — fill blank margins DURING the drag: a repeating tick warms the
    // raster tile caches at the live drag extent and starts a render job
    // (cancel-and-replace, worker thread). Lazily created like m_rasterTimer.
    if (!m_dragRenderTimer)
    {
        m_dragRenderTimer = new QTimer(this);
        m_dragRenderTimer->setInterval(200);
        connect(m_dragRenderTimer, &QTimer::timeout,
                this, &MapCanvas::onDragRenderTick);
    }
    m_dragRenderTimer->start();
}

void MapCanvas::endPan()
{
    if (m_dragRenderTimer)
        m_dragRenderTimer->stop();
    m_isPanning = false;
    m_isZooming = false;
    syncExtentFromView();
    emit extentChanged(m_extent);
    emit scaleChanged(scale());
    refresh();
}

void MapCanvas::onDragRenderTick()
{
    if (!m_isPanning || width() <= 0 || height() <= 0)
    {
        if (m_dragRenderTimer)
            m_dragRenderTimer->stop();
        return;
    }
    // Raster-only mid-drag refresh at the CURRENT drag extent. Deliberately
    // bypasses refreshLayerItems() (it bails while panning by design — the
    // scene overlay keeps its stale-buffer blit); mirrors fireRasterChannel's
    // shape. fetchCache is non-blocking on every raster layer (network gets /
    // worker-queue fills), and startRenderJob() is cancel-and-replace.
    const QSize vpSize(width(), height());
    for (OpenSWMMVisLayer *layer : std::as_const(m_layers))
    {
        if (!layer->isRasterLayer() || !layer->isVisible())
            continue;
        layer->setViewportSize(vpSize.width(), vpSize.height());
        layer->fetchCache(m_extent, vpSize, m_canvasSRS);
    }
    startRenderJob();
}

bool MapCanvas::isPanning() const
{
    return m_isPanning;
}

// ---------------------------------------------------------------------------
// AR correction helper
// ---------------------------------------------------------------------------

MapExtent MapCanvas::arCorrectedExtent(const MapExtent &ext) const
{
    if (width() <= 0 || height() <= 0 || !ext.isValid())
        return ext;

    double viewAspect = static_cast<double>(width()) / height();
    double extAspect  = ext.width() / ext.height();
    double adjW = ext.width(), adjH = ext.height();

    if (viewAspect > extAspect)
        adjW = adjH * viewAspect;
    else
        adjH = adjW / viewAspect;

    double cx = ext.centerX(), cy = ext.centerY();
    return MapExtent(cx - adjW * 0.5, cy - adjH * 0.5,
                     cx + adjW * 0.5, cy + adjH * 0.5);
}

// ---------------------------------------------------------------------------
// Sync extent from overlay view transform (used after smooth pan/zoom)
// ---------------------------------------------------------------------------

void MapCanvas::syncExtentFromView()
{
    // Derive extent from the overlay transform and canvas pixel dimensions.
    // Never use viewport()->rect() — the overlay view is hidden, so that rect
    // is stale on macOS and produces wrong (skewed) extents.
    const QTransform &t = m_overlayView->transform();
    const double s = t.m11();
    if (s < 1e-12)
        return;

    const double xMin =  -t.dx() / s;
    const double yMax =   t.dy() / s;
    const double xMax =  xMin + (double)width()  / s;
    const double yMin =  yMax - (double)height() / s;

    if (xMin < xMax && yMin < yMax)
        m_extent = MapExtent(xMin, yMin, xMax, yMax);
}

// ---------------------------------------------------------------------------
// Smooth pan: translate overlay and update m_previewTransform for raster buffer
// ---------------------------------------------------------------------------

void MapCanvas::translateViewBy(int dx, int dy)
{
    // Smooth-pan path: translate m_extent in map coordinates so paintEvent's
    // QGraphicsScene::render() picks up the new source rect on every mouse-move,
    // not just at gesture end.
    if (!m_extent.isValid() || width() <= 0 || height() <= 0)
        return;

    const double mapPerPxX = m_extent.width()  / static_cast<double>(width());
    const double mapPerPxY = m_extent.height() / static_cast<double>(height());

    // dx > 0 means cursor moved right → user is dragging the map right → the
    // visible window shifts left in map-space → xMin/xMax decrease.
    // dy > 0 means cursor moved down  → window shifts up in map-space → yMin/yMax increase.
    const double mapDx = -dx * mapPerPxX;
    const double mapDy =  dy * mapPerPxY;

    m_extent = MapExtent(m_extent.xMin() + mapDx,
                         m_extent.yMin() + mapDy,
                         m_extent.xMax() + mapDx,
                         m_extent.yMax() + mapDy);

    // Keep the overlay view transform in sync so any code that still queries
    // it (overviewMap, hit-testing) sees the live position.
    applyExtentToOverlay();

    // Emit live so the status-bar coordinates and scale update during the drag.
    emit extentChanged(m_extent);
    emit scaleChanged(scale());

    update(); // repaint immediately for smooth gesture
}

// ---------------------------------------------------------------------------
// Zoom around a viewport pixel position
// ---------------------------------------------------------------------------

void MapCanvas::zoomAroundCursor(double factor, const QPoint &viewportPos)
{
    if (qFuzzyIsNull(factor) || qFuzzyCompare(factor, 1.0))
        return;

    m_isZooming = true;

    // Compute the scene point under the cursor directly from the stored transform
    // matrix, bypassing mapToScene() which adds internal scrollbar values that
    // can be non-zero even with ScrollBarAlwaysOff, causing the anchor to drift.
    const QTransform &cur = m_overlayView->transform();
    const double oldS  = cur.m11();
    const double oldDx = cur.dx();
    const double oldDy = cur.dy();
    const double sceneX = (viewportPos.x() - oldDx) / oldS;
    const double sceneY = (viewportPos.y() - oldDy) / oldS;

    const double newS  = qBound(1e-9, oldS * factor, 1e6);
    const double newDx = viewportPos.x() - sceneX * newS;
    const double newDy = viewportPos.y() - sceneY * newS;

    // Predict and pre-expand the scene rect
    const double vpW = width();
    const double vpH = height();
    QRectF futureVisible((0.0 - newDx) / newS, (0.0 - newDy) / newS,
                         vpW / newS, vpH / newS);
    ensureOverlaySceneRectCovers(futureVisible);

    m_overlayView->setTransform(QTransform(newS, 0.0, 0.0, newS, newDx, newDy));

    update();

    syncExtentFromView();
    m_isZooming = false;

    emit extentChanged(m_extent);
    emit scaleChanged(scale());
    refresh();
}

// ---------------------------------------------------------------------------
// Scale bar + coordinate display (painted in widget coordinates)
// ---------------------------------------------------------------------------

double MapCanvas::metresPerPixel() const
{
    if (width() <= 0 || !m_extent.isValid() || m_extent.width() <= 0)
        return 1.0;

    const double mapp = m_extent.width() / static_cast<double>(width()); // CRS units / pixel

    if (!m_canvasSRS)
        return mapp;

    if (m_canvasSRS->isProjected())
        return mapp * m_canvasSRS->linearUnitsToMetres();

    if (m_canvasSRS->isGeographic())
    {
        // Convert longitude-degrees/pixel → metres/pixel at the centre latitude.
        // d = R · |Δλ| · cos(φ)  where R is the WGS-84 semi-major axis.
        constexpr double R = 6378137.0;
        const double lat  = qDegreesToRadians(m_extent.centerY());
        return mapp * (M_PI / 180.0) * R * std::abs(std::cos(lat));
    }

    return mapp; // local / unknown CRS — raw units
}

void MapCanvas::renderScaleBar(QPainter &painter) const
{
    const int margin = 10;

    if (width() <= 0 || !m_extent.isValid() || m_extent.width() <= 0)
        return;

    const int    maxLen = m_scaleBarSettings->maxBarLength();
    const double mpp    = metresPerPixel(); // metres per screen pixel (CRS-aware)
    // rawCRS = true only for the generic "Untitled (Local)" fallback where the
    // unit is unknown. Auto-generated local CRS ("Local (ft)" / "Local (m)")
    // has a meaningful linearUnitsToMetres() factor — treat it as real so the
    // scale bar shows proper distances instead of "X units".
    const bool   rawCRS = !m_canvasSRS
        || (m_canvasSRS->isLocal()
            && m_canvasSRS->description() == QStringLiteral("Untitled (Local)"));

    // Round to a "nice" bar length in metres
    double barMetres  = maxLen * mpp;
    double magnitude  = std::pow(10.0, std::floor(std::log10(barMetres)));
    double nice       = barMetres / magnitude;
    if      (nice < 2.0) nice = 1.0;
    else if (nice < 5.0) nice = 2.0;
    else                 nice = 5.0;
    barMetres = nice * magnitude;

    int barPixels = static_cast<int>(std::round(barMetres / mpp));
    if (barPixels < 2) barPixels = 2;

    int barX, barY;
    switch (m_scaleBarSettings->position())
    {
        case ScaleBarSettings::BottomRight:
            barX = width() - margin - barPixels;
            barY = height() - margin - 6;
            break;
        case ScaleBarSettings::TopLeft:
            barX = margin;
            barY = margin + 20;
            break;
        case ScaleBarSettings::TopRight:
            barX = width() - margin - barPixels;
            barY = margin + 20;
            break;
        default: // BottomLeft
            barX = margin;
            barY = height() - margin - 6;
            break;
    }

    painter.setPen(m_scaleBarSettings->pen());
    painter.drawLine(barX, barY, barX + barPixels, barY);
    painter.drawLine(barX, barY - 4, barX, barY + 4);
    painter.drawLine(barX + barPixels, barY - 4, barX + barPixels, barY + 4);

    painter.setFont(m_scaleBarSettings->font());
    painter.drawText(barX, barY - 6,
                     m_scaleBarSettings->formatLabel(barMetres, rawCRS));
}

void MapCanvas::renderTerrainLabel(QPainter &painter) const
{
    if (!m_terrainZ.has_value()) return;

    const QString text = m_terrainUnit.isEmpty()
                             ? QStringLiteral("Z: %1").arg(*m_terrainZ, 0, 'f', 3)
                             : QStringLiteral("Z: %1 %2").arg(*m_terrainZ, 0, 'f', 3).arg(m_terrainUnit);

    painter.setFont(QFont(QStringLiteral("sans-serif"), 9));
    const QFontMetrics fm(painter.font());
    const int textW = fm.horizontalAdvance(text);
    const int textH = fm.height();

    // Position the bubble 14 px right and 28 px above the cursor.
    const int margin = 4;
    int bx = m_lastMousePxX + 14;
    int by = m_lastMousePxY - 28;

    // Clamp so the label never overflows the canvas edges.
    bx = qBound(margin, bx, width()  - textW - 2 * margin);
    by = qBound(textH + margin, by, height() - margin);

    const QRect bg(bx - margin, by - textH, textW + 2 * margin, textH + margin);
    painter.fillRect(bg, QColor(255, 255, 220, 220));
    painter.setPen(QColor(80, 80, 0));
    painter.drawRect(bg);
    painter.setPen(Qt::black);
    painter.drawText(bx, by, text);
}

void MapCanvas::renderCoordinates(QPainter &painter, double mapX, double mapY) const
{
    QString text = QStringLiteral("X: %1  Y: %2")
                       .arg(mapX, 0, 'f', 5)
                       .arg(mapY, 0, 'f', 5);
    if (m_terrainZ.has_value())
        text += QStringLiteral("  Z: %1").arg(*m_terrainZ, 0, 'f', 3);

    painter.setFont(QFont(QStringLiteral("sans-serif"), 9));
    QFontMetrics fm(painter.font());
    int textW = fm.horizontalAdvance(text);
    int textH = fm.height();
    int x     = width()  - textW - 12;
    int y     = height() - 8;

    painter.fillRect(x - 3, y - textH, textW + 6, textH + 4,
                     QColor(255, 255, 255, 180));
    painter.setPen(Qt::black);
    painter.drawText(x, y, text);
}
