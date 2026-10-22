/**
 * @file      reactionsystemeditordialog.h
 * @brief     Editor for the reaction system (.rxn / [REACTION_*]) — GUI plan
 *        §3.2, rounds G-B2/G-C1.
 *
 * @details Tabs: Options / Species / Coefficients / Terms / Expressions /
 *          Initial Quality / File (raw text) / Sources (the
 *          source/pattern/parameter/report/wall-area tables).
 *
 *          The engine handle IS the model (GUI plan §2): structured tabs
 *          apply as-you-go through the swmm_reaction_* CRUD (each mutation
 *          eagerly validated engine-side and rolled back on failure, so the
 *          model can never go uncompilable), and the File tab serializes
 *          from / applies to the same state — which is the whole sync
 *          mechanism between the views (CLAUDE.md §5.1). FILE persistence
 *          happens only on Save (swmm_reactions_save) to the bound
 *          [PROCESS_COMPONENTS] config, with a one-step "create component +
 *          config file" flow when none is bound (G-D1).
 *
 *          Dependency-light on purpose — Qt Widgets + the engine ABI — so
 *          tests construct and drive it (the WaterAgeSourcesDialog
 *          contract).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   Apache-2.0
 */

#ifndef OPENSWMM_UI_DIALOGS_REACTIONSYSTEMEDITORDIALOG_H
#define OPENSWMM_UI_DIALOGS_REACTIONSYSTEMEDITORDIALOG_H

#include <openswmm/engine/openswmm_engine.h>

#include <QDialog>
#include <QMap>
#include <QStringList>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace OpenSWMMVis
{

/*!
 * \brief Non-modal editor for the model's reaction system.
 */
class ReactionSystemEditorDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Build and hydrate a reaction editor over a borrowed engine.
     * @param[in] engine Nullable engine handle; caller retains ownership and must call
     * invalidateEngine() before release. No automatic owner/engine-close subscription is
     * installed here.
     * @param[in] parent Optional Qt owner; WA_DeleteOnClose is false, so closing does not delete
     * the dialog.
     * @details GUI-thread synchronous engine access. Structured controls write live, while
     * Sources/File drafts publish when leaving their tab or through Save/Close-button gates.
     * Already-applied changes are not rolled back on dismissal. Caller can inspect writeCount()
     * to decide whether to mark the project dirty.
     */
    explicit ReactionSystemEditorDialog(SWMM_Engine engine, QWidget *parent = nullptr);
    /**
     * @brief Release Qt-owned controls/delegates without releasing the borrowed engine.
     * @details No implicit file save or rollback of previously applied engine edits.
     */
    ~ReactionSystemEditorDialog() override = default;

    /**
     * @brief Count successful tracked engine mutations and saves since construction.
     * @return Nonnegative counter, including successful component registration/file save and
     * separate steps of override identity replacement; not a count of distinct user edits and
     * not reset by reloadAll().
     *
     * @details Successful engine writes performed so far (test-facing; the
     *         caller marks the project dirty when > 0 on close).
     */
    [[nodiscard]] int writeCount() const { return m_writes; }
    /**
     * @brief Report whether any tracked mutation/save has succeeded.
     * @return True when writeCount() is positive, even if later work failed or the dialog was
     * dismissed; false otherwise.
     */
    [[nodiscard]] bool wroteAnyChanges() const { return m_writes > 0; }

    /**
     * @brief Rehydrate every structured/text/configuration tab from current engine state.
     * @details Replaces pending File/Sources drafts and their baselines; does not prompt or save
     * them. Individual loaders handle absent engines/read failures. Starts loading suppression
     * and ends it false; some loaders also toggle the shared flag internally. Does not reset the
     * successful-write counter.
     *
     * @details Reload every tab from the engine (after external CRUD).
     */
    void reloadAll();

public slots:
    /**
     * @brief Detach the raw engine, invalidate expression widgets and stop owned timers.
     * @details Disables/rejects the dialog before caller releases the engine; no engine
     * setter/save is attempted and the write counter is preserved. Required lifetime handoff for
     * this raw borrowed handle.
     */
    void invalidateEngine();

private slots:
    /**
     * @brief Add the entered BULK/WALL species with trimmed name/units and default tolerances.
     * @details Empty names/refused API writes produce status messages without clearing input;
     * success increments the write counter, clears the new-name field and reloads all tabs,
     * discarding any other uncommitted drafts. No undo or file save is added.
     */
    void onAddSpecies();
    /**
     * @brief Remove the current displayed species by row through the engine API.
     * @details Missing engine/selection is ignored; refusal (such as a referenced species)
     * leaves views and reports status. Success increments writes and reloads all tabs. The
     * displayed row is used as the native index, so successful complete loading is assumed.
     */
    void onRemoveSpecies();
    /**
     * @brief Add the trimmed named CONSTANT/PARAMETER with the numeric widget value.
     * @details Empty/refused inputs retain draft and report status; success increments writes,
     * clears name and reloads all tabs. Numeric value uses authored reaction coefficient
     * conventions without project-unit conversion.
     */
    void onAddCoefficient();
    /**
     * @brief Remove the selected coefficient by displayed row/native index.
     * @details Missing selection/engine is ignored. Refusal retains data with a status error;
     * success increments writes and reloads all tabs, with no local undo/rollback baseline.
     */
    void onRemoveCoefficient();
    /**
     * @brief Add a trimmed named reaction term with initial expression zero.
     * @details Empty/refused names retain draft and report status; success counts the mutation,
     * clears name and reloads every tab. Existing reactions may constrain engine acceptance.
     */
    void onAddTerm();
    /**
     * @brief Remove the selected displayed term through native indexed CRUD.
     * @details Missing engine/row is ignored; referenced-term refusal reports status, while
     * success counts the write and reloads all tabs. No file persistence occurs here.
     */
    void onRemoveTerm();
    /**
     * @brief Append an owned local node/link/species/value override row and connect live edits.
     * @details Requires an engine; choices snapshot current nodes/links/species. Construction
     * itself does not publish the default row; later selection/value changes invoke
     * commitInitialOverride(). Uses species-native values and supports precise hydrated
     * numerical storage; can also be called under loading suppression while rebuilding saved
     * overrides.
     */
    void onAddInitOverride();
    /**
     * @brief Remove the current override by its saved identity, then discard its draft row.
     * @details Searches the engine using the spin's committed scope/element/species key, not
     * table position. Failed reads/removal retain the row with status; successful removal
     * increments writes. An uncommitted or absent saved key only removes the local row. No
     * confirmation or rollback is added.
     */
    void onRemoveInitOverride();
    /**
     * @brief Apply the active raw/configuration draft and save serialized reactions to the bound
     * file.
     * @details Invalid draft gates saving. Without a reactions component, prompts to register
     * model.rxn and counts successful registration before attempting save. Save success also
     * increments writes and updates title; save failure can leave registration and earlier
     * engine edits applied, and a failed native truncating file write can leave partial disk
     * content. This helper adds no atomic file replacement or rollback. Caller must keep the
     * borrowed engine valid across the modal question.
     */
    void onSave();
    /**
     * @brief Gate leaving File/Sources on successful draft application and refresh destination
     * views.
     * @param[in] index New zero-based tab index supplied by QTabWidget.
     * @details Hydration only records the index; bounce-back guards avoid recursion. Failed
     * application returns to the originating tab without destroying invalid draft/diagnostics.
     * Successful transitions reload shared engine views; entering File regenerates serialized
     * text and entering Sources loads configuration tables.
     */
    void onTabChanged(int index);

private:
    /**
     * @brief Create owned reaction tabs, status and Save/Close controls once.
     * @details Close-button callback applies active File/Sources drafts before accept; invalid
     * drafts keep that button path open. Other window rejection paths are not separately
     * overridden here. Structured controls publish independently and no overall rollback
     * transaction exists.
     */
    void buildUi();
    /**
     * @brief Build live solver/coupling/unit/timestep/tolerance option controls.
     * @return Dialog-owned page adopted by tabs.
     * @details Combo setters report failure and reload displayed options; numeric setters only
     * count success. Pattern/reaction step units are seconds, rate/area tokens define
     * reaction-law units, tolerances retain native meaning. Loading callbacks honor the shared
     * suppression flag; no implicit file save.
     */
    QWidget *buildOptionsTab();
    /**
     * @brief Build read-only species listing plus live Add/Remove inputs.
     * @return Dialog-owned page adopted by tabs; table owns its item widgets.
     * @details New species choose BULK/WALL and a default MG token; existing
     * kind/name/unit/tolerance cells are not directly editable.
     */
    QWidget *buildSpeciesTab();
    /**
     * @brief Build coefficient listing and new CONSTANT/PARAMETER controls.
     * @return Dialog-owned page adopted by tabs.
     * @details Names/kinds are read-only after loading; per-row numeric editors installed by
     * loadCoefficients() write live. New values allow -1e9..1e9 with eight decimals.
     */
    QWidget *buildCoefficientsTab();
    /**
     * @brief Build named-term CRUD and engine-validating expression editing.
     * @return Dialog-owned page adopted by tabs, owning its expression delegate.
     * @details Only expression column edits publish; successful native validation increments
     * writes, refusal reloads previous terms and reports status. Delegate borrows the engine
     * until invalidated.
     */
    QWidget *buildTermsTab();
    /**
     * @brief Build per-species PIPE/TANK reaction-form/expression editing.
     * @return Dialog-owned page adopted by tabs with an owned validating delegate.
     * @details Empty expression maps to NONE and nonempty text with NONE to RATE. Setters count
     * success; refusal reloads current engine expressions. Form selection may retain an
     * empty-expression draft pending text entry.
     */
    QWidget *buildExpressionsTab();
    /**
     * @brief Build global initial values and node/link override controls.
     * @return Dialog-owned page adopted by tabs.
     * @details Global values and override edits use species-native units; row addition/removal
     * delegates identity-based publication rather than relying on displayed order. No
     * hydraulic-unit conversion or file save.
     */
    QWidget *buildInitialQualityTab();
    /**
     * @brief Build the raw .rxn editor, themed highlighter and debounced validation.
     * @return Dialog-owned page adopted by tabs; document owns its highlighter and editor owns
     * its timer.
     * @details Dry-run checks after 300 ms when text differs from baseline and is nonempty; they
     * do not retain native changes. Discard reserializes current engine text. Actual application
     * is gated separately on tab switch/Save/Close-button.
     */
    QWidget *buildFileTab();
    /**
     * @brief Build editable Sources/Patterns/Parameters/Report/Walls configuration tables.
     * @return Dialog-owned page adopted by tabs; the historical name does not denote an empty
     * placeholder.
     * @details Row/cell edits set a local dirty flag and publish together on leaving/saving.
     * MASS is species mass/second, other source values follow species concentration conventions,
     * wall area follows AREA_UNITS and patterns are cyclic factors. Edit Configuration opens the
     * raw File page after gating the draft.
     */
    QWidget *buildSourcesPlaceholderTab();

    /**
     * @brief Hydrate recognized option widgets from successful 64-byte native string reads.
     * @details Leaves prior values for failed/unrecognized reads; numeric text uses toDouble()
     * and widget clamping. Does not independently set loading suppression: callers must guard
     * callbacks when hydration should not write options.
     */
    void loadOptions();
    /**
     * @brief Rebuild the read-only species table from native rows.
     * @details No-op without engine; failed rows are skipped using 128-byte names/32-byte units.
     * Values/tolerances are displayed without conversion, and incomplete reads can make visible
     * row ordinals differ from native indexes.
     */
    void loadSpecies();
    /**
     * @brief Rebuild coefficient rows with live numeric editors bound to native indexes.
     * @details Skips failed 128-byte name reads. Hydrates values before connecting signals,
     * clamps to -1e9..1e9/eight decimals and later counts successful native value setters; no
     * restore/status on numeric refusal here.
     */
    void loadCoefficients();
    /**
     * @brief Rebuild term names/expressions with loading suppression.
     * @details Reads 128-byte names/512-byte expressions, skipping failed rows; names become
     * read-only. Ends the shared loading flag false rather than restoring a previous true state.
     * Row-based editing assumes reads were complete.
     */
    void loadTerms();
    /**
     * @brief Rebuild PIPE/TANK expression rows and form combos for the species-name snapshot.
     * @details Begins suppression and ends it false; zero-initialized 512-byte expressions/NONE
     * form remain when getters fail. Forms use native enums and delegates carry per-cell scope.
     * Callback setters publish live and can reload refused edits.
     */
    void loadExpressions();
    /**
     * @brief Rebuild global values and saved override rows from engine state.
     * @details Uses species-native numerical values, stores exact hydrated override values and
     * committed identity on each spin, and skips failed override reads. Global getter failure
     * leaves zero. Reuses onAddInitOverride() under loading suppression; ends that flag false.
     * Does not clear writeCount().
     */
    void loadInitialQuality();
    /**
     * @brief Validate a local override identity and publish its precise value.
     * @param[in,out] spin Borrowed current value widget; not retained. Ignored while
     * loading/invalidated or when not present in column three of the owned table.
     * @details Requires valid scope/element/species, rejects duplicate draft identities and
     * overwriting a saved target owned by another draft. Upserts the desired value first;
     * identity changes then remove the previous saved key. Each successful native step
     * increments writes. A removal failure leaves both saved entries and retains the old
     * committed key, reporting partial success without rollback. Refusal keeps the draft and
     * focuses the offending field; success records the new key/accessibility text.
     */
    void commitInitialOverride(QDoubleSpinBox *spin);
    /**
     * @brief Refresh row/column-specific names and immediate-edit guidance on override widgets.
     * @details Uses one-based displayed row numbers and existing header labels. Presentation
     * only; does not validate or publish the current draft.
     */
    void refreshInitialOverrideAccessibility();
    /**
     * @brief Replace raw text/baseline with the current serialized engine reaction system.
     * @details No-op without engine; preserves prior loading flag and clears text diagnostics.
     * Serialization failure produces an empty baseline/editor, with no independent failure
     * message; pending text edits are discarded.
     */
    void loadFileTab();
    /**
     * @brief Parse serialized managed sections into the five local configuration tables.
     * @details Clears prior rows, strips semicolon comments, splits whitespace and joins pattern
     * factors into the second cell. Unrecognized sections are ignored; serialization failure
     * gives empty tables. Restores the prior loading flag and clears draft dirtiness without
     * publishing setters.
     */
    void loadConfigurationTables();
    /**
     * @brief Replace managed reaction sections from the staged table cells in one text apply.
     * @return True when absent-engine/clean no-op or native whole-text application succeeds;
     * false with status text on refusal, keeping dirty draft rows.
     * @details Serializes current engine state to retain other sections, writes managed tables
     * in key order, omits wholly blank rows and substitutes * for empty non-final fields. Native
     * validation restores reaction/registry state on refusal; success increments writes and
     * clears dirtiness. Does not compare an optimistic baseline against concurrent external
     * edits to these managed sections.
     */
    bool applyConfigurationTables();
    QMap<QString, QTableWidget *> m_configurationTables;
    bool m_configurationDirty = false;
    int m_sourcesTabIndex = -1;
    /**
     * @brief Apply changed raw text to the current reaction system through whole-text
     * validation.
     * @return True without engine/editor, for exact unchanged baseline, or successful
     * application; false on refused text with diagnostics retained.
     * @details Success increments writes and replaces the baseline with normalized
     * serialization, leaving displayed raw text as authored. Refused native parse/validation
     * restores prior reaction/species state. Does not save to disk or reject external edits
     * using an expected-state comparison.
     */
    bool applyFileTab(); ///< returns false when the text is bad

    /**
     * @brief Refresh the title from the registered reactions component's resolved/config
     * filename.
     * @details Reads fixed-size ID/path buffers and falls back to no-file-bound title on
     * absent/failed binding. No-op without engine. Does not alter Save-button enablement or
     * register/save a component.
     */
    void refreshBinding(); ///< Refresh the component filename in the title.
    /**
     * @brief Replace the status text and optionally use the palette link color.
     * @param[in] msg Borrowed text copied into the owned label; empty clears the message.
     * @param[in] error True applies the error stylesheet, false clears it.
     * @details Presentation only; no engine query, mutation or modal prompt.
     */
    void setStatus(const QString &msg, bool error);
    /**
     * @brief Increment the successful-write/save counter once.
     * @details Caller invokes only after its tracked native operation succeeds; does not emit
     * project-dirty signals or persist files itself.
     */
    void bumpWrites() { ++m_writes; }

    /**
     * @brief Copy species names from successful native species reads in native order.
     * @return Empty without engine, otherwise owned list with failed rows omitted; 128-byte
     * name/32-byte unit buffers are used, so returned ordinals assume successful complete reads
     * when used as native indexes.
     */
    QStringList speciesNames() const;

    SWMM_Engine m_engine = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_saveBtn = nullptr;

    // Options
    QComboBox *m_solverCombo = nullptr;
    QComboBox *m_couplingCombo = nullptr;
    QComboBox *m_rateUnitsCombo = nullptr;
    QComboBox *m_areaUnitsCombo = nullptr;
    QComboBox *m_wallGeometryCombo = nullptr;
    QDoubleSpinBox *m_patternStepSpin = nullptr;
    QDoubleSpinBox *m_timestepSpin = nullptr;
    QDoubleSpinBox *m_atolSpin = nullptr;
    QDoubleSpinBox *m_rtolSpin = nullptr;

    // Species
    QTableWidget *m_speciesTable = nullptr;
    QLineEdit *m_newSpeciesName = nullptr;
    QComboBox *m_newSpeciesKind = nullptr;
    QLineEdit *m_newSpeciesUnits = nullptr;

    // Coefficients
    QTableWidget *m_coeffTable = nullptr;
    QLineEdit *m_newCoeffName = nullptr;
    QComboBox *m_newCoeffKind = nullptr;
    QDoubleSpinBox *m_newCoeffValue = nullptr;

    // Terms
    QTableWidget *m_termTable = nullptr;
    QLineEdit *m_newTermName = nullptr;

    // Expressions
    QTableWidget *m_exprTable = nullptr;

    // Initial quality
    QTableWidget *m_initGlobalTable = nullptr;
    QTableWidget *m_initOverrideTable = nullptr;

    // File tab
    QPlainTextEdit *m_fileEdit = nullptr;
    QLabel *m_fileStatus = nullptr;
    QString m_fileBaseline; ///< last serialize (dirty check)
    int m_fileTabIndex = -1;
    int m_lastTabIndex = 0;

    bool m_loading = false;    ///< suppress apply during load
    bool m_gatingBack = false; ///< bouncing to the File tab
    int m_writes = 0;
};

} // namespace OpenSWMMVis

#endif // OPENSWMM_UI_DIALOGS_REACTIONSYSTEMEDITORDIALOG_H
