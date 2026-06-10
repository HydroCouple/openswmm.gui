/*!
 * \file   userflagseditbutton.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/properties/userflagseditbutton.h"

#include "ui/dialogs/userflagvaluesdialog.h"

#include <QHBoxLayout>
#include <QPushButton>

UserFlagsEditButton::UserFlagsEditButton(QWidget *parent)
    : QWidget(parent), m_btn(new QPushButton(this))
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_btn);
    setFocusProxy(m_btn);

    connect(m_btn, &QPushButton::clicked, this, &UserFlagsEditButton::onClicked);
    refreshLabel();
}

void UserFlagsEditButton::setValue(const UserFlagsEditRef &ref)
{
    m_ref = ref;
    refreshLabel();
    // Nothing to edit without a bound store or before flags exist.
    m_btn->setEnabled(m_ref.model != nullptr && !m_ref.objectName.isEmpty());
}

void UserFlagsEditButton::refreshLabel()
{
    if (m_ref.summary.isEmpty())
        m_btn->setText(tr("Edit…"));
    else
        m_btn->setText(tr("%1 — Edit…").arg(m_ref.summary));
}

void UserFlagsEditButton::onClicked()
{
    if (!m_ref.model || m_ref.objectName.isEmpty()) return;

    UserFlagValuesDialog dlg(m_ref, this);
    dlg.exec();

    // Pull the recomputed summary back regardless of accept/cancel so
    // the cell never shows a stale count.
    m_ref.summary = dlg.updatedSummary();
    refreshLabel();
    emit valueChanged();
}
