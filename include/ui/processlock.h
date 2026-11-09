/**
 * @file      processlock.h
 * @brief     Whether an optional process is switched off, so its records are read-only.
 *
 * Process-aware editing plan §5 (Phase 4): a process switched off explicitly is read-only on
 * every route — ribbon and menu commands, its dialogs and the Properties/table Edit… actions.
 * Its authored records stay and stay inspectable, and the switch itself is never locked. Every
 * route asks here, so they agree.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QCoreApplication>
#include <QString>

#include <openswmm/engine/openswmm_model.h>

namespace openswmmvis::ui
{

/*! Processes switched off only by an explicit choice. Snow and heat are not lockable: the
 *  engine reads IGNORE_SNOWMELT as YES whenever a model has no snowpack (legacy
 *  project_validate), and Heat Configuration holds HEAT_TRANSPORT beside every heat setting,
 *  so both are set up first and switched on afterwards. */
enum class LockableProcess { Groundwater2D, SurfaceQuality };

/**
 * @brief Say why \p process cannot be edited, or nothing when it can.
 * @param[in] engine Borrowed engine; null locks nothing (callers gate on a model separately).
 * @param[in] process Process whose switch is read.
 * @return Empty when writable; otherwise a translated sentence naming the switch. Groundwater
 *   locks only on GROUNDWATER NO: AUTO switches on once aquifer rows exist, so authoring them
 *   must stay possible.
 */
inline QString processLockReason(SWMM_Engine engine, LockableProcess process)
{
    if (!engine)
        return {};
    const auto option = [engine](const char *key, bool extension) {
        char value[64] = {};
        const int rc = extension ? swmm_options_get_ext(engine, key, value, sizeof value)
                                 : swmm_options_get(engine, key, value, sizeof value);
        return rc == 0 ? QString::fromUtf8(value).trimmed().toUpper() : QString();
    };
    const auto yes = [](const QString &value) {
        return value == QLatin1String("YES") || value == QLatin1String("TRUE") ||
               value == QLatin1String("1");
    };
    const auto tr = [](const char *text) {
        return QCoreApplication::translate("ProcessLock", text);
    };
    switch (process) {
    case LockableProcess::Groundwater2D:
        if (option("GROUNDWATER", true) == QLatin1String("NO"))
            return tr("2D groundwater is off. Set Simulation Options › 2D Surface Routing › "
                      "Processes › Groundwater to Automatic or On to edit it.");
        break;
    case LockableProcess::SurfaceQuality:
        if (yes(option("IGNORE_QUALITY", false)))
            return tr("Water quality is off. Tick Water quality under Simulation Options › "
                      "Models / Processes › Active processes to edit it.");
        break;
    }
    return {};
}

} // namespace openswmmvis::ui
