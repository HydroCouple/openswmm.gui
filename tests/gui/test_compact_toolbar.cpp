// UI redesign P6 — CompactToolbarController on a standalone QMainWindow:
// tab switching swaps toolbar-row visibility, contextual tabs start
// hidden and fall back to a visible tab when hidden while current, the
// last active tab persists through QSettings, and every catalog entry
// carrying a tab id names a known tab.

/**
 * @file      test_compact_toolbar.cpp
 * @brief     Tests for compact toolbar.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest/QtTest>

#include <QLabel>
#include <QDir>
#include <QMainWindow>
#include <QSet>
#include <QTabBar>
#include <QSettings>
#include <QToolBar>

#include "ui/actioncatalog.h"
#include "ui/toolbars/compacttoolbarcontroller.h"

using openswmmvis::ui::CompactToolbarController;
using openswmmvis::ui::kActionCatalog;
using openswmmvis::ui::kActionCatalogTabs;

class TestCompactToolbar : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void tabSwitchSwapsVisibility();
    void contextualTabHiddenUntilRevealed();
    void hidingCurrentTabFallsBack();
    void lastTabPersists();
    void catalogTabIdsAreMounted();
    void pluginTabsRemainIndependentAndRetainOwners();
    void contextualPluginTabsRemainIndependent();
    void pluginTabPreferenceRestoresAfterRegistration();

    // iteration 3 — tab-scoped own-row bars (Terrain / Mesh Editing)
    void ownRowBarSitsBelowBreak();
    void hiddenOwnRowConsumesNoHeight();
    void revealingTabWidensStripNoScrollers();

private:
    struct Rig {
        QMainWindow *window;
        CompactToolbarController *controller;
        QToolBar *home;
        QToolBar *model;
        QToolBar *mesh;
    };
    Rig makeRig(bool finalize = true);
};

/**
 *
 * @brief Select the isolated openswmm-test/compacttoolbar-test process application identity for
 * toolbar persistence.
 *
 */
void TestCompactToolbar::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("compacttoolbar-test"));
    const auto directory = qEnvironmentVariable("OPENSWMM_RIBBON_TEST_DIRECTORY",
                                                QCoreApplication::applicationDirPath()) +
                           "/compact-toolbar-settings";
    QVERIFY(QDir().mkpath(directory));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, directory);
}

/**
 *
 * @brief Remove the saved CompactToolbarTab setting before each case; does not clear other
 * main-window settings.
 *
 */
void TestCompactToolbar::init()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("SWMMVis::MainWindow"));
    settings.remove(QStringLiteral("CompactToolbarTab"));
    settings.endGroup();
}

/**
 *
 * @brief Allocate/show a main window with Home/Model and initially contextual Mesh toolbar tabs.
 * @param finalize Restore/complete controller setup when true, default true.
 * @return By-value raw-pointer bundle: caller owns window and must delete it;
 * window owns all bars and controller. Controller may read/write current test
 * QSettings. A false finalize leaves that step to the caller.
 *
 */
TestCompactToolbar::Rig TestCompactToolbar::makeRig(bool finalize)
{
    auto *window = new QMainWindow;
    auto *home = new QToolBar(QStringLiteral("Home"), window);
    home->setObjectName(QStringLiteral("toolBarHome"));
    auto *model = new QToolBar(QStringLiteral("Model"), window);
    model->setObjectName(QStringLiteral("toolBarModel"));
    auto *mesh = new QToolBar(QStringLiteral("Mesh"), window);
    mesh->setObjectName(QStringLiteral("toolBarMesh2D"));

    auto *controller = new CompactToolbarController(window);
    controller->addTab(QStringLiteral("home"), QStringLiteral("Home"), {home});
    controller->addTab(QStringLiteral("model"), QStringLiteral("Model"), {model});
    controller->addTab(QStringLiteral("mesh2d"), QStringLiteral("Mesh 2D"), {mesh},
                       /*contextual*/ true);
    if (finalize)
        controller->finalize();
    window->show();
    return {window, controller, home, model, mesh};
}

/**
 *
 * @brief Require changing Home to Model to swap visible toolbar rows and update the current tab.
 *
 */
void TestCompactToolbar::tabSwitchSwapsVisibility()
{
    Rig rig = makeRig();
    QCOMPARE(rig.controller->currentTab(), QStringLiteral("home"));
    QVERIFY(rig.home->isVisibleTo(rig.window));
    QVERIFY(!rig.model->isVisibleTo(rig.window));

    rig.controller->setCurrentTab(QStringLiteral("model"));
    QCOMPARE(rig.controller->currentTab(), QStringLiteral("model"));
    QVERIFY(!rig.home->isVisibleTo(rig.window));
    QVERIFY(rig.model->isVisibleTo(rig.window));
    delete rig.window;
}

/**
 *
 * @brief Require a hidden Mesh tab to refuse selection until explicitly revealed, then show its
 * toolbar.
 *
 */
void TestCompactToolbar::contextualTabHiddenUntilRevealed()
{
    Rig rig = makeRig();
    QVERIFY(!rig.controller->isTabVisible(QStringLiteral("mesh2d")));
    // Selecting a hidden tab is a no-op.
    rig.controller->setCurrentTab(QStringLiteral("mesh2d"));
    QCOMPARE(rig.controller->currentTab(), QStringLiteral("home"));

    rig.controller->setTabVisible(QStringLiteral("mesh2d"), true);
    QVERIFY(rig.controller->isTabVisible(QStringLiteral("mesh2d")));
    rig.controller->setCurrentTab(QStringLiteral("mesh2d"));
    QCOMPARE(rig.controller->currentTab(), QStringLiteral("mesh2d"));
    QVERIFY(rig.mesh->isVisibleTo(rig.window));
    delete rig.window;
}

/**
 *
 * @brief Require hiding the active contextual tab to choose another visible tab and display that
 * tab toolbar.
 *
 */
void TestCompactToolbar::hidingCurrentTabFallsBack()
{
    Rig rig = makeRig();
    rig.controller->setTabVisible(QStringLiteral("mesh2d"), true);
    rig.controller->setCurrentTab(QStringLiteral("mesh2d"));
    QCOMPARE(rig.controller->currentTab(), QStringLiteral("mesh2d"));

    // Mesh unloads while its tab is current → QTabBar falls back to a
    // neighboring visible tab; the hidden tab's row disappears and the
    // new current tab's row shows.
    rig.controller->setTabVisible(QStringLiteral("mesh2d"), false);
    const QString current = rig.controller->currentTab();
    QVERIFY(current != QStringLiteral("mesh2d"));
    QVERIFY(!rig.mesh->isVisibleTo(rig.window));
    const auto bars = rig.controller->toolbarsForTab(current);
    QVERIFY(!bars.isEmpty());
    QVERIFY(bars.first()->isVisibleTo(rig.window));
    delete rig.window;
}

/**
 *
 * @brief Require choosing Model in one window to restore Model when a fresh fixture window is
 * created in the same test namespace.
 *
 */
void TestCompactToolbar::lastTabPersists()
{
    {
        Rig rig = makeRig();
        rig.controller->setCurrentTab(QStringLiteral("model"));
        delete rig.window;
    }
    {
        Rig rig = makeRig();
        QCOMPARE(rig.controller->currentTab(), QStringLiteral("model"));
        delete rig.window;
    }
}

/**
 *
 * @brief Require every nonempty catalog tab ID to belong to kActionCatalogTabs.
 * This checks the vocabulary, without constructing the complete application tab mounting.
 *
 */
void TestCompactToolbar::catalogTabIdsAreMounted()
{
    QSet<QString> known;
    for (const char *tab : kActionCatalogTabs)
        known.insert(QString::fromLatin1(tab));
    // The app mounts exactly the catalog's tab vocabulary (home, model,
    // mesh2d, analysis, results, view) — every catalog tab id must be in
    // that set, so no action can be assigned to an unmounted tab.
    for (const auto &entry : kActionCatalog) {
        const QString tab = QString::fromLatin1(entry.tab);
        if (!tab.isEmpty())
            QVERIFY2(known.contains(tab),
                     qPrintable(QStringLiteral("entry '%1' names unmounted tab '%2'")
                                    .arg(QLatin1String(entry.id), tab)));
    }
}

/** @brief Qualified plugin tabs retain their own captions, toolbar visibility and cleanup. */
void TestCompactToolbar::pluginTabsRemainIndependentAndRetainOwners()
{
    Rig rig = makeRig();
    auto *data = new QToolBar("Data", rig.window);
    auto *other = new QToolBar("Other", rig.window);
    rig.controller->addTab("ext.data.tab", "Data", {data});
    rig.controller->addTab("ext.other.tab", "Other", {other});
    rig.controller->addTab("ext.data.tab", "Duplicate", {other});
    QCOMPARE(rig.controller->tabIds().count("ext.data.tab"), 1);
    QVERIFY(!rig.controller->tabIds().contains("plugins"));
    QCOMPARE(rig.controller->toolbarsForTab("ext.data.tab"), QList<QToolBar *>{data});
    auto *tabs = rig.window->findChild<QTabBar *>("compactToolbarTabBar");
    QVERIFY(tabs);
    const auto ids = rig.controller->tabIds();
    QCOMPARE(tabs->tabText(ids.indexOf("ext.data.tab")), "Data");
    QCOMPARE(tabs->tabText(ids.indexOf("ext.other.tab")), "Other");
    rig.controller->setCurrentTab("ext.data.tab");
    QCOMPARE(rig.controller->currentTab(), "ext.data.tab");
    QVERIFY(!data->isHidden());
    QVERIFY(other->isHidden());
    QVERIFY(rig.home->isHidden());
    rig.controller->setCurrentTab("ext.other.tab");
    QVERIFY(data->isHidden());
    QVERIFY(!other->isHidden());
    rig.controller->removeTab("ext.data.tab");
    delete data;
    QCOMPARE(rig.controller->currentTab(), "ext.other.tab");
    QVERIFY(!other->isHidden());
    QCOMPARE(rig.controller->toolbarsForTab("ext.other.tab"), QList<QToolBar *>{other});
    rig.controller->removeTab("ext.other.tab");
    delete other;
    QVERIFY(!rig.controller->tabIds().contains("ext.other.tab"));
    QVERIFY(rig.controller->toolbarsForTab("ext.data.tab").isEmpty());
    delete rig.window;
}

/** @brief A contextual plugin tab can be revealed or hidden without changing another plugin. */
void TestCompactToolbar::contextualPluginTabsRemainIndependent()
{
    Rig rig = makeRig(false);
    auto *hidden = new QToolBar("Contextual", rig.window);
    auto *shown = new QToolBar("Shown", rig.window);
    rig.controller->addTab("ext.context.tab", "Contextual", {hidden}, true);
    rig.controller->addTab("ext.shown.tab", "Shown", {shown});
    rig.controller->finalize();
    QVERIFY(!rig.controller->isTabVisible("ext.context.tab"));
    QVERIFY(rig.controller->isTabVisible("ext.shown.tab"));
    rig.controller->setCurrentTab("ext.context.tab");
    QCOMPARE(rig.controller->currentTab(), "home");
    rig.controller->setCurrentTab("ext.shown.tab");
    QVERIFY(hidden->isHidden());
    QVERIFY(!shown->isHidden());
    rig.controller->setTabVisible("ext.context.tab", true);
    QVERIFY(hidden->isHidden());
    QVERIFY(!shown->isHidden());
    rig.controller->setCurrentTab("ext.context.tab");
    QVERIFY(!hidden->isHidden());
    QVERIFY(shown->isHidden());
    rig.controller->setTabVisible("ext.shown.tab", false);
    QVERIFY(!hidden->isHidden());
    rig.controller->setTabVisible("ext.context.tab", false);
    QVERIFY(rig.controller->currentTab() != "ext.context.tab");
    QVERIFY(hidden->isHidden());
    QVERIFY(shown->isHidden());
    rig.controller->setTabVisible("ext.shown.tab", true);
    rig.controller->setCurrentTab("ext.shown.tab");
    QCOMPARE(rig.controller->currentTab(), "ext.shown.tab");
    QVERIFY(!shown->isHidden());
    QVERIFY(hidden->isHidden());
    delete rig.window;
}

/** @brief A saved plugin-owned tab identity restores when its contribution registers after setup. */
void TestCompactToolbar::pluginTabPreferenceRestoresAfterRegistration()
{
    QSettings settings;
    settings.setValue("SWMMVis::MainWindow/CompactToolbarTab", "ext.data.tab");
    Rig rig = makeRig();
    auto *data = new QToolBar("Data", rig.window);
    rig.controller->addTab("ext.data.tab", "Data", {data});
    QCOMPARE(rig.controller->currentTab(), "ext.data.tab");
    QCOMPARE(settings.value("SWMMVis::MainWindow/CompactToolbarTab").toString(), "ext.data.tab");
    delete rig.window;
}

/**
 *
 * @brief Require a tab-owned terrain bar to mount after its own toolbar break and swap visibility
 * together with its ordinary Model bar.
 *
 */
void TestCompactToolbar::ownRowBarSitsBelowBreak()
{
    // An own-row bar (Terrain / Mesh Editing) mounts AFTER a toolbar
    // break — its own full row — while ordinary bars share row 2.
    auto *window = new QMainWindow;
    auto *home = new QToolBar(QStringLiteral("Home"), window);
    home->setObjectName(QStringLiteral("toolBarHome"));
    auto *model = new QToolBar(QStringLiteral("Model"), window);
    model->setObjectName(QStringLiteral("toolBarModel"));
    auto *terrain = new QToolBar(QStringLiteral("Terrain"), window);
    terrain->setObjectName(QStringLiteral("toolBarTerrain"));

    auto *controller = new CompactToolbarController(window);
    controller->addTab(QStringLiteral("home"), QStringLiteral("Home"), {home});
    controller->addTab(QStringLiteral("model"), QStringLiteral("Model"), {model, terrain});
    controller->setOwnRow(terrain);
    controller->finalize();
    window->show();

    // home leads row 2, so the strip->row-2 break sits before IT; model
    // shares that row (no break); terrain opens row 3 with its own break.
    QVERIFY(window->toolBarBreak(home));
    QVERIFY(!window->toolBarBreak(model));
    QVERIFY(window->toolBarBreak(terrain));
    QCOMPARE(window->toolBarArea(terrain), Qt::TopToolBarArea);

    // Both of the tab's bars still swap together.
    controller->setCurrentTab(QStringLiteral("model"));
    QVERIFY(model->isVisibleTo(window));
    QVERIFY(terrain->isVisibleTo(window));
    controller->setCurrentTab(QStringLiteral("home"));
    QVERIFY(!model->isVisibleTo(window));
    QVERIFY(!terrain->isVisibleTo(window));
    delete window;
}

/**
 *
 * @brief Require hiding the dedicated terrain row to restore central-widget top position exactly
 * after a visible-row round trip.
 *
 */
void TestCompactToolbar::hiddenOwnRowConsumesNoHeight()
{
    // The design leans on QToolBarAreaLayout collapsing a line whose
    // bars are all hidden — verify the third row costs zero height on
    // tabs without an own-row bar.
    auto *window = new QMainWindow;
    window->setCentralWidget(new QWidget(window));
    auto *home = new QToolBar(QStringLiteral("Home"), window);
    home->setObjectName(QStringLiteral("toolBarHome"));
    home->addWidget(new QWidget(home)); // non-empty so the row has height
    auto *model = new QToolBar(QStringLiteral("Model"), window);
    model->setObjectName(QStringLiteral("toolBarModel"));
    auto *terrain = new QToolBar(QStringLiteral("Terrain"), window);
    terrain->setObjectName(QStringLiteral("toolBarTerrain"));
    terrain->addWidget(new QLabel(QStringLiteral("terrain row"), terrain));

    auto *controller = new CompactToolbarController(window);
    controller->addTab(QStringLiteral("home"), QStringLiteral("Home"), {home});
    controller->addTab(QStringLiteral("model"), QStringLiteral("Model"), {model, terrain});
    controller->setOwnRow(terrain);
    controller->finalize();
    window->resize(700, 400);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));

    controller->setCurrentTab(QStringLiteral("home"));
    QTRY_VERIFY(!terrain->isVisibleTo(window));
    const int topWithoutOwnRow = window->centralWidget()->mapTo(window, QPoint(0, 0)).y();

    controller->setCurrentTab(QStringLiteral("model"));
    QTRY_VERIFY(terrain->isVisible());
    const int topWithOwnRow = window->centralWidget()->mapTo(window, QPoint(0, 0)).y();
    QVERIFY2(topWithOwnRow > topWithoutOwnRow,
             qPrintable(QStringLiteral("own row added no height (%1 -> %2)")
                            .arg(topWithoutOwnRow)
                            .arg(topWithOwnRow)));

    controller->setCurrentTab(QStringLiteral("home"));
    QTRY_COMPARE(window->centralWidget()->mapTo(window, QPoint(0, 0)).y(), topWithoutOwnRow);
    delete window;
}

/**
 *
 * @brief Require revealing Mesh to resize the tab bar to at least its size hint with the final
 * tab inside the visible width.
 *
 */
void TestCompactToolbar::revealingTabWidensStripNoScrollers()
{
    // Revealing a contextual tab grows the tab bar's size hint; the host
    // strip toolbar must re-lay so the bar gets its full width instead of
    // clamping at the stale width and sprouting scroll arrows despite
    // ample row space.
    Rig rig = makeRig();
    rig.window->resize(900, 300);
    QVERIFY(QTest::qWaitForWindowExposed(rig.window));

    auto *tabBar = rig.window->findChild<QTabBar *>(QStringLiteral("compactToolbarTabBar"));
    QVERIFY(tabBar);

    rig.controller->setTabVisible(QStringLiteral("mesh2d"), true);
    QCoreApplication::processEvents();

    QTRY_VERIFY2(tabBar->width() >= tabBar->sizeHint().width(),
                 qPrintable(QStringLiteral("tab bar %1 px < hint %2 px — "
                                           "scroll arrows would show")
                                .arg(tabBar->width())
                                .arg(tabBar->sizeHint().width())));
    // Every tab's rect must sit inside the visible bar.
    const QRect last = tabBar->tabRect(tabBar->count() - 1);
    QVERIFY(last.right() <= tabBar->width());
    delete rig.window;
}

QTEST_MAIN(TestCompactToolbar)
#include "test_compact_toolbar.moc"
