/**
 * @file      maptooladdnode.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Click-to-create tool for SWMM nodes — free placement, or insertion
 *         on a conduit (which splits it) when the click lands on one.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPTOOLADDNODE_H
#define MAPTOOLADDNODE_H

#include "map/tools/conduitsplitpick.h"
#include "map/tools/maptool.h"
#include "map/snapengine.h"

#include <QColor>
#include <QString>

class GISRasterLayer;
class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolAddNode
 * \brief Left-click on the canvas to place a new SWMM node.
 * \details The tool is configured with a fixed node type at construction
 *          (Junction / Outfall / Storage / Divider). A numeric suffix on
 *          a default name prefix is auto-assigned to keep the
 *          engine's uniqueness invariant ("J1", "J2", …) unless the
 *          project window provides a custom prefix.
 *
 *          Where the click lands decides what happens
 *          (ADDNODE_SPLIT_REDESIGN_PLAN_2026-09-10.md):
 *            - empty canvas (or snapped to an existing node) → the node is
 *              placed there (AddNodeCommand, terrain-derived invert).
 *            - a conduit → the conduit is split at that point and the node
 *              inserted there (InsertNodeSplitCommand; storage and dividers
 *              are converted from the split junction and get their creation
 *              defaults). Outfalls are refused on a conduit — an outfall must
 *              be terminal — with a status-bar hint and no edit.
 *          Pumps, weirs, orifices and outlets have no length to divide and
 *          never count as a conduit hit. The click is armed on press and
 *          committed on RELEASE (the convention for canvas tools that may
 *          open a dialog — see maptoolpick2dcells.h).
 *
 *          Every placement pushes one command onto the canvas' MapUndoStack.
 *          Engine mutation APIs require OPENED or BUILDING; the tool itself
 *          does not perform a lifecycle precheck or report every rejected add.
 */
class OpenSWMMVisMapToolAddNode : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /*!
     * \param canvas    Target map canvas.
     * \param nodeType  SWMM_NodeType value (0=Junction / 1=Outfall /
     *                  2=Storage / 3=Divider).
     * \param elementKind  Naming-preference key: "junction", "outfall",
     *                     "storage", or "divider". Used to look up the
     *                     configurable prefix from PreferencesManager.
     * @brief Create inactive network drawing tool without installing it or changing engine.
     * @param[in] parent Optional QObject owner; caller owns tool if null.
     * @note GUI-thread only. Current raw model is chosen from canvas list during use;
     *   model/terrain/hover lifetimes and topology coherence remain caller responsibility. Undo
     *   stack required for actual creation; absent stack deletes commands without executing.
     */
    OpenSWMMVisMapToolAddNode(MapCanvas *canvas, int nodeType, const QString &elementKind,
                              QObject *parent = nullptr);

    /**
     * @return Crosshair independent of current snap or drawing phase; no widget cursor change.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Clear snap/hover/armed state, activate base and emit type/LID-specific placement
     *   instructions. No model edit or explicit overlay invalidation.
     */
    void activate() override;
    /**
     * @brief Clear snap/hover/armed state, clear status and deactivate base. No undo of previously
     *   committed placements.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed press; left+canvas arms, other presses disarm.
     * @brief Arm release-based placement only; no hit test, model mutation or event acceptance
     *   here.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed release; left+armed+canvas required, no movement
     *   threshold.
     * @brief Resolve snap and conduit hit at release, then free-place or split according to
     *   node/LID restrictions.
     * @note First-model Node/Gage snap suppresses conduit hit across canvas layers. Outfall or
     *   nonempty LID control refuses conduit hit with status/no edit. Valid conduit may belong to
     *   another visible model than first model. After attempted commit clears previews and
     *   invalidates Scene/Overlay, except early no-model/refusal branches. Tool does not itself
     *   enforce/log engine lifecycle; mutation APIs reject invalid state.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed widget move when canvas lives.
     * @brief Compute first-model SnapEngine candidate and, unless node/gage snap wins, shared
     *   visible-model conduit hover.
     * @note Every move invalidates Overlay; no selection/model edit. Shared SnapEngine has
     *   native/cache frame compatibility requirement. Retains raw hover model until cleared.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] painter Optional active widget-pixel painter; SnapEngine guards null and hover
     *   rendering does too.
     * @param[in] extent Unused current canvas extent.
     * @param[in] srs Unused borrowed canvas CRS.
     * @brief Paint snap ring and projected split-point marker using configured node symbol colour.
     * @note Split-capable kind draws solid7pixelring/10pixelcross; outfall/LID refusal dotted ring
     *   only. Layer transform status ignored with native fallback; saves/restores state, no
     *   DPR/transform reset or repaint request. Preview closest point may differ from clamped split
     *   fraction.
     */
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

    /*!
     * \brief Sets the active terrain layer and node offset used to auto-fill
     *        \c InvertElev when a node is placed freely (a node inserted on a
     *        conduit takes the engine's interpolated invert instead).
     * \param layer   Raster layer to sample; nullptr disables terrain assistance.
     * \param offset  Signed value added to terrain Z (negative = below ground).
     * \param factor  Conversion factor from raster vertical units to model
     *                vertical units (e.g., 3.28084 when raster is metres and
     *                model is feet).  Defaults to 1.0 (no conversion).
     * @note Stores borrowed RAW raster pointer, offset and factor without validation, signal,
     *   sampling or repaint. Raster must outlive use; sample band1 at commit in layer CRS. On valid
     *   sample invert=z*factor+offset in model vertical units, failed sample uses0 sentinel
     *   (AddNodeCommand skips exactly-zero override). Only free placement uses terrain.
     */
    void setTerrain(GISRasterLayer *layer, double offset, double factor = 1.0);

    /*! \brief Whether this tool's node kind may be inserted mid-conduit —
     *         all non-outfall kinds without LID control binding.
     * @return Type is not outfall1 AND stored LID control name empty. Does not validate other
     *   numeric node codes or inspect candidate conduit/engine rules.
     */
    [[nodiscard]] bool kindCanSplitConduit() const;

    /**
     * @param[in] control Exact LID control ID; empty disables LID binding/split restriction.
     * @brief Store creation binding name; no existing engine changes, status update or preview
     *   invalidation.
     * @note Nonempty prevents conduit split and passes name to AddNodeCommand; missing control can
     *   cause attempted add rollback.
     */
    void setLidControl(QString control) { m_lidControl = std::move(control); }
    /**
     * @return Constructor-supplied engine node code, unchanged by conversion success/failure or LID
     *   binding.
     */
    [[nodiscard]] int nodeType() const { return m_nodeType; }

signals:
    /**
     * @param[in] name Desired/free or found/split node ID.
     * @param[in] nodeType Configured requested type; split conversion can fail and leave actual
     *   junction instead.
     * @param[in] x Native target layer X.
     * @param[in] y Native target layer Y.
     * @note Free placement announces after attempted push regardless of add success/no stack. Split
     *   path requires nodeIndex resolves, but does not verify requested subtype. Consumer must
     *   query actual engine state when acceptance matters.
     */
    void nodeAdded(const QString &name, int nodeType, double x, double y);
    /*! Emitted when a placement split a conduit (in addition to nodeAdded).
     * @param[in] nodeName Generated node ID found after split attempt.
     * @param[in] splitLinkName Original upstream conduit name.
     * @param[in] newLinkName Generated downstream name, not independently checked.
     * @note Emitted before nodeAdded/status when node exists; conversion warnings may accompany
     *   successful split with different actual subtype.
     */
    void nodeInsertedOnConduit(const QString &nodeName, const QString &splitLinkName,
                               const QString &newLinkName);
    /**
     * @param[in] message Translated placement instructions, refusal/result/warnings, or empty on
     *   deactivate.
     * @note Synchronous GUI signal; free-placement path itself emits no success/failure status,
     *   only nodeAdded.
     */
    void statusMessageChanged(const QString &message);

private:
    /**
     * @return Borrowed first SWMMModelLayer in canvas list, null without canvas/model. No
     *   visibility, opacity, selected-layer, open-engine or editability filter.
     */
    [[nodiscard]] SWMMModelLayer *activeModelLayer() const;

    /*!
     * \brief Propose a unique name by walking prefix1, prefix2, …
     *        until the engine's nodeIndex lookup returns -1.
     * @param[in] layer Required borrowed model with coherent current names; helper does not reserve
     *   result.
     * @return Preference-prefix plus first unused integer1..99999 for corresponding object kind;
     *   unchecked prefix_X on exhaustion. Node delegates shared helper (which accepts null), other
     *   helpers require live layer. No mutation/cross-kind guarantee.
     */
    [[nodiscard]] QString nextAvailableName(SWMMModelLayer *layer) const;

    /*! Human label for status messages ("junction", "storage node", …).
     * @return Translated LID storage label if control nonempty, otherwise configured
     *   outfall/storage/divider label or junction fallback. No engine query/validation.
     */
    [[nodiscard]] QString kindLabel() const;
    /*! The placed symbol's fill colour, for the hover marker.
     * @param[in] layer Required borrowed model for current legacy symbol-style lookup.
     * @return Type-specific fill colour (outfall/storage/divider, otherwise junction), no
     *   per-object renderer rule/class resolution or LID-specific colour.
     */
    [[nodiscard]] QColor markerColor(const SWMMModelLayer *layer) const;

    /**
     * @param[in] layer Required borrowed target model with live canvas; current selection replaced
     *   after attempt.
     * @param[in] mapX Release X in canvas CRS unless saved snap overrides.
     * @param[in] mapY Release Y in canvas CRS unless saved snap overrides.
     * @brief Resolve native placement and optional terrain invert, generate name and push
     *   AddNodeCommand with LID binding.
     * @note Saved snap native coordinates used directly; otherwise canvas→layer transform status
     *   ignored. Terrain band1 lookup uses layer SRS and conversion/offset. Absent stack deletes
     *   without applying; selection/nodeAdded still occur without checking creation. No direct
     *   invalidation here (release caller does).
     */
    void commitFreePlacement(SWMMModelLayer *layer, double mapX, double mapY);
    /**
     * @param[in] hit Required valid borrowed conduit hit with current layer/name/clamped fraction.
     * @brief Generate node/downstream names, push InsertNodeSplitCommand, then announce/select
     *   found node and append conversion warnings to status.
     * @note Requires live canvas. No stack deletes command; node-name postlookup is acceptance
     *   test. Existing partial split can announce configured type despite failed conversion;
     *   coordinates reread cached node with hit-point fallback. Terrain ignored for split.
     */
    void commitSplit(const ConduitSplitPick::ConduitHit &hit);

    int m_nodeType;
    QString m_elementKind;
    QString m_lidControl;
    SnapEngine::Result m_snap;
    ConduitSplitPick::ConduitHit m_hover; ///< conduit under the cursor, if any
    bool m_armed = false;

    GISRasterLayer *m_terrainLayer = nullptr;
    double m_terrainOffset = 0.0;
    double m_terrainFactor = 1.0; // raster → model vertical unit
};

#endif // MAPTOOLADDNODE_H
