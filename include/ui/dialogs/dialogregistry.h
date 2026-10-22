/**
 * @file      dialogregistry.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     App-wide register of open modeless dialogs, in most-recently-used
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
 *     In both modes, showing or activating a modeless dialog also raises
 *     its visible peers in MRU order, so opening a rainfall plot cannot
 *     leave an existing comparison plot buried behind the main window.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace openswmmvis::ui
{

class DialogRegistry : public QObject
{
    Q_OBJECT

public:
    /*! \brief How modeless dialogs are kept above the application's windows. */
    enum class StackingMode {
        NativeChildWindow, ///< macOS AppKit -addChildWindow: (current default)
        QtRaiseOnActivate  ///< Portable raise() pass driven by activation
    };
    Q_ENUM(StackingMode)

    /*! \brief The process-wide instance. Created on first use; install it on
     *  the QApplication as an event filter to start tracking.
     * @return Borrowed process-wide heap singleton, lazily created without parent and retained to
     *   process exit. Does NOT install itself as application filter or apply
     *   configuredStackingMode; host does both. GUI-thread first/use required for
     *   QObject/timer/window operations, no deletion by caller.
     */
    static DialogRegistry *instance();

    /*! \brief Resolve the configured mode: OPENSWMM_DIALOG_STACKING env var,
     *  then the "Window/DialogStacking" setting, then the platform default.
     * @return Trimmed case-insensitive env qt/native, else QSettings Window/DialogStacking
     *   qt/native, else Native on macOS/QtRaise elsewhere. Invalid env falls through preference.
     *   Read EVERY invocation, no state/persistence/native-window mutation here.
     */
    static StackingMode configuredStackingMode();

    /**
     * @return Stored registry mode, initially NativeChildWindow even platforms whose configured
     *   default differs until caller sets it. No live settings/environment query.
     */
    StackingMode stackingMode() const { return mMode; }
    /**
     * @param[in] mode Raw enum stored verbatim, no validation/equality check.
     * @note Store only: no signal, persistence, restack, detach/attach native window or retroactive
     *   tracking. Platform attachment handled outside registry.
     */
    void setStackingMode(StackingMode mode);

    /*! \brief Open modeless top-level dialogs, least-recently-used first.
     *
     *  Guarded pointers, not raw ones: callers iterate this list while doing
     *  things to windows (moving, raising, showing), and a handler reached
     *  that way could close a dialog further along the list. Re-checking each
     *  entry as it is used is then meaningful rather than decorative.
     * @return Value copy of currently nonnull entries, least-recently-used first. May include
     *   invisible/minimized/spontaneously-hidden windows; dead entries filtered, NOT removed from
     *   internal list by this getter. Recheck QPointer per use, no ownership transfer or automatic
     *   open-existing-window enumeration.
     */
    QList<QPointer<QDialog>> openDialogs() const;

    /*! \brief Raise every registered dialog in MRU order, so the most recently
     *  used ends up on top. Never activates — stealing focus on an app switch
     *  is what the original AppKit attachment was chosen to avoid.
     * @note If any active modal widget no-op. Snapshot nonnull MRU list, recheck each visible
     *   dialog and call raise least-used first without activateWindow; raw stacking mode not
     *   checked, so can be explicitly called in either mode. GUI-thread window operations, no show
     *   of hidden windows or source edits. Raising may dispatch events; guards protect deleted
     *   later peers.
     */
    void raiseAllInOrder();

signals:
    /*! \brief The tracked live set changed — the Window
     *  menu rebuilds from this.
     * @note Emitted for new tracked dialog, pruning dead entries during promote, or actual live
     *   forget. PURE MRU reorder intentionally emits nothing, and dead-only housekeeping in
     *   forget/read emits nothing. Receiver rebuilds recovery menu; signal does not raise/show/own
     *   dialogs.
     */
    void openDialogsChanged();

protected:
    /**
     * @param[in] watched Borrowed event target; QDialog/widget casts checked, nonmatching target
     *   ignored.
     * @param[in] event Required borrowed Qt event, no null guard, not retained.
     * @return QObject base filter result, never intentionally consumes tracked event.
     * @note Show/activate eligible dialog tracks/promotes and queues restack in BOTH modes;
     *   ordinary-window/app activation restacks QtRaise mode only. Spontaneous Hide preserves
     *   recovery entry, explicit Hide/Close forgets eligible live dialog. No external native
     *   attachment or settings save here. Install filter explicitly on app, GUI-thread event loop
     *   required.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /**
     * @param[in] parent Optional QObject owner, singleton uses null.
     * @brief Initialize empty order, stored NativeChildWindow and no pending timer; do not read
     *   preference/install filter or create windows.
     */
    explicit DialogRegistry(QObject *parent = nullptr);

    /*! \brief Modeless, top-level, non-transient dialogs only; nullptr for
     *  anything that should not appear in the Window menu.
     * @param[in] watched Optional borrowed QObject inspected with qobject_cast.
     * @return Borrowed QDialog only when window+NonModal and window type not
     *   Popup/ToolTip/SplashScreen, else null. Does not check visibility/name/noLayoutPersistence
     *   or existing registration. No ownership transfer/window mutation.
     */
    static QDialog *trackableDialog(QObject *watched);

    /**
     * @param[in] dlg Optional borrowed live dialog, null no-op; function itself does NOT call
     *   trackableDialog or verify modality/window flags.
     * @note Remove dead entries/all occurrences of dialog, append guarded dialog as MRU last. Emit
     *   openDialogsChanged only new target/dead pruning, not pure promotion. No
     *   show/activate/raise/scheduled restack or ownership transfer itself.
     */
    void trackOrPromote(QDialog *dlg);
    /**
     * @param[in] dlg Optional borrowed identity to remove; null only prunes dead entries.
     * @note Remove target/live matches and dead QPointers, emit openDialogsChanged only if actual
     *   live target removed. No hide/close/delete, no restack or mutation of other window MRU
     *   order.
     */
    void forget(QDialog *dlg);
    /**
     * @note Queue one context-bound zero-delay timer unless already pending. Keep pending flag
     *   DURING raiseAllInOrder to prevent activation feedback; clear after pass even modal skip. No
     *   retry after skipped modal or source/window activation; destructor/context removes timer.
     *   Requires GUI event loop, no settings/native attach.
     */
    void scheduleRestack();

    QList<QPointer<QDialog>> mOrder; ///< MRU — most recently used LAST
    StackingMode mMode = StackingMode::NativeChildWindow;
    bool mRestackQueued = false; ///< re-entrancy guard
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_DIALOGREGISTRY_H
