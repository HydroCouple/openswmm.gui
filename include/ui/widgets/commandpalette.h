#ifndef COMMANDPALETTE_H
#define COMMANDPALETTE_H

/*!
 * \file commandpalette.h
 *
 * UI redesign P7 — the command palette (Ctrl+Shift+P): a frameless
 * popup with a filter line and a relevance-sorted action list reaching
 * every ActionRegistry-registered capability (dock toggles and dialog
 * launchers included). Enter triggers/toggles the selected action;
 * Esc or focusing away dismisses. Disabled actions render grayed and
 * are not triggerable.
 */

#include <QDialog>

class QLineEdit;
class QListView;
class QMainWindow;

namespace openswmmvis::ui {

class CommandPaletteModel;

class CommandPalette : public QDialog
{
    Q_OBJECT

public:
    explicit CommandPalette(QMainWindow *parent);

    /*! Reload actions, clear the filter, center over the parent window
     *  and show. */
    void popup();

    QLineEdit *filterEdit() const { return mFilter; }
    QListView *listView() const { return mList; }
    CommandPaletteModel *model() const { return mModel; }

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void triggerCurrent();
    void moveSelection(int delta);

    CommandPaletteModel *mModel = nullptr;
    QLineEdit *mFilter = nullptr;
    QListView *mList = nullptr;
};

}   // namespace openswmmvis::ui

#endif // COMMANDPALETTE_H
