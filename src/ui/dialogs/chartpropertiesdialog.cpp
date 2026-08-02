/*!
 * \file   chartpropertiesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/chartpropertiesdialog.h"

#include "plot/chartproperties.h"

#include <qpropertyitemdelegate.h>
#include <qpropertymodel.h>

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QTreeView>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using openswmmvis::plot::ChartProperties;

ChartPropertiesDialog::ChartPropertiesDialog(ChartProperties *props,
                                              QWidget *parent)
    : QDialog(parent), m_props(props)
{
    setWindowTitle(tr("Chart Properties"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("ChartPropertiesDialog"));
    setWindowFlags(windowFlags() | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(360, 460);

    if (m_props) m_props->setParent(this);

    auto *root = new QVBoxLayout(this);

    m_tree = new QTreeView(this);
    m_tree->setAlternatingRowColors(true);
    m_tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setRootIsDecorated(false);
    m_tree->setItemsExpandable(false);

    auto *pm = new QPropertyModel(m_props.data(), this);
    m_tree->setModel(pm);
    m_tree->setItemDelegate(new QPropertyItemDelegate(m_tree));
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->expandAll();

    root->addWidget(m_tree, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(bb);

    // Close the dialog automatically if the underlying chart disappears.
    if (m_props && m_props->chart()) {
        connect(m_props->chart(), &QObject::destroyed,
                this, &QDialog::close);
    }
}

} // namespace openswmmvis::ui
