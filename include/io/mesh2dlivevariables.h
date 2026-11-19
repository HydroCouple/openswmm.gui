/**
 * @file      mesh2dlivevariables.h
 * @brief     Worker-owned snapshots of live 2D simulation variables.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_IO_MESH2DLIVEVARIABLES_H
#define OPENSWMMVIS_IO_MESH2DLIVEVARIABLES_H

#include "io/mesh2dresultvariable.h"
#include <openswmm/engine/openswmm_engine.h>
#include <QMetaType>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

namespace openswmmvis::io
{

// A worker-owned value snapshot. Published only as shared_ptr<const>; it never
// contains engine handles or pointers into solver storage.
struct Mesh2DLiveVariable {
    Mesh2DResultVariable descriptor;
    std::vector<float> values;
    std::vector<Mesh2DValueStatus> status;
};
struct Mesh2DLiveVariables {
    int cellCount = 0;
    QVector<Mesh2DLiveVariable> variables;
    QStringList warnings;
    /**
     * @brief Measure values and status element payloads across the captured variables.
     * @return Bytes occupied by populated float/status elements, excluding capacity, metadata, warnings, and container overhead.
     */
    size_t payloadBytes() const
    {
        size_t bytes = 0;
        for (const auto &v : variables)
            bytes += v.values.size() * sizeof(float) + v.status.size() * sizeof(Mesh2DValueStatus);
        return bytes;
    }
};
using Mesh2DLiveVariablesPtr = std::shared_ptr<const Mesh2DLiveVariables>;

// Call only on the simulation worker while its engine is initialized. A null
// result with no error means inactive GW. Active GW always includes available
// table/base elevations, even without chemical species. Units come
// from public authoring metadata matched to actual runtime species identities.
// Unknown units remain explicit; raw values can still be inspected.
// Capture all optional mesh processes into one frame, so adding snow does not
// replace groundwater variables on the receiving results layer.
/**
 * @brief Combine optional groundwater and snow results in one immutable cell snapshot.
 * @param[in] engine Borrowed initialized engine; use only on its simulation worker.
 * @param[in] expectedCells Positive native cell count used to validate captured processes.
 * @param[out] error Optional diagnostic cleared by groundwater capture; may accompany a partial returned snapshot when a process fails.
 * @return Owning snapshot, including groundwater already captured when snow capture fails; null when no process is active or nothing can be captured.
 * @details Snow descriptors use held temporal values and metadata units. Nonfinite captured snow values are marked Missing. No cross-thread solver handles escape.
 */
Mesh2DLiveVariablesPtr captureSurfaceProcessVariables(SWMM_Engine engine, int expectedCells,
                                                      QString *error = nullptr);
/**
 * @brief Copy available groundwater variables into an immutable, owning cell snapshot.
 * @param[in] engine Borrowed initialized engine; call exclusively on its simulation worker.
 * @param[in] expectedCells Positive native surface cell count, required to match groundwater dimensions.
 * @param[out] error Optional diagnostic, cleared on entry and populated on invalid input or capture failure.
 * @return Snapshot without solver pointers, or null on inactive groundwater or failure; an empty error distinguishes inactivity.
 * @details Active groundwater includes available table/base elevations even without species. Units follow public authoring metadata matched by runtime species identity; unknown units are not inferred.
 */
Mesh2DLiveVariablesPtr captureGroundwaterVariables(SWMM_Engine engine, int expectedCells,
                                                   QString *error = nullptr);
}
Q_DECLARE_METATYPE(openswmmvis::io::Mesh2DLiveVariablesPtr)
#endif
