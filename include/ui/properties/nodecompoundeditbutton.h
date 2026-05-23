/*!
 * \file   nodecompoundeditbutton.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DB.2 — Custom cell editor widget for compound node attributes
 * (Inflows / DWF / RDII / Treatment).
 *
 * Registered with `QPropertyItemDelegate::registerCustomTypeEditorCreator`
 * against the `NodeCompoundEditRef` metatype; QPropertyModel instantiates
 * this widget when the user enters the value cell of any compound row.
 * The widget shows a single `Edit…` button labelled with the current
 * `summary`; clicking opens `NodeCompoundEditDialog` and (when the user
 * commits a change) propagates the refreshed summary back to the model
 * via the `value` `Q_PROPERTY` (USER true).
 */

#ifndef NODECOMPOUNDEDITBUTTON_H
#define NODECOMPOUNDEDITBUTTON_H

#include <QWidget>

#include "ui/properties/nodecompoundeditref.h"

class QPushButton;

class NodeCompoundEditButton : public QWidget
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  edit round-trip (`setEditorData` / `setModelData`). */
    Q_PROPERTY(NodeCompoundEditRef value
                   READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    explicit NodeCompoundEditButton(QWidget *parent = nullptr);

    [[nodiscard]] NodeCompoundEditRef value() const noexcept { return m_ref; }
    void setValue(const NodeCompoundEditRef &ref);

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

    NodeCompoundEditRef m_ref;
    QPushButton        *m_btn = nullptr;
};

#endif // NODECOMPOUNDEDITBUTTON_H
