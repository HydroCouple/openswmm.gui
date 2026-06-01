/*!
 * \file   maptoolpick2dcells.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice CF.3.3 — canvas map tool that selects 2D mesh cells via
 *         box or lasso, then emits the picked triangle index list.
 *
 * Behaviour:
 *   - **Box mode (default)** — left-click and drag rubber-band; on release,
 *     calls `SWMM2DResultsLayer::pickCellsInRect` and emits the result.
 *     Mirrors the existing OpenSWMMVisMapToolSelect rubber-band logic.
 *   - **Lasso mode** — left-click adds polygon vertices, double-click closes,
 *     Escape cancels. Calls `pickCellsInPolygon`. Mirrors
 *     MapToolAddSubcatchment's polygon state machine.
 *   - **Single click** (no drag) — calls `pickCellAt` and emits a one-element
 *     list, falling back to box mode for selection.
 *   - **B / L keys** — swap modes while active.
 *
 * The tool finds the first SWMM2DResultsLayer on the canvas at activate()
 * time; selection is reported against that layer. The host wire is
 * `cellsPicked(layer, triIdxList)` which `SWMMVis::openComparisonPlotForCells`
 * consumes.
 */
#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLPICK2DCELLS_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLPICK2DCELLS_H

#include "map/tools/maptool.h"

#include <QPoint>
#include <QPolygon>
#include <QPolygonF>
#include <QPointer>
#include <QRectF>
#include <QVector>

class SWMM2DResultsLayer;
class SWMM2DMeshLayer;
class SelectionManager;

class MapToolPick2DCells : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    enum class Mode { Box, Lasso };

    explicit MapToolPick2DCells(MapCanvas *canvas,
                                SelectionManager *selection,
                                QObject *parent = nullptr);
    ~MapToolPick2DCells() override = default;

    void setMode(Mode m);
    [[nodiscard]] Mode mode() const noexcept { return m_mode; }

    /*! Bind the tool to a specific 2D results layer (the project window's
     *  active analysis layer). When set, picks report against this layer
     *  instead of the first SWMM2DResultsLayer found on the canvas. Passing
     *  nullptr restores the first-found fallback. */
    void setTargetLayer(SWMM2DResultsLayer *layer);

    void activate() override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted on Box release / Lasso completion / single click.
     *  \param layer       The active 2D results layer (non-owning).
     *  \param triIdxList  Picked triangle indices. Empty for nothing-hit. */
    void cellsPicked(SWMM2DResultsLayer *layer, const QVector<int> &triIdxList);

private:
    /*! \brief Find the first SWMM2DResultsLayer on the active canvas. */
    SWMM2DResultsLayer *findResultsLayer_() const;

    /*! \brief Find the active (else first) SWMM2DMeshLayer on the canvas —
     *  its sourcePath keys the MeshCell selection refs. */
    SWMM2DMeshLayer *findMeshLayer_() const;

    /*! \brief Push \p hits into the SelectionManager as MeshCell refs per the
     *  modifier convention (Shift = add, Ctrl = toggle, none = replace). The
     *  toolbar mirrors the selection into the mesh layer's triangle highlight
     *  + cell label. Selection only highlights — it does NOT open the plot. */
    void applySelection_(const QVector<int> &hits, Qt::KeyboardModifiers mods);

    /*! \brief The triangle indices currently selected as MeshCell refs for
     *  the active mesh layer. */
    [[nodiscard]] QVector<int> selectedCells_() const;

    /*! \brief Right-click handler: plot the current cell selection (or the
     *  cell under \p pixel when nothing is selected) by emitting cellsPicked. */
    void requestPlotAt_(const QPoint &pixel);

    /*! \brief Map (px,py) → scene (sx,sy) via the layer's Y-flip convention
     *  (sx = mx, sy = -my). */
    QPointF pixelToScene_(int px, int py) const;

    /*! \brief Pick a single cell at \p scenePt. Prefers the mesh layer (so
     *  selection works during mesh editing before any results are loaded);
     *  falls back to the results layer for results-only views (no mesh
     *  geometry held by a SWMM2DMeshLayer). Returns -1 on miss. */
    int pickCellAtScene_(const QPointF &scenePt);

    /*! \brief Box pick — peer of `pickCellAtScene_`. */
    QVector<int> pickCellsInRectScene_(const QRectF &sceneRect);

    /*! \brief Lasso pick — peer of `pickCellAtScene_`. */
    QVector<int> pickCellsInPolygonScene_(const QPolygonF &scenePoly);

    Mode                            m_mode = Mode::Box;

    // Box state.
    bool                            m_dragging   = false;
    QPoint                          m_startPixel;
    QPoint                          m_currentPixel;
    static constexpr int            kDragThreshPx = 8;

    // Lasso state.
    bool                            m_drawing    = false;
    QVector<QPointF>                m_lassoMapPts;     // map coords
    QPoint                          m_cursorPixel;

    QPointer<SWMM2DResultsLayer>    m_targetLayer;
    QPointer<SWMM2DMeshLayer>       m_meshLayer;
    QPointer<SelectionManager>      m_selection;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLPICK2DCELLS_H
