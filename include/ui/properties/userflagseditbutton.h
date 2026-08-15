/*!
 * \file   userflagseditbutton.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — custom cell editor
 * widget for the per-object "User Flags" row. Mirrors
 * NodeCompoundEditButton: registered with QPropertyItemDelegate against
 * the UserFlagsEditRef metatype; shows a "summary — Edit…" button that
 * opens UserFlagValuesDialog and propagates the refreshed summary back
 * to the model via the USER property.
 */

#ifndef USERFLAGSEDITBUTTON_H
#define USERFLAGSEDITBUTTON_H

#include <QWidget>

#include "ui/properties/userflagseditref.h"

class QPushButton;

class UserFlagsEditButton : public QWidget
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  edit round-trip (`setEditorData` / `setModelData`). */
    Q_PROPERTY(UserFlagsEditRef value
                   READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    explicit UserFlagsEditButton(QWidget *parent = nullptr);

    [[nodiscard]] UserFlagsEditRef value() const noexcept { return m_ref; }
    void setValue(const UserFlagsEditRef &ref);

signals:
    /*! Emitted after the dialog closes with a (possibly) updated
     *  summary; the delegate commits the new value to the model. */
    void valueChanged();

private slots:
    void onClicked();

private:
    void refreshLabel();

    UserFlagsEditRef m_ref;
    QPushButton     *m_btn = nullptr;
};

#endif // USERFLAGSEDITBUTTON_H
