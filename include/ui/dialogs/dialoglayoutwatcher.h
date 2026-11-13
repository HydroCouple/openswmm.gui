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
 * Accessibility defaults (review A-02/A-03/A-14): on the first Show of every
 * top-level dialog (named or not) and every dock panel, applyAccessibilityDefaults()
 * pairs inline "Label:" texts with the field after them and names unnamed item views,
 * so a screen reader announces what they are rather than only their kind.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QObject>

class QWidget;

namespace openswmmvis::ui
{

/// Dynamic property opting a QDialog out of automatic persistence.
inline constexpr char kNoLayoutPersistenceProp[] = "noLayoutPersistence";
/// Instance property marking that the first-show restore already ran.
inline constexpr char kLayoutRestoredOnceProp[] = "layoutRestoredOnce";
/// Instance property marking that the accessibility defaults already ran.
inline constexpr char kAccessibilityDefaultsProp[] = "accessibilityDefaultsApplied";

/**
 * @brief Give a window's unnamed controls the names a screen reader needs (A-02/A-03/A-14).
 * @details A QLabel whose plain text ends in ':' and that has no buddy becomes the buddy of
 *   the field right after it in its row, grid cell or form row (line edit, spin box, combo
 *   box, text edit, slider or item view); that field takes the label text as its accessible
 *   name when it has none. An item view without an accessible name is named from such a
 *   label, else "<group box title> table|list|tree", else "<window title> table|list|tree"
 *   (the kind is not repeated when the title already ends with it). Existing names and
 *   buddies are kept; views inside combo boxes and calendars, qt_-named views and controls
 *   of nested dialogs or docks are left alone.
 * @param[in,out] root Window or dock panel whose descendants are inspected.
 */
void applyAccessibilityDefaults(QWidget *root);

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
