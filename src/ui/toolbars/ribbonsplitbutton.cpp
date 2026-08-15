/*!
 * \file ribbonsplitbutton.cpp
 *
 * UI redesign iteration 2 (R2/R4) — last-used split button
 * implementation. See ribbonsplitbutton.h for the design contract.
 */
#include "ui/toolbars/ribbonsplitbutton.h"

#include <QAction>
#include <QMenu>
#include <QSettings>

#include <utility>

namespace {

const char kSettingsGroup[] = "SWMMVis::Ribbon";

QString lastUsedKey(const QString &familyId)
{
    return QStringLiteral("LastUsed/") + familyId;
}

}   // namespace

namespace openswmmvis::ui {

RibbonSplitButton::RibbonSplitButton(const QString &familyId,
                                     const QList<QAction *> &members,
                                     QWidget *parent)
    : QToolButton(parent)
    , mFamilyId(familyId)
    , mMembers(members)
{
    setAutoRaise(true);
    setPopupMode(QToolButton::MenuButtonPopup);

    auto *menu = new QMenu(this);
    for (QAction *member : std::as_const(mMembers)) {
        menu->addAction(member);
        connect(member, &QAction::triggered, this,
                [this, member]() { promote(member); });
        connect(member, &QAction::toggled, this, [this, member](bool on) {
            if (on)
                promote(member);
        });
    }
    setMenu(menu);

    restoreLastUsed();
}

void RibbonSplitButton::restoreLastUsed()
{
    if (mMembers.isEmpty())
        return;

    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const QString name = settings.value(lastUsedKey(mFamilyId)).toString();
    settings.endGroup();

    QAction *face = mMembers.first();
    if (!name.isEmpty()) {
        for (QAction *member : std::as_const(mMembers)) {
            if (member->objectName() == name) {
                face = member;
                break;
            }
        }
    }
    setDefaultAction(face);
}

void RibbonSplitButton::promote(QAction *member)
{
    if (defaultAction() != member)
        setDefaultAction(member);

    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.setValue(lastUsedKey(mFamilyId), member->objectName());
    settings.endGroup();
}

}   // namespace openswmmvis::ui
