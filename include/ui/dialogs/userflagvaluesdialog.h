/*!
 * \file   userflagvaluesdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Per-object user-flag value editor — Phase 4 of
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
    explicit UserFlagValuesDialog(UserFlagsEditRef ref,
                                  QWidget *parent = nullptr);

    /*! True once OK committed at least one set/clear to the engine. */
    [[nodiscard]] bool wroteAnyChanges() const noexcept { return m_wroteAnyChanges; }

    /*! Recomputed cell summary for the button to pull back after exec(). */
    [[nodiscard]] QString updatedSummary() const;

    /*! Commit the staged table through UserFlagsModel. Returns false
     *  (with *outError set) when the engine rejects a value — e.g. a
     *  non-numeric string for an INTEGER flag. Public for tests. */
    bool applyChanges(QString *outError = nullptr);

    /*! Test hook — when false, engine-rejection warnings are returned
     *  silently instead of raising a QMessageBox. Defaults to true. */
    void setConfirmationsEnabled(bool enabled) { m_confirmationsEnabled = enabled; }

    /*! The staged values table. Column 2 holds the value editors. */
    [[nodiscard]] QTableWidget *table() const noexcept { return m_table; }

private slots:
    void onAccepted();

private:
    void reloadFromModel();

    UserFlagsEditRef m_ref;
    QTableWidget    *m_table = nullptr;
    bool             m_wroteAnyChanges      = false;
    bool             m_confirmationsEnabled = true;
};

#endif // USERFLAGVALUESDIALOG_H
