/**
 * @file      groundwaterassigndialog.h
 * @brief     Staged assignment of groundwater forcing to selected mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_GROUNDWATERASSIGNDIALOG_H
#define OPENSWMMVIS_GROUNDWATERASSIGNDIALOG_H
#include "assignment/groundwaterassignment.h"
#include "assignment/groundwatertransportassignment.h"
#include "assignment/meshassignmentsampling.h"
#include "assignment/rasterfluxintegration.h"
#include <QDialog>
#include <QPointer>
#include <QFutureWatcher>
#include <QJsonObject>
class SWMMModelLayer;
class SWMM2DMeshLayer;
class MapCanvas;
class SelectionManager;
class UnitSystem;
class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QCheckBox;
namespace openswmmvis::ui
{
class GroundwaterAssignmentEvents : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Create a QObject event relay whose owner can outlive the assignment dialog.
     * @param[in] parent Optional Qt owner; the dialog normally uses its model layer, falling
     * back to itself when no model exists.
     * @details Carries audit events from undo commands after the editor closes; owns no engine
     * or assignment state.
     */
    explicit GroundwaterAssignmentEvents(QObject *parent = nullptr) : QObject(parent) {}
signals:
    /**
     * @brief Publish assignment installation, removal or failure-after-write audit state.
     * @param[in] recipe Borrowed JSON payload during emission; contains snapshot
     * recipe/provenance and an event token such as applied, redone, undone or
     * failed-after-write. Receivers copy it if retaining it.
     * @param[in] installed True for applied/redone and failure-after-write events; false for
     * undone. True alone does not prove the full requested batch succeeded.
     * @details Relay lifetime follows its Qt parent, normally the model, so valid undo commands
     * can notify after the dialog is destroyed.
     */
    void changed(const QJsonObject &recipe, bool installed);
};
// One nonmodal, staged assignment workflow. Existing map rectangle/lasso tools
// and the selection bus remain usable while this editor is open.
class GroundwaterAssignDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @brief Build a nonmodal reviewed assignment workflow over guarded project objects.
     * @param[in] model Borrowed nullable model; its current engine identity is captured and
     * closure/destruction invalidates the dialog.
     * @param[in] mesh Borrowed nullable mesh; connects infiltration ownership to model and
     * tracks selected-cell context for later previews.
     * @param[in] canvas Borrowed nullable canvas; a live undo stack is required for an
     * applicable batch.
     * @param[in] selection Borrowed optional selection bus; selection changes invalidate
     * reviewed values.
     * @param[in] units Borrowed optional unit system read for length/rate labels, with
     * project-label fallbacks; unitsChanged invalidates context. Numerical property values are
     * not implicitly converted.
     * @param[in] parent Optional Qt owner of the dialog and controls/watchers.
     * @details Uses GUI-thread engine/UI access and immutable worker snapshots for
     * sampling/fingerprints. Missing required context disables/rejects the editor after
     * construction. Supports aquifer properties/initial state, flow/species sources, whole-mesh
     * infiltration ownership and ET/seepage policy reviews; opening the form does not apply
     * assignments.
     */
    GroundwaterAssignDialog(SWMMModelLayer *, SWMM2DMeshLayer *, MapCanvas *, SelectionManager *,
                            const UnitSystem *, QWidget *parent = nullptr);
    /**
     * @brief Request cancellation of an outstanding preview or source-verification watcher.
     * @details Does not wait synchronously for worker completion; value-captured jobs own their
     * input snapshots and do not dereference dialog/model widgets.
     */
    ~GroundwaterAssignDialog() override;
    /**
     * @brief Borrow the audit-event relay used by initial writes and subsequent undo/redo.
     * @return Guarded relay pointer or null after its owner is destroyed. Normally model-owned
     * and able to outlive this dialog; caller never deletes it.
     */
    GroundwaterAssignmentEvents *assignmentEvents() const { return m_events; }
public slots:
    /**
     * @brief Cancel outstanding work or reject an idle editor without applying a draft.
     * @details Clears policy/ownership readiness and disables Apply. With an active watcher,
     * requests cancellation, advances the serial and leaves the window open with cancelling
     * status; a later close can reject after work settles. Otherwise calls QDialog::reject().
     * Does not undo already committed batches.
     */
    void reject() override;
signals:
    /**
     * @brief Notify that a reviewed operation was applied or transport reported writes before
     * failure.
     * @details Emitted after successful process, ownership or property/transport commits. A
     * failed transport commit with changed=true also emits when an event relay exists, so
     * receivers must refresh actual engine state rather than assume all requested values
     * succeeded. Undo/redo notifications use assignmentEvents() instead.
     */
    void applied();
    /**
     * @brief Publish the successful property/transport snapshot recipe for persistence/audit.
     * @param[in] recipe Borrowed JSON snapshot during emission, including source hashes, chosen
     * cell IDs, units/sampling/source details and an operation ID; copy to retain it.
     * @details Emitted only by successful commitVerified(), not process/ownership branches or
     * failed-after-write outcomes. The recipe is an audit snapshot, not a live source refresh
     * binding.
     */
    void recipeAccepted(const QJsonObject &recipe);
private slots:
    /**
     * @brief Drop engine access, cancel work and reject the disabled editor after context loss.
     * @details Clears the borrowed engine, advances the serial, requests watcher cancellation,
     * clears ordinary preview readiness and calls QDialog::reject() directly. No synchronous
     * join or assignment write occurs.
     */
    void invalidateContext();
    /**
     * @brief Invalidate every reviewed draft when controls or selection change.
     * @details Clears process/ownership/ordinary readiness, increments the serial, disables
     * Apply and requests active watcher cancellation. Does not erase displayed results or change
     * the engine.
     */
    void invalidatePreview();
    /**
     * @brief Prepare a reviewable immutable assignment proposal without changing model values.
     * @details No-ops during existing work or without live model/mesh/engine. Process and
     * ownership targets review whole-mesh snapshots synchronously; ordinary
     * property/quality/source targets capture sorted scope, mesh path/CRS/revisions and engine
     * authoring state, then run copied sampling/preview jobs. Spatial properties use centroid
     * sampling; explicit raster flux-density mode integrates pixel-cell area in declared units.
     * Hashes all GDAL-listed source files before and after sampling, rejects changed/invalid
     * sources, and supports cooperative cancellation. FLOW uses m³/s independent of project
     * units; quality/MASS terms keep species-native units and region totals are restricted to
     * manual input. No live engine handle or widget pointer is passed into the worker;
     * finishPreview() gates publication.
     */
    void preview();
    /**
     * @brief Recheck and apply exactly a prepared review, verifying source bytes when needed.
     * @details Process/ownership branches validate current mesh/scope and expected engine
     * snapshots, install their changes and push dedicated first-redo-skipping undo commands.
     * Ordinary previews first recheck context; manual inputs commit directly, while file-backed
     * inputs asynchronously rehash the reviewed file set and compare serial/hash equality before
     * commitVerified(). Missing owner/context, cancellation, changed source or helper error
     * keeps the batch unapplied or reports the helper's failure; this method does not resample
     * silently.
     */
    void apply();

private:
    struct WorkResult {
        assignment::AquiferPreview preview;
        assignment::GroundwaterTransportPreview transport;
        bool isTransport = false, isFlux = false;
        assignment::RasterFluxResult flux;
        QVector<double> values;
        QMap<QString, QByteArray> fingerprints;
        QString error, summary;
        bool cancelled = false;
    };
    /**
     * @brief Invalidate the review and configure target-specific form visibility/units.
     * @details Switches among ownership, shared process policy, initial chemistry, flow sources
     * and scalar properties. Explains native concentration, fixed m³/s flow and project
     * property-unit conventions; quality layer controls are enabled only for layered initial
     * state. Does not convert numbers or apply a target.
     */
    void updateTarget();
    /**
     * @brief Append an owned concentration/MASS source-term row with catalog-based units.
     * @details Creates species/kind/series controls, initializes constant zero, connects all
     * changes to preview invalidation and refreshes accessible row identities. Species
     * concentration and mass-rate units come from the captured catalog; unsupported mass bases
     * remain visibly unsupported, not automatically converted.
     */
    void addSpeciesTerm();
    /**
     * @brief Refresh per-row/column accessible names for the initialized species-term table.
     * @details Labels embedded controls and constant items, adding displayed native-unit text to
     * unit-label descriptions. Does not change term values, preview readiness or engine state
     * itself.
     */
    void refreshTermAccessibility();
    /**
     * @brief Rebuild loaded-layer choices for the selected feature/raster input route.
     * @details Clears selection to the choose-file entry, then copies matching canvas layer
     * names/IDs when a canvas exists. Manual route adds no loaded layers; no files are opened
     * here.
     */
    void populateSources();
    /**
     * @brief Invalidate the review and configure source, sampling and coverage controls.
     * @details Enables relevant manual/vector/raster fields, with density/coverage controls only
     * for raster FLOW integration. For a matching loaded layer, copies its source path and
     * optional OGR layer name into the form. Does not read/sample the dataset or infer
     * physical-unit conversion.
     */
    void sourceChanged();
    /**
     * @brief Resolve the current target/scope controls into sorted unique mesh cell IDs.
     * @param[out] error Non-null message destination; written on failure or empty ordinary
     * selection and not cleared on success.
     * @return Owned ascending zero-based cells for keyed map selection, all cells, exact tag,
     * comma-separated one-based IDs or valid nonempty polygon WKT centroid intersections in mesh
     * CRS. Invalid scope/range returns empty with an error.
     * @details Process/ownership targets always return every mesh cell, or empty without a mesh,
     * bypassing ordinary empty-scope errors. Polygon holes are allowed and boundary
     * intersections count; no coordinate transformation or engine mutation occurs.
     */
    QVector<int> scope(QString *error) const;
    /**
     * @brief Check engine identity and the exact mesh/scope context captured by preview.
     * @param[out] error Non-null failure message destination, unchanged on success.
     * @return True when guarded model/mesh and engine identity survive, geometry/attribute
     * revisions, source path and CRS match, and current sorted scope equals m_scope; false
     * otherwise.
     * @details Does not itself validate engine editability, undo-stack availability, source-file
     * bytes or every model option; those checks belong to the commit helpers/source
     * verification.
     */
    bool currentContext(QString *error) const;
    /**
     * @brief Install the retained property/transport proposal after final context/undo-owner
     * checks.
     * @details Requires an ordinary ready preview and the previously completed source
     * verification when file-backed. Calls the appropriate assignment helper; failed transport
     * writes can emit failed-after-write/applied with rollbackComplete metadata, while other
     * failures invalidate review and report status. On success stores source SHA-256 in the
     * audit recipe, pushes a guarded first-redo-skipping AssignmentUndo, refreshes mesh
     * ownership, emits relay/applied/recipeAccepted and invalidates the consumed review.
     * Subsequent relay callbacks can outlive this dialog; partial-failure reporting does not
     * imply rollback succeeded.
     */
    void commitVerified();
    /**
     * @brief Consume a finished sampling future and expose only current successful reviewed
     * values.
     * @details Detaches/deletes the watcher, catches future errors, rejects cancellation/stale
     * engine/mesh/scope and helper errors, then stores the immutable result. Shows at most 500
     * changed/assigned cells, source coverage/flow summaries and enables Apply only for a
     * nonempty change. Updates raster integral audit metadata in m² and m³/s; no engine mutation
     * occurs. Caller must invoke after the matching watcher has finished.
     */
    void finishPreview();
    QPointer<GroundwaterAssignmentEvents> m_events;
    QPointer<SWMMModelLayer> m_model;
    QPointer<SWMM2DMeshLayer> m_mesh;
    QPointer<MapCanvas> m_canvas;
    QPointer<SelectionManager> m_selection;
    SWMM_Engine m_engine = nullptr;
    quint64 m_geometry = 0, m_attributes = 0, m_serial = 0;
    QString m_meshPath, m_meshCrs, m_length, m_rate;
    QVector<int> m_scope;
    QJsonObject m_recipe;
    WorkResult m_reviewed;
    assignment::InfiltrationOwnershipPreview m_ownership;
    assignment::AquiferProcessPreview m_process;
    bool m_haveProcess = false;
    QWidget *m_processControls = nullptr;
    QComboBox *m_processEt = nullptr, *m_processLink = nullptr;
    QCheckBox *m_wiltingAuto = nullptr;
    QDoubleSpinBox *m_wilting = nullptr;
    bool m_haveOwnership = false;
    QCheckBox *m_removeInfilOverrides = nullptr;
    bool m_havePreview = false, m_committing = false;
    QFutureWatcher<WorkResult> *m_watcher = nullptr;
    QComboBox *m_target = nullptr, *m_route = nullptr, *m_scopeChoice = nullptr,
              *m_source = nullptr, *m_sampling = nullptr;
    QComboBox *m_densityUnits = nullptr, *m_coverage = nullptr;
    QComboBox *m_species = nullptr, *m_zone = nullptr, *m_distribution = nullptr,
              *m_flowSeries = nullptr;
    QLineEdit *m_sourceName = nullptr;
    QWidget *m_transportControls = nullptr, *m_sourceControls = nullptr;
    QVector<assignment::TransportSpecies> m_speciesCatalog;
    QLineEdit *m_scopeText = nullptr, *m_path = nullptr, *m_layerName = nullptr, *m_field = nullptr,
              *m_filter = nullptr;
    QDoubleSpinBox *m_value = nullptr, *m_scale = nullptr, *m_offset = nullptr;
    QSpinBox *m_band = nullptr, *m_qualityLayer = nullptr;
    QCheckBox *m_skip = nullptr, *m_selectedFeatures = nullptr;
    QLabel *m_status = nullptr, *m_units = nullptr;
    QPushButton *m_preview = nullptr, *m_apply = nullptr;
    QTableWidget *m_table = nullptr, *m_terms = nullptr;
};
}
#endif
