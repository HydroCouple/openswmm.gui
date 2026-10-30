/**
 * @file      meshprofileplotdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Host dialog for the 2D-mesh longitudinal profile.  Builds a
 *         MeshProfile from a traced polyline, hosts a MeshProfilePlotWidget,
 *         and drives the animated depth column from the 2D results layer's
 *         frame changes (which the global AnimationController advances).
 *
 *         Widget content hosted in a PlotDock tab. Project closure severs
 *         source connections before removing the tab; activation owns the
 *         canvas overlay and map marker tool.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MESH_PROFILE_PLOT_DIALOG_H
#define MESH_PROFILE_PLOT_DIALOG_H

#include "plot/meshprofilesampler.h"
#include "plot/profilesectionseries.h"

#include <QDateTime>
#include <QHash>
#include "ui/panels/plotpanelview.h"
#include <QPointer>
#include <QPointF>
#include <QVector>

class QVBoxLayout;
class QAction;
class QComboBox;
class QLabel;
class QTableView;
class MeshProfileTracksWidget;
class MeshProfileSamplesModel;
class MeshProfileSeriesEditor;
class AnimationController;
class MapToolProfileMarker;
class MeshProfileOverlay;
class MeshProfilePlotOptions;
class MeshProfilePlotWidget;
class OpenSWMMVisMapTool;
class SWMM2DMeshLayer;
class SWMM2DResultsLayer;
class SWMMVisProjectWindow;

class MeshProfilePlotDialog : public openswmmvis::ui::PlotPanelView
{
    Q_OBJECT
public:
    /**
     * @brief Build a dock-hosted GUI-thread mesh section, series editor and map overlay.
     * @param[in,out] mesh Nullable borrowed terrain mesh, QPointer-guarded.
     * @param[in,out] results Nullable borrowed primary results, QPointer-guarded; used for exact
     * water geometry, frames and envelope. At least one mesh/results is needed for nonempty
     * samples.
     * @param[in,out] anim Nullable borrowed animation clock, guarded; active tabs advance their own
     * results even when hidden on the map.
     * @param[in] scenePolyline Copied traced polyline in canvas scene coordinates; fewer than two
     * points omits the map overlay.
     * @param[in,out] projectWindow Nullable borrowed project/canvas/unit provider, guarded;
     * aboutToClose disconnects sources, invalidates context, removes overlay and closes the panel.
     * @param[in] parent Optional Qt widget owner.
     * Owns plot/options/series controls and marker tool; manually owns the non-QObject overlay
     * borrowed by the canvas. Section geometry and unit metadata are copied into a fresh UUID
     * definition; no project save occurs until requested.
     */
    MeshProfilePlotDialog(SWMM2DMeshLayer *mesh,
                          SWMM2DResultsLayer *results, // may be null
                          AnimationController *anim, const QVector<QPointF> &scenePolyline,
                          SWMMVisProjectWindow *projectWindow, QWidget *parent = nullptr);
    /**
     * @brief Detach/delete this overlay, restore its previous canvas tool if still active, and
     * disconnect primary animation/results routes.
     * Qt destroys child controls/tool; borrowed layers/project/clock remain externally owned.
     */
    ~MeshProfilePlotDialog() override;
    /**
     * @brief Capture the current section definition including traced polyline and current
     * display-option JSON.
     * @return Owned value copy suitable for validation/persistence; does not save the project.
     */
    ProfileSection::Definition definition() const;
    /**
     * @brief Validate and install a saved section in the current coordinate system.
     * @param[in] definition Borrowed definition copied on success; scene CRS must equal the current
     * canvas/results/mesh CRS.
     * @param[out] error Optional diagnostic destination used by validation/CRS/options failure; not
     * explicitly cleared on success.
     * @return False before committing section geometry for invalid definition/CRS/options; true
     * after loading options/series, clearing station cache, rebuilding geometry/series, updating
     * overlay/title. No automatic CRS transformation or project save occurs.
     */
    bool setDefinition(const ProfileSection::Definition &definition, QString *error = nullptr);
    /**
     * @brief Replace discovery subscriptions and sampled-source choices while retaining primary
     * animation wiring.
     * @param[in] sources Borrowed layer list; nulls/duplicates are skipped and the primary is
     * prepended when absent. Layers are retained through QPointer, never owned.
     * Clears geometry station cache, adds missing stable source references to the definition,
     * rebuilds choices and current series. Time-range/destruction events refresh discovery;
     * secondary time changes refresh only while active.
     */
    void setResultSources(const QList<SWMM2DResultsLayer *> &sources);
    /**
     * @brief Replace saved-section combo entries, retaining selected ID when present.
     * @param[in] definitions Borrowed value list; stores each ID and title (ID fallback), without
     * loading it or taking ownership.
     */
    void setSavedDefinitions(const QVector<ProfileSection::Definition> &definitions);
    /**
     * @brief Borrow the cached sampled section.
     * @return Const reference owned by this panel; contents change on geometry/time/series refresh
     * and lifetime ends with the panel.
     */
    const ProfileSection::Section &section() const { return m_profile; }
signals:
    /**
     * @brief Request external persistence after validating the current section.
     * @param[in] definition Borrowed signal argument containing a value snapshot; queued delivery
     * copies it. The dialog does not save the project itself.
     */
    void saveDefinitionRequested(const ProfileSection::Definition &definition);
    /**
     * @brief Request the owner to resolve and open a saved section.
     * @param[in] id Exact ID stored by the selected saved-section combo item; dialog performs no
     * lookup/persistence itself.
     */
    void openSavedDefinitionRequested(const QString &id);
    /**
     * @brief Notify title, series or display-option edits; option notifications are suppressed
     * during saved-definition loading.
     */
    void definitionChanged();

private:
    /**
     * @brief Create owned navigation/options toolbar, scrollable figure/tracks and section settings
     * once.
     * Connects cursor synchronization and per-project axis labels; settings visibility is
     * controlled by its persisted action.
     */
    void buildLayout();
    /**
     * @brief Append title/save/open/export, series editing, sortable samples and availability
     * controls.
     * @param[in,out] layout Required borrowed vertical layout receiving child layouts/widgets;
     * initialized once by buildLayout.
     * Save emits a validated definition, open emits an ID; sample-row selection moves
     * chart/track/map chainage through normal cursor notifications.
     */
    void buildSectionControls(QVBoxLayout *layout);
    /**
     * @brief Sample current configured series from guarded source bindings at the requested time
     * and publish plot/tracks/table state.
     * Uses primary frame time when request is invalid; caches per-source stations by geometry
     * revision, borrows source handles only during sampling, and guards point-to-cell callbacks.
     * Marks out-of-coverage surface data, clears series on sampler failure, reports series
     * diagnostics, and displays native horizontal/elevation units from the definition.
     */
    void refreshSectionSeries();
    /**
     * @brief Rebuild editor source/variable catalogs from surviving result layers with sources.
     * Uses layer name or filename fallback and reapplies the current definition; missing references
     * remain in the definition for diagnostics.
     */
    void refreshSourceChoices();
    /**
     * @brief Resolve a stable result-source ID for saved section references.
     * @param[in] layer Nullable borrowed results layer.
     * @return Existing definition source ID for an equivalent normalized file path, normalized
     * source path otherwise, layerId when pathless, or empty for null.
     */
    QString sourceId(SWMM2DResultsLayer *layer) const;
    /**
     * @brief Prompt for PNG export and render the current figure through QSaveFile.
     * Rechecks panel/context after the modal picker, rejects figures above 64 million pixels, uses
     * the current base palette background and reports open/encode/commit errors in status. Does not
     * save section configuration.
     */
    void exportSectionImage();
    /**
     * @brief Prompt for CSV export of the current cached section/definition.
     * Rechecks panel lifetime/context after the picker and reports exporter failure/success in
     * status; cancellation writes nothing.
     */
    void exportSectionTable();
    /**
     * @brief Resample terrain/water geometry, convert vertical values to definition elevation units
     * and refresh series.
     * Clears the profile if both sources are absent. Rebuild uses zero extra sampling distance and
     * no synchronous envelope scan; cached-ready envelopes apply during the guarded rebuild,
     * otherwise a layer request follows.
     */
    void rebuildProfile(); // full resample (geometry + envelope)
    /**
     * @brief Apply a completed primary temporal envelope or request its asynchronous construction.
     * No-op without primary results or exact water geometry; envelopeReady later calls
     * applyEnvelope.
     */
    void requestEnvelope(); // ask the layer for the temporal maximum
    /**
     * @brief Sample the primary maximum onto cached exact-water geometry when its revision still
     * matches.
     * Uses vertical-axis scaling and refreshes series unless already inside a geometry rebuild;
     * stale/missing geometry is ignored.
     */
    void applyEnvelope(); // sample that maximum onto the profile
    /**
     * @brief Transfer map-overlay ownership of the display slot and catch up time/series when the
     * tab activates.
     * @param[in] active New plot activation state. Deactivation unchecks/restores marker tool and
     * detaches only this overlay; activation installs it and samples the clock as-of time. Does not
     * own/delete the canvas.
     */
    void plotActivationChanged(bool active) override;
    /**
     * @brief Update per-station signed/visible depth and surface availability for the primary
     * frame.
     * No-op without primary results/samples; mismatched exact geometry triggers full rebuild.
     * Converts SI depth through mesh and vertical-axis scales, clamps visible depth nonnegative,
     * updates the plot and all configured series.
     */
    void refreshCurrentDepths(); // per-frame depth-column update
    /**
     * @brief Show a parent-owned modeless DeleteOnClose options dialog editing this panel options
     * live.
     * Uses the optional property model/delegate when built, otherwise an explanatory label. Close
     * keeps changes; no private rollback snapshot.
     */
    void openDisplayOptions();

    // Persistent map overlay (profile line + position arrow) + the map-drag
    // tool that scrubs the arrow. Lifetime tied to this dialog.
    /**
     * @brief Create a manually owned profile overlay and Qt-owned drag tool when canvas/polyline
     * are valid.
     * Canvas borrows the overlay only while active; connects chart/map cursor movement and seeds
     * the cursor at mid-chainage. Called once after plot construction.
     */
    void setupMapOverlay();
    /**
     * @brief Restore this active marker tool, detach only this canvas overlay and delete it safely.
     * Clears the tool overlay pointer before manual deletion; does not remove another profile
     * overlay occupying the shared canvas slot.
     */
    void removeOverlay();
    /**
     * @brief Switch the canvas between this marker tool and its guarded previous tool.
     * @param[in] on True activates only for an active plot; false restores the prior tool only when
     * this tool still owns the canvas slot. Missing canvas/tool/project is ignored.
     */
    void setMarkerToolActive(bool on); // toggle map-side dragging

    QPointer<SWMM2DMeshLayer> m_mesh;
    QPointer<SWMM2DResultsLayer> m_results;
    QPointer<AnimationController> m_anim;
    QPointer<SWMMVisProjectWindow> m_projectWindow;
    QVector<QPointF> m_scenePolyline;

    MeshProfilePlotWidget *m_plot = nullptr;
    MeshProfilePlotOptions *m_options = nullptr;

    ProfileSection::Definition m_definition;
    QList<QPointer<SWMM2DResultsLayer>> m_sectionSources;
    QVector<QMetaObject::Connection> m_sectionConnections;
    QHash<QString, QPair<quint64, QVector<ProfileSection::SeriesStation>>> m_seriesStations;
    MeshProfileTracksWidget *m_tracks = nullptr;
    MeshProfileSamplesModel *m_samplesModel = nullptr;
    MeshProfileSeriesEditor *m_seriesEditor = nullptr;
    QTableView *m_samplesTable = nullptr;
    QComboBox *m_savedSections = nullptr;
    QLabel *m_sectionStatus = nullptr;
    QWidget *m_figure = nullptr;
    QDateTime m_requestedTime;
    bool m_loadingDefinition = false;
    bool m_settingTimeFromAnimation = false;
    bool m_rebuildingProfile = false;
    bool m_seriesRefreshed = false; // set by refreshSectionSeries()
    bool m_contextValid = true;
    double m_verticalScale = 1.0;
    double m_mapUnitsPerMetre = 1.0;

    QAction *m_markerAction = nullptr;
    MeshProfileOverlay *m_overlay = nullptr;      // manually owned; canvas borrows
    MapToolProfileMarker *m_markerTool = nullptr; // parented to dialog
    QPointer<OpenSWMMVisMapTool> m_prevTool;      // restore on toggle-off/close

    // Cached profile — geometry/envelope stay static across animation frames;
    // only the depth column is re-sampled per tick (cheap vs a full rebuild).
    MeshProfileSampler::MeshProfile m_profile;
};

#endif // MESH_PROFILE_PLOT_DIALOG_H
