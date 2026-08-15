#ifndef DIALOGLAYOUTWATCHER_H
#define DIALOGLAYOUTWATCHER_H

/*!
 * \file dialoglayoutwatcher.h
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
 */

#include <QObject>

namespace openswmmvis::ui {

/// Dynamic property opting a QDialog out of automatic persistence.
inline constexpr char kNoLayoutPersistenceProp[] = "noLayoutPersistence";
/// Instance property marking that the first-show restore already ran.
inline constexpr char kLayoutRestoredOnceProp[] = "layoutRestoredOnce";

class DialogLayoutWatcher : public QObject
{
    Q_OBJECT

public:
    explicit DialogLayoutWatcher(QObject *parent = nullptr);

    bool eventFilter(QObject *watched, QEvent *event) override;
};

}   // namespace openswmmvis::ui

#endif // DIALOGLAYOUTWATCHER_H
