/*!
 * \file   subcatchcompoundeditbutton.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 3 — custom cell editor widget for compound subcatchment attributes
 * (land-use coverage / groundwater / LID usage). Mirror of
 * NodeCompoundEditButton: shows a single "summary — Edit…" button that opens
 * SubcatchCompoundEditDialog and propagates the refreshed summary back.
 */

#ifndef SUBCATCHCOMPOUNDEDITBUTTON_H
#define SUBCATCHCOMPOUNDEDITBUTTON_H

#include <QWidget>

#include "ui/properties/subcatchcompoundeditref.h"

class QPushButton;

class SubcatchCompoundEditButton : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(SubcatchCompoundEditRef value
                   READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    explicit SubcatchCompoundEditButton(QWidget *parent = nullptr);

    [[nodiscard]] SubcatchCompoundEditRef value() const noexcept { return m_ref; }
    void setValue(const SubcatchCompoundEditRef &ref);

signals:
    void valueChanged();

private slots:
    void onClicked();

private:
    void refreshLabel();

    SubcatchCompoundEditRef m_ref;
    QPushButton            *m_btn = nullptr;
};

#endif // SUBCATCHCOMPOUNDEDITBUTTON_H
