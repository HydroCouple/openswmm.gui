/*!
 * \file dialoglayoutwatcher.cpp
 *
 * UI redesign iteration 2 (D1) — automatic dialog layout persistence.
 * See dialoglayoutwatcher.h for the design contract.
 */
#include "ui/dialogs/dialoglayoutwatcher.h"

#include <QDialog>
#include <QEvent>

#include "ui/dialogs/dialoglayoutpersistence.h"

namespace openswmmvis::ui {

namespace {

QDialog *persistableDialog_(QObject *watched)
{
    auto *dlg = qobject_cast<QDialog *>(watched);
    if (!dlg || !dlg->isWindow())
        return nullptr;
    if (dlg->objectName().isEmpty())
        return nullptr;   // unnamed = implicitly opted out
    if (dlg->property(kNoLayoutPersistenceProp).toBool())
        return nullptr;
    return dlg;
}

}   // namespace

DialogLayoutWatcher::DialogLayoutWatcher(QObject *parent)
    : QObject(parent)
{
}

bool DialogLayoutWatcher::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Show:
        if (QDialog *dlg = persistableDialog_(watched)) {
            // Once per instance: re-showing a live modeless dialog must
            // not snap it back to the stored layout.
            if (!dlg->property(kLayoutRestoredOnceProp).toBool()) {
                dlg->setProperty(kLayoutRestoredOnceProp, true);
                // Synchronous, pre-map: no flicker, and it runs after
                // the ctor's resize() so saved geometry wins.
                restoreDialogLayout(dlg);
            }
        }
        break;
    case QEvent::Hide:
    case QEvent::Close:
        // During destruction the QDialog part is already gone by the
        // time the QWidget hide runs, so the cast fails — an accepted
        // miss; every ordinary path (done(), exec() return, close(),
        // hide()) still passes through here as a live QDialog.
        if (QDialog *dlg = persistableDialog_(watched))
            saveDialogLayout(dlg);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

}   // namespace openswmmvis::ui
