#ifndef RIBBONSPLITBUTTON_H
#define RIBBONSPLITBUTTON_H

/**
 * @file      ribbonsplitbutton.h
 * @brief     UI redesign iteration 2 (R2/R4) — a last-used split button for a family of related
 *            actions (Select ▾, Add Node ▾, Import ▾, …).
 *
 * UI redesign iteration 2 (R2/R4) — a last-used split button for a
 * family of related actions (Select ▾, Add Node ▾, Import ▾, …). The
 * face is a real setDefaultAction mirror of the last-used member, so
 * icon, text, enabled state and — crucially — the checked state of
 * checkable map tools all track automatically; the arrow opens the
 * family menu (QToolButton::MenuButtonPopup, the proven Plot-Profile
 * pattern).
 *
 * The face follows both user triggers AND programmatic toggles
 * (toggled(true)), so objectName-keyed tool sync (Esc → Select) still
 * promotes the active tool onto the face with its checked state shown.
 * Last-used persists under SWMMVis::Ribbon/LastUsed/familyId as the
 * member action's objectName (unknown or unset → first member).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QList>
#include <QString>
#include <QToolButton>

class QAction;

namespace openswmmvis::ui
{

class RibbonSplitButton : public QToolButton
{
    Q_OBJECT

public:
    /**
     * @param[in] familyId Copied settings key suffix; no escaping/uniqueness validation.
     * @param[in] members Borrowed nonnull actions, copied in menu order; must outlive button use.
     * @param[in] parent Optional QWidget owner.
     * @brief Create owned menu and MenuButtonPopup face, connect triggers/toggled(true) to promote,
     *   restore persisted face.
     * @note GUI-thread only. Does not own QActions, trigger/check them during restore, or prefer
     *   currently checked member over saved name. Empty family has no default action. Distinct
     *   buttons sharing familyId share persisted choice.
     */
    RibbonSplitButton(const QString &familyId, const QList<QAction *> &members,
                      QWidget *parent = nullptr);

    /**
     * @return Copied constructor identity, no persistence lookup or mutation.
     */
    QString familyId() const { return mFamilyId; }
    /**
     * @return Copied list of borrowed action pointers; no lifetime guard or ownership transfer.
     */
    QList<QAction *> members() const { return mMembers; }

private:
    /**
     * @param[in] member Required live member action, identity expected from constructor hooks;
     *   membership not checked.
     * @note Change default face only if different, always persist member objectName under
     *   SWMMVis::Ribbon/LastUsed/familyId even if unchanged/empty. Does not trigger/toggle action;
     *   QToolButton mirrors its state.
     */
    void promote(QAction *member);
    /**
     * @note Empty members no-op. Read stored objectName, choose first matching member or first
     *   member fallback, then setDefaultAction. Does not write fallback choice, trigger/check it or
     *   validate enabled state; raw members must remain live.
     */
    void restoreLastUsed();

    QString mFamilyId;
    QList<QAction *> mMembers;
};

} // namespace openswmmvis::ui

#endif // RIBBONSPLITBUTTON_H
