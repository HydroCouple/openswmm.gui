#ifndef COMMANDPALETTE_H
#define COMMANDPALETTE_H

/**
 * @file      commandpalette.h
 * @brief     UI redesign P7 — the command palette (Ctrl+Shift+P): a frameless popup with a filter
 *            line and a relevance-sorted action list reaching every ActionRegistry-registered
 *            capability (dock toggles and dialog launchers included).
 *
 * UI redesign P7 — the command palette (Ctrl+Shift+P): a frameless
 * popup with a filter line and a relevance-sorted action list reaching
 * every ActionRegistry-registered capability (dock toggles and dialog
 * launchers included). Enter triggers/toggles the selected action;
 * Esc or focusing away dismisses. Disabled actions render grayed and
 * are not triggerable.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QDialog>

class QLineEdit;
class QListView;
class QMainWindow;

namespace openswmmvis::ui
{

class CommandPaletteModel;

class CommandPalette : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Build a nonmodal frameless command popup with an initially empty registry model.
     * @param[in] parent Optional main-window owner used for popup placement; controls/delegate/model become children.
     */
    explicit CommandPalette(QMainWindow *parent);

    /*! Reload actions, clear the filter, center over the parent window
     *  and show.
     * @note Selects the first command, positions near the parent's upper centre, raises/activates the window and focuses the filter. Reload invalidates prior model indexes.
     */
    void popup();

    /**
     * @brief Access the palette's command filter for integration.
     * @return Borrowed widget-owned line edit.
     */
    QLineEdit *filterEdit() const { return mFilter; }
    /**
     * @brief Access the palette's command results list.
     * @return Borrowed widget-owned view.
     */
    QListView *listView() const { return mList; }
    /**
     * @brief Access the palette's filtered registry model.
     * @return Borrowed palette-owned model; do not delete independently.
     */
    CommandPaletteModel *model() const { return mModel; }

protected:
    /**
     * @brief Hide on window deactivation, then forward events to QDialog.
     * @param[in] event Required Qt event.
     * @return Base QDialog event-handling result.
     */
    bool event(QEvent *event) override;
    /**
     * @brief Translate filter-edit navigation/activation keys into palette actions.
     * @param[in] watched Watched QObject; custom handling applies only to the filter edit.
     * @param[in] event Required Qt event; Up/Down move one, PageUp/PageDown eight, Enter triggers, Escape hides.
     * @return True for handled keys, otherwise QDialog's filter result.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /**
     * @brief Trigger the selected enabled QAction after hiding the palette.
     * @note Invalid/disabled selections are ignored and leave the palette visible; triggering checkable actions follows QAction semantics.
     */
    void triggerCurrent();
    /**
     * @brief Move the current result selection within visible row bounds.
     * @param[in] delta Signed row offset, clamped to first/last result; empty models are ignored.
     */
    void moveSelection(int delta);

    CommandPaletteModel *mModel = nullptr;
    QLineEdit *mFilter = nullptr;
    QListView *mList = nullptr;
};

} // namespace openswmmvis::ui

#endif // COMMANDPALETTE_H
