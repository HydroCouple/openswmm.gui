/*!
 * \file   linkcompoundeditbutton.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/linkcompoundeditbutton.h"

#include "ui/dialogs/linkcompoundeditdialog.h"

#include <QHBoxLayout>
#include <QPushButton>

LinkCompoundEditButton::LinkCompoundEditButton(QWidget *parent)
    : QWidget(parent), m_btn(new QPushButton(this))
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_btn);
    setFocusProxy(m_btn);

    connect(m_btn, &QPushButton::clicked, this, &LinkCompoundEditButton::onClicked);
    refreshLabel();
}

void LinkCompoundEditButton::setValue(const LinkCompoundEditRef &ref)
{
    m_ref = ref;
    refreshLabel();
}

void LinkCompoundEditButton::refreshLabel()
{
    if (m_ref.summary.isEmpty())
        m_btn->setText(tr("Edit…"));
    else
        m_btn->setText(tr("%1 — Edit…").arg(m_ref.summary));
}

void LinkCompoundEditButton::onClicked()
{
    if (!m_ref.engine || m_ref.linkName.isEmpty()) return;

    LinkCompoundEditDialog dlg(m_ref, this);
    dlg.exec();

    // Always pull the dialog's updated summary back: even on Cancel,
    // applied-as-you-go pages (XSection) commit immediately. The dialog
    // tracks the live summary in its m_ref and exposes it via
    // `updatedSummary()`.
    m_ref.summary = dlg.updatedSummary();
    refreshLabel();
    emit valueChanged();
}
