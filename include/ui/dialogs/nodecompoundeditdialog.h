/**
 * @file      nodecompoundeditdialog.h
 * @brief     One dialog with a stacked-widget body, one page per compound node attribute
 *            (Inflows / DWF / RDII / Treatment).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DB.2 — One dialog with a stacked-widget body, one page
 * per compound node attribute (Inflows / DWF / RDII / Treatment).
 *
 * **Engine-API status (2026-05-24)** — all four pages have full per-entry
 * round-trip. Inflows uses `swmm_ext_inflow_get/add/remove`, DWF uses
 * `swmm_dwf_get/add/remove`, RDII uses `swmm_rdii_get/add/remove`, and
 * Treatment uses `swmm_treatment_get/set/clear` indexed by
 * (node_idx, pollut_idx). Each page shows existing per-node entries in
 * a table with Add and Remove buttons; per-entry edits are
 * remove-and-readd via the table selection + form below.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef NODECOMPOUNDEDITDIALOG_H
#define NODECOMPOUNDEDITDIALOG_H

#include <QDialog>

#include "ui/properties/nodecompoundeditref.h"

class QStackedWidget;
class QTableWidget;
class QLabel;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QDialogButtonBox;
class QPushButton;
class LabeledPickerCombo;

class NodeCompoundEditDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] ref Moved value with borrowed raw engine/layer, node ID and page kind. Use
     *   Inflows/Dwf/Rdii/Treatment (0..3); navigation-only kinds belong to other dialogs, invalid
     *   enum can index title array out of bounds.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog and modal use via
     *   exec.
     * @brief Build all four page controls, show chosen page and refresh its current engine
     *   entries/summary.
     * @note Changes apply immediately through C API, without undo commands, dialog-level model
     *   markEdited/attribute signals or transactional Cancel rollback. Layer
     *   destroy/engineAboutToClose invalidates; geometryChanged rechecks node by name. Without
     *   layer, caller invalidates BEFORE engine closure; raw handle is not lifetime-guarded.
     *   GUI-thread engine access and dialog lifetime through nested prompts required.
     */
    explicit NodeCompoundEditDialog(NodeCompoundEditRef ref, QWidget *parent = nullptr);

    /*! Refreshed summary string after the dialog closes — the cell's
     *  property adapter reads this back to update the row text without
     *  requerying the engine. Equal to the constructor's
     *  `ref.summary` only until the first active-page refresh.
     * @return Copied cached summary last computed by active-page refresh, including constructor
     *   refresh or node-removed label; does not query engine now or indicate whether a mutation
     *   succeeded. Caller refreshes property row/dirty state separately.
 */
    [[nodiscard]] QString updatedSummary() const { return m_ref.summary; }

public slots:
    /*! Disable editing before the project engine or owning layer is released.
     * @brief Null both borrowed engine/layer fields, disable whole form and reject without closing
     *   engine or undoing previous writes.
     * @note Call before releasing untracked engine; also connected to layer
     *   destruction/about-to-close. Does not clear current table, cached summary or erase committed
     *   records.
 */
    void invalidateContext();

private slots:
    /**
     * @brief Resolve bound node by current name and invalidate when absent/unavailable.
     * @note Geometry changes trigger check; node rename is treated as removal of old name, not
     *   followed. No engine identity/state rebind, source writes or undo.
     */
    void checkContext();

private:
    /**
     * @brief Create owned external-inflow table/form and connect immediate add/update/remove
     *   callbacks.
     * @note Baseline editor [0,1e12]/four decimals; multiplier and scale [-1e6,1e6]/four decimals
     *   default 1. Baseline/series values follow selected FLOW project flow units, constituent
     *   concentration/mass convention or water-age HOURS; scale is explicit user factor, no
     *   automatic unit conversion. Negative age baseline cannot be entered through this nonnegative
     *   spin.
     * @note Add/Update searches backwards for last EXACT node/constituent duplicate, confirms then
     *   removes it (remove status ignored) BEFORE append; failed add can leave removed original
     *   without rollback/refresh. Remove uses stored global engine row index. Errors warn;
     *   successful operation refreshes active page, no own undo/model notification. Selection reads
     *   six-significant-digit table text into bounded spins, so subsequent resubmit may round/clamp
     *   original values.
     */
    void buildInflowsPage();
    /**
     * @brief Create owned DWF table, nonnegative average spin [0,1e12]/four decimals and four
     *   unfiltered pattern pickers, with immediate C API changes.
     * @note Average uses raw project FLOW/native constituent units (age hours when token valid), no
     *   conversion. Add compares constituent current DISPLAY TEXT to saved engine token, removes
     *   last exact duplicate after confirmation (remove failure ignored), then appends; friendly
     *   age label is passed as text rather than engine item data in this path. Failed append does
     *   not restore removed row.
     * @note Table selection also matches constituent by displayed text, loads six-digit average
     *   into bounded spin and raw pattern names. Remove uses saved global row index; no undo,
     *   transaction, dialog-level dirty/attribute signal or automatic external-row refresh.
     */
    void buildDwfPage();
    /**
     * @brief Create owned node RDII table/UH picker and sewer-area spin [0,1e9]/four decimals,
     *   wiring direct add/remove.
     * @note Area in project area units (acres or hectares), no conversion/suffix. Add requires
     *   nonempty raw group text and current node; API appends without dialog duplicate replacement
     *   or group-existence checks. Remove uses stored global index; selection loads six-digit
     *   displayed area into bounded spin. Errors warn, successful writes refresh page, no undo or
     *   model dirty signal here.
     */
    void buildRdiiPage();
    /**
     * @brief Create pollutant-indexed expression table with live validation delegate and direct
     *   set/clear on committed Expression cell.
     * @note Trimmed empty clears, nonempty passes UTF-8 expression to engine. Rejection runs
     *   validation for diagnostic, warns, then ALWAYS refreshes so rejected cells revert; accepted
     *   cells show canonical/readback text. Delegate banner reports validation only, not
     *   committed-state confirmation. Suppress itemChanged during refresh to prevent write echo.
     * @note Pollutant row index is current engine pollutant index, not stable name; host prevents
     *   external reorder while editing. No reaction-species rows, undo command, transaction or
     *   direct layer dirty notification. Empty model displays guidance, engine/target lifetime tied
     *   to invalidation contract.
     */
    void buildTreatmentPage();

    /*! Re-read the engine state for the active page and refresh the
     *  table + summary line. Call after any successful Add / Remove.
     * @brief Requery current node and selected compound page, replacing table/read summary without
     *   engine writes.
     * @note Missing node updates cached summary to node removed but leaves table untouched.
     *   Inflow/DWF/RDII scan global rows, filter current node, skip getter failures and store
     *   global row indices for remove; numeric display six significant digits and text buffers can
     *   truncate. Refresh resets selection/remove enable but not form inputs.
     * @note Treatment enumerates all pollutants, reads expressions through 256-byte buffer (get
     *   status ignored, initialized blank on failure), and counts nonempty expressions under commit
     *   suppression; no-rule summary otherwise. Navigation-only kinds do nothing. Cached summary is
     *   recomputed even when no mutation occurred; does not refresh choice catalogs, notify parent
     *   layer or subscribe to other engine edits.
 */
    void refreshActivePage();

    /*! Look up the bound node's index on the engine. Returns -1 if the
     *  node has been removed since the dialog opened.
     * @return Current zero-based engine node lookup by copied name, or -1 for null
     *   handle/empty/missing name. Handle must still be live; no validation against stored raw
     *   layer engine identity or editable lifecycle.
 */
    [[nodiscard]] int nodeIdx() const;

    // DB.4 — combo population helpers. Each clears + repopulates the
    // picker with the engine's current set of data objects.
    /**
     * @param[in] c Optional borrowed combo; null/no engine leaves existing items unchanged.
     * @brief With signals blocked replace items by FLOW, pollutants, deduplicated exact nonwall
     *   reaction names and reserved age, storing ENGINE token in item data.
     * @note Age gets friendly hours label; temperature is not deliberately added except any
     *   reserved collision supplied as reaction/pollutant name. No current selection restoration or
     *   availability option gate for reserved age. Case-sensitive reaction dedup only against
     *   accumulated names; no source mutation.
     */
    void populateConstituentCombo(QComboBox *c);
    /**
     * @param[in] p Optional borrowed owned picker, null/no engine leaves existing items unchanged.
     * @brief Enumerate engine table type0 entries only and replace picker items while requesting
     *   preservation of current text.
     * @note Failed type reads/missing IDs skipped; no sorting, registry creation, time-series read
     *   or engine write. No live subscription after enumeration.
     */
    void populateTimeSeriesCombo(LabeledPickerCombo *p);
    /**
     * @param[in] p Optional picker, null/no engine no-op.
     * @brief Enumerate all current nonempty engine pattern IDs, independent of
     *   monthly/daily/hourly/weekend type, and replace choices preserving requested current text.
     *   No pattern writes or validation of selected type.
     */
    void populatePatternCombo(LabeledPickerCombo *p);
    /**
     * @param[in] p Optional picker, null/no engine no-op.
     * @brief Read current hydrograph group IDs through 128-byte buffers and replace choices
     *   requesting current text preservation.
     * @note Failed/empty IDs skipped, long IDs can truncate; no source writes, sorting or live
     *   change subscription.
     */
    void populateUhGroupCombo(LabeledPickerCombo *p);

    /*! Open the full complex editor for the given data category. If
     *  \p currentName is empty the editor opens in CreateNew mode; if it
     *  names an existing data object, the editor opens in Edit mode
     *  pre-selecting that object. Returns the name of the object
     *  selected when the editor closes — used to refresh the picker
     *  combo. Categories without a complex editor surface the gap-
     *  tooltip info box.
     * @param[in] dataCategory Raw model DataCategory; only TimeSeries, Patterns and Hydrographs
     *   have implemented picker dispatch here.
     * @param[in] currentName Existing selection passed to modal picker; empty requests that editor
     *   new-object flow.
     * @return Selected editor name according to underlying picker semantics, empty on unavailable
     *   registry/no result/unsupported category. Not a universal accepted/committed flag.
     * @note Requires bound raw layer; missing/unsupported shows information. Lazily ensures
     *   registries, passes null undo stack to series/pattern editors; pattern path explicitly saves
     *   registry to current engine after picker even if empty result, ignoring save failure. Nested
     *   editor writes can persist independently of this dialog closing; no surrounding
     *   undo/rollback or raw-this lifetime guard.
 */
    QString launchObjectEditor(int dataCategory, const QString &currentName);

    /*! Wire a picker's "..." button so it opens the right complex
     *  editor (Pattern / Time Series / UH group). After the editor
     *  closes the combo is repopulated and the worked-on item is
     *  re-selected.
     * @param[in] picker Optional borrowed dialog-owned picker; null no-op, must outlive
     *   connection/callback.
     * @param[in] dataCategory Raw category forwarded to launchObjectEditor.
     * @param[in] repopulate Required nonnull member-function pointer accepting same live picker; no
     *   check before invocation.
     * @brief Connect picker button to launch using trimmed current text; on nonempty result
     *   repopulate and select returned name.
     * @note Empty result leaves choices unchanged even if nested editor changed source. Raw
     *   this/picker captured across modal child, caller must preserve lifetime. No own engine
     *   writes except delegated editor path or selection application.
 */
    void wirePicker(LabeledPickerCombo *picker, int dataCategory,
                    void (NodeCompoundEditDialog::*repopulate)(LabeledPickerCombo *));

    /*! Apply the FLOW/MASS rule to the inflow Type combo based on the
     *  currently-selected constituent. MASS is pollutant-only — disable
     *  it and downshift the selection when FLOW is chosen.
     * @brief Toggle third type item MASS using current ENGINE constituent data: disabled for FLOW
     *   and reserved age/temperature.
     * @note If currently MASS and forbidden, downshift to FLOW for FLOW or CONCEN otherwise.
     *   Requires standard-item combo model, absent controls/model/item no-op. Presentation guard
     *   only; other code can programmatically set disabled item afterward. Does not alter engine
     *   until Add/Update.
 */
    void updateInflowsMassEnabled();

    NodeCompoundEditRef m_ref;

    // RDII page — `swmm_rdii_*`.
    QTableWidget *m_rdiiTable = nullptr;
    QPushButton *m_rdiiRemoveBtn = nullptr;
    LabeledPickerCombo *m_rdiiUhPicker = nullptr; // DB.4f — combo + "..." button
    QDoubleSpinBox *m_rdiiAreaSpin = nullptr;
    QLabel *m_rdiiSummary = nullptr;

    // Inflows page — `swmm_ext_inflow_*`.
    QLabel *m_inflowsSummary = nullptr;
    QTableWidget *m_inflowsTable = nullptr;
    QPushButton *m_inflowsRemoveBtn = nullptr;
    QComboBox *m_inflowsConstCombo = nullptr;        // DB.4d — FLOW + pollutants
    QComboBox *m_inflowsTypeCombo = nullptr;         // MASS disabled when FLOW
    LabeledPickerCombo *m_inflowsTsPicker = nullptr; // time series + "..."
    QDoubleSpinBox *m_inflowsBaseSpin = nullptr;
    QDoubleSpinBox *m_inflowsMFactSpin = nullptr;
    QDoubleSpinBox *m_inflowsSFactSpin = nullptr;
    LabeledPickerCombo *m_inflowsPatPicker = nullptr; // pattern + "..."

    // DWF page — `swmm_dwf_*`.
    QLabel *m_dwfSummary = nullptr;
    QTableWidget *m_dwfTable = nullptr;
    QPushButton *m_dwfRemoveBtn = nullptr;
    QComboBox *m_dwfConstCombo = nullptr; // DB.4e
    QDoubleSpinBox *m_dwfAvgSpin = nullptr;
    LabeledPickerCombo *m_dwfPat1Picker = nullptr; // Monthly + "..."
    LabeledPickerCombo *m_dwfPat2Picker = nullptr; // Daily   + "..."
    LabeledPickerCombo *m_dwfPat3Picker = nullptr; // Hourly  + "..."
    LabeledPickerCombo *m_dwfPat4Picker = nullptr; // Weekend + "..."

    // Treatment — per-(node, pollutant) removal expression, edited
    // inline in a 2-col table (Pollutant | Expression). Writes route
    // through `swmm_treatment_set` / `swmm_treatment_clear` as the
    // user commits the Expression cell.
    QLabel *m_treatmentSummary = nullptr;
    QTableWidget *m_treatmentTable = nullptr;
    QLabel *m_treatmentBanner = nullptr; // live validator verdict
    // Re-entrancy guard: applying engine state to the table fires
    // QTableWidget::itemChanged for every cell, which would otherwise
    // bounce right back into the commit handler. Set to true while
    // populating; the cell-changed slot bails when set.
    bool m_treatmentSuppressCommit = false;

    QStackedWidget *m_stack = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif // NODECOMPOUNDEDITDIALOG_H
