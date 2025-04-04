/*!
 * \file   swmmvisgraphicsview.cpp
 * \author Caleb Buahin <buahin.caleb@epa.gov>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2024
 * \pre
 * \bug
 * \warning
 * \todo
 */

#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QSurfaceFormat>
#include <QGraphicsScene>
#include <QPainter>

#include "swmmvisgraphicsview.h"
#include "swmmvisscene.h"

SWMMVisGraphicsView::SWMMVisGraphicsView(SWMMVisScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{

    QOpenGLWidget *gl = new QOpenGLWidget(this);
    QSurfaceFormat format;
    gl->setFormat(format);
    setViewport(gl);

    format.setSamples(4);
    format.setSwapBehavior(QSurfaceFormat::SwapBehavior::DoubleBuffer);

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

SWMMVisGraphicsView::SWMMVisGraphicsView(QWidget *parent)
    : QGraphicsView(parent)
{

}

SWMMVisGraphicsView::~SWMMVisGraphicsView()
{

}

