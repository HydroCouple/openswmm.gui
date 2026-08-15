/*!
 * \file   userflagsdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  User Flags Manager — Phase 2 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md.
 *
 * Modal editor for the [USER_FLAGS] schema: one row per flag definition
 * (Name, Type, Description) with Add / Remove. Edits are staged in the
 * table and committed to the engine through UserFlagsModel on Apply / OK
 * (Cancel discards), mirroring SimulationOptionsDialog's read-on-open /
 * write-on-apply round trip. The caller marks the project dirty off
 * wroteAnyChanges(), exactly like onSimulationOptions does.
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
    explicit UserFlagsDialog(openswmmvis::ui::UserFlagsModel *model,
                             QWidget *parent = nullptr);

    /*! True once any Apply/OK round actually changed the engine. */
    [[nodiscard]] bool wroteAnyChanges() const noexcept { return m_wroteAnyChanges; }

    /*! Commit the staged table to the engine. Returns false (with
     *  *outError set) on validation failure — empty or duplicate names.
     *  Public so tests can drive the commit without the button box. */
    bool applyChanges(QString *outError = nullptr);

    /*! Test hook — when false, destructive confirmations (removing a
     *  defined flag, changing the type of a flag that may carry values)
     *  are skipped instead of showing a QMessageBox. Defaults to true. */
    void setConfirmationsEnabled(bool enabled) { m_confirmationsEnabled = enabled; }

    /*! The staged definitions table (rows may not be applied yet). */
    [[nodiscard]] QTableWidget *table() const noexcept { return m_table; }

    /*! Append a staged row. Exposed for the Add button and tests. */
    void addRow(const QString &name,
                openswmmvis::ui::UserFlagsModel::FlagType type,
                const QString &description);

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onApplyClicked();
    void onAccepted();

private:
    void reloadFromModel();
    [[nodiscard]] openswmmvis::ui::UserFlagsModel::FlagType rowType(int row) const;

    openswmmvis::ui::UserFlagsModel *m_model = nullptr;
    QTableWidget     *m_table       = nullptr;
    QPushButton      *m_addButton   = nullptr;
    QPushButton      *m_removeButton = nullptr;
    QDialogButtonBox *m_buttonBox   = nullptr;
    bool              m_wroteAnyChanges      = false;
    bool              m_confirmationsEnabled = true;
};

#endif // USERFLAGSDIALOG_H
