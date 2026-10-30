/**
 * @file      swmm2dmeshstylepanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Styling panel for a SWMM2DMeshLayer — one tab per sublayer,
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_SWMM2DMESHSTYLEPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_SWMM2DMESHSTYLEPANEL_H

#include <QPointer>
#include <QWidget>

class SWMM2DMeshLayer;

namespace openswmmvis::ui
{

class Swmm2DMeshStylePanel : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Construct seven live per-sublayer terrain styling tabs on the GUI thread.
     * @param[in,out] layer Nullable borrowed mesh layer, guarded by QPointer for the member. Null
     * produces an empty tab container. A non-null layer and its sublayer/style objects must outlive
     * active tabs because callbacks also retain raw pointers.
     * @param[in] parent Optional Qt widget owner.
     * Controls are hydrated before wiring user edits; edits directly mutate live layer/styles with
     * no local snapshot/undo. Tab tooltips carry sublayer IDs for style-dialog routing; external
     * mutations are not generally rehydrated into these controls.
     */
    explicit Swmm2DMeshStylePanel(SWMM2DMeshLayer *layer, QWidget *parent = nullptr);

private:
    /**
     * @brief Build the live terrain attribute/no-data/category classification and sun-lighting
     * page.
     * Uses mesh elevation samples/range for classification, degree-valued azimuth/altitude and
     * dimensionless exaggeration/shadow controls; callbacks keep sliders/thumb in sync.
     * @param[in] parent Optional Qt owner for the new page.
     * @return New page owned by parent (caller-owned if null), with controls only when the
     * corresponding style exists. Requires a live non-null m_layer; callbacks borrow its styles for
     * page lifetime.
     */
    [[nodiscard]] QWidget *buildTerrainFillTab(QWidget *parent);
    /**
     * @brief Build the live filled-elevation classification and smooth-boundary page using mesh bed
     * samples/range.
     * @param[in] parent Optional Qt owner for the new page.
     * @return New page owned by parent (caller-owned if null), with controls only when the
     * corresponding style exists. Requires a live non-null m_layer; callbacks borrow its styles for
     * page lifetime.
     */
    [[nodiscard]] QWidget *buildBandTab(QWidget *parent);
    /**
     * @brief Build elevation isoline visibility/count/color/pixel-width/label controls with count
     * 1..200 and width 0.25..10 pixels.
     * @param[in] parent Optional Qt owner for the new page.
     * @return New page owned by parent (caller-owned if null), with controls only when the
     * corresponding style exists. Requires a live non-null m_layer; callbacks borrow its styles for
     * page lifetime.
     */
    [[nodiscard]] QWidget *buildIsolineTab(QWidget *parent);
    /**
     * @brief Build edge visibility/style, pixel-per-cell zoom threshold and steep-edge emphasis
     * controls.
     * Normal/wide widths span 0.1..10 pixels, zero zoom threshold means always and slope-break UI
     * spans [0,1].
     * @param[in] parent Optional Qt owner for the new page.
     * @return New page owned by parent (caller-owned if null), with controls only when the
     * corresponding style exists. Requires a live non-null m_layer; callbacks borrow its styles for
     * page lifetime.
     */
    [[nodiscard]] QWidget *buildMeshEdgeTab(QWidget *parent);
    /**
     * @brief Build vertex visibility, color, pixel size/outline and pixel-per-cell zoom controls.
     * Marker size spans 0.5..20 pixels, outline width 0..5 pixels and zero zoom threshold means
     * always.
     * @param[in] parent Optional Qt owner for the new page.
     * @return New page owned by parent (caller-owned if null), with controls only when the
     * corresponding style exists. Requires a live non-null m_layer; callbacks borrow its styles for
     * page lifetime.
     */
    [[nodiscard]] QWidget *buildMeshNodeTab(QWidget *parent);
    /**
     * @brief Build per-type boundary visibility/colors/pixel widths.
     * Each edit marks the BC style customized against future preference reseeding; wall/interior
     * edges inherit wireframe width while other widths span 0.1..20 pixels. Absent mesh types
     * remain editable.
     * @param[in] parent Optional Qt owner for the new page.
     * @return New page owned by parent (caller-owned if null), with controls only when the
     * corresponding style exists. Requires a live non-null m_layer; callbacks borrow its styles for
     * page lifetime.
     */
    [[nodiscard]] QWidget *buildBcTab(QWidget *parent);
    /**
     * @brief Build coupled-vertex visibility, color and 0.5..40-pixel marker-size controls.
     * The renderer uses the mesh-vertex zoom threshold for these markers.
     * @param[in] parent Optional Qt owner for the new page.
     * @return New page owned by parent (caller-owned if null), with controls only when the
     * corresponding style exists. Requires a live non-null m_layer; callbacks borrow its styles for
     * page lifetime.
     */
    [[nodiscard]] QWidget *buildCoupledNodeTab(QWidget *parent);

    QPointer<SWMM2DMeshLayer> m_layer;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SWMM2DMESHSTYLEPANEL_H
