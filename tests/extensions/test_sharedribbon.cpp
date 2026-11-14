/**
 * @file test_sharedribbon.cpp
 * @brief Shared ribbon ownership, dynamic groups and optional Data ribbon placement.
 * @details Exercises two independent contributors through the real native toolbar controller.
 * Test settings and screenshots stay under the repository verification directory.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionsharedribbon.h"
#include "plugins/extensionmanager.h"
#include "plugins/extensionpreferences.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include "ui/panels/toolboxpanel.h"
#include "ui/toolbars/compacttoolbarcontroller.h"
#include "ui/toolbars/ribbongroup.h"
#include <QComboBox>
#include <QDir>
#include <QMainWindow>
#include <QMdiArea>
#include <QPluginLoader>
#include <QSettings>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <memory>

using namespace openswmmvis;
using namespace openswmmvis::ui;
using namespace OpenSWMM::Sdk;
namespace
{
SharedRibbonTabSpec spec(const QString &id = "tools", const QString &tab = "data",
                         const QString &title = "Data")
{
    return {id, tab, title, {{"Download data", {"open"}}}};
}
struct Fixture {
    QMainWindow window;
    CompactToolbarController *controller = new CompactToolbarController(&window);
    ExtensionSharedRibbon *ribbon = new ExtensionSharedRibbon(&window, controller);
    explicit Fixture(bool home = true)
    {
        if (home) {
            auto *bar = new QToolBar("Home", &window);
            bar->setObjectName("home-toolbar");
            controller->addTab("home", "Home", {bar});
        }
        controller->finalize();
    }
};
}

class SharedRibbonTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QDir().mkpath(QStringLiteral(SDK_SHARED_RIBBON_VERIFICATION_DIRECTORY));
        QCoreApplication::setOrganizationName("OpenSWMMTests");
        QCoreApplication::setApplicationName("SharedRibbon");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral(SDK_SHARED_RIBBON_VERIFICATION_DIRECTORY));
        QSettings{}.clear();
    }
    void twoPluginsShareOneTabAndKeepOwnCommands()
    {
        Fixture f;
        QAction first("USGS time series");
        QAction second("Elevation raster");
        int firstRuns = 0;
        int secondRuns = 0;
        connect(&first, &QAction::triggered, this, [&] { ++firstRuns; });
        connect(&second, &QAction::triggered, this, [&] { ++secondRuns; });
        QString error;
        QVERIFY2(f.ribbon->add("org.first", spec(), {{"open", &first}}, &error), qPrintable(error));
        QVERIFY2(f.ribbon->add("org.second", spec(), {{"open", &second}}, &error), qPrintable(error));
        QCOMPARE(f.controller->tabIds(), QStringList({"home", "shared.data"}));
        const auto bars = f.controller->toolbarsForTab("shared.data");
        QCOMPARE(bars.size(), 2);
        QVERIFY(bars[0] != bars[1]);
        QCOMPARE(bars[0]->objectName(), "ext.org.first.shared.tools");
        QCOMPARE(bars[1]->objectName(), "ext.org.second.shared.tools");
        f.controller->setCurrentTab("shared.data");
        QVERIFY(!bars[0]->isHidden());
        QVERIFY(!bars[1]->isHidden());
        bars[0]->findChild<RibbonGroup *>()->buttonForAction(&first)->click();
        bars[1]->findChild<RibbonGroup *>()->buttonForAction(&second)->click();
        QCOMPARE(firstRuns, 1);
        QCOMPARE(secondRuns, 1);
        QPointer<QToolBar> firstBar = bars[0];
        f.ribbon->removeOwner("org.first");
        QVERIFY(firstBar.isNull());
        QCOMPARE(f.controller->toolbarsForTab("shared.data"), QList<QToolBar *>{bars[1]});
        QCOMPARE(f.controller->currentTab(), "shared.data");
        bars[1]->findChild<RibbonGroup *>()->buttonForAction(&second)->click();
        QCOMPARE(secondRuns, 2);
        f.ribbon->remove("org.second", "tools");
        QCOMPARE(f.controller->tabIds(), QStringList{"home"});
    }
    void invalidRegistrationIsAtomicAndCaptionsAgree()
    {
        Fixture f;
        QAction action("Open");
        const QHash<QString, QPointer<QAction>> actions{{"open", &action}};
        QString error;
        QVERIFY(!f.ribbon->add("org.first", spec(), {}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(f.controller->tabIds(), QStringList{"home"});
        QVERIFY(f.ribbon->add("org.first", spec(), actions, &error));
        QVERIFY(!f.ribbon->add("org.first", spec(), actions, &error));
        QVERIFY(!f.ribbon->add("org.second", spec("tools", "data", "Other title"), actions, &error));
        QVERIFY(error.contains("caption"));
        QCOMPARE(f.controller->toolbarsForTab("shared.data").size(), 1);
        auto invalid = spec("empty");
        invalid.groups.clear();
        QVERIFY(!f.ribbon->add("org.second", invalid, actions, &error));
        QVERIFY(f.ribbon->add("org.second", spec(), actions, &error));
        QCOMPARE(f.controller->toolbarsForTab("shared.data").size(), 2);
    }
    void sharedKeysCannotTakeOverAnExistingTab()
    {
        Fixture f;
        auto *external = new QToolBar("Reserved", &f.window);
        f.controller->addTab("shared.external", "Reserved", {external});
        QAction action("Open");
        QString error;
        QVERIFY(!f.ribbon->add("org.first", spec("tools", "external", "Reserved"),
                               {{"open", &action}}, &error));
        f.ribbon->removeOwner("org.first");
        QVERIFY(f.controller->tabIds().contains("shared.external"));
        QCOMPARE(f.controller->toolbarsForTab("shared.external"), QList<QToolBar *>{external});
    }
    void actionDestructionRemovesOnlyItsContribution()
    {
        Fixture f;
        auto first = std::make_unique<QAction>("First");
        QAction second("Second");
        QVERIFY(f.ribbon->add("org.first", spec(), {{"open", first.get()}}));
        QVERIFY(f.ribbon->add("org.second", spec(), {{"open", &second}}));
        first.reset();
        QCOMPARE(f.controller->toolbarsForTab("shared.data").size(), 1);
        QCOMPARE(f.controller->toolbarsForTab("shared.data").first()->objectName(),
                 "ext.org.second.shared.tools");
    }
    void externallyDestroyedToolbarDoesNotLeaveDanglingPointers()
    {
        Fixture f;
        QAction action("Open");
        QVERIFY(f.ribbon->add("org.first", spec(), {{"open", &action}}));
        delete f.controller->toolbarsForTab("shared.data").first();
        QTRY_VERIFY(!f.controller->tabIds().contains("shared.data"));
        QVERIFY(f.ribbon->add("org.first", spec(), {{"open", &action}}));
    }
    void controllerCanRetireBeforeSharedService()
    {
        Fixture f;
        QAction action("Open");
        QVERIFY(f.ribbon->add("org.first", spec(), {{"open", &action}}));
        delete f.controller;
        f.controller = nullptr;
        f.ribbon->removeOwner("org.first");
        QVERIFY(f.window.findChildren<QToolBar *>("ext.org.first.shared.tools").isEmpty());
    }
    void reentrantRemovalCannotLeaveSharedTab()
    {
        Fixture f(false);
        QAction action("Open");
        connect(f.controller, &CompactToolbarController::currentTabChanged, f.ribbon,
                [&] { f.ribbon->removeOwner("org.first"); });
        QVERIFY(!f.ribbon->add("org.first", spec(), {{"open", &action}}));
        QVERIFY(!f.controller->tabIds().contains("shared.data"));
        QVERIFY(f.window.findChildren<QToolBar *>("ext.org.first.shared.tools").isEmpty());
    }
    void controllerToolbarMembershipIsIndependent()
    {
        Fixture f;
        auto *bar = new QToolBar("Extra", &f.window);
        QVERIFY(f.controller->addToolbarToTab("home", bar));
        QVERIFY(!f.controller->addToolbarToTab("home", bar));
        QVERIFY(!f.controller->addToolbarToTab("missing", bar));
        QCOMPARE(f.controller->toolbarsForTab("home").size(), 2);
        QVERIFY(f.controller->removeToolbarFromTab("home", bar));
        QVERIFY(!f.controller->removeToolbarFromTab("home", bar));
        QVERIFY(bar->isHidden());
        QCOMPARE(f.controller->toolbarsForTab("home").size(), 1);
        delete bar;
    }
    void realHostsRetireOnlyTheirOwnedContributions()
    {
        Fixture f;
        auto *mdi = new QMdiArea(&f.window);
        f.window.setCentralWidget(mdi);
        ExtensionStateRegistry states;
        QSettings settings(QStringLiteral(SDK_SHARED_RIBBON_VERIFICATION_DIRECTORY "/hosts.ini"),
                            QSettings::IniFormat);
        settings.clear();
        const auto makeHost = [&](const QString &owner) {
            ExtensionUiBridge bridge;
            bridge.addSharedTab = [&, owner](const SharedRibbonTabSpec &spec,
                const QHash<QString, QPointer<QAction>> &actions, QString *error) {
                return f.ribbon->add(owner, spec, actions, error);
            };
            bridge.removeSharedTab = [&, owner](const QString &id) { f.ribbon->remove(owner, id); };
            return std::make_unique<GuiExtensionHost>(owner, ExtensionManager::buildManifest(),
                &f.window, mdi, std::move(bridge), &settings, nullptr, &states);
        };
        auto first = makeHost("org.first");
        auto second = makeHost("org.second");
        auto *firstAction = new QAction("First", first->lifetime());
        auto *secondAction = new QAction("Second", second->lifetime());
        QVERIFY(first->registerAction("open", firstAction));
        QVERIFY(second->registerAction("open", secondAction));
        auto *firstRibbon = qobject_cast<ISharedRibbon *>(first->lifetime());
        auto *secondRibbon = qobject_cast<ISharedRibbon *>(second->lifetime());
        QVERIFY(firstRibbon && secondRibbon);
        QVERIFY(firstRibbon->registerSharedRibbonTab(spec()));
        QVERIFY(secondRibbon->registerSharedRibbonTab(spec()));
        QCOMPARE(f.controller->toolbarsForTab("shared.data").size(), 2);
        QPointer<QAction> preserved(firstAction);
        first->unregisterAction("open");
        QVERIFY(preserved);
        QCOMPARE(f.controller->toolbarsForTab("shared.data").size(), 1);
        QVERIFY(first->registerAction("again", new QAction("Again", first->lifetime())));
        auto replacement = spec();
        replacement.groups.first().actionIds = {"again"};
        QVERIFY(firstRibbon->registerSharedRibbonTab(replacement));
        QCOMPARE(f.controller->toolbarsForTab("shared.data").size(), 2);
        first->prepareShutdown();
        QCOMPARE(f.controller->toolbarsForTab("shared.data").size(), 1);
        first.reset();
        QVERIFY(f.controller->tabIds().contains("shared.data"));
        second.reset();
        QCOMPARE(f.controller->tabIds(), QStringList{"home"});
    }
    void actualDataPlacementIsOptional()
    {
        const auto module = qEnvironmentVariable("SDK_SHARED_DATA_MODULE");
        if (module.isEmpty())
            QSKIP("Set SDK_SHARED_DATA_MODULE to the rebuilt Data module for this installed-plugin check.");
        Fixture f;
        auto *mdi = new QMdiArea(&f.window);
        f.window.setCentralWidget(mdi);
        ExtensionStateRegistry states;
        QSettings settings(QStringLiteral(SDK_SHARED_RIBBON_VERIFICATION_DIRECTORY "/data.ini"),
                            QSettings::IniFormat);
        settings.clear();
        const QString pluginId = "openswmm.gui.data";
        ExtensionUiBridge bridge;
        bridge.addTab = [&](const RibbonTabSpec &spec,
                            const QHash<QString, QPointer<QAction>> &actions) {
            if (f.controller->tabIds().contains(spec.id))
                return false;
            auto *bar = new QToolBar(spec.title, &f.window);
            bar->setObjectName(spec.id);
            for (const auto &group : spec.groups)
                for (const auto &id : group.actionIds)
                    bar->addAction(actions.value(id));
            f.controller->addTab(spec.id, spec.title, {bar}, spec.contextual);
            return f.controller->tabIds().contains(spec.id);
        };
        bridge.removeTab = [&](const QString &id) {
            const auto bars = f.controller->toolbarsForTab(id);
            f.controller->removeTab(id);
            qDeleteAll(bars);
        };
        bridge.addSharedTab = [&](const SharedRibbonTabSpec &spec,
                                  const QHash<QString, QPointer<QAction>> &actions, QString *error) {
            return f.ribbon->add(pluginId, spec, actions, error);
        };
        bridge.removeSharedTab = [&](const QString &id) { f.ribbon->remove(pluginId, id); };
        QPluginLoader loader(module);
        loader.setLoadHints(QLibrary::ResolveAllSymbolsHint);
        auto *instance = loader.instance();
        QVERIFY2(instance, qPrintable(loader.errorString()));
        auto *extension = qobject_cast<IGuiExtension *>(instance);
        QVERIFY(extension);
        auto host = std::make_unique<GuiExtensionHost>(pluginId, ExtensionManager::buildManifest(),
            &f.window, mdi, std::move(bridge), &settings, nullptr, &states);
        QString error;
        QVERIFY2(extension->initialize(host.get(), &error), qPrintable(error));
        auto *catalog = f.window.findChild<ToolboxPanel *>("analysisToolboxPanel");
        QVERIFY(catalog);
        QCOMPARE(catalog->toolCount(), 4);
        IPreferencesPageFactory *preferences = nullptr;
        for (const auto &entry : extensionPreferences())
            if (entry.owner == host.get())
                preferences = entry.factory;
        QVERIFY(preferences);
        std::unique_ptr<QWidget> page(preferences->create(host->settings()));
        auto *placement = page->findChild<QComboBox *>("dataRibbonPlacement");
        QVERIFY(placement);
        for (const auto &mode : {QString("toolbox"), QString("separate"), QString("shared"), QString("toolbox")}) {
            const int index = placement->findData(mode);
            QVERIFY2(index >= 0, qPrintable("Missing Data ribbon placement " + mode));
            placement->setCurrentIndex(index);
            preferences->apply();
            QCOMPARE(f.controller->tabIds().contains("ext.openswmm.gui.data.data"), mode == "separate");
            QCOMPARE(f.controller->tabIds().contains("shared.data"), mode == "shared");
            QCOMPARE(catalog->toolCount(), 4);
        }
        placement->setCurrentIndex(placement->findData("shared"));
        preferences->apply();
        QVERIFY(f.controller->tabIds().contains("shared.data"));
        page.reset();
        host->prepareShutdown();
        QVERIFY(!f.controller->tabIds().contains("shared.data"));
        extension->shutdown();
        host.reset();
        QVERIFY(loader.unload());
    }
};
QTEST_MAIN(SharedRibbonTest)
#include "test_sharedribbon.moc"
