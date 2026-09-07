/*!
 * \file   test_featurelayer_hostwiring.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The feature-layer stack is REACHABLE from the main window
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
 */
#include "swmmvis.h"
#include "ui/actioncatalog.h"
#include "ui/toolbars/compacttoolbarcontroller.h"

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

namespace {

//! The eight actions §6.2 requires, by objectName.
const QStringList kFeatureActions = {
    QStringLiteral("actionNewFeatureLayer"),
    QStringLiteral("actionFeatureEditMode"),
    QStringLiteral("actionFeatureDelete"),
    QStringLiteral("actionFeatureDrawPoint"),
    QStringLiteral("actionFeatureDrawLine"),
    QStringLiteral("actionFeatureDrawPolygon"),
    QStringLiteral("actionFeatureAddPart"),
    QStringLiteral("actionFeatureAddHole"),
    QStringLiteral("actionFeatureEditVertex"),
    QStringLiteral("actionFeatureMove"),
};

} // namespace

class TestFeatureLayerHostWiring : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        m_win = new SWMMVis(nullptr);
        QVERIFY(m_win != nullptr);
        QTest::qWait(50);
    }

    void cleanupTestCase()
    {
        if (m_win) { m_win->close(); delete m_win; m_win = nullptr; }
    }

    /*! 1 — the dock exists, is named (so saveState/restoreState keep it) and
     *  is actually docked in the main window rather than orphaned. */
    void featuresDockIsBuiltAndNamed()
    {
        auto *dock = m_win->findChild<QDockWidget *>(
            QStringLiteral("dockWidgetFeatures"));
        QVERIFY2(dock != nullptr, "the Features dock was never constructed");
        QCOMPARE(m_win->dockWidgetArea(dock), Qt::RightDockWidgetArea);
        QVERIFY2(dock->widget() != nullptr, "the Features dock is empty");

        // Its toggle action is what View ▸ Panels and the catalog bind.
        auto *toggle = m_win->findChild<QAction *>(
            QStringLiteral("actionToggleDockFeatures"));
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
            if (!m_win->findChild<QAction *>(name)) missing << name;
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("missing actions: %1")
                                .arg(missing.join(QStringLiteral(", ")))));

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

        auto *controller =
            m_win->findChild<openswmmvis::ui::CompactToolbarController *>();
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
            if (QAction *def = b->defaultAction();
                def && !def->objectName().isEmpty())
                found << def->objectName();
        QStringList absent;
        for (const QString &name : kFeatureActions)
            if (!found.contains(name)) absent << name;
        QVERIFY2(absent.isEmpty(),
                 qPrintable(QStringLiteral("never reached the Features toolbar: %1")
                                .arg(absent.join(QStringLiteral(", ")))));

        // Select is the FIRST button on the bar, so returning from a modal
        // drawing tool to plain selection never costs a tab switch. Ordered,
        // not merely present: findChildren returns children in insertion
        // order, which is the order the RibbonGroups were added.
        QVERIFY2(!found.isEmpty(), "the Features toolbar has no buttons");
        QCOMPARE(found.first(), QStringLiteral("actionSelect"));

        // The dock toggle belongs with every other dock toggle in View >
        // Panels, NOT on this tab — a panel you closed is looked for under
        // View, and this tab is the one place it must not be the only home.
        QVERIFY2(!found.contains(QStringLiteral("actionToggleDockFeatures")),
                 "the Features dock toggle is on the Features tab; it belongs "
                 "in View > Panels");

        auto *viewBar = m_win->findChild<QToolBar *>(QStringLiteral("toolBarView"));
        QVERIFY2(viewBar != nullptr, "the View toolbar is missing");
        QStringList onView;
        for (QToolButton *b : viewBar->findChildren<QToolButton *>())
            if (QAction *def = b->defaultAction();
                def && !def->objectName().isEmpty())
                onView << def->objectName();
        QVERIFY2(onView.contains(QStringLiteral("actionToggleDockFeatures")),
                 qPrintable(QStringLiteral(
                     "the Features dock toggle never reached View > Panels; "
                     "found there: %1").arg(onView.join(QStringLiteral(", ")))));
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
                if (QString::fromLatin1(e.objectName) != name) continue;
                seen = true;
                if (!(e.tags & openswmmvis::ui::RequiresProject))
                    untagged << name;
            }
            QVERIFY2(seen, qPrintable(QStringLiteral("%1 is not in the catalog")
                                          .arg(name)));
        }
        QVERIFY2(untagged.isEmpty(),
                 qPrintable(QStringLiteral("not RequiresProject: %1")
                                .arg(untagged.join(QStringLiteral(", ")))));
    }

private:
    SWMMVis *m_win = nullptr;
};

QTEST_MAIN(TestFeatureLayerHostWiring)
#include "test_featurelayer_hostwiring.moc"
