/**
 * @file      initialqualityeditbutton.cpp
 * @brief     Initial-quality UI round — custom cell editor widget for the per-element "Initial
 *            Quality" row.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/properties/initialqualityeditbutton.h"
#include "ui/dialogs/inheritedpropertiesdialog.h"
#include "layers/swmmmodellayer.h"

#include "ui/dialogs/initialqualitydialog.h"

#include <QHBoxLayout>
#include <QPushButton>

InitialQualityEditButton::InitialQualityEditButton(QWidget *parent)
    : QWidget(parent), m_btn(new QPushButton(this))
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_btn);
    setFocusProxy(m_btn);

    connect(m_btn, &QPushButton::clicked, this, &InitialQualityEditButton::onClicked);
    refreshLabel();
}

void InitialQualityEditButton::setValue(const InitialQualityEditRef &ref)
{
    m_ref = ref;
    m_owner = ref.layer;
    refreshLabel();
    // Nothing to edit without an engine or before the element exists.
    m_btn->setEnabled(m_ref.engine != nullptr && !m_ref.elementName.isEmpty());
}

void InitialQualityEditButton::refreshLabel()
{
    if (m_ref.summary.isEmpty())
        m_btn->setText(tr("Edit…"));
    else
        m_btn->setText(tr("%1 — Edit…").arg(m_ref.summary));
}

void InitialQualityEditButton::onClicked()
{
    if (m_ref.layer) {
        if (!m_owner || m_owner->engine() != m_ref.engine) return;
        openInheritedPropertiesDialog(m_owner, {{m_ref.isLink ? SWMMObjectRef::Link : SWMMObjectRef::Node, m_ref.elementName}}, true, window());
        return;
    }

    if (!m_ref.engine || m_ref.elementName.isEmpty())
        return;

    OpenSWMMVis::InitialQualityDialog dlg(m_ref.engine, this);
    dlg.setElementScope(m_ref.isLink, m_ref.elementName);
    dlg.exec();

    // Pull the recomputed summary back regardless of accept/cancel so
    // the cell never shows a stale count.
    m_ref.summary = initialQualitySummaryFor(m_ref.engine, m_ref.isLink, m_ref.elementName);
    refreshLabel();
    if (dlg.wroteAnyChanges()) {
        m_ref.wroteChanges = true;
        emit valueChanged();
    }
}
