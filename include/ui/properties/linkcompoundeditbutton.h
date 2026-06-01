/*!
 * \file   linkcompoundeditbutton.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice SC.1 — Custom cell editor widget for compound link attributes
 * (XSection / CulvertCode / InletUsage).
 *
 * Registered with `QPropertyItemDelegate::registerCustomTypeEditorCreator`
 * against the `LinkCompoundEditRef` metatype; QPropertyModel instantiates
 * this widget when the user enters the value cell of any compound row.
 * Mirrors `NodeCompoundEditButton` line-for-line.
 */

#ifndef LINKCOMPOUNDEDITBUTTON_H
#define LINKCOMPOUNDEDITBUTTON_H

#include <QWidget>

#include "ui/properties/linkcompoundeditref.h"

class QPushButton;

class LinkCompoundEditButton : public QWidget
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  edit round-trip (`setEditorData` / `setModelData`). */
    Q_PROPERTY(LinkCompoundEditRef value
                   READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    explicit LinkCompoundEditButton(QWidget *parent = nullptr);

    [[nodiscard]] LinkCompoundEditRef value() const noexcept { return m_ref; }
    void setValue(const LinkCompoundEditRef &ref);

signals:
    /*! Emitted after the dialog closes with a (possibly) updated
     *  summary string. The QPropertyItemDelegate listens via the
     *  `notify` signal of the USER property and commits the new value
     *  back to the model. */
    void valueChanged();

private slots:
    void onClicked();

private:
    void refreshLabel();

    LinkCompoundEditRef m_ref;
    QPushButton        *m_btn = nullptr;
};

#endif // LINKCOMPOUNDEDITBUTTON_H
