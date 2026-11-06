/**
 * @file extensiontimeseriesaccess.h
 * @brief Private native dataset controller backing the optional SDK import service.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
namespace openswmmvis
{
class GuiExtensionHost;
/** @brief Install guarded dataset import/query callbacks before plugin initialization.
 * @param[in] host Owner of jobs and service callbacks; model access must already be installed.
 * @note GUI thread only. Concrete dataset/layer classes stay outside the generic SDK host. */
void installExtensionTimeSeriesAccess(GuiExtensionHost *host);
}
