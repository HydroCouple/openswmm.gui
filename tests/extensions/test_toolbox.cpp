/**
 * @file test_toolbox.cpp
 * @brief Acceptance tests for tagged tool discovery and deferred dock ownership.
 * @details Exercises real extension hosts, two independent plugin namespaces,
 * command activation, guarded factories and the shared toolbox search model.
 * Test settings remain under the repository verification directory.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionmanager.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include "sdk/IToolbox.hpp"
#include "ui/panels/toolboxpanel.h"
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QPointer>
#include <QPixmap>
#include <QSettings>
#include <QTest>
#include <memory>
#include <utility>

using namespace OpenSWMM::Sdk;
using namespace openswmmvis;

namespace
{
/** @brief Real generic host with reviewable settings and no model fixture. */
struct ToolboxFixture {
    ExtensionStateRegistry registry; ///< Native session/activity value registry.
    QMainWindow window;              ///< Outlives the extension hosts and widgets.
    QMdiArea *mdi = nullptr;         ///< Window-owned document area.
    QSettings settings{QStringLiteral(SDK_TOOLBOX_VERIFICATION_DIRECTORY "/host.ini"),
                       QSettings::IniFormat}; ///< Reviewable test preferences.
    std::unique_ptr<GuiExtensionHost> host; ///< Extension under test.

    /** @brief Create a fresh host in an empty native main window. */
    ToolboxFixture()
    {
        QDir().mkpath(QStringLiteral(SDK_TOOLBOX_VERIFICATION_DIRECTORY));
        settings.clear();
        mdi = new QMdiArea(&window);
        window.setCentralWidget(mdi);
        host = makeHost("org.openswmm.toolbox-first");
    }

    /** @brief Add an independent extension facade sharing only the native window.
     * @param[in] id Unique plugin identity.
     * @return Owning generic extension host. */
    std::unique_ptr<GuiExtensionHost> makeHost(const QString &id)
    {
        return std::make_unique<GuiExtensionHost>(id, ExtensionManager::buildManifest(),
                                                  &window, mdi, ExtensionUiBridge{},
                                                  &settings, nullptr, &registry);
    }

    /** @brief Create and register one host-owned command.
     * @param[in] id Extension-local command identity.
     * @return Host-owned action, or null on registration failure. */
    QAction *action(const QString &id)
    {
        auto *result = new QAction(id, host->lifetime());
        return host->registerAction(id, result) ? result : nullptr;
    }
};

/** @brief Metadata for a tagged download tool.
 * @param[in] id Local identity, also used as the command identity.
 * @param[in] tags Searchable category keys.
 * @return Owning SDK metadata. */
ToolSpec tool(const QString &id, const QStringList &tags = {"download"})
{
    return {id, id, "Review and download source data", {{"download", "Download data"}}, tags};
}

/** @brief Run one lifecycle hook from native widget reparenting. */
class ReparentingLabel final : public QLabel
{
public:
    std::function<void()> attached; ///< One-shot callback after the widget acquires a parent.

protected:
    /** @brief Preserve native event handling before running the tested reentrant callback. */
    bool event(QEvent *event) override
    {
        const bool result = QLabel::event(event);
        if (event->type() == QEvent::ParentChange && parentWidget()) {
            auto callback = std::exchange(attached, {});
            if (callback)
                callback();
        }
        return result;
    }
};
}

/** @brief Behavior covered by the approved toolbox implementation plan. */
class ToolboxTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Invalid metadata leaves no registration and the same ID can be corrected. */
    void invalidMetadataIsAtomic()
    {
        ToolboxFixture f;
        auto *action = f.action("sites");
        QVERIFY(action);
        auto *service = qobject_cast<IToolbox *>(f.host->lifetime());
        QVERIFY(service);
        QObject context;
        int created = 0;
        const auto factory = [&](ExtensionHost *) {
            ++created;
            return new QLabel("Tool");
        };
        QString error;
        QVERIFY(!service->registerTool(tool("sites", {}), &context, factory, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!service->openTool("sites", &error));
        QCOMPARE(created, 0);
        QVERIFY(service->registerTool(tool("sites"), &context, factory, &error));
        QVERIFY(!service->registerTool(tool("sites"), &context, factory, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(created, 0);
        QVERIFY(service->openTool("sites", &error));
        QCOMPARE(created, 1);
    }

    /** @brief Unknown commands, absent guards and reused commands cannot register a tool. */
    void registrationRequiresOwnedActionAndFactory()
    {
        ToolboxFixture f;
        auto *service = qobject_cast<IToolbox *>(f.host->lifetime());
        QVERIFY(service);
        QObject context;
        const auto factory = [](ExtensionHost *) { return new QLabel("Tool"); };
        QString error;
        QVERIFY(!service->registerTool(tool("missing"), &context, factory, &error));
        QVERIFY(f.action("sites"));
        QVERIFY(!service->registerTool(tool("sites"), nullptr, factory, &error));
        QVERIFY(!service->registerTool(tool("sites"), &context, {}, &error));
        QVERIFY(service->registerTool(tool("sites"), &context, factory, &error));
        auto other = tool("other");
        other.actionId = "sites";
        QVERIFY(!service->registerTool(other, &context, factory, &error));
    }

    /** @brief Tree and ribbon/palette activation share a single stateful deferred widget. */
    void commandsOpenOneStatefulPanel()
    {
        ToolboxFixture f;
        auto *action = f.action("sites");
        QVERIFY(action);
        auto *service = qobject_cast<IToolbox *>(f.host->lifetime());
        QObject context;
        int created = 0;
        QPointer<QLabel> content;
        QVERIFY(service->registerTool(tool("sites"), &context, [&](ExtensionHost *) {
            ++created;
            content = new QLabel("Initial");
            return content.data();
        }));
        QCOMPARE(created, 0);
        action->trigger();
        QCOMPARE(created, 1);
        QVERIFY(content);
        content->setText("Preserved selection");
        auto *dock = qobject_cast<QDockWidget *>(content->parentWidget());
        if (!dock)
            dock = content->findChild<QDockWidget *>();
        // The tool wrapper may be between the transferred content and its dock.
        for (auto *parent = content->parentWidget(); !dock && parent;
             parent = parent->parentWidget())
            dock = qobject_cast<QDockWidget *>(parent);
        QVERIFY(dock);
        dock->close();
        QVERIFY(content);
        QVERIFY(service->openTool("sites"));
        action->trigger();
        QCOMPARE(created, 1);
        QCOMPARE(content->text(), QString("Preserved selection"));
        action->setEnabled(false);
        QString error;
        QVERIFY(!service->openTool("sites", &error));
        QVERIFY(!error.isEmpty());
    }

    /** @brief Context destruction retires both the registration and its live widget. */
    void contextLifetimeOwnsRegistration()
    {
        ToolboxFixture f;
        auto *action = f.action("sites");
        QVERIFY(action);
        auto *service = qobject_cast<IToolbox *>(f.host->lifetime());
        auto *context = new QObject;
        QPointer<QWidget> content;
        QVERIFY(service->registerTool(tool("sites"), context, [&](ExtensionHost *) {
            content = new QLabel("Tool");
            return content.data();
        }));
        QVERIFY(service->openTool("sites"));
        delete context;
        QVERIFY(content.isNull());
        QVERIFY(!service->openTool("sites"));
        action->trigger();
        QVERIFY(content.isNull());
    }

    /** @brief A destroyed command cannot leave a clickable tool or borrowed callback. */
    void actionLifetimeOwnsRegistration()
    {
        ToolboxFixture f;
        auto *action = f.action("sites");
        QVERIFY(action);
        auto *service = qobject_cast<IToolbox *>(f.host->lifetime());
        QObject context;
        QPointer<QWidget> content;
        QVERIFY(service->registerTool(tool("sites"), &context, [&](ExtensionHost *) {
            content = new QLabel("Tool");
            return content.data();
        }));
        QVERIFY(service->openTool("sites"));
        delete action;
        QVERIFY(content.isNull());
        QVERIFY(!service->openTool("sites"));
    }

    /** @brief Unregistering a command retires its tool even when the QAction remains alive. */
    void unregisteringActionRetiresTool()
    {
        ToolboxFixture f;
        auto *action = f.action("sites");
        QVERIFY(action);
        auto *service = qobject_cast<IToolbox *>(f.host->lifetime());
        QObject context;
        QPointer<QWidget> content;
        QVERIFY(service->registerTool(tool("sites"), &context, [&](ExtensionHost *) {
            content = new QLabel("Tool");
            return content.data();
        }));
        QVERIFY(service->openTool("sites"));
        f.host->unregisterAction("sites");
        QVERIFY(content.isNull());
        QVERIFY(!service->openTool("sites"));
        action->trigger();
        QVERIFY(content.isNull());
    }

    /** @brief Deferred restore reopens available tools but respects an explicitly closed dock. */
    void restorePreviouslyOpenTool()
    {
        ToolboxFixture f;
        f.window.show();
        QVERIFY(f.action("sites"));
        auto *service = qobject_cast<IToolbox *>(f.host->lifetime());
        QObject context;
        QVERIFY(service->registerTool(tool("sites"), &context,
                                      [](ExtensionHost *) { return new QLabel("Initial"); }));
        QVERIFY(service->openTool("sites"));
        f.host.reset();
        f.host = f.makeHost("org.openswmm.toolbox-first");
        QVERIFY(f.action("sites"));
        service = qobject_cast<IToolbox *>(f.host->lifetime());
        int created = 0;
        QPointer<QLabel> content;
        QVERIFY(service->registerTool(tool("sites"), &context, [&](ExtensionHost *) {
            ++created;
            content = new QLabel("Restored");
            return content.data();
        }));
        QCOMPARE(created, 0);
        QCoreApplication::processEvents();
        QCOMPARE(created, 1);
        QVERIFY(content);
        QDockWidget *dock = nullptr;
        for (auto *parent = content->parentWidget(); !dock && parent;
             parent = parent->parentWidget())
            dock = qobject_cast<QDockWidget *>(parent);
        QVERIFY(dock);
        dock->close();
        QCoreApplication::processEvents();
        f.host.reset();
        f.host = f.makeHost("org.openswmm.toolbox-first");
        QVERIFY(f.action("sites"));
        service = qobject_cast<IToolbox *>(f.host->lifetime());
        created = 0;
        QVERIFY(service->registerTool(tool("sites"), &context, [&](ExtensionHost *) {
            ++created;
            return new QLabel("Should stay deferred");
        }));
        QCoreApplication::processEvents();
        QCOMPARE(created, 0);
    }

    /** @brief Reentrant unregister during creation disposes the unmounted widget. */
    void reentrantFactoryDoesNotLeaveDock()
    {
        ToolboxFixture f;
        QVERIFY(f.action("sites"));
        auto *service = qobject_cast<IToolbox *>(f.host->lifetime());
        QObject context;
        QPointer<QWidget> content;
        QVERIFY(service->registerTool(tool("sites"), &context, [&](ExtensionHost *) {
            service->unregisterTool("sites");
            content = new QLabel("Refused");
            return content.data();
        }));
        QVERIFY(!service->openTool("sites"));
        QVERIFY(content.isNull());
        QVERIFY(!service->openTool("sites"));
    }

    /** @brief An old factory cannot publish into a replacement registration using the same ID. */
    void replacedRegistrationRejectsOldFactoryContent()
    {
        ToolboxFixture f;
        QVERIFY(f.action("sites"));
        auto *service = qobject_cast<IToolbox *>(f.host->lifetime());
        QObject context;
        QPointer<QWidget> obsolete;
        QPointer<QWidget> replacement;
        bool registeredReplacement = false;
        QVERIFY(service->registerTool(tool("sites"), &context, [&](ExtensionHost *) {
            service->unregisterTool("sites");
            registeredReplacement = service->registerTool(
                tool("sites"), &context, [&](ExtensionHost *) {
                    replacement = new QLabel("Replacement");
                    return replacement.data();
                });
            obsolete = new QLabel("Obsolete");
            return obsolete.data();
        }));
        QVERIFY(!service->openTool("sites"));
        QVERIFY(registeredReplacement);
        QVERIFY(obsolete.isNull());
        QVERIFY(replacement.isNull());
        QVERIFY(service->openTool("sites"));
        QVERIFY(replacement);
    }

    /** @brief Unregistration from native content attachment safely retires the in-flight dock. */
    void unregisterDuringNativeAttachment()
    {
        ToolboxFixture f;
        QVERIFY(f.action("sites"));
        auto *service = qobject_cast<IToolbox *>(f.host->lifetime());
        QObject context;
        QPointer<ReparentingLabel> content;
        QVERIFY(service->registerTool(tool("sites"), &context, [&](ExtensionHost *) {
            content = new ReparentingLabel;
            content->attached = [service] { service->unregisterTool("sites"); };
            return content.data();
        }));
        QVERIFY(!service->openTool("sites"));
        QVERIFY(content.isNull());
        QVERIFY(!service->openTool("sites"));
    }

    /** @brief Identical local IDs belong to separate plugin namespaces. */
    void pluginNamespacesRemainIndependent()
    {
        ToolboxFixture f;
        auto second = f.makeHost("org.openswmm.toolbox-second");
        QVERIFY(f.action("sites"));
        auto *secondAction = new QAction("Second sites", second->lifetime());
        QVERIFY(second->registerAction("sites", secondAction));
        auto *firstService = qobject_cast<IToolbox *>(f.host->lifetime());
        auto *secondService = qobject_cast<IToolbox *>(second->lifetime());
        QObject context;
        int firstCreated = 0;
        int secondCreated = 0;
        QVERIFY(firstService->registerTool(tool("sites"), &context, [&](ExtensionHost *) {
            ++firstCreated;
            return new QLabel("First");
        }));
        QVERIFY(secondService->registerTool(tool("sites"), &context, [&](ExtensionHost *) {
            ++secondCreated;
            return new QLabel("Second");
        }));
        QVERIFY(firstService->openTool("sites"));
        QVERIFY(secondService->openTool("sites"));
        firstService->unregisterTool("sites");
        QVERIFY(!firstService->openTool("sites"));
        QVERIFY(secondService->openTool("sites"));
        QCOMPARE(firstCreated, 1);
        QCOMPARE(secondCreated, 1);
    }

    /** @brief Tags intersect across owners and combine with text/path matching. */
    void graphicalFiltersIntersect()
    {
        openswmmvis::ui::ToolboxPanel panel;
        QAction sites("USGS time series");
        QAction terrain("Elevation raster");
        QAction other("Other station data");
        panel.addTool("org.first", tool("sites", {"download", "usgs", "time-series"}), &sites);
        panel.addTool("org.first", tool("terrain", {"download", "usgs", "raster"}), &terrain);
        panel.addTool("org.second", tool("sites", {"station", "time-series"}), &other);
        QCOMPARE(panel.toolCount(), 3);
        QCOMPARE(panel.visibleToolCount(), 3);
        panel.setSelectedTags({"download"});
        QCOMPARE(panel.visibleToolCount(), 2);
        panel.setSelectedTags({"download", "raster"});
        QCOMPARE(panel.visibleToolCount(), 1);
        panel.resize(460, 560);
        panel.show();
        QCoreApplication::processEvents();
        QDir().mkpath(QStringLiteral(SDK_TOOLBOX_VERIFICATION_DIRECTORY));
        QVERIFY(panel.grab().save(
            QStringLiteral(SDK_TOOLBOX_VERIFICATION_DIRECTORY "/filtered-toolbox-fixture.png")));
        panel.setSearchText("USGS time");
        QCOMPARE(panel.visibleToolCount(), 0);
        panel.setSelectedTags({});
        QCOMPARE(panel.visibleToolCount(), 1);
        panel.setSearchText("Download data");
        QCOMPARE(panel.visibleToolCount(), 3);
        panel.setSearchText({});
        panel.setSelectedTags({"time-series"});
        QCOMPARE(panel.visibleToolCount(), 2);
        panel.removeTool("org.first", "sites");
        QCOMPARE(panel.toolCount(), 2);
        QCOMPARE(panel.visibleToolCount(), 1);
        panel.setSelectedTags({});
        QCOMPARE(panel.visibleToolCount(), 2);
    }

    /** @brief A large metadata catalogue filters without invoking any tool factory. */
    void thousandToolsRemainMetadataOnly()
    {
        openswmmvis::ui::ToolboxPanel panel;
        QAction action("Synthetic tool");
        QElapsedTimer timer;
        timer.start();
        for (int index = 0; index < 1000; ++index) {
            const QString owner = index < 500 ? "org.first" : "org.second";
            panel.addTool(owner,
                          tool(QString::number(index),
                               index % 2 ? QStringList{"download", "raster"}
                                         : QStringList{"download", "time-series"}),
                          &action);
        }
        QCoreApplication::processEvents();
        const auto registrationMs = timer.elapsed();
        QCOMPARE(panel.toolCount(), 1000);
        timer.restart();
        panel.setSelectedTags({"download", "raster"});
        QCOMPARE(panel.visibleToolCount(), 500);
        qInfo() << "1000 tools registration ms" << registrationMs << "filter ms" << timer.elapsed();
    }
};
QTEST_MAIN(ToolboxTest)
#include "test_toolbox.moc"
