/**
 * @file      rasterprofileplotdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Host dialog for a DEM-raster longitudinal profile.  The terrain peer
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
 *         A nonmodal top-level window uses the supplied QWidget parent; the
 *         caller controls deletion on close. Project aboutToClose removes
 *         the map overlay before scene teardown and closes the window.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
     * @param[in] projectWindow Optional borrowed guarded project/canvas context for sampling CRS,
     *   map overlay/tool and axis labels. Does not become QObject parent automatically.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog and any
     *   delete-on-close policy externally.
     * @brief Copy trace/scaling, build static terrain chart and optional map overlay/marker tool,
     *   and remove overlay on project aboutToClose.
     * @note Inputs are guarded for later queries but no automatic raster-deletion/path/band or
     *   CRS-change resampling subscription. Caller invokes factor update/rebuild as needed.
     *   Constructor sets nonmodal top-level flags but does NOT set WA_DeleteOnClose; ordinary close
     *   can hide retained dialog with overlay until destructor/project teardown cleanup. No mesh
     *   water/results ownership or source edits.
     */
    RasterProfilePlotDialog(GISRasterLayer *raster, double vertFactor,
                            const QVector<QPointF> &scenePolyline,
                            SWMMVisProjectWindow *projectWindow, QWidget *parent = nullptr);
    /**
     * @brief Restore marker tool when still active, detach own overlay binding and manually delete
     *   overlay before child destruction.
     * @note Does not delete borrowed raster/project/canvas or a later overlay installed by another
     *   profile. Host must preserve scene/canvas teardown ordering through project aboutToClose.
     */
    ~RasterProfilePlotDialog() override;

    /*! \brief Re-sample the DEM with a new raw-Z → model-units factor (the
     *  terrain toolbar's unit / factor controls changed).
     * @param[in] vertFactor Raw DEM elevation multiplier into model vertical units; finite scaling
     *   expected by sampler, no positivity/finiteness clamp in setter.
     * @brief On qFuzzyCompare difference store factor and resample complete terrain profile
     *   synchronously.
     * @note Does not refresh axis-unit labels, trace coordinates or overlay/tool geometry. Equal
     *   fuzzy factor no-op; no result/engine writes or undo.
 */
    void setVerticalFactor(double vertFactor);

private:
    /**
     * @brief Create owned plot/options toolbar, marker toggle and Close controls with
     *   project/global length labels.
     * @note Chart modes identify/zoom/pan/fit are plot-only; marker toggle delegates map tool
     *   activation. Labels use unit snapshot at layout creation, horizontal chainage remains
     *   scene/canvas units without conversion, vertical samples raw DEM times factor. No automatic
     *   unit-label subscription, source writes or data sampling inside helper.
     */
    void buildLayout();
    /**
     * @brief Synchronously sample current guarded raster file/render band along copied scene trace
     *   using current project canvas SRS and stored vertical factor.
     * @note Null/deleted raster clears profile/chart only WHEN this method runs; deletion itself
     *   does not invoke it. Sampler may return empty/gapped NaN section for IO/transform/NoData,
     *   passed through without dialog error message. Horizontal distance in scene/canvas units,
     *   elevation in rawZ*factor; no source handle retained, water data, overlay rebuild or
     *   automatic pan/CRS requery afterward.
     */
    void rebuildProfile();

    // Persistent map overlay (profile line + position arrow) + the map-drag
    // tool that scrubs the arrow. Lifetime tied to this dialog.
    /**
     * @brief With project canvas and at least two trace vertices, allocate owned polyline/arrow
     *   overlay, bind canvas borrowed slot and create child marker tool.
     * @note Chart cursor updates map arrow and Overlay invalidation; map marker updates chart
     *   display-only cursor, no feedback emission. Start halfway along trace. Does not activate
     *   marker tool until toggled. Not idempotent: no existing-overlay cleanup inside helper; other
     *   profile can replace canvas slot without taking ownership of this overlay. No
     *   engine/geometry edits.
     */
    void setupMapOverlay();
    /**
     * @brief Restore guarded previous map tool only when own marker tool active, detach canvas
     *   overlay only if still ours and delete owned overlay.
     * @note Null-project/canvas path still clears marker overlay pointer and deletes owned overlay.
     *   Repeated calls safe; leaves later profiles/tools untouched. Does not close/delete dialog,
     *   clear chart or delete borrowed canvas.
     */
    void removeOverlay();
    /**
     * @param[in] on True installs marker tool after remembering current active tool, false restores
     *   remembered live tool or null only if marker still active.
     * @brief Temporarily switch map tool when project/canvas/marker available.
     * @note Repeated true while already active no-op; false leaves another newly active tool
     *   unchanged. Previous tool guarded but not owned; no trace/model edits, marker reset or
     *   repaint beyond canvas tool handling.
     */
    void setMarkerToolActive(bool on); // toggle map-side dragging
    /**
     * @brief Create another nonmodal child options window editing shared owned profile options
     *   live; it deletes on close.
     * @note With QPropertyModel bind current options and refresh on changed; absent library shows
     *   unavailable message. Multiple windows permitted, no draft/apply/cancel rollback, persistent
     *   preference save or engine write. Terrain water/cell toggles were initially off, not hidden
     *   by this method.
     */
    void openDisplayOptions();

    QPointer<GISRasterLayer> m_raster;
    QPointer<SWMMVisProjectWindow> m_projectWindow;
    QVector<QPointF> m_scenePolyline;
    double m_vertFactor = 1.0;

    MeshProfilePlotWidget *m_plot = nullptr;
    MeshProfilePlotOptions *m_options = nullptr;

    MeshProfileOverlay *m_overlay = nullptr;      // manually owned; canvas borrows
    MapToolProfileMarker *m_markerTool = nullptr; // parented to dialog
    QPointer<OpenSWMMVisMapTool> m_prevTool;      // restore on toggle-off/close

    ProfileSection::Section m_profile;
};

#endif // RASTER_PROFILE_PLOT_DIALOG_H
