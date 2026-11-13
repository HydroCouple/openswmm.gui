/**
 * @file      dialoglayoutwatcher.cpp
 * @brief     UI redesign iteration 2 (D1) — app-wide automatic dialog layout persistence.
 *
 * UI redesign iteration 2 (D1) — automatic dialog layout persistence.
 * See dialoglayoutwatcher.h for the design contract.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/dialoglayoutwatcher.h"

#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QCalendarWidget>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QTableView>
#include <QTextEdit>
#include <QTreeView>

#include "ui/dialogs/dialoglayoutpersistence.h"

namespace openswmmvis::ui
{

namespace
{

/**
 * @param[in] watched Optional borrowed QObject, no ownership transfer.
 * @return Named top-level QDialog with noLayoutPersistence property false, else null. Does not
 *   exclude modal/popup types or require current visibility, no Qt-internal root-name check or
 *   mutation.
 */
QDialog *persistableDialog_(QObject *watched)
{
    auto *dlg = qobject_cast<QDialog *>(watched);
    if (!dlg || !dlg->isWindow())
        return nullptr;
    if (dlg->objectName().isEmpty())
        return nullptr; // unnamed = implicitly opted out
    if (dlg->property(kNoLayoutPersistenceProp).toBool())
        return nullptr;
    return dlg;
}

/*! Title text without the modified marker or mnemonic ampersands ("&&" stays "&"). */
QString plainTitle(QString text)
{
    text.remove(QStringLiteral("[*]"));
    text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    text.remove(QLatin1Char('&'));
    text.replace(QStringLiteral("\x01"), QStringLiteral("&"));
    return text.trimmed();
}

/*! The field name a "Name:" label gives, or empty when the label is not a plain field label. */
QString fieldLabelText(const QLabel *label)
{
    if (label->textFormat() == Qt::RichText || Qt::mightBeRichText(label->text()))
        return {};
    QString text = label->text().trimmed();
    if (!text.endsWith(QLatin1Char(':')))
        return {};
    text.chop(1);
    return plainTitle(text);
}

bool isField(const QWidget *w)
{
    return qobject_cast<const QLineEdit *>(w) || qobject_cast<const QAbstractSpinBox *>(w) ||
           qobject_cast<const QComboBox *>(w) || qobject_cast<const QTextEdit *>(w) ||
           qobject_cast<const QPlainTextEdit *>(w) || qobject_cast<const QAbstractSlider *>(w) ||
           qobject_cast<const QAbstractItemView *>(w);
}

/*! The layout, at any depth under \p layout, that holds \p w directly. */
QLayout *layoutHolding(QLayout *layout, QWidget *w)
{
    if (!layout)
        return nullptr;
    if (layout->indexOf(w) >= 0)
        return layout;
    for (int i = 0; i < layout->count(); ++i)
        if (QLayout *found = layoutHolding(layout->itemAt(i)->layout(), w))
            return found;
    return nullptr;
}

/*! The field right after \p label: the next widget in its row or column, the next cell of its
 *  grid row, or the field of its form row; null when that is not a field. */
QWidget *fieldAfter(QLabel *label)
{
    QWidget *parent = label->parentWidget();
    QLayout *layout = parent ? layoutHolding(parent->layout(), label) : nullptr;
    if (!layout)
        return nullptr;
    const int index = layout->indexOf(label);
    QWidget *next = nullptr;
    if (auto *grid = qobject_cast<QGridLayout *>(layout)) {
        int row = 0, column = 0, rowSpan = 0, columnSpan = 0;
        grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
        if (QLayoutItem *item = grid->itemAtPosition(row, column + columnSpan))
            next = item->widget();
    } else if (auto *form = qobject_cast<QFormLayout *>(layout)) {
        int row = 0;
        QFormLayout::ItemRole role = QFormLayout::FieldRole;
        form->getItemPosition(index, &row, &role);
        if (role == QFormLayout::LabelRole)
            if (QLayoutItem *item = form->itemAt(row, QFormLayout::FieldRole))
                next = item->widget();
    } else {
        for (int i = index + 1; i < layout->count() && !next; ++i) {
            QLayoutItem *item = layout->itemAt(i);
            if (item->widget())
                next = item->widget();
            else if (!item->spacerItem())
                break; // a nested layout: the label does not introduce it
        }
    }
    return next && isField(next) ? next : nullptr;
}

/*! True when \p w belongs to \p root itself, not to a nested dialog, dock or a Qt-internal
 *  composite (a combo box's popup, a calendar's grid). */
bool ownedBy(QWidget *root, QWidget *w)
{
    for (QWidget *p = w->parentWidget(); p && p != root; p = p->parentWidget()) {
        if (qobject_cast<QComboBox *>(p) || qobject_cast<QCalendarWidget *>(p) ||
            qobject_cast<QDialog *>(p) || qobject_cast<QDockWidget *>(p))
            return false;
    }
    return true;
}

QString viewKind(const QAbstractItemView *view)
{
    if (qobject_cast<const QTableView *>(view))
        return QObject::tr("table");
    if (qobject_cast<const QTreeView *>(view))
        return QObject::tr("tree");
    return QObject::tr("list");
}

/*! "<base> <kind>", without repeating a kind the base already ends with. */
QString withKind(const QString &base, const QString &kind)
{
    if (base.isEmpty())
        return kind.left(1).toUpper() + kind.mid(1);
    if (base.endsWith(kind, Qt::CaseInsensitive))
        return base;
    return QObject::tr("%1 %2").arg(base, kind);
}

} // namespace

void applyAccessibilityDefaults(QWidget *root)
{
    if (!root)
        return;
    const QString context = plainTitle(root->windowTitle());
    QHash<QWidget *, QString> labelled; // field -> the text of the label that introduces it
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (!ownedBy(root, label))
            continue;
        const QString text = fieldLabelText(label);
        if (text.isEmpty())
            continue;
        QWidget *field = label->buddy();
        if (!field) {
            field = fieldAfter(label);
            if (!field)
                continue;
            label->setBuddy(field);
        }
        labelled.insert(field, text);
        if (field->accessibleName().isEmpty())
            field->setAccessibleName(text);
    }
    for (QAbstractItemView *view : root->findChildren<QAbstractItemView *>()) {
        if (!view->accessibleName().isEmpty() || view->objectName().startsWith(QLatin1String("qt_")) ||
            !ownedBy(root, view))
            continue;
        if (labelled.contains(view))
            continue; // named from its label above
        QString base;
        for (QWidget *p = view->parentWidget(); p && p != root && base.isEmpty();
             p = p->parentWidget())
            if (auto *group = qobject_cast<QGroupBox *>(p))
                base = plainTitle(group->title());
        view->setAccessibleName(withKind(base.isEmpty() ? context : base, viewKind(view)));
    }
}

DialogLayoutWatcher::DialogLayoutWatcher(QObject *parent) : QObject(parent) {}

bool DialogLayoutWatcher::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Show:
        // Every top-level dialog and dock panel, named or not: once per instance.
        if (auto *w = qobject_cast<QWidget *>(watched);
            w && !w->property(kAccessibilityDefaultsProp).toBool() &&
            ((qobject_cast<QDialog *>(w) && w->isWindow()) || qobject_cast<QDockWidget *>(w))) {
            w->setProperty(kAccessibilityDefaultsProp, true);
            applyAccessibilityDefaults(w);
        }
        if (QDialog *dlg = persistableDialog_(watched)) {
            // Once per instance: re-showing a live modeless dialog must
            // not snap it back to the stored layout.
            if (!dlg->property(kLayoutRestoredOnceProp).toBool()) {
                dlg->setProperty(kLayoutRestoredOnceProp, true);
                // Synchronous, pre-map: no flicker, and it runs after
                // the ctor's resize() so saved geometry wins.
                restoreDialogLayout(dlg);
            }
        }
        break;
    case QEvent::Hide:
    case QEvent::Close:
        // During destruction the QDialog part is already gone by the
        // time the QWidget hide runs, so the cast fails — an accepted
        // miss; every ordinary path (done(), exec() return, close(),
        // hide()) still passes through here as a live QDialog.
        if (QDialog *dlg = persistableDialog_(watched))
            saveDialogLayout(dlg);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace openswmmvis::ui
