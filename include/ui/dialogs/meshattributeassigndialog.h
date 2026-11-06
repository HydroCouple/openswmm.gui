/**
 * @file      meshattributeassigndialog.h
 * @brief     Assign per-cell 2D mesh parameters from a GIS source: a raster band or a vector
 *            layer's attribute field, mapped onto the mesh cells in one undoable step.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Assign per-cell 2D mesh parameters from a GIS source: a raster band or a
 * vector layer's attribute field, mapped onto the mesh cells in one undoable
 * step.
 *
 * Complements the mesh-editing toolbar's cell editor — that prescribes one
 * value to a hand-picked selection; this prescribes a spatially varying field
 * to the whole mesh (or the current selection).
 *
 * Four mapping modes (GUI plan §3.4, phase GG0e):
 *
 *  - **Single numeric target** — the original behaviour: one band / one field
 *    → one mesh::cellParamSpecs() target, sampled at the cell centroid.
 *  - **Multiple numeric targets** — N bands / N fields → N parameters in one
 *    pass (Horton's f0 / fmin / decay together), still one undo entry.
 *  - **Classified infiltration lookup** — a categorical key field (landuse
 *    code, hydrologic soil group), optionally a second key (Curve Number is
 *    assigned by landuse × HSG), mapped through an editable, CSV-round-tripped
 *    mesh::InfilLookupTable to a whole mesh::InfilRow.
 *  - **Natural-neighbour interpolation** — for scattered point sources rather
 *    than coverages; a *sampling* choice rather than a target choice, so it
 *    composes with the numeric modes. Same mesh::NaturalNeighbourInterpolator
 *    and the same wording the mesh generation dialog uses for elevation.
 *
 * Sampling is orthogonal to the mode: cell centroid (point sample),
 * area-weighted mean, majority (largest covering share), or natural
 * neighbour. Overlay sampling is bound to the source's declared type —
 * majority for categorical, area-weighted mean for continuous — with an
 * explicit user override, because offering the wrong one silently produces
 * plausible nonsense.
 *
 * Numeric writes go through mesh::pushCellParamEdits and infiltration writes
 * through mesh::pushCellInfilEdit, so the result lands on the same undo stack
 * as every other cell edit and every view refreshes through the layer's
 * `attributeChanged` signal. Multi-target and multi-row assignments are
 * wrapped in a single QUndoStack macro so a whole assignment is one Ctrl+Z.
 *
 * Sampling runs on a QtConcurrent worker guarded by a QFutureWatcher, exactly
 * as MeshGenerationDialog does: the Job carries plain data only (no widgets,
 * no layers, no GDAL handles) and the worker RE-OPENS every raster / vector
 * source by path on its own thread, because GDAL/OGR handles must not cross
 * threads. Nothing is written to the mesh until the worker has finished
 * successfully, so cancelling mid-run leaves the mesh untouched.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_MESHATTRIBUTEASSIGNDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_MESHATTRIBUTEASSIGNDIALOG_H

#include "assignment/meshassignmentsampling.h"
#include "mesh/meshinfil.h"
#include "mesh/naturalnbinterpolator.h"

#include <QByteArray>
#include <QDialog>
#include <QFutureWatcher>
#include <QPointF>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class MapCanvas;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QCheckBox;
class QTableWidget;
class SelectionManager;
class SWMM2DMeshLayer;

namespace openswmmvis::ui
{

class MeshAttributeAssignDialog : public QDialog
{
    Q_OBJECT

public:
    using Source = assignment::MeshAssignmentSampling::Source;
    using Mode = assignment::MeshAssignmentSampling::Mode;
    using Sampling = assignment::MeshAssignmentSampling::Sampling;
    using WriteTarget = assignment::MeshAssignmentSampling::WriteTarget;
    using Job = assignment::MeshAssignmentSampling::Job;
    using SampleResult = assignment::MeshAssignmentSampling::SampleResult;

    /**
     * @brief Build a GIS-to-mesh sampling draft over guarded borrowed GUI objects.
     * @param[in] meshLayer Nullable destination mesh, held by QPointer; no ownership transfer.
     * @param[in] canvas Nullable canvas providing source layers and optional undo stack, held by
     * QPointer.
     * @param[in] selection Nullable selection manager for the selected-cell scope, held by
     * QPointer.
     * @param[in] initialSource Initial Raster/Vector radio choice; does not open a source
     * dataset.
     * @param[in] depthUnitLabel Borrowed project depth label copied for parameter captions; no
     * numeric conversion is inferred from it.
     * @param[in] parent Optional Qt parent owning this dialog.
     * @details GUI-thread access only; sampling jobs own copied paths/geometry and reopen
     * datasets on worker threads. Source-layer combo payloads are raw borrowed pointers, so
     * caller must keep those source objects alive while the dialog reads their metadata. Caller
     * must keep mesh geometry/context stable across a run: completion does not compare a
     * revision/expected baseline before applying sampled cell indices.
     */
    MeshAttributeAssignDialog(SWMM2DMeshLayer *meshLayer, MapCanvas *canvas,
                              SelectionManager *selection, Source initialSource,
                              const QString &depthUnitLabel, QWidget *parent = nullptr);

public slots:
    /**
     * @brief Request cancellation while sampling, otherwise reject the dialog.
     * @details A running watcher is canceled cooperatively, Stop disabled and the dialog left
     * open until completion; a later close/reject is needed. Cancellation before publication
     * leaves this run unapplied. Does not undo earlier successful assignments or synchronously
     * wait for the worker.
     *
     * @details Esc / Close / the window button. While a sampling pass is
     *         running this cancels it instead of closing, so the dialog never
     *         disappears with a worker still writing into its watcher.
     */
    void reject() override;

private slots:
    /**
     * @brief Gate source-specific controls and rebuild multi-target band/field editors.
     * @details Raster uses one-based bands and scale/offset for numeric modes; vector uses
     * copied field names and optional selected IDs. Replacing each row editor resets its prior
     * band/field choice. Rebuilds classified key labels/visibility and button gates without
     * opening datasets or writing mesh values.
     */
    void onSourceChanged();
    /**
     * @brief Recompute Preview/Apply availability after single-target selection.
     * @details Uses registry enablement/source presence checks; does not validate source
     * readability or execute sampling.
     */
    void onTargetChanged();
    /**
     * @brief Show the selected mapping controls and restrict meaningless classified sampling
     * modes.
     * @details Seeds one numeric target row when needed, disables natural-neighbour/area-mean
     * for classified keys and changes an incompatible current method to centroid. Reapplies
     * source/sampling gates and can reset target source editors; all changes are local draft
     * state.
     */
    void onModeChanged();
    /**
     * @brief Toggle interpolation-variant controls and require a vector source for natural
     * neighbour.
     * @details Natural neighbour selected with Raster switches the radio to Vector, then
     * refreshes availability. Does not validate vector geometry type or execute interpolation.
     */
    void onSamplingChanged();
    /**
     * @brief Start a new sampled preview using the current draft.
     * @details Delegates startSampling(false); does not reuse cached results or publish mesh
     * changes.
     */
    void onPreview();
    /**
     * @brief Resample the current draft and request publication on successful completion.
     * @details Delegates startSampling(true); keeps the editor open and does not synchronously
     * alter the mesh.
     */
    void onApply();
    /**
     * @brief Choose a raster path and add/select it in the source combo.
     * @details Cancel is a no-op. Stores the path/filename without opening or validating the
     * dataset; source I/O happens in the sampling worker.
     */
    void onBrowseRaster();
    /**
     * @brief Route the Close/Stop action through reject() cancellation semantics.
     * @details Running work remains visible until its completion handler; idle use rejects
     * without rolling back earlier assignments.
     */
    void onCancelOrClose();
    /**
     * @brief Consume the worker outcome on the GUI thread and optionally publish sampled values.
     * @details Detaches/deletes the watcher later, restores controls and catches
     * result-retrieval exceptions into status text. Canceled/error runs do not apply; previews
     * only report and empty successful applies do nothing. Successful applies dispatch using
     * m_runJob mode/target keys, but infiltration destination/inheritance choices are read from
     * current controls at completion. Adds no source hash, geometry revision or expected-value
     * concurrency check.
     */
    void onSampleFinished();

    // Multi-target table
    /**
     * @brief Append a numeric registry-parameter combo and source-specific editor.
     * @details Uses a one-based raster band default row+1 or current vector fields. Owns widgets
     * through the table and updates action gates; no data publication.
     */
    void onAddTargetRow();
    /**
     * @brief Remove the current multi-target draft row when selected.
     * @details Recomputes buttons; does not undo any previously applied assignment.
     */
    void onRemoveTargetRow();

    // Classified lookup table
    /**
     * @brief Append an infiltration lookup row with method/parameter/destination editors.
     * @details First row is a noneditable unmatched-key fallback; later key cells begin empty.
     * Method changes locate their widget's current row to survive earlier removals. All
     * destination choices remain available with prerequisites in tooltips, and unused parameter
     * cells are masked. Does not validate/publish complete row values.
     */
    void onAddLookupRow();
    /**
     * @brief Remove the current classified draft row except fallback row zero.
     * @details Empty/no selection and fallback are ignored; preserves a fallback row and
     * refreshes action gates.
     */
    void onRemoveLookupRow();
    /**
     * @brief Load a selected CSV into the local classified draft table.
     * @details Cancel/read failure leaves prior draft intact; failure reports a status
     * diagnostic. Success replaces controls via applyLookupTable() and reports nonfallback row
     * count; no mesh assignment occurs.
     */
    void onLoadLookupCsv();
    /**
     * @brief Serialize the current classified draft to a selected CSV path.
     * @details Cancel is a no-op; writes the collected fallback/entries through
     * saveLookupTableCsv() and reports its result. Draft parsing conventions, including omitted
     * blank keys/invalid numeric NaNs, apply. File output is independent of mesh Apply and is
     * not added to undo.
     */
    void onSaveLookupCsv();
    /**
     * @brief Show/hide the second key controls/column and refresh availability.
     * @details Preserves hidden key contents without changing the mesh or sampling immediately.
     */
    void onTwoKeyToggled();

private:
    /**
     * @brief Create owned mapping/source/sampling/scope/status/action controls once.
     * @param[in] initialSource Initial Raster/Vector radio choice.
     * @param[in] depthUnitLabel Borrowed project label passed to numeric parameter captions.
     * @details Captures an initial count for the selected-cell radio availability, seeds
     * classified fallback, wires Stop/Close and sets All cells initially. Requires uninitialized
     * control members. Source combos/worker are not populated/launched by this helper itself.
     */
    void buildUi(Source initialSource, const QString &depthUnitLabel);
    /**
     * @brief Build owned single- and multiple-numeric target panels once.
     * @param[in] depthUnitLabel Borrowed unit caption for registry-parameter labels; no value
     * conversion.
     * @details Skips enum targets and disables unsupported registry parameters; target table
     * begins empty until multi-mode selection seeds it.
     */
    void buildTargetGroup(const QString &depthUnitLabel);
    /**
     * @brief Build the owned one/two-key infiltration lookup table and CSV actions once.
     * @details Creates a fallback-only seed plus an empty mapping entry; method parameters
     * retain InfilRow project conventions and disabled unused slots. No engine
     * validation/assignment occurs.
     */
    void buildLookupGroup();
    /**
     * @brief Append raster/vector canvas layers as borrowed pointer combo payloads.
     * @details No-op without canvas; does not clear previous entries or subscribe to layer
     * removal/destruction. Source layer objects must remain alive while these raw payloads are
     * read. Refreshes vector fields after enumeration; mesh/canvas QPointers do not guard
     * individual source payloads.
     */
    void populateLayerCombos();
    /**
     * @brief Refresh field choices from the currently selected live vector layer.
     * @details Clears single/key combos, copies all field names without numeric filtering and
     * restores matching prior text. Updates existing multi-target field combos too. Early
     * incomplete UI/no layer returns; no source dataset is opened independently and raw combo
     * payload lifetime is caller-managed.
     */
    void refreshVectorFields();
    /**
     * @brief Enable Preview/Apply from basic current target/source/mesh presence.
     * @details Ignores incomplete UI and a running watcher. Single mode requires an enabled
     * registry key, multi mode at least one valid key, classified mode more than fallback plus a
     * key/source choice. Does not validate nonempty scope, source file readability, field
     * numeric type or complete infiltration parameters; collectJob()/worker handle later checks.
     */
    void updateButtons();
    /**
     * @brief Toggle progress/action presentation for a sampling run.
     * @param[in] running True disables Preview/Apply and mapping-mode selection and labels Close
     * as Stop; false restores action gates.
     * @details Does not disable every source/target/lookup/write control, cancel a worker or
     * publish data. Captured m_runJob protects most sampling choices from later UI edits, while
     * infiltration write-target/inheritance controls remain read at completion.
     */
    void setRunning(bool running);

    /**
     * @brief Append numeric cell-parameter registry choices and disable unsupported targets.
     * @param[in,out] combo Non-null borrowed combo; caller owns it, and existing items are not
     * cleared.
     * @param[in] depthUnitLabel Borrowed depth caption for display labels, copied by created
     * items.
     * @details Omits enumeration targets (classified mode assigns methods), stores exact
     * registry byte keys/tooltips and relies on a QStandardItemModel to grey unsupported
     * entries.
     *
     * @details Fill a numeric-parameter combo with the cellParamSpecs() entries
     *         (disabled ones greyed, as the original target combo does).
     */
    void fillParamCombo(QComboBox *combo, const QString &depthUnitLabel) const;

    /**
     * @brief Read the selected mapping mode stored in combo item data.
     * @return SingleNumeric, MultiNumeric or ClassifiedInfil in the initialized UI; no
     * validation beyond QVariant integer conversion.
     */
    [[nodiscard]] Mode currentMode() const;
    /**
     * @brief Read the selected sampling algorithm stored in combo data.
     * @return Current Sampling enum cast from item data; assumes an initialized valid combo and
     * does not resolve OverlayAuto yet.
     */
    [[nodiscard]] Sampling currentSampling() const;

    /**
     * @brief Snapshot candidate cell indices for the current All/Selected scope.
     * @return Empty without mesh/selection or matches, otherwise all 0..N-1 cells or valid
     * selected MeshCell references matching this mesh source-path layer key. Does not
     * independently deduplicate repeated references or preserve selection during a run.
     *
     * @details Triangle indices in scope (all cells, or the selected ones).
     */
    [[nodiscard]] QVector<int> scopeTriangles() const;

    /**
     * @brief Copy area centroids for a supplied cell-index sequence in mesh CRS coordinates.
     * @param[in] tris Borrowed valid zero-based cells of the current mesh, in desired output
     * order.
     * @return Owned parallel coordinates, or empty without mesh. Uses cellCentroid() for
     * triangle/quad geometry; no CRS transform or geometry revision snapshot beyond copied
     * coordinates.
     *
     * @details Cell centroids, in the mesh layer's own CRS, parallel to \p tris.
     */
    [[nodiscard]] QVector<QPointF> centroidsFor(const QVector<int> &tris) const;

    /**
     * @brief Copy current GUI configuration/geometry/source paths into a worker-safe sampling
     * job.
     * @param[in,out] job Non-null caller-owned fresh/default Job; vectors are appended in some
     * branches, so reusing a populated job is unsupported. Failure can leave it partially
     * populated.
     * @param[out] err Non-null caller-owned diagnostic destination, assigned on validation
     * failure and not cleared on success.
     * @return False for unavailable mesh, empty scope, missing source path/field/targets or
     * unusable classified table; true after copying a complete basic configuration, without
     * opening/validating dataset contents.
     * @details Copies centroids, optional four-vertex footprint strides (triangle repeats v0),
     * mesh CRS WKT, exact target keys/min/max, raster bands or vector layer/filter/selected IDs,
     * classified keys/rows and interpolation variant. Jobs own plain values only; GUI/raw source
     * layer metadata is read synchronously before worker launch. Numeric parameter units remain
     * registry conventions, with explicit scale/offset rather than inferred GIS physical units.
     *
     * @details Collect the whole sampling configuration into a worker-safe Job.
     */
    [[nodiscard]] bool collectJob(Job *job, QString *err) const;

    /**
     * @brief Collect a fresh job and launch copied-value sampling on QtConcurrent.
     * @param[in] apply False reports a preview, true publishes successful nonempty results from
     * the completion handler.
     * @details Ignores a running watcher and reports collectJob() failures without launching.
     * Stores m_runJob/m_applyPending, creates a dialog-owned watcher and connects
     * progress/completion before launch. Worker owns its GDAL datasets/transforms; it never
     * dereferences mesh/widgets or publishes edits. Caller must maintain destination
     * geometry/context until completion; no expected-state guard is captured here.
     *
     * @details Start the worker for a preview (\p apply false) or an apply.
     */
    void startSampling(bool apply);

    /**
     * @brief Copy valid enabled parameter keys from multi-target rows in order.
     * @return Owned byte-key vector; missing/unsupported rows are skipped, duplicate keys
     * retained and no source band/field validation performed.
     *
     * @details Read the numeric-target rows (key + band/field) off the table.
     */
    [[nodiscard]] QVector<QByteArray> collectTargetKeys() const;

    /**
     * @brief Replace one target row's source editor for current Raster/Vector mode.
     * @param[in] row Zero-based target-table row; out-of-range is ignored.
     * @details New raster spin defaults to row+1 in range 1..512; vector combo copies current
     * raw layer field names. Table owns widgets and replacing one loses its prior band/field
     * selection. Assumes any stored source-layer pointer remains live.
     *
     * @details Install the column-1 editor for a multi-target row: a band spin
     *         box for a raster source, a field combo for a vector one.
     */
    void setTargetRowSourceWidget(int row);

    /**
     * @brief Parse fallback and keyed infiltration draft rows without semantic validation.
     * @return Owned InfilLookupTable carrying two-key flag/labels, row-zero fallback and
     * nonempty first-key entries. Used numeric cells parse with toDouble; invalid/blank cells
     * retain default NaNs, unused slots ignored, duplicate keys retained.
     * @details Method ordinals assume combo order begins at InfilMethod::None and destinations
     * use their zero-based enum indexes. Parameters use native InfilRow project units; source
     * classification keys are trimmed strings with no scale/offset.
     *
     * @details Read the lookup-table editor into a mesh::InfilLookupTable.
     *         Row 0 is the "unmatched keys" fallback.
     */
    [[nodiscard]] mesh::InfilLookupTable collectLookupTable() const;
    /**
     * @brief Replace classified editor controls from a supplied lookup table.
     * @param[in] t Borrowed table copied into owned controls; does not retain its address.
     * @details Recreates fallback plus entries, stores finite parameters with default QString
     * precision and blanks nonfinite values, sets method/destination combos and masks unused
     * slots. Updates two-key visibility and buttons; no mesh/native mutation or row-validity
     * check.
     */
    void applyLookupTable(const mesh::InfilLookupTable &t);
    /**
     * @brief Show the second key column and enable its controls only when two-key mode is
     * checked.
     * @details Does not erase hidden key values or rebuild lookup data; source-specific
     * visibility remains controlled separately.
     */
    void rebuildLookupColumns();
    /**
     * @brief Enable used infiltration parameter cells and blank unused slots with an em dash.
     * @param[in] row Existing table row whose method widget is queried; absent combo is ignored.
     * @details Uses infilUsesParam()/infilParamLabel(). Re-enabling an em-dash cell makes it
     * empty; method changes can discard previous unused parameter text, with no engine write.
     *
     * @details Grey out the parameter cells the row's method does not use, the
     *         same masking mesh::infilUsesParam() drives elsewhere.
     */
    void maskLookupRow(int row);

    /**
     * @brief Format sampled/ignored counts, resolved overlay method and region-tag
     * correspondence.
     * @param[in] r Borrowed result with parallel sampled identities/values and worker counters.
     * @return Owned status text describing candidate cells, no-data/non-numeric/range skips and
     * unmatched-key fallback. Current UI mode/current mesh tags influence the optional
     * region-default suggestion; no values are applied.
     *
     * @details Human-readable "n of m cells …" summary of \p r.
     */
    [[nodiscard]] QString summarise(const SampleResult &r) const;

    /**
     * @brief Dispatch a successful sampling result using the captured run's mapping mode.
     * @param[in] r Borrowed result; numeric values correspond to m_runJob.targetKeys, classified
     * rows/keys follow sampled cells.
     * @details GUI-thread publication via existing command funnels; no independent
     * geometry/context/concurrency validation, file save or rollback transaction.
     *
     * @details Write \p r to the mesh as one undo entry.
     */
    void applyResult(const SampleResult &r);
    /**
     * @brief Queue finite per-target sampled values through the mesh parameter command funnel.
     * @param[in] r Borrowed result; uses the shorter triangle/value extent per target and
     * ignores targets beyond captured keys.
     * @details Missing destination returns. Skips nonfinite values, groups multiple nonempty
     * targets in one undo macro when a canvas stack exists and otherwise applies directly.
     * Return counts from pushCellParamEdits are candidate attempts, not confirmed setter
     * successes; no aggregate atomic rollback or expected-baseline comparison. Updates status
     * with those counts.
     */
    void applyNumericResult(const SampleResult &r);
    /**
     * @brief Apply classified rows as current per-cell overrides or region defaults.
     * @param[in] r Borrowed result; triangle/row vectors are read to their shorter extent.
     * @details Reads current Write Defaults/Keep Inherited controls, not captured run values.
     * Default mode delegates by tag; override mode groups equal rows, optionally skips exactly
     * equal inherited rows and uses provenance-preserving command funnels. Multiple groups share
     * one undo macro if a stack exists; otherwise direct edits are not undoable. Counts
     * attempted candidates and does not guarantee whole-assignment atomicity or validate a
     * geometry revision.
     */
    void applyInfilResult(const SampleResult &r);
    /**
     * @brief Publish one classified infiltration default per sampled key matching a current mesh
     * tag.
     * @param[in] r Borrowed result with parallel source keys/rows, read to their shorter extent.
     * @details Skips empty/duplicate keys, takes the first row per key and reports unknown tags.
     * One command updates existing region tags; all cells inheriting those tags can be affected
     * beyond the sampled/selected subset, while explicit overrides remain overrides. No
     * destination returns; counts candidate default rows, with optional undo/direct fallback and
     * no whole-edit rollback.
     *
     * @details WriteTarget::RegionDefaults — one [2D_INFILTRATION_DEFAULTS] row
     *  per source key naming an existing region tag, through
     *  mesh::pushInfilDefaultsEdit. Keys matching no tag are skipped and
     *  reported: a row for an unknown tag reaches no cell.
     */
    void applyInfilDefaultsResult(const SampleResult &r);

    QPointer<SWMM2DMeshLayer> m_mesh;
    QPointer<MapCanvas> m_canvas;
    QPointer<SelectionManager> m_selection;

    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_targetCombo = nullptr; // SingleNumeric
    QGroupBox *m_singleGroup = nullptr;
    QGroupBox *m_multiGroup = nullptr;
    QTableWidget *m_targetTable = nullptr; // MultiNumeric
    QPushButton *m_addTargetBtn = nullptr;
    QPushButton *m_delTargetBtn = nullptr;
    QGroupBox *m_lookupGroup = nullptr; // ClassifiedInfil
    QTableWidget *m_lookupTable = nullptr;
    QComboBox *m_key1Combo = nullptr;
    QComboBox *m_key2Combo = nullptr;
    QSpinBox *m_key1BandSpin = nullptr;
    QSpinBox *m_key2BandSpin = nullptr;
    QCheckBox *m_twoKeyCheck = nullptr;
    QLabel *m_key1Label = nullptr;
    QLabel *m_key2Label = nullptr;

    QRadioButton *m_srcRaster = nullptr;
    QRadioButton *m_srcVector = nullptr;
    QComboBox *m_rasterCombo = nullptr;
    QPushButton *m_browseBtn = nullptr;
    QSpinBox *m_bandSpin = nullptr;
    QDoubleSpinBox *m_scaleSpin = nullptr;
    QDoubleSpinBox *m_offsetSpin = nullptr;
    QComboBox *m_vectorCombo = nullptr;
    QComboBox *m_fieldCombo = nullptr;
    QCheckBox *m_selectedOnly = nullptr;

    QComboBox *m_samplingCombo = nullptr;
    QComboBox *m_nnVariantCombo = nullptr;
    QLabel *m_nnVariantLbl = nullptr;

    QRadioButton *m_writeOverrides = nullptr;
    QRadioButton *m_writeDefaults = nullptr;
    QCheckBox *m_keepInherited = nullptr;
    QGroupBox *m_writeGroup = nullptr;

    QRadioButton *m_scopeAll = nullptr;
    QRadioButton *m_scopeSelected = nullptr;
    QLabel *m_statusLbl = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_previewBtn = nullptr;
    QPushButton *m_applyBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;

    /*! Path typed via Browse… (empty when a canvas layer is chosen). */
    QString m_browsedRasterPath;
    /*! UnitSystem::depthLabel(), kept so the multi-target rows label their
     *  length parameters the same way the single-target combo does. */
    QString m_depthUnitLabel;

    /*! Live sampling worker; null when idle. Owned via QObject parenting and
     *  cleared in onSampleFinished(). */
    QFutureWatcher<SampleResult> *m_watcher = nullptr;
    /*! Configuration the running / last-finished pass used. The result carries
     *  values, not the target keys they belong to, so the completion handler
     *  reads them back from here. */
    Job m_runJob;
    /*! True when the running pass should write its result to the mesh. */
    bool m_applyPending = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESHATTRIBUTEASSIGNDIALOG_H
