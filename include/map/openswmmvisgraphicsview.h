/*!
 * \file   openswmmvisgraphicsview.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Transparent QGraphicsView overlay that sits on top of the MapCanvas
 *         raster buffer, hosting interactive vector items in scene coordinates.
 *
 * \details OpenSWMMVisGraphicsView is a thin subclass of QGraphicsView used by
 *          the MapCanvas two-layer compositing strategy.  It is made transparent
 *          (Qt::WA_TranslucentBackground) so the raster buffer painted in the
 *          parent widget remains visible underneath.  Mouse and keyboard events
 *          are forwarded to the active map tool rather than processed directly
 *          by the view.
 */
#ifndef SWMMVISGRAPHICSVIEW_H
#define SWMMVISGRAPHICSVIEW_H

#include <QGraphicsView>

class OpenSWMMVisScene;

/*!
 * \class OpenSWMMVisGraphicsView
 * \brief Transparent QGraphicsView overlay for interactive vector content in
 *        the MapCanvas.
 *
 * \details The view is stacked on top of the MapCanvas widget and sized to
 *          cover it exactly.  The scene coordinate system uses map CRS
 *          coordinates (Y-up) so that items added to the scene already sit in
 *          the correct geographic location without any per-item transform.
 *
 *          The MapCanvas is responsible for keeping the view's transform
 *          synchronised with the current extent via applyExtentToOverlay().
 */
class OpenSWMMVisGraphicsView : public QGraphicsView
{
    Q_OBJECT

public:

    /*!
     * \brief Constructs a view displaying \p scene.
     * \param scene   Shared scene hosting all layer items.
     * \param parent  Qt parent widget (typically the MapCanvas).
     */
    OpenSWMMVisGraphicsView(OpenSWMMVisScene *scene, QWidget *parent = nullptr);

    /*!
     * \brief Default constructor without a pre-created scene.
     * \param parent  Qt parent widget.
     */
    explicit OpenSWMMVisGraphicsView(QWidget *parent = nullptr);

    /*!
     * \brief Destructor.
     */
    virtual ~OpenSWMMVisGraphicsView();

};

#endif // SWMMVISGRAPHICSVIEW_H
