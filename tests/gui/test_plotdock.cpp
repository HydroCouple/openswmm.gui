/**
 * @file      test_plotdock.cpp
 * @brief     Tests for plotdock.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/panels/plotdock.h"
#include "ui/panels/plotpanelview.h"
#include "ui/dialogs/dialoglayoutpersistence.h"
#include <QAction>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

using namespace openswmmvis::ui;

class TestPlotDock : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("OpenSWMMPlotDockTests");
        QCoreApplication::setApplicationName("PlotDock");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    }
    void init() { QSettings().clear(); }
    void independentTabsHideFloatAndClose()
    {
        QMainWindow window;
        auto *dock = new PlotDock("Profiles", "profiles", &window);
        window.addDockWidget(Qt::BottomDockWidgetArea, dock);
        window.resize(900, 600);
        window.show();
        QPointer<PlotPanelView> first = new PlotPanelView;
        QPointer<PlotPanelView> second = new PlotPanelView;
        first->setProperty("zoom", 2.0);
        second->setProperty("zoom", 7.0);
        dock->addView(first, "First");
        dock->addView(second, "Second");
        QCOMPARE(dock->tabs()->count(), 2);
        QTRY_VERIFY(second->isPlotActive());
        QVERIFY(!first->isPlotActive());
        dock->tabs()->tabBar()->moveTab(1, 0);
        QCOMPARE(dock->tabs()->currentWidget(), second.data());
        dock->reveal(first);
        QTRY_VERIFY(first->isPlotActive());
        QVERIFY(!second->isPlotActive());
        dock->close();
        QVERIFY(first);
        QVERIFY(second);
        QCOMPARE(dock->tabs()->count(), 2);
        QVERIFY(!first->isPlotActive());
        dock->reveal(first);
        dock->setFloating(true);
        QTRY_VERIFY(dock->isFloating());
        QCOMPARE(first->property("zoom").toDouble(), 2.0);
        QCOMPARE(second->property("zoom").toDouble(), 7.0);
        dock->setFloating(false);
        window.showMinimized();
        QTRY_VERIFY(!window.isVisible() || window.isMinimized());
        // Cocoa animates minimization asynchronously. Let the native window
        // finish hiding before asking it to restore.
        QTest::qWait(500);
        window.showNormal();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(first->isPlotActive());
        QTest::keyClick(first, Qt::Key_Escape);
        QCOMPARE(dock->tabs()->count(), 2);
        dock->closeView(first);
        QCOMPARE(dock->tabs()->count(), 1);
        QTRY_VERIFY(first.isNull());
        QVERIFY(second);
        QTRY_VERIFY(second->isPlotActive());
        // Programmatic project closure follows the same path as a tab button.
        second->close();
        QCOMPARE(dock->tabs()->count(), 0);
        QTRY_VERIFY(second.isNull());
    }
    void projectContextAndTabbedDockVisibility()
    {
        QMainWindow window;
        QObject projectA, projectB;
        auto *one = new PlotDock("1D", "one", &window);
        auto *two = new PlotDock("2D", "two", &window);
        window.addDockWidget(Qt::BottomDockWidgetArea, one);
        window.addDockWidget(Qt::BottomDockWidgetArea, two);
        window.tabifyDockWidget(one, two);
        window.show();
        auto *a = new PlotPanelView;
        auto *b = new PlotPanelView;
        one->setActiveContext(&projectA);
        two->setActiveContext(&projectA);
        one->addView(a, "A", &projectA);
        two->addView(b, "B", &projectB);
        QVERIFY(!b->isPlotActive());
        two->setActiveContext(&projectB);
        QTRY_VERIFY(b->isPlotActive());
        QVERIFY(!a->isPlotActive());
        one->reveal(a);
        QTRY_VERIFY(a->isPlotActive());
        QTRY_VERIFY(!b->isPlotActive());
        one->setActiveContext(&projectB);
        QVERIFY(!a->isPlotActive());
    }
    void embeddedLayoutNeverMovesHostAndDoesNotRestoreMapTools()
    {
        QMainWindow window;
        auto *dock = new PlotDock("Profiles", "profiles", &window);
        window.addDockWidget(Qt::BottomDockWidgetArea, dock);
        window.resize(950, 650);
        window.show();
        auto create = [] {
            auto *view = new PlotPanelView;
            view->setObjectName("LayoutTest");
            auto *layout = new QVBoxLayout(view);
            auto *splitter = new QSplitter(view);
            splitter->setObjectName("main");
            splitter->addWidget(new QWidget);
            splitter->addWidget(new QWidget);
            layout->addWidget(splitter);
            return view;
        };
        auto *first = create();
        dock->addView(first, "First");
        auto *splitter = first->findChild<QSplitter *>();
        splitter->setSizes({200, 600});
        first->saveLayout();
        QSettings s;
        QVERIFY(s.contains("Dialogs/LayoutTest/splitter/main"));
        QVERIFY(!s.contains("Dialogs/LayoutTest/geometry"));
        // Old dialog geometry must not resize the main window or a floating dock.
        s.setValue("Dialogs/LayoutTest/geometry", QRect(1, 1, 200, 100));
        s.setValue("Dialogs/LayoutTest/toggle/mapTool", true);
        auto *second = create();
        auto *mapTool = new QAction(second);
        mapTool->setObjectName("mapTool");
        mapTool->setCheckable(true);
        mapTool->setProperty("noLayoutPersistence", true);
        const QRect geometry = window.geometry();
        dock->addView(second, "Second");
        QCOMPARE(window.geometry(), geometry);
        QVERIFY(!mapTool->isChecked());
        const auto sizes = second->findChild<QSplitter *>()->sizes();
        QVERIFY(sizes[1] > sizes[0] * 2);
        splitter->setSizes({600, 200});
        dock->reveal(first);
        QVERIFY(splitter->sizes()[0] > splitter->sizes()[1]);
    }
    void layoutRestoresDockPositionsWithEmptyTabs()
    {
        QMainWindow first;
        auto *dock = new PlotDock("Profiles", "profiles", &first);
        first.addDockWidget(Qt::RightDockWidgetArea, dock);
        first.show();
        const auto state = first.saveState(2);
        QMainWindow second;
        auto *restored = new PlotDock("Profiles", "profiles", &second);
        second.addDockWidget(Qt::BottomDockWidgetArea, restored);
        QVERIFY(second.restoreState(state, 2));
        QCOMPARE(second.dockWidgetArea(restored), Qt::RightDockWidgetArea);
        QCOMPARE(restored->tabs()->count(), 0);
    }
};
QTEST_MAIN(TestPlotDock)
#include "test_plotdock.moc"
