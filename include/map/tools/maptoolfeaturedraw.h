/**
 * @file      maptoolfeaturedraw.h
 * @brief     Digitising tools that draw into a FeatureLayer
 *            (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.3).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Digitising tools that draw into a FeatureLayer
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.3).
 *
 * The interaction contract is copied verbatim from
 * OpenSWMMVisMapToolAddSubcatchment (maptooladdsubcatchment.h:23-29) so a user
 * who can draw a subcatchment can draw a feature without learning anything:
 *
 *   left click     append a vertex (snapped)
 *   right click    remove the last vertex; cancel when only one remains
 *   double click   commit, INCLUDING the double-clicked point as the final
 *                  vertex (Qt sends only one press per double-click pair, so
 *                  there is no duplicate to drop)
 *   Return / Enter commit
 *   Escape         cancel
 *
 * Vertices are accumulated in CANVAS CRS, because the rubber-band paint runs
 * through toPixelCoords which expects canvas coordinates; commit() invokes the canvasToLayer hook,
 *   which currently returns coordinates unchanged. That is the same split the subcatchment tool
 *   documents at
 * maptooladdsubcatchment.cpp:86-95 — the difference is that a FeatureLayer
 * declares its own CRS, so the conversion goes through the layer's transform
 * rather than SWMMModelLayer::transformCanvasToLayer.
 *
 * The tools never write to the layer directly: every commit pushes an
 * AddFeatureCommand onto the canvas undo stack (CLAUDE.md §5.1 — the tool is
 * the controller, the layer is the model).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MAPTOOLFEATUREDRAW_H
#define MAPTOOLFEATUREDRAW_H

#include "map/tools/maptool.h"
#include "map/snapengine.h"
#include "feature/featuregeometry.h"
#include "feature/featuretypes.h"

#include <QPointF>
#include <QPointer>
#include <QVector>

class FeatureLayer;

/*!
 * \class OpenSWMMVisMapToolFeatureDrawBase
 * \brief Shared vertex accumulation, rubber band, snapping and commit.
 *
 * Abstract shared implementation; subclasses supply minimumVertices and buildGeometry.
 */
class OpenSWMMVisMapToolFeatureDrawBase : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /**
     * @brief Create inactive feature tool without installing on canvas or changing store.
     * @param[in] toolName Human-readable identity copied into base tool.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events/undo commands.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread only. Feature target uses QPointer; canvas and model snap source are
     *   borrowed. No automatic selected-target binding; host calls setTargetLayer.
     */
    OpenSWMMVisMapToolFeatureDrawBase(const QString &toolName, MapCanvas *canvas,
                                      QObject *parent = nullptr);

    /*! \brief The layer being drawn into. Set by the Features dock; when it is
     *         null or not editable the tool ignores input.
     * @param[in] layer Optional borrowed editable FeatureLayer, guarded against QObject deletion.
     * @note Equal target no-op; otherwise cancel pending shape/Overlay BEFORE replacing pointer.
     *   Does not change layer selection, install tool, signal target change or own layer.
     */
    void setTargetLayer(FeatureLayer *layer);
    /**
     * @return Borrowed guarded target or null after deletion, without editability/attachment/CRS
     *   validation.
     */
    [[nodiscard]] FeatureLayer *targetLayer() const { return m_target.data(); }

    /**
     * @return Crosshair regardless of drawing/target readiness, no widget mutation.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Cancel pending vertices/snaps and invalidate Overlay, then activate base. Does not
     *   undo committed features or change target.
     */
    void activate() override;
    /**
     * @brief Cancel pending vertices/snaps and invalidate Overlay, then deactivate base. Does not
     *   undo committed features or change target.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed press with canvas and editable target; left/right handled.
     * @brief Append snapped canvas-frame vertex on left; right removes last if>1, otherwise
     *   cancels.
     * @note First left refreshes snap, later left reuses last mouse-move candidate. Snap source is
     *   FIRST model layer (regardless visibility), not feature geometry; native snap projected back
     *   to canvas with ignored status. No event acceptance, geometry validation or direct store
     *   write.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed move when canvas exists, even without editable/nonnull
     *   target.
     * @brief Refresh first-model snap and canvas cursor then invalidate Overlay; no vertex append
     *   or store mutation.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed left event with canvas; target editability checked at
     *   commit.
     * @brief Fresh-snap endpoint, drop prior trailing point within2pixelper-axis of raw click,
     *   append once and commit.
     * @note No drawing-state requirement; target/missing/minimum count refusal handled by commit.
     *   Does not accept event.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key; Escape cancel, Enter/Return commit existing list.
     * @brief Cancel/commit without appending rubber cursor or switching tool; no event
     *   acceptance/base forwarding.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @param[in] painter Optional active widget-pixel painter; null/idle/empty no-op.
     * @param[in] canvasExtent Unused extent.
     * @param[in] canvasSRS Unused borrowed CRS.
     * @brief Draw blue dashed line/filled closed preview, vertex dots/live cursor and generic snap
     *   ring.
     * @note Closure controlled by virtual isClosed. Saves/restores state, no DPR/transform reset.
     *   Generic ring directly maps SnapEngine native result, so compatible snap/canvas frame
     *   required; no store mutation or repaint request.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted after a successful commit, with the new feature's id.
     * @param[in] featureId Stable ID returned by successfully applied AddFeatureCommand.
     * @note Emitted after stack push only when command lastError empty, not when no stack or
     *   geometry invalid. New feature uses schema default attributes; no automatic selection of it.
     */
    void featureDrawn(qint64 featureId);
    /*! \brief Emitted when a commit is refused, with the reason from
     *         FeatureGeometry::validate. The Features dock shows it.
     * @param[in] reason Geometry-validation reason OR AddFeatureCommand failure string.
     * @note Missing/noneditable target, too few points or no stack silently cancels without this
     *   signal. Emission synchronous, command/store may retain their own failure state.
     */
    void drawRejected(const QString &reason);

protected:
    /*! Minimum vertices before commit() will do anything.
     * @return Required accumulated point count for subtype before commit; pure virtual, not a
     *   distinct/valid-geometry count.
     */
    [[nodiscard]] virtual int minimumVertices() const = 0;
    /*! Close the ring in the rubber-band preview.
     * @return False in base (open preview); derived polygon/hole/part tools override. Does not
     *   itself close stored ring.
     */
    [[nodiscard]] virtual bool isClosed() const { return false; }
    /*! Build the geometry to store from the accumulated LAYER-CRS vertices.
     * @param[in] ring Accumulated points after canvasToLayer hook, with no Z sampled yet.
     * @return Value geometry for later validation/Z sampling/new-feature insertion; pure virtual.
     *   No direct layer mutation; caller owns returned value.
     */
    [[nodiscard]] virtual openswmmvis::feature::FeatureGeometry
    buildGeometry(const openswmmvis::feature::Ring &ring) const = 0;

    /**
     * @brief Clear vertex/drawing/snap state and invalidate Overlay, retaining
     *   target/configuration/cursor value. No store rollback or cancellation signal.
     */
    void cancel();
    /**
     * @brief Build/validate/normalize target geometry, optionally sample/densify Z and enqueue NEW
     *   AddFeatureCommand with schema defaults.
     * @note Noneditable/missing target or too few vertices cancels silently. canvasToLayer
     *   currently identity even differing CRS, so caller ensures target geometry frame matches
     *   canvas. 3D layer sampleZ runs before validation, unsampled count ignored. Invalid geometry
     *   emits drawRejected then cancels.
     * @note Stack push runs insertion; command error emits drawRejected, otherwise featureDrawn. No
     *   stack deletes command without applying/signalling. Always cancel and Scene/Overlay
     *   invalidate after viable command attempt. AddHole/AddPart inherit this NEW-feature insertion
     *   behavior; selected original is not replaced.
     */
    void commit();

    /*! Canvas CRS → the target layer's CRS. Identity when the layer declares
     *  no CRS, which is exactly what GISVectorLayer assumes for such a layer
     *  (see its crsAssumed signal).
     * @param[in] canvasPt Canvas-map point accumulated for shape.
     * @return Input point unchanged in ALL current branches, including nonidentical declared
     *   target/canvas CRSs. This is a placeholder hook, not a reprojection; caller must supply
     *   compatible coordinate frames. No mutation/error indicator.
     */
    [[nodiscard]] QPointF canvasToLayer(const QPointF &canvasPt) const;

    QPointer<FeatureLayer> m_target;
    QVector<QPointF> m_vertices; ///< canvas CRS
    QPointF m_cursor;            ///< canvas CRS
    bool m_drawing = false;
    SnapEngine::Result m_snap;
};

/*! \brief Click once to place a point feature. */
class OpenSWMMVisMapToolDrawPoint : public OpenSWMMVisMapToolFeatureDrawBase
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
    explicit OpenSWMMVisMapToolDrawPoint(MapCanvas *canvas, QObject *parent = nullptr);
    /**
     * @param[in] event Required borrowed event; base eligibility/append path first.
     * @brief Delegate normal draw press then immediately commit any accumulated point on left.
     * @note Right still removes/cancels through base; no new release arming. No stack means no
     *   insertion.
     */
    void mousePressEvent(QMouseEvent *event) override;

protected:
    /**
     * @return 1 accumulated entries required; full target-type/topological validation happens in
     *   base commit.
     */
    [[nodiscard]] int minimumVertices() const override { return 1; }
    /**
     * @param[in] ring Caller-owned shape points in intended target frame; no direct store edit.
     * @return One-part Point/MultiPoint value, choosing Multi only when target declares matching
     *   Multi type, otherwise simple default. Uses first point only, origin if empty. Base commit
     *   later validates and adds NEW feature.
     */
    [[nodiscard]] openswmmvis::feature::FeatureGeometry
    buildGeometry(const openswmmvis::feature::Ring &ring) const override;
};

/*! \brief Click to add vertices, double-click or Enter to finish a polyline. */
class OpenSWMMVisMapToolDrawLine : public OpenSWMMVisMapToolFeatureDrawBase
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
    explicit OpenSWMMVisMapToolDrawLine(MapCanvas *canvas, QObject *parent = nullptr);

protected:
    /**
     * @return 2 accumulated entries required; full target-type/topological validation happens in
     *   base commit.
     */
    [[nodiscard]] int minimumVertices() const override { return 2; }
    /**
     * @param[in] ring Caller-owned shape points in intended target frame; no direct store edit.
     * @return One-part LineString/MultiLineString value, choosing Multi only when target declares
     *   matching Multi type, otherwise simple default. Copies entire ring as exterior without
     *   validation/orientation/Z sampling. Base commit later validates and adds NEW feature.
     */
    [[nodiscard]] openswmmvis::feature::FeatureGeometry
    buildGeometry(const openswmmvis::feature::Ring &ring) const override;
};

/*! \brief Click to add vertices, double-click or Enter to close a polygon. */
class OpenSWMMVisMapToolDrawPolygon : public OpenSWMMVisMapToolFeatureDrawBase
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
    explicit OpenSWMMVisMapToolDrawPolygon(MapCanvas *canvas, QObject *parent = nullptr);

protected:
    /**
     * @return 3 accumulated entries required; full target-type/topological validation happens in
     *   base commit.
     */
    [[nodiscard]] int minimumVertices() const override { return 3; }
    /**
     * @return True for filled/closed rubber preview; stored ring still goes through base geometry
     *   normalization.
     */
    [[nodiscard]] bool isClosed() const override { return true; }
    /**
     * @param[in] ring Caller-owned shape points in intended target frame; no direct store edit.
     * @return One-part Polygon/MultiPolygon value, choosing Multi only when target declares
     *   matching Multi type, otherwise simple default. Copies entire ring as exterior without
     *   validation/orientation/Z sampling. Base commit later validates and adds NEW feature.
     */
    [[nodiscard]] openswmmvis::feature::FeatureGeometry
    buildGeometry(const openswmmvis::feature::Ring &ring) const override;
};

#endif // MAPTOOLFEATUREDRAW_H
