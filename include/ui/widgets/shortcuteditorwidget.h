#ifndef SHORTCUTEDITORWIDGET_H
#define SHORTCUTEDITORWIDGET_H

/*!
 * \file shortcuteditorwidget.h
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
 */

#include <QWidget>

class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace openswmmvis::ui {

class ShortcutEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ShortcutEditorWidget(QWidget *parent = nullptr);

    /*! Rebuild the command tree from the registry. */
    void reload();

    /*! Select the row for catalog id \a id (expands its category). */
    void selectCommand(const QString &id);
    QString selectedCommandId() const;

    // Test / integration surface.
    QTreeWidget *tree() const { return mTree; }
    QKeySequenceEdit *sequenceEdit() const { return mSequenceEdit; }
    QPushButton *assignButton() const { return mAssign; }
    QPushButton *clearButton() const { return mClear; }
    QPushButton *resetButton() const { return mReset; }
    QPushButton *resetAllButton() const { return mResetAll; }
    QLabel *conflictLabel() const { return mConflict; }

private:
    void onFilterChanged(const QString &text);
    void onSelectionChanged();
    void onSequenceChanged();
    void onAssign();
    void onClear();
    void onReset();
    void onResetAll();
    void refreshItem(QTreeWidgetItem *item);

    QLineEdit        *mFilter = nullptr;
    QTreeWidget      *mTree = nullptr;
    QKeySequenceEdit *mSequenceEdit = nullptr;
    QPushButton      *mAssign = nullptr;
    QPushButton      *mClear = nullptr;
    QPushButton      *mReset = nullptr;
    QPushButton      *mResetAll = nullptr;
    QLabel           *mConflict = nullptr;
};

}   // namespace openswmmvis::ui

#endif // SHORTCUTEDITORWIDGET_H
