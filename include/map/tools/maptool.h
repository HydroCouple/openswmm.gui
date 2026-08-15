/*!
 * \file   maptool.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Abstract base class for all interactive tools attached to a MapCanvas.
 */

#ifndef MAPTOOL_H
#define MAPTOOL_H

#include <QObject>
#include <QCursor>
#include <QString>

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QPainter;
class QGraphicsScene;
class MapCanvas;
class MapExtent;
class SpatialReferenceSystem;

/*!
 * \class OpenSWMMVisMapTool
 * \brief Abstract base class for interactive map tools used in the MapCanvas.
 * \details A map tool handles mouse, wheel, and keyboard events on behalf of the
 *          MapCanvas.  Exactly one tool is active at a time.  The canvas calls
 *          activate() / deactivate() when switching tools.
 *
 *          Tools can also draw their own transient graphics on top of the map
 *          by overriding paint().
 *
 *          Concrete tool subclasses:
 *          - OpenSWMMVisMapToolPan    — pan by dragging
 *          - OpenSWMMVisMapToolZoom   — zoom in/out by rubber-band or wheel
 *          - OpenSWMMVisMapToolMeasure — measure distance or area
 *          - OpenSWMMVisMapToolSelect  — select features by click or rubber-band
 *          - OpenSWMMVisMapToolIdentify — identify (query) features at a point
 */
class OpenSWMMVisMapTool : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString toolName READ toolName CONSTANT)
    Q_PROPERTY(QCursor cursor   READ cursor   NOTIFY cursorChanged)
    Q_PROPERTY(bool    active   READ isActive NOTIFY activeChanged)

public:

    explicit OpenSWMMVisMapTool(const QString &toolName,
                            MapCanvas *canvas,
                            QObject *parent = nullptr);

    virtual ~OpenSWMMVisMapTool() = default;

    // ----- Identity -------------------------------------------------------

    /*!
     * \brief Returns the human-readable name of this tool.
     */
    [[nodiscard]] QString toolName() const;

    /*!
     * \brief Returns the mouse cursor to display while this tool is active.
     */
    [[nodiscard]] virtual QCursor cursor() const;

    /*!
     * \brief Returns true when this tool is the active tool on its canvas.
     */
    [[nodiscard]] bool isActive() const;

    // ----- Canvas access --------------------------------------------------

    /*!
     * \brief Returns the MapCanvas this tool is attached to.
     */
    [[nodiscard]] MapCanvas *canvas() const;

    // ----- Lifecycle hooks ------------------------------------------------

    /*!
     * \brief Called by the canvas when this tool becomes the active tool.
     */
    virtual void activate();

    /*!
     * \brief Called by the canvas when another tool replaces this one.
     */
    virtual void deactivate();

    // ----- Event handlers (override in subclasses) ------------------------

    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);
    virtual void mouseReleaseEvent(QMouseEvent *event);
    virtual void mouseDoubleClickEvent(QMouseEvent *event);
    virtual void wheelEvent(QWheelEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);
    virtual void keyReleaseEvent(QKeyEvent *event);

    // ----- Painting (via scene items) --------------------------------------

    /*!
     * \brief Called by the canvas in drawForeground() so the tool can draw
     *        transient overlay graphics (rubber-band, measure segments, etc.).
     * \details Override this to paint tool overlays directly via QPainter in
     *          **view coordinates**.
     */
    virtual void paint(QPainter *painter,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS);

    /*!
     * \brief Returns the scene this tool's canvas is using.
     */
    [[nodiscard]] QGraphicsScene *scene() const;

    // ----- Helpers provided to subclasses --------------------------------

    /*!
     * \brief Converts a widget pixel position to map coordinates.
     * \param px  Pixel X in widget space.
     * \param py  Pixel Y in widget space.
     * \param mapX Output map X.
     * \param mapY Output map Y.
     */
    void toMapCoords(int px, int py, double &mapX, double &mapY) const;

    /*!
     * \brief Converts map coordinates to widget pixel position.
     */
    void toPixelCoords(double mapX, double mapY, int &px, int &py) const;

signals:
    void cursorChanged(const QCursor &cursor);
    void activeChanged(bool active);

    /*!
     * \brief Emitted to instruct the canvas to repaint its overlay.
     */
    void repaintRequested();

protected:
    QString     m_toolName;
    MapCanvas  *m_canvas = nullptr;

private:
    bool        m_active = false;

    friend class MapCanvas;
    void setActive(bool active);
};

#endif // MAPTOOL_H
