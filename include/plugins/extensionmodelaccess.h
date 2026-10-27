/**
 * @file extensionmodelaccess.h
 * @brief Adapter entry point connecting SDK model access to existing project windows.
 * @details Concrete model and canvas classes remain private to the application. A host owns its model facades; QPointer guards prevent access after project-window destruction.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "plugins/guiextensionhost.h"
#include <functional>
class SWMMVisProjectWindow;
namespace openswmmvis
{
/** @brief Begin a checked native time-series assignment transaction.
 * @param[in] window Exact live target; running/leased/closing models are refused.
 * @param[in] label Complete undo caption.
 * @param[out] error Snapshot/lease refusal detail, cleared on entry.
 * @return Owned mediated scope. Assignment provenance in the window's
 * timeSeriesAssignments property is restored with engine state on abort/undo/redo.
 * @note GUI thread only; do not process events inside the scope. */
std::unique_ptr<OpenSWMM::Sdk::IEditScope>
beginTimeSeriesModelEdit(SWMMVisProjectWindow *window, const QString &label,
                         QString *error = nullptr);
/** @brief Install QPointer-backed SDK model facades owned by the extension host.
 * @param[in] host Live private host; owns the adapter context and every facade.
 * @param[in] windows GUI-thread callback enumerating current nonclosing project windows.
 * @param[in] active GUI-thread callback resolving the active model window, or null.
 * @note Install once before plugin initialize(). Returned SDK pointers stay allocated until host
 * teardown and become inert after their concrete window closes; no window ownership transfers. */
void installExtensionModelAccess(GuiExtensionHost *host,
                                 std::function<QList<SWMMVisProjectWindow *>()> windows,
                                 std::function<SWMMVisProjectWindow *()> active);
}
