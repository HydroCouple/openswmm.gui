#ifndef DIALOGLAYOUTWATCHER_H
#define DIALOGLAYOUTWATCHER_H

/**
 * @file      dialoglayoutwatcher.h
 * @brief     UI redesign iteration 2 (D1) — app-wide automatic dialog layout persistence.
 *
 * UI redesign iteration 2 (D1) — app-wide automatic dialog layout
 * persistence. Installed as its OWN application event filter (one line
 * in the SWMMVisApplication ctor; deliberately separate from the
 * existing macOS stacking filter): every top-level QDialog with a
 * non-empty objectName gets restoreDialogLayout() synchronously on its
 * FIRST Show (pre-map — after the ctor, so hard-coded defaults are the
 * first-run values and saved geometry wins thereafter) and
 * saveDialogLayout() on Hide/Close (Hide covers the QDialog::done() /
 * exec() paths, which never emit Close).
 *
 * Opt-out: set the "noLayoutPersistence" dynamic property (the command
 * palette does — it is deliberately fixed-size). Unnamed dialogs are
 * implicitly out.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QObject>

namespace openswmmvis::ui
{

/// Dynamic property opting a QDialog out of automatic persistence.
inline constexpr char kNoLayoutPersistenceProp[] = "noLayoutPersistence";
/// Instance property marking that the first-show restore already ran.
inline constexpr char kLayoutRestoredOnceProp[] = "layoutRestoredOnce";

class DialogLayoutWatcher : public QObject
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QObject owner, independent of QApplication lifetime.
     * @brief Construct filter object only, not automatically installed; host installs on live
     *   QApplication and preserves through GUI events. No layout read/write until events.
     */
    explicit DialogLayoutWatcher(QObject *parent = nullptr);

    /**
     * @param[in] watched Borrowed optional event target checked as eligible named top-level
     *   QDialog.
     * @param[in] event Required borrowed Qt event, no null guard/ownership retention.
     * @return QObject base filter result, does not intentionally consume Show/Hide/Close.
     * @note First eligible Show marks layoutRestoredOnce BEFORE synchronous restore (even no
     *   settings), later show no restore. Every Hide/Close saves including spontaneous hide; modal
     *   dialogs eligible, root noLayoutPersistence opts out. Destruction cast may fail and skip
     *   save. No content/engine write or automatic filter installation.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;
};

} // namespace openswmmvis::ui

#endif // DIALOGLAYOUTWATCHER_H
