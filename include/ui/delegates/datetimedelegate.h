/*!
 * \file   datetimedelegate.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Calendar-popup date-time editor for cells whose EditRole is a
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
 */
#ifndef OPENSWMMVIS_UI_DELEGATES_DATETIMEDELEGATE_H
#define OPENSWMMVIS_UI_DELEGATES_DATETIMEDELEGATE_H

#include <QStyledItemDelegate>

namespace openswmmvis::ui {

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
    explicit DateTimeDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DELEGATES_DATETIMEDELEGATE_H
