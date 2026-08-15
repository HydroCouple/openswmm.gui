/*!
 * \file   maptoolmeshselectedge.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice §V.VB — map tool that picks 2D mesh edges by single click or
 * box rubber-band. Defaults to boundary-only (the BC-meaningful subset).
 * Press "A" while the tool is active to toggle including interior edges
 * (rare; mainly diagnostic) — "B" to revert to boundary-only.
 *
 * Picks are reported via SelectionManager using MeshObjectRef-encoded
 * SWMMObjectRef names; same modifier grammar as MapToolMeshSelectVertex,
 * except that Ctrl/⌘ is repurposed here for boundary path picking (see
 * below) instead of Toggle.
 *
 * Ctrl/⌘ + click on a boundary edge Adds the shortest chain of boundary
 * edges (inclusive of both ends) between it and the run's starting edge
 * to the selection. "Shortest" is geometric length.
 *
 * The starting edge is, in order of preference:
 *   1. an explicitly placed anchor (see below);
 *   2. the edge this tool last selected on its own — a plain click, or the
 *      far end of the path it just committed, which is what makes runs
 *      chain: each Ctrl/⌘ + click extends the boundary run;
 *   3. the single selected edge, when the selection holds exactly one for
 *      this mesh (e.g. picked from another view).
 * With nothing to start from — empty or ambiguous selection — the first
 * Ctrl/⌘ + click instead places the anchor, drawn distinctly, and the next
 * one commits the path.
 */
#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTEDGE_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTEDGE_H

#include "map/tools/maptool.h"
#include "plot/plotattribute.h"   // PlotAttribute (edge flow vs flux)

#include <QPoint>
#include <QPointer>

class SelectionManager;
class SWMM2DMeshLayer;

class MapToolMeshSelectEdge : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    explicit MapToolMeshSelectEdge(MapCanvas *canvas,
                                   SelectionManager *selection,
                                   QObject *parent = nullptr);

    [[nodiscard]] bool boundaryOnly() const { return m_boundaryOnly; }
    void setBoundaryOnly(bool on);

    void activate() override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event)   override;
    void mouseMoveEvent(QMouseEvent *event)    override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)       override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted when the user right-clicks an edge and chooses
     *  "Plot edge flow" or "Plot edge flux". The host opens the comparison plot
     *  with that one series for the edge; `attr` is Mesh2DEdgeFlow (volumetric
     *  Q, m³/s) or Mesh2DEdgeFlux (unit-width q, m²/s). */
    void plotEdgeFluxRequested(SWMM2DMeshLayer *mesh, int triIdx, int edgeLocal,
                               openswmmvis::plot::PlotAttribute attr);

    /*! \brief Path-picking feedback for the host's status bar (anchor set,
     *  chain added, "requires boundary edges", no path). */
    void statusMessageChanged(const QString &message);

private:
    SWMM2DMeshLayer *findActiveMeshLayer_() const;
    QPointF pixelToScene_(int px, int py) const;

    /*! Flat edge slot (`tri*3 + eLocal`) nearest \p pos, or -1. */
    int  pickEdgeAtPixel_(const QPoint &pos, bool boundaryOnly) const;
    /*! Handle one Ctrl/⌘ + click: set the anchor, or commit the path. */
    void handlePathClick_(const QPoint &pos);
    /*! Boundary slot the next path should start from, taken from the
     *  current selection; -1 when nothing unambiguous is selected. */
    int  pathStartFromSelection_() const;
    /*! Drop the anchor (and repaint if it was showing). */
    void clearPathAnchor_();
    /*! Mesh geometry republished — drop the anchor and the path start. */
    void onMeshGeometryReady_();
    /*! Resolve / re-resolve m_target, wiring mesh-rebuild invalidation. */
    void retargetMeshLayer_();

    QPointer<SelectionManager> m_selection;
    QPointer<SWMM2DMeshLayer>  m_target;

    // Default to selecting ANY edge (interior + boundary): interior edges
    // are selectable so their flux time series can be plotted. Boundary
    // conditions are still restricted to boundary edges by the toolbar's
    // commitBCParam. Press B to restrict picking to boundary edges, A to
    // include interior again.
    bool   m_boundaryOnly = false;
    bool   m_dragging     = false;
    QPoint m_startPixel;
    QPoint m_currentPixel;

    // Ctrl/⌘ path picking. m_pathAnchorSlot is the flat edge slot of the
    // first Ctrl-clicked boundary edge (-1 = no anchor); it is dropped on
    // deactivate, on a target-layer change, and when the layer republishes
    // its scene geometry (the slot numbering would no longer mean the same
    // edge). m_pathClickHandled swallows the release that follows the
    // Ctrl-press so it doesn't also run a normal single-click select.
    int    m_pathAnchorSlot   = -1;
    bool   m_pathClickHandled = false;

    // The single edge this tool last put in the selection (plain click,
    // right-click, or the far end of the last committed path), or -1 after
    // a box-select / a miss, where "the selected edge" is ambiguous. Only
    // honoured while it is still in the selection.
    int    m_lastEdgeSlot     = -1;
    static constexpr int kDragThreshPx = 6;
    static constexpr double kPickTolPx = 8.0;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTEDGE_H
