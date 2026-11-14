/**
 * @file      openswmmvisgraphicsview.cpp
 * @brief     raster buffer, hosting interactive vector items in scene coordinates.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QGraphicsScene>
#include <QPainter>

#include "map/openswmmvisgraphicsview.h"
#include "map/openswmmvisscene.h"

OpenSWMMVisGraphicsView::OpenSWMMVisGraphicsView(OpenSWMMVisScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    // IMPORTANT: do NOT use an OpenGL viewport here.
    // The MapCanvas keeps this view *hidden* and renders its scene via
    // QWidget::render() into a CPU QImage. An OpenGL viewport requires an
    // active GL context that never exists for a never-shown widget, so
    // QWidget::render() would produce an empty image and vector items would
    // not appear in the canvas. The default raster viewport works correctly.

    setInteractive(true);
    setAcceptDrops(true);

    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setRenderHint(QPainter::VerticalSubpixelPositioning, true);
    setRenderHint(QPainter::LosslessImageRendering, true);

    setViewportUpdateMode(QGraphicsView::ViewportUpdateMode::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::ViewportAnchor::NoAnchor);

    setDragMode(QGraphicsView::DragMode::RubberBandDrag);
    setRubberBandSelectionMode(Qt::ItemSelectionMode::IntersectsItemShape);

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setCacheMode(QGraphicsView::CacheNone);
    setFocusPolicy(Qt::WheelFocus);
    setMouseTracking(true);
}

OpenSWMMVisGraphicsView::OpenSWMMVisGraphicsView(QWidget *parent) : QGraphicsView(parent) {}

OpenSWMMVisGraphicsView::~OpenSWMMVisGraphicsView() {}
