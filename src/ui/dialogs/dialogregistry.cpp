/*!
 * \file   dialogregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * See dialogregistry.h for the design contract.
 */
#include "ui/dialogs/dialogregistry.h"

#include <QByteArray>
#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace openswmmvis::ui {

namespace {

const char *const kStackingEnvVar = "OPENSWMM_DIALOG_STACKING";
const char *const kStackingKey    = "Window/DialogStacking";

}   // namespace

DialogRegistry::DialogRegistry(QObject *parent)
    : QObject(parent)
{
}

DialogRegistry *DialogRegistry::instance()
{
    static DialogRegistry *s_instance = new DialogRegistry();
    return s_instance;
}

DialogRegistry::StackingMode DialogRegistry::configuredStackingMode()
{
    // Environment first so a tester can flip strategies for one run without
    // writing to (and having to clean up) the user's preferences.
    const QByteArray env = qgetenv(kStackingEnvVar).trimmed().toLower();
    if (env == "qt")     return StackingMode::QtRaiseOnActivate;
    if (env == "native") return StackingMode::NativeChildWindow;

    const QString pref = QSettings().value(QString::fromLatin1(kStackingKey))
                             .toString().trimmed().toLower();
    if (pref == QLatin1String("qt"))     return StackingMode::QtRaiseOnActivate;
    if (pref == QLatin1String("native")) return StackingMode::NativeChildWindow;

#ifdef Q_OS_MACOS
    // Default stays on the shipped behaviour; the Qt path is opt-in until it
    // has been through the freeze-retest checklist on real hardware.
    return StackingMode::NativeChildWindow;
#else
    // Elsewhere there is no native attachment at all — Qt::Tool +
    // WindowStaysOnTopHint (see floatingPanelFlags) already handle stacking,
    // and the raise pass is a harmless no-op reinforcement.
    return StackingMode::QtRaiseOnActivate;
#endif
}

void DialogRegistry::setStackingMode(StackingMode mode)
{
    mMode = mode;
}

QDialog *DialogRegistry::trackableDialog(QObject *watched)
{
    auto *dlg = qobject_cast<QDialog *>(watched);
    if (!dlg)
        return nullptr;
    // Embedded QDialog widgets sitting inside someone else's layout are not
    // windows the user can lose.
    if (!dlg->isWindow())
        return nullptr;
    // Modal dialogs are transient and block interaction anyway — listing them
    // in a Window menu the user cannot reach would be noise.
    if (dlg->windowModality() != Qt::NonModal)
        return nullptr;
    // Popups/tooltips borrow QDialog occasionally; they are not user-managed
    // windows and must never be raised or listed.
    const Qt::WindowType type = dlg->windowType();
    if (type == Qt::Popup || type == Qt::ToolTip || type == Qt::SplashScreen)
        return nullptr;
    return dlg;
}

void DialogRegistry::trackOrPromote(QDialog *dlg)
{
    if (!dlg) return;

    bool alreadyTracked = false;
    bool prunedDead     = false;
    for (int i = mOrder.size() - 1; i >= 0; --i) {
        if (mOrder.at(i).isNull()) { mOrder.removeAt(i); prunedDead = true; continue; }
        if (mOrder.at(i) == dlg)   { mOrder.removeAt(i); alreadyTracked = true; }
    }
    mOrder.append(QPointer<QDialog>(dlg));   // most recently used goes last

    // Only announce SET changes. A pure MRU reorder happens on every click
    // into a dialog, and the Window menu rebuilds from this signal — firing
    // it there would rebuild the menu on every focus change for no visible
    // benefit (the menu is a recovery affordance, not a z-order readout).
    if (!alreadyTracked || prunedDead)
        emit openDialogsChanged();
}

void DialogRegistry::forget(QDialog *dlg)
{
    bool removedLive = false;
    for (int i = mOrder.size() - 1; i >= 0; --i) {
        if (mOrder.at(i).isNull()) {
            mOrder.removeAt(i);          // dead entry — housekeeping, not news
            continue;
        }
        if (mOrder.at(i) == dlg) {
            mOrder.removeAt(i);
            removedLive = true;
        }
    }
    // Only a real removal is worth a signal: the Window menu rebuild is not
    // free, and this runs on every dialog hide in the application.
    if (removedLive)
        emit openDialogsChanged();
}

QList<QPointer<QDialog>> DialogRegistry::openDialogs() const
{
    QList<QPointer<QDialog>> out;
    out.reserve(mOrder.size());
    for (const QPointer<QDialog> &p : mOrder) {
        if (!p.isNull())
            out.append(p);
    }
    return out;
}

void DialogRegistry::raiseAllInOrder()
{
    // Least-recently-used first so the most recent finishes on top.
    const QList<QPointer<QDialog>> dialogs = openDialogs();
    for (const QPointer<QDialog> &d : dialogs) {
        if (d && d->isVisible())
            d->raise();
    }
}

void DialogRegistry::scheduleRestack()
{
    if (mMode != StackingMode::QtRaiseOnActivate || mRestackQueued)
        return;
    mRestackQueued = true;
    // Deferred: raising inside an activation event can generate further
    // activation events, and we want exactly one pass per activation. The
    // flag is held for the DURATION of the pass — clearing it first would
    // let a WindowActivate produced by the raising queue another pass, and
    // the two would ping-pong.
    QTimer::singleShot(0, this, [this]() {
        raiseAllInOrder();
        mRestackQueued = false;
    });
}

bool DialogRegistry::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Show:
        // Showing counts as using: a new dialog lands on top, and a re-shown
        // one returns to the front rather than under its older siblings.
        if (QDialog *dlg = trackableDialog(watched))
            trackOrPromote(dlg);
        break;

    case QEvent::WindowActivate:
        if (QDialog *dlg = trackableDialog(watched)) {
            trackOrPromote(dlg);   // MRU: clicking a dialog moves it to the end
        } else if (auto *w = qobject_cast<QWidget *>(watched)) {
            // A non-dialog ordinary window took focus — in practice the main
            // window. Re-assert dialog stacking so a click on the map cannot
            // bury them (the job the native attachment does in the other mode).
            //
            // Qt::Window specifically: menus, popups and tooltips are also
            // top-level widgets and activate constantly, and restacking on
            // every menu open would be both pointless and visibly jumpy.
            if (w->isWindow() && w->windowType() == Qt::Window)
                scheduleRestack();
        }
        break;

    case QEvent::ApplicationStateChange:
        // Qt 6 spelling of "the app came to the foreground" (the old
        // ApplicationActivate event type is gone). Re-assert dialog stacking
        // after an app switch, which is where the native attachment used to
        // do the work.
        if (QGuiApplication::applicationState() == Qt::ApplicationActive)
            scheduleRestack();
        break;

    case QEvent::Hide:
        // A SPONTANEOUS hide is the window server hiding the app or
        // minimising the window (Cmd+H, minimise), not the user dismissing
        // the dialog. Dropping the entry there would empty the Window menu
        // precisely when the user needs it to get a window back.
        if (event->spontaneous())
            break;
        Q_FALLTHROUGH();
    case QEvent::Close:
        // Note: during destruction the QDialog sub-object is already gone, so
        // the cast fails and nothing is removed here — the QPointer entries
        // simply read back null and are pruned on the next access.
        if (QDialog *dlg = trackableDialog(watched))
            forget(dlg);
        break;

    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

}   // namespace openswmmvis::ui
