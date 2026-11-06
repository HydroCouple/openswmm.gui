/**
 * @file      maptoolfeatureedit.h
 * @brief     Editing tools for an existing FeatureLayer feature
 *            (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.3): add a hole,
 *            add a part, edit vertices, move a whole feature.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Editing tools for an existing FeatureLayer feature
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.3):
 * add a hole, add a part, edit vertices, move a whole feature.
 *
 * Vertex editing follows OpenSWMMVisMapToolEditVertex's contract
 * (maptooleditvertex.h:29-39) — drag a handle to move it, right-click a
 * segment to insert, right-click a handle to delete, Escape to cancel — but
 * generalised: a FeatureLayer geometry has parts and interior rings, so a
 * handle is addressed by (part, ring, vertex) rather than by a bare index.
 * Handles are painted on the overlay, not added as scene items, exactly as the
 * SWMM vertex editor does.
 *
 * Vertex and whole-feature edits use EditFeatureGeometryCommand on release.
 * Vertex drags are feature-wide mergeable; discrete insert/delete and whole-feature moves are not.
 * AddHole/AddPart inherit the draw base commit, which currently inserts a NEW feature
 * holding the modified selected geometry and schema-default attributes; it does not replace
 * the selected original.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MAPTOOLFEATUREEDIT_H
#define MAPTOOLFEATUREEDIT_H

#include "map/tools/maptoolfeaturedraw.h"
#include "feature/featuregeometry.h"
#include "feature/featuretypes.h"

#include <QPointF>
#include <QPointer>

class FeatureLayer;

/*!
 * \struct FeatureVertexRef
 * \brief Addresses one vertex inside a FeatureGeometry.
 *
 * \c ring == -1 means the part's exterior; >= 0 indexes \c Part::holes.
 */
struct FeatureVertexRef {
    int part = -1;
    int ring = -1;
    int vertex = -1;

    /**
     * @return part>=0 and vertex>=0 only; ring and actual geometry/vector bounds not checked.
     *   Negative ring values map to exterior in ringFor.
     */
    [[nodiscard]] bool isValid() const { return part >= 0 && vertex >= 0; }
    /**
     * @brief Reset part/ring/vertex to minus one; no geometry/layer mutation.
     */
    void clear()
    {
        part = -1;
        ring = -1;
        vertex = -1;
    }
    /**
     * @param[in] o Other value reference tuple.
     * @return Exact equality of all three indices, including invalid tuples; no source geometry
     *   identity/bounds check.
     */
    [[nodiscard]] bool operator==(const FeatureVertexRef &o) const
    {
        return part == o.part && ring == o.ring && vertex == o.vertex;
    }
};

/*!
 * \class OpenSWMMVisMapToolAddHole
 * \brief Draw a ring inside the selected polygon; it becomes an interior ring.
 *
 * The ring is validated with FeatureGeometry::validate BEFORE it is committed,
 * so a hole drawn outside the polygon, crossing its boundary, or overlapping
 * another hole is refused with the reason string rather than written and
 * discovered later by the mesh generator.
 */
class OpenSWMMVisMapToolAddHole : public OpenSWMMVisMapToolFeatureDrawBase
{
    Q_OBJECT
public:
    /**
     * @brief Create inactive feature tool without installing on canvas or changing store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events/undo commands.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread only. Feature target uses QPointer; canvas and model snap source are
     *   borrowed. No automatic selected-target binding; host calls setTargetLayer.
     */
    explicit OpenSWMMVisMapToolAddHole(MapCanvas *canvas, QObject *parent = nullptr);

protected:
    /**
     * @return 3 ring entries before inherited commit; validity/distinctness checked later.
     */
    [[nodiscard]] int minimumVertices() const override { return 3; }
    /**
     * @return True to render closed filled hole preview; no store mutation.
     */
    [[nodiscard]] bool isClosed() const override { return true; }
    /**
     * @param[in] ring New candidate hole points in intended target frame; first point chooses host
     *   exterior.
     * @return COPY of sole selected feature geometry with CCW-normalized hole appended to FIRST
     *   part whose exterior contains first point. Empty missing selection/feature/parts/no host.
     *   Full boundary/hole validity deferred to base.
     * @note Does not modify original feature/attributes; inherited base commit inserts returned
     *   copy as NEW feature using schema defaults, preserving original selected feature. Existing
     *   holes do not participate in host-selection test.
     */
    [[nodiscard]] openswmmvis::feature::FeatureGeometry
    buildGeometry(const openswmmvis::feature::Ring &ring) const override;

private:
    /*! The single selected feature, or kInvalidFeatureId when the selection is
     *  empty or ambiguous.
     * @return Sole target selected stable ID, kInvalidFeatureId for null/zero/multiple selection;
     *   does not validate existence/editability or geometry.
     */
    [[nodiscard]] openswmmvis::feature::FeatureId selectedFeature() const;
};

/*!
 * \class OpenSWMMVisMapToolAddPart
 * \brief Draw an additional part for the selected multi-part feature.
 *
 * Promotes a single-part geometry to its Multi* equivalent when the layer's
 * declared type allows it; when the layer is single-part the commit is refused
 * with an explanation rather than silently replacing the existing part.
 */
class OpenSWMMVisMapToolAddPart : public OpenSWMMVisMapToolFeatureDrawBase
{
    Q_OBJECT
public:
    /**
     * @brief Create inactive feature tool without installing on canvas or changing store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events/undo commands.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread only. Feature target uses QPointer; canvas and model snap source are
     *   borrowed. No automatic selected-target binding; host calls setTargetLayer.
     */
    explicit OpenSWMMVisMapToolAddPart(MapCanvas *canvas, QObject *parent = nullptr);

protected:
    /**
     * @return Target point types1, line types2, otherwise3; no target defaults3. Count only, not
     *   validation.
     */
    [[nodiscard]] int minimumVertices() const override;
    /**
     * @return True without target or for Polygon/MultiPolygon, false other target types. Preview
     *   only.
     */
    [[nodiscard]] bool isClosed() const override;
    /**
     * @param[in] ring Candidate additional part exterior in intended target frame.
     * @return COPY of sole selected geometry set to declared Multi type with one new part appended,
     *   polygon part CCW-normalized; empty absent target/sole feature/non-Multi declared type.
     * @note No original selected feature mutation; inherited base commit adds returned copy as NEW
     *   feature/schema-default attributes. No specific refusal string generated here; base
     *   validation diagnoses empty result.
     */
    [[nodiscard]] openswmmvis::feature::FeatureGeometry
    buildGeometry(const openswmmvis::feature::Ring &ring) const override;

private:
    /**
     * @return Sole target selected stable ID, kInvalidFeatureId for null/zero/multiple selection;
     *   does not validate existence/editability or geometry.
     */
    [[nodiscard]] openswmmvis::feature::FeatureId selectedFeature() const;
};

/*!
 * \class OpenSWMMVisMapToolEditFeatureVertex
 * \brief Move, insert and delete vertices of the selected feature.
 */
class OpenSWMMVisMapToolEditFeatureVertex : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    /**
     * @brief Create inactive feature tool without installing on canvas or changing store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events/undo commands.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread only. Feature target uses QPointer; canvas and model snap source are
     *   borrowed. No automatic selected-target binding; host calls setTargetLayer.
     */
    explicit OpenSWMMVisMapToolEditFeatureVertex(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @param[in] layer Optional new target guarded by QPointer.
     * @brief On changed pointer reload sole selected geometry and clear hover/drag indices,
     *   invalidating Overlay. Does not roll back committed edit or notify cursor change.
     */
    void setTargetLayer(FeatureLayer *layer);
    /**
     * @return Borrowed guarded target or null, no ownership transfer/editability check.
     */
    [[nodiscard]] FeatureLayer *targetLayer() const { return m_target.data(); }

    /**
     * @return SizeAll when hover reference minimally valid, otherwise Cross; no geometry bounds
     *   check or widget mutation.
     */
    [[nodiscard]] QCursor cursor() const override;
    /**
     * @brief Reload sole selected feature/local geometry, clear hover/drag and redraw, then
     *   activate base.
     */
    void activate() override;
    /**
     * @brief Clear hover/drag references and dragged flag then deactivate base; local
     *   geometry/feature ID retained.
     * @note No explicit repaint/reload/cursor signal, uncommitted working copy remains until later
     *   activate/selection reload. Committed store geometry unchanged by local drag.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event if target editable; current sole selection
     *   checked/refreshed first.
     * @brief Right deletes nearest handle or inserts at raw clicked canvas point after nearby
     *   segment; left arms nearest handle drag.
     * @note Native target coordinates treated as canvas coordinates throughout, requiring
     *   compatible frames. Right insertion Z startsNaN; optional resampleOnEdit samples whole
     *   working geometry without densify. Discrete edits validate/push nonmergeable command, no
     *   modal context menu; local bounds failures return silently. No acceptance.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed move with canvas; active drag ignores button mask.
     * @brief Idle update nearest hover/cursorChanged; dragging moves local ring vertex to raw
     *   canvas-map point and redraws Overlay.
     * @note No engine/store write or command on motion, no snapping/reprojection/Z sample. Native
     *   and canvas frames must match. Does not recheck target editability/selection on every move.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Ignored borrowed event, may be null; button/position not read.
     * @brief If actual local drag occurred, optionally resample Z and push ONE mergeable geometry
     *   command, clear drag and invalidate Scene/Overlay.
     * @note Last motion point wins. Consecutive mergeable edits of SAME feature can collapse even
     *   across different vertices/drags; no per-vertex/time key. No command when no movement; no
     *   event acceptance.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed Escape event; other keys unchanged.
     * @brief Active drag restores local before geometry and clears drag; otherwise reload current
     *   sole selection. Then Overlay redraw.
     * @note No store undo, tool switch or key acceptance.
     */
    void keyPressEvent(QKeyEvent *event) override;
    /**
     * @param[in] painter Optional active widget-pixel painter; null/invalid feature ID no-op.
     * @param[in] canvasExtent Unused current extent.
     * @param[in] canvasSRS Unused borrowed current CRS.
     * @brief Draw8pixelsquarehandles for all exterior/hole rings, orange hovered/dragged otherwise
     *   blue, white outline.
     * @note Local native geometry passed directly to canvas pixel mapping, no reprojection.
     *   Saves/restores state, no DPR/transform reset, full geometry preview or store write.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /**
     * @param[in] reason FeatureGeometry validation reason before command push.
     * @note Does not report EditFeatureGeometryCommand/store failures (statuses are not inspected
     *   here). Working copy reset to before on emitted validation rejection.
     */
    void editRejected(const QString &reason);

private:
    /*! Reload \ref m_geom from the layer for the currently selected feature.
     * @note Cache sole selected ID or invalid, clear local geometry, load existing feature geometry
     *   if found, clear hover/drag and invalidate Overlay. Failed fetch retains selected ID but
     *   empty geometry; before snapshot/dragged flag not explicitly reset. No layer selection/store
     *   mutation.
     */
    void reloadSelection();
    /*! Handle within \p tolPx pixels of the widget position \p pos.
     * @param[in] pos Widget position for pixel-distance query.
     * @param[in] tolPx Inclusive nonnegative screen radius, default7.
     * @return Nearest handle within radius, first scan-order tie across parts/exterior/holes;
     *   invalid miss. Native points mapped directly as canvas, so compatible frames required. No
     *   mutation.
     */
    [[nodiscard]] FeatureVertexRef handleAt(const QPoint &pos, int tolPx = 7) const;
    /*! Segment (returns the vertex index BEFORE the split point) near \p pos.
     * @param[in] pos Widget position for pixel-segment query.
     * @param[in] tolPx Inclusive screen radius, default6.
     * @return Nearest segment reference identifying vertex BEFORE insert location, first tie;
     *   invalid no segment. Polygon exteriors/holes include closing segment, lines only consecutive
     *   edges. No mutation/reprojection.
     */
    [[nodiscard]] FeatureVertexRef segmentAt(const QPoint &pos, int tolPx = 6) const;
    /**
     * @param[in] ref Part/ring selector; any negative ring selects exterior, vertex index ignored.
     * @return Borrowed pointer into tool-local m_geom, null invalid part/hole; invalidated by
     *   geometry replacement/vector structural edit/destruction. Mutable overload permits local
     *   edits only. No layer write.
     */
    [[nodiscard]] openswmmvis::feature::Ring *ringFor(const FeatureVertexRef &ref);
    /**
     * @param[in] ref Part/ring selector; any negative ring selects exterior, vertex index ignored.
     * @return Borrowed pointer into tool-local m_geom, null invalid part/hole; invalidated by
     *   geometry replacement/vector structural edit/destruction. Const overload exposes read-only
     *   ring. No layer write.
     */
    [[nodiscard]] const openswmmvis::feature::Ring *ringFor(const FeatureVertexRef &ref) const;
    /*! Push an EditFeatureGeometryCommand from \ref m_before to \ref m_geom.
     * @param[in] text Undo-menu label copied to command.
     * @param[in] mergeable True gives feature-wide merge ID, false discrete edit remains separate.
     * @note Requires target/valid ID/canvas. Validate working geometry; rejection emits
     *   editRejected, restores before and redraws Overlay. Valid creates EditFeatureGeometryCommand
     *   before→working; stack applies, absent stack deletes without applying. Regardless of store
     *   write success, local before becomes working; command error not checked, so local/store can
     *   diverge until reload. No orientation normalization or direct repaint on success here.
     */
    void pushEdit(const QString &text, bool mergeable);

    QPointer<FeatureLayer> m_target;
    openswmmvis::feature::FeatureId m_featureId;
    openswmmvis::feature::FeatureGeometry m_geom;   ///< working copy
    openswmmvis::feature::FeatureGeometry m_before; ///< at drag start
    FeatureVertexRef m_hover;
    FeatureVertexRef m_dragging;
    bool m_dragged = false;
};

/*!
 * \class OpenSWMMVisMapToolMoveFeature
 * \brief Drag the whole selected feature.
 */
class OpenSWMMVisMapToolMoveFeature : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    /**
     * @brief Create inactive feature tool without installing on canvas or changing store.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events/undo commands.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread only. Feature target uses QPointer; canvas and model snap source are
     *   borrowed. No automatic selected-target binding; host calls setTargetLayer.
     */
    explicit OpenSWMMVisMapToolMoveFeature(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @param[in] layer Optional target guarded by QPointer.
     * @brief Assign target only, preserving any current movement/before snapshot/ID; caller changes
     *   outside gesture or deactivates first.
     */
    void setTargetLayer(FeatureLayer *layer);

    /**
     * @return Closed hand while moving flag true, otherwise open; no widget mutation.
     */
    [[nodiscard]] QCursor cursor() const override;
    /**
     * @brief Clear moving/feature ID and deactivate base; no store mutation, cursor signal or
     *   explicit repaint.
     */
    void deactivate() override;
    /**
     * @param[in] event Required borrowed event; left with editable target and exactly1selected
     *   feature needed.
     * @brief Snapshot selected full geometry and clicked canvas-map anchor, set moving and emit
     *   cursorChanged.
     * @note No hit-test against selected feature: press anywhere can move it. Missing
     *   selection/fetch no-op. Stored native geometry later shifted by canvas delta, so frames must
     *   match. No store write/acceptance at press.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Ignored borrowed event, may be null.
     * @brief While moving/live canvas, invalidate Overlay only; committed geometry stays unchanged
     *   and no translated live preview is built.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed release when moving and target/ID/canvas survive; button
     *   not checked.
     * @brief Stop moving/emit cursor change, translate all exterior/hole XY by release-anchor
     *   delta, optionally resample Z and push one nonmergeable command.
     * @note Fuzzy-zero delta skips command. Native geometry shifted by canvas-map delta without
     *   reprojection, all Z retained unless resampleOnEdit. No explicit geometry validation or
     *   command failure signal here. Absent stack deletes command without edit; Scene/Overlay
     *   invalidated after attempted push.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key; Escape only.
     * @brief Clear moving/feature ID and emit cursorChanged on Escape, without store undo, repaint,
     *   acceptance or tool switch.
     */
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPointer<FeatureLayer> m_target;
    openswmmvis::feature::FeatureId m_featureId;
    openswmmvis::feature::FeatureGeometry m_before;
    QPointF m_anchor;
    bool m_moving = false;
};

#endif // MAPTOOLFEATUREEDIT_H
