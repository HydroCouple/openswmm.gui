/** @file toolboxpanel.cpp
 * @brief Searchable plugin/toolset tree with intersecting graphical tag filters.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "ui/panels/toolboxpanel.h"
#include <QAction>
#include <QCheckBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>

namespace openswmmvis::ui
{
namespace
{
constexpr int IdentityRole = Qt::UserRole + 1;
constexpr int ToolRole = Qt::UserRole + 2;
QString entryKey(const QString &plugin, const QString &tool) { return plugin + "/" + tool; }
QString tagTitle(QString tag)
{
    if (tag == "usgs")
        return QStringLiteral("USGS");
    tag.replace('-', ' ');
    if (!tag.isEmpty())
        tag[0] = tag[0].toUpper();
    return tag;
}
}
ToolboxPanel::ToolboxPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("analysisToolboxPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    mTabs = new QTabWidget(this);
    layout->addWidget(mTabs);
    auto *tools = new QWidget;
    mTabs->addTab(tools, tr("Tools"));
    auto *body = new QVBoxLayout(tools);
    auto *row = new QHBoxLayout;
    mSearch = new QLineEdit(tools);
    mSearch->setObjectName("toolboxSearch");
    mSearch->setPlaceholderText(tr("Search tools…"));
    mSearch->setAccessibleName(tr("Search tools"));
    mSearch->setClearButtonEnabled(true);
    row->addWidget(mSearch);
    auto *tags = new QToolButton(tools);
    tags->setText(tr("Tags"));
    tags->setPopupMode(QToolButton::InstantPopup);
    mTags = new QMenu(tags);
    auto *tagBody = new QWidget;
    auto *tagBodyLayout = new QVBoxLayout(tagBody);
    mTagSearch = new QLineEdit(tagBody);
    mTagSearch->setPlaceholderText(tr("Find tags…"));
    mTagSearch->setAccessibleName(tr("Find tags"));
    tagBodyLayout->addWidget(mTagSearch);
    auto *tagList = new QWidget;
    mTagLayout = new QVBoxLayout(tagList);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(tagList);
    scroll->setMinimumSize(220, 220);
    tagBodyLayout->addWidget(scroll);
    auto *action = new QWidgetAction(mTags);
    action->setDefaultWidget(tagBody);
    mTags->addAction(action);
    tags->setMenu(mTags);
    row->addWidget(tags);
    auto *clear = new QPushButton(tr("Clear filters"), tools);
    connect(clear, &QPushButton::clicked, this, [this] {
        mSearch->clear();
        setSelectedTags({});
    });
    row->addWidget(clear);
    body->addLayout(row);
    mChips = new QWidget(tools);
    auto *chipsLayout = new QHBoxLayout(mChips);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    body->addWidget(mChips);
    mTree = new QTreeView(tools);
    mTree->setObjectName("treeViewAnalysisToolbox");
    mTree->setHeaderHidden(true);
    mTree->setAccessibleName(tr("Analysis tools"));
    mTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTree->setUniformRowHeights(true);
    mModel = new QStandardItemModel(this);
    mTree->setModel(mModel);
    body->addWidget(mTree, 1);
    mCount = new QLabel(tools);
    body->addWidget(mCount);
    mDescription = new QLabel(tools);
    mDescription->setTextFormat(Qt::PlainText);
    mDescription->setWordWrap(true);
    body->addWidget(mDescription);
    connect(mSearch, &QLineEdit::textChanged, this, &ToolboxPanel::filter);
    connect(mTagSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (auto *check : mTags->findChildren<QCheckBox *>())
            check->setVisible(check->text().contains(text, Qt::CaseInsensitive));
    });
    connect(mTree, &QTreeView::activated, this, [this](const QModelIndex &index) {
        const auto key = index.data(ToolRole).toString();
        auto action = mEntries.value(key).action;
        if (action && action->isEnabled() && action->isVisible())
            action->trigger();
    });
    connect(mTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &index) {
        const auto entry = mEntries.value(index.data(ToolRole).toString());
        mDescription->setText(entry.spec.description +
                             (entry.spec.tags.isEmpty() ? QString{} :
                                  "\n" + entry.spec.tags.join(", ")));
    });
    filter();
}
void ToolboxPanel::addTool(const QString &pluginId, const OpenSWMM::Sdk::ToolSpec &spec,
                           QAction *action)
{
    removeTool(pluginId, spec.id);
    const auto key = entryKey(pluginId, spec.id);
    Entry entry{pluginId, spec, action, {}, {}};
    if (action) {
        entry.changed = connect(action, &QAction::changed, this, &ToolboxPanel::requestRebuild);
        entry.destroyed = connect(action, &QObject::destroyed, this,
                                  [this, pluginId, id = spec.id] { removeTool(pluginId, id); });
    }
    mEntries.insert(key, entry);
    requestRebuild();
}
void ToolboxPanel::removeTool(const QString &pluginId, const QString &toolId)
{
    const auto key = entryKey(pluginId, toolId);
    if (!mEntries.contains(key))
        return;
    const auto entry = mEntries.take(key);
    disconnect(entry.changed);
    disconnect(entry.destroyed);
    requestRebuild();
}
QSet<QString> ToolboxPanel::expandedIds() const
{
    QSet<QString> ids;
    std::function<void(QStandardItem *)> visit = [&](QStandardItem *item) {
        for (int row = 0; row < item->rowCount(); ++row) {
            auto *child = item->child(row);
            if (mTree->isExpanded(child->index()))
                ids.insert(child->data(IdentityRole).toString());
            visit(child);
        }
    };
    visit(mModel->invisibleRootItem());
    return ids;
}
void ToolboxPanel::restoreExpanded(const QSet<QString> &ids)
{
    std::function<void(QStandardItem *)> visit = [&](QStandardItem *item) {
        for (int row = 0; row < item->rowCount(); ++row) {
            auto *child = item->child(row);
            mTree->setExpanded(child->index(), ids.contains(child->data(IdentityRole).toString()));
            visit(child);
        }
    };
    visit(mModel->invisibleRootItem());
}
void ToolboxPanel::requestRebuild()
{
    if (mRebuildScheduled)
        return;
    mRebuildScheduled = true;
    QTimer::singleShot(0, this, [this] {
        mRebuildScheduled = false;
        rebuild();
    });
}
void ToolboxPanel::rebuild()
{
    const auto expanded = expandedIds();
    const auto selected = mTree->currentIndex().data(IdentityRole).toString();
    mModel->clear();
    QHash<QString, QStandardItem *> folders;
    auto keys = mEntries.keys();
    std::sort(keys.begin(), keys.end());
    for (const auto &key : keys) {
        const auto &entry = mEntries[key];
        auto path = entry.pluginId;
        auto *parent = mModel->invisibleRootItem();
        auto folder = [&](const QString &id, const QString &title) {
            if (!folders.contains(id)) {
                auto *item = new QStandardItem(title);
                item->setData(id, IdentityRole);
                parent->appendRow(item);
                folders.insert(id, item);
            }
            parent = folders.value(id);
        };
        folder(path, entry.spec.toolboxTitle.isEmpty() ? entry.pluginId : entry.spec.toolboxTitle);
        for (const auto &set : entry.spec.toolsets) {
            path += "/" + set.id;
            folder(path, set.title);
        }
        auto *item = new QStandardItem(entry.action ? entry.action->icon() : QIcon{},
                                      entry.action ? entry.action->text() : entry.spec.id);
        item->setData(key, ToolRole);
        item->setData("tool/" + key, IdentityRole);
        item->setToolTip(entry.spec.description);
        item->setEnabled(entry.action && entry.action->isEnabled());
        parent->appendRow(item);
        if (item->data(IdentityRole).toString() == selected)
            mTree->setCurrentIndex(item->index());
    }
    restoreExpanded(expanded);
    filter();
}
bool ToolboxPanel::matches(const Entry &entry, const QStringList &tags) const
{
    if (!entry.action || !entry.action->isVisible())
        return false;
    for (const auto &tag : tags)
        if (!entry.spec.tags.contains(tag))
            return false;
    QString text = entry.action->text() + " " + entry.spec.description + " " +
                   entry.spec.tags.join(" ") + " " + entry.spec.toolboxTitle + " " + entry.pluginId;
    for (const auto &set : entry.spec.toolsets)
        text += " " + set.title;
    const auto terms = mSearch->text().simplified().split(' ', Qt::SkipEmptyParts);
    for (const auto &term : terms)
        if (!text.contains(term, Qt::CaseInsensitive))
            return false;
    return true;
}
void ToolboxPanel::filter()
{
    const bool filtering = !mSearch->text().trimmed().isEmpty() || !mSelectedTags.isEmpty();
    if (filtering && !mFiltering) {
        mExpandedBeforeFilter = expandedIds();
        mSelectedBeforeFilter = mTree->currentIndex().data(IdentityRole).toString();
    }
    std::function<bool(QStandardItem *)> visit = [&](QStandardItem *item) {
        bool any = false;
        for (int row = 0; row < item->rowCount(); ++row) {
            auto *child = item->child(row);
            const auto key = child->data(ToolRole).toString();
            const bool visible = key.isEmpty() ? visit(child) :
                                                    matches(mEntries.value(key), mSelectedTags);
            mTree->setRowHidden(row, item->index(), !visible);
            any |= visible;
            if (filtering && key.isEmpty() && visible)
                mTree->expand(child->index());
            if (!filtering && mFiltering &&
                child->data(IdentityRole).toString() == mSelectedBeforeFilter)
                mTree->setCurrentIndex(child->index());
        }
        return any;
    };
    visit(mModel->invisibleRootItem());
    if (!filtering && mFiltering)
        restoreExpanded(mExpandedBeforeFilter);
    mFiltering = filtering;
    const int count = visibleToolCount();
    mCount->setText(mEntries.isEmpty() ? tr("Enable a plugin in Tools → Plugins to add tools.") :
                       count == 0 ? tr("No matching tools. Clear filters to browse all tools.") :
                       tr("%1 of %2 tools").arg(count).arg(mEntries.size()));
    auto *chips = mChips->layout();
    while (auto *item = chips->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    for (const auto &tag : mSelectedTags) {
        auto *chip = new QPushButton(tagTitle(tag) + " ×", mChips);
        chip->setAccessibleName(tr("Remove tag %1").arg(tagTitle(tag)));
        connect(chip, &QPushButton::clicked, this, [this, tag] {
            auto tags = mSelectedTags;
            tags.removeAll(tag);
            setSelectedTags(tags);
        });
        chips->addWidget(chip);
    }
    mChips->setVisible(!mSelectedTags.isEmpty());
    QSet<QString> allTags;
    for (const auto &entry : std::as_const(mEntries))
        for (const auto &tag : entry.spec.tags)
            allTags.insert(tag);
    for (const auto &tag : mSelectedTags)
        allTags.insert(tag);
    // Reuse live checkboxes so toggling a tag keeps the popup and keyboard focus.
    QHash<QString, QCheckBox *> checks;
    for (auto *check : mTags->findChildren<QCheckBox *>())
        checks.insert(check->property("tag").toString(), check);
    auto sortedTags = allTags.values();
    std::sort(sortedTags.begin(), sortedTags.end());
    for (const auto &tag : sortedTags) {
        auto *check = checks.take(tag);
        if (!check) {
            check = new QCheckBox;
            check->setProperty("tag", tag);
            mTagLayout->addWidget(check);
            connect(check, &QCheckBox::toggled, this, [this, tag](bool selected) {
                auto tags = mSelectedTags;
                if (selected)
                    tags.append(tag);
                else
                    tags.removeAll(tag);
                setSelectedTags(tags);
            });
        }
        auto tags = mSelectedTags;
        if (!tags.contains(tag))
            tags.append(tag);
        int matchesCount = 0;
        for (const auto &entry : std::as_const(mEntries))
            matchesCount += matches(entry, tags);
        const QSignalBlocker blocker(check);
        check->setChecked(mSelectedTags.contains(tag));
        check->setText(tr("%1 (%2)").arg(tagTitle(tag)).arg(matchesCount));
        check->setVisible(check->text().contains(mTagSearch->text(), Qt::CaseInsensitive));
    }
    qDeleteAll(checks);
}
void ToolboxPanel::setSearchText(const QString &text) { mSearch->setText(text); }
void ToolboxPanel::setSelectedTags(const QStringList &tags)
{
    mSelectedTags = tags;
    mSelectedTags.removeDuplicates();
    filter();
}
int ToolboxPanel::visibleToolCount() const
{
    int count = 0;
    for (const auto &entry : mEntries)
        count += matches(entry, mSelectedTags);
    return count;
}
void ToolboxPanel::setHistoryWidget(QWidget *widget)
{
    if (widget && mTabs->count() == 1)
        mTabs->addTab(widget, tr("History"));
    else
        delete widget;
}
ToolboxPanel *ensureToolboxPanel(QMainWindow *window)
{
    if (!window)
        return nullptr;
    if (auto *panel = window->findChild<ToolboxPanel *>("analysisToolboxPanel"))
        return panel;
    auto *dock = window->findChild<QDockWidget *>("dockWidgetAnalysisToolBox");
    if (!dock) {
        dock = new QDockWidget(QObject::tr("Analysis Toolbox"), window);
        dock->setObjectName("dockWidgetAnalysisToolBox");
        window->addDockWidget(Qt::RightDockWidgetArea, dock);
    }
    auto *panel = new ToolboxPanel(dock);
    auto *old = dock->widget();
    dock->setWidget(panel);
    delete old;
    return panel;
}
}
