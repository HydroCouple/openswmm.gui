/* SPDX-License-Identifier: Apache-2.0 */

/**
 * @file      initialbedqualitydialog.h
 * @brief     Editing of initial bed and porewater species conditions.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   Apache-2.0
 */

#ifndef OPENSWMM_UI_DIALOGS_INITIALBEDQUALITYDIALOG_H
#define OPENSWMM_UI_DIALOGS_INITIALBEDQUALITYDIALOG_H

#include <openswmm/engine/openswmm_engine.h>
#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QTableWidget;

namespace OpenSWMMVis
{

class InitialBedQualityDialog final : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] engine Borrowed optional engine; caller serializes access and invokes
     *   invalidateEngine BEFORE closing/replacing it. Nonnull handle is not lifetime-guarded.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build and stage authored bed/porewater quality rows, enabling mutation only in
     *   BUILDING/OPENED state.
     * @note Reads configuration synchronously on GUI thread, no undo or writes on construction.
     *   Heat configuration controls informational active/inactive text, not authoring permission;
     *   rows can be saved for later activation.
     */
    explicit InitialBedQualityDialog(SWMM_Engine engine, QWidget *parent = nullptr);
    /**
     * @return Stored marker set to last_write_count>0 after a successfully completed changed draft;
     *   initially false. Unchanged acceptance, invalidation or failed attempts do not reset it.
     * @note Failed apply attempts can retain a positive lastWriteCount despite requested rollback
     *   while leaving this marker unchanged. No dirty flag or engine query here.
     */
    bool wroteAnyChanges() const noexcept { return wrote_any_changes_; }
    /**
     * @return Count of successful remove/upsert calls in most recent apply reaching the diff phase,
     *   not number of numerically changed rows. Initially zero; each full changed draft upserts
     *   EVERY desired row.
     * @note Validation/read failures before comparison leave old count. Write failure keeps count
     *   of successful calls before rollback; it is not a count of currently persisted changes.
     */
    int lastWriteCount() const noexcept { return last_write_count_; }

signals:
    /**
     * @brief Notify a successfully completed changed draft with a positive successful-call count.
     * @note Emitted after snapshot destruction and before accept; unchanged draft,
     *   read/validation/allocation/write failure and invalidation emit nothing. Caller handles
     *   project dirty state/refresh; no undo command supplied.
     */
    void changesApplied();

public slots:
    /**
     * @brief Forget the raw engine and disable table/Add/Remove/OK while changing Cancel to Close
     *   and showing closed-model status.
     * @note Call BEFORE engine destruction; does not close engine, erase staged rows, reset
     *   markers, reject dialog or reconnect to a replacement handle.
     */
    void invalidateEngine();

private slots:
    /**
     * @brief Append a staged global row with zero concentration and first available species,
     *   without engine writes or an editable-state check inside slot.
     * @note Normally reached through disabled/enabled Add button; direct invocation still
     *   builds/query-populates controls even after invalidation.
     */
    void addRow();
    /**
     * @brief Remove current staged row when currentRow>=0, otherwise no-op.
     * @note No confirmation, engine write, marker reset or lifecycle check inside slot; actual
     *   removals occur on accepted draft.
     */
    void removeRow();
    /**
     * @brief Validate the full draft and apply its bed-quality diff with a protective heat snapshot
     *   before accepting.
     * @note Require current BUILDING/OPENED handle; reject empty species, negative/nonfinite
     *   concentration, blank trimmed TAG target, negative parsed CELL or duplicate
     *   scope/target/case-folded-species key. CELL parse uses toInt without checking conversion
     *   success; unknown/invalid targets may be rejected by the engine. No mesh/species resolution
     *   is independently performed here.
     * @note Read all existing rows; read failures silently keep open. Compare keyed exact
     *   concentration equality, ignoring row order. On a change, snapshot current authoring, remove
     *   absent keys in descending index order, then upsert EVERY desired row. Count successful API
     *   calls; unchanged draft accepts with count zero and no signal.
     * @note Snapshot allocation/lifecycle/validation failures warn; write failure attempts snapshot
     *   restore (return ignored), destroys snapshot and reports restore text without accepting. No
     *   undo command or externally guaranteed rollback on restore failure. Successful changed draft
     *   updates marker and emits changesApplied before accept. Serialize engine access and preserve
     *   dialog/engine lifetime through nested warning loops and signal delivery.
     */
    void acceptDraft();

private:
    /**
     * @brief Create owned draft table, help/status labels, row buttons and OK/Cancel wiring.
     * @note Help expresses native species concentration units and water-age hours; specific
     *   cell/tag rows override global on model initialization. Does not load engine data or install
     *   source-lifetime tracking. GUI-thread construction only.
     */
    void buildUi();
    /**
     * @brief Replace staged rows from authored bed-quality API and report whether coupled
     *   local-storage bed heat is configured.
     * @note Null engine leaves existing table unchanged. Nonnull clears first, count failure leaves
     *   empty and status unchanged; individual row failures are skipped. Read target/species
     *   through fixed 256/128-byte UTF-8 buffers. Options checked by exact
     *   YES/COUPLED/LOCAL_STORAGE strings. No engine mutation or marker reset; controls can clamp
     *   imported concentration to editor range/precision.
     */
    void readFromEngine();
    /**
     * @param[in] scope Requested GLOBAL/TAG/CELL enum; unknown value leaves scope combo at its
     *   first option while initial target population uses requested value.
     * @param[in] target Initial TAG string or zero-based decimal CELL index; global ignores target.
     *   Cell labels display index+1, stored data remains zero-based.
     * @param[in] species Raw initial species name, preserved as extra combo option if not currently
     *   advertised; no resolution validation.
     * @param[in] concentration Initial native-species concentration, water age in hours;
     *   QDoubleSpinBox clamps to [0, 1e12] and rounds to 10 decimals.
     * @brief Append owned scope/target/species/value controls and populate choices from current
     *   engine.
     * @note CELL choices include all cells; TAG choices discover triangle tags only,
     *   case-insensitively sorted, and allow typed text. Switching scope preserves current target
     *   within same scope or restores ORIGINAL target when returning to original scope; other
     *   scopes start empty/first.
     * @note Species lists pollutants, nonwall reaction species deduplicated by exact data and water
     *   age only when enabled. Selection/data takes precedence over editable target text in
     *   readRow. No model write or editable-state guard here; table owns controls and callbacks use
     *   borrowed current engine. Each appended row adds a currentCellChanged connection.
     */
    void appendRow(int scope, const QString &target, const QString &species, double concentration);
    /**
     * @param[in] editable Exact control enable flag; this helper does not independently query
     *   engine lifecycle.
     * @brief Enable/disable table, Add/OK, conditionally Remove, and set Cancel versus Close
     *   caption.
     * @note Current-cell handlers elsewhere only test nonnull engine and can later re-enable Remove
     *   independently of this lifecycle flag. Does not invalidate handle, discard draft or mutate
     *   engine.
     */
    void setEditable(bool editable);
    SWMM_Engine engine_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *remove_ = nullptr;
    bool wrote_any_changes_ = false;
    int last_write_count_ = 0;
};

} // namespace OpenSWMMVis
#endif
