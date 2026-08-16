/*!
 * \file   dialogregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  App-wide register of open modeless dialogs, in most-recently-used
 *         order, plus the dialog stacking strategy.
 *
 * Two jobs, both of which need the same list:
 *
 *  1. RECOVERY (Phase A5). Modeless dialogs are ordinary top-level windows
 *     that the user can drag anywhere, including onto a monitor that is later
 *     disconnected. On macOS a natively-attached child window does not even
 *     appear in Mission Control, so a lost dialog was unreachable. The Window
 *     menu lists everything in here, and "Reset Window Positions" walks it.
 *
 *  2. STACKING (Phase B). Keeping dialogs above the main window is done one
 *     of two ways, selected by StackingMode:
 *
 *     - NativeChildWindow (default, macOS): AppKit -addChildWindow:. Reliable
 *       ordering, but child windows move rigidly with their host and are
 *       invisible to the window server's recovery affordances.
 *     - QtRaiseOnActivate: pure Qt. When the application (or the main window)
 *       is activated, every registered dialog is raise()d in MRU order. No
 *       native gluing at all, so dialogs are fully independent windows.
 *
 *     The mode is read once at startup from the OPENSWMM_DIALOG_STACKING
 *     environment variable ("qt" / "native"), else the "Window/DialogStacking"
 *     QSettings key, else the platform default. The environment override
 *     exists so a tester can flip strategies without touching preferences.
 *
 * ORDERING is most-recently-USED, not open order: a dialog moves to the end
 * of the list when it is activated. Re-raising on MRU order preserves the
 * z-order the user arranged by clicking, whereas strict open order would
 * shuffle their arrangement back every time the app was re-activated. A
 * freshly shown dialog is activated as part of being shown, so "newest on
 * top" still falls out naturally.
 *
 * Entries are QPointer, so a dialog destroyed without a Hide/Close event
 * (WA_DeleteOnClose teardown) simply drops out on the next read.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_DIALOGREGISTRY_H
#define OPENSWMMVIS_UI_DIALOGS_DIALOGREGISTRY_H

#include <QList>
#include <QObject>
#include <QPointer>
// Qt 6's QPointer<T> needs T complete wherever it is assigned or compared,
// and a forward declaration is not enough — same reasoning as the note in
// ui/panels/legenddock.h.
#include <QDialog>

class QEvent;

namespace openswmmvis::ui {

class DialogRegistry : public QObject
{
    Q_OBJECT

public:
    /*! \brief How modeless dialogs are kept above the application's windows. */
    enum class StackingMode {
        NativeChildWindow,   ///< macOS AppKit -addChildWindow: (current default)
        QtRaiseOnActivate    ///< Portable raise() pass driven by activation
    };
    Q_ENUM(StackingMode)

    /*! \brief The process-wide instance. Created on first use; install it on
     *  the QApplication as an event filter to start tracking. */
    static DialogRegistry *instance();

    /*! \brief Resolve the configured mode: OPENSWMM_DIALOG_STACKING env var,
     *  then the "Window/DialogStacking" setting, then the platform default. */
    static StackingMode configuredStackingMode();

    StackingMode stackingMode() const { return mMode; }
    void setStackingMode(StackingMode mode);

    /*! \brief Open modeless top-level dialogs, least-recently-used first.
     *
     *  Guarded pointers, not raw ones: callers iterate this list while doing
     *  things to windows (moving, raising, showing), and a handler reached
     *  that way could close a dialog further along the list. Re-checking each
     *  entry as it is used is then meaningful rather than decorative. */
    QList<QPointer<QDialog>> openDialogs() const;

    /*! \brief Raise every registered dialog in MRU order, so the most recently
     *  used ends up on top. Never activates — stealing focus on an app switch
     *  is what the original AppKit attachment was chosen to avoid. */
    void raiseAllInOrder();

signals:
    /*! \brief A dialog was registered, removed, or reordered — the Window
     *  menu rebuilds from this. */
    void openDialogsChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit DialogRegistry(QObject *parent = nullptr);

    /*! \brief Modeless, top-level, non-transient dialogs only; nullptr for
     *  anything that should not appear in the Window menu. */
    static QDialog *trackableDialog(QObject *watched);

    void trackOrPromote(QDialog *dlg);
    void forget(QDialog *dlg);
    void scheduleRestack();

    QList<QPointer<QDialog>> mOrder;   ///< MRU — most recently used LAST
    StackingMode             mMode = StackingMode::NativeChildWindow;
    bool                     mRestackQueued = false;   ///< re-entrancy guard
};

}   // namespace openswmmvis::ui

#endif   // OPENSWMMVIS_UI_DIALOGS_DIALOGREGISTRY_H
