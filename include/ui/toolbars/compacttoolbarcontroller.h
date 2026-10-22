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

class QMainWindow;
class QTabBar;
class QToolBar;

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

    /*! Register a tab. \a toolbars are shown side-by-side (in list order)
     *  while the tab is current. \a contextual tabs start hidden — reveal
     *  with setTabVisible(). Call before finalize().
     * @param[in] id Stable identity copied; uniqueness/nonempty not validated, lookups use first
     *   duplicate.
     * @param[in] title Display caption copied.
     * @param[in] toolbars Borrowed toolbar pointers in display order, null entries skipped during
     *   mount/visibility; must outlive controller.
     * @param[in] contextual True tab is hidden during finalize, not immediately on add.
     * @note Must precede finalize (debug assertion only). Appends metadata/tab without mounting
     *   toolbars or changing their QObject parents. Shared toolbar in multiple tabs is unsupported
     *   in practice: visibility loop later entries can override earlier.
     */
    void addTab(const QString &id, const QString &title, const QList<QToolBar *> &toolbars,
                bool contextual = false);

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
     *   assignments, last tab wins. Does not mount/reparent toolbars or guard raw deletion.
     *   GUI-thread only.
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
    QList<Tab> mTabs;
    QList<QToolBar *> mOwnRowBars;
    bool mFinalized = false;
};

} // namespace openswmmvis::ui

#endif // COMPACTTOOLBARCONTROLLER_H
