/* SPDX-License-Identifier: GPL-3.0-or-later */

/**
 * @file      twodbackendpolicy.h
 * @brief     Worker-side selection of compatible 2D execution backends.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include <QString>
#include <openswmm/engine/openswmm_engine.h>
#ifdef OPENSWMM_HAS_2D
#include <openswmm/engine/openswmm_2d.h>
#endif

namespace openswmmvis::simulation
{
// Apply only to the worker's opened handle. The editing model and checkpoint
// model identity retain their authored BACKEND. No process-global mutation.
/**
 * @brief Establish the GUI CPU default for AUTO on a worker-owned opened engine.
 * @param[in,out] engine Borrowed opened worker engine; editing/checkpoint authoring identities are not changed.
 * @param[out] notice Cleared on entry, then set to override/default explanation or failure diagnostic.
 * @return SWMM_OK, SWMM_ERR_BADPARAM for an unrecognized environment override, or the engine backend-policy error. Without 2D support returns SWMM_OK with empty notice.
 * @details Recognizes auto/cpu/omp/cuda/hip/sycl case-insensitively. The engine applies the environment override; this helper does not mutate process environment or global solver state.
 */
inline int applyTwoDBackendPolicy(SWMM_Engine engine, QString &notice)
{
    notice.clear();
#ifdef OPENSWMM_HAS_2D
    const auto override = qEnvironmentVariable("OPENSWMM_2D_BACKEND");
    if (!override.isEmpty()) {
        const auto mode = override.toLower();
        if (mode != "auto" && mode != "cpu" && mode != "omp" && mode != "cuda" && mode != "hip" &&
            mode != "sycl") {
            notice = QStringLiteral("Invalid OPENSWMM_2D_BACKEND value: %1").arg(override);
            return SWMM_ERR_BADPARAM;
        }
        notice =
            QStringLiteral(
                "2D backend environment override: %1 (overrides the GUI CPU default and model request).")
                .arg(override);
    } else {
        notice = QStringLiteral(
            "2D AUTO uses the built-in CPU marcher in the GUI; explicit model backend requests are retained.");
    }
    const int rc = swmm_2d_set_auto_backend_cpu(engine, 1);
    if (rc != SWMM_OK)
        notice = QStringLiteral("Cannot establish the GUI CPU backend default (error %1).").arg(rc);
    return rc;
#else
    Q_UNUSED(engine);
    return SWMM_OK;
#endif
}
} // namespace openswmmvis::simulation
