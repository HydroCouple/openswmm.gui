/**
 * @file      pluginsdialog.cpp
 * @brief     Tools → Plugins.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/pluginsdialog.h"

#include "plugins/filefilterregistry.h"
#include "plugins/extensionmanager.h"
#include <openswmm/engine/openswmm_process_components.h>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSignalBlocker>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

PluginsDialog::PluginsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Plugins"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("PluginsDialog"));
    resize(1000, 560);
    buildUi();
    populate();

    connect(openswmmvis::FileFilterRegistry::instance(),
            &openswmmvis::FileFilterRegistry::entriesChanged, this, &PluginsDialog::populate);
    connect(openswmmvis::ExtensionManager::instance(), &openswmmvis::ExtensionManager::changed,
            this, &PluginsDialog::populate);
    connect(m_tree, &QTreeWidget::itemChanged, this, [](QTreeWidgetItem *item, int column) {
        const auto id = item->data(0, Qt::UserRole).toString();
        if (column == 0 && !id.isEmpty())
            openswmmvis::ExtensionManager::instance()->setEnabled(id, item->checkState(0) == Qt::Checked);
    });
}

void PluginsDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);

    auto *header = new QLabel(tr("Engine file-format plugins, process components, and GUI extensions. "
                                 "GUI extension changes take effect after restarting SWMMVis. "
                                 "Start with --no-extensions or hold Shift at launch for safe mode."),
                              this);
    header->setWordWrap(true);
    outer->addWidget(header);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName("pluginsTree");
    m_tree->setColumnCount(7);
    m_tree->setHeaderLabels({tr("Name"), tr("Version / patterns"), tr("Vendor / plugin"),
        tr("License / R/W"), tr("Built against"), tr("Status"), tr("Details")});
    m_tree->setRootIsDecorated(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->setSortingEnabled(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_tree->setColumnWidth(0, 240);
    m_tree->setColumnWidth(1, 160);
    m_tree->setColumnWidth(2, 140);
    outer->addWidget(m_tree, 1);

    auto *bb = new QDialogButtonBox(this);
    auto *rescan = bb->addButton(tr("Rescan"), QDialogButtonBox::ActionRole);
    auto *add = bb->addButton(tr("Add directory…"), QDialogButtonBox::ActionRole);
    auto *folder = bb->addButton(tr("Open plugin folder"), QDialogButtonBox::ActionRole);
    bb->addButton(QDialogButtonBox::Close);
    outer->addWidget(bb);

    connect(rescan, &QPushButton::clicked, this, &PluginsDialog::onRescan);
    connect(add, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getExistingDirectory(this, tr("GUI extension directory"));
        if (!path.isEmpty())
            openswmmvis::ExtensionManager::instance()->addDirectory(path);
    });
    connect(folder, &QPushButton::clicked, this, []() {
        const auto path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath("plugins");
        QDir().mkpath(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
}

void PluginsDialog::onRescan()
{
    openswmmvis::FileFilterRegistry::instance()->rescan();
    openswmmvis::ExtensionManager::instance()->rescan();
    // populate() runs via the entriesChanged signal, but trigger explicitly
    // for the no-op case where no entries change.
    populate();
}

void PluginsDialog::populate()
{
    if (!m_tree)
        return;
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();
    auto *ioRoot = new QTreeWidgetItem(m_tree, {tr("Engine I/O plugins")});
    ioRoot->setExpanded(true);

    using openswmmvis::FileFilterRegistry;
    using openswmmvis::FilterKind;

    auto *reg = FileFilterRegistry::instance();
    const auto entries = reg->allEntries();

    QHash<FilterKind, QTreeWidgetItem *> kindItems;
    auto kindRoot = [&](FilterKind k) -> QTreeWidgetItem * {
        auto it = kindItems.find(k);
        if (it != kindItems.end())
            return it.value();
        auto *parent = new QTreeWidgetItem(ioRoot);
        parent->setText(0, tr(FileFilterRegistry::kindLabel(k)));
        parent->setFirstColumnSpanned(true);
        parent->setExpanded(true);
        kindItems.insert(k, parent);
        return parent;
    };

    for (const auto &e : entries) {
        auto *parent = kindRoot(e.kind);
        auto *child = new QTreeWidgetItem(parent);
        child->setText(0, e.description);
        child->setText(1, e.patterns.join(QLatin1String(" ")));
        child->setText(2, e.pluginId.isEmpty() ? tr("(built-in)") : e.pluginId);
        QString rw;
        if (e.canRead)
            rw += QLatin1Char('R');
        if (e.canWrite)
            rw += QLatin1Char('W');
        child->setText(3, rw);
        if (!e.enabled) {
            for (int c = 0; c < m_tree->columnCount(); ++c)
                child->setForeground(c, palette().color(QPalette::Disabled, QPalette::Text));
        }
    }
    auto *processRoot = new QTreeWidgetItem(m_tree, {tr("Process components")});
    processRoot->setExpanded(true);
    for (int i = 0; i < swmm_process_component_known_count(); ++i) {
        char id[256]{}, description[1024]{};
        int implemented = 0;
        if (swmm_process_component_known_get(i, id, sizeof(id), description, sizeof(description),
                                             &implemented) != SWMM_OK)
            continue;
        auto *item = new QTreeWidgetItem(processRoot);
        item->setText(0, QString::fromUtf8(id));
        item->setText(5, implemented ? tr("Available") : tr("Not implemented"));
        item->setText(6, QString::fromUtf8(description));
    }
    auto *guiRoot = new QTreeWidgetItem(m_tree, {tr("GUI extensions")});
    guiRoot->setExpanded(true);
    for (const auto &record : openswmmvis::ExtensionManager::instance()->records()) {
        auto *item = new QTreeWidgetItem(guiRoot);
        item->setText(0, record.info.caption.isEmpty() ? record.info.id : record.info.caption);
        item->setData(0, Qt::UserRole, record.info.id);
        if (!record.info.id.isEmpty()) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, record.enabled ? Qt::Checked : Qt::Unchecked);
        }
        item->setText(1, record.info.version);
        item->setText(2, record.info.vendor);
        item->setText(3, record.info.licenseType);
        item->setText(4, tr("SDK %1, Qt %2, %3").arg(record.builtAgainst.sdkApiVersion,
            record.builtAgainst.qtVersion, record.builtAgainst.arch));
        item->setText(5, record.state);
        item->setText(6, record.error.isEmpty() ? record.warning : record.error);
        item->setToolTip(0, record.path);
        item->setToolTip(6, record.error + "\n" + record.warning);
        for (const auto &child : record.children)
            new QTreeWidgetItem(item, {child.caption, child.version, child.vendor, child.licenseType});
    }
}
