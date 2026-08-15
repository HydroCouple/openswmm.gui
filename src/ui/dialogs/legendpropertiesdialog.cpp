/*!
 * \file   legendpropertiesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/legendpropertiesdialog.h"

#include "render/legendoverlaystyle.h"

#include <qpropertymodel.h>

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using OpenSWMM::Render::LegendOverlayStyle;

LegendPropertiesDialog::LegendPropertiesDialog(LegendOverlayStyle *style, QWidget *parent)
    : QDialog(parent), m_style(style)
{
    setWindowTitle(tr("Legend Properties"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("LegendPropertiesDialog"));
    setWindowFlags(windowFlags() | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(420, 520);

    // Snapshot for Cancel — captured before any edits flow through.
    if (m_style) m_snapshot = m_style->toJson();

    auto *root = new QVBoxLayout(this);

    m_tree = new QTreeView(this);
    m_tree->setAlternatingRowColors(true);
    m_tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setRootIsDecorated(false);
    m_tree->setItemsExpandable(false);

    auto *pm = new QPropertyModel(m_style.data(), this);
    m_tree->setModel(pm);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->expandAll();

    root->addWidget(m_tree, 1);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Reset | QDialogButtonBox::Close,
        this);
    bb->button(QDialogButtonBox::Close)->setDefault(true);
    bb->button(QDialogButtonBox::Reset)->setToolTip(tr("Restore all fields to their built-in defaults"));
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);   // Close == accept current state.
    connect(bb->button(QDialogButtonBox::Reset), &QPushButton::clicked,
            this, &LegendPropertiesDialog::onResetClicked);
    root->addWidget(bb);

    // Live preview: as the property model writes through to the style's
    // setters, the style emits changed() → any subscribed view repaints.
    // No additional wiring needed here.

    // Close the dialog automatically if the style disappears.
    if (m_style) {
        connect(m_style.data(), &QObject::destroyed, this, &QDialog::close);
    }
}

void LegendPropertiesDialog::reject()
{
    // Cancel — revert any live-preview edits applied since construction.
    if (m_style) m_style->fromJson(m_snapshot);
    QDialog::reject();
}

void LegendPropertiesDialog::onResetClicked()
{
    if (!m_style) return;
    m_style->resetToDefaults();
    // Force the tree to re-read every cell since resetToDefaults() fires
    // setters that write through QMetaProperty already, but the view
    // may have cached widget state for an open editor.
    if (auto *pm = qobject_cast<QPropertyModel *>(m_tree->model()))
        pm->refreshValues();
}

} // namespace openswmmvis::ui
