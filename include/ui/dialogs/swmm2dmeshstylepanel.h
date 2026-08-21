/*!
 * \file   swmm2dmeshstylepanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Styling panel for a SWMM2DMeshLayer — one tab per sublayer,
 *         mirroring Swmm2DResultsStylePanel (the QGIS mesh-layer idiom).
 *
 *         Replaces the combined "Mesh / TIN" form (MeshHillshadeEditor),
 *         which bundled display toggles, terrain fill, hillshade, contour
 *         lines, and filled bands into a single page while the sublayers
 *         underneath were already distinct. Tabs:
 *           Terrain Fill · Elevation Bands · Elevation Isolines ·
 *           Mesh Edges · Mesh Vertices · Boundary Conditions ·
 *           Coupled Nodes
 *         Every control writes live into the owning sublayer / its style
 *         bag; each tab's tooltip carries the sublayer id so right-click →
 *         "Edit Sublayer Style…" routes to the matching tab.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_SWMM2DMESHSTYLEPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_SWMM2DMESHSTYLEPANEL_H

#include <QPointer>
#include <QWidget>

class SWMM2DMeshLayer;

namespace openswmmvis::ui {

class Swmm2DMeshStylePanel : public QWidget
{
    Q_OBJECT
public:
    explicit Swmm2DMeshStylePanel(SWMM2DMeshLayer *layer, QWidget *parent = nullptr);

private:
    [[nodiscard]] QWidget *buildTerrainFillTab(QWidget *parent);
    [[nodiscard]] QWidget *buildBandTab(QWidget *parent);
    [[nodiscard]] QWidget *buildIsolineTab(QWidget *parent);
    [[nodiscard]] QWidget *buildMeshEdgeTab(QWidget *parent);
    [[nodiscard]] QWidget *buildMeshNodeTab(QWidget *parent);
    [[nodiscard]] QWidget *buildBcTab(QWidget *parent);
    [[nodiscard]] QWidget *buildCoupledNodeTab(QWidget *parent);

    QPointer<SWMM2DMeshLayer> m_layer;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SWMM2DMESHSTYLEPANEL_H
