/**
 * @file      linkcompoundeditbutton.cpp
 * @brief     Custom cell editor widget for compound link attributes (XSection / InletUsage).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/linkcompoundeditbutton.h"

#include "ui/dialogs/linkcompoundeditdialog.h"
#include "layers/swmmmodellayer.h"
#include <openswmm/engine/openswmm_links.h>

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
    m_owner = ref.layer;
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
    if (!m_ref.engine || m_ref.linkName.isEmpty())
        return;
    if (m_ref.layer && (!m_owner || m_owner->engine() != m_ref.engine))
        return;

    int type = -1;
    swmm_link_get_type(m_ref.engine, swmm_link_index(m_ref.engine, m_ref.linkName.toUtf8().constData()), &type);
    if (m_ref.layer && (m_ref.kind == LinkCompoundEditRef::InletUsage ||
        type == SWMM_LINK_ORIFICE || type == SWMM_LINK_WEIR)) {
        auto *dialog = new LinkCompoundEditDialog(m_ref, window());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        return;
    }
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
