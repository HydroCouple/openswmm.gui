/**
 * @file      tableclipboard.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Excel-style copy / paste of selected table rows for editor dialogs.
 *
 * One helper gives every editor table the same clipboard behaviour:
 *
 *  - **Copy** (Ctrl/Cmd+C) puts the selected cells' rows on the clipboard as
 *    tab-separated text with a header line, which Excel, Sheets and text
 *    editors accept directly. A single selected cell copies just its value.
 *    Nothing selected copies the whole table.
 *  - **Paste** (Ctrl/Cmd+V) reads tab-, comma- or semicolon-separated text
 *    (quoted cells included) and writes it into the table starting at the top
 *    selected row. A full-width block fills whole rows; a narrower block
 *    starts at the selected column (or, for a whole-row selection, at the
 *    first editable column). A header line is skipped. A single value fills
 *    every selected cell. Rows past the end are added when the dialog
 *    supplies an \c appendRow callback. Empty pasted cells leave the target
 *    cell unchanged.
 *
 * Writes go through the same channels a user's typing would, so the
 * dialog's existing validation and commit paths stay the only way data
 * changes:
 *  - a plain cell is edited through its delegate's editor (so a combo column
 *    maps a pasted label to the value it stores, and validators apply);
 *  - a QTableWidget cell widget (combo box, spin box, date edit, check box,
 *    line edit) has its value set and the signal a user's edit would send
 *    emitted; a model view's open persistent editor commits through its
 *    delegate.
 *
 * Copy/Paste keys are claimed while the table or a cell widget in it has
 * focus — like a line edit claims them — so a window's own Copy action does
 * not compete with the table's. A text editor in a cell keeps them while a
 * cell is being edited, for Copy of its selected text, and for Paste of a
 * single value.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_UTIL_TABLECLIPBOARD_H
#define OPENSWMMVIS_UI_UTIL_TABLECLIPBOARD_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

class QAction;
class QEvent;
class QModelIndex;
class QTableView;
class QUndoStack;
class QWidget;

namespace openswmmvis::ui {

/** @brief Per-table wiring supplied by the hosting dialog. */
struct TableClipboardOptions {
    /**
     * @brief Adds one empty row at the end of the table.
     * @return \c true when a row was added. Leave empty for tables whose
     *         row count is fixed; a paste that runs past the last row is
     *         then truncated and reported.
     */
    std::function<bool()> appendRow;
    /** @brief When set, one paste becomes one undo step. */
    QUndoStack *undoStack = nullptr;
    /** @brief Copy only (results, previews, pickers): no Paste action, and
     *  the Paste key is left to the window. */
    bool copyOnly = false;
    /**
     * @brief Writes a whole pasted block at once, for tables whose rows must
     *        change together (e.g. stations that must stay increasing).
     *
     * Receives the first target row, the first target column (logical) and
     * the pasted rows with any header removed.
     * @return Rows written, or -1 to let the cell-by-cell paste run instead.
     *         \a message receives the feedback shown to the user.
     */
    std::function<int(int firstRow, int firstColumn, const QList<QStringList> &rows,
                      QString *message)>
        pasteRows;
};

/**
 * @brief Copy / Paste actions bound to one table view (see file comment).
 *
 * Owned by the view it is installed on.
 */
class TableClipboard final : public QObject
{
    Q_OBJECT

public:
    using Options = TableClipboardOptions;

    /**
     * @brief Adds Copy and Paste to \a view: keyboard shortcuts, and a
     *        right-click menu on the cells and row headers when they have no
     *        context menu of their own.
     *
     * A view that already shows a custom context menu keeps it; its handler
     * adds copyAction() and pasteAction() itself. Plain cells of a view with
     * no edit triggers are treated as read-only; its cell widgets and check
     * boxes still accept pasted values.
     */
    static TableClipboard *install(QTableView *view, Options options = {});

    /** @brief The Copy action (Ctrl/Cmd+C), for a dialog's own context menu. */
    QAction *copyAction() const { return m_copy; }
    /** @brief The Paste action (Ctrl/Cmd+V), for a dialog's own context menu;
     *  null for a copy-only table. */
    QAction *pasteAction() const { return m_paste; }

    /** @brief Text Copy puts on the clipboard for the current selection. */
    QString selectionText() const;

    /**
     * @brief Writes \a text into the table as Paste does.
     * @return \c true when at least one cell was written; always \c false
     *         for a copy-only table.
     */
    bool pasteText(const QString &text);

    /** @brief Feedback from the last copy or paste (also shown to the user). */
    QString lastMessage() const { return m_lastMessage; }

    /**
     * @brief Splits delimited text into rows of cells.
     *
     * The delimiter is a tab when the text has one, otherwise a semicolon
     * when the first line has one outside quotes (comma-decimal locales),
     * otherwise a comma.
     * Double-quoted cells may contain delimiters, line breaks and doubled
     * quotes. A trailing line break does not add an empty row.
     */
    static QList<QStringList> parseDelimited(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    TableClipboard(QTableView *view, Options options);

    enum class CellResult { Written, ReadOnly, Rejected };

    void copyToClipboard();
    void pasteFromClipboard();
    void watchTree(QObject *object);
    bool isOpenCellEditor(QObject *object) const;
    QList<int> visibleColumns() const;
    QString headerLabel(int column) const;
    bool hasTextHeader(const QList<int> &columns) const;
    bool isHeaderRow(const QList<QStringList> &rows, const QList<int> &targetColumns) const;
    QString cellText(const QModelIndex &index) const;
    bool isDelegateEditor(const QModelIndex &index) const;
    bool isCheckOnly(const QModelIndex &index) const;
    bool isEditable(const QModelIndex &index) const;
    CellResult writeCell(const QModelIndex &index, const QString &token);
    CellResult writeThroughDelegate(const QModelIndex &index, const QString &token,
                                    QWidget *openEditor);
    CellResult writePlain(const QModelIndex &index, const QString &token);
    void notify(const QString &message);

    QPointer<QTableView> m_view;
    Options m_options;
    QAction *m_copy = nullptr;
    QAction *m_paste = nullptr;
    QString m_lastMessage;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_UTIL_TABLECLIPBOARD_H
