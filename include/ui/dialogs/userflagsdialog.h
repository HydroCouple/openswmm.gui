/**
 * @file      userflagsdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     User Flags Manager — Phase 2 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md.
 *
 * Modal editor for the [USER_FLAGS] schema: one row per flag definition
 * (Name, Type, Description) with Add / Remove. Edits are staged in the
 * table and committed to the engine through UserFlagsModel on Apply / OK
 * (Cancel discards), mirroring SimulationOptionsDialog's read-on-open /
 * write-on-apply round trip. The caller marks the project dirty off
 * wroteAnyChanges(), exactly like onSimulationOptions does.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef USERFLAGSDIALOG_H
#define USERFLAGSDIALOG_H

#include <QDialog>

#include "ui/models/userflagsmodel.h"

class QTableWidget;
class QPushButton;
class QDialogButtonBox;

/*!
 * \class UserFlagsDialog
 * \brief Staged editor for user-flag definitions.
 *
 * Constructed over the project's shared UserFlagsModel
 * (SWMMModelLayer::ensureUserFlagsModel()); taking the model rather than
 * the layer keeps the dialog independently testable against a bare
 * engine handle.
 */
class UserFlagsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] model Optional borrowed shared flag store, no deletion guard; caller keeps
     *   store/engine alive throughout dialog and modal confirmations.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Copy current definitions into staged table and connect Add/Remove/Apply/OK controls on
     *   GUI thread.
     * @note Null model produces empty editor whose apply fails. No subscription refreshes external
     *   schema edits; writes use current model definitions on apply. Cancel discards only edits
     *   since last successful Apply, not already committed mutations.
     */
    explicit UserFlagsDialog(openswmmvis::ui::UserFlagsModel *model, QWidget *parent = nullptr);

    /*! True once a completed Apply/OK round recorded an engine change.
     * @return Cumulative successful-completion marker set when a completed apply recorded a
     *   successful remove/define. Initially false, never reset by reload.
     * @note A failing later define can leave earlier engine mutations without updating this marker;
     *   do not treat false as proof no changes occurred. Caller handles project dirty state, not
     *   this accessor.
 */
    [[nodiscard]] bool wroteAnyChanges() const noexcept { return m_wroteAnyChanges; }

    /*! Commit the staged table to the engine. Returns false (with
     *  *outError set) on validation failure — empty or duplicate names.
     *  Public so tests can drive the commit without the button box.
     * @param[out] outError Optional null-safe missing-model, validation,
     *   cancelled-type-confirmation or engine-define diagnostic; unchanged on success.
     * @return False for those failures; true after completing attempted diff, including unchanged
     *   schema or ignored undefine failures.
     * @note Trim/uppercase names and trim descriptions, reject empty/case-insensitive duplicate
     *   names before writes. Confirm type changes once when enabled; omitted definitions are
     *   removed first, then new/changed definitions applied in staged order. Description-only
     *   same-type edits retain values; type changes attempt undefine first, discarding values when
     *   accepted.
     * @note Direct UserFlagsModel operations emit its schema notifications; no undo, transaction or
     *   rollback. Failed undefines are ignored, failed define stops after earlier changes and
     *   leaves table/marker unreloaded. Only full completion ORs successful writes into marker and
     *   reloads canonical engine definitions. Programmatic row removal bypasses the Remove-button
     *   confirmation; apply does not separately confirm omitted definitions.
 */
    bool applyChanges(QString *outError = nullptr);

    /*! Test hook — when false, destructive confirmations (removing a
     *  defined flag, changing the type of a flag that may carry values)
     *  are skipped instead of showing a QMessageBox. Defaults to true.
     * @param[in] enabled False suppresses Remove/type-change confirmation and Apply/OK failure
     *   warning; direct apply error text still returned.
     * @note Changes only local interaction policy, no model mutation or pending row reset. Defaults
     *   true; destructive type changes proceed without confirmation when disabled.
 */
    void setConfirmationsEnabled(bool enabled) { m_confirmationsEnabled = enabled; }

    /*! The staged definitions table (rows may not be applied yet).
     * @return Borrowed owned table for inspection/staging, valid until dialog destruction. Direct
     *   changes do not write engine until apply and can bypass Remove-button confirmation; external
     *   code must preserve column/editor layout.
 */
    [[nodiscard]] QTableWidget *table() const noexcept { return m_table; }

    /*! Append a staged row. Exposed for the Add button and tests.
     * @param[in] name Raw copied name; normalization/empty/duplicate validation deferred to apply.
     * @param[in] type Initial combo enum; unrecognized value selects no item and currentData
     *   converts to integer zero at apply.
     * @param[in] description Raw copied description, trimmed only at apply.
     * @brief Append an owned staged row/type combo without validation, model writes or selection
     *   change.
 */
    void addRow(const QString &name, openswmmvis::ui::UserFlagsModel::FlagType type,
                const QString &description);

private slots:
    /**
     * @brief Append blank String definition, select Name cell and start editing it. No engine
     *   change until apply.
     */
    void onAddClicked();
    /**
     * @brief Remove current staged row, optionally confirming when its trimmed uppercase name
     *   currently exists in model.
     * @note No current row is a no-op; No cancels removal. Engine values are not removed until
     *   apply; warning is based on current staged name, not original row identity. Uses nested
     *   modal loop with borrowed model lifetime required; no separate apply-time removal
     *   confirmation.
     */
    void onRemoveClicked();
    /**
     * @brief Attempt apply while keeping dialog open, showing failure warning only when
     *   confirmations enabled.
     * @note No undo/rollback of partial failure; earlier successful applies remain committed even
     *   if dialog is later cancelled.
     */
    void onApplyClicked();
    /**
     * @brief Apply staged schema and accept only on complete success; failure keeps dialog open and
     *   optionally warns.
     * @note A partial engine mutation can survive failure or later Cancel. Does not directly set
     *   project dirty flag; caller inspects its tracked state.
     */
    void onAccepted();

private:
    /**
     * @brief Discard staged rows and copy current cached model definitions in engine order, or
     *   leave empty for null model.
     * @note No model refresh/invalidation request, write-marker reset, external-change subscription
     *   or engine mutation. Table owns new items/combos; clear destroys previous staged editors.
     */
    void reloadFromModel();
    /**
     * @param[in] row Staged table row expected in range; no explicit bounds check.
     * @return Current combo UserRole converted to enum, or String when no combo. No-current/invalid
     *   combo data converts to zero (Boolean), no validation or model query.
     */
    [[nodiscard]] openswmmvis::ui::UserFlagsModel::FlagType rowType(int row) const;

    openswmmvis::ui::UserFlagsModel *m_model = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
    bool m_wroteAnyChanges = false;
    bool m_confirmationsEnabled = true;
};

#endif // USERFLAGSDIALOG_H
