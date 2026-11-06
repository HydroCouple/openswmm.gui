/**
 * @file      initialqualitydialog.h
 * @brief     Editor for `[INITIAL_QUALITY]` — per-node/per-link initial
 *        concentrations (GUI plan §3.4 pattern / program E-A, round G-A1).
 *
 * @details One add/remove row table over the engine's
 *          `swmm_init_quality_*` surface: Scope (NODE/LINK), Element,
 *          Constituent (pollutants plus the reserved `__WATER_AGE__` /
 *          `__TEMPERATURE__` species, offered only when their [OPTIONS]
 *          toggle is on), Value. Pollutant values are concentrations in the
 *          pollutant's own units; water age is HOURS (signed — engine
 *          D-NS1); temperature is degC.
 *
 *          The engine handle IS the model (GUI plan §2): edits are read
 *          from and written to `openswmm_initial_quality.h` directly, with
 *          no intermediate registry. Deliberately **dependency-light** — Qt
 *          Widgets plus that one engine ABI — so a test can link and drive
 *          it (the `WaterAgeSourcesDialog` / `ClimatologyDialog` precedent).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   Apache-2.0
 */

#ifndef OPENSWMM_UI_DIALOGS_INITIALQUALITYDIALOG_H
#define OPENSWMM_UI_DIALOGS_INITIALQUALITYDIALOG_H

#include <openswmm/engine/openswmm_engine.h>

#include <QDialog>
#include <QSet>

class QTableWidget;
class QLabel;
class QLineEdit;
class QPushButton;

namespace OpenSWMMVis
{

/*!
 * \brief Non-modal editor for the per-element initial-quality table.
 */
class InitialQualityDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] engine Optional borrowed engine, must remain live until invalidateEngine is called
     *   before closure/replacement; serialize engine access on GUI thread.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog, delete-on-close
     *   explicitly disabled.
     * @brief Stage current node/link quality and CSV-source reference, capturing BUILDING/OPENED
     *   edit permission at construction.
     * @note Nonmodal by default, caller may exec. Noneditable/null handle switches controls to
     *   Close/read-only. Does not write source, load network data or subscribe to model
     *   identity/lifecycle changes.
     */
    explicit InitialQualityDialog(SWMM_Engine engine, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned widgets; no engine closure, draft commit or reversal of prior partial
     *   writes.
     */
    ~InitialQualityDialog() override = default;

    /*! \brief Restrict the dialog to one node (\p isLink 0) or link
     *         (\p isLink 1). The Scope / Element columns collapse, existing inline rows
     *         for other elements are neither shown nor removed on OK,
     *         and Add creates rows pinned to the element. No-op when
     *         the element is unknown to the engine. Used by the
     *         Property Browser's per-element "Initial Quality" cell.
     * @param[in] isLink Zero resolves node, any nonzero resolves link; stored normalized to 0 or 1.
     * @param[in] elementName Required nonempty ID resolved immediately to engine index; null
     *   handle/empty/unknown name leaves existing mode untouched.
     * @brief Replace draft with rows for resolved element and hide/pin scope/element controls for
     *   subsequently added rows.
     * @note Captures index without later rename/topology validation; model edits can make index
     *   refer to another element. Repeated valid call discards staged rows. CSV reference remains
     *   global/editable and import can programmatically set hidden controls to other elements;
     *   caller must restrict those pathways if strict per-element isolation is needed. No engine
     *   mutation or whole-model reset mode here.
 */
    void setElementScope(int isLink, const QString &elementName);

    /*! \brief True once any OK attempt has written a change, including a partial failure.
     * @return Cumulative true after any successful mutation in onAccept, including partial failure.
     *   No-op attempts/read-only checks/reload/invalidation do not reset it; no direct dirty-flag
     *   update.
 */
    [[nodiscard]] bool wroteAnyChanges() const { return m_wroteAnyChanges; }

    /*! \brief Number of engine writes the last OK performed (0 when the
     *         dialog was accepted with no edits). Test-facing.
     * @return Successful mutation-call count during last editable onAccept attempt, zeroed before
     *   writes. Includes FILE reference change, removals and sets even on later failure. Read-only
     *   onAccept returns without resetting previous count.
 */
    [[nodiscard]] int lastWriteCount() const { return m_lastWriteCount; }

signals:
    /*! At least one engine mutation succeeded, even if a later write failed.
     * @note Emitted after positive write count updates marker, before error warning or acceptance.
     *   Partial successful writes therefore notify even if form stays open;
     *   no-op/read-only/zero-write failure do not emit. Caller refreshes/marks dirty, no undo
     *   payload.
 */
    void changesApplied();

public slots:
    /**
     * @brief Forget handle, clear cached edit permission, disable whole dialog and reject.
     * @note Caller invokes before destroying engine. Does not close engine, erase draft, reset
     *   markers or reverse already applied changes. Instance remains owned by caller/parent.
     */
    void invalidateEngine();

private slots:
    /**
     * @brief Stage one row with current node/link choices, species choices and value editor; null
     *   engine is a no-op.
     * @note Includes pollutants and all reaction species including WALL, deduplicated by exact
     *   token, plus water age/temperature only when exact option YES. Persisted unoffered species
     *   are restored separately by readFromEngine. Source combo changes repopulate element choices
     *   via a persistent row index, preserving callback row identity after deletion.
     * @note Nonreserved species use minimum zero; reserved water age/temperature allow -1e6, all
     *   use maximum 1e6/four decimals. Native concentration units, hours or degrees C respectively.
     *   Scope mode pins disabled hidden combos initially; no cached edit-permission check inside
     *   slot and no engine writes.
     */
    void onAddRow();
    /**
     * @brief Remove current inline staged row and shift FILE-backed row markers after it.
     * @note No-op for missing current or FILE-backed row; update accessibility/removal enabled
     *   state afterward. No engine/lifecycle check or writes here.
     */
    void onRemoveRow();
    /**
     * @brief If construction-cached edit permission is true, reset count, validate/write current
     *   draft and accept only with empty error.
     * @note Positive partial write count marks cumulative changes and emits changesApplied BEFORE
     *   warning. Error warns, then focuses borrowed failedWidget if available; no rollback, and
     *   Cancel leaves applied writes. writeToEngine rechecks live editable lifecycle; callers
     *   preserve engine/dialog/widget lifetimes through nested warning loop and signal delivery.
     */
    void onAccept();
    //! U2: read a CSV's rows into the table as ordinary inline rows.
    /**
     * @brief Choose a CSV/text file and append parsed entries as ordinary inline staged rows,
     *   without retaining a file reference or writing engine.
     * @note GUI invocation requires live editable engine, enforced by disabled button rather than
     *   an internal guard. Split each trimmed UTF-8 physical line on comma/semicolon/whitespace; no
     *   quote/escape support. Blank/leading-semicolon lines ignored; require at least four tokens
     *   (extras ignored), NODE/LINK scope and numeric fourth token. A nonnumeric first physical
     *   line is treated as header without skipped count; later malformed lines count.
     * @note Resolve element via combo findText, append unknown constituent as raw choice, preserve
     *   numeric precision behind display. Unknown element discards appended row; no deduplication
     *   or independent finite/sign validation. Scoped dialog import can overwrite initially pinned
     *   hidden scope/element values with other elements. Report added/skipped counts in hint;
     *   chooser cancellation leaves draft, file-open failure changes hint.
     */
    void onImportCsv();

private:
    /**
     * @brief Create owned table, CSV reference/browse/import controls, row buttons and OK/Cancel
     *   handlers.
     * @note Browse only copies selected path into draft, without read/rebase or engine write.
     *   FILE-backed rows are later disabled during hydration. Does not itself hydrate model values
     *   or enforce read-only lifecycle.
     */
    void buildUi();
    /**
     * @brief Replace table and FILE markers from current authored rows, filtering existing rows by
     *   captured element scope.
     * @note Null engine leaves all unchanged. Read FILE reference through 1024-byte UTF-8 buffer
     *   (failure leaves field), species through 128-byte buffer; failed rows skipped.
     *   Unknown/unoffered constituent preserved, but unavailable element falls back to current
     *   first choice. Hydrate exact values behind rounded/clamped displays.
     * @note FILE-backed rows remain visible/disabled and are skipped by write diff. No engine
     *   writes, topology guard, marker reset or source-file edit; scope changes call this to
     *   discard draft.
     */
    void readFromEngine();
    /**
     * @param[in,out] failedWidget Borrowed offending field output; set on specific
     *   validation/read/write errors, otherwise left unchanged. Caller initializes null and
     *   preserves widget lifetime.
     * @return Empty after complete success/no-op; translated error on first
     *   lifecycle/read/validation/write failure.
     * @note Require live BUILDING/OPENED state. Read existing editable non-FILE rows in requested
     *   scope, canonicalize pollutant tokens via engine case-insensitive lookup while keeping
     *   reaction/reserved names exact. Validate complete inline selections and duplicate
     *   scope/index/constituent keys BEFORE any mutations; does not verify current element name
     *   still matches captured combo index or independently validate numeric range/sign/finiteness.
     * @note Trim and save model-wide FILE reference first, then remove absent editable rows in
     *   descending engine index order, then upsert missing/different rows using
     *   qFuzzyCompare(1+old,1+desired). FILE-backed rows never explicitly removed/rewritten. Uses
     *   preciseValue so unchanged rounded display preserves exact source numeric.
     * @note Increment m_lastWriteCount per successful mutation; no reset, signal, accept, undo or
     *   rollback inside helper. First API failure returns after earlier changes; FILE reference
     *   change can affect whole model even in element scope, and imported out-of-scope inline rows
     *   are not filtered during table collection.
     */
    QString writeToEngine(QWidget *&failedWidget);
    /**
     * @brief Show no-model/state hint, disable current table editors and
     *   file/add/remove/browse/import controls, and replace OK/Cancel with Close.
     * @note Does not query lifecycle, set m_editable, disable entire dialog or block later direct
     *   slots; new rows created programmatically are not automatically disabled. No writes/marker
     *   reset.
     */
    void setReadOnly();
    /**
     * @param[in] row Zero-based existing staged row; absent scope/element combo or null engine is a
     *   no-op.
     * @brief Replace element choices from current engine according to scope data exactly equal to 1
     *   for links, otherwise nodes.
     * @note Stores zero-based engine indices and displayed IDs, fallback numeric label for missing
     *   ID; previous selection discarded. No name/index guard for future topology changes,
     *   lifecycle check or writes.
     */
    void populateElementCombo(int row);
    /**
     * @param[in] firstRow First row to relabel, normally nonnegative; default zero updates whole
     *   table.
     * @brief Refresh one-based row/column accessibility names and FILE ownership descriptions, then
     *   enable Remove only for editable inline current row.
     * @note Requires intact standard headers; no source read, writes or numeric changes.
     */
    void updateRowPresentation(int firstRow = 0);

    SWMM_Engine m_engine = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_hintLabel = nullptr;
    /// U2: `[INITIAL_QUALITY] FILE <csv>` reference and the table rows that
    /// came from it (shown greyed; neither re-written nor removed on OK).
    QLineEdit *m_fileEdit = nullptr;
    QPushButton *m_removeButton = nullptr;
    QSet<int> m_fileRows;
    bool m_editable = false;
    bool m_wroteAnyChanges = false;
    int m_lastWriteCount = 0;
    int m_scopeIsLink = -1;  ///< -1 = whole-model mode
    int m_scopeElemIdx = -1; ///< engine index of the scoped element
};

} // namespace OpenSWMMVis

#endif // OPENSWMM_UI_DIALOGS_INITIALQUALITYDIALOG_H
