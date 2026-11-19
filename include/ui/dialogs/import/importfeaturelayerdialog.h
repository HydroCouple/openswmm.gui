/**
 * @file      importfeaturelayerdialog.h
 * @brief     FEATURE_LAYER_TO_SWMM_IMPORT — the "Import Feature Layer → SWMM Objects" dialog
 *            (view).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — the "Import Feature Layer → SWMM
 * Objects" dialog (view). Programmatic buildUi (aboutdialog pattern),
 * geometry persisted via dialoglayoutpersistence. The dry run executes
 * on a QtConcurrent worker (MeshGenerationDialog idiom); the import
 * itself runs on the GUI thread as one undo macro.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTFEATURELAYERDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTFEATURELAYERDIALOG_H

#include "ui/dialogs/import/importmapping.h"
#include "ui/dialogs/import/importplan.h"

#include <QDialog>
#include <QFutureWatcher>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableView;

class GISVectorLayer;
class MapCanvas;
class SWMMModelLayer;
class SWMMVisProjectWindow;

namespace openswmmvis::import
{

class ImportMappingModel;
class ImportPreviewModel;

class ImportFeatureLayerDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] projectWindow Optional borrowed RAW project window; capture canvas/model pointers
     *   ONCE, host must preserve all until dialog destruction/import callbacks.
     * @param[in] parent Optional QWidget owner, not necessarily project owner; dialog owns
     *   UI/mapping/preview models/future watcher.
     * @brief Build main-thread import configuration, initial kind/source/presets, apply
     *   always-on-top policy and default size.
     * @note No project-change/layer-removal rebind subscription; raw combo pointers must remain
     *   alive. Preview captures plain values to worker; construction itself creates no SWMM
     *   objects, file import or undo step.
     */
    explicit ImportFeatureLayerDialog(SWMMVisProjectWindow *projectWindow,
                                      QWidget *parent = nullptr);
    /**
     * @brief If previewRunning flag set, synchronously wait for worker completion before destroying
     *   owned UI/models.
     * @note Does not cancel reader/planner, process results/rollback import or explicitly persist
     *   presets/layout here. Wait can block GUI until full dataset read finishes; worker captures
     *   plain data and no dialog/source pointers. Host closes before deleting raw project context.
     */
    ~ImportFeatureLayerDialog() override;

    /*! Pre-select \p layer in the source combo (context-menu launch).
     * @param[in] layer Borrowed pointer identity looked up in CURRENT compatible source combo, null
     *   normally absent.
     * @note Select matching row and trigger normal source-change reset; absent/filtered-out source
     *   ignored. Does not refresh combo/change target kind or add source to canvas. Equal current
     *   row may emit nothing.
     */
    void setSourceLayer(GISVectorLayer *layer);

protected:
private slots:
    /**
     * @brief Show link endpoint options as appropriate, refilter source choices, rebuild mapping
     *   attributes for current kind/source, refresh source text/presets and clear preview.
     * @note Preserve surviving key/default bindings via model reset, not full mapping replacement;
     *   options retained. During active worker this does not cancel/epoch-tag job, so its later
     *   completion can mark old-kind plan current again. No source/engine write.
     */
    void onKindChanged();
    /**
     * @brief Rebuild mapping rows for current source fields, refresh source text, clear
     *   preview/summary and mark plan stale.
     * @note Existing same-key source bindings kept only exact-present fields, options retained.
     *   Does not cancel worker or monitor dataset/filter/selection changes independently;
     *   host/source pointers must survive. No engine write.
     */
    void onSourceLayerChanged();
    /**
     * @brief Mark preview stale and refresh validation/button states, preserving old displayed
     *   preview rows.
     * @note No worker cancel/epoch increment or engine write. Active worker finish later
     *   unconditionally marks captured plan current if no read error.
     */
    void onMappingEdited();
    /**
     * @note Require current source/model and no running job, sync widget options, validate
     *   mappings, then main-thread capture SourceSpec/ModelSnapshot/mapping. Dispatch own-GDAL
     *   reader + pure planner through QtConcurrent, disable Preview/Import and set busy caption.
     *   Configuration controls remain editable; no generation/revision freshness guard. No engine
     *   write, worker cancellation or exceptions-to-error wrapper; raw sources needed only capture
     *   phase.
     */
    void onPreview();
    /**
     * @note Read watcher result, clear running flag/busy caption. Reader error clears plan/marks
     *   stale; otherwise replace preview, summary and mark current UNCONDITIONALLY relative to
     *   controls/source/model changes since capture. Zero-feature summary explicit. No automatic
     *   import/rollback/source refresh or epoch validation. Worker exceptions follow Qt future
     *   behavior, not caught here.
     */
    void onPreviewFinished();
    /**
     * @note Require current source/model/current-plan flag and positive planned work; handler does
     *   not independently revalidate mappings, resultMode, running worker or engine state. Build
     *   modal progress, execute using current mapping/raw context, then show returned plan
     *   past-tense, mark stale and disable repeat. Progress may process GUI events, host must
     *   preserve project/context. Stopping keeps current/prior work; result tail counters can
     *   overstate actual executed objects. No fresh source/model snapshot or automatic rollback;
     *   missing canvas/stack can return unchanged plan still labeled finished.
     */
    void onImport();
    /**
     * @brief Prompt trimmed nonempty preset name and write compact mapping JSON to per-kind
     *   application QSettings, overwriting same key without another prompt.
     * @note Sync current options first, no mapping validation required. Name path separators not
     *   sanitized, QSettings hierarchical-key semantics apply; write errors ignored. Refresh combo
     *   then select name, no source/engine write or implicit import.
     */
    void onSavePreset();
    /**
     * @note None row ignored. Confirm current preset name then remove QSettings key/group under
     *   CURRENT kind, refresh combo. No engine/data deletion, undo or settings error check; host
     *   preserves dialog/current controls through modal reentry, kind is not snapshot-rechecked.
     */
    void onDeletePreset();
    /**
     * @param[in] comboIndex Preset combo row, <=0 ignored, upper bound not independently checked
     *   before itemText.
     * @note Read settings JSON, tolerant ImportMapping parse; failure warning then no apply. Apply
     *   first matching bindings to CURRENT kind, drop absent exact source field names, then
     *   populate option widgets; parsed kind itself does NOT switch target. Widget range clamps
     *   tolerance 0..1e9. Report dropped fields in summary, otherwise old summary can remain, mark
     *   stale; no preview run/source/engine write. Worker is not canceled.
     */
    void onLoadPreset(int comboIndex);

private:
    struct PreviewResult {
        ImportPlan plan;
        QString error;
        int featureCount = 0;
    };

    /**
     * @brief Constructor-only owned configuration/mapping/preview widgets, source-field delegate
     *   and button/option signals, not idempotent.
     * @note Snap default true/tolerance 1 model-unit, auto-create false/prefix J_, conflict
     *   Skip/default updateAttrs true/updateGeometry false. Every option edit invalidates plan
     *   except future completion freshness not guarded. No source enumeration until kind slot or
     *   engine write, optional only dialog layout watcher persists window externally.
     */
    void buildUi();
    /**
     * @note Rebuild under QSignalBlocker from canvas GISVectorLayer objects, using RAW pointers.
     *   Link target permits declared line/multiline, point target declared point/multipoint;
     *   unknown/None/missing OGR geometry accepted. Preserve previous pointer if still compatible,
     *   otherwise Qt first entry. Visibility/selection/readiness not filtered, no layer
     *   add/remove/destruction subscription. No source/engine mutation or source-change signal
     *   emitted during rebuild.
     */
    void repopulateSourceCombo();
    /**
     * @brief Show source count/CRS description and warning when description strings differ from
     *   model description.
     * @note Warning is heuristic and does not prove valid WKTs/coordinate transform; reader can
     *   leave native coordinates on CRS failure. Missing source shows guidance only. No
     *   source/engine mutation.
     */
    void refreshSourceInfo();
    /**
     * @note Under QSignalBlocker replace none+QSettings childKeys for current target kind. Resets
     *   selection to none, no old-name restoration, validation, import or source write. Nested keys
     *   not listed recursively.
     */
    void refreshPresetCombo();
    /**
     * @brief Copy option controls into mutable owned mapping, no mappingEdited signal.
     * @note Raw prefix retained except EXACT empty becomes J_; whitespace not trimmed. Includes
     *   disabled option values, tolerance uses spin 0..1e9 model CRS units. Does not alter
     *   bindings/kind, validate per-feature values or invalidate plan itself.
     */
    void syncOptionsIntoMapping();
    /**
     * @note Sync options, show mapping validation error, enable Preview iff
     *   source/model/nonbusy/error-free; Import additionally current future-mode plan and planned
     *   create+update>0. Does not check engine handle/running state/canvas/undo stack, source
     *   lifetime/version or plan contents per-feature. Mutates dialog options/UI only, no engine
     *   write.
     */
    void updateActionEnablement();
    /**
     * @return Current combo payload cast, invalid/separator QVariant conversion yields underlying
     *   0; no enum validation/source write.
     */
    [[nodiscard]] TargetKind currentKind() const;
    /**
     * @return Borrowed RAW pointer extracted from current combo, null no selection; not
     *   QPointer/destruction-guarded, no ownership transfer or canvas-membership check.
     */
    [[nodiscard]] GISVectorLayer *currentSourceLayer() const;
    /**
     * @return Application settings ImportPresets/kindN for current target enum, independent of
     *   project/source schema/path. No settings write.
     */
    [[nodiscard]] QString presetSettingsGroup() const;

    SWMMVisProjectWindow *m_projectWindow = nullptr;
    MapCanvas *m_canvas = nullptr;
    SWMMModelLayer *m_modelLayer = nullptr;

    ImportMappingModel *m_mappingModel = nullptr;
    ImportPreviewModel *m_previewModel = nullptr;

    // Source group
    QComboBox *m_sourceCombo = nullptr;
    QCheckBox *m_selectedOnly = nullptr;
    QLabel *m_sourceInfo = nullptr;

    // Target group
    QComboBox *m_kindCombo = nullptr;

    // Mapping group
    QTableView *m_mappingView = nullptr;
    QPushButton *m_autoMatchBtn = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QPushButton *m_presetSave = nullptr;
    QPushButton *m_presetDelete = nullptr;
    QLabel *m_validation = nullptr;

    // Endpoint group (link kinds only)
    QGroupBox *m_endpointGroup = nullptr;
    QCheckBox *m_epFields = nullptr;
    QCheckBox *m_epSnap = nullptr;
    QDoubleSpinBox *m_epTolerance = nullptr;
    QCheckBox *m_epAutoCreate = nullptr;
    QLineEdit *m_epPrefix = nullptr;

    // Conflict group
    QRadioButton *m_conflictSkip = nullptr;
    QRadioButton *m_conflictUpdate = nullptr;
    QCheckBox *m_updateAttrs = nullptr;
    QCheckBox *m_updateGeom = nullptr;

    // Preview / results
    QTableView *m_previewView = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QPushButton *m_previewBtn = nullptr;
    QPushButton *m_importBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;

    QFutureWatcher<PreviewResult> m_previewWatcher;
    bool m_previewRunning = false;
    bool m_planIsCurrent = false; ///< false after any edit → must re-preview
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTFEATURELAYERDIALOG_H
