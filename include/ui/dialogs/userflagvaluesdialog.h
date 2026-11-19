/**
 * @file      userflagvaluesdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Per-object user-flag value editor — Phase 4 of
 *         docs/USER_FLAGS_UI_PLAN_2026-06-03.md.
 *
 * One row per defined flag (Flag, Type, Value). Boolean values edit via
 * an (unset)/YES/NO combo; Integer / Real / String values edit as text
 * where a blank means unset. Edits are staged and committed through
 * UserFlagsModel on OK (Cancel discards), so observers (Attribute Table
 * columns) see the same valueChanged() notifications as everywhere else.
 *
 * Opened from the Property Browser's "User Flags" row
 * (UserFlagsEditButton), mirroring NodeCompoundEditDialog's launch
 * pattern.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef USERFLAGVALUESDIALOG_H
#define USERFLAGVALUESDIALOG_H

#include <QDialog>

#include "ui/properties/userflagseditref.h"

class QTableWidget;

class UserFlagValuesDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] ref Moved value containing copied object identity and borrowed raw model;
     *   model/engine must outlive editor and apply. Cached summary is not the source of row values.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Stage current per-object values for all defined flags, with unset/YES/NO Boolean
     *   combos and text editors otherwise.
     * @note Null model gives empty table and apply failure. No live schema/value refresh
     *   subscription or object-rename tracking. Cancel discards staged cells but cannot reverse a
     *   prior direct/partially failing apply. GUI-thread engine access.
     */
    explicit UserFlagValuesDialog(UserFlagsEditRef ref, QWidget *parent = nullptr);

    /*! True after a successful value set or an attempted clear of a found value.
     * @return Cumulative marker set after successful setValue or ANY attempted clear of a currently
     *   found value; initially false and never reset.
     * @note Clear failure is ignored, so true does not guarantee a successful clear. Successful
     *   earlier rows remain marked on later set failure. Getter does not mark project dirty itself.
 */
    [[nodiscard]] bool wroteAnyChanges() const noexcept { return m_wroteAnyChanges; }

    /*! Recomputed cell summary for the button to pull back after exec().
     * @return Fresh userFlagsSummaryFor from current borrowed model/object identity, rather than
     *   staged table or cached ref.summary. Null model yields no-flags-defined text. No
     *   mutation/ownership transfer.
 */
    [[nodiscard]] QString updatedSummary() const;

    /*! Commit the staged table through UserFlagsModel. Returns false
     *  (with *outError set) when the engine rejects a value — e.g. a
     *  non-numeric string for an INTEGER flag. Public for tests.
     * @param[out] outError Optional null-safe missing-model or setValue-rejection message;
     *   unchanged on success and ignored clear failure.
     * @return False for missing model/first rejected set; true after all considered rows even when
     *   a clear failed.
     * @note Read CURRENT definitions and pair rows by position up to min(rowCount,
     *   definitionCount), not displayed flag name; caller must prevent external schema
     *   reorder/change while editing. Trim nonboolean text, empty means unset (cannot preserve
     *   leading/trailing spaces or set empty String). Boolean item data below zero unsets, exactly
     *   one means YES, all other nonnegative means NO.
     * @note Compare raw staged/current strings; changed values call shared model directly and emit
     *   its notifications, with no undo/transaction/rollback. Each attempted found-value clear
     *   marks wroteAnyChanges even on failure; successful sets mark immediately. Later rejection
     *   preserves earlier committed rows and stage, no reload after success.
 */
    bool applyChanges(QString *outError = nullptr);

    /*! Test hook — when false, engine-rejection warnings are returned
     *  silently instead of raising a QMessageBox. Defaults to true.
     * @param[in] enabled False suppresses failure warning in onAccepted only; direct apply
     *   continues returning diagnostics.
     * @note Local interaction setting, no destructive confirmation exists in this editor and no
     *   model write occurs here. Defaults true.
 */
    void setConfirmationsEnabled(bool enabled) { m_confirmationsEnabled = enabled; }

    /*! The staged values table. Column 2 holds the value editors.
     * @return Borrowed dialog-owned staged table, valid until destruction. Column 2 contains value
     *   item/combo; preserve row order relative to schema for apply. Direct cell edits do not write
     *   model.
 */
    [[nodiscard]] QTableWidget *table() const noexcept { return m_table; }

private slots:
    /**
     * @brief Apply all considered staged rows and accept on success; otherwise keep open and
     *   optionally show warning.
     * @note Partial prior-row writes are retained/marked and are not rolled back on later
     *   rejection/Cancel. Caller handles dirty state using marker with documented clear-attempt
     *   limitation.
     */
    void onAccepted();

private:
    /**
     * @brief Replace staged table with current cached definitions and current object value strings;
     *   null model leaves empty.
     * @note Definition/type labels are read-only, descriptions become name tooltips. Only exact
     *   stored YES selects true; any other found Boolean string selects NO. No schema-cache
     *   invalidation, model mutation or write-marker reset; external changes are not followed
     *   automatically.
     */
    void reloadFromModel();

    UserFlagsEditRef m_ref;
    QTableWidget *m_table = nullptr;
    bool m_wroteAnyChanges = false;
    bool m_confirmationsEnabled = true;
};

#endif // USERFLAGVALUESDIALOG_H
