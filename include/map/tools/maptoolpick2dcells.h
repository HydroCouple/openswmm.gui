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
#include <QPointer>
#include <QVector>

class SWMM2DResultsLayer;

class MapToolPick2DCells : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    enum class Mode { Box, Lasso };

    explicit MapToolPick2DCells(MapCanvas *canvas, QObject *parent = nullptr);
    ~MapToolPick2DCells() override = default;

    void setMode(Mode m);
    [[nodiscard]] Mode mode() const noexcept { return m_mode; }

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

    /*! \brief Map (px,py) → scene (sx,sy) via the layer's Y-flip convention
     *  (sx = mx, sy = -my). */
    QPointF pixelToScene_(int px, int py) const;

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
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLPICK2DCELLS_H
