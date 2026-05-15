/*!
 * \file   maptoolselect.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#ifndef MAPTOOLSELECT_H
#define MAPTOOLSELECT_H

#include "map/tools/maptool.h"
#include "selection/selectionmanager.h"

#include <QPoint>
#include <QRect>
#include <QSet>

class OpenSWMMVisLayer;

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

    void mousePressEvent(QMouseEvent *event)   override;
    void mouseMoveEvent(QMouseEvent *event)    override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)       override;

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

private:
    void selectAtPoint(const QPoint &pixel, Qt::KeyboardModifiers mods);
    void selectInRect(const QRect &pixelRect, Qt::KeyboardModifiers mods);

    /*! Show the right-click context menu at the given pixel. */
    void showContextMenu(const QPoint &pixel);

    /*! Delete all currently selected SWMM objects (Del key / context menu).
     *  Groups all deletions under a single parent undo command. */
    void deleteSelectedObjects();

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
