/**
 * @file extension.cpp
 * @brief Standalone example consuming only the installed GUI SDK.
 * @details Registers a ribbon action and dock panel without private GUI headers. Widgets and actions are owned by the host; subscriptions disconnect with their widget context.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include <sdk/IGuiExtension.hpp>
#include <sdk/ExtensionHost.hpp>
#include <sdk/IModelInstance.hpp>
#include <QLabel>
#include <QVBoxLayout>

using namespace OpenSWMM::Sdk;

/** @brief Minimal installed-SDK plugin contributing one ribbon command and a model-aware dock. */
class ExampleExtension final : public QObject, public IGuiExtension, public IDockPanelFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OPENSWMM_GUI_EXTENSION_IID FILE "plugin.json")
    Q_INTERFACES(OpenSWMM::Sdk::IGuiExtension)
public:
    ExtensionInfo info() const override
    {
        return {"org.openswmm.example",
                "Example extension",
                "Installed SDK example",
                "1.0.0",
                "OpenSWMM",
                {},
                "GPL-3.0-or-later",
                {}};
    }
    bool initialize(ExtensionHost *host, QString *) override
    {
        mHost = host;
        auto *action = new QAction(tr("Show extension panel"), host->lifetime());
        connect(action, &QAction::triggered, host->lifetime(),
                [host]() { host->ui()->showDock("panel"); });
        if (!host->ui()->registerAction("show", action) ||
            !host->ui()->registerDockPanelFactory(this))
            return false;
        const RibbonTabSpec tab{"example", tr("Example"), false, {{tr("Extensions"), {"show"}}}};
        if (!host->ui()->registerRibbonTab(tab))
            return false;
        host->settings()->setValue("initialized", true);
        host->console()->append("info", "Example extension loaded through the public SDK.");
        return true;
    }
    void shutdown() override { mHost = nullptr; }
    QStringList contributions() const override { return {"ribbon", "dock"}; }
    QString id() const override { return "panel"; }
    QString title() const override { return tr("Example extension"); }
    QWidget *create(ExtensionHost *host) override
    {
        auto *widget = new QWidget;
        auto *layout = new QVBoxLayout(widget);
        auto *label =
            new QLabel(tr("This panel was built against the installed OpenSWMM GUI SDK."), widget);
        label->setWordWrap(true);
        layout->addWidget(label);
        auto *selection = new QLabel(widget);
        layout->addWidget(selection);
        host->observe(widget, [host, selection](const QString &, const QVariantMap &) {
            auto *instance = host->activeInstance();
            selection->setText(instance ? QObject::tr("Active model: %1 — %2 selected objects")
                                              .arg(instance->title())
                                              .arg(instance->selection().size())
                                        : QObject::tr("Open a model to begin."));
        });
        layout->addStretch();
        return widget;
    }

private:
    ExtensionHost *mHost = nullptr;
};
#include "extension.moc"
