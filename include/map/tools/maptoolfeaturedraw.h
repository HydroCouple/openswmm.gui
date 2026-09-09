/*!
 * \file   maptoolfeaturedraw.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 * through toPixelCoords which expects canvas coordinates; commit() converts
 * once. That is the same split the subcatchment tool documents at
 * maptooladdsubcatchment.cpp:86-95 — the difference is that a FeatureLayer
 * declares its own CRS, so the conversion goes through the layer's transform
 * rather than SWMMModelLayer::transformCanvasToLayer.
 *
 * The tools never write to the layer directly: every commit pushes an
 * AddFeatureCommand onto the canvas undo stack (CLAUDE.md §5.1 — the tool is
 * the controller, the layer is the model).
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
 * Not abstract in the C++ sense — it draws polylines — but it is not meant to
 * be instantiated directly; use one of the three subclasses.
 */
class OpenSWMMVisMapToolFeatureDrawBase : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    OpenSWMMVisMapToolFeatureDrawBase(const QString &toolName,
                                      MapCanvas *canvas,
                                      QObject *parent = nullptr);

    /*! \brief The layer being drawn into. Set by the Features dock; when it is
     *         null or not editable the tool ignores input. */
    void setTargetLayer(FeatureLayer *layer);
    [[nodiscard]] FeatureLayer *targetLayer() const { return m_target.data(); }

    [[nodiscard]] QCursor cursor() const override;

    void activate() override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted after a successful commit, with the new feature's id. */
    void featureDrawn(qint64 featureId);
    /*! \brief Emitted when a commit is refused, with the reason from
     *         FeatureGeometry::validate. The Features dock shows it. */
    void drawRejected(const QString &reason);

protected:
    /*! Minimum vertices before commit() will do anything. */
    [[nodiscard]] virtual int minimumVertices() const = 0;
    /*! Close the ring in the rubber-band preview. */
    [[nodiscard]] virtual bool isClosed() const { return false; }
    /*! Build the geometry to store from the accumulated LAYER-CRS vertices. */
    [[nodiscard]] virtual openswmmvis::feature::FeatureGeometry
        buildGeometry(const openswmmvis::feature::Ring &ring) const = 0;

    void cancel();
    void commit();

    /*! Canvas CRS → the target layer's CRS. Identity when the layer declares
     *  no CRS, which is exactly what GISVectorLayer assumes for such a layer
     *  (see its crsAssumed signal). */
    [[nodiscard]] QPointF canvasToLayer(const QPointF &canvasPt) const;

    QPointer<FeatureLayer> m_target;
    QVector<QPointF>       m_vertices;   ///< canvas CRS
    QPointF                m_cursor;     ///< canvas CRS
    bool                   m_drawing = false;
    SnapEngine::Result     m_snap;
};

/*! \brief Click once to place a point feature. */
class OpenSWMMVisMapToolDrawPoint : public OpenSWMMVisMapToolFeatureDrawBase
{
    Q_OBJECT
public:
    explicit OpenSWMMVisMapToolDrawPoint(MapCanvas *canvas, QObject *parent = nullptr);
    void mousePressEvent(QMouseEvent *event) override;

protected:
    [[nodiscard]] int minimumVertices() const override { return 1; }
    [[nodiscard]] openswmmvis::feature::FeatureGeometry
        buildGeometry(const openswmmvis::feature::Ring &ring) const override;
};

/*! \brief Click to add vertices, double-click or Enter to finish a polyline. */
class OpenSWMMVisMapToolDrawLine : public OpenSWMMVisMapToolFeatureDrawBase
{
    Q_OBJECT
public:
    explicit OpenSWMMVisMapToolDrawLine(MapCanvas *canvas, QObject *parent = nullptr);

protected:
    [[nodiscard]] int minimumVertices() const override { return 2; }
    [[nodiscard]] openswmmvis::feature::FeatureGeometry
        buildGeometry(const openswmmvis::feature::Ring &ring) const override;
};

/*! \brief Click to add vertices, double-click or Enter to close a polygon. */
class OpenSWMMVisMapToolDrawPolygon : public OpenSWMMVisMapToolFeatureDrawBase
{
    Q_OBJECT
public:
    explicit OpenSWMMVisMapToolDrawPolygon(MapCanvas *canvas, QObject *parent = nullptr);

protected:
    [[nodiscard]] int  minimumVertices() const override { return 3; }
    [[nodiscard]] bool isClosed() const override { return true; }
    [[nodiscard]] openswmmvis::feature::FeatureGeometry
        buildGeometry(const openswmmvis::feature::Ring &ring) const override;
};

#endif // MAPTOOLFEATUREDRAW_H
