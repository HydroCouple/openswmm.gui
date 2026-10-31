/**
 * @file extension.cpp
 * @brief Installed-SDK example of tagged tools, deferred docks and analysis results.
 * @details Two commands share a ribbon while opening independent panels. Each
 * accepted calculation publishes copied results and messages through IToolRuns.
 * No private GUI header or engine image is required.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include <sdk/IGuiExtension.hpp>
#include <sdk/IToolbox.hpp>
#include <sdk/IToolRuns.hpp>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>

using namespace OpenSWMM::Sdk;

/** @brief Minimal independent plugin consuming tool capabilities only through the SDK. */
class ToolboxExample final : public QObject, public IGuiExtension
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OPENSWMM_GUI_EXTENSION_IID FILE "plugin.json")
    Q_INTERFACES(OpenSWMM::Sdk::IGuiExtension)
public:
    /** @brief Return the identity embedded by openswmmvis_add_extension().
     * @return Owning plugin metadata. */
    ExtensionInfo info() const override
    {
        return {"org.openswmm.toolbox-example", "Toolbox example",
                "Tagged tools with independent panels", "1.0.0", "OpenSWMM", {},
                "GPL-3.0-or-later", {"example", "analysis"}};
    }

    /** @brief Register commands and deferred factories without constructing their widgets.
     * @param[in] host Borrowed initialized host.
     * @param[out] error Optional missing capability or contribution diagnostic.
     * @return True after both tools and their ribbon have registered. */
    bool initialize(ExtensionHost *host, QString *error) override
    {
        auto *tools = qobject_cast<IToolbox *>(host->lifetime());
        auto *runs = qobject_cast<IToolRuns *>(host->lifetime());
        if (!tools || !runs) {
            if (error)
                *error = tr("The toolbox example requires SDK 1.10 tool services.");
            return false;
        }
        for (const auto &id : {QStringLiteral("square"), QStringLiteral("double")}) {
            const bool square = id == "square";
            auto *action = new QAction(square ? tr("Square a value…") : tr("Double a value…"),
                                       host->lifetime());
            ToolSpec spec{id, id, square ? tr("Calculate the square of a supplied value.")
                                        : tr("Multiply a supplied value by two."),
                          {{"arithmetic", tr("Arithmetic")}},
                          {"example", "calculation", square ? "square" : "double"},
                          tr("Toolbox example")};
            if (!host->ui()->registerAction(id, action) ||
                !tools->registerTool(spec, host->lifetime(),
                                     [runs, id, square](ExtensionHost *) {
                                         return makePanel(runs, id, square);
                                     }, error))
                return false;
        }
        return host->ui()->registerRibbonTab(
            {"example", tr("Example"), false, {{tr("Arithmetic"), {"square", "double"}}}});
    }

    /** @brief The host owns the registered commands, panels and factory contexts. */
    void shutdown() override {}
    /** @brief Advertise the contribution families for the plugin manager.
     * @return Tool discovery and ribbon contribution captions. */
    QStringList contributions() const override { return {"toolbox", "ribbon"}; }

private:
    /** @brief Build one independent input panel on its first activation.
     * @param[in] runs Borrowed tool-run capability.
     * @param[in] id Registered local tool identity.
     * @param[in] square True to square, false to double the input.
     * @return New widget transferring to the host-owned tool dock. */
    static QWidget *makePanel(IToolRuns *runs, const QString &id, bool square)
    {
        auto *panel = new QWidget;
        auto *layout = new QFormLayout(panel);
        auto *input = new QDoubleSpinBox(panel);
        input->setRange(-1000000., 1000000.);
        input->setDecimals(3);
        auto *result = new QLabel(panel);
        auto *run = new QPushButton(tr("Calculate"), panel);
        layout->addRow(tr("Value"), input);
        layout->addRow(run);
        layout->addRow(tr("Result"), result);
        QObject::connect(run, &QPushButton::clicked, panel, [runs, id, square, input, result, panel] {
            const double value = input->value();
            const auto execution = runs->beginToolRun(
                {id, tr("Calculate"), tr("Input value %1").arg(value), {}, 0}, panel);
            if (execution.isNull())
                return;
            const double answer = square ? value * value : 2. * value;
            result->setText(QString::number(answer, 'g', 12));
            runs->appendToolMessage(execution, "info", tr("Calculated from the reviewed input."));
            runs->addToolResult(execution,
                                {ToolResultKind::Value, tr("Result"), result->text(), {}, {}, {}});
            runs->finishToolRun(execution, ToolRunOutcome::Succeeded, tr("Calculation complete."));
        });
        return panel;
    }
};
#include "extension.moc"
