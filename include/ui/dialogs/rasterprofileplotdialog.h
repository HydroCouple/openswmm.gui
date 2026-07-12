/*!
 * \file   rasterprofileplotdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Host dialog for a DEM-raster longitudinal profile.  The terrain peer
 *         of MeshProfilePlotDialog: builds a ProfileSection::Section from a
 *         traced polyline via RasterProfileSampler and hosts the SAME
 *         MeshProfilePlotWidget chart + MeshProfileOverlay map line.
 *
 *         Simpler than the mesh dialog in exactly one way — a DEM has no water,
 *         so there is no results layer, no AnimationController subscription and
 *         no per-frame depth column; the section is static once traced. It is
 *         rebuilt when the terrain's vertical factor changes (the toolbar's
 *         unit / factor controls), so the ground line always reads in the
 *         project's model vertical units.
 *
 *         Lifetime mirrors MeshProfilePlotDialog: a top-level window parented
 *         to its project window, WA_DeleteOnClose, that severs external
 *         connections on the project's aboutToClose so a multi-window teardown
 *         can't deliver into a half-destroyed dialog.
 */
#ifndef RASTER_PROFILE_PLOT_DIALOG_H
#define RASTER_PROFILE_PLOT_DIALOG_H

#include "plot/profilesection.h"

#include <QDialog>
#include <QPointer>
#include <QPointF>
#include <QVector>

class GISRasterLayer;
class MapToolProfileMarker;
class MeshProfileOverlay;
class MeshProfilePlotOptions;
class MeshProfilePlotWidget;
class OpenSWMMVisMapTool;
class SWMMVisProjectWindow;

class RasterProfilePlotDialog : public QDialog
{
    Q_OBJECT
public:
    /*!
     * \param raster         The DEM to sample (TerrainToolbar's active terrain).
     * \param vertFactor     Raw DEM Z → model vertical units
     *                       (TerrainToolbar::verticalToModelFactor()).
     * \param scenePolyline  Traced vertices in scene coords (sx = canvasX,
     *                       sy = -canvasY), as MapToolMeshProfile emits them.
     */
    RasterProfilePlotDialog(GISRasterLayer         *raster,
                            double                  vertFactor,
                            const QVector<QPointF> &scenePolyline,
                            SWMMVisProjectWindow   *projectWindow,
                            QWidget                *parent = nullptr);
    ~RasterProfilePlotDialog() override;

    /*! \brief Re-sample the DEM with a new raw-Z → model-units factor (the
     *  terrain toolbar's unit / factor controls changed). */
    void setVerticalFactor(double vertFactor);

private:
    void buildLayout();
    void rebuildProfile();

    // Persistent map overlay (profile line + position arrow) + the map-drag
    // tool that scrubs the arrow. Lifetime tied to this dialog.
    void setupMapOverlay();
    void removeOverlay();
    void setMarkerToolActive(bool on);   // toggle map-side dragging
    void openDisplayOptions();

    QPointer<GISRasterLayer>       m_raster;
    QPointer<SWMMVisProjectWindow> m_projectWindow;
    QVector<QPointF>               m_scenePolyline;
    double                         m_vertFactor = 1.0;

    MeshProfilePlotWidget        *m_plot    = nullptr;
    MeshProfilePlotOptions       *m_options = nullptr;

    MeshProfileOverlay           *m_overlay    = nullptr;  // owned by map scene
    MapToolProfileMarker         *m_markerTool = nullptr;  // parented to dialog
    QPointer<OpenSWMMVisMapTool>  m_prevTool;              // restore on toggle-off/close

    ProfileSection::Section        m_profile;
};

#endif // RASTER_PROFILE_PLOT_DIALOG_H
