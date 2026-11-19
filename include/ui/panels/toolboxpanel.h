/** @file toolboxpanel.h
 * @brief Window-owned tool catalog, tree and graphical tag filters.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IToolbox.hpp"
#include <QPointer>
#include <QSet>
#include <QHash>
#include <QWidget>
class QAction;
class QLineEdit;
class QTreeView;
class QStandardItemModel;
class QStandardItem;
class QSortFilterProxyModel;
class QLabel;
class QMenu;
class QTabWidget;
class QMainWindow;
class QVBoxLayout;
namespace openswmmvis::ui
{
/** @brief One shared catalog backed by registered actions, never by constructed tool widgets. */
class ToolboxPanel final : public QWidget
{
    Q_OBJECT
public:
    /** @brief Construct an empty metadata catalog and its search/filter controls.
     * @param[in] parent Widget owner. */
    explicit ToolboxPanel(QWidget *parent = nullptr);
    /** @brief Add copied metadata and track the live action presentation.
     * @param[in] pluginId Validated plugin namespace.
     * @param[in] spec Owning tool metadata already validated by the host.
     * @param[in] action Borrowed launch command whose lifetime guards the entry. */
    void addTool(const QString &pluginId, const OpenSWMM::Sdk::ToolSpec &spec, QAction *action);
    /** @brief Remove one exact owner/tool pair and prune empty folders.
     * @param[in] pluginId Plugin namespace.
     * @param[in] toolId Local tool identity. */
    void removeTool(const QString &pluginId, const QString &toolId);
    /** @brief Set literal case-insensitive search across captions, path, description and tags.
     * @param[in] text Space-separated terms which all must match. */
    void setSearchText(const QString &text);
    /** @brief Select the tag intersection; empty accepts every tool.
     * @param[in] tags Canonical tag keys which all must match. */
    void setSelectedTags(const QStringList &tags);
    /** @brief Count current matches without requiring expanded tree folders.
     * @return Number of entries matching text, tags and action visibility. */
    int visibleToolCount() const;
    /** @brief Count registered metadata entries.
     * @return Number of tools across all plugin namespaces. */
    int toolCount() const { return mEntries.size(); }
    /** @brief Add a shared run-history page to the toolbox.
     * @param[in] widget Newly allocated history projection transferring to this panel. */
    void setHistoryWidget(QWidget *widget);
private:
    struct Entry {
        QString pluginId;
        OpenSWMM::Sdk::ToolSpec spec;
        QPointer<QAction> action;
        QMetaObject::Connection changed;
        QMetaObject::Connection destroyed;
    };
    /** @brief Coalesce catalog changes before rebuilding the tree on the next GUI turn. */
    void requestRebuild();
    void rebuild();
    void filter();
    bool matches(const Entry &entry, const QStringList &tags) const;
    QSet<QString> expandedIds() const;
    void restoreExpanded(const QSet<QString> &ids);
    QHash<QString, Entry> mEntries;
    QLineEdit *mSearch;
    QTreeView *mTree;
    QStandardItemModel *mModel;
    QLabel *mDescription;
    QLabel *mCount;
    QMenu *mTags;
    QLineEdit *mTagSearch;
    QVBoxLayout *mTagLayout;
    QWidget *mChips;
    QTabWidget *mTabs;
    QStringList mSelectedTags;
    QSet<QString> mExpandedBeforeFilter;
    QString mSelectedBeforeFilter;
    bool mFiltering = false;
    bool mRebuildScheduled = false; ///< Batch large plugin registrations into one tree update.
};
/** @brief Reuse the designer dock and keep its objectName stable for Qt layout restore.
 * @param[in] window Main window owning the Analysis Toolbox dock.
 * @return Borrowed unique toolbox panel, or null without a main window. */
ToolboxPanel *ensureToolboxPanel(QMainWindow *window);
}
