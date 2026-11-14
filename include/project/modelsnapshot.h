// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modelsnapshot.h
 * @brief     Write the open model, unsaved edits included, as an independent snapshot.
 * @details   Shared by extension edit scopes (`.swmmvis/edits`) and Check Model
 *            (`.swmmvis/check`, review G-09).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QString>

class SWMMVisProjectWindow;

namespace openswmmvis::project
{

/**
 * @brief Write the open model and its companions into a new snapshot folder.
 * @details Flushes staged data-object edits, writes through swmm_model_write_staged with the mesh
 *   inline, and publishes into `.swmmvis/<purpose>/<uuid>/` under the model's folder, so relative
 *   references (rainfall files, rasters, component files) resolve as they do for the model. An
 *   unsaved model uses `<app data>/unsaved/<unsavedKey>/` as the base instead. Nothing is written
 *   outside the new folder, and the model file itself is protected. GUI thread only.
 * @param[in] window Borrowed project window with an open model.
 * @param[in] purpose Folder name under `.swmmvis`, e.g. "edits" or "check".
 * @param[in] unsavedKey Folder name under `unsaved/` for a model that has no file yet.
 * @param[out] error Optional reason when the snapshot could not be written.
 * @return Absolute path of the snapshot's `model.inp`, or empty on failure. The caller owns the
 *   folder and removes it when done.
 */
QString writeModelSnapshot(SWMMVisProjectWindow *window, const QString &purpose,
                           const QString &unsavedKey, QString *error);

} // namespace openswmmvis::project
