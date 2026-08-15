/*!
 * \file   pluginsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/pluginsdialog.h"

#include "plugins/filefilterregistry.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

PluginsDialog::PluginsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Plugins"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("PluginsDialog"));
    resize(640, 480);
    buildUi();
    populate();

    connect(openswmmvis::FileFilterRegistry::instance(),
            &openswmmvis::FileFilterRegistry::entriesChanged,
            this, &PluginsDialog::populate);
}

void PluginsDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);

    auto *header = new QLabel(
        tr("File-format filters available to Open / Save dialogs. Built-in "
           "filters are bundled with SWMMVis; engine plugins are discovered "
           "from the running engine. Enable / disable and user-installed "
           "plugins arrive in a follow-up slice."),
        this);
    header->setWordWrap(true);
    outer->addWidget(header);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({
        tr("Description"),
        tr("Patterns"),
        tr("Plugin"),
        tr("R/W")
    });
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->setColumnWidth(0, 240);
    m_tree->setColumnWidth(1, 160);
    m_tree->setColumnWidth(2, 140);
    outer->addWidget(m_tree, 1);

    auto *bb = new QDialogButtonBox(this);
    auto *rescan = bb->addButton(tr("Rescan"), QDialogButtonBox::ActionRole);
    bb->addButton(QDialogButtonBox::Close);
    outer->addWidget(bb);

    connect(rescan, &QPushButton::clicked, this, &PluginsDialog::onRescan);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
}

void PluginsDialog::onRescan()
{
    openswmmvis::FileFilterRegistry::instance()->rescan();
    // populate() runs via the entriesChanged signal, but trigger explicitly
    // for the no-op case where no entries change.
    populate();
}

void PluginsDialog::populate()
{
    if (!m_tree) return;
    m_tree->clear();

    using openswmmvis::FileFilterRegistry;
    using openswmmvis::FilterKind;

    auto *reg = FileFilterRegistry::instance();
    const auto entries = reg->allEntries();

    QHash<FilterKind, QTreeWidgetItem *> kindItems;
    auto kindRoot = [&](FilterKind k) -> QTreeWidgetItem * {
        auto it = kindItems.find(k);
        if (it != kindItems.end()) return it.value();
        auto *parent = new QTreeWidgetItem(m_tree);
        parent->setText(0, tr(FileFilterRegistry::kindLabel(k)));
        parent->setFirstColumnSpanned(true);
        parent->setExpanded(true);
        kindItems.insert(k, parent);
        return parent;
    };

    for (const auto &e : entries)
    {
        auto *parent = kindRoot(e.kind);
        auto *child = new QTreeWidgetItem(parent);
        child->setText(0, e.description);
        child->setText(1, e.patterns.join(QLatin1String(" ")));
        child->setText(2, e.pluginId.isEmpty() ? tr("(built-in)") : e.pluginId);
        QString rw;
        if (e.canRead)  rw += QLatin1Char('R');
        if (e.canWrite) rw += QLatin1Char('W');
        child->setText(3, rw);
        if (!e.enabled)
        {
            for (int c = 0; c < m_tree->columnCount(); ++c)
                child->setForeground(c, palette().color(QPalette::Disabled, QPalette::Text));
        }
    }
}
