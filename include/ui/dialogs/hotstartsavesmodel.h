/**
 * @file      hotstartsavesmodel.h
 * @brief     MVC backing for the scheduled-hot-start-saves table in the Simulation Options dialog
 *            ([FILES] SAVE HOTSTART).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * MVC backing for the scheduled-hot-start-saves table in the Simulation
 * Options dialog ([FILES] SAVE HOTSTART). Pairs with the reusable
 * PathBrowseDelegate (for the path column) and ships its own
 * HotstartSavesDateTimeDelegate for the datetime picker and
 * HotstartSavesFormatDelegate for the file-format choice.
 *
 * The delegates are intended for use with QTableView::openPersistentEditor()
 * so each row's path field, browse button, and date-time picker are visible
 * without first clicking into the cell.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef HOTSTARTSAVESMODEL_H
#define HOTSTARTSAVESMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QStyledItemDelegate>

/*!
 * \brief Row schema: {path relative to the .inp dir, OLE-Automation datetime
 *        sentinel, file format}. An oaDate of 0.0 means "save at end of run"
 *        and renders as the QDateTimeEdit special-value text "(end of run)".
 *        The format is the engine's SWMM_HOTSTART_FORMAT_* value (0 Auto,
 *        1 Legacy, 2 Native); EditRole carries the int, DisplayRole the name.
 */
class HotstartSavesModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColPath = 0, ColDateTime = 1, ColFormat = 2, ColCount };

    /**
     * @brief Create an empty editable schedule model.
     * @param parent Optional QObject owner; null leaves lifetime management to the caller.
     * All operations belong on the owning GUI thread. Rows are a local draft; this
     * model neither owns an engine nor writes hot-start/input files.
     */
    explicit HotstartSavesModel(QObject *parent = nullptr);

    // QAbstractTableModel -----------------------------------------------------
    /**
     * @brief Return the flat schedule row count.
     * @param parent Parent model index; a valid parent has no children.
     * @return Current draft size for the root, otherwise zero.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Return the flat schedule column count.
     * @param parent Parent index; a valid parent has no child columns.
     * @return Three path/datetime/format columns at the root, otherwise zero.
     */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Read a draft cell in model-native form or its format tooltip.
     * @param index Borrowed model cell with a current row; invalid/out-of-range rows return
     * empty.
     * @param role DisplayRole/EditRole return path QString, OA-date double or format
     * name/integer respectively. ToolTipRole is supported only for the format column.
     * @return Owned QVariant, empty for unsupported role/column. Datetime remains a
     * double even for display: zero is end-of-run, otherwise decimal days from
     * 1899-12-30; the delegate presents its wall-clock fields. No engine or file access.
     */
    QVariant data(const QModelIndex &index, int role) const override;
    /**
     * @brief Edit one draft cell and notify only accepted changes.
     * @param index Borrowed current model cell; invalid/out-of-range rows refuse.
     * @param value Path converts to QString; datetime converts to double without
     * conversion-success, finiteness or schedule-range validation. Format must convert
     * to an integer from zero through two.
     * @param role Must be EditRole.
     * @return True for accepted edits and unchanged values, false for invalid cell,
     * role, column or format. Datetime equality uses qFuzzyCompare. Emits dataChanged
     * for display/edit roles only on change; does not validate paths, Apply to an
     * engine or write a file. Invalid numeric conversion may become the zero sentinel.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @brief Return localized schedule column captions.
     * @param section Zero-based path/datetime/format column number.
     * @param orientation Only Horizontal is supported; vertical row headers are empty.
     * @param role Only DisplayRole is supported.
     * @return Owned caption or empty QVariant for an unsupported request.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @brief Expose valid model cells as selectable, enabled and editable.
     * @param index Borrowed index normally created by this model; only isValid is
     * checked, without row/column bounds or model-identity validation.
     * @return NoItemFlags for invalid indices, otherwise all three editing flags.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @brief Insert blank end-of-run Auto schedule rows.
     * @param row Zero-based insertion position from zero through current rowCount.
     * @param count Positive row count whose index arithmetic must fit int.
     * @param parent Must be invalid for this flat model.
     * @return True after begin/endInsertRows, false on rejected parent/count/position.
     * Default rows contain empty paths, OA date zero and format zero; no engine write.
     */
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    /**
     * @brief Remove a contiguous draft range.
     * @param row Zero-based first existing row.
     * @param count Positive number of rows fully inside the model; arithmetic must fit int.
     * @param parent Must be invalid for the flat model.
     * @return True after begin/endRemoveRows, false on rejected parent/range.
     * Does not remove any scheduled output files or alter an engine schedule.
     */
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    // Convenience -------------------------------------------------------------
    /**
     * @brief Reset a nonempty draft to no rows using model reset notifications.
     * Empty drafts are silent. Does not clear an engine schedule or delete files.
     */
    void clearRows();
    /**
     * @brief Append a copied schedule row with an optional format fallback.
     * @param path Borrowed path copied verbatim, normally relative to the input directory;
     * existence, emptiness and traversal are not checked here.
     * @param oaDate OA decimal days from 1899-12-30, zero for end-of-run; stored without
     * validation.
     * @param format Auto/Legacy/Native integer zero/one/two; other values become Auto.
     * @return Zero-based appended row, after insertion notifications; no engine/file IO.
     */
    int appendRow(const QString &path, double oaDate, int format = 0);
    /**
     * @brief Exchange two complete draft rows and notify their enclosing cell range.
     * @param a Zero-based existing first row.
     * @param b Distinct zero-based existing second row.
     * @return True after exchange, false for equal/out-of-range indices. Emits
     * dataChanged across both rows and intervening cells rather than row-move signals;
     * persistent indices remain associated with their positions.
     */
    bool swapRows(int a, int b);

    /**
     * @brief Copy the stored draft output path.
     * @param row Zero-based row, permitted invalid.
     * @return Owned path or empty if out of range; an empty result may also be a blank draft.
     */
    QString pathAt(int row) const;
    /**
     * @brief Read the stored schedule timestamp.
     * @param row Zero-based row, permitted invalid.
     * @return OA decimal days or zero when out of range; zero is also the valid
     * end-of-run sentinel, so it does not distinguish an invalid row.
     */
    double oaDateAt(int row) const;
    /**
     * @brief Read the stored file-format choice.
     * @param row Zero-based row, permitted invalid.
     * @return Auto/Legacy/Native as zero/one/two, with Auto also returned for an invalid row.
     */
    int formatAt(int row) const;

    //! Display name of a SWMM_HOTSTART_FORMAT_* value.
    /**
     * @brief Localize the engine hot-start format choice.
     * @param format Legacy one, Native two; every other integer selects Auto.
     * @return Owned translated name. Does not determine the eventual Auto output encoding.
     */
    static QString formatName(int format);

private:
    struct Row {
        QString path;
        double oaDate{0.0};
        int format{0};
    };
    QList<Row> m_rows;
};

/*!
 * \brief Delegate for the Datetime column. Editor is a QDateTimeEdit whose
 *        minimum date-time renders as the special value "(end of run)" so
 *        the user can pick a wall-clock time or leave it as the sentinel.
 */
class HotstartSavesDateTimeDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @brief Create a persistent schedule datetime delegate.
     * @param parent Optional QObject owner; null delegates are caller-owned.
     * The delegate must outlive its editors because their immediate-commit callback
     * captures this delegate while its Qt connection context is the editor.
     */
    explicit HotstartSavesDateTimeDelegate(QObject *parent = nullptr);

    /**
     * @brief Allocate the wall-clock datetime editor with end-of-run special value.
     * @param parent Borrowed owner for the new editor; null makes it caller-owned.
     * @param opt Unused style options.
     * @param idx Unused cell identity; hydration occurs separately.
     * @return New QDateTimeEdit with calendar popup, second-resolution display and
     * minimum 1899-12-30 representing OA zero. Every datetime change emits commitData
     * through an editor-context callback; the delegate must remain alive. No engine write.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override;
    /**
     * @brief Hydrate a datetime editor while blocking its change signals.
     * @param editor Borrowed editor; a non-QDateTimeEdit is ignored.
     * @param idx Borrowed source cell whose EditRole converts to OA days.
     * Positive OA values use the dialog wall-clock conversion; zero, negative and NaN
     * use the minimum/end-of-run display. Conversion failures have no separate error
     * path. Does not commit the hydrated value back to the model.
     */
    void setEditorData(QWidget *editor, const QModelIndex &idx) const override;
    /**
     * @brief Commit the datetime editor value into the draft model.
     * @param editor Borrowed QDateTimeEdit; another type is ignored.
     * @param model Borrowed destination model, permitted null.
     * @param idx Borrowed destination cell passed through to setData.
     * Writes zero for the editor minimum, otherwise the dialog OA conversion, using
     * EditRole. Does not inspect the model write result, save files or Apply an engine edit.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &idx) const override;
};

/*!
 * \brief Delegate for the Format column: a combo of Auto / Legacy / Native
 *        that commits on every change, like the datetime picker.
 */
class HotstartSavesFormatDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @brief Create the persistent Auto/Legacy/Native editor delegate.
     * @param parent Optional QObject owner; null delegates are caller-owned.
     * Must outlive its editors because their immediate-commit callbacks capture this
     * while Qt uses each editor as the connection context.
     */
    explicit HotstartSavesFormatDelegate(QObject *parent = nullptr);

    /**
     * @brief Allocate a three-choice format combo and connect immediate commits.
     * @param parent Borrowed owner of the new editor; null makes it caller-owned.
     * @param opt Unused style options.
     * @param idx Unused source cell; hydration occurs separately.
     * @return New frameless combo containing localized names and engine integers
     * zero/one/two. Index changes emit commitData; no schedule or file is written here.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override;
    /**
     * @brief Hydrate the format combo while blocking change signals.
     * @param editor Borrowed combo; non-QComboBox widgets are ignored.
     * @param idx Borrowed source cell whose EditRole converts to an integer.
     * Chooses the matching item data or Auto at index zero when unknown; invalid
     * QVariant integer conversion also defaults zero. Does not commit during hydration.
     */
    void setEditorData(QWidget *editor, const QModelIndex &idx) const override;
    /**
     * @brief Commit combo item data into the draft model.
     * @param editor Borrowed combo; a different widget type is ignored.
     * @param model Borrowed destination model, permitted null.
     * @param idx Borrowed target cell passed to setData.
     * Writes currentData using EditRole without inspecting success or creating output files.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &idx) const override;
};

#endif // HOTSTARTSAVESMODEL_H
