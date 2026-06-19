/*!
 * \file   subcatchcompoundeditbutton.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/subcatchcompoundeditbutton.h"

#include "ui/dialogs/subcatchcompoundeditdialog.h"

#include <QHBoxLayout>
#include <QPushButton>

SubcatchCompoundEditButton::SubcatchCompoundEditButton(QWidget *parent)
    : QWidget(parent), m_btn(new QPushButton(this))
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_btn);
    setFocusProxy(m_btn);

    connect(m_btn, &QPushButton::clicked, this, &SubcatchCompoundEditButton::onClicked);
    refreshLabel();
}

void SubcatchCompoundEditButton::setValue(const SubcatchCompoundEditRef &ref)
{
    m_ref = ref;
    refreshLabel();
}

void SubcatchCompoundEditButton::refreshLabel()
{
    if (m_ref.summary.isEmpty())
        m_btn->setText(tr("Edit…"));
    else
        m_btn->setText(tr("%1 — Edit…").arg(m_ref.summary));
}

void SubcatchCompoundEditButton::onClicked()
{
    if (!m_ref.engine || m_ref.subName.isEmpty()) return;

    SubcatchCompoundEditDialog dlg(m_ref, this);
    dlg.exec();

    // Pages commit immediately on Add/Apply, so pull the live summary back
    // even on Close.
    m_ref.summary = dlg.updatedSummary();
    refreshLabel();
    emit valueChanged();
}
