/**
 * @file IApplicationServices.hpp
 * @brief Public adapters for the application's simulation, plotting and dialog services.
 * @details All calls use the GUI thread and delegate to the existing application controllers.
 * Returned metadata is copied; no runner, plot-model or private dialog objects cross the SDK boundary.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QStringList>
#include <QVariantMap>
namespace OpenSWMM::Sdk
{
class IModelInstance;
/** @brief Simulation Status facade supporting native and legacy worker runs. */
class IRunRegistry
{
public:
    /** @brief Destroy an implementation; extensions borrow this host-owned service. */
    virtual ~IRunRegistry() = default;
    /** @brief Copy current Simulation Status rows.
     * @return Maps containing id, name, inputPath, engineVersion, status, progress, start/end,
     * errorCode, error, warnings and available continuity percentages. */
    virtual QList<QVariantMap> runs() const = 0;
    /** @brief Save and run an existing model using its configured engine/options.
     * @param[in] instance Borrowed live, previously saved model facade.
     * @param[out] error Optional save, lease or startup refusal detail.
     * @return Simulation Status job ID, or minus one if execution did not start.
     * @note Uses normal application preflight/output rules and can present their existing dialogs. */
    virtual int start(IModelInstance *instance, QString *error = nullptr) = 0;
    /** @brief Request cancellation of one active run.
     * @param[in] id Stable Simulation Status job ID; unknown/finished IDs are ignored. */
    virtual void cancel(int id) = 0;
};
/** @brief Access to existing time-series and profile plot panels. */
class IPlotService
{
public:
    /** @brief Destroy an implementation; extensions borrow this host-owned service. */
    virtual ~IPlotService() = default;
    /** @brief Enumerate plottable variables from the model's active 1D results.
     * @param[in] instance Borrowed live model.
     * @param[in] category One of node, link, subcatchment or system.
     * @return Maps with stable key, label and units; empty if no matching results/kind exists. */
    virtual QList<QVariantMap> variables(IModelInstance *instance,
                                         const QString &category) const = 0;
    /** @brief Add a result series to the shared time-series plot panel.
     * @param[in] instance Borrowed live model with active 1D results.
     * @param[in] category Node, link, subcatchment or system.
     * @param[in] name Object ID, or empty for system results.
     * @param[in] variableKey Key returned by variables(); no GUI enum values are required.
     * @return False for an unavailable model/result/object/variable; true after panel submission. */
    virtual bool timeSeries(IModelInstance *instance, const QString &category, const QString &name,
                            const QString &variableKey) = 0;
    /** @brief Open the standard profile panel along connected named nodes.
     * @param[in] instance Borrowed live model; result overlays follow its active results.
     * @param[in] nodeNames At least two ordered connected node IDs; each adjacent pair must have a link.
     * @return False if a node/link is missing or no supported model is available. */
    virtual bool profile(IModelInstance *instance, const QStringList &nodeNames) = 0;
};
/** @brief Argument-free built-in dialog commands, exposed through registered application actions. */
class IDialogs
{
public:
    /** @brief Destroy an implementation; extensions borrow this host-owned service. */
    virtual ~IDialogs() = default;
    /** @brief Enumerate currently registered built-in commands that open dialogs.
     * @return Stable ActionRegistry identities whose user-facing action advertises a dialog. */
    virtual QStringList ids() const = 0;
    /** @brief Trigger a built-in dialog in the current application context.
     * @param[in] id Identity returned by ids(); extension commands are excluded.
     * @return False if unavailable/disabled/unknown; true after triggering the normal action. */
    virtual bool open(const QString &id) = 0;
};
}
