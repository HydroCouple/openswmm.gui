/*!
 * \file   maptoolselect.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Click/rubber-band feature selection tool for SWMM model layers.
 */

#ifndef MAPTOOLSELECT_H
#define MAPTOOLSELECT_H

#include "map/tools/maptool.h"
#include "plot/plotattribute.h"
#include "plot/resultdescriptor.h"
#include "selection/selectionmanager.h"

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSet>
#include <QVector>

class OpenSWMMVisLayer;
class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolSelect
 * \brief Tool that selects features in vector and SWMM model layers.
 * \details Single-click selects the nearest feature (within a configurable
 *          pixel tolerance).  Click-and-drag draws a rubber-band rectangle
 *          and selects all features that intersect it.
 *
 *          Holding Shift adds to the current selection.
 *          Holding Ctrl subtracts from the current selection.
 *          Pressing Escape clears all selections across all selectable layers.
 *
 *          Each selection change pushes an undo command so it can be reversed.
 */
class OpenSWMMVisMapToolSelect : public OpenSWMMVisMapTool
{
    Q_OBJECT

    Q_PROPERTY(int    pixelTolerance READ pixelTolerance WRITE setPixelTolerance
               NOTIFY pixelToleranceChanged)
    Q_PROPERTY(QColor rubberBandColor READ rubberBandColor WRITE setRubberBandColor
               NOTIFY rubberBandColorChanged)

public:

    explicit OpenSWMMVisMapToolSelect(MapCanvas *canvas, QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    // ----- Configuration -------------------------------------------------

    [[nodiscard]] int    pixelTolerance()  const;
    void setPixelTolerance(int pixels);

    [[nodiscard]] QColor rubberBandColor() const;
    void setRubberBandColor(const QColor &color);

    // ----- Tool interface ------------------------------------------------

    void activate()   override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event)       override;
    void mouseMoveEvent(QMouseEvent *event)        override;
    void mouseReleaseEvent(QMouseEvent *event)     override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)           override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    void pixelToleranceChanged(int pixels);
    void rubberBandColorChanged(const QColor &color);

    /*!
     * \brief Emitted after the selection changes, carrying the names/IDs of selected elements.
     */
    void selectionChanged(OpenSWMMVisLayer *layer);

    /*!
     * \brief Emitted from the right-click context menu's "Plot Time Series…"
     *        action. Handled at the main-window level identically to the
     *        Object Browser's identically-named signal, so the chart dialog
     *        opens against the active project's results .out file.
     */
    void plotTimeSeriesRequested(const SWMMObjectRef &ref);

    /*! \brief Slice AT.2 — emitted when the user picks a specific attribute
     *  from the right-click attribute submenu on a map object. Carries
     *  `PlotAttribute::Unknown` for the "All attributes" entry. */
    void plotAttributeRequested(const SWMMObjectRef &ref,
                                const openswmmvis::plot::ResultDescriptor &descriptor);

    /*! \brief Variant of \ref plotAttributeRequested that names a specific
     *  results layer. Emitted from the two-level "Plot Time Series ▸
     *  <layer> ▸ <variable>" submenu shown when more than one SWMM
     *  Output (.out) layer is loaded on the canvas. The receiver plots
     *  against that exact \p layer (no auto-pick-first-found). */
    void plotAttributeForLayerRequested(const SWMMObjectRef &ref,
                                         const openswmmvis::plot::ResultDescriptor &descriptor,
                                         class SWMMResultsLayer *layer);

    /*! \brief Slice AT.2 — emitted when the user picks a system-wide
     *  variable from the background right-click menu ("Plot System
     *  Variable…" submenu). */
    void plotSystemRequested(openswmmvis::plot::PlotAttribute attribute);

private:
    void selectAtPoint(const QPoint &pixel, Qt::KeyboardModifiers mods);
    void selectInRect(const QRect &pixelRect, Qt::KeyboardModifiers mods);

    /*! Show the right-click context menu at the given pixel. */
    void showContextMenu(const QPoint &pixel);

    /*! Delete all currently selected SWMM objects (Del key / context menu).
     *  Groups all deletions under a single parent undo command. */
    void deleteSelectedObjects();

    // ----- Inline edit sub-mode -------------------------------------------
    // Activated by double-clicking a node or link. Vertex handles appear in
    // the overlay and can be dragged without switching tools. Escape or
    // double-clicking empty space exits back to normal selection behaviour.

    enum class EditKind { None, Node, Link, Subcatch };

    /*! Enter edit mode for the given layer object. */
    void enterEditMode(SWMMModelLayer *layer, const QString &name,
                       EditKind kind, int soaIndex);
    /*! Exit edit mode and clear all handle state. */
    void clearEditMode();
    /*! Return the index of the handle under \p pixel, or -1. */
    int  hitTestEditHandle(const QPoint &pixel) const;
    /*! Select all handles whose pixel position falls inside \p pixelRect. */
    void selectHandlesInRect(const QRect &pixelRect);
    /*! Apply \p (dx,dy) delta (map coords) to all selected handles. */
    void applyGroupDragDelta(double dx, double dy);
    /*! Commit a completed node-handle drag. */
    void commitNodeDrag(double newX, double newY);
    /*! Commit \p newInterior as the link's interior vertices. */
    void commitLinkDrag(QVector<QPointF> newInterior);
    /*! Commit \p newVertices as the subcatchment's polygon. */
    void commitSubcatchDrag(QVector<QPointF> newVertices);

    /*! Delete all handles in m_editSelectedHandles.
     *  For links any number of interior vertices may be removed.
     *  For subcatchments the result is validated; if fewer than 3 vertices
     *  would remain the user is offered the option to delete the whole object. */
    void deleteSelectedEditHandles();

    EditKind         m_editKind      = EditKind::None;
    SWMMModelLayer  *m_editLayer     = nullptr;
    QString          m_editName;
    int              m_editSoaIdx    = -1;
    // Handle layout per EditKind:
    //   Node:    [0] = node position (circle+crosshair, translates node)
    //   Link:    [0..N-1] = interior vertices (squares)
    //   Subcatch:[0] = centroid (filled square, translates all vertices)
    //            [1..N] = polygon vertices (circles)
    QVector<QPointF> m_editHandles;
    QVector<QPointF> m_editSubcatchVerts; // cached polygon vertices (Subcatch mode)
    bool             m_editDragging     = false;
    int              m_editDragHandle   = -1;
    QPointF          m_editDragOrigPt;       // pre-drag map position of dragged vertex (for snap exclusion)
    QPointF          m_editCentroidPrev;     // centroid before current drag tick (Subcatch)
    QPointF          m_editGroupDragPrev;    // map pos of previous move tick (group drag)
    double           m_editNodeOrigX    = 0.0;
    double           m_editNodeOrigY    = 0.0;

    // Rubber-band vertex selection within edit mode
    QSet<int>        m_editSelectedHandles;
    bool             m_editRubberbanding = false;
    bool             m_editPressedEmpty  = false; // pressed on empty, not yet a drag
    QPoint           m_editRubberStart;
    QPoint           m_editRubberCurrent;

    static constexpr int kEditHandlePx  = 7;  // half-size of a vertex handle in pixels
    static constexpr int kSnapRadiusPx  = 15; // pixel radius within which snap kicks in

    // Snap state — updated each drag tick, consumed by paint()
    bool    m_snapping = false;
    QPointF m_snapPt;              // snapped position in map/layer CRS

    // ----- Normal selection state -----------------------------------------

    bool   m_dragging    = false;
    QPoint m_startPixel;
    QPoint m_currentPixel;
    // Default user-preference tolerance. The EFFECTIVE pixel radius
    // used at pick time is `max(m_pixelTol, largest-rendered-marker-
    // half-bounds + halo)`, so clicks inside the visible glyph always
    // succeed regardless of this number. See
    // `selectAtPoint` in the .cpp — it pulls the marker floor from
    // the layer's symbology. Slice V will turn this into a
    // user-configurable preference.
    int    m_pixelTol    = 16;
    // Drag threshold — only start the rubber-band once the cursor
    // moves this many pixels from the press point. Trackpad micro-
    // jitter on a click commonly hits 10–12 px, which used to flip
    // the tool into rubber-band mode and select a node + its
    // connected line together. 15 leaves enough slack to kill that
    // while still responding to intentional drags. Override via
    // PreferencesManager.
    int    m_dragThreshPx = 15;
    QColor m_rubberColor = QColor(0, 120, 255, 80);
};

#endif // MAPTOOLSELECT_H
