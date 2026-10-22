/**
 * @file      datetimedelegate.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Calendar-popup date-time editor for cells whose EditRole is a
 *         QDateTime.
 *
 * Qt's default delegate does hand a QDateTimeEdit to a QDateTime cell, but it
 * arrives carrying the system locale's short format (`8/14/26 5:06 AM`) and no
 * calendar. This one edits in `core::swmmDateTimeDisplayFormat()` —
 * `MM/dd/yyyy HH:mm`, the same stamp a SWMM `.inp` carries — with a calendar
 * popup.
 *
 * Deliberately standalone rather than another class in `attributedelegates.h`:
 * that header's delegates drag in the whole compound-editor / data-object-
 * picker chain, which any target installing just this one would then have to
 * link. Pure Qt plus the header-only datetime helper.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DELEGATES_DATETIMEDELEGATE_H
#define OPENSWMMVIS_UI_DELEGATES_DATETIMEDELEGATE_H

#include <QStyledItemDelegate>

namespace openswmmvis::ui
{

/*! \brief QDateTimeEdit-in-a-cell, formatted `MM/dd/yyyy HH:mm`.
 *
 *  Seconds are absent from the format but NOT discarded. The editor is seeded
 *  with the model's full QDateTime and a QDateTimeEdit preserves the sections
 *  its display format omits, so a stamp at 00:15:30 survives an edit of its
 *  minute field. SWMM stores whole seconds and the engine's `[TIMESERIES]`
 *  writer emits `HH:MM:SS`, so dropping them here would be the same class of
 *  silent truncation as GH #1. */
class DateTimeDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner; view does not automatically own a parentless
     *   delegate.
     * @brief Create GUI-thread styled delegate without opening an editor or reading a model.
     */
    explicit DateTimeDelegate(QObject *parent = nullptr);

    /**
     * @param[in] parent Optional QWidget owner for returned editor; caller/view must manage
     *   lifetime if null.
     * @param[in] opt Unused style option.
     * @param[in] index Unused model index, no validity/type check.
     * @return New parent-owned QDateTimeEdit pinned to UTC. Editable SWMM wall-clock format with
     *   calendar popup; no initial model data until setEditorData. No model mutation.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed editor; non-QDateTimeEdit delegates to base.
     * @param[in] index Borrowed source index; Qt::EditRole converted to QDateTime.
     * @note Valid timestamp seeds full editor value including hidden seconds; invalid data leaves
     *   existing editor unchanged. UTC editor expects UTC-labelled SWMM wall clock; helper does not
     *   relabel source itself. No engine/model write.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed QDateTimeEdit; other types use styled base implementation.
     * @param[in] model Required mutable destination model, unchecked for date editor path.
     * @param[in] index Destination index, passed through unchanged.
     * @note Submit editor full dateTime through Qt::EditRole, preserving seconds stored in editor.
     *   Ignores model setData success/failure and does not call interpretText, enforce engine date
     *   range, create undo or emit custom signals; model owns those responsibilities.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

/*! \brief Read-only `yyyy-MM-dd HH:mm:ss` display of a simulation time, for
 *  persistent editors in status views (the Simulation Status panel).
 *
 *  SWMM times are timezone-naive wall clocks labelled UTC. A QDateTimeEdit in
 *  the default local zone CONVERTS such a value on setDateTime() in Qt 6, so
 *  the panel showed every time shifted by the machine's UTC offset (9 hours
 *  for the tutorial reporter). This editor is pinned to UTC. */
class ReadOnlyDateTimeDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner; view does not automatically own a parentless
     *   delegate.
     * @brief Create GUI-thread styled delegate without opening an editor or reading a model.
     */
    explicit ReadOnlyDateTimeDelegate(QObject *parent = nullptr);

    /**
     * @param[in] parent Optional QWidget owner for returned editor; caller/view must manage
     *   lifetime if null.
     * @param[in] opt Unused style option.
     * @param[in] index Unused model index, no validity/type check.
     * @return New parent-owned QDateTimeEdit pinned to UTC. Read-only centred yyyy-MM-dd HH:mm:ss,
     *   no frame/buttons/calendar/focus and transparent to mouse; no initial model data. No model
     *   mutation.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed editor; null/non-QDateTimeEdit ignored.
     * @param[in] index Borrowed source index; Qt::DisplayRole must convert to valid QDateTime.
     * @note Valid timestamp seeds full editor value including hidden seconds; invalid data leaves
     *   existing editor unchanged. UTC editor expects UTC-labelled SWMM wall clock; helper does not
     *   relabel source itself. No engine/model write.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @brief Ignore commit requests completely; all unnamed editor/model/index arguments are
     *   unused. No model write, validation or notification.
     */
    void setModelData(QWidget *, QAbstractItemModel *, const QModelIndex &) const override {}
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DELEGATES_DATETIMEDELEGATE_H
