/**
 * @file      maptooladdlink.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Click-to-draw tool for SWMM links (conduit/pump/orifice/weir/outlet).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPTOOLADDLINK_H
#define MAPTOOLADDLINK_H

#include "map/tools/maptool.h"
#include "map/snapengine.h"

#include <QPointF>
#include <QString>
#include <QVector>

class GISRasterLayer;
class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolAddLink
 * \brief Two-click polyline tool that creates a SWMM link between two nodes.
 *
 * Interaction:
 *   - Click on (or near) an existing node → anchors the from-node.
 *   - Subsequent left-clicks add intermediate polyline vertices.
 *   - Click on a second node → commits the link.
 *   - Right-click → removes the last intermediate vertex (or cancels if none).
 *   - Escape → cancels without creating anything.
 *
 * Node snapping activates within the preference tolerance in pixels of a node centre.
 * The nearest node within that tolerance is highlighted with a snap
 * indicator ring painted on the overlay channel.
 */
class OpenSWMMVisMapToolAddLink : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /*! \param elementKind  Naming-preference key: "conduit", "pump",
     *                      "orifice", "weir", or "outlet".
     * @brief Create inactive network drawing tool without installing it or changing engine.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events; no ownership
     *   transfer.
     * @param[in] parent Optional QObject owner; caller owns tool if null.
     * @param[in] linkType Engine link type0conduit/1pump/2orifice/3weir/4outlet; no constructor
     *   validation.
     * @note GUI-thread only. Current raw model is chosen from canvas list during use;
     *   model/terrain/hover lifetimes and topology coherence remain caller responsibility. Undo
     *   stack required for actual creation; absent stack deletes commands without executing.
     */
    OpenSWMMVisMapToolAddLink(MapCanvas *canvas, int linkType, const QString &elementKind,
                              QObject *parent = nullptr);

    /**
     * @return Crosshair independent of current snap or drawing phase; no widget cursor change.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Cancel uncommitted polyline and snap state, then activate base tool; Overlay
     *   invalidated by cancel. No rollback of created chained links.
     */
    void activate() override;
    /**
     * @brief Cancel uncommitted polyline and snap state, then deactivate base tool; Overlay
     *   invalidated by cancel. No rollback of created chained links.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event when canvas/model exist; left/right handled.
     * @brief Start from existing eligible node, add intermediate bend, or commit/chain to a
     *   different eligible node.
     * @note Uses node-only snap independent of snapEnabled preference; virtual/inlet nodes
     *   rejected. Idle background ignored, same origin-node ignored. Right removes last bend while
     *   drawing, otherwise cancels. General vertex snap for bend uses LAST move result without
     *   refreshing at click, round-tripped to canvas frame. No acceptance or release arming here.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed widget event during drawing/live canvas.
     * @brief Track canvas cursor, eligible endpoint node and fallback general SnapEngine vertex,
     *   then invalidate Overlay.
     * @note No model edit. Without a current model, previous node-target can remain stored; caller
     *   preserves target/model identity during drawing. General snap query follows preferences,
     *   endpoint node query does not.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event; left only handled.
     * @brief Drop any trailing bend and cancel active drawing/chain on left double-click.
     * @note Does not commit a free endpoint or undo links already created by preceding press. No
     *   event acceptance.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event; Escape cancels, other keys unchanged.
     * @brief Cancel current drawing/chain on Escape without deleting committed links or switching
     *   active tool.
     */
    void keyPressEvent(QKeyEvent *event) override;
    /**
     * @param[in] painter Required active widget-pixel painter when drawing/live canvas; null not
     *   guarded then.
     * @param[in] extent Unused current canvas extent.
     * @param[in] srs Unused borrowed canvas CRS.
     * @brief Paint dashed two-pixel polyline, bend dots, green origin and orange endpoint/cyan
     *   generic snap ring.
     * @note Endpoint and accumulated bends use canvas frame; generic SnapEngine native coordinates
     *   are passed directly to pixel conversion here, so that tip/ring requires compatible
     *   canvas/layer frames. Painter state saved/restored, no DPR/transform reset or repaint
     *   request.
     */
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

    /*!
     * \brief Sets the active terrain layer and link offset used to estimate
     *        upstream and downstream invert elevations when a link is committed.
     * \param layer   Raster to sample; nullptr disables terrain assistance.
     * \param offset  Signed value added to terrain Z at each endpoint.
     * @param[in] factor Raster-Z to model vertical conversion factor, default1; no finite/range
     *   validation.
     * @note Stores raw borrowed raster pointer, signed model-vertical offset and factor only.
     *   Lifetime must cover commit; no sampling/notification until commit. Band1 endpoint samples
     *   are reprojected with layer CRS.
     */
    void setTerrain(GISRasterLayer *layer, double offset, double factor = 1.0);

    /*!
     * \brief Convert a terrain-derived invert ELEVATION at one end of a new
     *        link into the offset the engine stores: a DEPTH above that end
     *        node's invert, never negative.
     * \details The engine keeps link offsets as depths whatever the
     *          LINK_OFFSETS display mode (see linkoffsetdisplay.h). Writing
     *          the sampled elevation directly gave every new pipe an offset
     *          equal to the ground elevation. Returns 0 when the node is
     *          unknown or the elevation lies at or below its invert.
     * @param[in] layer Optional borrowed model; null/closed returns zero.
     * @param[in] nodeName Exact existing endpoint node ID.
     * @param[in] invertElevation Finite sampled invert elevation in same project vertical units as
     *   endpoint node invert.
     * @return max(0,sampled elevation minus node invert), zero for nonfinite sample, unknown node
     *   or failed getter. No engine mutation; LINK_OFFSETS display setting does not change this
     *   depth calculation.
     */
    [[nodiscard]] static double
    invertElevationToOffset(SWMMModelLayer *layer, const QString &nodeName, double invertElevation);

signals:
    /**
     * @param[in] name Desired generated link ID, not verified after attempted command.
     * @param[in] linkType Configured requested link-type code.
     * @param[in] fromNode Saved origin node ID.
     * @param[in] toNode Chosen destination node ID.
     * @note Emitted and typed selection changed even missing undo stack or rejected add. Tool then
     *   chains from destination regardless; consumer checks model for actual creation.
     */
    void linkAdded(const QString &name, int linkType, const QString &fromNode,
                   const QString &toNode);

private:
    enum class State { Idle, Drawing };

    /**
     * @return Borrowed first SWMMModelLayer in canvas list, null without canvas/model. No
     *   visibility, opacity, selected-layer, open-engine or editability filter.
     */
    [[nodiscard]] SWMMModelLayer *activeModelLayer() const;
    /**
     * @param[in] layer Required borrowed model with coherent current names; helper does not reserve
     *   result.
     * @return Preference-prefix plus first unused integer1..99999 for corresponding object kind;
     *   unchecked prefix_X on exhaustion. Node delegates shared helper (which accepts null), other
     *   helpers require live layer. No mutation/cross-kind guarantee.
     */
    [[nodiscard]] QString nextAvailableName(SWMMModelLayer *layer) const;

    /*! Snap to the nearest node within preference tolerance; returns "" if none found.
     * @param[in] layer Required borrowed model, tool requires live canvas for tolerance conversion.
     * @param[in] mapX Canvas-map X.
     * @param[in] mapY Canvas-map Y.
     * @param[out] snapX Optional canvas X output, only written on success.
     * @param[out] snapY Optional canvas Y output, only written on success.
     * @return Exact qualifying Node name from identifyAt within preference pixel tolerance; empty
     *   otherwise, including virtual/inlet junction. Node coordinate projected layer→canvas with
     *   ignored cached/transform statuses and initialized zero/native fallback.
     * @note Does not consult global snapEnabled, impose second distance check or globally search
     *   beyond identifyAt first feature. No selection/model mutation.
     */
    [[nodiscard]] QString snapToNode(SWMMModelLayer *layer, double mapX, double mapY, double *snapX,
                                     double *snapY) const;

    /**
     * @brief Return idle, clear origin ID/bends/snap targets and generic snap, invalidate Overlay
     *   when canvas lives.
     * @note Stored point values/terrain/configuration retained; no engine undo or active-tool
     *   switch.
     */
    void cancel();
    /**
     * @param[in] layer Required borrowed model with live canvas, containing origin/destination
     *   nodes.
     * @param[in] toNodeName Exact chosen destination ID.
     * @param[in] toX Destination canvas-map X for next chained origin.
     * @param[in] toY Destination canvas-map Y for next chained origin.
     * @brief Convert bend canvas coordinates to layer frame, estimate terrain endpoint offsets and
     *   enqueue named AddLinkCommand.
     * @note Band1 samples converted z*factor+offset then clamped nonnegative depth above respective
     *   node invert; transform/getter failures retain fallbacks. Current defaults/autoLength
     *   handled by command. No stack deletes command; selection/linkAdded still emitted. Clears
     *   bends/snaps and chains from destination even rejected creation, invalidates Scene/Overlay.
     */
    void commit(SWMMModelLayer *layer, const QString &toNodeName, double toX, double toY);

    int m_linkType;
    QString m_elementKind;

    State m_state = State::Idle;
    QString m_fromNode;
    QPointF m_fromPt;                // map coords of from-node
    QVector<QPointF> m_vertices;     // intermediate vertices (map coords)
    QPointF m_cursor;                // current mouse map coords (for rubber-band)
    QString m_snapTarget;            // name of end-node currently snapped to (node-only)
    QPointF m_snapPt;                // map coords of node snap target
    SnapEngine::Result m_vertexSnap; // general vertex snap for intermediate points

    GISRasterLayer *m_terrainLayer = nullptr;
    double m_terrainOffset = 0.0;
    double m_terrainFactor = 1.0;
};

#endif // MAPTOOLADDLINK_H
