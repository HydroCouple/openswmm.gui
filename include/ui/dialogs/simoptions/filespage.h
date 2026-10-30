/**
 * @file      filespage.h
 * @brief     Simulation Options → Files / Output / Plugins.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation Options → Files / Output / Plugins.
 *
 * The largest page, and already inner-tabbed before this restructure: report
 * contents, the [FILES] interface-file section, output/report paths, the
 * writer plug-in combos, the [PLUGINS] table, the [PROCESS_COMPONENTS] table
 * and the hot-start save schedule. Ported as-is — T2 is a class split, not a
 * layout change.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef FILESPAGE_H
#define FILESPAGE_H

#include "ui/dialogs/simoptions/simoptionspage.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableView;
class QVBoxLayout;

// These carry the member types verbatim from the monolith, so the page needs
// their full declarations rather than forward declarations.
#include "ui/dialogs/climatologydialog.h"  // openswmmvis::ui::RelativePathPicker
#include "ui/dialogs/hotstartsavesmodel.h" // HotstartSavesModel + its delegate
#include "ui/dialogs/pathbrowsedelegate.h"
#include "ui/dialogs/pluginstablemodel.h"
#include "ui/dialogs/processcomponentsmodel.h"
#include "ui/dialogs/simulationoptionsdialog.h" // ProcessComponentsModel

namespace openswmmvis::ui
{

class FilesPage : public SimOptionsPage
{
    Q_OBJECT

public:
    /**
     * @brief Construct owned file, hot-start, report, writer/plugin and component draft editors.
     * @param ctx Borrowed GUI-thread context that must outlive page accesses; valid engine/
     * layer handles are required during hydration/apply and nested browse callbacks.
     * @param parent Optional QWidget owner; null leaves lifetime to the caller.
     * Snapshots project anchor, writer filters and built-in plugin ids during construction;
     * caller invokes read() to load model state. No option/file commit occurs here.
     */
    explicit FilesPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the localized sidebar caption.
     * @return Owned Files / Output / Plugins string.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Load plugin/component rows, secondary files/hot-starts, writer choices, report flags
     * and saved output paths in order.
     * Individual readers define missing-engine behavior and can leave old draft portions
     * unchanged. Resets model-backed draft rows where specified, with normal UI signal
     * feedback. Does not write engine options, QSettings paths or output files.
     */
    void read() override;
    /**
     * @brief Apply plugin/component, file/hot-start, writer and report drafts, then persist
     * output paths.
     * @return Sum of specialized reader/writer counts: successful setters/removals and a
     * single attempted changed hot-start-list rebuild. Zero does not exclude QSettings
     * changes, and positive does not prove every hot-start call succeeded. No validation,
     * atomic rollback or file save is performed; failures can leave partial engine edits.
     * Writer choices may re-add a plugin row just removed by the table writer.
     */
    int write() override;
    /**
     * @brief Delegate file/report draft validation and advisory accumulation.
     * @param warn Optional borrowed append-only diagnostics output, retaining existing text.
     * @return validateFilesTab() result; does not write or normalize draft values.
     */
    bool validate(QString *warn) override;

    /**
     * @brief Disable unsupported signed-head output and legacy plugin/writer controls.
     * Uses cached capability flags and explanatory tooltips; supported paths do not
     * re-enable prior restrictions. Does not clear drafts, re-probe the engine or prevent
     * write() from considering values in disabled controls.
     */
    void applyCapabilities() override;

private slots:
    /**
     * @brief Open a modal report save-path picker using registered report-writer filters.
     * Seeds from trimmed current text, uses All Files when registry filter is empty and
     * copies a nonempty result into the draft edit. Cancel leaves it unchanged. Does not
     * create the file, choose a writer, persist QSettings or write engine options.
     */
    void browseForReportFile();
    /**
     * @brief Open a modal results save-path picker using registered results-writer filters.
     * Seeds from trimmed current text and copies a nonempty result into the draft edit;
     * empty filters use All Files. Does not create output, choose a writer or persist paths.
     */
    void browseForOutputFile();
    /**
     * @brief Append a blank end-of-run draft save row and immediately browse for its path.
     * Chooses Native when the current engine reports vertices or omitted legacy state,
     * Auto otherwise. Selects its path cell; cancelled browse leaves the blank row for
     * later completion/removal. Missing model/view is a no-op; no engine schedule write.
     */
    void onHotstartSaveAddRow();
    /**
     * @brief Browse for a save path for the current valid hot-start draft row.
     * Missing dependencies/invalid row/cancel are no-ops. Copies the chosen path directly
     * through setData and ignores its result; the row number captured before the modal
     * picker must still identify the intended draft row afterward. Does not write a file.
     */
    void onHotstartSaveBrowseRow();
    /**
     * @brief Remove the current hot-start draft row through its model.
     * Missing dependencies/invalid selection are no-ops; model handles row bounds.
     * Does not delete an existing hot-start file or alter the engine schedule here.
     */
    void onHotstartSaveRemoveRow();
    /**
     * @brief Swap the current hot-start draft row with its predecessor when one exists.
     * Missing dependencies or first/invalid row are no-ops; no engine schedule edit.
     */
    void onHotstartSaveMoveRowUp();
    /**
     * @brief Swap the current hot-start draft row with its successor when one exists.
     * Missing dependencies or last/invalid row are no-ops; no engine schedule edit.
     */
    void onHotstartSaveMoveRowDown();
    /**
     * @brief Lock/unlock output/report writer drafts against the selected input writer.
     * @param on True selects the input plugin id in both role combos and disables them;
     * false enables both without restoring their previous selections. Missing input combo
     * is a no-op. Does not recheck tri-role eligibility, update paths or write plugin rows.
     */
    void onSingleContainerToggled(bool on);

private:
    /**
     * @brief Create owned Files/Output/Plugins tabs, models, delegates and connections once.
     * Path-row helper copies label/caption/filter, writes a newly owned picker through a
     * required output pointer, borrows an optional mode combo and uses defaultSave bool
     * when mode is unset. Retains the construction-time project anchor. Mode callbacks
     * switch picker accept mode without committing a path. Writer-population helper borrows
     * a combo plus filter-kind enum and appends distinct nonempty registered plugin ids.
     * Persistent-editor helpers take row indices; rowsInserted callbacks ignore parent
     * index and visit inclusive first/last, reset callbacks reopen all applicable rows.
     * Selection callbacks borrow current/previous indices, using current validity for
     * buttons; built-in plugin removal gating matches exact cached ids only. Component/
     * plugin Add/Remove callbacks change draft models; input-writer changes refresh the
     * container gate. Hot-start action buttons invoke draft handlers. Does not load model
     * rows, validate library/config files or write the engine during construction.
     */
    void buildUi();
    /**
     * @brief Associate report flags/selectors and interface-file mode widgets with reachability
     * keys.
     * Path/list/model edits use specialized APIs and are not all represented by these tags.
     * Does not read, validate or commit any draft value.
     */
    void tagWidgets();
    /**
     * @brief Build owned report flags and per-object selector groups into a supplied layout.
     * @param parentLayout Borrowed non-null layout adopting the newly allocated group.
     * @param parent Borrowed parent widget for the group; layout ownership manages it.
     * Local selector builder copies its label and writes None/All/Selected radios and
     * list edit through required pointer outputs, returning the owned row group. Selected
     * radio gates its list. Disabled-report callback ignores its bool and gates only six
     * summary checks, leaving signed heads outside that gate. Creates draft controls only.
     */
    void buildReportContentsGroup(QVBoxLayout *parentLayout, QWidget *parent);
    /**
     * @brief Swap two draft schedule rows and select the destination path cell on success.
     * @param from Zero-based source row; invalid indices are refused by swapRows().
     * @param to Zero-based destination row, not a post-removal insertion position.
     * Missing model/view or refused swap is a no-op. Does not perform a Qt row move,
     * reorder engine entries or preserve logical identity through persistent-index remapping.
     */
    void moveHotstartSaveRow(int from, int to);
    /**
     * @brief Recompute container eligibility from the selected input plugin's discovered role
     * set.
     * Eligible requires input-read/output-write/report-write roles; does not verify common
     * extension, library availability or actual write success. When ineligible and checked,
     * unchecks with signals blocked and unlocks output/report combos. Eligible switches
     * do not re-synchronize an already checked container's other ids. No engine writes.
     */
    void updateSingleContainerEnabled();

    /**
     * @brief Hydrate eight report/output boolean checks and three object selectors with signals
     * blocked.
     * Absent engine leaves existing draft unchanged. Local box helper borrows box/key plus
     * bool fallback, mapping only Checked text to true; selector helper borrows three radios,
     * list edit and key, mapping empty/ALL to All, NONE to None and other text to Selected.
     * Missing controls are skipped. Refreshes summary disablement; no option writes.
     */
    void readReportContentsFromEngine();
    /**
     * @brief Submit eight report/output flags and three selector strings through shared change
     * detection.
     * @return Successful common-option setter count, zero without engine; refusals/no-ops
     * are recorded by context but contribute zero. Local checkbox helper borrows optional
     * box plus bool default; selector helper borrows radios/list and yields ALL for missing
     * controls, NONE for None/empty selected text, ALL for All, otherwise trimmed raw text.
     * Key/value forwarding adds each context result without rollback or object-name validation.
     */
    int writeReportContentsToEngine();
    /**
     * @brief Clear plugin draft rows and append readable engine path/argument pairs.
     * Missing model is a no-op; absent engine/failed count leaves cleared rows empty.
     * Uses 1024/2048-byte buffers and skips failed getters. Does not preserve unapplied
     * edits, load plugin libraries or report read failures separately.
     */
    void readPluginsFromEngine();
    /**
     * @brief Apply keyed plugin-table additions/argument replacements and remove absent engine
     * keys.
     * @return Successful set/remove count, zero without model/engine. Reads existing rows
     * into a hash using 1024/2048-byte buffers, skipping failed reads; trims draft keys/args,
     * skips blank keys and passes null for empty args. Duplicate draft ids are not rejected
     * or consolidated before setter attempts. Existing unseen keys are removed independently,
     * including external edits not in this draft. No atomic rollback, load validation or
     * baseline refresh occurs here; later writer-combo handling may re-add selected ids.
     */
    int writePluginsToEngine();
    /**
     * @brief Reload component drafts using the engine and current model directory for status
     * paths.
     * Missing model is a no-op; model load handles null engine and resets its dirty baseline.
     * Does not parse component files or preserve unapplied draft edits.
     */
    void readProcessComponentsFromEngine();
    /**
     * @brief Delegate registration/removal of component drafts to their model's commit().
     * @return Model successful-write count, zero without model/engine. Commit can partially
     * succeed and reload/discard rejected drafts; this wrapper adds no rollback, validation,
     * key-log entry, error propagation or config-file write.
     */
    int writeProcessComponentsToEngine();
    /**
     * @brief Select the first discovered active plugin for each writer role, falling back to
     * built-in.
     * Updates container eligibility and, when all three nonempty ids match, checks it with
     * signals blocked and locks output/report combos. Does not clear every possible stale
     * container check, reload registry entries or write engine registrations.
     */
    void readWriterCombosFromEngine();
    /**
     * @brief Ensure each distinct nonempty selected writer id has an engine plugin row.
     * @return Successful additions only, zero without engine. Local helper borrows an optional
     * combo and collects unique current ids. Existing rows keep their arguments; count/getter
     * failures are not surfaced separately and can make an existing id appear absent.
     * Does not remove/reorder role conflicts, force a selected id to become the first active
     * role, validate tri-role behavior or persist the single-container checkbox itself.
     */
    int writeWriterCombosToEngine();
    /**
     * @brief Hydrate report/results path drafts from per-model QSettings keys.
     * Requires a layer and both edits; otherwise preserves current text. Model path is used
     * verbatim in SWMMVis/Project keys. Missing settings yield empty automatic paths.
     * Does not query engine paths, resolve relative values or touch output files.
     */
    void readOutputPathsFromSettings();
    /**
     * @brief Persist trimmed report/results path drafts under per-model QSettings keys.
     * Requires a layer and both edits; otherwise no-op. Writes even unchanged/empty values,
     * without explicit sync/status check, engine write count, changesApplied signal or
     * project-file save. Does not create parent directories or output files.
     */
    int writeOutputPathsToSettings();
    /**
     * @brief Hydrate interface-file paths/modes and replace the hot-start save draft list.
     * Absent engine leaves old drafts unchanged. Local key reader uses 1024 bytes, returning
     * empty on failure; path helper displays absolute paths relative to the model file
     * when possible. Mode helper borrows combo/token and falls back to index zero on no
     * match. Hot-start count failure empties rows; per-row path/date/format getter status
     * is ignored, preserving initialized defaults, with dates at/below zero displayed as
     * end-of-run. No engine setters, file existence checks or schedule baseline merge.
     */
    void readFilesSectionFromEngine();
    /**
     * @brief Apply changed interface-file references and rebuild a changed hot-start schedule.
     * @return Successful individual file/mode setters plus one per attempted changed list
     * rebuild, even when clear/add/format calls fail. Zero without engine. Entire pass is
     * non-atomic and performs no rollback or input/hot-start file save.
     * Local key/value helpers read/compare exact strings; path helper borrows raw text,
     * trims and resolves relative paths against the input directory, retaining empty,
     * already absolute and leading-angle-bracket slot tokens. Schedule comparison uses
     * resolved paths, exact OA dates and formats; blank path with zero date is omitted.
     * Changed list is cleared/re-added and formatted without checking any return status,
     * so partial failure can also misalign format indices. Does not record these keys
     * through the common context or validate the complete draft before applying.
     */
    int writeFilesSectionToEngine();
    /**
     * @brief Accumulate file/plugin/hot-start draft errors and advisory report/path warnings.
     * @param warn Optional borrowed append-only output; existing text is retained. Null
     * suppresses messages without changing the blocking decision.
     * @return False only for blank trimmed plugin ids or hot-start save paths; true otherwise.
     * Local advisory helpers borrow Selected radio/list and label, or output path edit and
     * label, and append empty-selection/nonexistent-parent warnings. Relative parent checks
     * use QFileInfo's current-directory semantics. Does not validate component ids, library
     * loading, hot-start dates/formats, readable USE files or writable SAVE destinations.
     */
    bool validateFilesTab(QString *warn);

    QPushButton *m_componentsAddBtn = nullptr;
    ProcessComponentIdDelegate *m_componentsIdDel = nullptr;
    ProcessComponentsModel *m_componentsModel = nullptr;
    PathBrowseDelegate *m_componentsPathDel = nullptr;
    QPushButton *m_componentsRemoveBtn = nullptr;
    QTableView *m_componentsView = nullptr;
    QPushButton *m_hotstartSavesAddBtn = nullptr;
    QPushButton *m_hotstartSavesBrowseBtn = nullptr;
    QPushButton *m_hotstartSavesDownBtn = nullptr;
    HotstartSavesDateTimeDelegate *m_hotstartSavesDtDel = nullptr;
    HotstartSavesFormatDelegate *m_hotstartSavesFmtDel = nullptr;
    HotstartSavesModel *m_hotstartSavesModel = nullptr;
    PathBrowseDelegate *m_hotstartSavesPathDel = nullptr;
    QPushButton *m_hotstartSavesRemoveBtn = nullptr;
    QPushButton *m_hotstartSavesUpBtn = nullptr;
    QTableView *m_hotstartSavesView = nullptr;
    openswmmvis::ui::RelativePathPicker *m_hotstartUseEdit = nullptr;
    openswmmvis::ui::RelativePathPicker *m_inflowsPathEdit = nullptr;
    QComboBox *m_inputWriterCombo = nullptr;
    openswmmvis::ui::RelativePathPicker *m_outflowsPathEdit = nullptr;
    QLineEdit *m_outputFilePathEdit = nullptr;
    QComboBox *m_outputWriterCombo = nullptr;
    QPushButton *m_pluginsAddBtn = nullptr;
    PluginsTableModel *m_pluginsModel = nullptr;
    PathBrowseDelegate *m_pluginsPathDel = nullptr;
    QPushButton *m_pluginsRemoveBtn = nullptr;
    QTableView *m_pluginsView = nullptr;
    QComboBox *m_rainfallModeCombo = nullptr;
    openswmmvis::ui::RelativePathPicker *m_rainfallPathEdit = nullptr;
    QComboBox *m_rdiiModeCombo = nullptr;
    openswmmvis::ui::RelativePathPicker *m_rdiiPathEdit = nullptr;
    QLineEdit *m_reportFilePathEdit = nullptr;
    QComboBox *m_reportWriterCombo = nullptr;
    QCheckBox *m_rptAveragesBox = nullptr;
    QCheckBox *m_rptLinkStepsBox = nullptr;
    QCheckBox *m_rptContinuityBox = nullptr;
    QCheckBox *m_rptControlsBox = nullptr;
    QCheckBox *m_rptDisabledBox = nullptr;
    QCheckBox *m_rptFlowstatsBox = nullptr;
    QCheckBox *m_rptInputBox = nullptr;
    QRadioButton *m_rptLinkAllRadio = nullptr;
    QLineEdit *m_rptLinkListEdit = nullptr;
    QRadioButton *m_rptLinkNoneRadio = nullptr;
    QRadioButton *m_rptLinkSomeRadio = nullptr;
    QRadioButton *m_rptNodeAllRadio = nullptr;
    QLineEdit *m_rptNodeListEdit = nullptr;
    QRadioButton *m_rptNodeNoneRadio = nullptr;
    QRadioButton *m_rptNodeSomeRadio = nullptr;
    QRadioButton *m_rptSubcatchAllRadio = nullptr;
    QLineEdit *m_rptSubcatchListEdit = nullptr;
    QRadioButton *m_rptSubcatchNoneRadio = nullptr;
    QRadioButton *m_rptSubcatchSomeRadio = nullptr;
    QComboBox *m_runoffModeCombo = nullptr;
    openswmmvis::ui::RelativePathPicker *m_runoffPathEdit = nullptr;
    QCheckBox *m_signedHeadsCheck = nullptr;
    QCheckBox *m_singleContainerBox = nullptr;
    class QGroupBox *m_writersGroup = nullptr; ///< Tab 7 — writer / container group.
};

} // namespace openswmmvis::ui

#endif // FILESPAGE_H
