/*!
 * \file   mapcanvas.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Central map display — QGIS-style hybrid rendering.
 */

#ifndef MAPCANVAS_H
#define MAPCANVAS_H

#include "map/mapextent.h"
#include "map/mapundostack.h"
#include "map/scalebarsettings.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <QTransform>
#include <QWidget>
#include <optional>

class OpenSWMMVisScene;
class OpenSWMMVisGraphicsView;
class OpenSWMMVisLayer;
class SpatialReferenceSystem;
class OpenSWMMVisMapTool;
class MapUndoStack;
class MapRenderJob;
class MeshProfileOverlay;
class ProfilePathOverlay;
class QGestureEvent;

/*!
 * \class MapCanvas
 * \brief The central map display widget — QGIS-style hybrid rendering.
 *
 * \details MapCanvas uses a two-layer compositing approach inspired by QGIS:
 *
 *  **Layer 1 — Raster buffer** (WMS, WMTS, GIS rasters)
 *  Raster layers are rendered into an off-screen QImage (m_mapBuffer) by a
 *  background MapRenderJob.  The buffer is blitted to the screen in
 *  paintEvent().  During a pan or zoom gesture the previous buffer is shown
 *  shifted/scaled via a preview transform for instant visual feedback.
 *
 *  **Layer 2 — QGraphicsView overlay** (SWMM nodes, links, GIS vectors)
 *  Interactive vector items live in a QGraphicsScene managed by a transparent
 *  OpenSWMMVisGraphicsView child widget stacked on top.  The overlay uses a
 *  matching map→pixel transform and provides Qt's BSP-tree hit-testing and
 *  selection.
 *
 * Coordinate systems:
 *  - **Scene coordinates** = map CRS coordinates (Y-up, X-right).
 *  - **Widget/pixel coordinates** = screen pixels (Y-down, X-right).
 *  - toMapCoords / toPixelCoords handle the conversion.
 */
class MapCanvas : public QWidget
{
    Q_OBJECT

    Q_PROPERTY(MapExtent extent READ extent WRITE setExtent NOTIFY extentChanged)
    Q_PROPERTY(double    scale  READ scale  NOTIFY scaleChanged)
    Q_PROPERTY(bool      showScaleBar    READ showScaleBar    WRITE setShowScaleBar
               NOTIFY showScaleBarChanged)
    Q_PROPERTY(bool      showCoordinates READ showCoordinates WRITE setShowCoordinates
               NOTIFY showCoordinatesChanged)
    Q_PROPERTY(int       maxUndoCount    READ maxUndoCount    WRITE setMaxUndoCount
               NOTIFY maxUndoCountChanged)
    Q_PROPERTY(QColor    backgroundColor READ backgroundColor WRITE setBackgroundColor
               NOTIFY backgroundColorChanged)
    Q_PROPERTY(ScaleBarSettings* scaleBarSettings READ scaleBarSettings CONSTANT)

public:

    explicit MapCanvas(QWidget *parent = nullptr);
    ~MapCanvas() override;

    // ----- Scene access --------------------------------------------------

    [[nodiscard]] OpenSWMMVisScene *mapScene() const;

    // ----- Overlay view access -------------------------------------------

    [[nodiscard]] OpenSWMMVisGraphicsView *overlayView() const;

    // ----- CRS -----------------------------------------------------------

    [[nodiscard]] SpatialReferenceSystem *canvasSRS() const;

    void setCanvasSRS(SpatialReferenceSystem *srs, bool ownsSRS = false);

    bool setCanvasSRSByCode(const QString &authName, int code);

    // ----- Extent / view -------------------------------------------------

    [[nodiscard]] MapExtent extent() const;

    void setExtent(const MapExtent &extent, bool pushUndo = true);

    void zoomToFullExtent();
    void zoomIn(double factor = 2.0);
    void zoomOut(double factor = 2.0);
    void pan(double dx, double dy);

    [[nodiscard]] double scale() const;

    /*! \brief Current scale as a 1:N denominator, DPI-aware and CRS-aware.
     *
     *  Uses the actual screen DPI (logicalDotsPerInchX) so the readout is
     *  accurate on Retina / HiDPI displays, where the previous hardcoded
     *  96 DPI assumption was off by ~2×.  Falls back to 96 DPI when no
     *  screen is attached (e.g. during construction). */
    [[nodiscard]] double scaleDenominator() const;

    /*! \brief Zoom the view to exactly the given 1:N scale, preserving the
     *         current centre point and canvas aspect ratio.  CRS-aware: the
     *         physical metres-per-pixel implied by \p denom is converted
     *         back to map units using the canvas SRS (cosine-latitude for
     *         geographic, linearUnitsToMetres for projected).
     *
     *  \note Triggers extentChanged() and scaleChanged() via setExtent(). */
    void setScaleDenominator(double denom);

    // ----- Interactive navigation (used by map tools) --------------------

    /*! \brief Smooth-pan the view by (dx, dy) screen pixels without updating
     *         m_extent or triggering tile reloads.  Call endPan() on release. */
    void translateViewBy(int dx, int dy);

    /*! \brief Zoom the view by \p factor anchored at \p viewportPos, then
     *         sync m_extent and schedule a tile refresh. */
    void zoomAroundCursor(double factor, const QPoint &viewportPos);

    /*! \brief Recompute m_extent from the current overlay view transform.
     *         Call after translateViewBy() or direct transform manipulation. */
    void syncExtentFromView();

    // ----- Layer management ----------------------------------------------

    [[nodiscard]] const QList<OpenSWMMVisLayer *> &layers() const;

    void addLayer(OpenSWMMVisLayer *layer, bool pushUndo = true);
    void insertLayer(int position, OpenSWMMVisLayer *layer, bool pushUndo = true);
    OpenSWMMVisLayer *takeLayer(int index, bool pushUndo = true);
    void moveLayer(int fromIndex, int toIndex, bool pushUndo = true);

    /*! \brief Atomically reorder the entire layer stack to match \p newOrder.
     *  \details \p newOrder must be a permutation of the current layer list
     *           (same pointers, different sequence). A single undo command is
     *           pushed when \p pushUndo is true (default). Used by the layer
     *           tree panel when whole category groups are dragged to new
     *           positions. */
    void reorderLayers(const QList<OpenSWMMVisLayer *> &newOrder,
                       bool pushUndo = true);

    [[nodiscard]] int layerCount() const;
    [[nodiscard]] OpenSWMMVisLayer *layerAt(int index) const;
    [[nodiscard]] MapExtent fullExtent() const;

    /*! \brief Reproject \p nativeExtent from \p layer's CRS into canvas CRS by
     *  transforming its four corners.  Returns the unmodified extent when the
     *  CRSes match or when either SRS is unavailable. */
    [[nodiscard]] MapExtent extentInCanvasCRS(const OpenSWMMVisLayer *layer,
                                              const MapExtent &nativeExtent) const;

    /*! \brief Convenience overload: reprojects the full extent of \p layer. */
    [[nodiscard]] MapExtent layerExtentInCanvasCRS(const OpenSWMMVisLayer *layer) const;

    // ----- Active tool ---------------------------------------------------

    [[nodiscard]] OpenSWMMVisMapTool *activeTool() const;
    void setActiveTool(OpenSWMMVisMapTool *tool);

    // ----- Mesh-profile overlay ------------------------------------------

    /*! \brief Bind the 2D-mesh profile overlay (line + position marker) drawn
     *  ON TOP of every map layer — including the QSG flood-map mesh, which a
     *  QGraphicsScene item can't sit above. Pass null to detach. The overlay
     *  is owned by MeshProfilePlotDialog, not the canvas. */
    void setMeshProfileOverlay(MeshProfileOverlay *overlay);

    /*! \brief Bind the 1D profile-path candidate/accepted overlay. Painted
     *  above the QSG result frame so profile paths are not obscured by 1D/2D
     *  result layers. The profile selection tool owns the object. */
    void setProfilePathOverlay(ProfilePathOverlay *overlay);

    // ----- Undo ----------------------------------------------------------

    [[nodiscard]] MapUndoStack *undoStack() const;
    [[nodiscard]] int maxUndoCount() const;
    void setMaxUndoCount(int count);

    // ----- Decorations ---------------------------------------------------

    [[nodiscard]] bool showScaleBar() const;
    void setShowScaleBar(bool show);

    [[nodiscard]] bool showCoordinates() const;
    void setShowCoordinates(bool show);

    /*!
     * \brief Sets the terrain elevation to display alongside X/Y in the
     *        coordinate overlay.  Pass an empty optional to hide the Z field.
     *        The value is in model vertical units (not converted here).
     */
    void setTerrainElevation(const std::optional<double> &z);

    /*! Sets the vertical unit label shown alongside the terrain Z value ("ft" or "m"). */
    void setTerrainUnit(const QString &unit);

    /*! Returns the most-recently-sampled terrain elevation, or an empty
     *  optional when no terrain is active or the cursor is out of extent. */
    [[nodiscard]] std::optional<double> terrainZ() const { return m_terrainZ; }

    /*! Returns the vertical unit label currently assigned to the terrain Z display. */
    [[nodiscard]] QString terrainUnit() const { return m_terrainUnit; }

    [[nodiscard]] QColor backgroundColor() const;
    void setBackgroundColor(const QColor &color);

    [[nodiscard]] ScaleBarSettings *scaleBarSettings() const;

    // ----- Coordinate conversion -----------------------------------------

    void toMapCoords(int px, int py, double &mapX, double &mapY) const;
    void toPixelCoords(double mapX, double mapY, int &px, int &py) const;

    // ----- Refresh -------------------------------------------------------

    /*!
     * \enum DirtyChannel
     * \brief Independent redraw channels — see Phase 0.9 in the GUI
     *        implementation plan. Each channel has a separate debounce
     *        timer so cheap updates don't pay for expensive ones.
     */
    enum DirtyChannel {
        NoChannel  = 0x0,
        Raster     = 0x1,   ///< WMS/WMTS/GIS-raster composite (150 ms debounce)
        Scene      = 0x2,   ///< QGraphicsScene vector items   (50 ms debounce)
        Overlay    = 0x4,   ///< Decorations / selection highlights (immediate)
        Extent     = 0x8,   ///< Extent committed (immediate; usually implies Raster|Scene)
    };
    Q_DECLARE_FLAGS(DirtyChannels, DirtyChannel)

    /*!
     * \brief Single entry point for dirtying one or more rendering channels.
     * \details Independent per-channel timers coalesce bursts. Call from any
     *          place that mutates state a user would see on screen. Pass
     *          \p reason to feed the SWMMVIS_LOG_REDRAW instrumentation
     *          (the string appears in the log so regressions are spottable
     *          in CI screenshots / output capture).
     */
    void invalidate(DirtyChannels channels,
                    const QString &reason = QString());

    /*!
     * \brief Bracket a batch of invalidate() calls so they fire once.
     *        Reference-counted — nested pairs are safe. On the matching
     *        resume() that brings the count back to zero, any pending
     *        channels fire at once.
     */
    void suspendRefresh();
    void resumeRefresh();

    // ----- Legacy refresh API (kept for call-site backward compat) -------

    void refresh();
    void refreshLayerItems();

    // ----- Pan state (suppresses tile reloads during drag) ----------------

    void beginPan();
    void endPan();
    [[nodiscard]] bool isPanning() const;

    // Used by ChangeCRSCommand::undo/redo to apply a CRS without pushing to the undo stack.
    void applyCRSInternal(SpatialReferenceSystem *srs, bool ownsSRS);

signals:
    void extentChanged(const MapExtent &extent);
    void scaleChanged(double scale);
    void canvasSRSChanged(SpatialReferenceSystem *srs);
    void layerAdded(OpenSWMMVisLayer *layer);
    void layerRemoved(OpenSWMMVisLayer *layer);
    void layerOrderChanged();
    void activeToolChanged(OpenSWMMVisMapTool *tool);
    void cursorPositionChanged(double mapX, double mapY);
    void showScaleBarChanged(bool show);
    void showCoordinatesChanged(bool show);
    void maxUndoCountChanged(int count);
    void backgroundColorChanged(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event)             override;
    void resizeEvent(QResizeEvent *event)          override;
    void showEvent(QShowEvent *event)              override;
    void hideEvent(QHideEvent *event)              override;

    void mousePressEvent(QMouseEvent *event)       override;
    void mouseMoveEvent(QMouseEvent *event)        override;
    void mouseReleaseEvent(QMouseEvent *event)     override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event)            override;
    void keyPressEvent(QKeyEvent *event)           override;
    void keyReleaseEvent(QKeyEvent *event)         override;

    /*! Override `event` so tooltip requests can pick the hovered SWMM
     *  object through each visible `SWMMModelLayer`'s `pickAt` API
     *  (Slice R Phase 4). No tool activation needed — hover tooltips
     *  fire while any tool is active. Also dispatches `QEvent::Gesture`
     *  to `gestureEvent` so two-finger pinch (Slice AV) zooms the
     *  canvas regardless of the active map tool. */
    bool event(QEvent *event)                      override;

    /*! Handle pinch gestures (touchscreen + Precision touchpad).
     *  Delegates to `zoomAroundCursor` so behaviour matches wheel-zoom. */
    void gestureEvent(QGestureEvent *event);

private slots:
    void onLayerRepaintRequested();
    void onRenderJobFinished(QImage result);
    void fireRasterChannel();
    void fireSceneChannel();
    void syncScaleBarFromPreferences();
    void syncQsgRenderKindsFromPreferences();

private:

    // Extent / overlay management
    void renderSceneBuffer();   ///< Rasterise the vector scene into m_sceneBuffer at the current extent.
    void applyExtentToOverlay();
    void ensureOverlaySceneRectCovers(const QRectF &needed);
    [[nodiscard]] QRectF overlayVisibleSceneRect() const;
    [[nodiscard]] MapExtent arCorrectedExtent(const MapExtent &ext) const;
    void updateLayerZValues();

    // Render job management
    void startRenderJob();
    void cancelRenderJob();

    // Decorations (painted in widget coordinates)
    void renderScaleBar(QPainter &painter) const;
    void renderCoordinates(QPainter &painter, double mapX, double mapY) const;
    void renderTerrainLabel(QPainter &painter) const;

    // Returns ground distance (metres) represented by one horizontal screen pixel
    // at the centre of the current view.  CRS-aware: uses the cosine-latitude
    // formula for geographic CRS and linearUnitsToMetres() for projected CRS.
    [[nodiscard]] double metresPerPixel() const;

    // ----- Scene & overlay ------------------------------------------------
    OpenSWMMVisScene           *m_scene          = nullptr;
    OpenSWMMVisGraphicsView    *m_overlayView    = nullptr;

    // ----- CRS & extent ---------------------------------------------------
    SpatialReferenceSystem *m_canvasSRS      = nullptr;
    bool                    m_ownsSRS        = false;
    MapExtent               m_extent;
    QList<OpenSWMMVisLayer *>   m_layers;

    // ----- Active tool & undo ---------------------------------------------
    OpenSWMMVisMapTool         *m_activeTool     = nullptr;
    MapUndoStack           *m_undoStack      = nullptr;

    // ----- Mesh-profile overlay (painted above the QSG frame) -------------
    // Not owned: MeshProfilePlotDialog creates it and clears this pointer
    // (setMeshProfileOverlay(nullptr)) before deleting it.
    MeshProfileOverlay         *m_meshProfileOverlay = nullptr;

    // Not owned: MapToolSelectProfile creates it and clears this pointer
    // (setProfilePathOverlay(nullptr)) before deleting it.
    ProfilePathOverlay         *m_profilePathOverlay = nullptr;

    // ----- Decorations ----------------------------------------------------
    bool                    m_showScaleBar   = true;
    bool                    m_showCoords     = false;
    std::optional<double>   m_terrainZ;           // set by TerrainToolbar sampling
    QString                 m_terrainUnit;        // "ft" or "m" from UnitSystem
    QColor                  m_bgColor        = Qt::white;
    ScaleBarSettings       *m_scaleBarSettings = nullptr;

    // ----- Refresh timer --------------------------------------------------
    QTimer                 *m_refreshTimer   = nullptr;

    // ----- Phase 0.9 — per-channel dirty / debounce -----------------------
    /// Per-channel debounce timers; null until first invalidate() arms them.
    QTimer                 *m_rasterTimer    = nullptr;
    QTimer                 *m_sceneTimer     = nullptr;
    /// Bitmask of pending channels.
    DirtyChannels           m_pendingChannels = NoChannel;
    /// suspend/resume reference counter (0 = not suspended).
    int                     m_suspendDepth   = 0;
    /// Last-attached "reason" — printed by SWMMVIS_LOG_REDRAW.
    QString                 m_pendingReason;

    // ----- QGIS-style render buffers --------------------------------------
    QImage                  m_mapBuffer;          /*!< 1× raster buffer, updated after each non-pan refresh. */
    MapExtent               m_mapBufferExtent;    /*!< Source extent for which m_mapBuffer was rendered.
                                                       Used to draw stale buffer at the correct pixel
                                                       position while a new render is in flight,
                                                       eliminating the "flash" on mouse-up after pan/zoom. */
    MapExtent               m_pendingRenderExtent;/*!< Extent currently being rendered by the worker.
                                                       Snapshotted when startRenderJob() fires; copied
                                                       into m_mapBufferExtent when the result arrives. */
    QImage                  m_frameBuffer;        /*!< Double-buffer: composite of all layers, blitted in one shot. */
    MapRenderJob           *m_renderJob   = nullptr;

    // QGIS-style cache for the vector QGraphicsScene (2D mesh, GIS vectors,
    // annotations), used only during an active pan/zoom gesture: the scene is
    // rasterised live when idle (so selection / profile / hover stay
    // immediate) and that same buffer is blitted with a stale-buffer transform
    // during pan/zoom, instead of re-running QGraphicsScene::render() — which
    // paints the full mesh — on every gesture frame.
    QImage                  m_sceneBuffer;
    MapExtent               m_sceneBufferExtent;

    // ----- Pan / zoom state -----------------------------------------------
    bool                    m_isPanning      = false;
    bool                    m_isZooming      = false;
    MapExtent               m_panStartExtent;
    double                  m_lastMouseMapX  = 0.0;
    double                  m_lastMouseMapY  = 0.0;
    int                     m_lastMousePxX   = 0;
    int                     m_lastMousePxY   = 0;

    // ----- Middle-mouse global pan ----------------------------------------
    bool                    m_middlePanActive = false;
    QPoint                  m_middlePanStart;

    // ----- Phase B.RHI — QQuickWidget host for the QSG renderers ----------
    // A transparent child widget overlaying the canvas, hosting (VS.8) a
    // root Item with two stacked renderers: SWMM2DResultsQSGRenderer
    // (flood map: Gouraud depth fill / bands / isolines / arrows) BELOW
    // SWMMLayerQSGRenderer (SWMM network: nodes/links/subcatchments).
    // The 2D TERRAIN mesh layer renders via QGraphicsScene (QPainter path).
    // Native Metal on macOS, Vulkan on Linux, D3D11 on Windows.
    // See docs/RENDERING_5M_PLAN.md (Phase B.RHI).
    class QQuickWidget               *m_qsgWidget     = nullptr;
    class SWMMLayerQSGRenderer       *m_qsgRenderer   = nullptr;
    class SWMM2DResultsQSGRenderer   *m_qsg2DRenderer = nullptr;

    // Cached QSG framebuffer (re-grabbed only when something the QSG
    // renderer cares about actually changed — extent, layer, widget
    // size, or a layer's repaintRequested signal). Without this cache,
    // every basemap repaint event paid the cost of a synchronous
    // repaint() + grabFramebuffer() on the QSG widget, which dominates
    // paintEvent on large models.
    QImage                       m_qsgFrameCache;
    bool                         m_qsgFrameDirty = true;
    MapExtent                    m_qsgCachedExtent;
    class SWMMModelLayer        *m_qsgCachedLayer = nullptr;
    class SWMM2DResultsLayer    *m_qsgCached2DLayer = nullptr;
    QSize                        m_qsgCachedSize;

    // VS.8 — true while the canvas force-enabled the 1D QSG kinds because a
    // 2D results layer is QSG-owned (the network must composite above the
    // flood map inside the same QSG frame). Restored from Preferences when
    // the 2D layer goes away.
    bool                         m_qsg1DForced = false;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(MapCanvas::DirtyChannels)

#endif // MAPCANVAS_H
