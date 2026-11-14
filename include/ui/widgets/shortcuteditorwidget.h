#ifndef SHORTCUTEDITORWIDGET_H
#define SHORTCUTEDITORWIDGET_H

/**
 * @file      shortcuteditorwidget.h
 * @brief     UI redesign P8 — the Keyboard page of Preferences: browse every ActionRegistry
 *            command grouped by category, rebind with a QKeySequenceEdit, and reset one or all
 *            bindings.
 *
 * UI redesign P8 — the Keyboard page of Preferences: browse every
 * ActionRegistry command grouped by category, rebind with a
 * QKeySequenceEdit, and reset one or all bindings. Writes go straight
 * through ActionRegistry (applied live, persisted under
 * SWMMVis::Shortcuts) — there is no pending-apply state.
 *
 * Conflict policy (registry scope): an exact duplicate of another
 * command's binding hard-blocks Assign, naming the holder; sequences on
 * the platform-reserved list only warn.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QWidget>

class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace openswmmvis::ui
{

class ShortcutEditorWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Create the live shortcut editor, populate registered commands and subscribe to external shortcut changes.
     * @param[in] parent Optional QWidget owner; updates write through ActionRegistry immediately on the GUI thread.
     */
    explicit ShortcutEditorWidget(QWidget *parent = nullptr);

    /*! Rebuild the command tree from the registry.
     * @note Preserves the selected command ID when available and reapplies the current filter. Commands lacking a catalog entry/action/text are omitted; all tree item pointers are replaced.
     */
    void reload();

    /*! Select the row for catalog id \a id (expands its category).
     * @param[in] id Exact registered catalog identity to select; absent IDs leave selection unchanged.
     */
    void selectCommand(const QString &id);
    /**
     * @brief Read the catalog identity stored on the selected command row.
     * @return Empty without a command selection, including a category header.
     */
    QString selectedCommandId() const;

    // Test / integration surface.
    /**
     * @brief Access the command tree for integration.
     * @return Borrowed child control; its lifetime follows this widget.
     */
    QTreeWidget *tree() const { return mTree; }
    /**
     * @brief Access the proposed-sequence editor for integration.
     * @return Borrowed child control; its lifetime follows this widget.
     */
    QKeySequenceEdit *sequenceEdit() const { return mSequenceEdit; }
    /**
     * @brief Access the Assign button for integration.
     * @return Borrowed child control; its lifetime follows this widget.
     */
    QPushButton *assignButton() const { return mAssign; }
    /**
     * @brief Access the Clear button for integration.
     * @return Borrowed child control; its lifetime follows this widget.
     */
    QPushButton *clearButton() const { return mClear; }
    /**
     * @brief Access the Reset button for integration.
     * @return Borrowed child control; its lifetime follows this widget.
     */
    QPushButton *resetButton() const { return mReset; }
    /**
     * @brief Access the Reset All button for integration.
     * @return Borrowed child control; its lifetime follows this widget.
     */
    QPushButton *resetAllButton() const { return mResetAll; }
    /**
     * @brief Access the conflict/warning label for integration.
     * @return Borrowed child control; its lifetime follows this widget.
     */
    QLabel *conflictLabel() const { return mConflict; }

private:
    /**
     * @brief Hide command rows whose title/category does not contain the query.
     * @param[in] text Trimmed case-insensitive substring; empty reveals all rows, and empty categories are hidden.
     */
    void onFilterChanged(const QString &text);
    /**
     * @brief Populate the sequence editor from the first effective shortcut and update Assign/Clear/Reset availability.
     * @note Blocks sequence-edit signals while loading and clears any conflict message.
     */
    void onSelectionChanged();
    /**
     * @brief Check the proposed sequence against exact registry conflicts and reserved-sequence warnings.
     * @note Empty/conflicting proposals disable Assign; reserved sequences display a warning but remain assignable. No registry mutation occurs here.
     */
    void onSequenceChanged();
    /**
     * @brief Store the current sequence as the selected command's live user override and restore selection.
     * @note No-op without a command; UI validation in onSequenceChanged() controls button availability. This handler does not repeat conflict checks.
     */
    void onAssign();
    /**
     * @brief Store an empty user shortcut for the selected command, immediately disabling its binding.
     * @note No-op without a command; registry change notifications rebuild the command tree.
     */
    void onClear();
    /**
     * @brief Remove the selected command's user override so catalog defaults apply again.
     * @note No-op without a command; writes through the registry immediately.
     */
    void onReset();
    /**
     * @brief Reset each registered command that currently has a user shortcut override.
     * @note Writes each change live through ActionRegistry; resulting notifications can reload the tree repeatedly.
     */
    void onResetAll();
    /**
     * @brief Update one command row with the first effective/default shortcut and override bolding.
     * @param[in,out] item Required live command tree item carrying its catalog ID.
     */
    void refreshItem(QTreeWidgetItem *item);

    QLineEdit *mFilter = nullptr;
    QTreeWidget *mTree = nullptr;
    QKeySequenceEdit *mSequenceEdit = nullptr;
    QPushButton *mAssign = nullptr;
    QPushButton *mClear = nullptr;
    QPushButton *mReset = nullptr;
    QPushButton *mResetAll = nullptr;
    QLabel *mConflict = nullptr;
};

} // namespace openswmmvis::ui

#endif // SHORTCUTEDITORWIDGET_H
