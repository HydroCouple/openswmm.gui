/**
 * @file      attributedelegates.h
 * @brief     Shared edit delegates used by both the Attribute Table (Slice Z.1) and the Property
 *            Browser (Slice AG.3, future).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice Z.5.1 — Shared edit delegates used by both the Attribute
 * Table (Slice Z.1) and the Property Browser (Slice AG.3, future).
 *
 * Pure-Qt — no engine deps.  Each delegate is small enough to be a
 * single class.  The right one for a given column is picked by
 * `SWMMAttributeTableModel`'s `ColumnSpec` schema (Z.5.2) and
 * installed by `AttributeTablePanel` after `setSource()`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef ATTRIBUTEDELEGATES_H
#define ATTRIBUTEDELEGATES_H

#include <QStyledItemDelegate>
#include <QVariant>
#include <QVariantList>
#include <limits>

namespace openswmmvis
{

/*! Numeric (double) editor with optional min/max/decimals. */
class NumericDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /*! \p step 0 derives one unit of the second-to-last decimal
     *  (10^(1-decimals)); a positive value is used as given (feedback C7).
     * @param[in] parent Optional QObject owner; delegate does not own source model or externally
     *   bound engine objects.
     * @brief Store delegate configuration; GUI-thread editor/model operations only, no editor
     *   opened or engine write at construction.
     * @param[in] minimum Requested spin minimum, raw double including default -infinity; passed to
     *   Qt range normalization.
     * @param[in] maximum Requested spin maximum, raw double including default +infinity.
     * @param[in] decimals Requested decimal precision, passed to QDoubleSpinBox::setDecimals
     *   without local clamp.
     * @param[in] step Positive explicit step, otherwise derive 10^(1-decimals), including
     *   negative/NaN fallback.
     */
    explicit NumericDelegate(QObject *parent = nullptr,
                             double minimum = -std::numeric_limits<double>::infinity(),
                             double maximum = std::numeric_limits<double>::infinity(),
                             int decimals = 4, double step = 0.0);

    /**
     * @param[in] parent Optional QWidget owner for returned editor; caller/view owns parentless
     *   return.
     * @param[in] opt Unused style option.
     * @param[in] index EditRole/optionsRole read only for compound/file-column dispatch; otherwise
     *   unused.
     * @return New parent-owned QDoubleSpinBox with configured decimals/range/derived step,
     *   keyboardTracking false and edit-tracking properties wired from value/text user changes. No
     *   immediate model mutation.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed editor from this delegate; wrong/null behavior described below.
     * @param[in] index Source cell, EditRole read, expected stable model/index.
     * @note QDoubleSpinBox only; convert EditRole toDouble(success flag), block signals and set
     *   number or 0/clear on failure. Set attributeValueWasNull=!conversionOK and Edited=false, so
     *   failed conversion treated null, not engine missing-value query. No source write/undo here.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed matching editor; delegate never deletes it.
     * @param[in] model Required mutable destination model for accepted editor path, unchecked null.
     * @param[in] index Destination cell forwarded unchanged; valid source/context must survive any
     *   compound/modal work.
     * @note Wrong/null spin ignored. If original conversion failed and no user edit tracked, NO
     *   write. Otherwise interpretText then submit normalized spin double; deleting text from a
     *   nonnull numeric cell is not a null-clear operation here. Uses EditRole, ignores setData
     *   result, no local engine/undo transaction or error dialog; model owns
     *   notifications/validation.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

private:
    double m_min;
    double m_max;
    int m_decimals;
    double m_step;
};

/*! Integer editor with optional min/max. */
class IntegerDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner; delegate does not own source model or externally
     *   bound engine objects.
     * @brief Store delegate configuration; GUI-thread editor/model operations only, no editor
     *   opened or engine write at construction.
     * @param[in] minimum Spin minimum, default INT_MIN.
     * @param[in] maximum Spin maximum, default INT_MAX; Qt normalizes inconsistent range.
     */
    explicit IntegerDelegate(QObject *parent = nullptr,
                             int minimum = std::numeric_limits<int>::min(),
                             int maximum = std::numeric_limits<int>::max());

    /**
     * @param[in] parent Optional QWidget owner for returned editor; caller/view owns parentless
     *   return.
     * @param[in] opt Unused style option.
     * @param[in] index EditRole/optionsRole read only for compound/file-column dispatch; otherwise
     *   unused.
     * @return New parent-owned QSpinBox with configured integer range, keyboardTracking false and
     *   edit-tracking property hooks. No immediate model mutation.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed editor from this delegate; wrong/null behavior described below.
     * @param[in] index Source cell, EditRole read, expected stable model/index.
     * @note QSpinBox only; EditRole toInt(success flag), block signals, set integer or 0/clear on
     *   conversion failure. Record WasNull/Edited properties; no fallback to base for wrong type.
     *   No source write/undo here.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed matching editor; delegate never deletes it.
     * @param[in] model Required mutable destination model for accepted editor path, unchecked null.
     * @param[in] index Destination cell forwarded unchanged; valid source/context must survive any
     *   compound/modal work.
     * @note Wrong/null spin ignored. Original conversion failure with no edit preserves null;
     *   otherwise interpretText then submit spin int. No null-clearing mechanism provided. Uses
     *   EditRole, ignores setData result, no local engine/undo transaction or error dialog; model
     *   owns notifications/validation.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

private:
    int m_min;
    int m_max;
};

/*! Compound-attribute editor — renders a NodeCompoundEditButton in
 *  the cell. Used by the Attribute Table for per-node multi-row
 *  attributes (Inflows / DWF / RDII / Treatment); reuses the same
 *  dialog the Property Browser opens. The model returns a
 *  NodeCompoundEditRef QVariant for these cells; displayText() shows
 *  the ref's summary so the cell stays informative when not in edit
 *  mode.
 *
 *  The metatype + QString converter need to be registered once at
 *  startup via registerNodeCompoundEditRefConverter() — the Attribute
 *  Table Panel does this in its constructor. */
class CompoundEditDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner; delegate does not own source model or externally
     *   bound engine objects.
     * @brief Store delegate configuration; GUI-thread editor/model operations only, no editor
     *   opened or engine write at construction.
     */
    explicit CompoundEditDelegate(QObject *parent = nullptr);

    /**
     * @param[in] value Typed compound ref or arbitrary QVariant.
     * @param[in] locale Ignored, implementation unnamed.
     * @return Node/Link/UserFlags/Subcatch summary plus Edit… or Edit… if empty; DataObject
     *   currentName or (unassigned); unrecognized value.toString. No engine lookup, dialog creation
     *   or mutation.
     */
    QString displayText(const QVariant &value, const QLocale &locale) const override;
    /**
     * @param[in] parent Optional QWidget owner for returned editor; caller/view owns parentless
     *   return.
     * @param[in] opt Unused style option.
     * @param[in] index EditRole/optionsRole read only for compound/file-column dispatch; otherwise
     *   unused.
     * @return New parent-owned Compound widget selected by EditRole metatype:
     *   Link/DataObject/UserFlags/Subcatch special editors, NodeCompoundEditButton FALLBACK for
     *   every other type. No ref seeded until setEditorData. No immediate model mutation.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed editor from this delegate; wrong/null behavior described below.
     * @param[in] index Source cell, EditRole read, expected stable model/index.
     * @note Only matching metatype/widget pair receives ref setValue for
     *   Node/Link/DataObject/UserFlags/Subcatch; mismatched/null/unrecognized editor silently
     *   unchanged. Ref may contain borrowed model callbacks, lifetime follows existing widget
     *   contract. No source write/undo here.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed matching editor; delegate never deletes it.
     * @param[in] model Required mutable destination model for accepted editor path, unchecked null.
     * @param[in] index Destination cell forwarded unchanged; valid source/context must survive any
     *   compound/modal work.
     * @note Recognized widget submits refreshed typed ref QVariant. Node/Link/UserFlags/Subcatch
     *   dialogs typically already applied edits and model uses commit for cache/notification;
     *   DataObject picker setData triggers actual object assignment. Delegate does not itself know
     *   setter success/undo. Uses EditRole, ignores setData result, no local engine/undo
     *   transaction or error dialog; model owns notifications/validation.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

/*! Enum editor.  The list is `{label, data}` pairs — display shows
 *  the label, the model stores the data value. */
class EnumDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /*! Values are QVariantList entries, each a 2-element
     *                 [label QString, data QVariant] pair.
     * @param[in] parent Optional QObject owner; delegate does not own source model or externally
     *   bound engine objects.
     * @brief Store delegate configuration; GUI-thread editor/model operations only, no editor
     *   opened or engine write at construction.
     * @param[in] values Moved pair-list, each convertible 2 entry QVariantList[label,data];
     *   malformed entries skipped at editor creation, no deduplication.
     */
    EnumDelegate(QObject *parent, QVariantList values);

    /**
     * @param[in] parent Optional QWidget owner for returned editor; caller/view owns parentless
     *   return.
     * @param[in] opt Unused style option.
     * @param[in] index EditRole/optionsRole read only for compound/file-column dispatch; otherwise
     *   unused.
     * @return New parent-owned Noneditable combo of valid stored [label,data] pairs, initial
     *   selection left to Qt until setEditorData. No immediate model mutation.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed editor from this delegate; wrong/null behavior described below.
     * @param[in] index Source cell, EditRole read, expected stable model/index.
     * @note Combo only; select findData(EditRole) or row 0 fallback if unknown, potentially
     *   changing unknown value on later commit. Empty list remains no selection. No source
     *   write/undo here.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed matching editor; delegate never deletes it.
     * @param[in] model Required mutable destination model for accepted editor path, unchecked null.
     * @param[in] index Destination cell forwarded unchanged; valid source/context must survive any
     *   compound/modal work.
     * @note Combo only, submit currentData verbatim, including invalid when empty/no current row.
     *   No revalidation of pair schema. Uses EditRole, ignores setData result, no local engine/undo
     *   transaction or error dialog; model owns notifications/validation.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

    /*! Helper: build the {label, data} pair-list from two parallel
     *  StringList / VariantList arrays.  Useful for static-table
     *  schema definitions.
     * @param[in] labels Display labels, order preserved.
     * @param[in] data Stored values corresponding by index.
     * @return New nested [label,data] pair list with min(input sizes) entries; unmatched tail
     *   silently ignored, no uniqueness/type validation or input mutation.
     */
    static QVariantList makePairs(const QStringList &labels, const QVariantList &data);

private:
    QVariantList m_values;
};

/*! Editable interval editor — a QComboBox seeded with the legacy rain gage
 *  H:MM presets (0:01 … 24:00) but editable, so the user can also type a
 *  custom clock value (legacy esComboEdit). The cell value is the H:MM
 *  string; the model's setter wrappers convert to/from engine seconds. */
class IntervalDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner; delegate does not own source model or externally
     *   bound engine objects.
     * @brief Store delegate configuration; GUI-thread editor/model operations only, no editor
     *   opened or engine write at construction.
     */
    explicit IntervalDelegate(QObject *parent = nullptr);

    /**
     * @param[in] parent Optional QWidget owner for returned editor; caller/view owns parentless
     *   return.
     * @param[in] opt Unused style option.
     * @param[in] index EditRole/optionsRole read only for compound/file-column dispatch; otherwise
     *   unused.
     * @return New parent-owned Editable no-insert combo of rain_interval::presetsHMM clock strings;
     *   raw custom text allowed, parsing deferred to model. No immediate model mutation.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed editor from this delegate; wrong/null behavior described below.
     * @param[in] index Source cell, EditRole read, expected stable model/index.
     * @note Combo only; set raw EditRole string as current text, no H:MM
     *   validation/canonicalization or signal blocker. No source write/undo here.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed matching editor; delegate never deletes it.
     * @param[in] model Required mutable destination model for accepted editor path, unchecked null.
     * @param[in] index Destination cell forwarded unchanged; valid source/context must survive any
     *   compound/modal work.
     * @note Combo only, submit currentText RAW/untrimmed; model setter parses/accepts H:MM,
     *   delegate neither parses nor converts seconds. Uses EditRole, ignores setData result, no
     *   local engine/undo transaction or error dialog; model owns notifications/validation.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

/*! File-path editor — a QLineEdit plus a "…" button that opens a
 *  QFileDialog with the column's name filter (ColumnSpec::fileFilter).
 *  The cell value is the plain path string; typing and clearing stay
 *  possible, Browse merely fills the line edit. First user: rain gage
 *  Rain File (path), multi-column series files (spec §4 task 4). */
class FileBrowseDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner; delegate does not own source model or externally
     *   bound engine objects.
     * @brief Store delegate configuration; GUI-thread editor/model operations only, no editor
     *   opened or engine write at construction.
     * @param[in] nameFilter Moved raw QFileDialog name filter, also used to classify persistent
     *   FileOpenDialog type.
     */
    FileBrowseDelegate(QObject *parent, QString nameFilter);

    /**
     * @param[in] parent Optional QWidget owner for returned editor; caller/view owns parentless
     *   return.
     * @param[in] opt Unused style option.
     * @param[in] index EditRole/optionsRole read only for compound/file-column dispatch; otherwise
     *   unused.
     * @return New parent-owned Owned composite line edit/browse button; chooser fills path only,
     *   actual model write on later commit. No immediate model mutation.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed editor from this delegate; wrong/null behavior described below.
     * @param[in] index Source cell, EditRole read, expected stable model/index.
     * @note Null composite ignored; nonnull editor MUST actually be FileBrowseEditor because
     *   static_cast is unchecked. Fill raw EditRole string into line edit, no path validation. No
     *   source write/undo here.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed matching editor; delegate never deletes it.
     * @param[in] model Required mutable destination model for accepted editor path, unchecked null.
     * @param[in] index Destination cell forwarded unchanged; valid source/context must survive any
     *   compound/modal work.
     * @note Null ignored; nonnull must be actual FileBrowseEditor (unchecked static_cast). Submit
     *   trimmed line text, including empty to clear. No file existence/readability/relative-path
     *   resolution. Uses EditRole, ignores setData result, no local engine/undo transaction or
     *   error dialog; model owns notifications/validation.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

private:
    QString m_nameFilter;
};

/*! Per-row column picker — an editable QComboBox whose items are supplied
 *  by the model through a custom role (the row's data-file header names;
 *  see kFileColumnOptionsRole in swmmattributetablemodel.h). Editable so a
 *  column can still be named when the file is currently unreadable. First
 *  user: rain gage Rain File Column. */
class FileColumnDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner; delegate does not own source model or externally
     *   bound engine objects.
     * @brief Store delegate configuration; GUI-thread editor/model operations only, no editor
     *   opened or engine write at construction.
     * @param[in] optionsRole Custom per-cell model role whose string list seeds editable
     *   file-column combo.
     */
    FileColumnDelegate(QObject *parent, int optionsRole);

    /**
     * @param[in] parent Optional QWidget owner for returned editor; caller/view owns parentless
     *   return.
     * @param[in] opt Unused style option.
     * @param[in] index EditRole/optionsRole read only for compound/file-column dispatch; otherwise
     *   unused.
     * @return New parent-owned Editable no-insert combo seeded ONCE from
     *   index.data(optionsRole).toStringList; raw text allowed even unknown/unreadable source file.
     *   No immediate model mutation.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed editor from this delegate; wrong/null behavior described below.
     * @param[in] index Source cell, EditRole read, expected stable model/index.
     * @note Combo only; set raw EditRole string as current text even if absent from offered file
     *   headers, no trimming/validation. No source write/undo here.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed matching editor; delegate never deletes it.
     * @param[in] model Required mutable destination model for accepted editor path, unchecked null.
     * @param[in] index Destination cell forwarded unchanged; valid source/context must survive any
     *   compound/modal work.
     * @note Combo only, submit trimmed current text, including empty/unknown header. Actual column
     *   validation left to model. Uses EditRole, ignores setData result, no local engine/undo
     *   transaction or error dialog; model owns notifications/validation.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

private:
    int m_optionsRole;
};

} // namespace openswmmvis

#endif // ATTRIBUTEDELEGATES_H
