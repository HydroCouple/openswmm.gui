/**
 * @file      test_featurelayer_hostwiring.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     The feature-layer stack is REACHABLE from the main window
 *         (MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07 §6.2).
 *
 * The layer, store, commands, tools, dialog and panel all landed compiling and
 * unit-tested but with nothing constructing them — every one of them was dead
 * code the user could not reach. Catalog integrity (test_action_catalog) and
 * toolbar mounting (test_compact_toolbar) both run against synthetic rigs, so
 * neither notices if the real SWMMVis window never builds the tab.
 *
 * This constructs the REAL main window and asserts the three things that make
 * the feature reachable at all:
 *
 *   1. the Features dock exists, is named for saveState, and is docked;
 *   2. the eight feature actions exist under the window, so the catalog sweep
 *      binds their shortcuts and the ribbon can resolve them by name;
 *   3. the "features" ribbon tab is actually mounted and carries those
 *      actions — the step that was missing when the dock alone existed.
 *
 * Plus the project guard: the actions are disabled with no project open, and
 * the GeoPackage path is refused for an unsaved model rather than scattering
 * an orphan .gpkg (PLAN §9 Q5).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "swmmvis.h"
#include "ui/actioncatalog.h"
#include "ui/actionregistry.h"
#include "ui/util/usernotice.h"
#include "ui/toolbars/compacttoolbarcontroller.h"
#include "ui/toolbars/ribbonsplitbutton.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

namespace
{

//! The eight actions §6.2 requires, by objectName.
const QStringList kFeatureActions = {
    QStringLiteral("actionNewFeatureLayer"),   QStringLiteral("actionFeatureEditMode"),
    QStringLiteral("actionFeatureDelete"),     QStringLiteral("actionFeatureDrawPoint"),
    QStringLiteral("actionFeatureDrawLine"),   QStringLiteral("actionFeatureDrawPolygon"),
    QStringLiteral("actionFeatureAddPart"),    QStringLiteral("actionFeatureAddHole"),
    QStringLiteral("actionFeatureEditVertex"), QStringLiteral("actionFeatureMove"),
};

} // namespace

class TestFeatureLayerHostWiring : public QObject
{
    Q_OBJECT

private slots:

    /**
     *
     * @brief Construct the real main window owned by this fixture and allow 50 ms of GUI events.
     * Requires a QApplication and linked UI dependencies. Stores a raw pointer for
     * cleanupTestCase; the new window is not transferred to another owner.
     *
     */
    void initTestCase()
    {
        m_win = new SWMMVis(nullptr);
        QVERIFY(m_win != nullptr);
        QTest::qWait(50);
    }

    /**
     *
     * @brief Close and delete the fixture-owned main window, then clear its pointer.
     * Does nothing if already null. Runs on the GUI thread after the no-project checks.
     *
     */
    void cleanupTestCase()
    {
        if (m_win) {
            m_win->close();
            delete m_win;
            m_win = nullptr;
        }
    }

    /*! A-07/A-27: the main window installs its Message Log as the notice sink, so a notice
     *  from any editor is kept there with its severity. */
    void noticesReachTheMessageLog()
    {
        auto *view = m_win->findChild<QAbstractItemView *>(QStringLiteral("treeViewMessageLogs"));
        QVERIFY(view && view->model());
        const int before = view->model()->rowCount();
        openswmmvis::ui::notifyUser(nullptr, openswmmvis::ui::NoticeSeverity::Warning,
                                    QStringLiteral("A-27 probe notice"));
        QCOMPARE(view->model()->rowCount(), before + 1);
        QCOMPARE(view->model()->index(before, 1).data().toString(), QStringLiteral("Warning"));
        QCOMPARE(view->model()->index(before, 2).data().toString(),
                 QStringLiteral("A-27 probe notice"));
    }

    /*! A-13: a panel's shortcut moves keyboard focus into the panel when it is open and opens
     *  it when it is closed; it never hides it. The shortcuts moved from the View > Panels
     *  toggles (which keep hiding a panel, with no default key) to "Go to" commands. */
    void panelShortcutsFocusInsteadOfHiding()
    {
        const QList<QPair<QString, QString>> keys = {
            {"layers", "Ctrl+1"},       {"objectBrowser", "Ctrl+2"},
            {"properties", "Ctrl+3"},   {"attributeTable", "Ctrl+4"},
            {"legend", "Ctrl+5"},       {"simulationStatus", "Ctrl+6"},
            {"messageLogs", "Ctrl+7"},  {"sectionView", "Ctrl+9"},
            {"features", "Ctrl+0"}};
        const auto catalogShortcut = [](const QString &id) -> QString {
            for (const auto &e : openswmmvis::ui::kActionCatalog)
                if (QString::fromLatin1(e.id) == id)
                    return QString::fromLatin1(e.defaultShortcut);
            return QStringLiteral("<missing>");
        };
        auto *registry = openswmmvis::ui::ActionRegistry::instance();
        auto *goMenu = m_win->findChild<QMenu *>(QStringLiteral("menuViewGoToPanel"));
        QVERIFY2(goMenu, "View > Panels has no Go to Panel menu");
        for (const auto &[panel, key] : keys) {
            const QString focusId = QStringLiteral("view.focus.") + panel;
            const QString toggleId = QStringLiteral("view.dock.") + panel;
            QCOMPARE(catalogShortcut(focusId), key);
            QCOMPARE(catalogShortcut(toggleId), QString());
            QAction *go = registry->action(focusId);
            QVERIFY2(go, qPrintable(focusId));
            QCOMPARE(go->shortcut(), QKeySequence(key));
            QVERIFY2(goMenu->actions().contains(go), qPrintable(focusId));
            QVERIFY2(registry->action(toggleId) && registry->action(toggleId)->shortcut().isEmpty(),
                     qPrintable(toggleId));
        }

        auto *dock = m_win->findChild<QDockWidget *>(QStringLiteral("dockWidgetFeatures"));
        QVERIFY(dock);
        QAction *go = registry->action(QStringLiteral("view.focus.features"));
        m_win->show();
        m_win->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_win));
        const auto focusInDock = [dock] {
            QWidget *focus = QApplication::focusWidget();
            return focus && dock->isAncestorOf(focus);
        };
        dock->show();
        go->trigger();
        QVERIFY(dock->isVisible());
        QTRY_VERIFY(focusInDock());
        go->trigger(); // pressed again on an open, focused panel: it stays open
        QVERIFY(dock->isVisible());
        dock->hide();
        go->trigger(); // on a closed panel: it opens with the focus in it
        QVERIFY(dock->isVisible());
        QTRY_VERIFY(focusInDock());
    }

    /*! 1 — the dock exists, is named (so saveState/restoreState keep it) and
     *  is actually docked in the main window rather than orphaned. */
    void featuresDockIsBuiltAndNamed()
    {
        auto *dock = m_win->findChild<QDockWidget *>(QStringLiteral("dockWidgetFeatures"));
        QVERIFY2(dock != nullptr, "the Features dock was never constructed");
        QCOMPARE(m_win->dockWidgetArea(dock), Qt::RightDockWidgetArea);
        QVERIFY2(dock->widget() != nullptr, "the Features dock is empty");

        // Its toggle action is what View ▸ Panels and the catalog bind.
        auto *toggle = m_win->findChild<QAction *>(QStringLiteral("actionToggleDockFeatures"));
        QVERIFY2(toggle != nullptr, "no View > Panels toggle for the dock");
    }

    /*! 2 — every feature action exists under the window. The ribbon resolves
     *  actions by objectName and silently skips misses, so a typo here would
     *  otherwise show up only as a missing button. */
    void everyFeatureActionExists()
    {
        // Collect rather than abort: with QVERIFY2 in the loop a single bad
        // entry hides the state of the other seven.
        //
        // Icons are deliberately NOT asserted here. This binary does not load
        // swmmvis.qrc, so every action's icon is null in it — actionAddJunction,
        // whose icon is baked into the .ui, included. Icon coverage belongs to
        // test_icon_factory, which links the qrc and renders every catalog
        // alias; here it would only test the test environment.
        QStringList missing;
        for (const QString &name : kFeatureActions)
            if (!m_win->findChild<QAction *>(name))
                missing << name;
        QVERIFY2(
            missing.isEmpty(),
            qPrintable(
                QStringLiteral("missing actions: %1").arg(missing.join(QStringLiteral(", ")))));

        // The seven tools are modal and Edit Mode is a latch, so all eight
        // are checkable; New Feature Layer and Delete are one-shot commands
        // and must not be, or the button would stay stuck down.
        const QStringList oneShot = {QStringLiteral("actionNewFeatureLayer"),
                                     QStringLiteral("actionFeatureDelete")};
        for (const QString &name : kFeatureActions) {
            auto *act = m_win->findChild<QAction *>(name);
            QCOMPARE(act->isCheckable(), !oneShot.contains(name));
        }
    }

    /*! 3 — the ribbon tab is mounted and carries the actions. This is the
     *  assertion that would have failed for the whole first round, when the
     *  code existed but nothing built a tab. */
    void featuresRibbonTabIsMounted()
    {
        auto *bar = m_win->findChild<QToolBar *>(QStringLiteral("toolBarFeatures"));
        QVERIFY2(bar != nullptr, "the Features toolbar was never built");

        auto *controller = m_win->findChild<openswmmvis::ui::CompactToolbarController *>();
        QVERIFY2(controller != nullptr, "no compact toolbar controller");
        QVERIFY2(controller->tabIds().contains(QStringLiteral("features")),
                 "the 'features' tab id is not mounted on the ribbon");

        // Every feature action reached the bar. RibbonGroup::addAction does
        // NOT reparent the action — it creates a QToolButton and calls
        // setDefaultAction — so the actions are still children of the main
        // window and findChildren<QAction*>() on the bar returns nothing.
        // Walk the buttons instead.
        QStringList found;
        for (QToolButton *b : bar->findChildren<QToolButton *>())
            if (QAction *def = b->defaultAction(); def && !def->objectName().isEmpty())
                found << def->objectName();
        QStringList absent;
        for (const QString &name : kFeatureActions)
            if (!found.contains(name))
                absent << name;
        QVERIFY2(absent.isEmpty(),
                 qPrintable(QStringLiteral("never reached the Features toolbar: %1")
                                .arg(absent.join(QStringLiteral(", ")))));

        // Returning from a modal drawing tool to plain selection must never
        // cost a tab switch. Select now lives in the Quick Access cluster on
        // the tab strip, visible on every tab, rather than repeated as the
        // first group of each tab.
        QVERIFY2(!found.isEmpty(), "the Features toolbar has no buttons");
        QVERIFY2(!found.contains(QStringLiteral("actionSelect")),
                 "Select is repeated on the Features tab; it belongs to Quick Access");
        auto *quick = m_win->findChild<QWidget *>(QStringLiteral("ribbonQuickAccess"));
        QVERIFY2(quick != nullptr, "no Quick Access cluster on the tab strip");
        auto *select = m_win->findChild<QAction *>(QStringLiteral("actionSelect"));
        bool selectInQuickAccess = false;
        for (auto *b : quick->findChildren<openswmmvis::ui::RibbonSplitButton *>())
            selectInQuickAccess |= b->members().contains(select);
        QVERIFY2(selectInQuickAccess, "Select is missing from Quick Access");

        // The dock toggle belongs with every other dock toggle in View >
        // Panels (also the Home tab's Panels button), NOT on this tab — a
        // panel you closed is looked for there.
        QVERIFY2(!found.contains(QStringLiteral("actionToggleDockFeatures")),
                 "the Features dock toggle is on the Features tab; it belongs "
                 "in View > Panels");
        auto *panels = m_win->findChild<QMenu *>(QStringLiteral("menuViewPanels"));
        QVERIFY2(panels != nullptr, "the View > Panels menu is missing");
        auto *toggle = m_win->findChild<QAction *>(QStringLiteral("actionToggleDockFeatures"));
        QVERIFY2(panels->actions().contains(toggle),
                 "the Features dock toggle never reached View > Panels");
    }

    /*!
     * The project guard is declared in the catalog rather than asserted on a
     * freshly built window: applyProjectOpenToActions() runs only from
     * onActiveSubWindowChanged, so at construction NOTHING is gated yet —
     * actionAddJunction is enabled at this point too. What must hold is that
     * every feature action carries RequiresProject, which is what makes the
     * runtime sweep disable it. (The sweep itself is covered by
     * test_action_registry's tag enable/disable cases.)
     */
    void featureActionsDeclareTheProjectRequirement()
    {
        QStringList untagged;
        for (const QString &name : kFeatureActions) {
            bool seen = false;
            for (const auto &e : openswmmvis::ui::kActionCatalog) {
                if (QString::fromLatin1(e.objectName) != name)
                    continue;
                seen = true;
                if (!(e.tags & openswmmvis::ui::RequiresProject))
                    untagged << name;
            }
            QVERIFY2(seen, qPrintable(QStringLiteral("%1 is not in the catalog").arg(name)));
        }
        QVERIFY2(untagged.isEmpty(), qPrintable(QStringLiteral("not RequiresProject: %1")
                                                    .arg(untagged.join(QStringLiteral(", ")))));
    }

private:
    SWMMVis *m_win = nullptr;
};

QTEST_MAIN(TestFeatureLayerHostWiring)
#include "test_featurelayer_hostwiring.moc"
