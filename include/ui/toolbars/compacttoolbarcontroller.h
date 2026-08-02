#ifndef COMPACTTOOLBARCONTROLLER_H
#define COMPACTTOOLBARCONTROLLER_H

/*!
 * \file compacttoolbarcontroller.h
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
 */

#include <QList>
#include <QObject>
#include <QString>

class QMainWindow;
class QTabBar;
class QToolBar;

namespace openswmmvis::ui {

class CompactToolbarController : public QObject
{
    Q_OBJECT

public:
    explicit CompactToolbarController(QMainWindow *window);

    /*! Register a tab. \a toolbars are shown side-by-side (in list order)
     *  while the tab is current. \a contextual tabs start hidden — reveal
     *  with setTabVisible(). Call before finalize(). */
    void addTab(const QString &id, const QString &title,
                const QList<QToolBar *> &toolbars, bool contextual = false);

    /*! Mount the strip + rows on the main window's top toolbar area and
     *  activate the persisted (or first visible) tab. */
    void finalize();

    void setTabVisible(const QString &id, bool visible);
    void setCurrentTab(const QString &id);
    QString currentTab() const;
    bool isTabVisible(const QString &id) const;
    QStringList tabIds() const;
    QList<QToolBar *> toolbarsForTab(const QString &id) const;

    /*! The strip row itself — for corner additions (command palette). */
    QToolBar *stripToolBar() const { return mStrip; }

signals:
    void currentTabChanged(const QString &id);

private:
    int indexOf(const QString &id) const;
    void applyVisibility();

    struct Tab {
        QString id;
        QString title;
        QList<QToolBar *> bars;
        bool contextual = false;
    };

    QMainWindow *mWindow = nullptr;
    QToolBar    *mStrip  = nullptr;
    QTabBar     *mTabBar = nullptr;
    QList<Tab>   mTabs;
    bool         mFinalized = false;
};

}   // namespace openswmmvis::ui

#endif // COMPACTTOOLBARCONTROLLER_H
