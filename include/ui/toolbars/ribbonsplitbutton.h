#ifndef RIBBONSPLITBUTTON_H
#define RIBBONSPLITBUTTON_H

/*!
 * \file ribbonsplitbutton.h
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
 * Last-used persists under SWMMVis::Ribbon/LastUsed/<familyId> as the
 * member action's objectName (unknown or unset → first member).
 */

#include <QList>
#include <QString>
#include <QToolButton>

class QAction;

namespace openswmmvis::ui {

class RibbonSplitButton : public QToolButton
{
    Q_OBJECT

public:
    RibbonSplitButton(const QString &familyId, const QList<QAction *> &members,
                      QWidget *parent = nullptr);

    QString familyId() const { return mFamilyId; }
    QList<QAction *> members() const { return mMembers; }

private:
    void promote(QAction *member);
    void restoreLastUsed();

    QString          mFamilyId;
    QList<QAction *> mMembers;
};

}   // namespace openswmmvis::ui

#endif // RIBBONSPLITBUTTON_H
