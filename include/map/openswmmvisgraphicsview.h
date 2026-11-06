/**
 * @file      openswmmvisgraphicsview.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Transparent QGraphicsView overlay that sits on top of the MapCanvas
 *         raster buffer, hosting interactive vector items in scene coordinates.
 *
 * @details OpenSWMMVisGraphicsView is a thin subclass of QGraphicsView used by
 *          the MapCanvas two-layer compositing strategy.  The scene-taking constructor configures a CPU raster viewport; the
 *          MapCanvas host manages hidden capture/compositing and background
 *          visibility. The constructors do not set WA_TranslucentBackground.  Mouse and keyboard events
 *          are forwarded to the active map tool rather than processed directly
 *          by the view.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
     * @note Scene is borrowed/managed by its own QObject owner; view does not take ownership
     *   through setScene constructor. Configures raster viewport rendering hints, full updates, no
     *   scrollbars/cache, NoAnchor, rubberband intersection selection, focus/mouse tracking/drops.
     *   Host normally keeps view hidden for CPU image capture; do not substitute GL viewport
     *   without adapting capture context. No CRS/Y transform or explicit translucent-background
     *   attribute is installed here. GUI thread only.
     */
    OpenSWMMVisGraphicsView(OpenSWMMVisScene *scene, QWidget *parent = nullptr);

    /*!
     * \brief Default constructor without a pre-created scene.
     * \param parent  Qt parent widget.
     * @note Uses Qt default QGraphicsView configuration without scene; does NOT run the custom
     *   scene-constructor render/selection/scrollbar setup. Caller supplies/owns scene later.
     */
    explicit OpenSWMMVisGraphicsView(QWidget *parent = nullptr);

    /*!
     * \brief Destructor.
     * @note Qt destroys owned viewport/widgets; separately owned scene and layer model remain
     *   externally managed. No worker wait/custom tool cleanup.
     */
    virtual ~OpenSWMMVisGraphicsView();
};

#endif // SWMMVISGRAPHICSVIEW_H
