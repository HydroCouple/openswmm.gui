/**
 * @file      datetimedelegate.cpp
 * @brief     QDateTime.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/delegates/datetimedelegate.h"

#include "core/swmmdatetimeformat.h"

#include <QDateTime>
#include <QDateTimeEdit>

namespace openswmmvis::ui
{

DateTimeDelegate::DateTimeDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QWidget *DateTimeDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem & /*opt*/,
                                        const QModelIndex & /*index*/) const
{
    auto *edit = new QDateTimeEdit(parent);
    edit->setDisplayFormat(openswmmvis::core::swmmDateTimeDisplayFormat());
    edit->setCalendarPopup(true);
    // Wall-clock convention: SWMM times are timezone-naive and labelled UTC so
    // no local-time / DST arithmetic is ever applied on the round trip (GH #2,
    // see core/swmmdatetime.h). A default-spec editor would relabel the value
    // LocalTime and shift it on the way back out.
    edit->setTimeSpec(Qt::UTC);
    return edit;
}

void DateTimeDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto *edit = qobject_cast<QDateTimeEdit *>(editor);
    if (!edit) {
        QStyledItemDelegate::setEditorData(editor, index);
        return;
    }
    // Seed with the FULL value, seconds included. They are not shown, but the
    // editor keeps them, so committing an edited minute does not zero them.
    const QDateTime dt = index.data(Qt::EditRole).toDateTime();
    if (dt.isValid())
        edit->setDateTime(dt);
}

void DateTimeDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                    const QModelIndex &index) const
{
    auto *edit = qobject_cast<QDateTimeEdit *>(editor);
    if (!edit) {
        QStyledItemDelegate::setModelData(editor, model, index);
        return;
    }
    model->setData(index, edit->dateTime(), Qt::EditRole);
}

ReadOnlyDateTimeDelegate::ReadOnlyDateTimeDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QWidget *ReadOnlyDateTimeDelegate::createEditor(QWidget *parent,
                                                const QStyleOptionViewItem & /*opt*/,
                                                const QModelIndex & /*index*/) const
{
    auto *w = new QDateTimeEdit(parent);
    w->setTimeSpec(Qt::UTC); // see class comment: never convert to local time
    w->setReadOnly(true);
    w->setButtonSymbols(QAbstractSpinBox::NoButtons);
    w->setFrame(false);
    w->setCalendarPopup(false);
    w->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    w->setFocusPolicy(Qt::NoFocus);
    w->setAlignment(Qt::AlignCenter);
    w->setAttribute(Qt::WA_TransparentForMouseEvents);
    return w;
}

void ReadOnlyDateTimeDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto *w = qobject_cast<QDateTimeEdit *>(editor);
    if (!w)
        return;
    const QVariant v = index.data(Qt::DisplayRole);
    if (v.canConvert<QDateTime>() && v.toDateTime().isValid())
        w->setDateTime(v.toDateTime());
}

} // namespace openswmmvis::ui
