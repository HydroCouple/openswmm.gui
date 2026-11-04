/**
 * @file      userflagseditbutton.cpp
 * @brief     Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — custom cell editor widget for the
 *            per-object "User Flags" row.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/properties/userflagseditbutton.h"
#include "ui/dialogs/inheritedpropertiesdialog.h"
#include "layers/swmmmodellayer.h"

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
    m_owner = ref.layer;
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
    if (m_ref.layer) {
        if (!m_owner || !m_owner->engine() || m_owner->engine() != m_ref.engine) return;
        auto targets = m_ref.targets;
        if (targets.isEmpty()) targets.append({m_ref.objectType == "NODE" ? SWMMObjectRef::Node :
            m_ref.objectType == "LINK" ? SWMMObjectRef::Link : m_ref.objectType == "SUBCATCHMENT" ? SWMMObjectRef::Subcatchment : SWMMObjectRef::RainGage, m_ref.objectName});
        openInheritedPropertiesDialog(m_owner, targets, false, window(), m_ref.undoStack);
        return;
    }

    if (!m_ref.model || m_ref.objectName.isEmpty())
        return;

    UserFlagValuesDialog dlg(m_ref, this);
    dlg.exec();

    // Pull the recomputed summary back regardless of accept/cancel so
    // the cell never shows a stale count.
    m_ref.summary = dlg.updatedSummary();
    refreshLabel();
    emit valueChanged();
}
