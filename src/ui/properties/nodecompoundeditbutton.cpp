/*!
 * \file   nodecompoundeditbutton.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/nodecompoundeditbutton.h"

#include "layers/gwsourcesummary.h"
#include "ui/dialogs/groundwaterexchangedialog.h"
#include "ui/dialogs/nodecompoundeditdialog.h"
#include "ui/properties/subcatchcompoundeditref.h"

#include <openswmm/engine/openswmm_nodes.h>

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
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

    // Groundwater Sources is navigation, not a page: one source opens its
    // GroundwaterExchangeDialog directly, several pop a chooser, none is a
    // no-op (the button label already reads "(none)").
    if (m_ref.kind == NodeCompoundEditRef::GroundwaterSources) {
        const int nodeIdx = swmm_node_index(m_ref.engine,
                                            m_ref.nodeName.toUtf8().constData());
        const QStringList subs =
            OpenSWMMVis::Groundwater::groundwaterSourceSubcatchments(m_ref.engine, nodeIdx);
        if (subs.isEmpty()) {
            m_btn->setToolTip(tr("No subcatchment discharges groundwater to this node."));
            return;
        }
        QString pick = subs.first();
        if (subs.size() > 1) {
            QMenu menu(this);
            for (const QString &s : subs) menu.addAction(s);
            QAction *act = menu.exec(m_btn->mapToGlobal(m_btn->rect().bottomLeft()));
            if (!act) return;
            pick = act->text();
        }
        SubcatchCompoundEditRef sref;
        sref.engine  = m_ref.engine;
        sref.layer   = m_ref.layer;
        sref.subName = pick;
        sref.kind    = SubcatchCompoundEditRef::Groundwater;
        // Parented to the top-level window: the delegate may destroy this
        // cell editor as soon as the cell loses focus.
        auto *dlg = new GroundwaterExchangeDialog(sref, window());
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        return;
    }

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
