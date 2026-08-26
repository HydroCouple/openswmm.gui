/*!
 * \file   initialqualityeditbutton.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Initial-quality UI round — custom cell editor widget for the
 * per-element "Initial Quality" row. Mirrors UserFlagsEditButton:
 * registered with QPropertyItemDelegate against the
 * InitialQualityEditRef metatype; shows a "summary — Edit…" button
 * that opens InitialQualityDialog scoped to the element and
 * propagates the refreshed summary back to the model via the USER
 * property.
 */

#ifndef INITIALQUALITYEDITBUTTON_H
#define INITIALQUALITYEDITBUTTON_H

#include <QWidget>

#include "ui/properties/initialqualityeditref.h"

class QPushButton;

class InitialQualityEditButton : public QWidget
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  edit round-trip (`setEditorData` / `setModelData`). */
    Q_PROPERTY(InitialQualityEditRef value
                   READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    explicit InitialQualityEditButton(QWidget *parent = nullptr);

    [[nodiscard]] InitialQualityEditRef value() const noexcept { return m_ref; }
    void setValue(const InitialQualityEditRef &ref);

signals:
    /*! Emitted after the dialog closes with a (possibly) updated
     *  summary; the delegate commits the new value to the model. */
    void valueChanged();

private slots:
    void onClicked();

private:
    void refreshLabel();

    InitialQualityEditRef m_ref;
    QPushButton          *m_btn = nullptr;
};

#endif // INITIALQUALITYEDITBUTTON_H
