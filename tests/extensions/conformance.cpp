/**
 * @file conformance.cpp
 * @brief Loadable native fixtures for SDK conformance and failed initialization.
 * @details The same source builds successful and deliberately failing plugins. Both create real dock contributions so lifecycle tests can verify cleanup rather than merely inspecting metadata.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include <sdk/ExtensionHost.hpp>
#include <sdk/IGuiExtension.hpp>
#include <QLabel>

using namespace OpenSWMM::Sdk;
/** @brief Native fixture exercising host contributions on successful or failed initialization. */
class ConformanceExtension final : public QObject, public IGuiExtension, public IDockPanelFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OPENSWMM_GUI_EXTENSION_IID FILE "plugin.json")
    Q_INTERFACES(OpenSWMM::Sdk::IGuiExtension)
public:
    ExtensionInfo info() const override
    {
#ifdef SDK_CONFORMANCE_FAIL
        return {"org.openswmm.failed", "Failed initialization", {}, "1.0.0", {}, {}, {}, {}};
#else
        return {"org.openswmm.conformance", "SDK conformance", {}, "1.0.0", {}, {}, {}, {}};
#endif
    }
    bool initialize(ExtensionHost *host, QString *error) override
    {
        host->settings()->setValue("initialized", true);
        if (!host->ui()->registerDockPanelFactory(this))
            return false;
#ifdef SDK_CONFORMANCE_FAIL
        *error = "Intentional initialization failure";
        return false;
#else
        return true;
#endif
    }
    void shutdown() override {}
    QString id() const override { return "conformanceDock"; }
    QString title() const override { return "Conformance dock"; }
    QWidget *create(ExtensionHost *) override { return new QLabel("SDK conformance"); }
};
#include "conformance.moc"
