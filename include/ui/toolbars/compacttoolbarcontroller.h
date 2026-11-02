#ifndef COMPACTTOOLBARCONTROLLER_H
#define COMPACTTOOLBARCONTROLLER_H

/**
 * @file      compacttoolbarcontroller.h
 * @brief     UI redesign P6 — the tabbed compact toolbar: a fixed tab-strip row (QTabBar inside a
 *            locked QToolBar) over one visible tool row.
 *
 * UI redesign P6 — the tabbed compact toolbar: a fixed tab-strip row
 * (QTabBar inside a locked QToolBar) over one visible tool row. Tabs
 * swap which QToolBar(s) are shown; nothing is re-parented — the
 * widget-heavy toolbars (animation scrubber, analysis combos, terrain
 * and mesh controls) are adopted wholesale and only hidden/shown, so
 * their embedded widgets and insertWidget anchors never move.
 *
 * QMainWindow::saveState() keeps working because every row is a real
 * QToolBar with a stable objectName; the last active tab persists under
 * SWMMVis::MainWindow/CompactToolbarTab.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QList>
#include <QObject>
#include <QString>

class QAction;
class QMainWindow;
class QTabBar;
class QToolBar;
class QWidget;

namespace openswmmvis::ui
{

class CompactToolbarController : public QObject
{
    Q_OBJECT

public:
    /**
     * @param[in] window Expected live main window; becomes QObject parent/owner. Null permits
     *   construction but finalize no-ops.
     * @brief Create locked tab-strip toolbar, tab bar/spacer and deferred tab-change persistence
     *   wiring.
     * @note GUI-thread only, registered toolbars remain externally owned by their
     *   widget/main-window parents. Controller stores raw pointers and must not outlive them.
     *   CurrentChanged is ignored until finalized; then visibility applies, active ID persists
     *   under SWMMVis::MainWindow/CompactToolbarTab and currentTabChanged emits.
     */
    explicit CompactToolbarController(QMainWindow *window);

    /*! Register a tab before or after finalize(). \a toolbars are shown side-by-side
     *  while the tab is current. \a contextual tabs start hidden after mounting.
     * @param[in] id Stable nonempty identity copied; duplicate and empty identities are ignored.
     * @param[in] title Display caption copied.
     * @param[in] toolbars Borrowed toolbar pointers in display order, null entries skipped during
     *   mount/visibility; must outlive controller.
     * @param[in] contextual True starts hidden when the tab is mounted; reveal with setTabVisible().
     * @note Before finalize(), only metadata/tab entries are appended. After finalize(), the bars
     *   are mounted immediately and visibility is refreshed, allowing runtime extension contributions.
     *   Toolbar ownership remains with the caller. Do not share a toolbar between tabs.
     */
    void addTab(const QString &id, const QString &title, const QList<QToolBar *> &toolbars,
                bool contextual = false);

    /** @brief Remove a registered tab and detach its toolbars from the main window.
     * @param[in] id Exact stable tab identity; unknown identities are ignored.
     * @note Toolbar ownership remains with the caller. Current-tab removal selects another
     * visible Qt tab and refreshes toolbar visibility. GUI-thread only. */
    void removeTab(const QString &id);

    /** @brief Append an independently owned toolbar to an existing tab.
     * @param[in] id Exact existing tab identity.
     * @param[in] toolbar Borrowed toolbar, unique to this tab while registered.
     * @return True after mounting; false for missing tabs, null/already assigned bars
     * or reentrant removal. The controller never takes ownership. */
    bool addToolbarToTab(const QString &id, QToolBar *toolbar);

    /** @brief Detach one toolbar without removing other contributors or the tab.
     * @param[in] id Exact existing tab identity.
     * @param[in] toolbar Previously assigned toolbar, retained by its owner.
     * @return True after removing its membership, false for an unknown membership. */
    bool removeToolbarFromTab(const QString &id, QToolBar *toolbar);

    /*! Mark \a bar (already registered via addTab) to occupy its own
     *  full row BELOW the shared group row while its tab is current —
     *  for the widget-heavy Terrain / Mesh Editing bars, which would
     *  otherwise starve the ribbon groups sharing their row. Call before
     *  finalize(). A row whose bars are all hidden costs no height.
     * @param[in] bar Optional borrowed previously registered toolbar; null/repeated ignored,
     *   membership not verified.
     * @note Must precede finalize (assertion). Appends row designation only, no immediate
     *   mount/layout. All designated bars share one extra row despite original own-row wording.
     */
    void setOwnRow(QToolBar *bar);

    /*! Mount the strip + rows on the main window's top toolbar area and
     *  activate the persisted (or first visible) tab.
     * @note Idempotent after success; null window no-op. Mounts strip/shared bars/own-row bars into
     *   top area using toolbar breaks; locks bar movement. Hides contextual tabs, then marks
     *   finalized, restores persisted visible ID or first visible tab and applies visibility/strip
     *   layout. If all tabs hidden none selected for display. Initial unchanged selection applies
     *   visibility without currentTabChanged/persistence callback. Does not preserve previous
     *   placement/dock area of adopted toolbars.
     */
    void finalize();

    /**
     * @param[in] id Stable tab ID, first match; unknown no-op.
     * @param[in] visible Desired QTabBar visibility.
     * @note Equality no-op. Qt may auto-select neighbouring visible tab and emit current-change
     *   callback; explicit visibility/layout application covers noncurrent change. No selection
     *   history or contextual-state persistence. Can be called before finalize, but finalize
     *   rehides contextual tabs.
     */
    void setTabVisible(const QString &id, bool visible);
    /**
     * @param[in] id First matching visible tab ID; unknown/hidden ignored.
     * @note Qt current-index transition drives finalized visibility/persistence/signal. Same
     *   selected tab no callback; pre-finalize changes only index. No model edits.
     */
    void setCurrentTab(const QString &id);
    /**
     * @return Copied current ID or empty for invalid/no index, even if controller not finalized.
     */
    QString currentTab() const;
    /**
     * @param[in] id Tab identity, first matching registration.
     * @return QTabBar stored visibility, false for unknown. Does not require toolbar widgets/window
     *   effectively visible.
     */
    bool isTabVisible(const QString &id) const;
    /**
     * @return Copied registration-order IDs, including hidden/contextual/duplicates.
     */
    QStringList tabIds() const;
    /**
     * @param[in] id Tab identity, first match.
     * @return Copied list of borrowed pointers, including nulls, or empty if unknown. No lifetime
     *   pin.
     */
    QList<QToolBar *> toolbarsForTab(const QString &id) const;

    /*! The strip row itself — for corner additions (command palette).
     * @return Borrowed strip toolbar owned by window (or caller/widget ownership rules if null
     *   window), no ownership transfer.
     */
    QToolBar *stripToolBar() const { return mStrip; }

    /*! Mount \a widget at the leading edge of the tab strip, before the tab
     *  bar — the Quick Access cluster that stays visible on every tab.
     * @param[in] widget Live widget; the strip adopts it. Null is ignored. Call once.
     * @note Inserts a separator between the widget and the tab bar and re-lays the strip.
     */
    void setLeadingWidget(QWidget *widget);

signals:
    /**
     * @param[in] id New current registered tab identity after visibility application/settings
     *   write.
     * @note Emitted only on valid QTabBar currentChanged when finalized; visibility changes can
     *   indirectly emit, finalize with same current index may not. No layer/tool change is implied.
     */
    void currentTabChanged(const QString &id);

private:
    /**
     * @param[in] id Case-sensitive identity to scan.
     * @return First registration index, or -1 when absent; no mutation.
     */
    int indexOf(const QString &id) const;
    /**
     * @brief Set each registered live toolbar visible only for current AND visible tab.
     * @note Null entries skipped; same pointer registered multiple times receives repeated
     *   assignments, last tab wins. Snapshots guarded toolbar pointers and stops an
     *   obsolete pass when a native callback changes visibility again. Does not mount
     *   or reparent toolbars. GUI-thread only.
     */
    void applyVisibility();
    /**
     * @brief Refresh tab-bar hint and synchronously invalidate/set strip layout geometry to current
     *   contents rect.
     * @note Missing strip/layout no-op; prevents stale scroll-arrow width after hiding/revealing
     *   tabs. No persistence or selection change.
     */
    void relayoutStrip();

    struct Tab {
        QString id;
        QString title;
        QList<QToolBar *> bars;
        bool contextual = false;
    };

    QMainWindow *mWindow = nullptr;
    QToolBar *mStrip = nullptr;
    QTabBar *mTabBar = nullptr;
    QAction *mTabBarAction = nullptr; ///< Strip slot of the tab bar; leading widgets go before it.
    QList<Tab> mTabs;
    QList<QToolBar *> mOwnRowBars;
    bool mFinalized = false;
    quint64 mVisibilityRevision = 0; ///< Prevent stale writes after synchronous tab changes.
};

} // namespace openswmmvis::ui

#endif // COMPACTTOOLBARCONTROLLER_H
