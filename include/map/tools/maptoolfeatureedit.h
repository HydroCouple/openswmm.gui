/*!
 * \file   maptoolfeatureedit.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 * Every committed change goes through EditFeatureGeometryCommand. A drag emits
 * one mergeable command per motion so the whole drag collapses into a single
 * undo step (the MoveNodeCommand::mergeWith idiom); discrete edits (insert,
 * delete, add hole, add part) are pushed non-mergeable so they stand alone.
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
struct FeatureVertexRef
{
    int part   = -1;
    int ring   = -1;
    int vertex = -1;

    [[nodiscard]] bool isValid() const { return part >= 0 && vertex >= 0; }
    void clear() { part = -1; ring = -1; vertex = -1; }
    [[nodiscard]] bool operator==(const FeatureVertexRef &o) const
    { return part == o.part && ring == o.ring && vertex == o.vertex; }
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
    explicit OpenSWMMVisMapToolAddHole(MapCanvas *canvas, QObject *parent = nullptr);

protected:
    [[nodiscard]] int  minimumVertices() const override { return 3; }
    [[nodiscard]] bool isClosed() const override { return true; }
    [[nodiscard]] openswmmvis::feature::FeatureGeometry
        buildGeometry(const openswmmvis::feature::Ring &ring) const override;

private:
    /*! The single selected feature, or kInvalidFeatureId when the selection is
     *  empty or ambiguous. */
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
    explicit OpenSWMMVisMapToolAddPart(MapCanvas *canvas, QObject *parent = nullptr);

protected:
    [[nodiscard]] int  minimumVertices() const override;
    [[nodiscard]] bool isClosed() const override;
    [[nodiscard]] openswmmvis::feature::FeatureGeometry
        buildGeometry(const openswmmvis::feature::Ring &ring) const override;

private:
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
    explicit OpenSWMMVisMapToolEditFeatureVertex(MapCanvas *canvas,
                                                 QObject *parent = nullptr);

    void setTargetLayer(FeatureLayer *layer);
    [[nodiscard]] FeatureLayer *targetLayer() const { return m_target.data(); }

    [[nodiscard]] QCursor cursor() const override;
    void activate() override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    void editRejected(const QString &reason);

private:
    /*! Reload \ref m_geom from the layer for the currently selected feature. */
    void reloadSelection();
    /*! Handle within \p tolPx pixels of the widget position \p pos. */
    [[nodiscard]] FeatureVertexRef handleAt(const QPoint &pos, int tolPx = 7) const;
    /*! Segment (returns the vertex index BEFORE the split point) near \p pos. */
    [[nodiscard]] FeatureVertexRef segmentAt(const QPoint &pos, int tolPx = 6) const;
    [[nodiscard]] openswmmvis::feature::Ring *ringFor(const FeatureVertexRef &ref);
    [[nodiscard]] const openswmmvis::feature::Ring *
        ringFor(const FeatureVertexRef &ref) const;
    /*! Push an EditFeatureGeometryCommand from \ref m_before to \ref m_geom. */
    void pushEdit(const QString &text, bool mergeable);

    QPointer<FeatureLayer>                 m_target;
    openswmmvis::feature::FeatureId        m_featureId;
    openswmmvis::feature::FeatureGeometry  m_geom;    ///< working copy
    openswmmvis::feature::FeatureGeometry  m_before;  ///< at drag start
    FeatureVertexRef                       m_hover;
    FeatureVertexRef                       m_dragging;
    bool                                   m_dragged = false;
};

/*!
 * \class OpenSWMMVisMapToolMoveFeature
 * \brief Drag the whole selected feature.
 */
class OpenSWMMVisMapToolMoveFeature : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    explicit OpenSWMMVisMapToolMoveFeature(MapCanvas *canvas, QObject *parent = nullptr);

    void setTargetLayer(FeatureLayer *layer);

    [[nodiscard]] QCursor cursor() const override;
    void deactivate() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPointer<FeatureLayer>                m_target;
    openswmmvis::feature::FeatureId       m_featureId;
    openswmmvis::feature::FeatureGeometry m_before;
    QPointF                               m_anchor;
    bool                                  m_moving = false;
};

#endif // MAPTOOLFEATUREEDIT_H
