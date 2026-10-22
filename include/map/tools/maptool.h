/**
 * @file      maptool.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Abstract base class for all interactive tools attached to a MapCanvas.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    Q_PROPERTY(QCursor cursor READ cursor NOTIFY cursorChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)

public:
    /**
     * @param[in] toolName Human-readable immutable identity copied into tool.
     * @param[in] canvas Optional borrowed raw canvas; must outlive tool and event/paint use.
     *   QObject parent ownership is separate.
     * @param[in] parent Optional QObject owner; caller owns tool if null.
     * @note GUI-thread only. Creates inactive base tool; does not install itself on canvas or wire
     *   repaint signals. Canvas manages activation/deactivation and cursor. Raw canvas pointer is
     *   not deletion-guarded.
     */
    explicit OpenSWMMVisMapTool(const QString &toolName, MapCanvas *canvas,
                                QObject *parent = nullptr);

    /**
     * @brief Destroy QObject-owned connections/children; no explicit canvas deactivation or
     *   active-tool restoration.
     * @note Host must detach active tool and preserve canvas lifetime before destruction; base
     *   holds raw pointer.
     */
    virtual ~OpenSWMMVisMapTool() = default;

    // ----- Identity -------------------------------------------------------

    /*!
     * \brief Returns the human-readable name of this tool.
     * @return Constructor-provided human-readable identity by value; no translation or mutation
     *   here.
     */
    [[nodiscard]] QString toolName() const;

    /*!
     * \brief Returns the mouse cursor to display while this tool is active.
     * @return Arrow cursor in base; derived tools return mode-specific cursor. Does not set widget
     *   cursor or emit cursorChanged.
     */
    [[nodiscard]] virtual QCursor cursor() const;

    /*!
     * \brief Returns true when this tool is the active tool on its canvas.
     * @return Stored active flag, initially false; may differ from canvas pointer state if
     *   activate/deactivate called directly. No live canvas query.
     */
    [[nodiscard]] bool isActive() const;

    // ----- Canvas access --------------------------------------------------

    /*!
     * \brief Returns the MapCanvas this tool is attached to.
     * @return Borrowed raw canvas or null, with no lifetime guard, ownership transfer or
     *   active-tool check.
     */
    [[nodiscard]] MapCanvas *canvas() const;

    // ----- Lifecycle hooks ------------------------------------------------

    /*!
     * \brief Called by the canvas when this tool becomes the active tool.
     * @note Base sets stored active=true and emits activeChanged only on transition. Does not
     *   install tool, set cursor or repaint; MapCanvas::setActiveTool performs host setup.
     */
    virtual void activate();

    /*!
     * \brief Called by the canvas when another tool replaces this one.
     * @note Base sets stored active=false and emits activeChanged only on transition; no
     *   canvas/overlay cleanup or previous-tool restoration.
     */
    virtual void deactivate();

    // ----- Event handlers (override in subclasses) ------------------------

    /**
     * @brief Base event hook leaves event and canvas unchanged for derived implementation.
     * @param[in] event Borrowed Qt event, may be null for base because it is not inspected; derived
     *   hooks document requirements.
     * @note Base neither accepts/ignores nor forwards event; host decides fallback dispatch.
     *   GUI-thread call, no retained event ownership.
     */
    virtual void mousePressEvent(QMouseEvent *event);
    /**
     * @brief Base event hook leaves event and canvas unchanged for derived implementation.
     * @param[in] event Borrowed Qt event, may be null for base because it is not inspected; derived
     *   hooks document requirements.
     * @note Base neither accepts/ignores nor forwards event; host decides fallback dispatch.
     *   GUI-thread call, no retained event ownership.
     */
    virtual void mouseMoveEvent(QMouseEvent *event);
    /**
     * @brief Base event hook leaves event and canvas unchanged for derived implementation.
     * @param[in] event Borrowed Qt event, may be null for base because it is not inspected; derived
     *   hooks document requirements.
     * @note Base neither accepts/ignores nor forwards event; host decides fallback dispatch.
     *   GUI-thread call, no retained event ownership.
     */
    virtual void mouseReleaseEvent(QMouseEvent *event);
    /**
     * @brief Base event hook leaves event and canvas unchanged for derived implementation.
     * @param[in] event Borrowed Qt event, may be null for base because it is not inspected; derived
     *   hooks document requirements.
     * @note Base neither accepts/ignores nor forwards event; host decides fallback dispatch.
     *   GUI-thread call, no retained event ownership.
     */
    virtual void mouseDoubleClickEvent(QMouseEvent *event);
    /**
     * @brief Base event hook leaves event and canvas unchanged for derived implementation.
     * @param[in] event Borrowed Qt event, may be null for base because it is not inspected; derived
     *   hooks document requirements.
     * @note Base neither accepts/ignores nor forwards event; host decides fallback dispatch.
     *   GUI-thread call, no retained event ownership.
     */
    virtual void wheelEvent(QWheelEvent *event);
    /**
     * @brief Base event hook leaves event and canvas unchanged for derived implementation.
     * @param[in] event Borrowed Qt event, may be null for base because it is not inspected; derived
     *   hooks document requirements.
     * @note Base neither accepts/ignores nor forwards event; host decides fallback dispatch.
     *   GUI-thread call, no retained event ownership.
     */
    virtual void keyPressEvent(QKeyEvent *event);
    /**
     * @brief Base event hook leaves event and canvas unchanged for derived implementation.
     * @param[in] event Borrowed Qt event, may be null for base because it is not inspected; derived
     *   hooks document requirements.
     * @note Base neither accepts/ignores nor forwards event; host decides fallback dispatch.
     *   GUI-thread call, no retained event ownership.
     */
    virtual void keyReleaseEvent(QKeyEvent *event);

    // ----- Painting (via scene items) --------------------------------------

    /*!
     * \brief Called by the canvas in drawForeground() so the tool can draw
     *        transient overlay graphics (rubber-band, measure segments, etc.).
     * \details Override this to paint tool overlays directly via QPainter in
     *          **view coordinates**.
     * @param[in] painter Borrowed active overlay painter; base does not inspect it.
     * @param[in] canvasExtent Current canvas-map extent, ignored by base.
     * @param[in] canvasSRS Borrowed current canvas CRS, possibly null; ignored by base.
     * @note Base draws nothing and does not alter painter/event state. Derived implementations must
     *   align their painter coordinates with host overlay and manage save/restore.
     */
    virtual void paint(QPainter *painter, const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS);

    /*!
     * \brief Returns the scene this tool's canvas is using.
     * @return Borrowed canvas mapScene or null without canvas; no ownership transfer, scene
     *   creation or validity check.
     */
    [[nodiscard]] QGraphicsScene *scene() const;

    // ----- Helpers provided to subclasses --------------------------------

    /*!
     * \brief Converts a widget pixel position to map coordinates.
     * \param px  Pixel X in widget space.
     * \param py  Pixel Y in widget space.
     * \param mapX Output map X.
     * \param mapY Output map Y.
     * @note Delegates widget integer pixels to current canvas extent (Y upward) in canvas CRS,
     *   without layer projection. Live canvas with invalid extent/nonpositive widget size writes
     *   zeros. Null canvas leaves BOTH output references untouched; caller initializes them if that
     *   case is possible. No clipping, DPR adjustment or view-transform sampling.
     */
    void toMapCoords(int px, int py, double &mapX, double &mapY) const;

    /*!
     * \brief Converts map coordinates to widget pixel position.
     * @param[in] mapX Finite canvas-map X in current canvas CRS.
     * @param[in] mapY Finite canvas-map Y, upward orientation.
     * @param[out] px Output widget pixel X, truncating scaled double to int; null canvas leaves
     *   untouched.
     * @param[out] py Output widget pixel Y, downward orientation; null canvas leaves untouched.
     * @note Current extent/size linear conversion, invalid extent/nonpositive size gives zeros. No
     *   clipping, layer reprojection, DPR adjustment or nonfinite/overflow validation.
     */
    void toPixelCoords(double mapX, double mapY, int &px, int &py) const;

signals:
    /**
     * @param[in] cursor New cursor a derived tool asks consumers to display; signal does not set
     *   canvas itself.
     * @note Base cursor getter/activation do not emit this signal; host/derived code manages
     *   notification.
     */
    void cursorChanged(const QCursor &cursor);
    /**
     * @param[in] active New stored state after setActive transition.
     * @note Synchronous signal on actual flag change, emitted by base activate/deactivate; no
     *   canvas pointer change itself.
     */
    void activeChanged(bool active);

    /*!
     * \brief Emitted to instruct the canvas to repaint its overlay.
     * @note Signal-only overlay redraw request for consumers; no invalidation occurs merely by
     *   calling paint. Base implementation never emits; derived tools/host must connect/use it.
     */
    void repaintRequested();

protected:
    QString m_toolName;
    MapCanvas *m_canvas = nullptr;

private:
    bool m_active = false;

    friend class MapCanvas;
    /**
     * @param[in] active Target stored flag; equality no-op.
     * @brief Update active flag and emit activeChanged once on transition.
     * @note Private host/base helper; does not change canvas activeTool, cursor or overlays.
     */
    void setActive(bool active);
};

#endif // MAPTOOL_H
