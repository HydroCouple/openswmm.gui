// UI redesign P6 — CompactToolbarController on a standalone QMainWindow:
// tab switching swaps toolbar-row visibility, contextual tabs start
// hidden and fall back to a visible tab when hidden while current, the
// last active tab persists through QSettings, and every catalog entry
// carrying a tab id names a known tab.
#include <QtTest/QtTest>

#include <QMainWindow>
#include <QSet>
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

void TestCompactToolbar::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("compacttoolbar-test"));
}

void TestCompactToolbar::init()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("SWMMVis::MainWindow"));
    settings.remove(QStringLiteral("CompactToolbarTab"));
    settings.endGroup();
}

TestCompactToolbar::Rig TestCompactToolbar::makeRig(bool finalize)
{
    auto *window = new QMainWindow;
    auto *home  = new QToolBar(QStringLiteral("Home"), window);
    home->setObjectName(QStringLiteral("toolBarHome"));
    auto *model = new QToolBar(QStringLiteral("Model"), window);
    model->setObjectName(QStringLiteral("toolBarModel"));
    auto *mesh  = new QToolBar(QStringLiteral("Mesh"), window);
    mesh->setObjectName(QStringLiteral("toolBarMesh2D"));

    auto *controller = new CompactToolbarController(window);
    controller->addTab(QStringLiteral("home"),  QStringLiteral("Home"),  {home});
    controller->addTab(QStringLiteral("model"), QStringLiteral("Model"), {model});
    controller->addTab(QStringLiteral("mesh2d"), QStringLiteral("Mesh 2D"), {mesh},
                       /*contextual*/ true);
    if (finalize)
        controller->finalize();
    window->show();
    return {window, controller, home, model, mesh};
}

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

QTEST_MAIN(TestCompactToolbar)
#include "test_compact_toolbar.moc"
