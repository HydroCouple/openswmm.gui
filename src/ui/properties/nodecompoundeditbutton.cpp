/*!
 * \file   nodecompoundeditbutton.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/nodecompoundeditbutton.h"

#include "ui/dialogs/nodecompoundeditdialog.h"

#include <QHBoxLayout>
#include <QPushButton>

NodeCompoundEditButton::NodeCompoundEditButton(QWidget *parent)
    : QWidget(parent), m_btn(new QPushButton(this))
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_btn);
    setFocusProxy(m_btn);

    connect(m_btn, &QPushButton::clicked, this, &NodeCompoundEditButton::onClicked);
    refreshLabel();
}

void NodeCompoundEditButton::setValue(const NodeCompoundEditRef &ref)
{
    m_ref = ref;
    refreshLabel();
}

void NodeCompoundEditButton::refreshLabel()
{
    // Pattern matches the modern "summary — Edit…" affordance used by
    // QGIS-style attribute editors: the summary stays informative while
    // the trailing "Edit…" tells the user what the click will do.
    if (m_ref.summary.isEmpty())
        m_btn->setText(tr("Edit…"));
    else
        m_btn->setText(tr("%1 — Edit…").arg(m_ref.summary));
}

void NodeCompoundEditButton::onClicked()
{
    if (!m_ref.engine || m_ref.nodeName.isEmpty()) return;

    NodeCompoundEditDialog dlg(m_ref, this);
    dlg.exec();

    // Always pull the dialog's updated summary back: even on Cancel,
    // the user may have added rows (Inflows / DWF / RDII pages commit
    // immediately on Add). The dialog tracks the live summary in its
    // m_ref and exposes it via `updatedSummary()`.
    m_ref.summary = dlg.updatedSummary();
    refreshLabel();
    emit valueChanged();
}
